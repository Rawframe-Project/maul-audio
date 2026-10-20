// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The echo canceller's linear stage (whitebox), block 512 at 48 kHz with
// 19 partitions: once adapted on a render of noise, each partition's
// share of the step is its weights' norm plus a tenth of the largest,
// scaled to sum to 0.99 (recomputed from the weights the block starts
// with, within 1e-9); on a render of tones at bins 1 to 200 only, whose
// bins above hold nothing but rounding, against a capture with noise of
// its own there, the weights above stay under half the norm of those
// below: the step's power floor keeps the empty bins from stepping on
// the near end's noise (0.17 to 0.24 over four seeds, what each
// partition's constraint spreads from the bins below; 0.9 to 4.8
// without the floor); after noise, through 3,000 blocks of silence, no
// state is ever subnormal (the recent powers, the paths' windows, the
// weights).

#include "echo_filter.h"
#include "real_fft.h"
#include "test_harness.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    BLOCK = 512,
    PARTITIONS = 19,
    DELAY = 100,
    TONES = 40
};

static float s_render[BLOCK];
static float s_capture[BLOCK];
static float s_error[BLOCK];
static float s_echo[BLOCK];
// The render's history for the echo's delay.
static float s_history[DELAY];

static float Noise(uint32_t* state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state >> 8) / 16777216.0f - 0.5f;
}

// The capture: half the render, DELAY samples late, and near-end noise
// of the given amplitude.
static void Capture(uint32_t* state, float near)
{
    for (uint32_t i = 0; i < BLOCK; ++i)
    {
        float late = i < DELAY ? s_history[i] : s_render[i - DELAY];
        s_capture[i] = 0.5f * late + near * Noise(state);
    }
    memcpy(s_history, s_render + BLOCK - DELAY, DELAY * sizeof(float));
}

typedef struct Filter
{
    maudRealFft fft;
    maudEchoFilter f;
    void* plan;
    void* memory;
} Filter;

static void Open(Filter* x)
{
    x->plan = malloc(maudRealFftBytes(2 * BLOCK));
    maudInitRealFft(&x->fft, 2 * BLOCK, x->plan);
    x->memory = malloc(maudEchoFilterBytes(BLOCK, PARTITIONS, 48000.0));
    maudInitEchoFilter(&x->f, BLOCK, PARTITIONS, 48000.0, &x->fft, x->memory);
    memset(s_history, 0, sizeof(s_history));
}

static void Close(Filter* x)
{
    free(x->memory);
    free(x->plan);
}

static void TestShares(void)
{
    Filter x;
    Open(&x);
    maudEchoFilter* f = &x.f;
    uint32_t state = 5;
    for (uint32_t b = 0; b < 300; ++b)
    {
        for (uint32_t i = 0; i < BLOCK; ++i)
        {
            s_render[i] = 0.2f * Noise(&state);
        }
        Capture(&state, 0.001f);
        if (b == 299)
        {
            // The shares the block's step takes, from its first weights.
            double norms[PARTITIONS];
            double largest = 0.0;
            double sum = 0.0;
            for (uint32_t m = 0; m < PARTITIONS; ++m)
            {
                const float* w = f->background + (size_t)m * 2 * f->bins;
                double e = 0.0;
                for (uint32_t k = 0; k < 2 * f->bins; ++k)
                {
                    e += (double)w[k] * (double)w[k];
                }
                norms[m] = sqrt(e);
                largest = fmax(largest, norms[m]);
            }
            for (uint32_t m = 0; m < PARTITIONS; ++m)
            {
                sum += norms[m] + 0.1 * largest + 1e-12;
            }
            maudRunEchoFilter(f, s_render, s_capture, s_error, s_echo);
            double worst = 0.0;
            double total = 0.0;
            for (uint32_t m = 0; m < PARTITIONS; ++m)
            {
                double want = 0.99 * (norms[m] + 0.1 * largest + 1e-12) / sum;
                worst = fmax(worst, fabs(f->shares[m] / want - 1.0));
                total += f->shares[m];
            }
            CHECK(f->adapted && worst < 1e-9 && fabs(total - 0.99) < 1e-9,
                  "each partition's share: its norm and a tenth of the largest, summing to 0.99");
            if (!(worst < 1e-9))
            {
                printf("echo_filter: a share off by %.3g of itself\n", worst);
            }
            break;
        }
        maudRunEchoFilter(f, s_render, s_capture, s_error, s_echo);
    }
    Close(&x);
}

static void TestEmptyBins(void)
{
    Filter x;
    Open(&x);
    maudEchoFilter* f = &x.f;
    uint32_t state = 9;
    // Tones at bin centres of the transform (whole periods in its 1,024
    // samples), so the render's bins above the last are rounding alone.
    double phase[TONES];
    uint32_t bin[TONES];
    for (uint32_t t = 0; t < TONES; ++t)
    {
        bin[t] = 1 + t * 5;
        phase[t] = 6.283185307179586 * (double)(Noise(&state) + 0.5f);
    }
    uint64_t n = 0;
    for (uint32_t b = 0; b < 500; ++b)
    {
        for (uint32_t i = 0; i < BLOCK; ++i, ++n)
        {
            double v = 0.0;
            for (uint32_t t = 0; t < TONES; ++t)
            {
                double cycles = (double)(((uint64_t)bin[t] * n) % (2 * BLOCK)) / (2.0 * BLOCK);
                v += 0.02 * sin(6.283185307179586 * cycles + phase[t]);
            }
            s_render[i] = (float)v;
        }
        Capture(&state, 0.01f);
        maudRunEchoFilter(f, s_render, s_capture, s_error, s_echo);
    }
    double low = 0.0;
    double high = 0.0;
    for (uint32_t m = 0; m < PARTITIONS; ++m)
    {
        const float* w = f->background + (size_t)m * 2 * f->bins;
        for (uint32_t k = 0; k < f->bins; ++k)
        {
            double e =
                (double)w[2 * k] * (double)w[2 * k] + (double)w[2 * k + 1] * (double)w[2 * k + 1];
            if (k <= 200)
            {
                low += e;
            }
            else if (k > 220)
            {
                high += e;
            }
        }
    }
    double ratio = sqrt(high / low);
    CHECK(low > 0.0 && ratio < 0.5, "the render's empty bins take no steps from the near end");
    if (!(ratio < 0.5))
    {
        printf("echo_filter: the empty bins' weights %.3g of the others'\n", ratio);
    }
    Close(&x);
}

static bool Kept(double x)
{
    return fpclassify(x) != FP_SUBNORMAL;
}

// Whether no state of the filter is subnormal.
static bool AllKept(const maudEchoFilter* f)
{
    bool kept = Kept(f->drop1) && Kept(f->drop2) && Kept(f->noise1) && Kept(f->noise2);
    for (uint32_t k = 0; k < f->bins; ++k)
    {
        kept = kept && Kept(f->errorRecent[k]) && Kept(f->echoRecent[k]);
    }
    size_t spectra = (size_t)PARTITIONS * 2 * f->bins;
    for (size_t i = 0; i < spectra; ++i)
    {
        kept = kept && Kept((double)f->background[i]) && Kept((double)f->foreground[i]);
    }
    return kept;
}

static void TestSilence(void)
{
    Filter x;
    Open(&x);
    maudEchoFilter* f = &x.f;
    uint32_t state = 13;
    bool kept = true;
    for (uint32_t b = 0; b < 3300; ++b)
    {
        for (uint32_t i = 0; i < BLOCK; ++i)
        {
            s_render[i] = b < 300 ? 0.2f * Noise(&state) : 0.0f;
        }
        Capture(&state, b < 300 ? 0.01f : 0.0f);
        maudRunEchoFilter(f, s_render, s_capture, s_error, s_echo);
        kept = kept && (b < 300 || AllKept(f));
    }
    CHECK(kept, "silence after noise: no state ever subnormal");
    Close(&x);
}

int main(void)
{
    TestShares();
    TestEmptyBins();
    TestSilence();
    return s_failures == 0 ? 0 : 1;
}
