// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AAudio backend on the device or emulator it runs on: the default
// output and input, and the default output alone in a context of one
// device; an output stream's blocks, rate, clock, stopping and
// starting, and its converted and refused rates; xruns; exclusive
// streams; and capture as far as the device lets the shell record. An
// emulator plays without a host audio connection but refuses the
// shell's recording, so capture is tested to its refusal there; with
// MAUD_REQUIRE_CAPTURE set it must run. Where AAudio cannot open a
// stream at all the test skips, unless MAUD_REQUIRE_AAUDIO is set.

// The emulator's virtual output, starved of the host's time, builds a
// backlog of up to a few seconds that stays once built: a latency that
// long is true there, and the clock still steady.
#define TEST_CLOCK_MAX_LATENCY 5000000000
#include "test_clock.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <android/api-level.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/system_properties.h>
#include <time.h>

// The exit code CTest reads as skipped.
#define SKIP 77

typedef struct Blocks
{
    atomic_uint count;
    atomic_uint wrongSize;
    atomic_uint onControl;
    uint32_t periodFrames;
    // The block at which the callback stalls for 300 ms, or 0.
    uint32_t stallAt;
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
    if (block->frameCount != blocks->periodFrames)
    {
        atomic_fetch_add(&blocks->wrongSize, 1);
    }
    if (block->output != nullptr)
    {
        memset(block->output, 0, (size_t)block->frameCount * 2 * sizeof(float));
    }
    if (blocks->stallAt != 0 && atomic_load(&blocks->count) == blocks->stallAt)
    {
        Sleep(300);
    }
    atomic_fetch_add(&blocks->count, 1);
}

static void Drain(maudContext* context)
{
    maudNotification ignored;
    while (maudNextNotification(context, &ignored) == maud_success)
    {
    }
}

static bool WaitForBlocks(maudContext* context, const Blocks* blocks, uint32_t count)
{
    for (int tries = 0; tries < 500 && atomic_load(&blocks->count) < count; ++tries)
    {
        Drain(context);
        Sleep(10);
    }
    return atomic_load(&blocks->count) >= count;
}

// The stream's rate: the best of three windows of three seconds, since
// a loaded machine's stalls only lower a window's count. AAudio moves
// frames in bursts (882 on the emulator, 20 ms): a window this long
// holds one burst more or less within 0.7%.
static double MeasureRate(const maudContext* context, maudStreamId stream)
{
    double best = 0.0;
    for (int window = 0; window < 3; ++window)
    {
        uint64_t first = 0;
        uint64_t last = 0;
        double start = Now();
        CHECK(maudGetStreamPosition(context, stream, &first) == maud_success, "position");
        Sleep(3000);
        CHECK(maudGetStreamPosition(context, stream, &last) == maud_success, "position");
        double rate = (double)(last - first) / (Now() - start);
        best = rate > best ? rate : best;
    }
    return best;
}

// At most 2% above the expected rate and 6% below; printed when not.
static bool Near(double rate, double expected)
{
    bool within = rate > expected * 0.94 && rate < expected * 1.02;
    if (!within)
    {
        fprintf(stderr, "measured %.0f frames/s, expected %.0f\n", rate, expected);
    }
    return within;
}

static bool Destroy(maudContext* context, maudStreamId stream)
{
    maudResult result = maudDestroyStream(context, stream);
    if (result != maud_success)
    {
        fprintf(stderr, "maudDestroyStream: %s\n", maudResultName(result));
    }
    return result == maud_success;
}

static maudStreamId OpenStream(maudContext* context, maudStreamDef* def, Blocks* blocks)
{
    def->periodFrames = 256;
    def->callback = CountBlocks;
    def->user = blocks;
    blocks->periodFrames = 256;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, def, &stream) == maud_success, "create");
    return stream;
}

// The one device of a direction.
static maudDeviceId OnlyDevice(const maudContext* context, maudDirection direction)
{
    maudDeviceId ids[4];
    uint32_t count = 0;
    CHECK(maudGetDevices(context, direction, ids, 4, &count) == maud_success && count == 1,
          "one device each way");
    return count >= 1 ? ids[0] : (maudDeviceId){0, 0};
}

static void TestDevices(maudContext* context)
{
    for (maudDirection direction = maud_directionOutput; direction <= maud_directionInput;
         ++direction)
    {
        maudDeviceId device = OnlyDevice(context, direction);
        maudDeviceInfo info = {0};
        char name[64] = {0};
        char key[64] = {0};
        size_t length = 0;
        CHECK(maudGetDeviceInfo(context, device, &info) == maud_success &&
                  maudGetDeviceName(context, device, name, sizeof(name) - 1, &length) ==
                      maud_success &&
                  maudGetDeviceKey(context, device, key, sizeof(key) - 1, &length) == maud_success,
              "described");
        printf("%s: %s [%s] %u Hz, layout %u\n",
               direction == maud_directionOutput ? "output" : "input", name, key,
               info.nativeSampleRate, (unsigned)info.nativeLayout);
        CHECK(strcmp(key, "default") == 0 && info.form == maud_formUnknown,
              "the platform's default, of no known form");
        CHECK(info.nativeSampleRate >= 8000 && info.minSampleRate == info.nativeSampleRate &&
                  info.maxSampleRate == info.nativeSampleRate,
              "one rate");
        CHECK(direction == maud_directionOutput || info.nativeLayout == maud_layoutMono,
              "a mono microphone");
        // Without the Java half, only an Android older than 12L can say:
        // it has no Spatializer.
        maudPlatformSpatializer expected =
            direction == maud_directionOutput && android_get_device_api_level() < 32
                ? maud_spatializerNone
                : maud_spatializerUnknown;
        CHECK(info.spatializer == expected && !info.headTracking && info.spatialObjects == 0,
              "the Spatializer's state where Android says it");
        for (maudDeviceRole role = maud_roleGeneral; role <= maud_roleCommunications; ++role)
        {
            maudDeviceId current = {0, 0};
            CHECK(maudGetDefaultDevice(context, direction, role, &current) == maud_success &&
                      current.index1 == device.index1,
                  "the default for both roles");
        }
    }
}

static void TestOutputStream(maudContext* context)
{
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, OnlyDevice(context, maud_directionOutput), &info) ==
              maud_success,
          "the output");
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenStream(context, &def, &blocks);
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success &&
              format.sampleRate == info.nativeSampleRate,
          "native at the device's rate");
    Sleep(100);
    CHECK(atomic_load(&blocks.count) == 0, "nothing before start");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 20), "blocks arrive");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(atomic_load(&blocks.onControl) == 0, "on AAudio's thread");
    CHECK(Near(MeasureRate(context, stream), (double)info.nativeSampleRate), "at its rate");
    CHECK(StreamClockIsSound(context, stream, true, true, Sleep),
          "its clock maps frames to host time");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    uint32_t stopped = atomic_load(&blocks.count);
    uint64_t before = 0;
    uint64_t after = 0;
    CHECK(maudGetStreamPosition(context, stream, &before) == maud_success, "position");
    Sleep(200);
    CHECK(maudGetStreamPosition(context, stream, &after) == maud_success, "position");
    CHECK(atomic_load(&blocks.count) == stopped, "no callbacks once stopped");
    CHECK(after == before, "the device stops with the stream");
    CHECK(maudStartStream(context, stream) == maud_success, "start again");
    CHECK(WaitForBlocks(context, &blocks, stopped + 20), "it runs again");
    CHECK(StreamClockIsSound(context, stream, true, true, Sleep), "its clock after a restart");
    CHECK(Destroy(context, stream), "destroy while running");
    // A stream marked as already spatialized: Android before API 32 has
    // no spatializer; after, the open stream carries the mark.
    Blocks marked = {0};
    def = maudDefaultStreamDef();
    def.contentSpatialized = true;
    stream = OpenStream(context, &def, &marked);
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success &&
              status.spatialMark == maud_markHonored,
          "a marked stream, honored");
    CHECK(maudStartStream(context, stream) == maud_success && WaitForBlocks(context, &marked, 10),
          "and it plays");
    CHECK(Destroy(context, stream), "destroy");
    double other = info.nativeSampleRate == 44100 ? 48000.0 : 44100.0;
    def = maudDefaultStreamDef();
    def.ratePolicy = maud_rateRequired;
    def.sampleRate = (uint32_t)other;
    def.callback = CountBlocks;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "a required rate the device does not run at");
    Blocks converted = {0};
    def.ratePolicy = maud_ratePlatformConverted;
    stream = OpenStream(context, &def, &converted);
    CHECK(maudStartStream(context, stream) == maud_success, "start converted");
    CHECK(WaitForBlocks(context, &converted, 10), "it runs");
    CHECK(Near(MeasureRate(context, stream), other), "at its own rate");
    CHECK(Destroy(context, stream), "destroy");
    def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.mode = maud_modePull;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no pull mode");
}

// A callback that stalls 300 ms leaves AAudio's buffer empty: an
// underrun, counted once AAudio reports it.
static void TestXruns(maudContext* context)
{
    Blocks blocks = {.stallAt = 30};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenStream(context, &def, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 80), "past the stall");
    maudStreamStatus status = {0};
    for (int tries = 0; tries < 100 && status.underruns == 0; ++tries)
    {
        Sleep(10);
        CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
    }
    printf("xruns after a stall: %llu under, %llu over\n", (unsigned long long)status.underruns,
           (unsigned long long)status.overruns);
    CHECK(status.underruns >= 1 && status.overruns == 0, "the stall counted as an underrun");
    CHECK(status.underruns < atomic_load(&blocks.count) / 2, "underruns counted, not callbacks");
    CHECK(Destroy(context, stream), "destroy");
}

// Whether the test runs in the emulator, whose AAudio has no MMAP path
// and so never grants a device to one stream alone.
static bool Emulated(void)
{
    char value[PROP_VALUE_MAX] = {0};
    return __system_property_get("ro.kernel.qemu", value) > 0 && value[0] == '1';
}

// An exclusive stream runs where AAudio grants the device to it alone
// and is refused where AAudio could only share it: always on the
// emulator.
static void TestExclusive(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.share = maud_shareExclusive;
    def.device = OnlyDevice(context, maud_directionOutput);
    def.periodFrames = 256;
    def.callback = CountBlocks;
    def.user = &blocks;
    blocks.periodFrames = 256;
    maudStreamId stream = {0, 0};
    maudResult result = maudCreateStream(context, &def, &stream);
    printf("exclusive: %s\n", maudResultName(result));
    CHECK(result == maud_success || result == maud_errorUnsupported, "granted or refused");
    CHECK(!Emulated() || result == maud_errorUnsupported, "refused where AAudio only shares");
    if (result == maud_success)
    {
        maudStreamStatus status = {0};
        CHECK(maudGetStreamStatus(context, stream, &status) == maud_success && status.exclusive,
              "it says so");
        CHECK(maudStartStream(context, stream) == maud_success &&
                  WaitForBlocks(context, &blocks, 20),
              "it runs");
        CHECK(Destroy(context, stream), "destroy");
    }
    def.ratePolicy = maud_ratePlatformConverted;
    def.sampleRate = 44100;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "never at a converted rate");
}

// An input stream opens and, asked for voice processing, reports what
// it asked; started, it captures at its rate where the shell may
// record. An emulator refuses: the stream then stays silent.
static void TestCapture(maudContext* context)
{
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, OnlyDevice(context, maud_directionInput), &info) ==
              maud_success,
          "the input");
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    def.voice = maud_voiceEchoCancellation | maud_voiceNoiseSuppression;
    def.periodFrames = 256;
    def.callback = CountBlocks;
    def.user = &blocks;
    blocks.periodFrames = 256;
    maudStreamId stream = {0, 0};
    maudResult result = maudCreateStream(context, &def, &stream);
    bool required = getenv("MAUD_REQUIRE_CAPTURE") != nullptr;
    printf("capture: %s\n", maudResultName(result));
    CHECK(result == maud_success || (!required && result == maud_errorPlatform),
          "an input stream, or the platform's refusal");
    if (result != maud_success)
    {
        return;
    }
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success && status.voiceReported &&
              status.voiceActive == def.voice,
          "the voice processing asked for, reported");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    bool captured = WaitForBlocks(context, &blocks, 20);
    printf("capture: %s\n", captured ? "runs" : "silent");
    CHECK(captured || !required, "it captures");
    if (captured)
    {
        CHECK(atomic_load(&blocks.wrongSize) == 0 && atomic_load(&blocks.onControl) == 0,
              "in whole periods, on AAudio's thread");
        CHECK(Near(MeasureRate(context, stream), (double)info.nativeSampleRate), "at its rate");
    }
    CHECK(Destroy(context, stream), "destroy");
}

// A context of one device lists the default output alone.
static void TestDeviceLimit(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.limits.devices = 1;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "a context of one device");
    if (context == nullptr)
    {
        return;
    }
    maudDeviceId ids[4];
    uint32_t outputs = 0;
    uint32_t inputs = 0;
    CHECK(maudGetDevices(context, maud_directionOutput, ids, 4, &outputs) == maud_success &&
              maudGetDevices(context, maud_directionInput, ids, 4, &inputs) == maud_success &&
              outputs == 1 && inputs == 0,
          "the default output alone");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

int main(void)
{
    s_control = pthread_self();
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    maudResult result = maudCreateContext(&def, &context);
    if (result == maud_errorUnsupported && getenv("MAUD_REQUIRE_AAUDIO") == nullptr)
    {
        printf("AAudio opens no stream here: skipped\n");
        return SKIP;
    }
    CHECK(result == maud_success, "a native context");
    if (context == nullptr)
    {
        return 1;
    }
    CHECK(maudGetContextBackend(context) == maud_backendAaudio, "AAudio chosen");
    TestDevices(context);
    TestOutputStream(context);
    TestXruns(context);
    TestExclusive(context);
    TestCapture(context);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    TestDeviceLimit();
    return s_failures == 0 ? 0 : 1;
}
