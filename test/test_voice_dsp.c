// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The voice parts' shared DSP (whitebox): the exponential integral E1
// within 1e-12 of its value (computed to 60 digits) from 1e-4 to 30, on
// both sides of each change of its series' degree and of its hand-over
// to the continued fraction at 3; a level in dBFS, silence at the floor; a high-pass and a
// band-pass biquad, after a second of noise at 48 kHz, through ten seconds of silence flushed every
// 10 ms: their states never subnormal, and zero at the end.

#include "test_harness.h"
#include "voice_dsp.h"

#include <math.h>

// Noise, then silence: whether a state was ever subnormal, and whether
// it ends at zero.
static bool Decays(maudBiquad* filter)
{
    uint32_t state = 3;
    bool normal = true;
    for (uint32_t n = 0; n < 11u * 48000u; ++n)
    {
        float in = 0.0f;
        if (n < 48000u)
        {
            state = state * 1664525u + 1013904223u;
            in = (float)(state >> 8) / 16777216.0f - 0.5f;
        }
        (void)maudFilter(filter, in);
        if ((n + 1) % 480u == 0)
        {
            maudFlushBiquad(filter);
        }
        normal = normal && fpclassify(filter->z1) != FP_SUBNORMAL &&
                 fpclassify(filter->z2) != FP_SUBNORMAL;
    }
    return normal && filter->z1 == 0.0f && filter->z2 == 0.0f;
}

int main(void)
{
    // Each side of each change of degree (1e-4, 0.01, 0.1, 1) and of the
    // hand-over to the continued fraction (3).
    const double x[18] = {0.0000999, 0.0001, 0.001, 0.0099, 0.01, 0.0999, 0.1, 0.5,  0.999,
                          1.0,       1.001,  2.0,   2.999,  3.0,  3.001,  5.0, 10.0, 30.0};
    const double want[18] = {8.6342251049132859,    8.6332247045747046,     6.331539364136149,
                             4.0478804082456561,    4.0379295765381142,     1.8238292938315386,
                             1.8229239584193906,    0.55977359477616084,    0.21975218202294455,
                             0.21938393439552029,   0.21901642252746886,    0.048900510708061118,
                             0.01306498785267253,   0.013048381094197037,   0.013031796463311481,
                             0.0011482955912753257, 4.1569689296853246e-06, 3.0215520106888124e-15};
    double worst = 0.0;
    for (int i = 0; i < 18; ++i)
    {
        worst = fmax(worst, fabs(maudExpIntegral(x[i]) / want[i] - 1.0));
    }
    printf("E1: worst %.2e relative\n", worst);
    CHECK(worst < 1e-12, "E1 within 1e-12");
    CHECK(maudLevelDbfs(0.0) == MAUD_FLOOR_DBFS && fabsf(maudLevelDbfs(1.0)) < 1e-4f &&
              fabsf(maudLevelDbfs(0.01) + 20.0f) < 1e-4f,
          "levels in dBFS, silence at the floor");
    maudBiquad high = {0};
    maudBiquad band = {0};
    maudDesignHighPass(&high, 80.0, 48000.0);
    maudDesignBandPass(&band, 250.0, 500.0, 48000.0);
    CHECK(Decays(&high) && Decays(&band), "a biquad's states decay to zero, never subnormal");
    // One state tiny and the other not: the flush keeps both.
    maudBiquad held = {.z1 = 0.0f, .z2 = 0.5f};
    maudFlushBiquad(&held);
    maudBiquad held2 = {.z1 = 0.5f, .z2 = 0.0f};
    maudFlushBiquad(&held2);
    CHECK(held.z2 == 0.5f && held2.z1 == 0.5f, "a flush needs both states tiny");
    return s_failures == 0 ? 0 : 1;
}
