// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Lists the devices with their keys, plays a quiet tone on the default
// output, and prints every notification with the stream's latency, for a
// number of seconds (the first argument, 60 by default). After it,
// "capture" also captures from the default input and prints, each
// second, the frames captured and their peak, and "--device <key>" plays
// on the output with that key instead of following the default. It is
// the host the
// device checklist (docs/operations/device-checklist.md) runs while devices are
// plugged in and out, defaults changed and the audio service
// restarted. On the web it runs ten minutes with the capture, from the
// first click on the page, which lets the browser start the sound.

#define _POSIX_C_SOURCE 200809L

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/layout.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#elif defined(_WIN32)
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

// What the capture brought since the last report: frames, and the peak
// in millionths of full scale.
static atomic_uint_least64_t s_capturedFrames;
static atomic_uint_least32_t s_capturedPeak;

static void Capture(const maudStreamBlock* block, void* user)
{
    (void)user;
    uint32_t samples = block->frameCount * maudGetLayoutChannelCount(block->layout);
    float peak = 0.0f;
    for (uint32_t i = 0; i < samples; ++i)
    {
        float value = fabsf(block->input[i]);
        peak = value > peak ? value : peak;
    }
    uint32_t scaled = (uint32_t)(fminf(peak, 1.0f) * 1e6f);
    uint32_t seen = atomic_load(&s_capturedPeak);
    while (scaled > seen && !atomic_compare_exchange_weak(&s_capturedPeak, &seen, scaled))
    {
    }
    atomic_fetch_add(&s_capturedFrames, block->frameCount);
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
    char key[256];
    if (maudGetDeviceKey(context, id, key, sizeof(key) - 1, &length) != maud_success)
    {
        length = 0;
    }
    key[length < sizeof(key) - 1 ? length : sizeof(key) - 1] = '\0';
    printf("  %u.%u %-40s %s%s%s\n        key %s\n", id.index1, id.generation, name, format,
           info.defaultGeneral ? ", default" : "",
           info.defaultCommunications ? ", communications default" : "", key);
}

// The output whose key is key, or the null id.
static maudDeviceId FindOutput(const maudContext* context, const char* key)
{
    maudDeviceId ids[64];
    uint32_t count = 0;
    if (maudGetDevices(context, maud_directionOutput, ids, 64, &count) != maud_success)
    {
        return (maudDeviceId){0, 0};
    }
    for (uint32_t i = 0; i < count && i < 64; ++i)
    {
        char bytes[256];
        size_t length = 0;
        if (maudGetDeviceKey(context, ids[i], bytes, sizeof(bytes), &length) == maud_success &&
            length == strlen(key) && memcmp(bytes, key, length) == 0)
        {
            return ids[i];
        }
    }
    return (maudDeviceId){0, 0};
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
    case maud_notifyDeviceFormatChanged:
        return "device format changed";
    }
    return "unknown";
}

// The run's state, stepped every 50 ms.
static maudContext* s_context;
static maudStreamId s_stream;
static maudStreamId s_capture;
static int s_ticks;
static int s_tick;
static int64_t s_lastLatency = -1;
static uint64_t s_lastUnderruns;

// One step: the notifications, the latency and underruns as they move,
// and the capture once a second. False once the run is over.
static bool Step(void)
{
    double at = s_tick / 20.0;
    maudNotification record;
    while (maudNextNotification(s_context, &record) == maud_success)
    {
        printf("%6.2f s  %-22s device %u.%u, stream %u, reason %u, %u Hz\n", at,
               KindName(record.kind), record.device.index1, record.device.generation,
               record.stream.index1, (unsigned)record.reason, record.sampleRate);
    }
    maudStreamClock clock = {0};
    // The latency when first known, then each time it moves by more
    // than 100 ms: a Bluetooth route adds hundreds, and the web's moves
    // by tens as its buffers fill.
    if (maudGetStreamClock(s_context, s_stream, &clock) == maud_success &&
        clock.hostNanoseconds != 0 &&
        (s_lastLatency < 0 || llabs(clock.latencyNanoseconds - s_lastLatency) > 100000000))
    {
        printf("%6.2f s  latency %.1f ms\n", at, clock.latencyNanoseconds / 1e6);
        s_lastLatency = clock.latencyNanoseconds;
    }
    // Underruns as the platform reveals them.
    maudStreamStatus status = {0};
    if (maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
        status.underruns != s_lastUnderruns)
    {
        printf("%6.2f s  underruns %llu\n", at, (unsigned long long)status.underruns);
        s_lastUnderruns = status.underruns;
    }
    if (s_capture.index1 != 0 && s_tick % 20 == 19)
    {
        unsigned long long frames = atomic_exchange(&s_capturedFrames, 0);
        double peak = atomic_exchange(&s_capturedPeak, 0) / 1e6;
        printf("%6.2f s  captured %llu frames, peak %.1f dBFS\n", at, frames,
               peak > 0.0 ? 20.0 * log10(peak) : -120.0);
    }
    return ++s_tick < s_ticks;
}

static void Finish(void)
{
    if (s_capture.index1 != 0)
    {
        (void)maudDestroyStream(s_context, s_capture);
    }
    (void)maudDestroyStream(s_context, s_stream);
    (void)maudDestroyContext(s_context);
    printf("Done.\n");
}

#if defined(__EMSCRIPTEN__)
static long s_interval;

static void StepOnTheWeb(void* user)
{
    (void)user;
    if (!Step())
    {
        emscripten_clear_interval(s_interval);
        Finish();
    }
}

EMSCRIPTEN_KEEPALIVE void SampleClicked(void);

void SampleClicked(void)
{
    (void)maudResumeContext(s_context);
}

EM_JS(void, ListenForClicks, (void),
      { document.addEventListener("click", function() { _SampleClicked(); }); });
#endif

// Opens and starts a stream on device, or with the null id on the
// default device of its direction.
static maudResult Open(maudDirection direction, maudDeviceId device, maudStreamCallback callback,
                       maudStreamId* out)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = direction;
    def.device = device;
    def.callback = callback;
    maudResult result = maudCreateStream(s_context, &def, out);
    return result == maud_success ? maudStartStream(s_context, *out) : result;
}

int main(int argc, char** argv)
{
#if defined(__EMSCRIPTEN__)
    (void)argc;
    (void)argv;
    int seconds = 600;
    bool capture = true;
#else
    int seconds = argc > 1 ? atoi(argv[1]) : 60;
    bool capture = false;
    const char* key = nullptr;
    for (int i = 2; i < argc; ++i)
    {
        if (strcmp(argv[i], "capture") == 0)
        {
            capture = true;
        }
        else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc)
        {
            key = argv[++i];
        }
        else
        {
            fprintf(stderr, "usage: sample_devices [seconds] [capture] [--device <key>]\n");
            return 2;
        }
    }
#endif
    s_ticks = seconds * 20;
    maudContextDef def = maudDefaultContextDef();
    maudResult result = maudCreateContext(&def, &s_context);
    if (result != maud_success)
    {
        fprintf(stderr, "no context: %s\n", maudResultName(result));
        return 1;
    }
    ListDevices(s_context);
    maudDeviceId output = {0, 0};
#if !defined(__EMSCRIPTEN__)
    if (key != nullptr && (output = FindOutput(s_context, key)).index1 == 0)
    {
        fprintf(stderr, "no output with the key %s\n", key);
        (void)maudDestroyContext(s_context);
        return 1;
    }
#endif
    result = Open(maud_directionOutput, output, Tone, &s_stream);
    if (result != maud_success)
    {
        fprintf(stderr, "no stream: %s\n", maudResultName(result));
        (void)maudDestroyContext(s_context);
        return 1;
    }
    if (capture && (result = Open(maud_directionInput, (maudDeviceId){0, 0}, Capture,
                                  &s_capture)) != maud_success)
    {
        fprintf(stderr, "no capture: %s\n", maudResultName(result));
        s_capture = (maudStreamId){0, 0};
    }
    printf("Playing a tone on %s%s for %d s.\n",
           output.index1 != 0 ? "the output asked for" : "the default output",
           s_capture.index1 != 0 ? " and capturing the default input" : "", seconds);
#if defined(__EMSCRIPTEN__)
    printf("Click the page to let the browser start the sound.\n");
    ListenForClicks();
    s_interval = emscripten_set_interval(StepOnTheWeb, 50.0, nullptr);
#else
    while (Step())
    {
        Pause(50);
    }
    Finish();
#endif
    return 0;
}
