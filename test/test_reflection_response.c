// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Responses from fields (whitebox): a field whose energy is the same in
// every band gives W the bins' energy (within 3 % over a second); bands
// decaying at 1.6, 1.0 and 0.4 s give every octave from 125 Hz to 16 kHz
// its interpolated time within 8 % (eighth-order Butterworth octaves, as
// the reverb's tests); a field from one direction gives every channel W
// times that direction's harmonic, sample for sample, a silent band
// included; the same field
// gives the same response; a silent field a silent one.

#include "reflection_response.h"
#include "spherical_harmonics.h"
#include "test_harness.h"

#include <math.h>
#include <stdlib.h>

#define PI   3.14159265358979323846
#define RATE 48000.0

enum
{
    BINS = 300,
    FRAMES = 300 * 480
};

static float s_field[16 * 3 * BINS];
static float s_out[16][FRAMES];
static double* s_scratch;

static void Decaying(const double* times, uint32_t channels, const float* gains)
{
    for (uint32_t c = 0; c < channels; ++c)
    {
        for (int b = 0; b < 3; ++b)
        {
            for (int i = 0; i < BINS; ++i)
            {
                double t = (i + 0.5) * 0.01;
                s_field[(c * 3 + (uint32_t)b) * BINS + (uint32_t)i] =
                    gains[c] * (float)(1e-3 * pow(10.0, -6.0 * t / times[b]));
            }
        }
    }
}

static void Build(uint32_t order)
{
    float* out[16];
    for (int c = 0; c < 16; ++c)
    {
        out[c] = s_out[c];
    }
    maudReconstructResponse(s_field, order, BINS, RATE, out, FRAMES, s_scratch);
}

static void TestEnergy(void)
{
    const double times[3] = {1.0, 1.0, 1.0};
    const float one[1] = {1.0f};
    Decaying(times, 1, one);
    Build(0);
    double got = 0.0;
    double want = 0.0;
    for (int i = 10; i < 110; ++i)
    {
        want += (double)s_field[i];
        for (int k = 0; k < 480; ++k)
        {
            got += (double)s_out[0][i * 480 + k] * (double)s_out[0][i * 480 + k];
        }
    }
    printf("energy over bins 10 to 110: %.4g of %.4g\n", got, want);
    CHECK(fabs(got / want - 1.0) < 0.03, "W carries the bins' energy");
}

static double T30(const float* signal, double hz)
{
    static double band[FRAMES];
    for (int i = 0; i < FRAMES; ++i)
    {
        band[i] = (double)signal[i];
    }
    for (int k = 0; k < 4; ++k)
    {
        double q = 1.0 / (2.0 * cos(PI * (2.0 * k + 1.0) / 16.0));
        for (int edge = 0; edge < 2; ++edge)
        {
            bool high = edge == 0;
            double fc = high ? hz / sqrt(2.0) : hz * sqrt(2.0);
            if (!high && fc >= 0.45 * RATE)
            {
                continue;
            }
            double w = 2.0 * PI * fc / RATE;
            double alpha = sin(w) / (2.0 * q);
            double c = cos(w);
            double a0 = 1.0 + alpha;
            double e = high ? (1.0 + c) / 2.0 : (1.0 - c) / 2.0;
            double b0 = e / a0;
            double b1 = (high ? -2.0 : 2.0) * e / a0;
            double a1 = -2.0 * c / a0;
            double a2 = (1.0 - alpha) / a0;
            double s1 = 0.0;
            double s2 = 0.0;
            for (int i = 0; i < FRAMES; ++i)
            {
                double x = band[i];
                double y = b0 * x + s1;
                s1 = b1 * x - a1 * y + s2;
                s2 = b0 * x - a2 * y;
                band[i] = y;
            }
        }
    }
    static double decay[FRAMES];
    double sum = 0.0;
    for (int i = FRAMES; i-- > 0;)
    {
        sum += band[i] * band[i];
        decay[i] = sum;
    }
    int start = -1;
    int end = -1;
    for (int i = 0; i < FRAMES && end < 0; ++i)
    {
        double db = 10.0 * log10(decay[i] / sum + 1e-30);
        start = start < 0 && db < -5.0 ? i : start;
        end = db < -35.0 ? i : end;
    }
    double st = 0.0;
    double sd = 0.0;
    double stt = 0.0;
    double std = 0.0;
    double n = end - start;
    for (int i = start; i < end; ++i)
    {
        double t = i / RATE;
        double db = 10.0 * log10(decay[i] / sum);
        st += t;
        sd += db;
        stt += t * t;
        std += t * db;
    }
    return -60.0 / ((n * std - st * sd) / (n * stt - st * st));
}

static void TestDecay(void)
{
    const double times[3] = {1.6, 1.0, 0.4};
    const float one[1] = {1.0f};
    Decaying(times, 1, one);
    Build(0);
    const double centres[3] = {log(sqrt(16000.0)), log(sqrt(6.4e6)), log(sqrt(1.6e8))};
    double worst = 0.0;
    printf("octave errors %%:");
    for (int k = 2; k <= 9; ++k)
    {
        double hz = 31.25 * pow(2.0, k);
        double l = log(hz);
        double want = l <= centres[0]   ? times[0]
                      : l >= centres[2] ? times[2]
                      : l < centres[1]
                          ? exp(log(times[0]) + (l - centres[0]) / (centres[1] - centres[0]) *
                                                    (log(times[1]) - log(times[0])))
                          : exp(log(times[1]) + (l - centres[1]) / (centres[2] - centres[1]) *
                                                    (log(times[2]) - log(times[1])));
        double error = T30(s_out[0], hz) / want - 1.0;
        printf(" %+.0f", 100.0 * error);
        worst = fmax(worst, fabs(error));
    }
    printf("\n");
    CHECK(worst < 0.08, "each octave decays at its time");
}

static void TestDirection(void)
{
    const double times[3] = {1.2, 0.8, 0.5};
    float g[16];
    maudSphericalHarmonics(3, 0.36f, -0.48f, 0.8f, g);
    Decaying(times, 16, g);
    Build(3);
    double worst = 0.0;
    double scale = 0.0;
    for (int c = 0; c < 16; ++c)
    {
        for (int i = 0; i < FRAMES; i += 7)
        {
            worst = fmax(worst, fabs((double)s_out[c][i] - (double)g[c] * (double)s_out[0][i]));
            scale = fmax(scale, fabs((double)s_out[0][i]));
        }
    }
    CHECK(scale > 0.0 && worst < 1e-5 * scale, "each channel W times its harmonic");
    // The top band silent: its share stays out of every channel alike.
    for (uint32_t c = 0; c < 16; ++c)
    {
        for (int i = 0; i < BINS; ++i)
        {
            s_field[(c * 3 + 2) * BINS + (uint32_t)i] = 0.0f;
        }
    }
    Build(3);
    worst = 0.0;
    for (int c = 0; c < 16; ++c)
    {
        for (int i = 0; i < FRAMES; i += 7)
        {
            worst = fmax(worst, fabs((double)s_out[c][i] - (double)g[c] * (double)s_out[0][i]));
        }
    }
    CHECK(worst < 1e-5 * scale, "a silent band changes no channel's direction");
    static float first[FRAMES];
    for (int i = 0; i < FRAMES; ++i)
    {
        first[i] = s_out[5][i];
    }
    Build(3);
    bool same = true;
    for (int i = 0; i < FRAMES; ++i)
    {
        same = same && first[i] == s_out[5][i];
    }
    CHECK(same, "the same field, the same response");
}

static void TestSilence(void)
{
    for (size_t i = 0; i < sizeof(s_field) / sizeof(s_field[0]); ++i)
    {
        s_field[i] = 0.0f;
    }
    s_out[0][100] = 3.0f;
    Build(1);
    bool silent = true;
    for (int c = 0; c < 4; ++c)
    {
        for (int i = 0; i < FRAMES; ++i)
        {
            silent = silent && s_out[c][i] == 0.0f;
        }
    }
    CHECK(silent, "a silent field, a silent response");
}

int main(void)
{
    s_scratch = malloc(maudResponseScratch(RATE) * sizeof(double));
    CHECK(maudResponseHop(RATE) == 480 && maudResponseHop(44100.0) == 441, "hops of 10 ms");
    TestEnergy();
    TestDecay();
    TestDirection();
    TestSilence();
    free(s_scratch);
    return s_failures == 0 ? 0 : 1;
}
