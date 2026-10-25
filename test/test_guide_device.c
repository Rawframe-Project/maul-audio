// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The C snippets of docs/user/ that open the platform's own backend,
// each as written there (tools/check_guide.py checks it): the context
// snippet opens the native backend or says why not, and the stream
// snippet plays on it. On Linux the sound-server fixture's daemon is the
// platform. Without a native backend with an output the suite is skipped
// (77), unless MAUD_REQUIRE_PIPEWIRE, MAUD_REQUIRE_COREAUDIO or
// MAUD_REQUIRE_AAUDIO says one is there, when it fails instead, as it
// does when the native backend is another than the one required. ALSA
// lists its default PCM with no sound card at all; there the suite skips
// only when the kernel shows no playback PCM and opening a stream on the
// default fails as the platform's (maud_errorPlatform). Any other failure
// is the suite's.

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
// getenv reads the test's switches; the C runtime's warning that it is
// unsafe is about its result's lifetime, which the test does not keep.
#define _CRT_SECURE_NO_WARNINGS

#include "test_harness.h"
#include "test_wait.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/layout.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <math.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__linux__) && !defined(__ANDROID__)
#include <dirent.h>
#include <string.h>
#define GUIDE_ALSA 1
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

// The backend a MAUD_REQUIRE_* switch names; maud_backendNative when no
// switch is set, and the suite may skip.
static maudBackendKind RequiredBackend(void)
{
    if (getenv("MAUD_REQUIRE_PIPEWIRE") != nullptr)
    {
        return maud_backendPipewire;
    }
    if (getenv("MAUD_REQUIRE_COREAUDIO") != nullptr)
    {
        return maud_backendCoreAudio;
    }
    return getenv("MAUD_REQUIRE_AAUDIO") != nullptr ? maud_backendAaudio : maud_backendNative;
}

// Fails the suite when a switch requires a backend and the native
// context chose another: a fallback that plays is not the platform the
// run requires.
static void CheckRequiredBackend(const maudContext* context)
{
    maudBackendKind required = RequiredBackend();
    maudBackendKind chosen = maudGetContextBackend(context);
    if (required != maud_backendNative && chosen != required)
    {
        printf("the native backend is kind %d, and this run requires kind %d\n", (int)chosen,
               (int)required);
    }
    CHECK(required == maud_backendNative || chosen == required,
          "the native backend is the one this run requires");
}

#if defined(GUIDE_ALSA)
// Whether the kernel shows this process a playback PCM, a node
// /dev/snd/pcmC<card>D<device>p, as every sound card's output has.
static bool KernelPlayback(void)
{
    DIR* directory = opendir("/dev/snd");
    bool any = false;
    for (struct dirent* entry = nullptr;
         directory != nullptr && !any && (entry = readdir(directory)) != nullptr;)
    {
        size_t length = strlen(entry->d_name);
        any = length > 4 && strncmp(entry->d_name, "pcmC", 4) == 0 &&
              entry->d_name[length - 1] == 'p';
    }
    if (directory != nullptr)
    {
        closedir(directory);
    }
    return any;
}
#endif

// Whether the native backend opens with an output to play on: it lists
// one, and, where it is ALSA on a machine whose kernel shows no playback
// PCM, a stream on the default output does not fail as the platform's.
static bool NativeWithOutput(void)
{
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    if (maudCreateContext(&def, &context) != maud_success)
    {
        return false;
    }
    uint32_t outputs = 0;
    bool any =
        maudGetDevices(context, maud_directionOutput, nullptr, 0, &outputs) == maud_success &&
        outputs > 0;
#if defined(GUIDE_ALSA)
    if (any && maudGetContextBackend(context) == maud_backendAlsa && !KernelPlayback())
    {
        Synth synth = {0};
        maudStreamDef probe = maudDefaultStreamDef();
        probe.callback = Play;
        probe.user = &synth;
        maudStreamId stream = {0, 0};
        any = maudCreateStream(context, &probe, &stream) != maud_errorPlatform;
    }
#endif
    CHECK(maudDestroyContext(context) == maud_success, "destroy the probe");
    return any;
}

static void Drain(maudContext* context)
{
    maudNotification ignored;
    while (maudNextNotification(context, &ignored) == maud_success)
    {
    }
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
    CHECK(made == maud_success, "the context snippet opens the native backend");
    if (made == maud_success)
    {
        CHECK(maudDestroyContext(context) == maud_success, "destroy");
    }
}

static void GuidePlay(void)
{
    maudContextDef native = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&native, &context) == maud_success, "a native context");
    if (context != nullptr)
    {
        CheckRequiredBackend(context);
    }
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
    CHECK(result == maud_success, "the stream snippet starts a stream");
    // Two seconds at most for the first block: a loaded machine's server
    // starts a stream within a few hundred milliseconds.
    if (result == maud_success)
    {
        POLL_UNTIL(2000, "the stream's first block", Drain(context),
                   atomic_load(&synth.samples) > 0);
    }
    CHECK(atomic_load(&synth.samples) > 0, "the stream snippet plays within two seconds");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

int main(void)
{
    if (!NativeWithOutput())
    {
        if (RequiredBackend() != maud_backendNative)
        {
            printf("FAIL: the platform this run requires has no native backend with an output\n");
            return 1;
        }
        return PlatformAbsent("no native backend with an output here");
    }
    GuideContext();
    GuidePlay();
    return s_failures == 0 ? 0 : 1;
}
