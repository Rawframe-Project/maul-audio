// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The real FFT (whitebox): every size from 4 to 4096 against a DFT in
// double (each bin within 1e-6 of the largest, measured 6e-8), sizes up
// to 65,536 back to their samples (within 1e-6, measured 5e-7), a
// shifted impulse's bins on the unit circle at the right phase.

#include "real_fft.h"
#include "test_harness.h"

#include <math.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

static void TestAgainstDft(void)
{
    for (uint32_t n = 4; n <= 4096; n *= 2)
    {
        maudRealFft fft;
        void* memory = malloc(maudRealFftBytes(n));
        maudInitRealFft(&fft, n, memory);
        float* x = malloc(n * sizeof(float));
        float* bins = malloc((n + 2) * sizeof(float));
        for (uint32_t j = 0; j < n; ++j)
        {
            x[j] = (float)(sin(1.3 * j + 0.2) + 0.1 * (double)(j % 7));
        }
        maudForwardRealFft(&fft, x, bins);
        double error = 0.0;
        double largest = 0.0;
        for (uint32_t k = 0; k <= n / 2; ++k)
        {
            double re = 0.0;
            double im = 0.0;
            for (uint32_t j = 0; j < n; ++j)
            {
                double a = -2.0 * PI * (double)((uint64_t)j * k % n) / n;
                re += (double)x[j] * cos(a);
                im += (double)x[j] * sin(a);
            }
            error = fmax(error, hypot(re - (double)bins[2 * k], im - (double)bins[2 * k + 1]));
            largest = fmax(largest, hypot(re, im));
        }
        CHECK(error < 1e-6 * largest, "the DFT's bins");
        free(memory);
        free(x);
        free(bins);
    }
}

static void TestRoundTrip(void)
{
    for (uint32_t n = 4; n <= 65536; n *= 4)
    {
        maudRealFft fft;
        void* memory = malloc(maudRealFftBytes(n));
        maudInitRealFft(&fft, n, memory);
        float* x = malloc(n * sizeof(float));
        float* y = malloc(n * sizeof(float));
        float* bins = malloc((n + 2) * sizeof(float));
        uint32_t seed = 7;
        for (uint32_t j = 0; j < n; ++j)
        {
            seed = seed * 1664525u + 1013904223u;
            x[j] = (float)(seed >> 8) / 16777216.0f - 0.5f;
        }
        maudForwardRealFft(&fft, x, bins);
        maudInverseRealFft(&fft, bins, y);
        double error = 0.0;
        for (uint32_t j = 0; j < n; ++j)
        {
            error = fmax(error, fabs((double)y[j] - (double)x[j]));
        }
        CHECK(error < 1e-6, "back to the samples");
        free(memory);
        free(x);
        free(y);
        free(bins);
    }
}

// An impulse at sample 3 of 64: bin k is e^(-2 pi i 3 k / 64).
static void TestImpulse(void)
{
    maudRealFft fft;
    void* memory = malloc(maudRealFftBytes(64));
    maudInitRealFft(&fft, 64, memory);
    float x[64] = {0};
    float bins[66];
    x[3] = 1.0f;
    maudForwardRealFft(&fft, x, bins);
    double error = 0.0;
    for (int k = 0; k <= 32; ++k)
    {
        double a = -2.0 * PI * 3.0 * k / 64.0;
        error = fmax(error, hypot((double)bins[2 * k] - cos(a), (double)bins[2 * k + 1] - sin(a)));
    }
    CHECK(error < 1e-6, "a shifted impulse");
    free(memory);
}

int main(void)
{
    TestAgainstDft();
    TestRoundTrip();
    TestImpulse();
    return s_failures == 0 ? 0 : 1;
}
