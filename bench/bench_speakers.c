// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The speaker paths' cost per 10 ms block at 48 kHz into 7.1.4: 64
// sources panned while they move, and a third-order bed decoded.
// Prints the best of five runs in microseconds per block, each row
// beside its recorded baseline.

// fopen reads the baseline; the C runtime's warning that it is unsafe
// is about the Annex K alternative, which the family does not use.
#define _CRT_SECURE_NO_WARNINGS

#include "baseline.h"

#include "maul-audio/ambisonics.h"
#include "maul-audio/layout.h"
#include "maul-audio/speakers.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#define BLOCK    480
#define BLOCKS   2000
#define SOURCES  64
#define CHANNELS 12
#define BED      16

static float s_in[BLOCK];
static float s_out[CHANNELS][BLOCK];
static float s_bed[BED][BLOCK];

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// Source i's direction at block b: around the listener, a turn in
// about four seconds, at its own height.
static maudPanSource At(int i, int b)
{
    float angle = (float)i * 0.39f + (float)b * 0.016f;
    float height = (float)(i % 5) * 0.2f - 0.4f;
    return (maudPanSource){{cosf(angle), height, sinf(angle)}, 1.0f};
}

static void Pan(void)
{
    maudSpeakerPannerDef def = maudDefaultSpeakerPannerDef();
    def.layout = maud_layout7Point1Point4;
    maudSpeakerPanner* panner = nullptr;
    if (maudCreateSpeakerPanner(&def, &panner) != maud_success)
    {
        return;
    }
    float* out[CHANNELS];
    for (int c = 0; c < CHANNELS; ++c)
    {
        out[c] = s_out[c];
    }
    for (int i = 0; i < BLOCK; ++i)
    {
        s_in[i] = 0.1f * sinf(0.05f * (float)i);
    }
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int b = 0; b < BLOCKS; ++b)
        {
            for (int i = 0; i < SOURCES; ++i)
            {
                maudPanSource from = At(i, b);
                maudPanSource to = At(i, b + 1);
                if (maudSpeakerPanner_Pan(panner, &from, &to, s_in, out, BLOCK) != maud_success)
                {
                    return;
                }
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    double micro = best / BLOCKS * 1e6;
    printf("pan, %d moving sources to 7.1.4   %7.2f us per 10 ms block\n", SOURCES, micro);
    Against("speakers.pan.us", micro, false);
    maudDestroySpeakerPanner(panner);
}

static void Decode(void)
{
    maudSpeakerDecoderDef def = maudDefaultSpeakerDecoderDef();
    def.layout = maud_layout7Point1Point4;
    def.order = 3;
    maudSpeakerDecoder* decoder = nullptr;
    if (maudCreateSpeakerDecoder(&def, &decoder) != maud_success)
    {
        return;
    }
    const float* bed[BED];
    float* out[CHANNELS];
    for (int c = 0; c < BED; ++c)
    {
        bed[c] = s_bed[c];
        for (int i = 0; i < BLOCK; ++i)
        {
            s_bed[c][i] = 0.05f * sinf(0.03f * (float)(i + c));
        }
    }
    for (int c = 0; c < CHANNELS; ++c)
    {
        out[c] = s_out[c];
    }
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int b = 0; b < BLOCKS; ++b)
        {
            if (maudSpeakerDecoder_Decode(decoder, bed, out, BLOCK) != maud_success)
            {
                return;
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    double micro = best / BLOCKS * 1e6;
    printf("decode, third order to 7.1.4      %7.2f us per 10 ms block\n", micro);
    Against("speakers.decode3.us", micro, false);
    maudDestroySpeakerDecoder(decoder);
}

int main(void)
{
    Pan();
    Decode();
    return 0;
}
