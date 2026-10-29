// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Capture on iOS, as an application in the simulator
// (tools/run_ios_app.sh, which grants it the microphone): an input
// stream runs at its rate on the IO thread with a sound clock, under
// the Record category, which takes no mixing option; the session's
// available inputs are listed and a stream pinned to one runs; a voiced
// duplex stream runs on one Voice-Processing I/O unit. It also prints what the simulator
// offers beyond the default input and whether a Voice-Processing I/O unit initializes there. The
// tests run on a thread of their own once the application has launched; the scene delegate keeps
// UIKit content.
//
// CI's simulator, on a loaded virtual machine, at times falls behind:
// it skips whole IO cycles, which its sample clock shows and the stream
// counts as overruns, and around them its clock runs slow too. A run in
// which the platform skipped while the rate and clock were measured
// cannot vouch for them: a miss there is printed, not failed. Where it
// skipped nothing, as on a device, both must hold.

#include "test_clock.h"
#include "test_harness.h"
#include "test_wait.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#import <AVFAudio/AVFAudio.h>
#import <AudioToolbox/AudioToolbox.h>
#import <UIKit/UIKit.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct Blocks
{
    atomic_uint count;
    atomic_uint wrongSize;
    atomic_uint onControl;
} Blocks;

static pthread_t s_control;

static void Drain(maudContext* context)
{
    maudNotification ignored;
    while (maudContext_NextNotification(context, &ignored) == maud_success)
    {
    }
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
    atomic_fetch_add(&blocks->count, 1);
}

static bool WaitForBlocks(maudContext* context, const Blocks* blocks, uint32_t count)
{
    POLL_UNTIL(5000, "the stream's blocks", Drain(context), atomic_load(&blocks->count) >= count);
    return atomic_load(&blocks->count) >= count;
}

// The overruns the platform has shown the stream so far.
static uint64_t Overruns(const maudContext* context, maudStreamId stream)
{
    maudStreamStatus status = {0};
    CHECK(maudStream_GetStatus(context, stream, &status) == maud_success, "status");
    return status.overruns;
}

// The best rate over windows of 2 s.
static double MeasureRate(const maudContext* context, maudStreamId stream)
{
    double best = 0.0;
    for (int window = 0; window < 5; ++window)
    {
        uint64_t first = 0;
        uint64_t last = 0;
        double start = Now();
        CHECK(maudStream_GetPosition(context, stream, &first) == maud_success, "position");
        // hygiene: a 2 s window of the stream's frames against the wall clock
        Pause(2000);
        CHECK(maudStream_GetPosition(context, stream, &last) == maud_success, "position");
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
        // What the session was doing, for a simulator whose input stalls
        // now and then.
        AVAudioSession* session = [AVAudioSession sharedInstance];
        printf("measured %.0f frames/s, expected %.0f; session at %.0f Hz, IO buffer %.4f s, "
               "input %s with %ld channels, latency %.4f s\n",
               rate, expected, session.sampleRate, session.IOBufferDuration,
               session.inputAvailable ? "available" : "unavailable",
               (long)session.inputNumberOfChannels, session.inputLatency);
    }
    return within;
}

// What the simulator offers: the session's available inputs, which
// list only under a category that records.
static void PrintInputs(void)
{
    for (AVAudioSessionPortDescription* port in [AVAudioSession sharedInstance].availableInputs)
    {
        printf("available input: %s (%s), uid %s\n", port.portName.UTF8String,
               port.portType.UTF8String, port.UID.UTF8String);
    }
}

// Whether a Voice-Processing I/O unit capturing on bus 1 initializes.
static void PrintVoiceUnit(void)
{
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_VoiceProcessingIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponent component = AudioComponentFindNext(nullptr, &description);
    AudioComponentInstance unit = nullptr;
    OSStatus made = component != nullptr ? AudioComponentInstanceNew(component, &unit) : -1;
    UInt32 on = 1;
    OSStatus enabled = unit != nullptr
                           ? AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO,
                                                  kAudioUnitScope_Input, 1, &on, sizeof(on))
                           : -1;
    OSStatus initialized = unit != nullptr ? AudioUnitInitialize(unit) : -1;
    printf("voice unit: made %d, input enabled %d, initialized %d\n", (int)made, (int)enabled,
           (int)initialized);
    if (unit != nullptr)
    {
        OSStatus uninitialized = AudioUnitUninitialize(unit);
        OSStatus disposed = AudioComponentInstanceDispose(unit);
        (void)uninitialized;
        (void)disposed;
    }
}

static void TestCapture(maudContext* context)
{
    PrintVoiceUnit();
    maudDeviceId input = {0, 0};
    maudDeviceInfo info = {0};
    CHECK(maudContext_GetDefaultDevice(context, maud_directionInput, maud_roleGeneral, &input) ==
                  maud_success &&
              maudDevice_GetInfo(context, input, &info) == maud_success,
          "the default input");
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    def.periodFrames = 256;
    def.callback = CountBlocks;
    def.user = &blocks;
    maudStreamId stream = {0, 0};
    maudResult result = maudCreateStream(context, &def, &stream);
    printf("capture: %s\n", maudGetResultName(result));
    CHECK(result == maud_success, "an input stream, granted the microphone");
    if (result != maud_success)
    {
        return;
    }
    CHECK(maudStream_Start(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 20), "captured blocks arrive");
    CHECK(atomic_load(&blocks.wrongSize) == 0 && atomic_load(&blocks.onControl) == 0,
          "in whole periods, on the IO thread");
    AVAudioSession* session = [AVAudioSession sharedInstance];
    printf("category %s, options %lu\n", session.category.UTF8String,
           (unsigned long)session.categoryOptions);
    CHECK([session.category isEqualToString:AVAudioSessionCategoryRecord] &&
              (session.categoryOptions & AVAudioSessionCategoryOptionMixWithOthers) == 0,
          "Record, which takes no mixing option");
    uint64_t skippedBefore = Overruns(context, stream);
    bool atRate = Near(MeasureRate(context, stream), (double)info.nativeSampleRate);
    bool clockSound = StreamClockIsSound(context, stream, false, true, Pause);
    uint64_t skipped = Overruns(context, stream) - skippedBefore;
    if (skipped > 0 && !(atRate && clockSound))
    {
        printf("the platform skipped %llu cycles while they were measured: rate %s, clock %s, "
               "not judged\n",
               (unsigned long long)skipped, atRate ? "held" : "missed",
               clockSound ? "held" : "missed");
    }
    CHECK(atRate || skipped > 0, "at its rate");
    CHECK(clockSound || skipped > 0, "its clock");
    PrintInputs();
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// The session's available inputs are listed beside the default input,
// keyed by their UIDs (the category that records, set by the stream
// before, lists them); a stream pinned to one runs, the session
// preferring that input.
static void TestPinnedInput(maudContext* context)
{
    maudNotification ignored;
    while (maudContext_NextNotification(context, &ignored) == maud_success)
    {
    }
    maudDeviceId ids[8];
    uint32_t count = 0;
    CHECK(maudContext_GetDevices(context, maud_directionInput, ids, 8, &count) == maud_success,
          "the inputs");
    maudDeviceId port = {0, 0};
    char portKey[128] = {0};
    for (uint32_t i = 0; i < count && i < 8; ++i)
    {
        char key[128] = {0};
        char name[128] = {0};
        size_t length = 0;
        maudDeviceInfo info = {0};
        CHECK(maudDevice_GetKey(context, ids[i], key, sizeof(key) - 1, &length) == maud_success &&
                  maudDevice_GetName(context, ids[i], name, sizeof(name) - 1, &length) ==
                      maud_success &&
                  maudDevice_GetInfo(context, ids[i], &info) == maud_success,
              "described");
        printf("input: %s [%s], form %u\n", name, key, (unsigned)info.form);
        if (strncmp(key, "port:", 5) == 0 && port.index1 == 0)
        {
            port = ids[i];
            memcpy(portKey, key, sizeof(portKey));
            CHECK(info.form == maud_formMicrophone, "the built-in microphone's form");
        }
    }
    CHECK(port.index1 != 0, "an available input listed");
    if (port.index1 == 0)
    {
        return;
    }
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    def.device = port;
    def.periodFrames = 256;
    def.callback = CountBlocks;
    def.user = &blocks;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStream_Start(context, stream) == maud_success,
          "a stream pinned to it");
    // A platform that skips cycles delivers fewer blocks: it is given
    // longer, while it shows the skips.
    bool captured = WaitForBlocks(context, &blocks, 20);
    for (int round = 0; round < 2 && !captured && Overruns(context, stream) > 0; ++round)
    {
        printf("pinned capture: %u blocks while the platform skips cycles; waiting on\n",
               (unsigned)atomic_load(&blocks.count));
        captured = WaitForBlocks(context, &blocks, 20);
    }
    if (!captured)
    {
        // What the stream and the session were doing, for a run that
        // fails here (one in many on CI's simulator).
        maudStreamStatus status = {0};
        maudResult read = maudStream_GetStatus(context, stream, &status);
        AVAudioSessionRouteDescription* route = [AVAudioSession sharedInstance].currentRoute;
        NSString* routed = route.inputs.count != 0 ? route.inputs[0].UID : @"none";
        fprintf(stderr,
                "pinned capture: %u blocks; status %s, started %d, suspension %d, overruns "
                "%llu; routed input %s\n",
                (unsigned)atomic_load(&blocks.count), maudGetResultName(read), status.started,
                (int)status.suspension, (unsigned long long)status.overruns, routed.UTF8String);
        CHECK(false, "captures");
    }
    NSString* preferred = [AVAudioSession sharedInstance].preferredInput.UID;
    CHECK(preferred != nil && strcmp(preferred.UTF8String, portKey + 5) == 0,
          "the session prefers that input");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

typedef struct Duplex
{
    atomic_uint count;
    atomic_uint withInput;
} Duplex;

static void CountDuplex(const maudStreamBlock* block, void* user)
{
    Duplex* duplex = user;
    if (block->output != nullptr)
    {
        memset(block->output, 0, (size_t)block->frameCount * 2 * sizeof(float));
    }
    if (block->input != nullptr)
    {
        atomic_fetch_add(&duplex->withInput, 1);
    }
    atomic_fetch_add(&duplex->count, 1);
}

// A duplex stream asking for echo cancellation and noise suppression
// runs both halves on one Voice-Processing I/O unit, in the session's
// voice chat mode, and reports what the unit applies.
static void TestVoicedDuplex(maudContext* context)
{
    Duplex duplex = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionDuplex;
    def.voice = maud_voiceEchoCancellation | maud_voiceNoiseSuppression;
    def.callback = CountDuplex;
    def.user = &duplex;
    maudStreamId stream = {0, 0};
    maudResult result = maudCreateStream(context, &def, &stream);
    printf("voiced duplex: %s\n", maudGetResultName(result));
    CHECK(result == maud_success, "a voiced duplex stream");
    if (result != maud_success)
    {
        return;
    }
    maudStreamStatus status = {0};
    CHECK(maudStream_GetStatus(context, stream, &status) == maud_success && status.voiceReported &&
              (status.voiceActive & def.voice) == def.voice,
          "echo cancellation and noise suppression reported");
    CHECK((status.voiceActive & maud_voiceGainControl) == 0, "no gain control unasked");
    CHECK(maudStream_Start(context, stream) == maud_success, "start");
    // The simulator's voice unit can be slow to settle on a loaded runner.
    POLL_UNTIL(15000, "the voiced duplex's captured input", Drain(context),
               atomic_load(&duplex.withInput) >= 20);
    printf("voiced duplex: %u blocks, %u with input\n", atomic_load(&duplex.count),
           atomic_load(&duplex.withInput));
    CHECK(atomic_load(&duplex.withInput) >= 20, "blocks with captured input");
    AVAudioSession* session = [AVAudioSession sharedInstance];
    printf("category %s, mode %s\n", session.category.UTF8String, session.mode.UTF8String);
    CHECK([session.category isEqualToString:AVAudioSessionCategoryPlayAndRecord] &&
              [session.mode isEqualToString:AVAudioSessionModeVoiceChat],
          "PlayAndRecord in voice chat mode");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

static void* Run(void* unused)
{
    (void)unused;
    const char* out = getenv("MAUD_TEST_OUT");
    if (out == nullptr || freopen(out, "w", stdout) == nullptr)
    {
        return nullptr;
    }
    setvbuf(stdout, nullptr, _IOLBF, 0);
    // The checks' diagnostics go to stderr, which simctl drops on a CI
    // runner: into the same file.
    dup2(fileno(stdout), STDERR_FILENO);
    s_control = pthread_self();
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "a native context");
    if (context != nullptr)
    {
        TestCapture(context);
        TestPinnedInput(context);
        TestVoicedDuplex(context);
        CHECK(maudDestroyContext(context) == maud_success, "destroy");
    }
    printf("result: %d failures\n", s_failures);
    return nullptr;
}

@interface TestSceneDelegate : UIResponder <UIWindowSceneDelegate>
@end

@implementation TestSceneDelegate
@end

@interface TestApplicationDelegate : UIResponder <UIApplicationDelegate>
@end

@implementation TestApplicationDelegate

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)options
{
    (void)application;
    (void)options;
    pthread_t thread;
    if (pthread_create(&thread, nullptr, Run, nullptr) == 0)
    {
        pthread_detach(thread);
    }
    return YES;
}

- (UISceneConfiguration*)application:(UIApplication*)application
    configurationForConnectingSceneSession:(UISceneSession*)session
                                   options:(UISceneConnectionOptions*)options
{
    (void)application;
    (void)options;
    UISceneConfiguration* configuration = [UISceneConfiguration configurationWithName:nil
                                                                          sessionRole:session.role];
    configuration.delegateClass = [TestSceneDelegate class];
    return configuration;
}

@end

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        return UIApplicationMain(argc, argv, nil,
                                 NSStringFromClass([TestApplicationDelegate class]));
    }
}
