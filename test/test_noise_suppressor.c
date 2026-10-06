// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The noise suppressor: steady white noise taken down 12 dB within a
// second and 14 later (white noise still looks like speech to some of
// its bins now and then), its level reported; speech-like bursts over noise
// kept within a decibel while the gaps between them fall; the output
// lagging by exactly 10 ms and independent of how the frames are cut;
// stereo channels sharing one gain; the high-pass filter; refusals; and
// no allocation while processing. (Its quality on recorded speech and
// noise is measured outside the tests.)

#include "test_harness.h"

#include "maul-audio/voice.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RATE    16000u
#define SECONDS 6u
#define FRAMES  (RATE * SECONDS)
#define HOP     (RATE / 100u)
#define PI      3.14159265358979f

static int s_allocations = 0;

static void* Counted(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_allocations++;
    return malloc(size);
}

static void Release(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

static uint32_t s_state = 7u;

static float Gaussian(void)
{
    float sum = 0.0f;
    for (int i = 0; i < 12; ++i)
    {
        s_state = s_state * 1664525u + 1013904223u;
        sum += (float)(s_state >> 8) / 16777216.0f;
    }
    return sum - 6.0f;
}

static double LevelDb(const float* x, uint32_t from, uint32_t to, uint32_t stride)
{
    double sum = 0.0;
    for (uint32_t i = from; i < to; ++i)
    {
        sum += (double)x[i * stride] * (double)x[i * stride];
    }
    return 10.0 * log10(sum / (to - from) + 1e-30);
}

static maudNoiseSuppressor* Make(maudChannelLayout layout, float highPassHz)
{
    maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();
    def.sampleRate = RATE;
    def.layout = layout;
    def.highPassHz = highPassHz;
    def.allocator = (maudAllocator){Counted, Release, nullptr};
    maudNoiseSuppressor* s = nullptr;
    CHECK(maudCreateNoiseSuppressor(&def, &s) == maud_success, "a noise suppressor");
    return s;
}

static float s_in[FRAMES];
static float s_out[FRAMES];

static void TestSteadyNoise(void)
{
    // White noise at -30 dBFS.
    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        s_in[i] = 0.0316f * Gaussian();
    }
    memcpy(s_out, s_in, sizeof(s_in));
    maudNoiseSuppressor* s = Make(maud_layoutMono, 0.0f);
    maudNoiseState state;
    CHECK(maudSuppressNoise(s, s_out, FRAMES, &state) == maud_success, "suppressed");
    double early = LevelDb(s_in, RATE / 2, RATE, 1) - LevelDb(s_out, RATE / 2, RATE, 1);
    double late = LevelDb(s_in, 3 * RATE, FRAMES, 1) - LevelDb(s_out, 3 * RATE, FRAMES, 1);
    printf("steady noise: %.1f dB down from 0.5 to 1 s, %.1f dB from 3 s; its level %.1f dBFS, "
           "speech %.2f\n",
           early, late, (double)state.noiseDbfs, (double)state.speechProbability);
    CHECK(early > 12.0 && late > 14.0, "taken down within a second");
    CHECK(fabsf(state.noiseDbfs + 30.0f) < 3.0f, "its level reported");
    CHECK(state.frames == FRAMES / HOP, "every 10 ms analysed");
    maudDestroyNoiseSuppressor(s);
}

// A voice-like burst: a 160 Hz harmonic series under a decaying
// spectrum, 300 ms on and 300 ms off, over white noise 20 dB down.
static bool Burst(uint32_t i)
{
    return (i / (RATE * 3 / 10)) % 2 == 1;
}

static void TestBursts(void)
{
    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        float voice = 0.0f;
        for (int h = 1; h <= 20; ++h)
        {
            voice += sinf(2.0f * PI * 160.0f * (float)h * (float)i / RATE) / (float)h;
        }
        s_in[i] = (Burst(i) ? 0.1f * voice : 0.0f) + 0.01f * Gaussian();
    }
    memcpy(s_out, s_in, sizeof(s_in));
    maudNoiseSuppressor* s = Make(maud_layoutMono, 0.0f);
    CHECK(maudSuppressNoise(s, s_out, FRAMES, nullptr) == maud_success, "suppressed");
    // Compare the middle of each burst and gap after 1.2 s, the output
    // moved back by its 10 ms.
    double worstBurst = 0.0;
    double leastGap = 1e9;
    for (uint32_t start = 4 * (RATE * 3 / 10); start + RATE * 3 / 10 <= FRAMES - HOP;
         start += RATE * 3 / 10)
    {
        uint32_t from = start + RATE / 20;
        uint32_t to = start + RATE / 4;
        double in = LevelDb(s_in, from, to, 1);
        double out = LevelDb(s_out + HOP, from, to, 1);
        if (Burst(start))
        {
            worstBurst = fmax(worstBurst, fabs(out - in));
        }
        else
        {
            leastGap = fmin(leastGap, in - out);
        }
    }
    printf("bursts: kept within %.2f dB, gaps down at least %.1f dB\n", worstBurst, leastGap);
    CHECK(worstBurst < 1.0, "the bursts kept");
    CHECK(leastGap > 15.0, "the gaps taken down");
    maudDestroyNoiseSuppressor(s);
}

static void TestLatencyAndCuts(void)
{
    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        s_in[i] = 0.2f * sinf(0.05f * (float)i) + 0.01f * Gaussian();
    }
    maudNoiseSuppressor* whole = Make(maud_layoutMono, 100.0f);
    memcpy(s_out, s_in, sizeof(s_in));
    CHECK(maudSuppressNoise(whole, s_out, FRAMES, nullptr) == maud_success, "whole");
    bool silent = true;
    for (uint32_t i = 0; i < HOP; ++i)
    {
        silent = silent && s_out[i] == 0.0f;
    }
    CHECK(silent && s_out[HOP] != 0.0f, "the first 10 ms silent, then the input");
    static const uint32_t cuts[4] = {1, 7, 160, 479};
    for (int c = 0; c < 4; ++c)
    {
        static float pieces[FRAMES];
        memcpy(pieces, s_in, sizeof(s_in));
        maudNoiseSuppressor* cut = Make(maud_layoutMono, 100.0f);
        for (uint32_t at = 0; at < FRAMES; at += cuts[c])
        {
            uint32_t n = at + cuts[c] <= FRAMES ? cuts[c] : FRAMES - at;
            CHECK(maudSuppressNoise(cut, pieces + at, n, nullptr) == maud_success, "a piece");
        }
        CHECK(memcmp(pieces, s_out, sizeof(s_out)) == 0, "the same however cut");
        maudDestroyNoiseSuppressor(cut);
    }
    maudDestroyNoiseSuppressor(whole);
}

static float s_stereo[2 * FRAMES];

static void TestChannels(void)
{
    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        s_in[i] = 0.05f * Gaussian() + (Burst(i) ? 0.2f * sinf(0.1f * (float)i) : 0.0f);
        s_stereo[2 * i] = s_in[i];
        s_stereo[2 * i + 1] = s_in[i];
    }
    memcpy(s_out, s_in, sizeof(s_in));
    maudNoiseSuppressor* mono = Make(maud_layoutMono, 100.0f);
    maudNoiseSuppressor* stereo = Make(maud_layoutStereo, 100.0f);
    maudNoiseState one;
    maudNoiseState two;
    CHECK(maudSuppressNoise(mono, s_out, FRAMES, &one) == maud_success &&
              maudSuppressNoise(stereo, s_stereo, FRAMES, &two) == maud_success,
          "both");
    CHECK(one.noiseDbfs == two.noiseDbfs, "the same noise level: the channels' mean");
    bool same = true;
    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        same = same && s_stereo[2 * i] == s_out[i] && s_stereo[2 * i + 1] == s_out[i];
    }
    CHECK(same, "identical channels come out as the mono input does");
    maudDestroyNoiseSuppressor(mono);
    maudDestroyNoiseSuppressor(stereo);
}

static void TestFloor(void)
{
    // At a floor of -6 dB, steady noise comes down no further.
    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        s_in[i] = 0.0316f * Gaussian();
    }
    memcpy(s_out, s_in, sizeof(s_in));
    maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();
    def.sampleRate = RATE;
    def.floorDb = -6.0f;
    def.highPassHz = 0.0f;
    maudNoiseSuppressor* s = nullptr;
    CHECK(maudCreateNoiseSuppressor(&def, &s) == maud_success &&
              maudSuppressNoise(s, s_out, FRAMES, nullptr) == maud_success,
          "suppressed");
    double down =
        LevelDb(s_in, RATE, FRAMES - HOP, 1) - LevelDb(s_out + HOP, RATE, FRAMES - HOP, 1);
    printf("a -6 dB floor: noise %.2f dB down\n", down);
    CHECK(down > 5.0 && down < 6.3, "down to the floor and no further");
    maudDestroyNoiseSuppressor(s);
}

static void TestHighPass(void)
{
    // A loud 30 Hz hum: the 100 Hz high-pass takes it down by its
    // response there (about 21 dB) before suppression even starts.
    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        s_in[i] = 0.3f * sinf(2.0f * PI * 30.0f * (float)i / RATE);
    }
    memcpy(s_out, s_in, sizeof(s_in));
    maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();
    def.sampleRate = RATE;
    def.floorDb = -6.0f;
    maudNoiseSuppressor* s = nullptr;
    CHECK(maudCreateNoiseSuppressor(&def, &s) == maud_success &&
              maudSuppressNoise(s, s_out, FRAMES, nullptr) == maud_success,
          "a hum");
    double down =
        LevelDb(s_in, RATE, FRAMES - HOP, 1) - LevelDb(s_out + HOP, RATE, FRAMES - HOP, 1);
    printf("a 30 Hz hum: %.1f dB down\n", down);
    CHECK(down > 20.0, "the hum taken out");
    maudDestroyNoiseSuppressor(s);
}

static void TestRefused(void)
{
    maudNoiseSuppressor* s = (maudNoiseSuppressor*)&s_state;
    maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();
    def.sampleRate = 44110;
    CHECK(maudCreateNoiseSuppressor(&def, &s) == maud_errorInvalid && s == nullptr,
          "a rate not a multiple of 100");
    def = maudDefaultNoiseSuppressorDef();
    def.floorDb = -41.0f;
    CHECK(maudCreateNoiseSuppressor(&def, &s) == maud_errorInvalid, "a floor out of range");
    def.floorDb = NAN;
    CHECK(maudCreateNoiseSuppressor(&def, &s) == maud_errorInvalid, "a floor not a number");
    def = maudDefaultNoiseSuppressorDef();
    def.highPassHz = 10.0f;
    CHECK(maudCreateNoiseSuppressor(&def, &s) == maud_errorInvalid, "a corner out of range");
    def = maudDefaultNoiseSuppressorDef();
    def.cookie = 0;
    CHECK(maudCreateNoiseSuppressor(&def, &s) == maud_errorInvalid, "no cookie");
    CHECK(maudCreateNoiseSuppressor(nullptr, &s) == maud_errorInvalid, "no def");
    CHECK(maudSuppressNoise(nullptr, s_out, 1, nullptr) == maud_errorInvalid, "no suppressor");
    def = maudDefaultNoiseSuppressorDef();
    CHECK(maudCreateNoiseSuppressor(&def, &s) == maud_success, "a suppressor");
    CHECK(maudSuppressNoise(s, nullptr, 1, nullptr) == maud_errorInvalid &&
              maudSuppressNoise(s, nullptr, 0, nullptr) == maud_success,
          "no frames, with and without a count");
    maudDestroyNoiseSuppressor(s);
    maudDestroyNoiseSuppressor(nullptr);
}

static void TestNoAllocation(void)
{
    maudNoiseSuppressor* s = Make(maud_layoutStereo, 100.0f);
    int before = s_allocations;
    CHECK(maudSuppressNoise(s, s_stereo, FRAMES, nullptr) == maud_success, "processed");
    CHECK(s_allocations == before, "without allocating");
    maudDestroyNoiseSuppressor(s);
}

int main(void)
{
    TestSteadyNoise();
    TestBursts();
    TestLatencyAndCuts();
    TestChannels();
    TestFloor();
    TestHighPass();
    TestRefused();
    TestNoAllocation();
    return s_failures == 0 ? 0 : 1;
}
