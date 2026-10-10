// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend in the simulator: the default output and input; an
// output stream's blocks, rate and clock, stopping and starting, its
// converted and refused rates; the session's category following what
// runs and the host's focus; interruptions, posted as the session
// posts them, holding the streams and ending with or without the hint
// to resume; a route change giving the default output its form; capture
// as far as the simulator lets a spawned process record.

#include "ios_session.h"
#include "test_clock.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/focus.h"
#include "maul-audio/notification.h"
#include "maul-audio/objects.h"
#include "maul-audio/stream.h"

#import <AVFAudio/AVFAudio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
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

static void TestDevices(maudContext* context, maudDeviceInfo* outputInfo)
{
    for (maudDirection direction = maud_directionOutput; direction <= maud_directionInput;
         ++direction)
    {
        // One output, as iOS lets none be chosen; the inputs are the
        // default and the session's available inputs.
        maudDeviceId ids[8];
        uint32_t count = 0;
        bool output = direction == maud_directionOutput;
        CHECK(maudGetDevices(context, direction, ids, 8, &count) == maud_success &&
                  (output ? count == 1 : count >= 1),
              "one output, and inputs");
        maudDeviceId current = {0, 0};
        maudDeviceInfo info = {0};
        char key[32] = {0};
        size_t length = 0;
        CHECK(
            maudGetDefaultDevice(context, direction, maud_roleGeneral, &current) == maud_success &&
                maudGetDeviceInfo(context, current, &info) == maud_success &&
                maudGetDeviceKey(context, current, key, sizeof(key) - 1, &length) == maud_success &&
                strcmp(key, "default") == 0 && info.nativeSampleRate >= 8000,
            "the session's default");
        printf("%s: %u Hz, layout %u, %u devices\n", output ? "output" : "input",
               info.nativeSampleRate, (unsigned)info.nativeLayout, count);
        if (output)
        {
            *outputInfo = info;
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
    // The unit itself stops: its position, which every render moves,
    // stands still.
    uint64_t before = 0;
    uint64_t after = 0;
    CHECK(maudGetStreamPosition(context, stream, &before) == maud_success, "position");
    Sleep(200);
    CHECK(maudGetStreamPosition(context, stream, &after) == maud_success && after == before,
          "the unit stops with the stream");
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

// Posts an interruption as the session does: began, or ended with or
// without the hint to resume.
static void PostInterruption(bool began, bool resume)
{
    NSMutableDictionary* info = [NSMutableDictionary dictionary];
    info[AVAudioSessionInterruptionTypeKey] =
        @(began ? AVAudioSessionInterruptionTypeBegan : AVAudioSessionInterruptionTypeEnded);
    if (!began)
    {
        info[AVAudioSessionInterruptionOptionKey] =
            @(resume ? AVAudioSessionInterruptionOptionShouldResume : 0);
    }
    [[NSNotificationCenter defaultCenter]
        postNotificationName:AVAudioSessionInterruptionNotification
                      object:[AVAudioSession sharedInstance]
                    userInfo:info];
}

static maudSuspendReason Suspension(const maudContext* context, maudStreamId stream)
{
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
    return status.suspension;
}

// Drains, then reads the context's focus.
static maudFocus FocusAfterDrain(maudContext* context)
{
    maudNotification ignored;
    while (maudNextNotification(context, &ignored) == maud_success)
    {
    }
    maudFocus focus = maud_focusNone;
    CHECK(maudGetContextFocus(context, &focus) == maud_success, "focus");
    return focus;
}

static void TestInterruptions(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = Open(context, &def, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success && WaitForBlocks(context, &blocks, 10),
          "a stream runs");
    PostInterruption(true, false);
    CHECK(FocusAfterDrain(context) == maud_focusPaused, "an interruption pauses focus");
    CHECK(Suspension(context, stream) == maud_suspendPolicy, "and holds the stream");
    PostInterruption(false, true);
    CHECK(FocusAfterDrain(context) == maud_focusNone, "ended with the hint, no focus asked");
    CHECK(Suspension(context, stream) == maud_suspendNone, "the stream runs again");
    uint32_t count = atomic_load(&blocks.count);
    CHECK(WaitForBlocks(context, &blocks, count + 10), "and plays");
    PostInterruption(true, false);
    PostInterruption(false, false);
    CHECK(FocusAfterDrain(context) == maud_focusPaused &&
              Suspension(context, stream) == maud_suspendPolicy,
          "ended without the hint, it stays held");
    CHECK(maudResumeContext(context) == maud_success &&
              FocusAfterDrain(context) == maud_focusNone &&
              Suspension(context, stream) == maud_suspendNone,
          "until the host resumes it");
    PostInterruption(true, false);
    CHECK(FocusAfterDrain(context) == maud_focusPaused, "interrupted again");
    CHECK(maudRequestFocus(context, maud_focusLasting, maud_roleGeneral) == maud_success &&
              FocusAfterDrain(context) == maud_focusHeld &&
              Suspension(context, stream) == maud_suspendNone,
          "or asks for focus");
    CHECK(maudRequestFocus(context, maud_focusRelease, maud_roleGeneral) == maud_success,
          "released");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroyed");
}

// The form the session's route leads to, as the backend reads it.
static maudDeviceForm RouteForm(void)
{
    AVAudioSessionPortDescription* port =
        [AVAudioSession sharedInstance].currentRoute.outputs.firstObject;
    if ([port.portType isEqualToString:AVAudioSessionPortBuiltInSpeaker])
    {
        return maud_formSpeakers;
    }
    if ([port.portType isEqualToString:AVAudioSessionPortHeadphones])
    {
        return maud_formHeadphones;
    }
    return maud_formUnknown;
}

static void TestRoute(maudContext* context)
{
    [[NSNotificationCenter defaultCenter] postNotificationName:AVAudioSessionRouteChangeNotification
                                                        object:[AVAudioSession sharedInstance]
                                                      userInfo:@{}];
    FocusAfterDrain(context);
    maudDeviceId output = {0, 0};
    maudDeviceInfo info = {0};
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &output) ==
                  maud_success &&
              maudGetDeviceInfo(context, output, &info) == maud_success,
          "the default output");
    printf("route: %s, form %u\n",
           [AVAudioSession sharedInstance].currentRoute.outputs.firstObject.portType.UTF8String,
           (unsigned)info.form);
    CHECK(info.form == RouteForm(), "leads where the session's route does");
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
        CHECK(Category(AVAudioSessionCategoryRecord, false),
              "Record, which takes no mixing option");
    }
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

typedef struct Placed
{
    atomic_uint count;
    atomic_uint offered;
} Placed;

// An object to the right playing a tone; the bed stays silent.
static void PlaceObject(const maudStreamBlock* block, void* user)
{
    Placed* placed = user;
    maudStreamObject* object = &block->objects[0];
    object->active = true;
    object->position[0] = 2.0f;
    for (uint32_t i = 0; i < block->frameCount; ++i)
    {
        object->samples[i] = (i & 32u) != 0 ? 0.25f : -0.25f;
    }
    atomic_store(&placed->offered, block->objectsAvailable);
    atomic_fetch_add(&placed->count, 1);
}

// An object stream renders through the system's spatial mixer in front
// of RemoteIO, which takes every object; outputs say so. A bed wider
// than stereo is refused.
static void TestObjects(maudContext* context)
{
    maudDeviceId output = {0, 0};
    maudDeviceInfo info = {0};
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &output) ==
                  maud_success &&
              maudGetDeviceInfo(context, output, &info) == maud_success &&
              info.spatializer == maud_spatializerOn &&
              info.spatialObjects == MAUD_MAX_STREAM_OBJECTS,
          "the output takes objects");
    Placed placed = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.objectCount = 2;
    def.callback = PlaceObject;
    def.user = &placed;
    maudStreamId stream = {0, 0};
    maudResult result = maudCreateStream(context, &def, &stream);
    printf("object stream: %s\n", maudResultName(result));
    CHECK(result == maud_success && maudStartStream(context, stream) == maud_success,
          "an object stream runs");
    for (int tries = 0; tries < 500 && atomic_load(&placed.count) < 20; ++tries)
    {
        Sleep(10);
    }
    CHECK(atomic_load(&placed.count) >= 20 && atomic_load(&placed.offered) == 2,
          "its callback runs, offered every object");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    def.layout = maud_layoutQuad;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "a bed wider than stereo");
}

// The context's allocations not yet given back.
static int s_live;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    void* memory = nullptr;
    if (posix_memalign(&memory, alignment < sizeof(void*) ? sizeof(void*) : alignment, size) != 0)
    {
        return nullptr;
    }
    s_live++;
    return memory;
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    s_live--;
    free(memory);
}

// The form of every port type the session names: the simulator routes
// to its speaker and microphone only.
static void TestPortForms(void)
{
    const struct
    {
        NSString* type;
        maudDeviceForm form;
    } ports[] = {
        {AVAudioSessionPortBuiltInSpeaker, maud_formSpeakers},
        {AVAudioSessionPortBuiltInReceiver, maud_formHandset},
        {AVAudioSessionPortHeadphones, maud_formHeadphones},
        {AVAudioSessionPortBluetoothA2DP, maud_formHeadphones},
        {AVAudioSessionPortHeadsetMic, maud_formHeadset},
        {AVAudioSessionPortBluetoothHFP, maud_formHeadset},
        {AVAudioSessionPortBuiltInMic, maud_formMicrophone},
        {AVAudioSessionPortLineOut, maud_formLine},
        {AVAudioSessionPortLineIn, maud_formLine},
        {AVAudioSessionPortHDMI, maud_formDigital},
        {AVAudioSessionPortAirPlay, maud_formUnknown},
        {AVAudioSessionPortUSBAudio, maud_formUnknown},
    };
    for (size_t i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i)
    {
        CHECK(maudIosPortForm(ports[i].type) == ports[i].form, ports[i].type.UTF8String);
    }
}

// A context of one device lists the output alone; of two, the output
// and the default input, without the session's inputs.
static void TestDeviceLimit(void)
{
    for (uint16_t limit = 1; limit <= 2; ++limit)
    {
        maudContextDef def = maudDefaultContextDef();
        def.limits.devices = limit;
        maudContext* context = nullptr;
        CHECK(maudCreateContext(&def, &context) == maud_success, "a small context");
        if (context == nullptr)
        {
            return;
        }
        maudDeviceId ids[4];
        uint32_t outputs = 0;
        uint32_t inputs = 0;
        CHECK(maudGetDevices(context, maud_directionOutput, ids, 4, &outputs) == maud_success &&
                  maudGetDevices(context, maud_directionInput, ids, 4, &inputs) == maud_success,
              "its devices");
        CHECK(outputs == 1 && inputs == limit - 1u, "within the limit");
        CHECK(maudDestroyContext(context) == maud_success, "destroyed");
    }
}

int main(void)
{
    s_control = pthread_self();
    TestPortForms();
    TestDeviceLimit();
    maudContextDef def = maudDefaultContextDef();
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
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
    TestObjects(context);
    TestInterruptions(context);
    TestRoute(context);
    TestCapture(context);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    CHECK(s_live == 0, "everything given back");
    return s_failures == 0 ? 0 : 1;
}
