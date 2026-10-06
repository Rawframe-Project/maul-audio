// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The three-band equalizer: the filters realize the prototypes' response
// (an impulse response's DFT against the closed form); the solve meets
// band targets measured densely within the research's 0.135 dB, where
// gains set directly to the targets miss by more than 1 dB; zero targets
// pass the signal unchanged; a ramp ends on the new filters and a step in
// the targets makes no click in a sine; coefficients changing every 16
// samples keep the output bounded; every supported rate holds.

#include "band_eq.h"
#include "test_harness.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846

enum
{
    TAPS = 8192
};

static float s_impulse[TAPS];
static float s_response[TAPS];

static double Level(const float* samples, int count, double hz, double rate)
{
    double re = 0.0;
    double im = 0.0;
    for (int n = 0; n < count; ++n)
    {
        double angle = -2.0 * PI * hz * n / rate;
        re += (double)samples[n] * cos(angle);
        im += (double)samples[n] * sin(angle);
    }
    return 10.0 * log10(re * re + im * im);
}

static void Respond(const maudBandEqSetup* setup, const double gains[3])
{
    maudBandEqFilters filters;
    maudDesignBandEq(setup, gains, &filters);
    maudBandEqState state = {{0.0f}, {0.0f}};
    memset(s_impulse, 0, sizeof(s_impulse));
    s_impulse[0] = 1.0f;
    maudRunBandEq(&state, &filters, &filters, s_impulse, s_response, TAPS);
}

static uint32_t s_seed = 5;

static double Uniform(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (double)(s_seed >> 8) / 16777216.0;
}

// The filters' response at single frequencies against the prototypes'
// closed form (maudBandEqMeans over a band of one point is not exposed,
// so the comparison is through band means of a flat band: the setup's
// own points).
static void TestResponse(void)
{
    maudBandEqSetup setup;
    maudSetupBandEq(&setup, 48000.0f);
    double gains[3] = {-7.0, 4.0, -15.0};
    Respond(&setup, gains);
    double means[3];
    maudBandEqMeans(&setup, gains, means);
    double worst = 0.0;
    for (int b = 0; b < 3; ++b)
    {
        double sum = 0.0;
        for (int p = 0; p < MAUD_BAND_POINTS; ++p)
        {
            double hz = atan(setup.points[b][p]) * 48000.0 / PI;
            sum += Level(s_response, TAPS, hz, 48000.0);
        }
        worst = fmax(worst, fabs(sum / MAUD_BAND_POINTS - means[b]));
    }
    printf("filters against the closed form: %.4f dB\n", worst);
    CHECK(worst < 0.01, "the filters realize the prototypes");
}

// A band's mean dB measured densely, as the research judged the solve.
static double DenseMean(double low, double high, double rate)
{
    double sum = 0.0;
    for (int i = 0; i < 60; ++i)
    {
        double hz = low * pow(high / low, ((double)i + 0.5) / 60.0);
        sum += Level(s_response, TAPS, hz, rate);
    }
    return sum / 60.0;
}

static double WorstError(const maudBandEqSetup* setup, double rate, bool solve, int trials)
{
    double top = fmin(20000.0, 0.45 * rate);
    double worst = 0.0;
    for (int trial = 0; trial < trials; ++trial)
    {
        double targets[3];
        double highest = -1e9;
        for (int b = 0; b < 3; ++b)
        {
            targets[b] = -24.0 * Uniform();
            highest = fmax(highest, targets[b]);
        }
        for (int b = 0; b < 3; ++b)
        {
            targets[b] -= highest;
        }
        double gains[3] = {targets[0], targets[1], targets[2]};
        if (solve)
        {
            maudSolveBandEq(setup, targets, gains);
        }
        Respond(setup, gains);
        double measured[3] = {DenseMean(20.0, 800.0, rate), DenseMean(800.0, 8000.0, rate),
                              DenseMean(8000.0, top, rate)};
        for (int b = 0; b < 3; ++b)
        {
            worst = fmax(worst, fabs(measured[b] - targets[b]));
        }
    }
    return worst;
}

static void TestSolve(void)
{
    maudBandEqSetup setup;
    maudSetupBandEq(&setup, 48000.0f);
    double solved = WorstError(&setup, 48000.0, true, 40);
    s_seed = 5;
    double direct = WorstError(&setup, 48000.0, false, 40);
    printf("band error at 48 kHz: solved %.3f dB, gains set directly %.3f dB\n", solved, direct);
    CHECK(solved < 0.2, "the solve meets band targets");
    CHECK(direct > 1.0, "where gains set directly do not");
    const float rates[] = {32000.0f, 44100.0f, 96000.0f, 192000.0f, 384000.0f};
    for (size_t i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
    {
        maudSetupBandEq(&setup, rates[i]);
        double error = WorstError(&setup, (double)rates[i], true, 8);
        printf("band error at %.0f Hz: %.3f dB\n", (double)rates[i], error);
        CHECK(error < 0.3, "at every supported rate");
    }
}

static void TestFlat(void)
{
    maudBandEqSetup setup;
    maudSetupBandEq(&setup, 48000.0f);
    double targets[3] = {0.0, 0.0, 0.0};
    double gains[3];
    maudSolveBandEq(&setup, targets, gains);
    CHECK(fabs(gains[0]) + fabs(gains[1]) + fabs(gains[2]) < 1e-9, "flat targets, flat filters");
    Respond(&setup, gains);
    bool through = fabsf(s_response[0] - 1.0f) < 1e-6f;
    for (int n = 1; n < 64; ++n)
    {
        through = through && fabsf(s_response[n]) < 1e-6f;
    }
    CHECK(through, "and the signal passes unchanged");
}

// The bell spans the middle band: alone at +12 dB it peaks at the band's
// geometric centre and is near half its gain at the band's edges.
static void TestBell(void)
{
    maudBandEqSetup setup;
    maudSetupBandEq(&setup, 48000.0f);
    double gains[3] = {0.0, 12.0, 0.0};
    Respond(&setup, gains);
    double low = Level(s_response, TAPS, 800.0, 48000.0);
    double centre = Level(s_response, TAPS, sqrt(800.0 * 8000.0), 48000.0);
    double high = Level(s_response, TAPS, 8000.0, 48000.0);
    printf("bell: %.2f dB at 800 Hz, %.2f at the centre, %.2f at 8 kHz\n", low, centre, high);
    CHECK(fabs(centre - 12.0) < 0.01, "the bell peaks at the middle band's centre");
    CHECK(fabs(low - 6.0) < 0.7 && fabs(high - 6.0) < 0.7, "and spans the band");
}

// A sine at 3 kHz through a step from flat to a -24 dB tilt across one
// call: the ramp ends on the new filters (a later call with them
// matches one that ran them throughout, once the old state has decayed)
// and no sample-to-sample step exceeds the sine's own.
static void TestRamp(void)
{
    maudBandEqSetup setup;
    maudSetupBandEq(&setup, 48000.0f);
    double flat[3] = {0.0, 0.0, 0.0};
    double tilt[3] = {0.0, -12.0, -24.0};
    double gains[3];
    maudSolveBandEq(&setup, tilt, gains);
    maudBandEqFilters before;
    maudBandEqFilters after;
    maudDesignBandEq(&setup, flat, &before);
    maudDesignBandEq(&setup, gains, &after);
    static float in[4800];
    static float out[4800];
    for (int n = 0; n < 4800; ++n)
    {
        in[n] = (float)sin(2.0 * PI * 3000.0 * n / 48000.0);
    }
    maudBandEqState state = {{0.0f}, {0.0f}};
    maudRunBandEq(&state, &before, &before, in, out, 960);
    maudRunBandEq(&state, &before, &after, in + 960, out + 960, 480);
    maudRunBandEq(&state, &after, &after, in + 1440, out + 1440, 3360);
    float step = 0.0f;
    for (int n = 1; n < 4800; ++n)
    {
        step = fmaxf(step, fabsf(out[n] - out[n - 1]));
    }
    float sineStep = (float)(2.0 * sin(PI * 3000.0 / 48000.0));
    printf("largest step %.4f against the sine's %.4f\n", (double)step, (double)sineStep);
    CHECK(step <= sineStep * 1.001f, "no click");
    static float reference[4800];
    maudBandEqState fresh = {{0.0f}, {0.0f}};
    maudRunBandEq(&fresh, &after, &after, in, reference, 4800);
    float apart = 0.0f;
    for (int n = 4000; n < 4800; ++n)
    {
        apart = fmaxf(apart, fabsf(out[n] - reference[n]));
    }
    CHECK(apart < 1e-4f, "the ramp ends on the new filters");
    // The ramp's first segment is still close to the old filters, its
    // last is the new ones: one call of a single segment from the same
    // state runs the new filters exactly.
    maudBandEqState a = {{0.0f}, {0.0f}};
    maudBandEqState b = {{0.0f}, {0.0f}};
    float rampOut[8];
    float newOut[8];
    float oldOut[8];
    maudRunBandEq(&a, &before, &before, in, out, 960);
    b = a;
    maudBandEqState c = a;
    maudRunBandEq(&a, &before, &after, in + 960, rampOut, 8);
    maudRunBandEq(&b, &after, &after, in + 960, newOut, 8);
    maudRunBandEq(&c, &before, &before, in + 960, oldOut, 8);
    float single = 0.0f;
    for (int n = 0; n < 8; ++n)
    {
        single = fmaxf(single, fabsf(rampOut[n] - newOut[n]));
    }
    CHECK(single < 1e-6f, "a one-segment ramp ends on the new filters");
    maudBandEqState d = c;
    static float rampLong[480];
    static float oldLong[480];
    maudRunBandEq(&c, &before, &after, in + 968, rampLong, 480);
    maudRunBandEq(&d, &before, &before, in + 968, oldLong, 480);
    float first = 0.0f;
    float whole = 0.0f;
    for (int n = 0; n < 8; ++n)
    {
        first = fmaxf(first, fabsf(rampLong[n] - oldLong[n]));
    }
    for (int n = 0; n < 480; ++n)
    {
        whole = fmaxf(whole, fabsf(rampLong[n] - oldLong[n]));
    }
    printf("ramp's first segment %.4f from the old filters, the call %.4f\n", (double)first,
           (double)whole);
    CHECK(first < 0.1f * whole, "a longer ramp starts close to the old filters");
}

// Gains jumping across the whole range every 16 samples, under noise.
static void TestModulation(void)
{
    maudBandEqSetup setup;
    maudSetupBandEq(&setup, 48000.0f);
    maudBandEqFilters filters[2];
    double gains[3] = {0.0, 0.0, 0.0};
    maudDesignBandEq(&setup, gains, &filters[0]);
    maudBandEqState state = {{0.0f}, {0.0f}};
    float peak = 0.0f;
    for (int block = 0; block < 3000; ++block)
    {
        for (int b = 0; b < 3; ++b)
        {
            gains[b] = -30.0 * Uniform();
        }
        maudDesignBandEq(&setup, gains, &filters[(block + 1) % 2]);
        float in[16];
        float out[16];
        for (int n = 0; n < 16; ++n)
        {
            in[n] = (float)(2.0 * Uniform() - 1.0);
        }
        maudRunBandEq(&state, &filters[block % 2], &filters[(block + 1) % 2], in, out, 16);
        for (int n = 0; n < 16; ++n)
        {
            peak = isfinite(out[n]) ? fmaxf(peak, fabsf(out[n])) : INFINITY;
        }
    }
    printf("peak under modulation %.3f\n", (double)peak);
    CHECK(peak < 4.0f, "modulation keeps the output bounded");
}

int main(void)
{
    TestResponse();
    TestSolve();
    TestFlat();
    TestBell();
    TestRamp();
    TestModulation();
    return s_failures == 0 ? 0 : 1;
}
