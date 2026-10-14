// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every refusal ambisonics.h, speakers.h, binaural.h and hrtf.h
// document, each made on its own: defs with one field out of range,
// calls with an order out of range, a value not finite, a frame count
// past the most, or a pointer or channel NULL; the bounds themselves
// taken. The HRTF parts read the shipped set (none on the web).

#include "test_harness.h"

#include "maul-audio/ambisonics.h"
#include "maul-audio/binaural.h"
#include "maul-audio/hrtf.h"
#include "maul-audio/speakers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// fopen reads the shipped set; the C runtime's warning that it is unsafe
// does not apply to a test.
#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    return malloc(size);
}

static const maudAllocator s_half = {Alloc, nullptr, nullptr};

static float s_buffers[16][64];

static void TestAmbisonics(void)
{
    float gains[16];
    maudVector3 ahead = {0.0f, 0.0f, -1.0f};
    maudVector3 lost = {INFINITY, 0.0f, 0.0f};
    CHECK(maudGetAmbisonicGains(1, ahead, gains) == maud_success &&
              maudGetAmbisonicGains(3, ahead, gains) == maud_success,
          "orders 1 and 3");
    CHECK(maudGetAmbisonicGains(0, ahead, gains) == maud_errorInvalid &&
              maudGetAmbisonicGains(4, ahead, gains) == maud_errorInvalid &&
              maudGetAmbisonicGains(1, ahead, nullptr) == maud_errorInvalid &&
              maudGetAmbisonicGains(1, lost, gains) == maud_errorInvalid,
          "gains for orders 0 and 4, nowhere, or a direction not finite");
    maudPanSource source = {ahead, 1.0f};
    maudPanSource badDirection = {lost, 1.0f};
    maudPanSource badGain = {ahead, NAN};
    float* bed[4] = {s_buffers[0], s_buffers[1], s_buffers[2], s_buffers[3]};
    float* holed[4] = {s_buffers[0], nullptr, s_buffers[2], s_buffers[3]};
    const float* in = s_buffers[8];
    CHECK(maudEncodeAmbisonic(1, &source, &source, in, bed, 64) == maud_success, "an encode");
    CHECK(maudEncodeAmbisonic(0, &source, &source, in, bed, 64) == maud_errorInvalid &&
              maudEncodeAmbisonic(4, &source, &source, in, bed, 64) == maud_errorInvalid &&
              maudEncodeAmbisonic(1, nullptr, &source, in, bed, 64) == maud_errorInvalid &&
              maudEncodeAmbisonic(1, &source, nullptr, in, bed, 64) == maud_errorInvalid &&
              maudEncodeAmbisonic(1, &badDirection, &source, in, bed, 64) == maud_errorInvalid &&
              maudEncodeAmbisonic(1, &source, &badGain, in, bed, 64) == maud_errorInvalid &&
              maudEncodeAmbisonic(1, &source, &source, nullptr, bed, 64) == maud_errorInvalid &&
              maudEncodeAmbisonic(1, &source, &source, in, nullptr, 64) == maud_errorInvalid &&
              maudEncodeAmbisonic(1, &source, &source, in, holed, 64) == maud_errorInvalid,
          "an encode with an order out of range, no source, a source not finite, no input, "
          "no bed or a bed channel missing");
    maudQuaternion facing = {0.0f, 0.0f, 0.0f, 1.0f};
    maudQuaternion endless = {0.0f, NAN, 0.0f, 1.0f};
    CHECK(maudRotateAmbisonic(1, &facing, &facing, bed, 64) == maud_success, "a rotation");
    CHECK(maudRotateAmbisonic(0, &facing, &facing, bed, 64) == maud_errorInvalid &&
              maudRotateAmbisonic(1, nullptr, &facing, bed, 64) == maud_errorInvalid &&
              maudRotateAmbisonic(1, &facing, nullptr, bed, 64) == maud_errorInvalid &&
              maudRotateAmbisonic(1, &endless, &facing, bed, 64) == maud_errorInvalid &&
              maudRotateAmbisonic(1, &facing, &endless, bed, 64) == maud_errorInvalid &&
              maudRotateAmbisonic(1, &facing, &facing, nullptr, 64) == maud_errorInvalid &&
              maudRotateAmbisonic(1, &facing, &facing, holed, 64) == maud_errorInvalid,
          "a rotation with an order out of range, no turn or one not finite, or no bed");
}

static void TestSpeakers(void)
{
    maudSpeakerPannerDef def = maudDefaultSpeakerPannerDef();
    def.layout = maud_layout5Point1;
    maudSpeakerPanner* panner = (maudSpeakerPanner*)&panner;
    CHECK(maudCreateSpeakerPanner(nullptr, &panner) == maud_errorInvalid && panner == nullptr,
          "a panner without a def");
    CHECK(maudCreateSpeakerPanner(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudSpeakerPannerDef bad[3] = {def, def, def};
    bad[0].cookie = 0;
    bad[1].layout = maud_layoutNone;
    bad[2].allocator = s_half;
    bool refused = true;
    for (int i = 0; i < 3; ++i)
    {
        panner = (maudSpeakerPanner*)&panner;
        refused = refused && maudCreateSpeakerPanner(&bad[i], &panner) == maud_errorInvalid &&
                  panner == nullptr;
    }
    CHECK(refused, "a panner def without its cookie, a layout or a whole allocator");
    CHECK(maudCreateSpeakerPanner(&def, &panner) == maud_success, "a panner");
    float gains[12];
    maudVector3 ahead = {0.0f, 0.0f, -1.0f};
    maudVector3 lost = {0.0f, 0.0f, NAN};
    CHECK(maudGetSpeakerGains(nullptr, ahead, gains) == maud_errorInvalid &&
              maudGetSpeakerGains(panner, ahead, nullptr) == maud_errorInvalid &&
              maudGetSpeakerGains(panner, lost, gains) == maud_errorInvalid,
          "gains without a panner, nowhere, or a direction not finite");
    maudPanSource source = {ahead, 1.0f};
    maudPanSource badGain = {ahead, INFINITY};
    const float* in = s_buffers[8];
    float* out[6] = {s_buffers[0], s_buffers[1], s_buffers[2],
                     s_buffers[3], s_buffers[4], s_buffers[5]};
    float* holed[6] = {s_buffers[0], s_buffers[1], s_buffers[2],
                       s_buffers[3], nullptr,      s_buffers[5]};
    CHECK(maudPanToSpeakers(panner, &source, &source, in, out, 64) == maud_success, "a pan");
    CHECK(maudPanToSpeakers(nullptr, &source, &source, in, out, 64) == maud_errorInvalid &&
              maudPanToSpeakers(panner, nullptr, &source, in, out, 64) == maud_errorInvalid &&
              maudPanToSpeakers(panner, &source, &badGain, in, out, 64) == maud_errorInvalid &&
              maudPanToSpeakers(panner, &source, &source, nullptr, out, 64) == maud_errorInvalid &&
              maudPanToSpeakers(panner, &source, &source, in, nullptr, 64) == maud_errorInvalid &&
              maudPanToSpeakers(panner, &source, &source, in, holed, 64) == maud_errorInvalid,
          "a pan without a panner, a source, a finite gain, input or every output");
    maudDestroySpeakerPanner(panner);
    maudSpeakerDecoderDef decoderDef = maudDefaultSpeakerDecoderDef();
    decoderDef.layout = maud_layout5Point1;
    maudSpeakerDecoder* decoder = (maudSpeakerDecoder*)&decoder;
    CHECK(maudCreateSpeakerDecoder(nullptr, &decoder) == maud_errorInvalid && decoder == nullptr,
          "a decoder without a def");
    CHECK(maudCreateSpeakerDecoder(&decoderDef, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudSpeakerDecoderDef badDecoders[5] = {decoderDef, decoderDef, decoderDef, decoderDef,
                                            decoderDef};
    badDecoders[0].cookie = 0;
    badDecoders[1].order = 0;
    badDecoders[2].order = 4;
    badDecoders[3].layout = maud_layoutNone;
    badDecoders[4].allocator = s_half;
    refused = true;
    for (int i = 0; i < 5; ++i)
    {
        decoder = (maudSpeakerDecoder*)&decoder;
        refused = refused &&
                  maudCreateSpeakerDecoder(&badDecoders[i], &decoder) == maud_errorInvalid &&
                  decoder == nullptr;
    }
    CHECK(refused, "a decoder def without its cookie, an order in range, a layout or an "
                   "allocator");
    decoderDef.order = 1;
    CHECK(maudCreateSpeakerDecoder(&decoderDef, &decoder) == maud_success, "a decoder");
    float matrix[6 * 4];
    CHECK(maudGetSpeakerDecoderMatrix(nullptr, matrix) == maud_errorInvalid &&
              maudGetSpeakerDecoderMatrix(decoder, nullptr) == maud_errorInvalid,
          "a matrix without a decoder or nowhere to put it");
    const float* bed[4] = {s_buffers[8], s_buffers[9], s_buffers[10], s_buffers[11]};
    const float* holedBed[4] = {s_buffers[8], s_buffers[9], nullptr, s_buffers[11]};
    CHECK(maudDecodeToSpeakers(decoder, bed, out, 64) == maud_success, "a decode");
    CHECK(maudDecodeToSpeakers(nullptr, bed, out, 64) == maud_errorInvalid &&
              maudDecodeToSpeakers(decoder, holedBed, out, 64) == maud_errorInvalid &&
              maudDecodeToSpeakers(decoder, bed, holed, 64) == maud_errorInvalid &&
              maudDecodeToSpeakers(decoder, nullptr, nullptr, 0) == maud_success,
          "a decode without a decoder or a channel, and none of no frames");
    maudDestroySpeakerDecoder(decoder);
}

static void TestHrtfParts(void)
{
#ifdef __EMSCRIPTEN__
    printf("the HRTF parts skipped on the web: no file system\n");
#else
    static unsigned char s_bytes[900000];
    FILE* file = fopen(MAUD_DATA_DIR "/hrtf/sadie2-ku100-48k.maudhrtf", "rb");
    CHECK(file != nullptr, "the shipped set");
    if (file == nullptr)
    {
        return;
    }
    size_t count = fread(s_bytes, 1, sizeof(s_bytes), file);
    fclose(file);
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = s_bytes;
    def.byteCount = count;
    maudHrtf* hrtf = (maudHrtf*)&hrtf;
    CHECK(maudLoadHrtf(nullptr, &hrtf) == maud_errorInvalid && hrtf == nullptr, "no def");
    CHECK(maudLoadHrtf(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudHrtfDef bad[7] = {def, def, def, def, def, def, def};
    bad[0].cookie = 0;
    bad[1].bytes = nullptr;
    bad[2].sampleRate = 7999;
    bad[3].sampleRate = 384001;
    bad[4].maxDirections = 0;
    bad[5].maxTaps = 0;
    bad[6].allocator = s_half;
    bool refused = true;
    for (int i = 0; i < 7; ++i)
    {
        hrtf = (maudHrtf*)&hrtf;
        bool one = maudLoadHrtf(&bad[i], &hrtf) == maud_errorInvalid && hrtf == nullptr;
        if (!one)
        {
            printf("HRTF def %d not refused\n", i);
            maudDestroyHrtf(hrtf);
        }
        refused = refused && one;
    }
    CHECK(refused, "a cookie, bytes, rate, limit or allocator out of range");
    def.sampleRate = 8000;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_success, "8 kHz, the lowest rate, taken");
    maudDestroyHrtf(hrtf);
    def.sampleRate = 0;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_success, "the set");
    maudHrtfInfo info;
    CHECK(maudGetHrtfInfo(nullptr, &info) == maud_errorInvalid &&
              maudGetHrtfInfo(hrtf, nullptr) == maud_errorInvalid,
          "info without a set or nowhere to put it");
    maudBinauralDef effectDef = maudDefaultBinauralDef();
    effectDef.hrtf = hrtf;
    effectDef.maxFrames = 64;
    maudBinaural* effect = (maudBinaural*)&effect;
    CHECK(maudCreateBinaural(nullptr, &effect) == maud_errorInvalid && effect == nullptr,
          "an effect without a def");
    CHECK(maudCreateBinaural(&effectDef, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudBinauralDef badEffects[7] = {effectDef, effectDef, effectDef, effectDef,
                                     effectDef, effectDef, effectDef};
    badEffects[0].cookie = 0;
    badEffects[1].hrtf = nullptr;
    badEffects[2].maxFrames = 0;
    badEffects[3].maxFrames = 16385;
    badEffects[4].headRadius = 0.049f;
    badEffects[5].headRadius = 0.151f;
    badEffects[6].allocator = s_half;
    refused = true;
    for (int i = 0; i < 7; ++i)
    {
        effect = (maudBinaural*)&effect;
        bool one =
            maudCreateBinaural(&badEffects[i], &effect) == maud_errorInvalid && effect == nullptr;
        if (!one)
        {
            printf("binaural def %d not refused\n", i);
            maudDestroyBinaural(effect);
        }
        refused = refused && one;
    }
    CHECK(refused, "an effect def without its cookie, a set, frames or a head in range");
    maudBinauralDef edge = effectDef;
    edge.headRadius = 0.05f;
    CHECK(maudCreateBinaural(&edge, &effect) == maud_success, "the smallest head taken");
    maudDestroyBinaural(effect);
    edge.headRadius = 0.15f;
    CHECK(maudCreateBinaural(&edge, &effect) == maud_success, "the largest head taken");
    maudDestroyBinaural(effect);
    CHECK(maudCreateBinaural(&effectDef, &effect) == maud_success, "an effect");
    maudBinauralParams params = {{1.0f, 0.0f, 0.0f}, 1.0f};
    maudBinauralParams lost = {{1.0f, NAN, 0.0f}, 1.0f};
    maudBinauralParams loud = {{1.0f, 0.0f, 0.0f}, INFINITY};
    const float* in = s_buffers[8];
    float* ears[2] = {s_buffers[0], s_buffers[1]};
    float* oneEar[2] = {s_buffers[0], nullptr};
    CHECK(maudProcessBinaural(effect, &params, in, ears, 64) == maud_success, "a block");
    CHECK(maudProcessBinaural(nullptr, &params, in, ears, 64) == maud_errorInvalid &&
              maudProcessBinaural(effect, nullptr, in, ears, 64) == maud_errorInvalid &&
              maudProcessBinaural(effect, &params, in, nullptr, 64) == maud_errorInvalid &&
              maudProcessBinaural(effect, &params, in, ears, 65) == maud_errorInvalid &&
              maudProcessBinaural(effect, &params, nullptr, ears, 64) == maud_errorInvalid &&
              maudProcessBinaural(effect, &params, in, oneEar, 64) == maud_errorInvalid &&
              maudProcessBinaural(effect, &lost, in, ears, 64) == maud_errorInvalid &&
              maudProcessBinaural(effect, &loud, in, ears, 64) == maud_errorInvalid,
          "a block without an effect, parameters, ears, input or within the frames, or with "
          "a value not finite");
    maudDestroyBinaural(effect);
    maudBinauralDecoderDef decoderDef = maudDefaultBinauralDecoderDef();
    decoderDef.hrtf = hrtf;
    decoderDef.order = 1;
    decoderDef.maxFrames = 64;
    maudBinauralDecoder* decoder = (maudBinauralDecoder*)&decoder;
    CHECK(maudCreateBinauralDecoder(nullptr, &decoder) == maud_errorInvalid && decoder == nullptr,
          "a decoder without a def");
    CHECK(maudCreateBinauralDecoder(&decoderDef, nullptr) == maud_errorInvalid,
          "nowhere to put it");
    maudBinauralDecoderDef badDecoders[7] = {decoderDef, decoderDef, decoderDef, decoderDef,
                                             decoderDef, decoderDef, decoderDef};
    badDecoders[0].cookie = 0;
    badDecoders[1].hrtf = nullptr;
    badDecoders[2].order = 0;
    badDecoders[3].order = 4;
    badDecoders[4].maxFrames = 0;
    badDecoders[5].maxFrames = 16385;
    badDecoders[6].allocator = s_half;
    refused = true;
    for (int i = 0; i < 7; ++i)
    {
        decoder = (maudBinauralDecoder*)&decoder;
        bool one = maudCreateBinauralDecoder(&badDecoders[i], &decoder) == maud_errorInvalid &&
                   decoder == nullptr;
        if (!one)
        {
            printf("binaural decoder def %d not refused\n", i);
            maudDestroyBinauralDecoder(decoder);
        }
        refused = refused && one;
    }
    CHECK(refused, "a decoder def without its cookie, a set, an order or frames in range");
    CHECK(maudCreateBinauralDecoder(&decoderDef, &decoder) == maud_success, "a decoder");
    const float* bed[4] = {s_buffers[8], s_buffers[9], s_buffers[10], s_buffers[11]};
    const float* holedBed[4] = {s_buffers[8], nullptr, s_buffers[10], s_buffers[11]};
    CHECK(maudDecodeBinaural(decoder, bed, ears, 64) == maud_success, "a decode");
    CHECK(maudDecodeBinaural(nullptr, bed, ears, 64) == maud_errorInvalid &&
              maudDecodeBinaural(decoder, bed, ears, 65) == maud_errorInvalid &&
              maudDecodeBinaural(decoder, nullptr, ears, 64) == maud_errorInvalid &&
              maudDecodeBinaural(decoder, bed, nullptr, 64) == maud_errorInvalid &&
              maudDecodeBinaural(decoder, bed, oneEar, 64) == maud_errorInvalid &&
              maudDecodeBinaural(decoder, holedBed, ears, 64) == maud_errorInvalid &&
              maudDecodeBinaural(decoder, nullptr, nullptr, 0) == maud_success,
          "a decode without a decoder, within the frames, a bed or both ears, and none of no "
          "frames");
    maudDestroyBinauralDecoder(decoder);
    maudDestroyHrtf(hrtf);
#endif
}

int main(void)
{
    TestAmbisonics();
    TestSpeakers();
    TestHrtfParts();
    return s_failures == 0 ? 0 : 1;
}
