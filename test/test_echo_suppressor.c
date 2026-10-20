// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The echo canceller's suppressor (whitebox): with echo alone, the
// linear stage's output all residual echo, for a minute at 48 kHz, its
// noise estimate decays to its floor and no lower, so that no bin's
// estimate turns subnormal (each multiply on one many times slower);
// the echo is still taken down by the echo floor.

#include "echo_suppressor.h"
#include "real_fft.h"
#include "test_harness.h"

#include <math.h>
#include <stdlib.h>

enum
{
    BLOCK = 512,
    BLOCKS = 6000
};

int main(void)
{
    maudRealFft fft;
    void* plan = malloc(maudRealFftBytes(2 * BLOCK));
    maudInitRealFft(&fft, 2 * BLOCK, plan);
    maudEchoSuppressor s;
    void* memory = malloc(maudEchoSuppressorBytes(BLOCK, 48000.0));
    maudInitEchoSuppressor(&s, BLOCK, 48000.0, -15.0, &fft, memory);
    static float echo[BLOCK];
    static float out[BLOCK];
    // One band over every bin, all of the echo estimate leaking.
    const float leak[1] = {1.0f};
    uint32_t state = 11;
    double in = 0.0;
    double left = 0.0;
    for (uint32_t b = 0; b < BLOCKS; ++b)
    {
        for (uint32_t i = 0; i < BLOCK; ++i)
        {
            state = state * 1664525u + 1013904223u;
            echo[i] = ((float)(state >> 8) / 16777216.0f - 0.5f) * 0.2f;
        }
        maudRunEchoSuppressor(&s, echo, echo, leak, s.bins, out);
        if (b >= BLOCKS - 100)
        {
            for (uint32_t i = 0; i < BLOCK; ++i)
            {
                in += (double)echo[i] * (double)echo[i];
                left += (double)out[i] * (double)out[i];
            }
        }
    }
    uint32_t subnormal = 0;
    double lowest = HUGE_VAL;
    for (uint32_t k = 0; k < s.bins; ++k)
    {
        subnormal += isnormal(s.noise[k]) ? 0u : 1u;
        lowest = fmin(lowest, s.noise[k]);
    }
    CHECK(subnormal == 0 && lowest > 0.0 && lowest < 1e-20,
          "echo alone: the noise estimate decays to its floor, never subnormal");
    if (subnormal != 0)
    {
        printf("echo_suppressor: %u of %u bins subnormal, lowest %.3g\n", subnormal, s.bins,
               lowest);
    }
    double down = 10.0 * log10(left / in);
    CHECK(down < -12.0, "and the echo is taken down to its floor");
    if (!(down < -12.0))
    {
        printf("echo_suppressor: the echo down %.1f dB\n", down);
    }
    free(memory);
    free(plan);
    return s_failures == 0 ? 0 : 1;
}
