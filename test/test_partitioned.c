// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Partitioned convolution (whitebox): two channels of a 1000-sample
// response in blocks of 64 match direct convolution delayed by one block
// (within 1e-5 of the largest output, measured 6e-7), fed in uneven
// chunks; an impulse comes out exactly one block late; a new response
// takes over across one block, faded linearly from the old one's
// convolution to its own, after which the output is the new
// response's convolution of all the input, history included; without a
// response the output is silent, and a reset forgets the input.

#include "partitioned.h"
#include "test_harness.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    BLOCK = 64,
    PARTITIONS = 16,
    CHANNELS = 2,
    LENGTH = 1000,
    FRAMES = 5000
};

static float s_h[2][CHANNELS][LENGTH];
static float s_x[FRAMES];
static float s_y[CHANNELS][FRAMES];
static float* s_responses[2];
static float s_scratch[4 * BLOCK + 2];

static maudPartitioned* Make(void)
{
    maudPartitioned* p = malloc(sizeof(maudPartitioned));
    maudInitPartitioned(p, BLOCK, PARTITIONS, CHANNELS,
                        malloc(maudPartitionedBytes(BLOCK, PARTITIONS, CHANNELS)));
    for (int r = 0; r < 2; ++r)
    {
        const float* channels[CHANNELS] = {s_h[r][0], s_h[r][1]};
        maudPartitionResponse(p, channels, LENGTH, s_responses[r], s_scratch);
    }
    return p;
}

static void Free(maudPartitioned* p)
{
    // The plan's memory starts the convolver's block.
    free(p->fft.twiddles);
    free(p);
}

// Feeds s_x in uneven chunks from frame first to last into s_y.
static void Feed(maudPartitioned* p, uint32_t first, uint32_t last)
{
    uint32_t k = 1;
    for (uint32_t at = first; at < last;)
    {
        uint32_t n = at + k > last ? last - at : k;
        float* out[CHANNELS] = {s_y[0] + at, s_y[1] + at};
        maudRunPartitioned(p, s_x + at, out, n);
        at += n;
        k = k * 7 % 101 + 1;
    }
}

// The largest difference from direct convolution with response r, over
// frames [first, FRAMES), and the largest output.
static void Compare(int r, uint32_t first, double* error, double* largest)
{
    *error = 0.0;
    *largest = 0.0;
    for (int c = 0; c < CHANNELS; ++c)
    {
        for (uint32_t n = first; n < FRAMES; ++n)
        {
            double sum = 0.0;
            for (uint32_t j = 0; j < LENGTH && j + BLOCK <= n; ++j)
            {
                sum += (double)s_h[r][c][j] * (double)s_x[n - BLOCK - j];
            }
            *error = fmax(*error, fabs(sum - (double)s_y[c][n]));
            *largest = fmax(*largest, fabs(sum));
        }
    }
}

static void TestDirect(void)
{
    maudPartitioned* p = Make();
    memset(s_y, 0, sizeof(s_y));
    maudSetPartitionedResponse(p, s_responses[0]);
    Feed(p, 0, FRAMES);
    double error = 0.0;
    double largest = 0.0;
    // The response is in use from the first block's end on.
    Compare(0, 2 * BLOCK, &error, &largest);
    printf("direct: error %.2e of %.2e\n", error, largest);
    CHECK(error < 1e-5 * largest, "direct convolution, one block late");
    Free(p);
}

static void TestLatency(void)
{
    maudPartitioned* p = Make();
    maudSetPartitionedResponse(p, s_responses[0]);
    float zero[BLOCK] = {0};
    float scratch[CHANNELS][BLOCK] = {{0}};
    float* out[CHANNELS] = {scratch[0], scratch[1]};
    // A first block in silence brings the response in.
    maudRunPartitioned(p, zero, out, BLOCK);
    float impulse[3 * BLOCK] = {0};
    impulse[0] = 1.0f;
    static float y[CHANNELS][3 * BLOCK];
    memset(y, 0, sizeof(y));
    float* to[CHANNELS] = {y[0], y[1]};
    maudRunPartitioned(p, impulse, to, 3 * BLOCK);
    double error = 0.0;
    for (int i = 0; i < 3 * BLOCK; ++i)
    {
        double want = i < BLOCK ? 0.0 : (double)s_h[0][1][i - BLOCK];
        error = fmax(error, fabs((double)y[1][i] - want));
    }
    CHECK(error < 1e-5, "an impulse one block late");
    Free(p);
}

static void TestSwap(void)
{
    maudPartitioned* p = Make();
    memset(s_y, 0, sizeof(s_y));
    maudSetPartitionedResponse(p, s_responses[0]);
    Feed(p, 0, 2000);
    maudSetPartitionedResponse(p, s_responses[1]);
    Feed(p, 2000, FRAMES);
    double error = 0.0;
    double largest = 0.0;
    // The swap is done by the end of the block after frame 2000.
    Compare(1, 2000 + 2 * BLOCK, &error, &largest);
    CHECK(error < 1e-5 * largest, "the new response, with all the history");
    bool finite = true;
    for (uint32_t n = 1900; n < 2200; ++n)
    {
        finite = finite && isfinite(s_y[0][n]) && fabs((double)s_y[0][n]) < 4.0 * largest;
    }
    CHECK(finite, "a bounded crossfade");
    // The block the swap happens in (output frames 2048 to 2112, the
    // first block boundary after frame 2000) fades linearly from the old
    // response's convolution to the new one's.
    double fade = 0.0;
    for (uint32_t i = 0; i < BLOCK; ++i)
    {
        uint32_t n = 2048 + i;
        double old = 0.0;
        double now = 0.0;
        for (uint32_t j = 0; j < LENGTH && j + BLOCK <= n; ++j)
        {
            old += (double)s_h[0][0][j] * (double)s_x[n - BLOCK - j];
            now += (double)s_h[1][0][j] * (double)s_x[n - BLOCK - j];
        }
        double w = ((double)i + 0.5) / BLOCK;
        fade = fmax(fade, fabs(old + w * (now - old) - (double)s_y[0][n]));
    }
    CHECK(fade < 1e-5 * largest, "a linear crossfade over the swap's block");
    maudSetPartitionedResponse(p, nullptr);
    memset(s_y, 0, sizeof(s_y));
    Feed(p, 0, 4 * BLOCK);
    bool silent = true;
    for (uint32_t n = 2 * BLOCK; n < 4 * BLOCK; ++n)
    {
        silent = silent && s_y[0][n] == 0.0f && s_y[1][n] == 0.0f;
    }
    CHECK(silent, "no response, no output");
    maudSetPartitionedResponse(p, s_responses[0]);
    Feed(p, 0, 4 * BLOCK);
    maudResetPartitioned(p);
    memset(s_y, 0, sizeof(s_y));
    float zero[FRAMES] = {0};
    float* out[CHANNELS] = {s_y[0], s_y[1]};
    maudRunPartitioned(p, zero, out, 4 * BLOCK);
    silent = true;
    for (uint32_t n = 0; n < 4 * BLOCK; ++n)
    {
        silent = silent && s_y[0][n] == 0.0f;
    }
    CHECK(silent, "a reset forgets the input");
    Free(p);
}

int main(void)
{
    for (int r = 0; r < 2; ++r)
    {
        s_responses[r] = malloc(maudResponseFloats(BLOCK, PARTITIONS, CHANNELS) * sizeof(float));
        for (int c = 0; c < CHANNELS; ++c)
        {
            for (int i = 0; i < LENGTH; ++i)
            {
                s_h[r][c][i] = (float)(sin(i * (0.37 + c + 0.11 * r)) * exp(-i / 300.0));
            }
        }
    }
    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        s_x[i] = (float)((i * 2654435761u) >> 16 & 0xFFFFu) / 65536.0f - 0.5f;
    }
    TestDirect();
    TestLatency();
    TestSwap();
    free(s_responses[0]);
    free(s_responses[1]);
    return s_failures == 0 ? 0 : 1;
}
