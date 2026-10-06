// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The octave equalizer (whitebox): over six decay curves (flat, falling
// gently and steeply, a measured hall, a narrow bump, a long room) and
// the shortest and longest lines, the equalizer's loss at each octave
// centre gives the requested time within 3 % (measured: 2.0 %), 0.7 % on
// average (measured: 0.49 %).

#include "octave_eq.h"
#include "test_harness.h"

#include <math.h>

#define PI 3.14159265358979323846

static double Db(const maudBiquad* f, double w)
{
    double c1 = cos(w);
    double c2 = cos(2.0 * w);
    double b0 = (double)f->b0;
    double b1 = (double)f->b1;
    double b2 = (double)f->b2;
    double a1 = (double)f->a1;
    double a2 = (double)f->a2;
    double num = b0 * b0 + b1 * b1 + b2 * b2 + 2.0 * (b0 * b1 + b1 * b2) * c1 + 2.0 * b0 * b2 * c2;
    double den = 1.0 + a1 * a1 + a2 * a2 + 2.0 * (a1 + a1 * a2) * c1 + 2.0 * a2 * c2;
    return 10.0 * log10(num / den);
}

static void TestFit(void)
{
    maudOctaveEqSetup setup;
    maudSetupOctaveEq(&setup, 48000.0);
    const double cases[6][MAUD_OCTAVES] = {{1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0},
                                           {2.0, 2.0, 2.0, 1.8, 1.6, 1.4, 1.2, 1.0, 0.7, 0.5},
                                           {2.0, 2.0, 2.0, 1.7, 1.4, 1.1, 0.8, 0.5, 0.35, 0.3},
                                           {3.0, 2.8, 2.68, 2.55, 2.47, 2.5, 2.3, 1.89, 1.4, 1.2},
                                           {0.5, 0.5, 0.6, 1.5, 0.6, 0.5, 0.5, 0.5, 0.5, 0.5},
                                           {8.0, 8.0, 6.0, 4.0, 3.0, 2.5, 2.0, 1.5, 1.0, 0.6}};
    const double lengths[2] = {1104.0, 3313.0};
    double worst = 0.0;
    double sum = 0.0;
    int count = 0;
    for (int c = 0; c < 6; ++c)
    {
        double shape[MAUD_OCTAVES];
        for (int m = 0; m < MAUD_OCTAVES; ++m)
        {
            shape[m] = -60.0 / cases[c][m];
        }
        double solve[MAUD_OCTAVES * MAUD_OCTAVE_POINTS];
        CHECK(maudFitOctaveEq(&setup, shape, solve), "a fit");
        for (int l = 0; l < 2; ++l)
        {
            double targets[MAUD_OCTAVES];
            for (int m = 0; m < MAUD_OCTAVES; ++m)
            {
                targets[m] = shape[m] * lengths[l] / 48000.0;
            }
            maudBiquad filters[MAUD_OCTAVE_FILTERS];
            float gain = 0.0f;
            maudDesignOctaveEq(&setup, solve, targets, filters, &gain);
            for (int m = 0; m < MAUD_OCTAVES; ++m)
            {
                double w = 2.0 * PI * maudOctaveCentre(m) / 48000.0;
                double got = 20.0 * log10((double)gain);
                for (int f = 0; f < MAUD_OCTAVE_FILTERS; ++f)
                {
                    got += Db(&filters[f], w);
                }
                // The time scales as the inverse of the loss per pass.
                double error = fabs(targets[m] / got - 1.0);
                worst = fmax(worst, error);
                sum += error;
                count += 1;
            }
        }
    }
    printf("time at the centres: worst %.2f %%, mean %.2f %%\n", 100.0 * worst,
           100.0 * sum / count);
    CHECK(worst < 0.03, "every octave's time");
    CHECK(sum / count < 0.007, "on average");
}

int main(void)
{
    TestFit();
    return s_failures == 0 ? 0 : 1;
}
