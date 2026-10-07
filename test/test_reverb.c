// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reverbs: the decay of the impulse response, measured per octave from
// 125 Hz to 16 kHz (Schroeder integration, a line fit from -5 to
// -35 dB), meets the requested times, the band times placed at the
// bands' centres and interpolated between: within 12 % to 8 kHz
// (measured: 9 % at most, at 125 Hz in a 0.6 s room) and 8 % in the top
// octave (measured: 5 %), including after the times change; the tail is
// diffuse, each directional channel carrying near a third of W's energy;
// splitting the stream into other blocks changes nothing; a change of
// times ramps across its call; silence stays silent; a reset silences the
// tail; nothing allocates while processing; bad calls write nothing.

#include "test_harness.h"

#include "maul-audio/reverb.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI           3.14159265358979323846
#define RATE         48000.0
#define DIFFUSE_LOW  0.25
#define DIFFUSE_HIGH 0.45
#define RAMPED       0.05

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

// The impulse response for given times, in blocks of 480, after two
// silent blocks at the times before.
static void Respond(maudReverb* r, const float* before, const float* times)
{
    memset(s_in, 0, sizeof(s_in));
    memset(s_bed, 0, sizeof(s_bed));
    maudReverbParams first = {{before[0], before[1], before[2]},
                              {0.0f, 0.0f, 0.0f},
                              0.0f,
                              {0.0f, 0.0f, 0.0f},
                              {0.0f, 0.0f, 0.0f}};
    for (int block = 0; block < 2; ++block)
    {
        float* bed[4] = {s_bed[0], s_bed[1], s_bed[2], s_bed[3]};
        CHECK(maudProcessReverb(r, &first, s_in, bed, 480) == maud_success, "process");
    }
    s_in[0] = 1.0f;
    maudReverbParams params = {{times[0], times[1], times[2]},
                               {0.0f, 0.0f, 0.0f},
                               0.0f,
                               {0.0f, 0.0f, 0.0f},
                               {0.0f, 0.0f, 0.0f}};
    for (int at = 0; at < LENGTH; at += 480)
    {
        float* bed[4] = {s_bed[0] + at, s_bed[1] + at, s_bed[2] + at, s_bed[3] + at};
        CHECK(maudProcessReverb(r, &params, s_in + at, bed, 480) == maud_success, "process");
    }
}

// Each directional channel's energy after the first 50 ms over W's: a
// third in a diffuse field (SN3D).
static void CheckDiffuse(void)
{
    double energy[4] = {0.0};
    for (int c = 0; c < 4; ++c)
    {
        for (int n = 2400; n < LENGTH; ++n)
        {
            energy[c] += (double)s_bed[c][n] * (double)s_bed[c][n];
        }
    }
    printf("  directional over W: %.3f %.3f %.3f\n", energy[1] / energy[0], energy[2] / energy[0],
           energy[3] / energy[0]);
    for (int c = 1; c < 4; ++c)
    {
        CHECK(energy[c] / energy[0] > DIFFUSE_LOW && energy[c] / energy[0] < DIFFUSE_HIGH,
              "the tail is diffuse");
    }
}

// One cookbook second-order section, high- or low-pass at hz with Q q,
// run in place over s_band.
static void Section(double hz, bool high, double q)
{
    double w = 2.0 * PI * hz / RATE;
    double alpha = sin(w) / (2.0 * q);
    double c = cos(w);
    double a0 = 1.0 + alpha;
    double edge = high ? (1.0 + c) / 2.0 : (1.0 - c) / 2.0;
    double b[3] = {edge / a0, (high ? -2.0 : 2.0) * edge / a0, edge / a0};
    double a[3] = {1.0, -2.0 * c / a0, (1.0 - alpha) / a0};
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

// The W channel through an octave band into s_band: eighth-order
// Butterworth edges a half octave either side of hz (none above when
// that edge passes 0.45 of the rate). Selectivity matters: where times
// fall steeply, a fourth-order band lets the slower octave below into
// the late decay and reads 15 to 20 % long.
static void BandPass(double hz)
{
    for (int n = 0; n < LENGTH; ++n)
    {
        s_band[n] = s_bed[0][n];
    }
    for (int k = 0; k < 4; ++k)
    {
        double q = 1.0 / (2.0 * cos(PI * (2.0 * k + 1.0) / 16.0));
        Section(hz / sqrt(2.0), true, q);
        if (hz * sqrt(2.0) < 0.45 * RATE)
        {
            Section(hz * sqrt(2.0), false, q);
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
    const float cases[4][3] = {
        {1.2f, 0.8f, 0.4f}, {3.0f, 2.0f, 0.8f}, {0.6f, 0.6f, 0.6f}, {2.0f, 1.0f, 0.3f}};
    // The third case starts from other times: the decay follows a change.
    const float before[4][3] = {
        {1.2f, 0.8f, 0.4f}, {3.0f, 2.0f, 0.8f}, {1.0f, 0.8f, 0.5f}, {2.0f, 1.0f, 0.3f}};
    for (int c = 0; c < 4; ++c)
    {
        maudReverb* r = Create();
        Respond(r, before[c], cases[c]);
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
        CheckDiffuse();
        CHECK(worst < 0.12, "each octave to 8 kHz decays as requested");
        CHECK(top < 0.08, "and the top octave");
        maudDestroyReverb(r);
    }
}

static void TestBehaviour(void)
{
    maudReverb* r = Create();
    maudReverbParams params = {
        {1.0f, 0.7f, 0.4f}, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
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
    maudReverbParams longer = {
        {2.5f, 1.5f, 0.6f}, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
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
    maudReverbParams bad = {
        {0.05f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
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

// Splitting the stream differently changes nothing: an impulse in the
// middle of a block gives what it gives at the start of one.
static void TestBlocks(void)
{
    maudReverb* whole = Create();
    maudReverb* split = Create();
    static float in[4800];
    static float a[4800];
    static float b[4800];
    static float zero[4][4800];
    in[100] = 1.0f;
    maudReverbParams params = {
        {1.0f, 0.7f, 0.4f}, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    for (int at = 0; at < 4800; at += 480)
    {
        float* bed[4] = {a + at, zero[1] + at, zero[2] + at, zero[3] + at};
        CHECK(maudProcessReverb(whole, &params, in + at, bed, 480) == maud_success, "process");
    }
    for (int at = 0; at < 4800;)
    {
        uint32_t count = at == 0 ? 100 : (at + 480 <= 4800 ? 480 : 4800 - (uint32_t)at);
        float* bed[4] = {b + at, zero[1] + at, zero[2] + at, zero[3] + at};
        CHECK(maudProcessReverb(split, &params, in + at, bed, count) == maud_success, "process");
        at += (int)count;
    }
    bool same = true;
    double energy = 0.0;
    for (int n = 0; n < 4800; ++n)
    {
        same = same && a[n] == b[n];
        energy += (double)a[n] * (double)a[n];
    }
    CHECK(energy > 0.0 && same, "an impulse mid-block gives the same tail");
    maudDestroyReverb(whole);
    maudDestroyReverb(split);
}

// A change of times ramps across the call: its first frames stay close to
// what the old times give, its last frames reach the new ones.
static void TestRamp(void)
{
    maudReverb* kept = Create();
    maudReverb* changed = Create();
    static float noise[480];
    static float a[4][480];
    static float b[4][480];
    uint32_t seed = 1;
    maudReverbParams before = {
        {1.0f, 0.7f, 0.4f}, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    maudReverbParams after = {
        {0.3f, 0.2f, 0.1f}, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    for (int block = 0; block < 11; ++block)
    {
        for (int n = 0; n < 480; ++n)
        {
            seed = seed * 1664525u + 1013904223u;
            noise[n] = (float)(seed >> 8) / 16777216.0f - 0.5f;
        }
        memset(a, 0, sizeof(a));
        memset(b, 0, sizeof(b));
        float* outA[4] = {a[0], a[1], a[2], a[3]};
        float* outB[4] = {b[0], b[1], b[2], b[3]};
        CHECK(maudProcessReverb(kept, &before, noise, outA, 480) == maud_success, "process");
        CHECK(maudProcessReverb(changed, block < 10 ? &before : &after, noise, outB, 480) ==
                  maud_success,
              "process");
    }
    double early = 0.0;
    double late = 0.0;
    double scale = 0.0;
    for (int n = 0; n < 16; ++n)
    {
        early += (double)(a[0][n] - b[0][n]) * (double)(a[0][n] - b[0][n]);
        late += (double)(a[0][464 + n] - b[0][464 + n]) * (double)(a[0][464 + n] - b[0][464 + n]);
        scale += (double)a[0][n] * (double)a[0][n];
    }
    printf("ramp: first 16 frames differ by %.2e of the signal, the last by %.2e\n", early / scale,
           late / scale);
    CHECK(early < RAMPED * late, "a change starts gently");
    maudDestroyReverb(kept);
    maudDestroyReverb(changed);
}

// The send's delay and levels: a delayed impulse's tail starts 23 ms
// after the delay; +6 dB in every band gives four times the energy;
// -20 dB in the low band alone takes the low octaves down and leaves the
// middle; values out of range are refused.
static void TestSend(void)
{
    maudReverbDef def = maudDefaultReverbDef();
    def.maxDelay = 0.2f;
    maudReverb* r[2] = {nullptr, nullptr};
    CHECK(maudCreateReverb(&def, &r[0]) == maud_success &&
              maudCreateReverb(&def, &r[1]) == maud_success,
          "two reverbs");
    const float flat[3] = {0.0f, 0.0f, 0.0f};
    const float louder[3] = {6.0f, 6.0f, 6.0f};
    static float out[2][4][24000];
    for (int k = 0; k < 2; ++k)
    {
        maudReverbParams p = {{1.0f, 1.0f, 1.0f},
                              {k == 0 ? flat[0] : louder[0], k == 0 ? flat[1] : louder[1],
                               k == 0 ? flat[2] : louder[2]},
                              k == 0 ? 0.0f : 0.1f,
                              {0.0f, 0.0f, 0.0f},
                              {0.0f, 0.0f, 0.0f}};
        static float in[24000];
        memset(in, 0, sizeof(in));
        in[0] = 1.0f;
        for (int at = 0; at < 24000; at += 480)
        {
            float* bed[4] = {out[k][0] + at, out[k][1] + at, out[k][2] + at, out[k][3] + at};
            CHECK(maudProcessReverb(r[k], &p, in + at, bed, 480) == maud_success, "process");
        }
    }
    int onset[2] = {-1, -1};
    for (int k = 0; k < 2; ++k)
    {
        for (int i = 0; i < 24000 && onset[k] < 0; ++i)
        {
            onset[k] = out[k][0][i] != 0.0f ? i : -1;
        }
    }
    printf("onsets: %d and %d frames\n", onset[0], onset[1]);
    CHECK(onset[1] - onset[0] == 4800, "delayed by 100 ms");
    CHECK(onset[0] > (int)(0.023 * 48000.0) && onset[0] < (int)(0.024 * 48000.0),
          "silent for its first 23 ms");
    double plain = 0.0;
    double loud = 0.0;
    for (int i = 0; i < 9600; ++i)
    {
        plain += (double)out[0][0][onset[0] + i] * (double)out[0][0][onset[0] + i];
        loud += (double)out[1][0][onset[1] + i] * (double)out[1][0][onset[1] + i];
    }
    printf("6 dB: energy ratio %.3f\n", loud / plain);
    CHECK(fabs(loud / plain / pow(10.0, 0.6) - 1.0) < 0.05, "6 dB louder");
    maudReverbParams bad = {
        {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, 0.21f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    CHECK(maudProcessReverb(r[0], &bad, out[0][0],
                            (float* const[4]){out[0][0], out[0][1], out[0][2], out[0][3]},
                            16) == maud_errorInvalid,
          "a delay past the longest");
    bad.delay = 0.0f;
    bad.level[1] = 25.0f;
    CHECK(maudProcessReverb(r[0], &bad, out[0][0],
                            (float* const[4]){out[0][0], out[0][1], out[0][2], out[0][3]},
                            16) == maud_errorInvalid,
          "a level past 24 dB");
    bad.level[1] = NAN;
    CHECK(maudProcessReverb(r[0], &bad, out[0][0],
                            (float* const[4]){out[0][0], out[0][1], out[0][2], out[0][3]},
                            16) == maud_errorInvalid,
          "a level not a number");
    maudDestroyReverb(r[0]);
    maudDestroyReverb(r[1]);
    maudReverbDef far = maudDefaultReverbDef();
    far.maxDelay = 4.5f;
    maudReverb* none = nullptr;
    CHECK(maudCreateReverb(&far, &none) == maud_errorInvalid, "a longest delay past 4 s");
}

// The 125 Hz octave's energy over the 2 kHz octave's, for levels.
static double LowOverMiddle(const float* levels)
{
    maudReverb* r = Create();
    maudReverbParams p = {{1.0f, 1.0f, 1.0f},
                          {levels[0], levels[1], levels[2]},
                          0.0f,
                          {0.0f, 0.0f, 0.0f},
                          {0.0f, 0.0f, 0.0f}};
    memset(s_in, 0, sizeof(s_in));
    memset(s_bed, 0, sizeof(s_bed));
    s_in[0] = 1.0f;
    for (int at = 0; at < LENGTH; at += 480)
    {
        float* bed[4] = {s_bed[0] + at, s_bed[1] + at, s_bed[2] + at, s_bed[3] + at};
        CHECK(maudProcessReverb(r, &p, s_in + at, bed, 480) == maud_success, "process");
    }
    maudDestroyReverb(r);
    double energy[2] = {0.0, 0.0};
    const double centres[2] = {125.0, 2000.0};
    for (int k = 0; k < 2; ++k)
    {
        BandPass(centres[k]);
        for (int i = 0; i < LENGTH; ++i)
        {
            energy[k] += (double)s_band[i] * (double)s_band[i];
        }
    }
    return energy[0] / energy[1];
}

// -20 dB in the low band alone: the 125 Hz octave falls by about 20 dB
// against the 2 kHz octave, compared with the same reverb flat.
static void TestBandLevel(void)
{
    const float flat[3] = {0.0f, 0.0f, 0.0f};
    const float low[3] = {-20.0f, 0.0f, 0.0f};
    double drop = 10.0 * log10(LowOverMiddle(low) / LowOverMiddle(flat));
    printf("low band at -20 dB: 125 Hz against 2 kHz, %.1f dB\n", drop);
    CHECK(drop < -16.0 && drop > -24.0, "the low band down");
}

// The W bed's impulse response from a reverb with a tail: params until
// block off (of 480 frames), then stopped (the tail's times 0).
static void TailResponse(const maudReverbParams* params, uint32_t off, bool tail)
{
    maudReverbDef def = maudDefaultReverbDef();
    def.tail = tail;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudReverb* r = nullptr;
    CHECK(maudCreateReverb(&def, &r) == maud_success, "a reverb with a tail");
    maudReverbParams stopped = *params;
    memset(stopped.tailTime, 0, sizeof(stopped.tailTime));
    memset(s_in, 0, sizeof(s_in));
    memset(s_bed, 0, sizeof(s_bed));
    s_in[0] = 1.0f;
    long before = s_allocations;
    for (uint32_t at = 0; at < LENGTH; at += 480)
    {
        float* bed[4] = {s_bed[0] + at, s_bed[1] + at, s_bed[2] + at, s_bed[3] + at};
        CHECK(maudProcessReverb(r, at / 480 < off ? params : &stopped, s_in + at, bed, 480) ==
                  maud_success,
              "process");
    }
    CHECK(s_allocations == before, "a tail allocates nothing");
    maudDestroyReverb(r);
}

// The W bed's 10 ms bins from start to end seconds, over the reverb's
// 0.0144 exp(-13.8 t / time) at level 0: the mean in dB.
static double OverModel(double time, double start, double end)
{
    double sum = 0.0;
    int bins = 0;
    for (int i = (int)(start * 100.0); i < (int)(end * 100.0); ++i, ++bins)
    {
        double energy = 0.0;
        for (int n = i * 480; n < (i + 1) * 480; ++n)
        {
            energy += (double)s_bed[0][n] * (double)s_bed[0][n];
        }
        sum += 10.0 * log10(energy / (0.0144 * exp(-13.815510557964274 * i * 0.01 / time)));
    }
    return sum / bins;
}

static double Peak(double start, double end)
{
    double peak = 0.0;
    for (int n = (int)(start * RATE); n < (int)(end * RATE); ++n)
    {
        peak = fmax(peak, fabs((double)s_bed[0][n]));
    }
    return peak;
}

// The tail: alone it gives the first's energy for its time and level; the
// two do not ring together (equal ones sum in energy, not amplitude); a
// reverb made without one ignores it; a stopped tail decays at its time,
// then stops; times and levels are checked.
static void TestTail(void)
{
    maudReverbParams alone = {
        {0.3f, 0.3f, 0.3f}, {-96.0f, -96.0f, -96.0f}, 0.0f, {1.5f, 1.5f, 1.5f}, {0.0f, 0.0f, 0.0f}};
    TailResponse(&alone, 1000, true);
    double tail = OverModel(1.5, 0.1, 0.8);
    maudReverbParams first = {
        {1.5f, 1.5f, 1.5f}, {0.0f, 0.0f, 0.0f}, 0.0f, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    TailResponse(&first, 1000, true);
    double reference = OverModel(1.5, 0.1, 0.8);
    double single = 0.0;
    for (int n = (int)(0.1 * RATE); n < (int)(1.0 * RATE); ++n)
    {
        single += (double)s_bed[0][n] * (double)s_bed[0][n];
    }
    maudReverbParams both = first;
    memcpy(both.tailTime, both.reverbTime, sizeof(both.tailTime));
    TailResponse(&both, 1000, true);
    double sum = 0.0;
    for (int n = (int)(0.1 * RATE); n < (int)(1.0 * RATE); ++n)
    {
        sum += (double)s_bed[0][n] * (double)s_bed[0][n];
    }
    printf("tail: alone %.2f dB from the model, the first %.2f dB; two equal %.2f times one\n",
           tail, reference, sum / single);
    CHECK(fabs(tail) < 1.0 && fabs(reference) < 1.0, "the tail's energy as the first's");
    CHECK(sum / single > 1.7 && sum / single < 2.3, "the two do not ring together");
    TailResponse(&both, 1000, false);
    float without[4800];
    memcpy(without, s_bed[0] + 4800, sizeof(without));
    TailResponse(&first, 1000, false);
    CHECK(memcmp(without, s_bed[0] + 4800, sizeof(without)) == 0,
          "a reverb without a tail ignores it");
    maudReverbParams brief = {
        {0.1f, 0.1f, 0.1f}, {-96.0f, -96.0f, -96.0f}, 0.0f, {0.2f, 0.2f, 0.2f}, {0.0f, 0.0f, 0.0f}};
    TailResponse(&brief, 1, true);
    double ringing = Peak(0.1, 0.15);
    double after = Peak(0.3, 0.5);
    printf("a stopped tail: %.2g at 100 ms, %.2g from 300 ms\n", ringing, after);
    CHECK(ringing > 1e-6, "a stopped tail decays at its time");
    CHECK(after < 1e-9, "and then stops");
    maudReverb* r = Create();
    float out[4][16] = {{0}};
    float* const bed[4] = {out[0], out[1], out[2], out[3]};
    maudReverbParams bad = first;
    bad.tailTime[1] = 0.05f;
    CHECK(maudProcessReverb(r, &bad, out[0], bed, 16) == maud_errorInvalid, "a tail too short");
    bad.tailTime[1] = NAN;
    CHECK(maudProcessReverb(r, &bad, out[0], bed, 16) == maud_errorInvalid, "a tail not a number");
    bad.tailTime[1] = 1.0f;
    bad.tailLevel[2] = 25.0f;
    CHECK(maudProcessReverb(r, &bad, out[0], bed, 16) == maud_errorInvalid,
          "a tail's level past 24 dB");
    maudDestroyReverb(r);
}

int main(void)
{
    TestTail();
    TestBlocks();
    TestRamp();
    TestDecay();
    TestSend();
    TestBandLevel();
    TestBehaviour();
    return s_failures == 0 ? 0 : 1;
}
