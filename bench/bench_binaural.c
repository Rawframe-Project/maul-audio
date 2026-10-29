// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The binaural effect's cost per source on the shipped SADIE II set at
// 48 kHz (128 taps), in 480-frame blocks (10 ms): a source standing
// still without the near field and with it, and one whose direction
// changes every block, so that it fades in every block; then a source
// encoded into a third-order ambisonic bed, and the bed decoded for both
// ears; and the set loaded, at its rate and resampled to 44.1 kHz; and
// a near-field source, the direct effect and the reverb on digital
// silence after a signal, their decaying states long settled (a state
// left to decay into the subnormal range shows there first).
// Prints the best of five runs as
// microseconds per block and as sources per millisecond of one core for each 10 ms of audio,
// each row beside its recorded baseline.

#define _CRT_SECURE_NO_WARNINGS

#include "baseline.h"

#include "maul-audio/ambisonics.h"
#include "maul-audio/binaural.h"
#include "maul-audio/direct.h"
#include "maul-audio/reverb.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#define FRAMES 480
#define BLOCKS 20000

static unsigned char s_bytes[900000];
static float s_in[FRAMES];
static float s_left[FRAMES];
static float s_right[FRAMES];

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static double Run(const maudHrtf* hrtf, bool moving, bool nearField)
{
    maudBinauralDef def = maudDefaultBinauralDef();
    def.hrtf = hrtf;
    def.maxFrames = FRAMES;
    def.enableNearField = nearField;
    maudBinaural* effect = nullptr;
    if (maudCreateBinaural(&def, &effect) != maud_success)
    {
        return 0.0;
    }
    float* out[2] = {s_left, s_right};
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int block = 0; block < BLOCKS; ++block)
        {
            float angle = moving ? 0.01f * (float)block : 0.5f;
            maudBinauralParams params = {{sinf(angle), 0.2f, -cosf(angle)}, 1.0f};
            if (maudBinaural_Process(effect, &params, s_in, out, FRAMES) != maud_success)
            {
                return 0.0;
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    maudDestroyBinaural(effect);
    return best / BLOCKS * 1e6;
}

static float s_bed[16][FRAMES];

// The bed's two costs per 10 ms block: encoding one moving source, and
// decoding the bed.
static void RunBed(const maudHrtf* hrtf)
{
    maudBinauralDecoderDef def = maudDefaultBinauralDecoderDef();
    def.hrtf = hrtf;
    def.maxFrames = FRAMES;
    maudBinauralDecoder* decoder = nullptr;
    if (maudCreateBinauralDecoder(&def, &decoder) != maud_success)
    {
        return;
    }
    float* bed[16];
    for (int c = 0; c < 16; ++c)
    {
        bed[c] = s_bed[c];
    }
    float* out[2] = {s_left, s_right};
    double encode = 1e9;
    double decode = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int block = 0; block < BLOCKS; ++block)
        {
            float angle = 0.01f * (float)block;
            maudPanSource from = {{sinf(angle), 0.2f, -cosf(angle)}, 1.0f};
            maudPanSource to = {{sinf(angle + 0.01f), 0.2f, -cosf(angle + 0.01f)}, 1.0f};
            if (maudEncodeAmbisonic(3, &from, &to, s_in, bed, FRAMES) != maud_success)
            {
                return;
            }
        }
        double middle = Seconds();
        for (int block = 0; block < BLOCKS; ++block)
        {
            if (maudBinauralDecoder_Decode(decoder, (const float* const*)bed, out, FRAMES) !=
                maud_success)
            {
                return;
            }
        }
        double end = Seconds();
        encode = middle - start < encode ? middle - start : encode;
        decode = end - middle < decode ? end - middle : decode;
    }
    printf("bed, encode a source        %7.2f us per 10 ms block\n", encode / BLOCKS * 1e6);
    Against("bed.encode.us", encode / BLOCKS * 1e6, false);
    printf("bed, decode order 3          %7.2f us per 10 ms block\n", decode / BLOCKS * 1e6);
    Against("bed.decode3.us", decode / BLOCKS * 1e6, false);
    maudDestroyBinauralDecoder(decoder);
}

// The direct effect per 10 ms block: a still source behind a wall, and
// one swinging between 5 and 50 m at up to 9 m/s, whose air absorption
// keeps changing.
static void RunDirect(void)
{
    maudDirectEffectDef def = maudDefaultDirectEffectDef();
    maudDirectEffect* effect = nullptr;
    if (maudCreateDirectEffect(&def, &effect) != maud_success)
    {
        return;
    }
    for (int moving = 0; moving < 2; ++moving)
    {
        double best = 1e9;
        for (int run = 0; run < 5; ++run)
        {
            maudDirectParams params = maudDefaultDirectParams();
            params.occlusion = 0.8f;
            params.transmission[0] = 0.5f;
            params.transmission[1] = 0.2f;
            params.transmission[2] = 0.05f;
            double start = Seconds();
            for (int block = 0; block < BLOCKS; ++block)
            {
                params.distance = moving ? 27.5f + 22.5f * sinf(0.004f * (float)block) : 20.0f;
                if (maudDirectEffect_Process(effect, &params, s_in, s_left, FRAMES) != maud_success)
                {
                    return;
                }
            }
            double elapsed = Seconds() - start;
            best = elapsed < best ? elapsed : best;
        }
        printf("direct effect, %-14s %7.2f us per 10 ms block\n", moving ? "moving" : "still",
               best / BLOCKS * 1e6);
        Against(moving ? "direct.moving.us" : "direct.still.us", best / BLOCKS * 1e6, false);
    }
    maudDestroyDirectEffect(effect);
}

// The reverb per 10 ms block, still and with its times changing every
// block (a refit and a ramp each time).
static void RunReverb(void)
{
    maudReverbDef def = maudDefaultReverbDef();
    maudReverb* reverb = nullptr;
    if (maudCreateReverb(&def, &reverb) != maud_success)
    {
        return;
    }
    float* bed[4] = {s_bed[0], s_bed[1], s_bed[2], s_bed[3]};
    for (int changing = 0; changing < 2; ++changing)
    {
        double best = 1e9;
        for (int run = 0; run < 3; ++run)
        {
            double start = Seconds();
            for (int block = 0; block < BLOCKS / 10; ++block)
            {
                float t = changing ? 1.0f + 0.001f * (float)(block % 100) : 1.0f;
                maudReverbParams params = {{1.5f * t, t, 0.5f * t},
                                           {0.0f, 0.0f, 0.0f},
                                           0.0f,
                                           {0.0f, 0.0f, 0.0f},
                                           {0.0f, 0.0f, 0.0f}};
                if (maudReverb_Process(reverb, &params, s_in, bed, FRAMES) != maud_success)
                {
                    return;
                }
            }
            double elapsed = Seconds() - start;
            best = elapsed < best ? elapsed : best;
        }
        printf("reverb, %-17s %7.2f us per 10 ms block\n", changing ? "times changing" : "still",
               best / (BLOCKS / 10) * 1e6);
        Against(changing ? "reverb.changing.us" : "reverb.still.us", best / (BLOCKS / 10) * 1e6,
                false);
    }
    maudDestroyReverb(reverb);
}

// The three effects with recursive states, by row.
typedef enum Effect
{
    EFFECT_BINAURAL,
    EFFECT_DIRECT,
    EFFECT_REVERB
} Effect;

typedef struct Effects
{
    maudBinaural* binaural;
    maudDirectEffect* direct;
    maudReverb* reverb;
} Effects;

static maudResult Step(Effects* e, Effect which)
{
    static float s_reverbBed[4][FRAMES];
    float* out[2] = {s_left, s_right};
    float* bed[4] = {s_reverbBed[0], s_reverbBed[1], s_reverbBed[2], s_reverbBed[3]};
    maudBinauralParams binaural = {{0.2f, 0.1f, -0.3f}, 1.0f};
    maudDirectParams direct = maudDefaultDirectParams();
    direct.occlusion = 0.8f;
    direct.transmission[0] = 0.5f;
    direct.transmission[1] = 0.2f;
    direct.transmission[2] = 0.05f;
    direct.distance = 20.0f;
    maudReverbParams reverb = {
        {1.5f, 1.0f, 0.5f}, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    switch (which)
    {
    case EFFECT_BINAURAL:
        return maudBinaural_Process(e->binaural, &binaural, s_in, out, FRAMES);
    case EFFECT_DIRECT:
        return maudDirectEffect_Process(e->direct, &direct, s_in, s_left, FRAMES);
    case EFFECT_REVERB:
        return maudReverb_Process(e->reverb, &reverb, s_in, bed, FRAMES);
    }
    return maud_errorInvalid;
}

// One effect on silence: 3 s of noise, 60 s of zeros for its slowest
// states to settle (a 1.5 s reverb's reach the subnormal range in some
// twenty), then the timed blocks of zeros.
static void RunSilent(const maudHrtf* hrtf, Effect which, const char* name, const char* key)
{
    maudBinauralDef binauralDef = maudDefaultBinauralDef();
    binauralDef.hrtf = hrtf;
    binauralDef.maxFrames = FRAMES;
    binauralDef.enableNearField = true;
    maudDirectEffectDef directDef = maudDefaultDirectEffectDef();
    maudReverbDef reverbDef = maudDefaultReverbDef();
    Effects e = {nullptr, nullptr, nullptr};
    maudResult made = which == EFFECT_BINAURAL ? maudCreateBinaural(&binauralDef, &e.binaural)
                      : which == EFFECT_DIRECT ? maudCreateDirectEffect(&directDef, &e.direct)
                                               : maudCreateReverb(&reverbDef, &e.reverb);
    if (made != maud_success)
    {
        return;
    }
    uint32_t state = 5;
    bool failed = false;
    for (int b = 0; b < 6300 && !failed; ++b)
    {
        for (int i = 0; i < FRAMES; ++i)
        {
            state = state * 1664525u + 1013904223u;
            s_in[i] = b < 300 ? 0.3f * ((float)(state >> 8) / 16777216.0f - 0.5f) : 0.0f;
        }
        failed = Step(&e, which) != maud_success;
    }
    const int blocks = BLOCKS / 10;
    double best = 1e9;
    for (int run = 0; run < 5 && !failed; ++run)
    {
        double start = Seconds();
        for (int b = 0; b < blocks && !failed; ++b)
        {
            failed = Step(&e, which) != maud_success;
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    if (!failed)
    {
        printf("%-30s %7.2f us per 10 ms block\n", name, best / blocks * 1e6);
        Against(key, best / blocks * 1e6, false);
    }
    maudDestroyBinaural(e.binaural);
    maudDestroyDirectEffect(e.direct);
    maudDestroyReverb(e.reverb);
}

// Loading the set from its bytes, at its own rate and resampled to
// 44.1 kHz: the best of five loads, in milliseconds.
static void RunLoad(size_t count)
{
    for (int resampled = 0; resampled < 2; ++resampled)
    {
        maudHrtfDef def = maudDefaultHrtfDef();
        def.bytes = s_bytes;
        def.byteCount = count;
        def.sampleRate = resampled ? 44100 : 0;
        double best = 1e9;
        for (int run = 0; run < 5; ++run)
        {
            maudHrtf* hrtf = nullptr;
            double start = Seconds();
            if (maudLoadHrtf(&def, &hrtf) != maud_success)
            {
                return;
            }
            double elapsed = Seconds() - start;
            best = elapsed < best ? elapsed : best;
            maudDestroyHrtf(hrtf);
        }
        printf("hrtf load, %-24s %7.2f ms\n", resampled ? "resampled to 44.1 kHz" : "at its rate",
               best * 1e3);
        Against(resampled ? "hrtf.load44.ms" : "hrtf.load.ms", best * 1e3, false);
    }
}

int main(void)
{
    FILE* file = fopen(MAUD_DATA_DIR "/hrtf/sadie2-ku100-48k.maudhrtf", "rb");
    if (file == nullptr)
    {
        printf("no data file\n");
        return 1;
    }
    size_t count = fread(s_bytes, 1, sizeof(s_bytes), file);
    fclose(file);
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = s_bytes;
    def.byteCount = count;
    maudHrtf* hrtf = nullptr;
    if (maudLoadHrtf(&def, &hrtf) != maud_success)
    {
        printf("the set does not load\n");
        return 1;
    }
    RunLoad(count);
    for (int i = 0; i < FRAMES; ++i)
    {
        s_in[i] = sinf(0.05f * (float)i);
    }
    const char* names[3] = {"still, no near field", "still", "moving"};
    const char* keys[3] = {"binaural.plain.us", "binaural.still.us", "binaural.moving.us"};
    for (int row = 0; row < 3; ++row)
    {
        double micro = Run(hrtf, row == 2, row != 0);
        printf("binaural, %-20s %7.2f us per 10 ms block, %6.1f sources per ms of a core\n",
               names[row], micro, micro > 0.0 ? 1000.0 / micro : 0.0);
        Against(keys[row], micro, false);
    }
    RunBed(hrtf);
    RunDirect();
    RunReverb();
    RunSilent(hrtf, EFFECT_BINAURAL, "binaural, near field, silent", "binaural.silent.us");
    RunSilent(hrtf, EFFECT_DIRECT, "direct effect, silent", "direct.silent.us");
    RunSilent(hrtf, EFFECT_REVERB, "reverb, silent", "reverb.silent.us");
    maudDestroyHrtf(hrtf);
    return 0;
}
