// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend in the simulator: the default output and input; an
// output stream's blocks, rate and clock, stopping and starting, its
// converted and refused rates; the session's category following what
// runs and the host's focus; capture as far as the simulator lets
// a spawned process record.

#include "test_clock.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/focus.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#import <AVFAudio/AVFAudio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

typedef struct Blocks
{
    atomic_uint count;
    atomic_uint wrongSize;
    atomic_uint onControl;
} Blocks;

static pthread_t s_control;

static void Sleep(int milliseconds)
{
    struct timespec delay = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&delay, nullptr);
}

static double Now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

static void CountBlocks(const maudStreamBlock* block, void* user)
{
    Blocks* blocks = user;
    if (pthread_equal(pthread_self(), s_control))
    {
        atomic_fetch_add(&blocks->onControl, 1);
    }
    if (block->frameCount != 256)
    {
        atomic_fetch_add(&blocks->wrongSize, 1);
    }
    if (block->output != nullptr)
    {
        memset(block->output, 0, (size_t)block->frameCount * 2 * sizeof(float));
    }
    atomic_fetch_add(&blocks->count, 1);
}

static bool WaitForBlocks(maudContext* context, const Blocks* blocks, uint32_t count)
{
    for (int tries = 0; tries < 500 && atomic_load(&blocks->count) < count; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        Sleep(10);
    }
    return atomic_load(&blocks->count) >= count;
}

// The best of three windows of two seconds.
static double MeasureRate(const maudContext* context, maudStreamId stream)
{
    double best = 0.0;
    for (int window = 0; window < 3; ++window)
    {
        uint64_t first = 0;
        uint64_t last = 0;
        double start = Now();
        CHECK(maudGetStreamPosition(context, stream, &first) == maud_success, "position");
        Sleep(2000);
        CHECK(maudGetStreamPosition(context, stream, &last) == maud_success, "position");
        double rate = (double)(last - first) / (Now() - start);
        best = rate > best ? rate : best;
    }
    return best;
}

static bool Near(double rate, double expected)
{
    bool within = rate > expected * 0.94 && rate < expected * 1.02;
    if (!within)
    {
        fprintf(stderr, "measured %.0f frames/s, expected %.0f\n", rate, expected);
    }
    return within;
}

static maudStreamId Open(maudContext* context, maudStreamDef* def, Blocks* blocks)
{
    def->periodFrames = 256;
    def->callback = CountBlocks;
    def->user = blocks;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, def, &stream) == maud_success, "create");
    return stream;
}

// Whether the session's category is category and it mixes with others
// as said.
static bool Category(AVAudioSessionCategory category, bool mixes)
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    bool mixing = (session.categoryOptions & AVAudioSessionCategoryOptionMixWithOthers) != 0;
    bool same = [session.category isEqualToString:category] && mixing == mixes;
    if (!same)
    {
        fprintf(stderr, "category %s, options %lu\n", session.category.UTF8String,
                (unsigned long)session.categoryOptions);
    }
    return same;
}

static void TestDevices(maudContext* context, maudDeviceInfo* output)
{
    for (maudDirection direction = maud_directionOutput; direction <= maud_directionInput;
         ++direction)
    {
        maudDeviceId ids[4];
        uint32_t count = 0;
        CHECK(maudGetDevices(context, direction, ids, 4, &count) == maud_success && count == 1,
              "one device each way");
        maudDeviceInfo info = {0};
        char key[32] = {0};
        size_t length = 0;
        CHECK(maudGetDeviceInfo(context, ids[0], &info) == maud_success &&
                  maudGetDeviceKey(context, ids[0], key, sizeof(key) - 1, &length) ==
                      maud_success &&
                  strcmp(key, "default") == 0 && info.nativeSampleRate >= 8000,
              "the session's default");
        printf("%s: %u Hz, layout %u\n", direction == maud_directionOutput ? "output" : "input",
               info.nativeSampleRate, (unsigned)info.nativeLayout);
        if (direction == maud_directionOutput)
        {
            *output = info;
        }
    }
}

static void TestOutput(maudContext* context, const maudDeviceInfo* info)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = Open(context, &def, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 20), "blocks arrive");
    CHECK(atomic_load(&blocks.wrongSize) == 0 && atomic_load(&blocks.onControl) == 0,
          "in whole periods, on the IO thread");
    CHECK(Category(AVAudioSessionCategoryPlayback, true), "Playback, mixing with others");
    CHECK(Near(MeasureRate(context, stream), (double)info->nativeSampleRate), "at its rate");
    CHECK(StreamClockIsSound(context, stream, true, true, Sleep), "its clock");
    CHECK(maudRequestFocus(context, maud_focusLasting, maud_roleGeneral) == maud_success,
          "focus asked for");
    CHECK(Category(AVAudioSessionCategoryPlayback, false), "then not mixing");
    maudFocus focus = maud_focusNone;
    CHECK(maudGetContextFocus(context, &focus) == maud_success && focus == maud_focusHeld, "held");
    CHECK(maudRequestFocus(context, maud_focusRelease, maud_roleGeneral) == maud_success,
          "released");
    CHECK(Category(AVAudioSessionCategoryPlayback, true), "mixing again");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    uint32_t stopped = atomic_load(&blocks.count);
    Sleep(200);
    CHECK(atomic_load(&blocks.count) == stopped, "no callbacks once stopped");
    CHECK(maudStartStream(context, stream) == maud_success &&
              WaitForBlocks(context, &blocks, stopped + 20),
          "it runs again");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy while running");
    double other = info->nativeSampleRate == 44100 ? 48000.0 : 44100.0;
    def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.ratePolicy = maud_rateRequired;
    def.sampleRate = (uint32_t)other;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "a required rate the session does not run at");
    Blocks converted = {0};
    def.ratePolicy = maud_ratePlatformConverted;
    stream = Open(context, &def, &converted);
    CHECK(maudStartStream(context, stream) == maud_success &&
              WaitForBlocks(context, &converted, 10),
          "a converted stream runs");
    CHECK(Near(MeasureRate(context, stream), other), "at its own rate");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.mode = maud_modePull;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no pull mode");
}

static void TestCapture(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    def.periodFrames = 256;
    def.callback = CountBlocks;
    def.user = &blocks;
    maudStreamId stream = {0, 0};
    maudResult result = maudCreateStream(context, &def, &stream);
    printf("capture: %s\n", maudResultName(result));
    CHECK(result == maud_success || result == maud_errorPlatform, "an input, or a refusal");
    if (result != maud_success)
    {
        return;
    }
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    bool captured = WaitForBlocks(context, &blocks, 20);
    printf("capture: %s\n", captured ? "runs" : "silent");
    if (captured)
    {
        CHECK(Category(AVAudioSessionCategoryRecord, true), "Record, mixing with others");
    }
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

int main(void)
{
    s_control = pthread_self();
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "a native context");
    if (context == nullptr)
    {
        return 1;
    }
    CHECK(maudGetContextBackend(context) == maud_backendCoreAudio, "CoreAudio chosen");
    maudDeviceInfo output = {0};
    TestDevices(context, &output);
    TestOutput(context, &output);
    TestCapture(context);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
