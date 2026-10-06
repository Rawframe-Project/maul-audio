// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Capture on iOS, as an application in the simulator
// (tools/run_ios_app.sh, which grants it the microphone): an input
// stream runs at its rate on the IO thread with a sound clock, under
// the Record category, which takes no mixing option. It also prints what the simulator offers
// beyond the default input and whether a Voice-Processing I/O unit
// initializes there. The tests run on a thread of their own once the
// application has launched; the scene delegate keeps UIKit content.

#include "test_clock.h"
#include "test_harness.h"

#include "maul-audio/context.h"
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
        printf("measured %.0f frames/s, expected %.0f\n", rate, expected);
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

// Each step of a RemoteIO input unit's set-up under the Record
// category, with its status, so that a refusal names its step.
static void PrintInputSteps(void)
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    NSError* error = nil;
    BOOL categorized = [session setCategory:AVAudioSessionCategoryRecord error:&error];
    printf("record permission %ld, input available %d, category set %d (%s)\n",
           (long)session.recordPermission, (int)session.inputAvailable, (int)categorized,
           error != nil ? error.localizedDescription.UTF8String : "no error");
    error = nil;
    BOOL activated = [session setActive:YES error:&error];
    printf("activated %d (%s), sample rate %.0f, inputs %ld\n", (int)activated,
           error != nil ? error.localizedDescription.UTF8String : "no error", session.sampleRate,
           (long)session.inputNumberOfChannels);
    PrintInputs();
    AudioComponentDescription description = {
        .componentType = kAudioUnitType_Output,
        .componentSubType = kAudioUnitSubType_RemoteIO,
        .componentManufacturer = kAudioUnitManufacturer_Apple,
    };
    AudioComponentInstance unit = nullptr;
    OSStatus made = AudioComponentInstanceNew(AudioComponentFindNext(nullptr, &description), &unit);
    UInt32 on = 1;
    UInt32 off = 0;
    OSStatus input = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO,
                                          kAudioUnitScope_Input, 1, &on, sizeof(on));
    OSStatus output = AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO,
                                           kAudioUnitScope_Output, 0, &off, sizeof(off));
    AudioStreamBasicDescription format = {
        .mSampleRate = session.sampleRate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = sizeof(float),
        .mFramesPerPacket = 1,
        .mBytesPerFrame = sizeof(float),
        .mChannelsPerFrame = 1,
        .mBitsPerChannel = 32,
    };
    OSStatus formatted = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
                                              kAudioUnitScope_Output, 1, &format, sizeof(format));
    OSStatus initialized = AudioUnitInitialize(unit);
    printf("remote input: made %d, input %d, output off %d, format %d, initialized %d\n", (int)made,
           (int)input, (int)output, (int)formatted, (int)initialized);
    OSStatus uninitialized = AudioUnitUninitialize(unit);
    OSStatus disposed = AudioComponentInstanceDispose(unit);
    (void)uninitialized;
    (void)disposed;
    BOOL deactivated = [session setActive:NO error:nil];
    (void)deactivated;
}

static OSStatus Ignore(void* user, AudioUnitRenderActionFlags* flags, const AudioTimeStamp* time,
                       UInt32 bus, UInt32 frames, AudioBufferList* data)
{
    (void)user;
    (void)flags;
    (void)time;
    (void)bus;
    (void)frames;
    (void)data;
    return noErr;
}

// The library's own sequence for an input unit (Record with Bluetooth
// allowed, no mode, an input callback and the largest slice), with the
// session left inactive as the library leaves it before a start, and
// then active.
static void PrintLibrarySteps(void)
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    for (int active = 0; active < 2; ++active)
    {
        NSError* error = nil;
        BOOL categorized = [session setCategory:AVAudioSessionCategoryRecord
                                           mode:AVAudioSessionModeDefault
                                        options:(AVAudioSessionCategoryOptions)0x4
                                          error:&error];
        BOOL activated = active != 0 ? [session setActive:YES error:nil] : NO;
        AudioComponentDescription description = {
            .componentType = kAudioUnitType_Output,
            .componentSubType = kAudioUnitSubType_RemoteIO,
            .componentManufacturer = kAudioUnitManufacturer_Apple,
        };
        AudioComponentInstance unit = nullptr;
        OSStatus made =
            AudioComponentInstanceNew(AudioComponentFindNext(nullptr, &description), &unit);
        UInt32 on = 1;
        UInt32 off = 0;
        UInt32 slice = 4096;
        AudioStreamBasicDescription format = {
            .mSampleRate = 48000.0,
            .mFormatID = kAudioFormatLinearPCM,
            .mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked,
            .mBytesPerPacket = sizeof(float),
            .mFramesPerPacket = 1,
            .mBytesPerFrame = sizeof(float),
            .mChannelsPerFrame = 1,
            .mBitsPerChannel = 32,
        };
        AURenderCallbackStruct callback = {.inputProc = Ignore, .inputProcRefCon = nullptr};
        OSStatus steps[5] = {
            AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Input, 1,
                                 &on, sizeof(on)),
            AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0,
                                 &off, sizeof(off)),
            AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1,
                                 &format, sizeof(format)),
            AudioUnitSetProperty(unit, kAudioOutputUnitProperty_SetInputCallback,
                                 kAudioUnitScope_Global, 0, &callback, sizeof(callback)),
            AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice,
                                 kAudioUnitScope_Global, 0, &slice, sizeof(slice)),
        };
        OSStatus initialized = AudioUnitInitialize(unit);
        printf("library steps, session %s: category %d (%s), activated %d, made %d, "
               "steps %d %d %d %d %d, initialized %d\n",
               active != 0 ? "active" : "inactive", (int)categorized,
               error != nil ? error.localizedDescription.UTF8String : "no error", (int)activated,
               (int)made, (int)steps[0], (int)steps[1], (int)steps[2], (int)steps[3], (int)steps[4],
               (int)initialized);
        OSStatus uninitialized = AudioUnitUninitialize(unit);
        OSStatus disposed = AudioComponentInstanceDispose(unit);
        (void)uninitialized;
        (void)disposed;
        BOOL deactivated = [session setActive:NO error:nil];
        (void)deactivated;
    }
}

static void TestCapture(maudContext* context)
{
    PrintInputSteps();
    PrintLibrarySteps();
    PrintVoiceUnit();
    maudDeviceId input = {0, 0};
    maudDeviceInfo info = {0};
    CHECK(maudGetDefaultDevice(context, maud_directionInput, maud_roleGeneral, &input) ==
                  maud_success &&
              maudGetDeviceInfo(context, input, &info) == maud_success,
          "the default input");
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    def.periodFrames = 256;
    def.callback = CountBlocks;
    def.user = &blocks;
    maudStreamId stream = {0, 0};
    maudResult result = maudCreateStream(context, &def, &stream);
    printf("capture: %s\n", maudResultName(result));
    CHECK(result == maud_success, "an input stream, granted the microphone");
    if (result != maud_success)
    {
        return;
    }
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 20), "captured blocks arrive");
    CHECK(atomic_load(&blocks.wrongSize) == 0 && atomic_load(&blocks.onControl) == 0,
          "in whole periods, on the IO thread");
    AVAudioSession* session = [AVAudioSession sharedInstance];
    printf("category %s, options %lu\n", session.category.UTF8String,
           (unsigned long)session.categoryOptions);
    CHECK([session.category isEqualToString:AVAudioSessionCategoryRecord] &&
              (session.categoryOptions & AVAudioSessionCategoryOptionMixWithOthers) == 0,
          "Record, which takes no mixing option");
    CHECK(Near(MeasureRate(context, stream), (double)info.nativeSampleRate), "at its rate");
    CHECK(StreamClockIsSound(context, stream, false, true, Sleep), "its clock");
    PrintInputs();
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
    s_control = pthread_self();
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "a native context");
    if (context != nullptr)
    {
        TestCapture(context);
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
