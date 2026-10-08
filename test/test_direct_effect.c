// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Direct effects: default params pass the signal unchanged; the host's
// gain applies and ramps; air absorption at 100 m, occlusion with
// transmission and directivity reach each band's level within 0.2 dB, a
// band never more than 24 dB below the loudest
// (band means measured densely on the effect's impulse response); an
// occlusion step makes no click; air absorption follows ISO 9613 part 1
// (the standard's table at 20 degrees C and 50 %); directivity follows
// the weighted dipole; bad calls write nothing; processing allocates
// nothing.

#include "test_harness.h"

#include "maul-audio/direct.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

enum
{
    TAPS = 8192
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

static maudDirectEffect* Create(void)
{
    maudDirectEffectDef def = maudDefaultDirectEffectDef();
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudDirectEffect* effect = nullptr;
    CHECK(maudCreateDirectEffect(&def, &effect) == maud_success, "an effect");
    return effect;
}

static float s_impulse[TAPS];
static float s_response[TAPS];

// The effect's impulse response with params held from the first call.
static void Respond(const maudDirectParams* params)
{
    maudDirectEffect* effect = Create();
    memset(s_impulse, 0, sizeof(s_impulse));
    s_impulse[0] = 1.0f;
    CHECK(maudProcessDirect(effect, params, s_impulse, s_response, TAPS) == maud_success,
          "process");
    maudDestroyDirectEffect(effect);
}

static double Level(double hz)
{
    double re = 0.0;
    double im = 0.0;
    for (int n = 0; n < TAPS; ++n)
    {
        double angle = -2.0 * PI * hz * n / 48000.0;
        re += (double)s_response[n] * cos(angle);
        im += (double)s_response[n] * sin(angle);
    }
    return 10.0 * log10(re * re + im * im);
}

static double BandMean(int band)
{
    const double edges[4] = {20.0, 800.0, 8000.0, 20000.0};
    double sum = 0.0;
    for (int i = 0; i < 40; ++i)
    {
        sum += Level(edges[band] * pow(edges[band + 1] / edges[band], ((double)i + 0.5) / 40.0));
    }
    return sum / 40.0;
}

// The response's band means against expected linear band gains.
static double Worst(const float* expected)
{
    double worst = 0.0;
    for (int b = 0; b < 3; ++b)
    {
        double error = BandMean(b) - 20.0 * log10((double)expected[b]);
        printf("  band %d: %+.2f dB off\n", b, error);
        worst = fmax(worst, fabs(error));
    }
    return worst;
}

static void TestPassThrough(void)
{
    maudDirectEffect* effect = Create();
    maudDirectParams params = maudDefaultDirectParams();
    float in[256];
    float out[256];
    for (int n = 0; n < 256; ++n)
    {
        in[n] = sinf(0.1f * (float)n);
    }
    CHECK(maudProcessDirect(effect, &params, in, out, 256) == maud_success &&
              memcmp(in, out, sizeof(in)) == 0,
          "default params pass the signal unchanged");
    params.gain = 0.5f;
    CHECK(maudProcessDirect(effect, &params, in, out, 256) == maud_success, "process");
    bool ramped = true;
    for (int n = 0; n < 256; ++n)
    {
        float gain = 1.0f - 0.5f * (float)(n + 1) / 256.0f;
        ramped = ramped && fabsf(out[n] - gain * in[n]) < 1e-6f;
    }
    CHECK(ramped, "the host's gain ramps across a call");
    // Leaving a clear path: behind a wall that keeps the lows and stops
    // the highs, a high tone is filtered from the block the wall starts
    // in, not passed on at the loudest band's level as a flat filter's
    // would be.
    float high[256];
    for (int n = 0; n < 256; ++n)
    {
        high[n] = sinf(2.5f * (float)n);
    }
    params = maudDefaultDirectParams();
    params.occlusion = 1.0f;
    params.transmission[0] = 0.3f;
    params.transmission[1] = 0.1f;
    params.transmission[2] = 0.02f;
    CHECK(maudProcessDirect(effect, &params, high, out, 256) == maud_success, "behind a wall");
    float loudest = 0.0f;
    for (int n = 224; n < 256; ++n)
    {
        loudest = fmaxf(loudest, fabsf(out[n]));
    }
    CHECK(loudest < 0.15f, "the wall's highs heard in the block it starts");
    maudDestroyDirectEffect(effect);
}

static void TestBands(void)
{
    maudDirectEffectDef def = maudDefaultDirectEffectDef();
    maudDirectParams params = maudDefaultDirectParams();
    params.distance = 100.0f;
    float expected[3];
    // The high band's 26.7 dB falls past the floor, 24 dB below the
    // loudest band, and stops there.
    for (int b = 0; b < 3; ++b)
    {
        expected[b] = fmaxf(expf(-def.airAbsorption[b] * 100.0f), powf(10.0f, -24.0f / 20.0f));
    }
    printf("air absorption at 100 m, floored:\n");
    Respond(&params);
    CHECK(Worst(expected) < 0.2, "air absorption reaches each band");
    params = maudDefaultDirectParams();
    params.occlusion = 0.75f;
    params.transmission[0] = 0.5f;
    params.transmission[1] = 0.2f;
    params.transmission[2] = 0.05f;
    params.gain = 0.8f;
    for (int b = 0; b < 3; ++b)
    {
        expected[b] = 0.8f * (0.25f + 0.75f * params.transmission[b]);
    }
    printf("occlusion 0.75 with transmission:\n");
    Respond(&params);
    CHECK(Worst(expected) < 0.2, "occlusion and transmission reach each band");
    params = maudDefaultDirectParams();
    params.directivity[0] = 1.0f;
    params.directivity[1] = 0.6f;
    params.directivity[2] = 0.3f;
    params.distance = 30.0f;
    for (int b = 0; b < 3; ++b)
    {
        expected[b] = params.directivity[b] * expf(-def.airAbsorption[b] * 30.0f);
    }
    printf("directivity and 30 m:\n");
    Respond(&params);
    CHECK(Worst(expected) < 0.2, "directivity reaches each band");
}

// A 1 kHz sine, its path closing over one call: occlusion 0 to 1 with
// transmission (0.5, 0.1, 0.02). No step exceeds the sine's own.
static void TestNoClick(void)
{
    maudDirectEffect* effect = Create();
    static float in[4800];
    static float out[4800];
    for (int n = 0; n < 4800; ++n)
    {
        in[n] = (float)sin(2.0 * PI * 1000.0 * n / 48000.0);
    }
    maudDirectParams params = maudDefaultDirectParams();
    long before = s_allocations;
    CHECK(maudProcessDirect(effect, &params, in, out, 960) == maud_success, "process");
    params.occlusion = 1.0f;
    params.transmission[0] = 0.5f;
    params.transmission[1] = 0.1f;
    params.transmission[2] = 0.02f;
    for (int at = 960; at < 4800; at += 480)
    {
        CHECK(maudProcessDirect(effect, &params, in + at, out + at, 480) == maud_success,
              "process");
    }
    CHECK(s_allocations == before, "processing allocates nothing");
    float step = 0.0f;
    for (int n = 1; n < 4800; ++n)
    {
        step = fmaxf(step, fabsf(out[n] - out[n - 1]));
    }
    float sine = (float)(2.0 * sin(PI * 1000.0 / 48000.0));
    printf("largest step %.4f against the sine's %.4f\n", (double)step, (double)sine);
    CHECK(step <= sine * 1.001f, "an occlusion step makes no click");
    float peak = 0.0f;
    for (int n = 4320; n < 4800; ++n)
    {
        peak = fmaxf(peak, fabsf(out[n]));
    }
    printf("closed level at 1 kHz: %.2f dB\n", 20.0 * log10((double)peak));
    CHECK(peak < 0.5f && peak > 0.05f, "and the closed path transmits");
    maudDestroyDirectEffect(effect);
}

// Runs calls of 480 frames of the noise in s_impulse's place through an
// effect, the last call's output in out.
static void Run(maudDirectEffect* effect, const maudDirectParams* params, int calls, float* out)
{
    static float noise[480];
    uint32_t seed = 17;
    for (int c = 0; c < calls; ++c)
    {
        for (int n = 0; n < 480; ++n)
        {
            seed = seed * 1664525u + 1013904223u;
            noise[n] = (float)(int32_t)(seed >> 8) / 8388608.0f - 1.0f;
        }
        CHECK(maudProcessDirect(effect, params, noise, out, 480) == maud_success, "process");
    }
}

// A source moving from 20 m to 25 m (its high band some 1.3 dB lower) is
// refitted: once settled, it sounds as an effect that began at 25 m.
// After a stretch of flat bands, what an effect heard before it no
// longer matters.
static void TestHistory(void)
{
    maudDirectParams near = maudDefaultDirectParams();
    near.distance = 20.0f;
    maudDirectParams far = near;
    far.distance = 25.0f;
    maudDirectEffect* moved = Create();
    maudDirectEffect* fresh = Create();
    float a[480];
    float b[480];
    Run(moved, &near, 10, a);
    Run(moved, &far, 40, a);
    Run(fresh, &far, 40, b);
    float apart = 0.0f;
    for (int n = 0; n < 480; ++n)
    {
        apart = fmaxf(apart, fabsf(a[n] - b[n]));
    }
    printf("moved against fresh: %.2e\n", (double)apart);
    CHECK(apart < 1e-4f, "a small move is refitted");
    maudDirectParams wall = maudDefaultDirectParams();
    wall.occlusion = 1.0f;
    wall.transmission[0] = 0.5f;
    wall.transmission[1] = 0.1f;
    wall.transmission[2] = 0.02f;
    maudDirectParams clear = maudDefaultDirectParams();
    maudDirectEffect* walled = Create();
    maudDirectEffect* open = Create();
    Run(walled, &wall, 5, a);
    Run(open, &clear, 5, b);
    Run(walled, &clear, 3, a);
    Run(open, &clear, 3, b);
    Run(walled, &wall, 1, a);
    Run(open, &wall, 1, b);
    CHECK(memcmp(a, b, sizeof(a)) == 0, "a flat stretch forgets what came before");
    maudDestroyDirectEffect(moved);
    maudDestroyDirectEffect(fresh);
    maudDestroyDirectEffect(walled);
    maudDestroyDirectEffect(open);
}

static void TestAir(void)
{
    float air[3];
    CHECK(maudGetAirAbsorption(20.0f, 50.0f, air) == maud_success, "air absorption");
    maudDirectEffectDef def = maudDefaultDirectEffectDef();
    CHECK(memcmp(air, def.airAbsorption, sizeof(air)) == 0, "the default is 20 degrees, 50 %");
    // The table of ISO 9613 part 1 at 20 degrees C and 50 %: 4.66 dB/km at 1 kHz
    // and 105 dB/km at 8 kHz lie inside the middle band's mean, which
    // rises with frequency across it: the mean in dB/km lies between.
    double middle = (double)air[1] * 20.0 / log(10.0) * 1000.0;
    double high = (double)air[2] * 20.0 / log(10.0) * 1000.0;
    printf("band means: %.2f, %.2f, %.2f dB/km\n", (double)air[0] * 20.0 / log(10.0) * 1000.0,
           middle, high);
    CHECK(middle > 4.66 && middle < 105.0 && high > 105.0, "between the standard's values");
    CHECK(fabs(middle - 25.7) < 0.5 && fabs(high - 266.8) < 3.0, "as research measured");
    float dry[3];
    CHECK(maudGetAirAbsorption(20.0f, 10.0f, dry) == maud_success && dry[2] != air[2],
          "humidity matters");
    CHECK(maudGetAirAbsorption(60.0f, 50.0f, dry) == maud_errorInvalid, "too hot");
    CHECK(maudGetAirAbsorption(50.0f, 100.0f, dry) == maud_success &&
              maudGetAirAbsorption(-20.0f, 10.0f, dry) == maud_success,
          "the standard's ends taken");
    CHECK(maudGetAirAbsorption(20.0f, 100.5f, dry) == maud_errorInvalid, "too humid");
    CHECK(maudGetAirAbsorption(20.0f, NAN, dry) == maud_errorInvalid, "no humidity");
}

static void TestDirectivity(void)
{
    maudDirectivityPattern pattern = {{0.0f, 0.5f, 1.0f}, {1.0f, 1.0f, 2.0f}};
    float d[3];
    CHECK(maudGetDirectivity(&pattern, (maudVector3){0.0f, 0.0f, -3.0f}, d) == maud_success &&
              d[0] == 1.0f && d[1] == 1.0f && d[2] == 1.0f,
          "ahead: full");
    CHECK(maudGetDirectivity(&pattern, (maudVector3){2.0f, 0.0f, 0.0f}, d) == maud_success &&
              d[0] == 1.0f && fabsf(d[1] - 0.5f) < 1e-6f && fabsf(d[2]) < 1e-6f,
          "to the side: omni, half, none");
    CHECK(maudGetDirectivity(&pattern, (maudVector3){0.0f, 0.0f, 1.0f}, d) == maud_success &&
              fabsf(d[1]) < 1e-6f && fabsf(d[2] - 1.0f) < 1e-6f,
          "behind: a cardioid's null, a squared dipole's full lobe");
    CHECK(maudGetDirectivity(&pattern, (maudVector3){0.0f, 0.0f, 0.0f}, d) == maud_success &&
              d[1] == 1.0f,
          "a zero direction is ahead");
    maudDirectivityPattern flat = {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 0.0f}};
    CHECK(maudGetDirectivity(&flat, (maudVector3){0.0f, 0.0f, 1.0f}, d) == maud_success &&
              d[2] == 1.0f,
          "a power of 0: the same all round");
    pattern.weight[0] = 1.5f;
    CHECK(maudGetDirectivity(&pattern, (maudVector3){0.0f, 0.0f, -1.0f}, d) == maud_errorInvalid,
          "a weight above 1");
}

static void TestMisuse(void)
{
    maudDirectEffect* effect = Create();
    maudDirectParams params = maudDefaultDirectParams();
    float in[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    float out[8] = {9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f, 9.0f};
    params.occlusion = 1.5f;
    CHECK(maudProcessDirect(effect, &params, in, out, 8) == maud_errorInvalid, "occlusion 1.5");
    params = maudDefaultDirectParams();
    params.gain = -1.0f;
    CHECK(maudProcessDirect(effect, &params, in, out, 8) == maud_errorInvalid, "a negative gain");
    params.gain = 0.0f;
    CHECK(maudProcessDirect(effect, &params, in, out, 8) == maud_success, "a gain of 0");
    for (int n = 0; n < 8; ++n)
    {
        out[n] = 9.0f;
    }
    params = maudDefaultDirectParams();
    params.distance = INFINITY;
    CHECK(maudProcessDirect(effect, &params, in, out, 8) == maud_errorInvalid, "infinite");
    params = maudDefaultDirectParams();
    params.transmission[2] = NAN;
    CHECK(maudProcessDirect(effect, &params, in, out, 8) == maud_errorInvalid, "NaN");
    CHECK(out[0] == 9.0f, "nothing written by a bad call");
    CHECK(maudProcessDirect(effect, nullptr, in, out, 8) == maud_errorInvalid, "no params");
    params = maudDefaultDirectParams();
    CHECK(maudProcessDirect(effect, &params, nullptr, nullptr, 0) == maud_success, "no frames");
    CHECK(maudResetDirectEffect(nullptr) == maud_errorInvalid, "reset nothing");
    CHECK(maudResetDirectEffect(effect) == maud_success, "reset");
    maudDestroyDirectEffect(effect);
    maudDestroyDirectEffect(nullptr);
    maudDirectEffectDef def = maudDefaultDirectEffectDef();
    maudDirectEffect* none = (maudDirectEffect*)&def;
    def.sampleRate = 16000.0f;
    CHECK(maudCreateDirectEffect(&def, &none) == maud_errorInvalid && none == nullptr, "16 kHz");
    def = maudDefaultDirectEffectDef();
    def.airAbsorption[1] = -1.0f;
    CHECK(maudCreateDirectEffect(&def, &none) == maud_errorInvalid, "negative absorption");
    def = maudDefaultDirectEffectDef();
    def.cookie = 0;
    CHECK(maudCreateDirectEffect(&def, &none) == maud_errorInvalid, "no cookie");
}

int main(void)
{
    TestPassThrough();
    TestBands();
    TestNoClick();
    TestHistory();
    TestAir();
    TestDirectivity();
    TestMisuse();
    return s_failures == 0 ? 0 : 1;
}
