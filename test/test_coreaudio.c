// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The CoreAudio backend against the BlackHole virtual device. The test
// makes BlackHole the system's default output, so it runs only where
// MAUD_REQUIRE_COREAUDIO is set (the macOS CI cell, which installs
// BlackHole); elsewhere it skips.

#include "test_clock.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <CoreAudio/CoreAudio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BLACKHOLE_UID "BlackHole2ch_UID"
// The macOS runners' own virtual device, at 44.1 kHz where BlackHole
// runs at 48 kHz.
#define NULL_DEVICE_UID "NullAudioDevice_UID"
// The aggregate device the test makes and destroys, over BlackHole.
#define AGGREGATE_UID "maud-test-aggregate"
// The exit code CTest reads as skipped.
#define SKIP 77

typedef struct Blocks
{
    atomic_uint count;
    atomic_uint wrongSize;
    atomic_uint onControl;
    uint32_t periodFrames;
    // Output: the level each sample plays at. Input: the loudest sample
    // captured, in thousandths.
    float level;
    atomic_uint loudest;
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

// Makes the device whose UID is uid the system's default output; false
// when it is not there.
static bool MakeDefaultOutput(const char* uid)
{
    CFStringRef text = CFStringCreateWithCString(kCFAllocatorDefault, uid, kCFStringEncodingUTF8);
    AudioObjectPropertyAddress address = {kAudioHardwarePropertyTranslateUIDToDevice,
                                          kAudioObjectPropertyScopeGlobal, 0};
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    OSStatus found = AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(text),
                                                (const void*)&text, &size, &device);
    CFRelease(text);
    if (found != noErr || device == kAudioObjectUnknown)
    {
        return false;
    }
    address.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    return AudioObjectSetPropertyData(kAudioObjectSystemObject, &address, 0, nullptr,
                                      sizeof(device), &device) == noErr;
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
    uint32_t samples = block->frameCount * 2;
    for (uint32_t i = 0; block->output != nullptr && i < samples; ++i)
    {
        block->output[i] = blocks->level;
    }
    for (uint32_t i = 0; block->input != nullptr && i < samples; ++i)
    {
        float sample = block->input[i] < 0.0f ? -block->input[i] : block->input[i];
        unsigned level = (unsigned)(sample * 1000.0f);
        if (level > atomic_load(&blocks->loudest))
        {
            atomic_store(&blocks->loudest, level);
        }
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

// The stream's rate: the best of three windows of a second, since a
// loaded machine's stalls only lower a window's count.
static double MeasureRate(const maudContext* context, maudStreamId stream)
{
    double best = 0.0;
    for (int window = 0; window < 3; ++window)
    {
        uint64_t first = 0;
        uint64_t last = 0;
        double start = Now();
        CHECK(maudGetStreamPosition(context, stream, &first) == maud_success, "position");
        Sleep(1000);
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

// Prints every device, so a failing run shows what the machine has.
static void ListDevices(const maudContext* context, maudDirection direction)
{
    maudDeviceId ids[32];
    uint32_t count = 0;
    CHECK(maudGetDevices(context, direction, ids, 32, &count) == maud_success, "list");
    for (uint32_t i = 0; i < count && i < 32; ++i)
    {
        char name[128] = {0};
        char key[128] = {0};
        size_t length = 0;
        maudDeviceInfo info = {0};
        CHECK(maudGetDeviceName(context, ids[i], name, sizeof(name) - 1, &length) == maud_success &&
                  maudGetDeviceKey(context, ids[i], key, sizeof(key) - 1, &length) ==
                      maud_success &&
                  maudGetDeviceInfo(context, ids[i], &info) == maud_success,
              "describe");
        printf("%s: %s [%s] %u Hz (%u to %u), layout %u\n",
               direction == maud_directionOutput ? "output" : "input", name, key,
               info.nativeSampleRate, info.minSampleRate, info.maxSampleRate,
               (unsigned)info.nativeLayout);
    }
}

static maudDeviceId FindByKey(const maudContext* context, maudDirection direction, const char* uid)
{
    maudDeviceId ids[32];
    uint32_t count = 0;
    CHECK(maudGetDevices(context, direction, ids, 32, &count) == maud_success, "list");
    for (uint32_t i = 0; i < count && i < 32; ++i)
    {
        char key[128] = {0};
        size_t length = 0;
        if (maudGetDeviceKey(context, ids[i], key, sizeof(key) - 1, &length) == maud_success &&
            strcmp(key, uid) == 0)
        {
            return ids[i];
        }
    }
    return (maudDeviceId){0, 0};
}

static void TestDevices(maudContext* context)
{
    ListDevices(context, maud_directionOutput);
    ListDevices(context, maud_directionInput);
    maudDeviceId output = FindByKey(context, maud_directionOutput, BLACKHOLE_UID);
    maudDeviceId input = FindByKey(context, maud_directionInput, BLACKHOLE_UID);
    CHECK(output.index1 != 0 && input.index1 != 0, "BlackHole both ways, keyed by its UID");
    maudDeviceInfo info = {0};
    char name[128] = {0};
    size_t length = 0;
    CHECK(maudGetDeviceInfo(context, output, &info) == maud_success &&
              info.nativeLayout == maud_layoutStereo && info.nativeSampleRate >= 8000 &&
              info.minSampleRate < info.nativeSampleRate &&
              info.maxSampleRate > info.nativeSampleRate,
          "stereo, its nominal rate inside the wider range it runs at");
    CHECK(maudGetDeviceName(context, output, name, sizeof(name) - 1, &length) == maud_success &&
              strstr(name, "BlackHole") != nullptr,
          "its name");
    for (maudDeviceRole role = maud_roleGeneral; role <= maud_roleCommunications; ++role)
    {
        maudDeviceId current = {0, 0};
        CHECK(maudGetDefaultDevice(context, maud_directionOutput, role, &current) == maud_success &&
                  current.index1 == output.index1 && current.generation == output.generation,
              "the default output for both roles");
    }
}

static void TestOutputStream(maudContext* context)
{
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, FindByKey(context, maud_directionOutput, BLACKHOLE_UID),
                            &info) == maud_success,
          "BlackHole");
    double nominal = (double)info.nativeSampleRate;
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenStream(context, &def, &blocks);
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success &&
              format.sampleRate == info.nativeSampleRate,
          "native at the nominal rate");
    Sleep(100);
    CHECK(atomic_load(&blocks.count) == 0, "nothing before start");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 20), "blocks arrive");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(atomic_load(&blocks.onControl) == 0, "on the IO thread");
    CHECK(Near(MeasureRate(context, stream), nominal), "at its rate");
    CHECK(StreamClockIsSound(context, stream, true, true, Sleep),
          "its clock maps frames to host time");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    uint32_t stopped = atomic_load(&blocks.count);
    uint64_t before = 0;
    uint64_t after = 0;
    CHECK(maudGetStreamPosition(context, stream, &before) == maud_success, "position");
    Sleep(100);
    CHECK(maudGetStreamPosition(context, stream, &after) == maud_success, "position");
    CHECK(atomic_load(&blocks.count) == stopped, "no callbacks once stopped");
    CHECK(after == before, "the device stops with the stream");
    CHECK(maudStartStream(context, stream) == maud_success, "start again");
    CHECK(WaitForBlocks(context, &blocks, stopped + 20), "it runs again");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy while running");
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
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.mode = maud_modePull;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no pull mode");
}

// Waits up to three seconds for the system's default output to reach
// the library: the default, the stream's move and, when the rate
// differs, its new rate.
static bool WaitForMove(maudContext* context, maudStreamId stream, maudDeviceId device,
                        bool rateChanges)
{
    bool defaulted = false;
    bool moved = false;
    bool reformatted = !rateChanges;
    for (int tries = 0; tries < 300 && !(defaulted && moved && reformatted); ++tries)
    {
        maudNotification record;
        while (maudNextNotification(context, &record) == maud_success)
        {
            bool here = record.deviceId.index1 == device.index1 &&
                        record.deviceId.generation == device.generation;
            defaulted = defaulted || (record.kind == maud_notifyDefaultChanged && here &&
                                      record.direction == maud_directionOutput);
            moved = moved || (record.kind == maud_notifyStreamMoved && here &&
                              record.streamId.index1 == stream.index1);
            reformatted = reformatted || (record.kind == maud_notifyStreamFormatChanged &&
                                          record.streamId.index1 == stream.index1);
        }
        Sleep(10);
    }
    if (!(defaulted && moved && reformatted))
    {
        fprintf(stderr, "default %d, moved %d, new rate %d\n", defaulted, moved, reformatted);
    }
    return defaulted && moved && reformatted;
}

// A stream on the default follows the system's default output to a
// device at another rate, and back.
static void TestDefaultMoves(maudContext* context)
{
    maudDeviceId blackhole = FindByKey(context, maud_directionOutput, BLACKHOLE_UID);
    maudDeviceId other = FindByKey(context, maud_directionOutput, NULL_DEVICE_UID);
    maudDeviceInfo info = {0};
    CHECK(other.index1 != 0 && maudGetDeviceInfo(context, other, &info) == maud_success,
          "the runner's null device");
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenStream(context, &def, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 10), "it plays");
    CHECK(MakeDefaultOutput(NULL_DEVICE_UID), "the null device made the default");
    CHECK(WaitForMove(context, stream, other, true), "the stream follows the default");
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success &&
              format.sampleRate == info.nativeSampleRate,
          "at the new device's rate");
    CHECK(WaitForBlocks(context, &blocks, atomic_load(&blocks.count) + 20), "it plays there");
    CHECK(Near(MeasureRate(context, stream), (double)info.nativeSampleRate), "at that rate");
    CHECK(MakeDefaultOutput(BLACKHOLE_UID), "BlackHole the default again");
    CHECK(WaitForMove(context, stream, blackhole, true), "and back");
    CHECK(WaitForBlocks(context, &blocks, atomic_load(&blocks.count) + 20), "it plays again");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods throughout");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// Makes a private aggregate device over BlackHole, seen by this process
// only; returns its object, or kAudioObjectUnknown.
static AudioObjectID MakeAggregate(void)
{
    CFMutableDictionaryRef sub = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(sub, CFSTR(kAudioSubDeviceUIDKey), CFSTR(BLACKHOLE_UID));
    const void* subs[1] = {sub};
    CFArrayRef list = CFArrayCreate(kCFAllocatorDefault, subs, 1, &kCFTypeArrayCallBacks);
    int one = 1;
    CFNumberRef isPrivate = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &one);
    CFMutableDictionaryRef description = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(description, CFSTR(kAudioAggregateDeviceUIDKey), CFSTR(AGGREGATE_UID));
    CFDictionarySetValue(description, CFSTR(kAudioAggregateDeviceNameKey),
                         CFSTR("Maud test aggregate"));
    CFDictionarySetValue(description, CFSTR(kAudioAggregateDeviceSubDeviceListKey), list);
    CFDictionarySetValue(description, CFSTR(kAudioAggregateDeviceIsPrivateKey), isPrivate);
    AudioObjectID aggregate = kAudioObjectUnknown;
    OSStatus status = AudioHardwareCreateAggregateDevice(description, &aggregate);
    CFRelease(description);
    CFRelease(isPrivate);
    CFRelease(list);
    CFRelease(sub);
    if (status != noErr)
    {
        fprintf(stderr, "AudioHardwareCreateAggregateDevice: %d\n", (int)status);
    }
    return status == noErr ? aggregate : kAudioObjectUnknown;
}

// Drains notifications for up to three seconds until one of kind about
// device (by key, when added) or stream arrives; returns its device id.
static maudDeviceId WaitFor(maudContext* context, maudNotificationKind kind, maudStreamId stream)
{
    for (int tries = 0; tries < 300; ++tries)
    {
        maudNotification record;
        while (maudNextNotification(context, &record) == maud_success)
        {
            bool aggregate = false;
            char key[128] = {0};
            size_t length = 0;
            if (record.kind == maud_notifyDeviceAdded)
            {
                aggregate = maudGetDeviceKey(context, record.deviceId, key, sizeof(key) - 1,
                                             &length) == maud_success &&
                            strcmp(key, AGGREGATE_UID) == 0 &&
                            record.direction == maud_directionOutput;
            }
            bool streamMatches = record.streamId.index1 == stream.index1 && stream.index1 != 0;
            if (record.kind == kind &&
                (aggregate || streamMatches || kind == maud_notifyDeviceRemoved))
            {
                return record.deviceId.index1 != 0 ? record.deviceId : (maudDeviceId){1, 0};
            }
        }
        Sleep(10);
    }
    fprintf(stderr, "no notification of kind %u\n", (unsigned)kind);
    return (maudDeviceId){0, 0};
}

static maudSuspendReason Suspension(const maudContext* context, maudStreamId stream)
{
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
    return status.suspension;
}

// A stream bound to a device that goes away is suspended and silent.
// The device comes back under the same UID as a new id, and the stream
// stays where it was opened, lost.
static void TestHotplug(maudContext* context)
{
    AudioObjectID aggregate = MakeAggregate();
    CHECK(aggregate != kAudioObjectUnknown, "an aggregate device made");
    maudStreamId none = {0, 0};
    maudDeviceId device = WaitFor(context, maud_notifyDeviceAdded, none);
    CHECK(device.index1 != 0, "it appears, keyed by its UID");
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.device = device;
    maudStreamId stream = OpenStream(context, &def, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success, "start on it");
    CHECK(WaitForBlocks(context, &blocks, 20), "it plays");
    CHECK(AudioHardwareDestroyAggregateDevice(aggregate) == noErr, "the device destroyed");
    CHECK(WaitFor(context, maud_notifyStreamSuspended, stream).index1 != 0, "the stream suspends");
    CHECK(Suspension(context, stream) == maud_suspendDeviceLost, "for the lost device");
    uint32_t lost = atomic_load(&blocks.count);
    Sleep(200);
    CHECK(atomic_load(&blocks.count) == lost, "silent while lost");
    aggregate = MakeAggregate();
    CHECK(aggregate != kAudioObjectUnknown, "the device made again");
    maudDeviceId again = WaitFor(context, maud_notifyDeviceAdded, none);
    CHECK(again.index1 != 0 &&
              (again.index1 != device.index1 || again.generation != device.generation),
          "it comes back as a new id");
    Sleep(200);
    CHECK(Suspension(context, stream) == maud_suspendDeviceLost, "the stream stays lost");
    CHECK(atomic_load(&blocks.count) == lost, "and silent");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    CHECK(AudioHardwareDestroyAggregateDevice(aggregate) == noErr, "the device destroyed again");
    CHECK(WaitFor(context, maud_notifyDeviceRemoved, none).index1 != 0, "it disappears");
}

// BlackHole's input hears its output: a stream captures what another
// plays into it, at the device's rate, with a sound clock. A converted
// input at another rate is refused.
static void TestCapture(maudContext* context)
{
    maudDeviceId output = FindByKey(context, maud_directionOutput, BLACKHOLE_UID);
    maudDeviceId input = FindByKey(context, maud_directionInput, BLACKHOLE_UID);
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, input, &info) == maud_success, "BlackHole's input");
    Blocks played = {.level = 0.25f};
    maudStreamDef def = maudDefaultStreamDef();
    def.device = output;
    maudStreamId player = OpenStream(context, &def, &played);
    Blocks heard = {0};
    def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    def.device = input;
    maudStreamId recorder = OpenStream(context, &def, &heard);
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, recorder, &format) == maud_success &&
              format.sampleRate == info.nativeSampleRate,
          "capture at the device's rate");
    CHECK(maudStartStream(context, player) == maud_success, "play");
    CHECK(maudStartStream(context, recorder) == maud_success, "capture");
    CHECK(WaitForBlocks(context, &heard, 40), "captured blocks arrive");
    CHECK(atomic_load(&heard.wrongSize) == 0, "in whole periods");
    CHECK(atomic_load(&heard.onControl) == 0, "on the IO thread");
    CHECK(Near(MeasureRate(context, recorder), (double)info.nativeSampleRate), "at its rate");
    unsigned loudest = atomic_load(&heard.loudest);
    if (loudest < 200)
    {
        fprintf(stderr, "loudest captured sample %u thousandths\n", loudest);
    }
    CHECK(loudest >= 200, "it hears what plays into BlackHole");
    CHECK(StreamClockIsSound(context, recorder, false, true, Sleep),
          "its clock maps frames to host time");
    CHECK(maudDestroyStream(context, recorder) == maud_success, "destroy the capture");
    CHECK(maudDestroyStream(context, player) == maud_success, "destroy the player");
    def.ratePolicy = maud_ratePlatformConverted;
    def.sampleRate = info.nativeSampleRate == 44100 ? 48000 : 44100;
    def.callback = CountBlocks;
    CHECK(maudCreateStream(context, &def, &recorder) == maud_errorUnsupported,
          "no converted capture at another rate");
}

int main(void)
{
    if (getenv("MAUD_REQUIRE_COREAUDIO") == nullptr)
    {
        return SKIP;
    }
    s_control = pthread_self();
    CHECK(MakeDefaultOutput(BLACKHOLE_UID), "BlackHole made the default output");
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "a native context");
    if (context == nullptr)
    {
        return 1;
    }
    CHECK(maudGetContextBackend(context) == maud_backendCoreAudio, "CoreAudio chosen");
    TestDevices(context);
    TestOutputStream(context);
    TestCapture(context);
    TestDefaultMoves(context);
    TestHotplug(context);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
