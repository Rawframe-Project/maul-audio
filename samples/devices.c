// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Lists the devices, plays a quiet tone on the default output, and
// prints every notification with the stream's latency, for a number of
// seconds (the first argument, 60 by default). It is the host the
// device checklist (docs/device-checklist.md) runs while devices are
// plugged in and out, defaults changed and the audio service
// restarted.

#define _POSIX_C_SOURCE 200809L

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/layout.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void Pause(int milliseconds)
{
    Sleep((DWORD)milliseconds);
}
#else
#include <time.h>
static void Pause(int milliseconds)
{
    struct timespec span = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&span, nullptr);
}
#endif

// A 440 Hz tone at -20 dBFS on every channel; the phase is the stream's
// own, so the callback keeps no state of the host's.
static void Tone(const maudStreamBlock* block, void* user)
{
    (void)user;
    uint32_t channels = maudGetLayoutChannelCount(block->layout);
    for (uint32_t i = 0; i < block->frameCount; ++i)
    {
        double t = (double)(block->position + i) / block->sampleRate;
        float value = (float)(0.1 * sin(2.0 * 3.14159265358979323846 * 440.0 * t));
        for (uint32_t c = 0; c < channels; ++c)
        {
            block->output[(size_t)i * channels + c] = value;
        }
    }
}

static void PrintDevice(const maudContext* context, maudDeviceId id)
{
    char name[256];
    size_t length = 0;
    maudDeviceInfo info = {0};
    if (maudGetDeviceName(context, id, name, sizeof(name) - 1, &length) != maud_success)
    {
        length = 0;
    }
    name[length < sizeof(name) - 1 ? length : sizeof(name) - 1] = '\0';
    if (maudGetDeviceInfo(context, id, &info) != maud_success)
    {
        return;
    }
    // A backend that cannot read a format without opening the device
    // (ALSA) reports none.
    char format[64] = "format not reported";
    if (info.nativeSampleRate != 0)
    {
        snprintf(format, sizeof(format), "%u Hz, %u channels", info.nativeSampleRate,
                 maudGetLayoutChannelCount(info.nativeLayout));
    }
    printf("  %u.%u %-40s %s%s%s\n", id.index1, id.generation, name, format,
           info.defaultGeneral ? ", default" : "",
           info.defaultCommunications ? ", communications default" : "");
}

static void ListDevices(const maudContext* context)
{
    const char* titles[2] = {"Outputs", "Inputs"};
    for (int d = 0; d < 2; ++d)
    {
        maudDeviceId ids[64];
        uint32_t count = 0;
        if (maudGetDevices(context, d == 0 ? maud_directionOutput : maud_directionInput, ids, 64,
                           &count) != maud_success)
        {
            continue;
        }
        printf("%s:\n", titles[d]);
        for (uint32_t i = 0; i < count && i < 64; ++i)
        {
            PrintDevice(context, ids[i]);
        }
    }
}

static const char* KindName(maudNotificationKind kind)
{
    switch (kind)
    {
    case maud_notifyDeviceAdded:
        return "device added";
    case maud_notifyDeviceRemoved:
        return "device removed";
    case maud_notifyDefaultChanged:
        return "default changed";
    case maud_notifyStreamMoved:
        return "stream moved";
    case maud_notifyStreamSuspended:
        return "stream suspended";
    case maud_notifyStreamResumed:
        return "stream resumed";
    case maud_notifyStreamFormatChanged:
        return "stream format changed";
    case maud_notifyOverflow:
        return "notifications dropped";
    case maud_notifyRouteChanged:
        return "route changed";
    case maud_notifyFocusChanged:
        return "focus changed";
    case maud_notifySpatializerChanged:
        return "spatializer changed";
    }
    return "unknown";
}

int main(int argc, char** argv)
{
    int seconds = argc > 1 ? atoi(argv[1]) : 60;
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    maudResult result = maudCreateContext(&def, &context);
    if (result != maud_success)
    {
        fprintf(stderr, "no context: %s\n", maudResultName(result));
        return 1;
    }
    ListDevices(context);
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.callback = Tone;
    maudStreamId stream = {0, 0};
    result = maudCreateStream(context, &streamDef, &stream);
    if (result == maud_success)
    {
        result = maudStartStream(context, stream);
    }
    if (result != maud_success)
    {
        fprintf(stderr, "no stream: %s\n", maudResultName(result));
        (void)maudDestroyContext(context);
        return 1;
    }
    printf("Playing a tone on the default output for %d s.\n", seconds);
    int64_t lastLatency = -1;
    uint64_t lastUnderruns = 0;
    for (int tick = 0; tick < seconds * 20; ++tick)
    {
        maudNotification record;
        while (maudNextNotification(context, &record) == maud_success)
        {
            printf("%6.2f s  %-22s device %u.%u, stream %u, reason %u, %u Hz\n", tick / 20.0,
                   KindName(record.kind), record.deviceId.index1, record.deviceId.generation,
                   record.streamId.index1, (unsigned)record.reason, record.sampleRate);
        }
        maudStreamClock clock = {0};
        // The latency when first known, then each time it moves by more
        // than 5 ms (a Bluetooth route adds hundreds).
        if (maudGetStreamClock(context, stream, &clock) == maud_success &&
            clock.hostNanoseconds != 0 &&
            (lastLatency < 0 || llabs(clock.latencyNanoseconds - lastLatency) > 5000000))
        {
            printf("%6.2f s  latency %.1f ms\n", tick / 20.0, clock.latencyNanoseconds / 1e6);
            lastLatency = clock.latencyNanoseconds;
        }
        // Underruns as the platform reveals them.
        maudStreamStatus status = {0};
        if (maudGetStreamStatus(context, stream, &status) == maud_success &&
            status.underruns != lastUnderruns)
        {
            printf("%6.2f s  underruns %llu\n", tick / 20.0, (unsigned long long)status.underruns);
            lastUnderruns = status.underruns;
        }
        Pause(50);
    }
    (void)maudDestroyStream(context, stream);
    (void)maudDestroyContext(context);
    return 0;
}
