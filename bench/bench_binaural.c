// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The binaural effect's cost per source on the shipped SADIE II set at
// 48 kHz (128 taps), in 480-frame blocks (10 ms): a source standing
// still without the near field and with it, and one whose direction
// changes every block, so that it fades in every block; then a source
// encoded into a third-order ambisonic bed, and the bed decoded for both
// ears. Prints the best of five runs as
// microseconds per block and as sources per millisecond of one core for each 10 ms of audio.

#define _CRT_SECURE_NO_WARNINGS

#include "maul-audio/ambisonics.h"
#include "maul-audio/binaural.h"
#include "maul-audio/direct.h"

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
    def.nearField = nearField;
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
            if (maudProcessBinaural(effect, &params, s_in, out, FRAMES) != maud_success)
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
            if (maudDecodeBinaural(decoder, (const float* const*)bed, out, FRAMES) != maud_success)
            {
                return;
            }
        }
        double end = Seconds();
        encode = middle - start < encode ? middle - start : encode;
        decode = end - middle < decode ? end - middle : decode;
    }
    printf("bed, encode a source        %7.2f us per 10 ms block\n", encode / BLOCKS * 1e6);
    printf("bed, decode order 3          %7.2f us per 10 ms block\n", decode / BLOCKS * 1e6);
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
                if (maudProcessDirect(effect, &params, s_in, s_left, FRAMES) != maud_success)
                {
                    return;
                }
            }
            double elapsed = Seconds() - start;
            best = elapsed < best ? elapsed : best;
        }
        printf("direct effect, %-14s %7.2f us per 10 ms block\n", moving ? "moving" : "still",
               best / BLOCKS * 1e6);
    }
    maudDestroyDirectEffect(effect);
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
    for (int i = 0; i < FRAMES; ++i)
    {
        s_in[i] = sinf(0.05f * (float)i);
    }
    const char* names[3] = {"still, no near field", "still", "moving"};
    for (int row = 0; row < 3; ++row)
    {
        double micro = Run(hrtf, row == 2, row != 0);
        printf("binaural, %-20s %7.2f us per 10 ms block, %6.1f sources per ms of a core\n",
               names[row], micro, micro > 0.0 ? 1000.0 / micro : 0.0);
    }
    RunBed(hrtf);
    RunDirect();
    maudDestroyHrtf(hrtf);
    return 0;
}
