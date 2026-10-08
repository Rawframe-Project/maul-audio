// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The guide's C snippets (docs/guide.md), each as written there:
// tools/check_guide.py checks that every snippet appears here unchanged,
// so the guide cannot drift from the API. Each function gives a snippet
// what it uses and checks what it keeps. The stream snippet plays on the
// native backend where one opens with an output, and is only built
// elsewhere; the offline one runs everywhere.

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/layout.h"
#include "maul-audio/notification.h"
#include "maul-audio/offline.h"
#include "maul-audio/stream.h"
#include "maul-audio/voice.h"

#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

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

// The host's state the stream snippet plays from: a 440 Hz tone at
// 48 kHz, and how many samples it has given.
typedef struct Synth
{
    double phase;
    atomic_uint samples;
} Synth;

static float NextSample(Synth* synth)
{
    synth->phase += 2.0 * 3.14159265358979323846 * 440.0 / 48000.0;
    atomic_fetch_add(&synth->samples, 1);
    return (float)(0.1 * sin(synth->phase));
}

// clang-format off
static void Play(const maudStreamBlock* block, void* user)
{
    Synth* synth = user;   // the host's own state
    uint32_t channels = maudGetLayoutChannelCount(block->layout);
    for (uint32_t i = 0; i < block->frameCount; ++i)
    {
        float value = NextSample(synth);
        for (uint32_t c = 0; c < channels; ++c)
        {
            block->output[i * channels + c] = value;
        }
    }
}
// clang-format on

// An offline context (its own output and input) with two more outputs,
// for the snippets that take one.
static maudContext* OfflineContext(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "an offline context");
    const char* names[2] = {"Speakers", "Headphones"};
    for (int i = 0; i < 2 && context != nullptr; ++i)
    {
        maudOfflineDeviceDef device = maudDefaultOfflineDeviceDef();
        device.name = names[i];
        device.nameLength = i == 0 ? 8 : 10;
        maudDeviceId id = {0, 0};
        CHECK(maudAddOfflineDevice(context, &device, &id) == maud_success, "a device");
    }
    return context;
}

static void GuideContext(void)
{
    // clang-format off
    maudContextDef def = maudDefaultContextDef();   // the platform's backend
    maudContext* context = NULL;
    maudResult made = maudCreateContext(&def, &context);
    if (made != maud_success)
    {
        // maud_errorUnsupported: no backend runs here, such as a Linux
        // machine without an audio service.
    }
    // clang-format on
    CHECK(made == maud_success || made == maud_errorUnsupported || made == maud_errorPlatform,
          "the context snippet opens the native backend or says why not");
    if (made == maud_success)
    {
        CHECK(maudDestroyContext(context) == maud_success, "destroy");
    }
}

static void GuideDevices(void)
{
    maudContext* context = OfflineContext();
    // clang-format off
    maudDeviceId ids[16];
    uint32_t count = 0;
    if (maudGetDevices(context, maud_directionOutput, ids, 16, &count) == maud_success)
    {
        for (uint32_t i = 0; i < count && i < 16; ++i)
        {
            char name[256];
            size_t length = 0;
            maudDeviceInfo info;
            if (maudGetDeviceName(context, ids[i], name, sizeof(name), &length) == maud_success &&
                maudGetDeviceInfo(context, ids[i], &info) == maud_success)
            {
                // name holds length bytes, not terminated; info has the native
                // rate and layout, the form (speakers, headphones, ...), and
                // whether it is a default.
            }
        }
    }
    // clang-format on
    CHECK(count == 3, "the devices snippet lists the context's output and the two added");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void GuidePlay(void)
{
    maudContextDef native = maudDefaultContextDef();
    maudContext* context = nullptr;
    if (maudCreateContext(&native, &context) != maud_success)
    {
        return;
    }
    uint32_t outputs = 0;
    bool any =
        maudGetDevices(context, maud_directionOutput, nullptr, 0, &outputs) == maud_success &&
        outputs > 0;
    Synth synth = {0};
    // clang-format off
    maudStreamDef def = maudDefaultStreamDef();   // output, stereo, native rate, the default
    def.callback = Play;
    def.user = &synth;
    maudStreamId stream = {0, 0};
    maudResult result = maudCreateStream(context, &def, &stream);
    if (result == maud_success)
    {
        result = maudStartStream(context, stream);
    }
    maudStreamFormat format;
    if (result == maud_success && maudGetStreamFormat(context, stream, &format) == maud_success)
    {
        // format.sampleRate and format.periodFrames: what the callback sees.
    }
    // clang-format on
    if (any && result == maud_success)
    {
        for (int tries = 0; tries < 200 && atomic_load(&synth.samples) == 0; ++tries)
        {
            maudNotification ignored;
            while (maudNextNotification(context, &ignored) == maud_success)
            {
            }
            Pause(10);
        }
        CHECK(atomic_load(&synth.samples) > 0, "the stream snippet plays");
    }
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void GuideNotifications(void)
{
    maudContext* context = OfflineContext();
    // clang-format off
    maudNotification note;
    while (maudNextNotification(context, &note) == maud_success)
    {
        switch (note.kind)
        {
        case maud_notifyDeviceAdded:
        case maud_notifyDeviceRemoved:
        case maud_notifyDefaultChanged:
            // refresh a device menu
            break;
        case maud_notifyStreamSuspended:
            // note.reason: the device was lost, there is none, or the platform
            // holds audio (a browser before a gesture, an iOS interruption)
            break;
        default:
            break;
        }
    }
    // clang-format on
    maudNotification left;
    CHECK(maudNextNotification(context, &left) == maud_empty,
          "the drain snippet empties the queue");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void GuideOffline(void)
{
    Synth synth = {0};
    // clang-format off
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    maudContext* context = NULL;
    maudResult result = maudCreateContext(&def, &context);
    maudDeviceId speakers = {0, 0};
    maudOfflineDeviceDef deviceDef = maudDefaultOfflineDeviceDef();   // stereo, 48 kHz
    deviceDef.name = "Speakers";
    deviceDef.nameLength = 8;
    if (result == maud_success)
    {
        result = maudAddOfflineDevice(context, &deviceDef, &speakers);
    }
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.mode = maud_modePull;   // the host's thread renders
    streamDef.callback = Play;
    streamDef.user = &synth;
    maudStreamId stream = {0, 0};
    if (result == maud_success)
    {
        result = maudCreateStream(context, &streamDef, &stream);
    }
    if (result == maud_success)
    {
        result = maudStartStream(context, stream);
    }
    float frames[2 * 480];
    if (result == maud_success)
    {
        result = maudRenderStream(context, stream, frames, 480);   // 10 ms
    }
    // clang-format on
    CHECK(result == maud_success && atomic_load(&synth.samples) == 480,
          "the offline snippet renders 10 ms through the callback");
    float loudest = 0.0f;
    for (int i = 0; i < 2 * 480; ++i)
    {
        loudest = fabsf(frames[i]) > loudest ? fabsf(frames[i]) : loudest;
    }
    CHECK(loudest > 0.05f, "of the tone");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static float s_frames[4800];
static float s_played[4800];

static void GuideNoise(void)
{
    float* frames = s_frames;
    uint32_t frameCount = 4800;
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        frames[i] = 0.01f * (float)((i * 2654435761u) >> 16 & 0xFF) / 255.0f;
    }
    // clang-format off
    maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();   // 48 kHz, mono
    maudNoiseSuppressor* suppressor = NULL;
    maudResult result = maudCreateNoiseSuppressor(&def, &suppressor);
    maudNoiseState state;
    if (result == maud_success)
    {
        result = maudSuppressNoise(suppressor, frames, frameCount, &state);   // in place
    }
    // state.speechProbability; the output lags the input by 10 ms.
    maudDestroyNoiseSuppressor(suppressor);
    // clang-format on
    CHECK(result == maud_success && state.frames == frameCount / 480,
          "the noise snippet runs its 10 ms frames through");
}

static void GuideEcho(void)
{
    float* capture = s_frames;
    const float* played = s_played;
    uint32_t frameCount = 4800;
    for (uint32_t i = 0; i < frameCount; ++i)
    {
        s_played[i] = 0.1f * sinf(0.05f * (float)i);
        capture[i] = 0.5f * s_played[i];
    }
    // clang-format off
    maudEchoCancellerDef def = maudDefaultEchoCancellerDef();   // 48 kHz, a 0.2 s path
    maudEchoCanceller* canceller = NULL;
    maudResult result = maudCreateEchoCanceller(&def, &canceller);
    if (result == maud_success)
    {
        result = maudCancelEcho(canceller, capture, played, frameCount, NULL);   // in place
    }
    maudDestroyEchoCanceller(canceller);
    // clang-format on
    CHECK(result == maud_success, "the echo snippet runs a block through");
}

int main(void)
{
    GuideContext();
    GuideDevices();
    GuidePlay();
    GuideNotifications();
    GuideOffline();
    GuideNoise();
    GuideEcho();
    return s_failures == 0 ? 0 : 1;
}
