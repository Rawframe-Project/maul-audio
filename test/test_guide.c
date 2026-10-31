// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The C snippets of docs/user/, each as written there:
// tools/check_guide.py checks that every snippet appears here or in
// test_guide_device.c unchanged, so the documentation cannot drift from
// the API. Each function gives a snippet what it uses and checks what it
// keeps. These run without a platform: the Device snippets on the
// offline backend, the Spatial ones on the shipped HRTF set; the
// snippets that open the native backend are test_guide_device.c's.

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
// fopen reads the HRTF set; the C runtime's warning that it is unsafe
// is about fopen_s's checks, which a test path does not need.
#define _CRT_SECURE_NO_WARNINGS

#include "test_harness.h"

#include "maul-audio/ambisonics.h"
#include "maul-audio/binaural.h"
#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/direct.h"
#include "maul-audio/hrtf.h"
#include "maul-audio/layout.h"
#include "maul-audio/notification.h"
#include "maul-audio/offline.h"
#include "maul-audio/reverb.h"
#include "maul-audio/stream.h"
#include "maul-audio/voice.h"

#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
        CHECK(maudCreateOfflineDevice(context, &device, &id) == maud_success, "a device");
    }
    return context;
}

static void GuideDevices(void)
{
    maudContext* context = OfflineContext();
    // clang-format off
    maudDeviceId ids[16];
    uint32_t count = 0;
    if (maudContext_GetDevices(context, maud_directionOutput, ids, 16, &count) == maud_success)
    {
        for (uint32_t i = 0; i < count && i < 16; ++i)
        {
            char name[256];
            size_t length = 0;
            maudDeviceInfo info;
            if (maudDevice_GetName(context, ids[i], name, sizeof(name), &length) == maud_success &&
                maudDevice_GetInfo(context, ids[i], &info) == maud_success)
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

static void GuideNotifications(void)
{
    maudContext* context = OfflineContext();
    // clang-format off
    maudNotification note;
    while (maudContext_NextNotification(context, &note) == maud_success)
    {
        switch (note.kind)
        {
        case maud_notifyDeviceAdded:
        case maud_notifyDeviceRemoved:
        case maud_notifyDefaultChanged:
            // refresh a device menu
            break;
        case maud_notifyStreamSuspended:
            // note.reason: the device was lost, there is none, the platform
            // holds audio (a browser before a gesture, an iOS interruption),
            // or it could not open the stream there (retried until it can)
            break;
        default:
            break;
        }
    }
    // clang-format on
    maudNotification left;
    CHECK(maudContext_NextNotification(context, &left) == maud_empty,
          "the drain snippet empties the queue");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

// What a loopback stream heard: its frames and its loudest sample.
typedef struct Heard
{
    uint32_t frames;
    float loudest;
} Heard;

static void Reference(const maudStreamBlock* block, void* user)
{
    Heard* heard = user;
    uint32_t channels = maudGetLayoutChannelCount(block->layout);
    for (uint32_t i = 0; i < block->frameCount * channels; ++i)
    {
        heard->loudest =
            fabsf(block->input[i]) > heard->loudest ? fabsf(block->input[i]) : heard->loudest;
    }
    heard->frames += block->frameCount;
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
        result = maudCreateOfflineDevice(context, &deviceDef, &speakers);
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
        result = maudStream_Start(context, stream);
    }
    float frames[2 * 480];
    if (result == maud_success)
    {
        result = maudStream_Render(context, stream, frames, 480);   // 10 ms
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
    Heard reference = {0};
    // clang-format off
    maudStreamDef loopbackDef = maudDefaultStreamDef();
    loopbackDef.direction = maud_directionLoopback;   // what the speakers play
    loopbackDef.device = speakers;
    loopbackDef.mode = maud_modePull;
    loopbackDef.callback = Reference;
    loopbackDef.user = &reference;
    maudStreamId loopback = {0, 0};
    if (result == maud_success)
    {
        result = maudCreateStream(context, &loopbackDef, &loopback);
    }
    if (result == maud_success)
    {
        result = maudStream_Start(context, loopback);
    }
    if (result == maud_success)
    {
        result = maudStream_Feed(context, loopback, frames, 480);   // what they played
    }
    // clang-format on
    CHECK(result == maud_success && reference.frames == 480 &&
              fabsf(reference.loudest - loudest) < 1e-6f,
          "the loopback snippet hears what the speakers played");
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
        result = maudNoiseSuppressor_Suppress(suppressor, frames, frameCount, &state);   // in place
    }
    // state.speechProbability; the output lags the input by 10 ms.
    maudDestroyNoiseSuppressor(suppressor);
    // clang-format on
    CHECK(result == maud_success && state.frameCount == frameCount / 480,
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
        result = maudEchoCanceller_Cancel(canceller, capture, played, frameCount, NULL);   // in place
    }
    maudDestroyEchoCanceller(canceller);
    // clang-format on
    CHECK(result == maud_success, "the echo snippet runs a block through");
}

static unsigned char s_set[900000];
static float s_mono[480];
static float s_left[480];
static float s_right[480];
static float s_channels[16][480];

// A burst of noise to render, and the shipped set's bytes.
static size_t ReadSet(void)
{
    for (uint32_t i = 0; i < 480; ++i)
    {
        s_mono[i] = 0.2f * ((float)((i * 2654435761u) >> 16 & 0xFF) / 127.5f - 1.0f);
    }
    FILE* file = fopen(MAUD_DATA_DIR "/hrtf/sadie2-ku100-48k.maudhrtf", "rb");
    CHECK(file != nullptr, "the shipped set");
    if (file == nullptr)
    {
        return 0;
    }
    size_t count = fread(s_set, 1, sizeof(s_set), file);
    fclose(file);
    return count;
}

static maudHrtf* GuideHrtf(void)
{
    const unsigned char* bytes = s_set;
    size_t byteCount = ReadSet();
    // clang-format off
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = bytes;          // the file's bytes, the host's to keep or free
    def.byteCount = byteCount;
    maudHrtf* hrtf = NULL;
    maudResult result = maudLoadHrtf(&def, &hrtf);
    // maud_errorFormat: a damaged or foreign file; maud_errorVersion: another version.
    // clang-format on
    CHECK(result == maud_success, "the HRTF snippet loads the shipped set");
    return hrtf;
}

static float Loudest(const float* samples, uint32_t count)
{
    float loudest = 0.0f;
    for (uint32_t i = 0; i < count; ++i)
    {
        loudest = fabsf(samples[i]) > loudest ? fabsf(samples[i]) : loudest;
    }
    return loudest;
}

static void GuideBinaural(const maudHrtf* hrtf)
{
    const float* mono = s_mono;
    float* left = s_left;
    float* right = s_right;
    // clang-format off
    maudBinauralDef def = maudDefaultBinauralDef();   // the near field on
    def.hrtf = hrtf;
    maudBinaural* effect = NULL;
    maudResult result = maudCreateBinaural(&def, &effect);
    maudBinauralParams params = {{2.0f, 0.0f, -1.0f}, 0.5f};   // ahead and right; its gain
    float* ears[2] = {left, right};
    if (result == maud_success)
    {
        result = maudBinaural_Process(effect, &params, mono, ears, 480);
    }
    // clang-format on
    CHECK(result == maud_success && Loudest(right, 480) > Loudest(left, 480),
          "the binaural snippet renders the source louder on the right");
    maudDestroyBinaural(effect);
}

static void GuideBed(const maudHrtf* hrtf)
{
    const float* mono = s_mono;
    float* left = s_left;
    float* right = s_right;
    float* bed[16];
    for (int c = 0; c < 16; ++c)
    {
        bed[c] = s_channels[c];
        for (int i = 0; i < 480; ++i)
        {
            s_channels[c][i] = 0.0f;
        }
    }
    // clang-format off
    uint32_t order = 3;
    maudPanSource was = {{0.0f, 0.0f, -1.0f}, 1.0f};   // where it was at the last call
    maudPanSource now = {{1.0f, 0.0f, -1.0f}, 1.0f};   // and is at this one's end
    maudResult result = maudEncodeAmbisonic(order, &was, &now, mono, bed, 480);
    maudQuaternion head = {0.0f, 0.0f, 0.0f, 1.0f};   // the listener's turn, from its pose
    if (result == maud_success)
    {
        result = maudRotateAmbisonic(bed, 480, order, &head, &head);
    }
    maudBinauralDecoderDef def = maudDefaultBinauralDecoderDef();
    def.hrtf = hrtf;
    def.order = order;
    maudBinauralDecoder* decoder = NULL;
    if (result == maud_success)
    {
        result = maudCreateBinauralDecoder(&def, &decoder);
    }
    float* ears[2] = {left, right};
    if (result == maud_success)
    {
        result = maudBinauralDecoder_Decode(decoder, (const float* const*)bed, ears, 480);
    }
    maudDestroyBinauralDecoder(decoder);
    // clang-format on
    CHECK(result == maud_success && maudGetAmbisonicChannelCount(order) == 16 &&
              Loudest(left, 480) > 0.0f,
          "the bed snippet encodes, turns and decodes a source");
}

static void GuideDirect(void)
{
    const float* mono = s_mono;
    float* filtered = s_left;
    // clang-format off
    maudDirectEffectDef def = maudDefaultDirectEffectDef();
    maudDirectEffect* effect = NULL;
    maudResult result = maudCreateDirectEffect(&def, &effect);
    maudDirectParams params = maudDefaultDirectParams();   // a clear path
    params.distance = 30.0f;          // air absorbs the highs over 30 m
    params.occlusion = 1.0f;          // behind a wall...
    params.transmission[0] = 0.3f;    // ...that lets some lows through
    params.transmission[1] = 0.1f;
    params.transmission[2] = 0.02f;
    if (result == maud_success)
    {
        result = maudDirectEffect_Process(effect, &params, mono, filtered, 480);
    }
    maudDestroyDirectEffect(effect);
    // clang-format on
    CHECK(result == maud_success && Loudest(filtered, 480) < Loudest(mono, 480),
          "the direct snippet quietens the source behind the wall");
}

static void GuideReverb(void)
{
    const float* mono = s_mono;
    float* bed[4] = {s_channels[0], s_channels[1], s_channels[2], s_channels[3]};
    for (int c = 0; c < 4; ++c)
    {
        for (int i = 0; i < 480; ++i)
        {
            bed[c][i] = 0.0f;
        }
    }
    // clang-format off
    maudReverbDef def = maudDefaultReverbDef();   // 48 kHz
    def.maxDelay = 0.1f;                          // the longest send delay it will take, s
    maudReverb* reverb = NULL;
    maudResult result = maudCreateReverb(&def, &reverb);
    maudReverbParams params = {
        .reverbTime = {1.8f, 1.2f, 0.6f},   // seconds to fall 60 dB: lows, mids, highs
        .level = {-12.0f, -12.0f, -18.0f},  // the send's level per band, dB
        .delay = 0.02f,                     // the send's delay, s
    };
    if (result == maud_success)
    {
        result = maudReverb_Process(reverb, &params, mono, bed, 480);   // four channels
    }
    maudDestroyReverb(reverb);
    // clang-format on
    // The room starts some 40 ms after the send (its delay and the
    // network's first echoes), past this one 10 ms block: silent yet.
    CHECK(result == maud_success && Loudest(bed[0], 480) == 0.0f,
          "the reverb snippet runs a block, the room not yet heard");
}

int main(void)
{
    GuideDevices();
    GuideNotifications();
    GuideOffline();
    GuideNoise();
    GuideEcho();
    maudHrtf* hrtf = GuideHrtf();
    if (hrtf != nullptr)
    {
        GuideBinaural(hrtf);
        GuideBed(hrtf);
        maudDestroyHrtf(hrtf);
    }
    GuideDirect();
    GuideReverb();
    return s_failures == 0 ? 0 : 1;
}
