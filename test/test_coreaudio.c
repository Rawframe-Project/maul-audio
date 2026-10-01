// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The CoreAudio backend against the BlackHole virtual device. The test
// makes BlackHole the system's default output, so it runs only where
// MAUD_REQUIRE_COREAUDIO is set (the macOS CI cell, which installs
// BlackHole); elsewhere it skips.

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
// The exit code CTest reads as skipped.
#define SKIP 77

typedef struct Blocks
{
    atomic_uint count;
    atomic_uint wrongSize;
    atomic_uint onControl;
    uint32_t periodFrames;
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

static maudDeviceId FindBlackHole(const maudContext* context, maudDirection direction)
{
    maudDeviceId ids[32];
    uint32_t count = 0;
    CHECK(maudGetDevices(context, direction, ids, 32, &count) == maud_success, "list");
    for (uint32_t i = 0; i < count && i < 32; ++i)
    {
        char key[128] = {0};
        size_t length = 0;
        if (maudGetDeviceKey(context, ids[i], key, sizeof(key) - 1, &length) == maud_success &&
            strcmp(key, BLACKHOLE_UID) == 0)
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
    maudDeviceId output = FindBlackHole(context, maud_directionOutput);
    maudDeviceId input = FindBlackHole(context, maud_directionInput);
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
    CHECK(maudGetDeviceInfo(context, FindBlackHole(context, maud_directionOutput), &info) ==
              maud_success,
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
    def.mode = maud_modeCallback;
    def.direction = maud_directionInput;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no capture yet");
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
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
