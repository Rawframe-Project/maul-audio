// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The voice parts' shared DSP (whitebox): the exponential integral E1
// within 1e-10 of its value (computed to 60 digits) on both sides of 1,
// where its series hands over to its continued fraction, from 0.001 to
// 30; a level in dBFS, silence at the floor.

#include "test_harness.h"
#include "voice_dsp.h"

#include <math.h>

int main(void)
{
    const double x[10] = {0.001, 0.1, 0.5, 0.999, 1.0, 1.001, 2.0, 5.0, 10.0, 30.0};
    const double want[10] = {6.331539364136149,     1.8229239584193906,    0.55977359477616084,
                             0.21975218202294455,   0.21938393439552029,   0.21901642252746886,
                             0.048900510708061118,  0.0011482955912753257, 4.1569689296853246e-06,
                             3.0215520106888124e-15};
    double worst = 0.0;
    for (int i = 0; i < 10; ++i)
    {
        worst = fmax(worst, fabs(maudExpIntegral(x[i]) / want[i] - 1.0));
    }
    printf("E1: worst %.2e relative\n", worst);
    CHECK(worst < 1e-10, "E1 within 1e-10");
    CHECK(maudLevelDbfs(0.0) == MAUD_FLOOR_DBFS && fabsf(maudLevelDbfs(1.0)) < 1e-4f &&
              fabsf(maudLevelDbfs(0.01) + 20.0f) < 1e-4f,
          "levels in dBFS, silence at the floor");
    return s_failures == 0 ? 0 : 1;
}
