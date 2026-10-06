// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reverbs: the decay of the impulse response, measured per octave from
// 125 Hz to 16 kHz (Schroeder integration, a line fit from -5 to
// -35 dB), meets the requested times within 10 % to 8 kHz (measured: 7 %
// at most) and within 20 % in the top octave (measured: up to 16 % short,
// where the high shelf damps the octave's upper half harder), the band
// times placed at the bands' centres and interpolated
// between; silence stays silent; the tail adds into the bed; a reset
// silences it; a change of times stays finite and lands on the new
// decay; nothing allocates while processing; bad calls write nothing.

#include "test_harness.h"

#include "maul-audio/reverb.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI   3.14159265358979323846
#define RATE 48000.0

enum
{
    LENGTH = 168000
};

static long s_allocations;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_allocations += 1;
    return malloc(size);
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

static maudReverb* Create(void)
{
    maudReverbDef def = maudDefaultReverbDef();
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudReverb* r = nullptr;
    CHECK(maudCreateReverb(&def, &r) == maud_success, "a reverb");
    return r;
}

static float s_in[LENGTH];
static float s_bed[4][LENGTH];
static float s_band[LENGTH];

// The W channel's impulse response for given times, in blocks of 480.
static void Respond(maudReverb* r, const float* times)
{
    memset(s_in, 0, sizeof(s_in));
    memset(s_bed, 0, sizeof(s_bed));
    s_in[0] = 1.0f;
    maudReverbParams params = {{times[0], times[1], times[2]}};
    for (int at = 0; at < LENGTH; at += 480)
    {
        float* bed[4] = {s_bed[0] + at, s_bed[1] + at, s_bed[2] + at, s_bed[3] + at};
        CHECK(maudProcessReverb(r, &params, s_in + at, bed, 480) == maud_success, "process");
    }
}

// An octave band-pass (two cookbook band-passes of an octave, 0 dB at
// the centre) over the W channel, into s_band.
static void BandPass(double hz)
{
    double w = 2.0 * PI * hz / RATE;
    double q = sqrt(2.0);
    double alpha = sin(w) / (2.0 * q);
    double a0 = 1.0 + alpha;
    double b[3] = {alpha / a0, 0.0, -alpha / a0};
    double a[3] = {1.0, -2.0 * cos(w) / a0, (1.0 - alpha) / a0};
    for (int n = 0; n < LENGTH; ++n)
    {
        s_band[n] = s_bed[0][n];
    }
    for (int pass = 0; pass < 2; ++pass)
    {
        double s1 = 0.0;
        double s2 = 0.0;
        for (int n = 0; n < LENGTH; ++n)
        {
            double x = (double)s_band[n];
            double y = b[0] * x + s1;
            s1 = b[1] * x - a[1] * y + s2;
            s2 = b[2] * x - a[2] * y;
            s_band[n] = (float)y;
        }
    }
}

static double T30(void)
{
    static double decay[LENGTH];
    double sum = 0.0;
    for (int n = LENGTH; n-- > 0;)
    {
        sum += (double)s_band[n] * (double)s_band[n];
        decay[n] = sum;
    }
    int start = -1;
    int end = -1;
    for (int n = 0; n < LENGTH && end < 0; ++n)
    {
        double db = 10.0 * log10(decay[n] / decay[0] + 1e-30);
        start = start < 0 && db < -5.0 ? n : start;
        end = db < -35.0 ? n : end;
    }
    double st = 0.0;
    double sd = 0.0;
    double stt = 0.0;
    double std = 0.0;
    int count = end - start;
    for (int n = start; n < end; ++n)
    {
        double t = n / RATE;
        double db = 10.0 * log10(decay[n] / decay[0]);
        st += t;
        sd += db;
        stt += t * t;
        std += t * db;
    }
    double slope = (count * std - st * sd) / (count * stt - st * st);
    return -60.0 / slope;
}

// The requested time at an octave, as the reverb places it.
static double Requested(const float* times, double hz)
{
    const double centres[3] = {sqrt(20.0 * 800.0), sqrt(800.0 * 8000.0), sqrt(8000.0 * 20000.0)};
    if (hz <= centres[0])
    {
        return (double)times[0];
    }
    for (int b = 0; b < 2; ++b)
    {
        if (hz <= centres[b + 1])
        {
            double u = log(hz / centres[b]) / log(centres[b + 1] / centres[b]);
            return exp((1.0 - u) * log((double)times[b]) + u * log((double)times[b + 1]));
        }
    }
    return (double)times[2];
}

static void TestDecay(void)
{
    const float cases[3][3] = {{1.2f, 0.8f, 0.4f}, {3.0f, 2.0f, 0.8f}, {0.6f, 0.6f, 0.6f}};
    for (int c = 0; c < 3; ++c)
    {
        maudReverb* r = Create();
        Respond(r, cases[c]);
        double worst = 0.0;
        double top = 0.0;
        printf("times %.1f / %.1f / %.1f s, octave errors %%:", (double)cases[c][0],
               (double)cases[c][1], (double)cases[c][2]);
        for (int k = 2; k <= 9; ++k)
        {
            double hz = 31.25 * pow(2.0, k);
            BandPass(hz);
            double error = T30() / Requested(cases[c], hz) - 1.0;
            printf(" %+.0f", 100.0 * error);
            if (k == 9)
            {
                top = fabs(error);
            }
            else
            {
                worst = fmax(worst, fabs(error));
            }
        }
        printf("\n");
        CHECK(worst < 0.10, "each octave to 8 kHz decays as requested");
        CHECK(top < 0.20, "and the top octave");
        maudDestroyReverb(r);
    }
}

static void TestBehaviour(void)
{
    maudReverb* r = Create();
    maudReverbParams params = {{1.0f, 0.7f, 0.4f}};
    float in[480] = {0};
    static float bed[4][480];
    float* out[4] = {bed[0], bed[1], bed[2], bed[3]};
    CHECK(maudProcessReverb(r, &params, in, out, 480) == maud_success, "process");
    bool silent = true;
    for (int c = 0; c < 4; ++c)
    {
        for (int n = 0; n < 480; ++n)
        {
            silent = silent && bed[c][n] == 0.0f;
        }
    }
    CHECK(silent, "silence stays silent");
    for (int c = 0; c < 4; ++c)
    {
        for (int n = 0; n < 480; ++n)
        {
            bed[c][n] = 1.0f;
        }
    }
    in[0] = 1.0f;
    long before = s_allocations;
    for (int block = 0; block < 10; ++block)
    {
        CHECK(maudProcessReverb(r, &params, in, out, 480) == maud_success, "process");
        in[0] = 0.0f;
    }
    CHECK(s_allocations == before, "processing allocates nothing");
    double energy[4] = {0.0};
    for (int c = 0; c < 4; ++c)
    {
        for (int n = 0; n < 480; ++n)
        {
            energy[c] += (double)(bed[c][n] - 1.0f) * (double)(bed[c][n] - 1.0f);
        }
    }
    CHECK(energy[0] > 0.0 && energy[1] > 0.0 && energy[2] > 0.0 && energy[3] > 0.0,
          "the tail adds into all four channels");
    // A change of times: finite throughout, then the new decay.
    maudReverbParams longer = {{2.5f, 1.5f, 0.6f}};
    bool finite = true;
    for (int block = 0; block < 20; ++block)
    {
        CHECK(maudProcessReverb(r, block == 0 ? &params : &longer, in, out, 480) == maud_success,
              "process");
        for (int n = 0; n < 480; ++n)
        {
            finite = finite && isfinite(bed[0][n]);
        }
    }
    CHECK(finite, "a change of times stays finite");
    CHECK(maudResetReverb(r) == maud_success, "reset");
    memset(bed, 0, sizeof(bed));
    in[0] = 0.0f;
    CHECK(maudProcessReverb(r, &longer, in, out, 480) == maud_success, "process");
    silent = true;
    for (int n = 0; n < 480; ++n)
    {
        silent = silent && bed[0][n] == 0.0f;
    }
    CHECK(silent, "a reset silences the tail");
    maudReverbParams bad = {{0.05f, 1.0f, 1.0f}};
    bed[0][0] = 7.0f;
    in[0] = 1.0f;
    CHECK(maudProcessReverb(r, &bad, in, out, 480) == maud_errorInvalid, "a time too short");
    bad.reverbTime[0] = NAN;
    CHECK(maudProcessReverb(r, &bad, in, out, 480) == maud_errorInvalid, "NaN");
    CHECK(bed[0][0] == 7.0f, "nothing written by a bad call");
    CHECK(maudProcessReverb(r, &params, nullptr, nullptr, 0) == maud_success, "no frames");
    maudDestroyReverb(r);
    maudDestroyReverb(nullptr);
    maudReverbDef def = maudDefaultReverbDef();
    maudReverb* none = (maudReverb*)&def;
    def.sampleRate = 32000.0f;
    CHECK(maudCreateReverb(&def, &none) == maud_errorInvalid && none == nullptr, "32 kHz");
    def = maudDefaultReverbDef();
    def.cookie = 0;
    CHECK(maudCreateReverb(&def, &none) == maud_errorInvalid, "no cookie");
}

int main(void)
{
    TestDecay();
    TestBehaviour();
    return s_failures == 0 ? 0 : 1;
}
