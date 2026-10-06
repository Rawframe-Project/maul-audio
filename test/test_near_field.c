// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Near-field filters: against the rigid sphere's exact variation at
// points on and off the table's grid (near_field_reference.h, from the
// generator); the identity when the source is at the set's distance;
// stable for every angle, distance, head radius and rate; clamped at
// the table's ends; the table read exactly at and between its points;
// frequencies scaled by the head's radius.

#include "near_field.h"
#include "near_field_reference.h"
#include "near_field_table.h"
#include "test_harness.h"

#include <math.h>

#define PI         3.14159265358979323846
#define HEAD       0.0875f
#define SET_METRES 1.2f

// The filter's gain in dB at a frequency.
static double MagnitudeDb(maudNearFieldFilter filter, double frequency, double rate)
{
    double w = 2.0 * PI * frequency / rate;
    double b0 = (double)filter.b0, b1 = (double)filter.b1, a1 = (double)filter.a1;
    double nr = b0 + b1 * cos(w), ni = -b1 * sin(w);
    double dr = 1.0 + a1 * cos(w), di = -a1 * sin(w);
    return 10.0 * log10((nr * nr + ni * ni) / (dr * dr + di * di));
}

static void TestReference(void)
{
    double worst = 0.0;
    for (int point = 0; point < REFERENCE_POINTS; ++point)
    {
        const double* row = s_reference[point];
        maudNearFieldFilter filter =
            maudNearField((float)row[0], HEAD / (float)row[1], HEAD / SET_METRES, HEAD, 48000.0f);
        for (int f = 0; f < REFERENCE_FREQUENCIES; ++f)
        {
            double error = fabs(MagnitudeDb(filter, s_referenceHz[f], 48000.0) - row[2 + f]);
            worst = error > worst ? error : worst;
        }
    }
    // The fit's own residual near the head (0.48 dB RMS at worst), at a
    // single frequency: 0.85 dB measured, at 85 degrees, 0.12 m, 6 kHz.
    printf("worst error against the sphere: %.3f dB\n", worst);
    CHECK(worst < 1.0, "within 1 dB of the sphere at every reference point");
}

static void TestIdentity(void)
{
    float distances[4] = {0.1f, 0.5f, 1.2f, 30.0f};
    for (int i = 0; i < 4; ++i)
    {
        float inverse = HEAD / distances[i];
        maudNearFieldFilter filter = maudNearField(42.0f, inverse, inverse, HEAD, 44100.0f);
        CHECK(filter.b0 == 1.0f && filter.b1 == filter.a1, "a source at the set's distance");
    }
    maudNearFieldFilter far = maudNearField(120.0f, 0.0f, 0.0f, HEAD, 48000.0f);
    CHECK(far.b0 == 1.0f && far.b1 == far.a1, "both at infinity");
}

// Every filter is stable: the pole inside the unit circle, for angles,
// distances, head radii and rates across their ranges.
static void TestStable(void)
{
    float radii[3] = {0.07f, 0.0875f, 0.11f};
    float rates[4] = {8000.0f, 44100.0f, 96000.0f, 384000.0f};
    bool stable = true;
    for (int angle = -10; angle <= 190; angle += 1)
    {
        for (float metres = 0.05f; metres < 40.0f; metres *= 1.07f)
        {
            for (int r = 0; r < 3; ++r)
            {
                for (int k = 0; k < 4; ++k)
                {
                    maudNearFieldFilter filter = maudNearField(
                        (float)angle, radii[r] / metres, radii[r] / SET_METRES, radii[r], rates[k]);
                    stable = stable && fabsf(filter.a1) < 1.0f && isfinite(filter.b0) &&
                             isfinite(filter.b1);
                }
            }
        }
    }
    CHECK(stable, "stable everywhere");
}

// With the set at infinity, the filter's DC gain is the table's gain:
// exactly at each grid point, and the neighbours' mean between them.
static void TestTableRead(void)
{
    bool exact = true;
    bool between = true;
    const float* axis = maudNearFieldInverseDistances;
    for (int i = 0; i < MAUD_NEAR_FIELD_ANGLES - 1; ++i)
    {
        for (int j = 0; j < MAUD_NEAR_FIELD_DISTANCES - 1; ++j)
        {
            float angle = 5.0f * (float)i;
            double at = MagnitudeDb(maudNearField(angle, axis[j], 0.0f, HEAD, 48000.0f), 0.0, 1.0);
            exact = exact && fabs(at - (double)maudNearFieldTable[i][j][0]) < 1e-3;
            float middle = 0.5f * (axis[j] + axis[j + 1]);
            double mean = 0.25 * ((double)maudNearFieldTable[i][j][0] +
                                  (double)maudNearFieldTable[i][j + 1][0] +
                                  (double)maudNearFieldTable[i + 1][j][0] +
                                  (double)maudNearFieldTable[i + 1][j + 1][0]);
            double half =
                MagnitudeDb(maudNearField(angle + 2.5f, middle, 0.0f, HEAD, 48000.0f), 0.0, 1.0);
            between = between && fabs(half - mean) < 1e-3;
        }
    }
    CHECK(exact, "the table's gain at each of its points");
    CHECK(between, "the mean of four points between them");
}

// Frequencies are normalized by the head's radius: a head twice as
// large at the same inverse distances answers at half the frequency.
static void TestRadius(void)
{
    float set = HEAD / SET_METRES;
    maudNearFieldFilter small = maudNearField(0.0f, HEAD / 0.15f, set, HEAD, 48000.0f);
    maudNearFieldFilter large = maudNearField(0.0f, HEAD / 0.15f, set, 2.0f * HEAD, 48000.0f);
    double worst = 0.0;
    for (double hz = 100.0; hz <= 2000.0; hz *= 1.25)
    {
        double error =
            fabs(MagnitudeDb(small, hz, 48000.0) - MagnitudeDb(large, 0.5 * hz, 48000.0));
        worst = error > worst ? error : worst;
    }
    CHECK(worst < 0.02, "the response scales with the head");
}

static void TestClamps(void)
{
    float set = HEAD / SET_METRES;
    maudNearFieldFilter past = maudNearField(200.0f, HEAD / 0.2f, set, HEAD, 48000.0f);
    maudNearFieldFilter end = maudNearField(180.0f, HEAD / 0.2f, set, HEAD, 48000.0f);
    CHECK(past.b0 == end.b0 && past.b1 == end.b1 && past.a1 == end.a1, "angles past 180");
    maudNearFieldFilter before = maudNearField(-5.0f, HEAD / 0.2f, set, HEAD, 48000.0f);
    maudNearFieldFilter start = maudNearField(0.0f, HEAD / 0.2f, set, HEAD, 48000.0f);
    CHECK(before.b0 == start.b0 && before.a1 == start.a1, "angles below 0");
    maudNearFieldFilter touching = maudNearField(30.0f, 2.0f, set, HEAD, 48000.0f);
    maudNearFieldFilter closest = maudNearField(30.0f, 1.0f / 1.15f, set, HEAD, 48000.0f);
    CHECK(touching.b0 == closest.b0 && touching.a1 == closest.a1,
          "nearer than the table counts as its nearest");
}

int main(void)
{
    TestReference();
    TestIdentity();
    TestStable();
    TestClamps();
    TestTableRead();
    TestRadius();
    return s_failures == 0 ? 0 : 1;
}
