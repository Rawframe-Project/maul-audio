// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Two-stage convolution (whitebox): the plans for a short, a middling
// and a one-second response; three channels of a 6000-sample response in
// head blocks of 64 and tail blocks of 512 match direct convolution
// delayed by one head block (within 1e-5 of the largest output), fed in
// uneven chunks; a new response takes over at the tail boundary after
// it is set, the tail crossfaded over the block it computes there and
// the head over its block when the output reaches the same point, after
// which the output is the new response's convolution of all the input;
// nothing set leaves silence, and a reset forgets the input.

#include "staged.h"
#include "test_harness.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    BLOCK = 64,
    CHANNELS = 3,
    LENGTH = 6000,
    FRAMES = 9000,
    SWITCH = 4000
};

static float s_h[2][CHANNELS][LENGTH];
static float s_x[FRAMES];
static float s_y[CHANNELS][FRAMES];
// Each response's convolution split at the head's end: [response][part]
// [channel][frame], delayed by one head block.
static double s_direct[2][2][CHANNELS][FRAMES];

static float Noise(uint32_t* state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state >> 8) / 16777216.0f - 0.5f;
}

static void Direct(uint32_t headFrames)
{
    for (int r = 0; r < 2; ++r)
    {
        for (int c = 0; c < CHANNELS; ++c)
        {
            for (uint32_t n = 0; n < FRAMES; ++n)
            {
                double sums[2] = {0.0, 0.0};
                for (uint32_t j = 0; j < LENGTH && j + BLOCK <= n; ++j)
                {
                    sums[j < headFrames ? 0 : 1] +=
                        (double)s_h[r][c][j] * (double)s_x[n - BLOCK - j];
                }
                s_direct[r][0][c][n] = sums[0];
                s_direct[r][1][c][n] = sums[1];
            }
        }
    }
}

// Feeds s_x in uneven chunks from frame first to last into s_y.
static void Feed(maudStaged* s, uint32_t first, uint32_t last)
{
    uint32_t k = 1;
    for (uint32_t at = first; at < last;)
    {
        uint32_t n = at + k > last ? last - at : k;
        float* out[CHANNELS] = {s_y[0] + at, s_y[1] + at, s_y[2] + at};
        maudRunStaged(s, s_x + at, out, n);
        at += n;
        k = k * 7 % 101 + 1;
    }
}

static void TestPlans(void)
{
    maudStagedPlan second = maudPlanStaged(128, 48000);
    CHECK(second.tailBlock == 2048 && second.headPartitions == 31 && second.headFrames == 3968 &&
              second.tailPartitions == 22,
          "one second at 48 kHz: a head of 31 blocks of 128, a tail of 22 of 2048");
    maudStagedPlan shortOne = maudPlanStaged(128, 300);
    CHECK(shortOne.tailBlock == 0 && shortOne.headPartitions == 3 && shortOne.headFrames == 300,
          "a short response keeps one stage");
    maudStagedPlan middling = maudPlanStaged(BLOCK, LENGTH);
    CHECK(middling.tailBlock == 512 && middling.headPartitions == 15 &&
              middling.headFrames == 960 && middling.tailPartitions == 10,
          "6000 samples in blocks of 64: a tail of 10 blocks of 512");
}

int main(void)
{
    TestPlans();
    uint32_t state = 7;
    for (int r = 0; r < 2; ++r)
    {
        for (int c = 0; c < CHANNELS; ++c)
        {
            for (uint32_t j = 0; j < LENGTH; ++j)
            {
                // A decaying tail, as a room's.
                s_h[r][c][j] = Noise(&state) * expf(-(float)j / 1500.0f);
            }
        }
    }
    for (uint32_t n = 0; n < FRAMES; ++n)
    {
        s_x[n] = Noise(&state);
    }
    maudStagedPlan plan = maudPlanStaged(BLOCK, LENGTH);
    uint32_t t = plan.tailBlock;
    maudStaged* s = malloc(sizeof(maudStaged));
    void* memory = malloc(maudStagedBytes(&plan, CHANNELS));
    size_t floats = maudStagedResponseFloats(&plan, CHANNELS);
    float* responses = malloc(2 * floats * sizeof(float));
    float* scratch = malloc(maudStagedScratchFloats(&plan) * sizeof(float));
    maudInitStaged(s, &plan, CHANNELS, memory);
    for (int r = 0; r < 2; ++r)
    {
        const float* channels[CHANNELS] = {s_h[r][0], s_h[r][1], s_h[r][2]};
        maudCutStagedResponse(s, channels, LENGTH, responses + (size_t)r * floats, scratch);
    }
    Direct(plan.headFrames);

    // Response 0 from the start: taken at the first tail boundary t, and
    // heard whole from 3t on; silent before 2t.
    maudSetStagedResponse(s, responses);
    CHECK(maudStagedSwitching(s), "a response set is a change under way");
    Feed(s, 0, SWITCH);
    CHECK(!maudStagedSwitching(s), "and over once the head has switched");
    maudSetStagedResponse(s, responses + floats);
    Feed(s, SWITCH, FRAMES);
    // Response 1 is taken at the next tail boundary p; both stages fade
    // from p + t, the head over a block and the tail over t.
    uint32_t p = (SWITCH / t + 1) * t;
    double error = 0.0;
    double largest = 0.0;
    bool silent = true;
    for (int c = 0; c < CHANNELS; ++c)
    {
        for (uint32_t n = 0; n < FRAMES; ++n)
        {
            if (n < 2 * t)
            {
                silent = silent && s_y[c][n] == 0.0f;
                continue;
            }
            if (n < 3 * t)
            {
                continue;
            }
            double head = n < p + t ? 0.0 : n < p + t + BLOCK ? ((n - p - t) + 0.5) / BLOCK : 1.0;
            double tail = n < p + t ? 0.0 : n < p + 2 * t ? ((n - p - t) + 0.5) / t : 1.0;
            double expected =
                s_direct[0][0][c][n] + head * (s_direct[1][0][c][n] - s_direct[0][0][c][n]) +
                s_direct[0][1][c][n] + tail * (s_direct[1][1][c][n] - s_direct[0][1][c][n]);
            error = fmax(error, fabs(expected - (double)s_y[c][n]));
            largest = fmax(largest, fabs(expected));
        }
    }
    CHECK(silent, "silent until the first response is heard");
    CHECK(largest > 0.1 && error < 1e-5 * largest,
          "the direct convolution of each response, and the crossfade between them");
    if (!(error < 1e-5 * largest))
    {
        printf("staged: error %.3g against an output of %.3g\n", error, largest);
    }

    // Nothing set: silence once the change is over; a reset forgets the
    // input.
    maudSetStagedResponse(s, nullptr);
    memset(s_y, 0, sizeof(s_y));
    Feed(s, 0, 4 * t);
    bool quiet = true;
    for (int c = 0; c < CHANNELS; ++c)
    {
        for (uint32_t n = 3 * t; n < 4 * t; ++n)
        {
            quiet = quiet && s_y[c][n] == 0.0f;
        }
    }
    CHECK(quiet, "no response, no output");
    maudSetStagedResponse(s, responses);
    Feed(s, 0, 4 * t);
    maudResetStaged(s);
    memset(s_x, 0, sizeof(s_x));
    memset(s_y, 0, sizeof(s_y));
    Feed(s, 0, 4 * t);
    for (int c = 0; c < CHANNELS; ++c)
    {
        for (uint32_t n = 0; n < 4 * t; ++n)
        {
            quiet = quiet && s_y[c][n] == 0.0f;
        }
    }
    CHECK(quiet, "a reset forgets the input");
    free(scratch);
    free(responses);
    free(memory);
    free(s);
    return s_failures == 0 ? 0 : 1;
}
