// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The echo canceller's linear stage: a render of noise heard through a
// fixed room response (10 ms late, 60 ms long) is taken down 25 dB or
// more within two seconds at 16 kHz and at 48 kHz, and it reports
// having adapted with a low leakage; a near end of noise at about the
// echo's level comes through at its level with the echo 12 dB under it,
// and the filter holds between its bursts (the residual is the
// suppressor's); the output lags by exactly one block and does not
// depend on how the frames are cut; a
// silent render leaves the capture as the notch leaves it; refusals;
// and no allocation while processing. (Its quality on recorded speech
// is measured outside the tests, against Speex's.)

#include "test_harness.h"

#include "maul-audio/voice.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SECONDS    4u
#define MAX_RATE   48000u
#define MAX_FRAMES (MAX_RATE * SECONDS)

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

static uint32_t s_state = 11u;

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

static float s_render[MAX_FRAMES];
static float s_echo[MAX_FRAMES];
static float s_near[MAX_FRAMES];
static float s_capture[MAX_FRAMES];
static float s_other[MAX_FRAMES];

// The render (noise at -20 dBFS), its echo through a room response 10 ms
// late with 60 ms of exponentially decaying taps, and a near end of
// noise bursts (a quarter second on, a quarter off) from the third
// second on, at the echo's level when near is set.
static void Scene(uint32_t rate, uint32_t frames, bool near)
{
    s_state = 11u;
    uint32_t delay = rate / 100;
    uint32_t taps = rate * 6 / 100;
    static float response[MAX_RATE * 6 / 100];
    for (uint32_t t = 0; t < taps; ++t)
    {
        response[t] = 0.08f * Gaussian() * expf(-6.0f * (float)t / (float)taps);
    }
    for (uint32_t i = 0; i < frames; ++i)
    {
        s_render[i] = 0.1f * Gaussian();
    }
    for (uint32_t i = 0; i < frames; ++i)
    {
        float sum = 0.0f;
        for (uint32_t t = 0; t < taps && t + delay <= i; ++t)
        {
            sum += response[t] * s_render[i - delay - t];
        }
        s_echo[i] = sum;
        bool speaks = near && i >= 2 * rate && (i / (rate / 4)) % 2 == 0;
        s_near[i] = speaks ? 0.05f * Gaussian() : 0.0f;
        s_capture[i] = s_echo[i] + s_near[i];
    }
}

static double Energy(const float* x, uint32_t from, uint32_t to)
{
    double sum = 0.0;
    for (uint32_t i = from; i < to; ++i)
    {
        sum += (double)x[i] * (double)x[i];
    }
    return sum;
}

static maudEchoCanceller* Create(uint32_t rate)
{
    maudEchoCancellerDef def = maudDefaultEchoCancellerDef();
    def.sampleRate = rate;
    def.allocator = (maudAllocator){Counted, Release, nullptr};
    maudEchoCanceller* c = nullptr;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_success && c != nullptr, "a canceller");
    return c;
}

// Runs the capture through a canceller in chunks of chunk frames.
static maudEchoState Run(maudEchoCanceller* c, float* capture, uint32_t frames, uint32_t chunk)
{
    maudEchoState state = {0};
    for (uint32_t at = 0; at < frames; at += chunk)
    {
        uint32_t count = frames - at < chunk ? frames - at : chunk;
        CHECK(maudCancelEcho(c, capture + at, s_render + at, count, &state) == maud_success,
              "cancel");
    }
    return state;
}

static void TestConverges(uint32_t rate, uint32_t block)
{
    uint32_t frames = rate * SECONDS;
    Scene(rate, frames, false);
    maudEchoCanceller* c = Create(rate);
    int before = s_allocations;
    maudEchoState state = Run(c, s_capture, frames, rate / 100);
    CHECK(s_allocations == before, "no allocation while processing");
    // The output lags by a block: compare it with the echo a block back.
    double erle = 10.0 * log10(Energy(s_echo, 3 * rate - block, frames - block) /
                               Energy(s_capture, 3 * rate, frames));
    printf("%u Hz: %.1f dB taken from the echo, leakage %.4f\n", rate, erle, (double)state.leakage);
    CHECK(erle > 25.0, "the echo taken down 25 dB or more");
    CHECK(state.adapted && state.leakage < 0.05f && state.frames == frames, "adapted, little left");
    maudDestroyEchoCanceller(c);
}

static void TestDoubleTalk(void)
{
    const uint32_t rate = 16000;
    const uint32_t block = 128;
    uint32_t frames = rate * SECONDS;
    Scene(rate, frames, true);
    maudEchoCanceller* c = Create(rate);
    (void)Run(c, s_capture, frames, 160);
    // From the third second on: while the near end speaks, the output's
    // level and what in it is not the near end; between its bursts, the
    // echo still taken.
    double out = 0.0;
    double error = 0.0;
    double near = 0.0;
    double gap = 0.0;
    double echo = 0.0;
    for (uint32_t i = 2 * rate + block; i < frames; ++i)
    {
        double x = (double)s_capture[i];
        double n = (double)s_near[i - block];
        if (n != 0.0)
        {
            out += x * x;
            error += (x - n) * (x - n);
            near += n * n;
        }
        else
        {
            gap += x * x;
            echo += (double)s_echo[i - block] * (double)s_echo[i - block];
        }
    }
    double level = 10.0 * log10(out / near);
    double rest = 10.0 * log10(error / near);
    double held = 10.0 * log10(echo / gap);
    printf("double talk: the near end at %+.2f dB, the rest %.1f dB under it; between, %.1f dB "
           "taken\n",
           level, rest, held);
    CHECK(fabs(level) < 1.0, "the near end comes through");
    CHECK(rest < -12.0, "with little echo left over it");
    CHECK(held > 20.0, "and the filter holds between its bursts");
    maudDestroyEchoCanceller(c);
}

static void TestFraming(void)
{
    const uint32_t rate = 16000;
    uint32_t frames = rate * 2;
    Scene(rate, frames, true);
    memcpy(s_other, s_capture, frames * sizeof(float));
    maudEchoCanceller* a = Create(rate);
    maudEchoCanceller* b = Create(rate);
    (void)Run(a, s_capture, frames, 480);
    uint32_t at = 0;
    for (uint32_t n = 1; at < frames; n = n % 37 + 1)
    {
        uint32_t count = frames - at < n ? frames - at : n;
        CHECK(maudCancelEcho(b, s_other + at, s_render + at, count, nullptr) == maud_success,
              "cancel");
        at += count;
    }
    CHECK(memcmp(s_capture, s_other, frames * sizeof(float)) == 0, "the same however cut");
    maudDestroyEchoCanceller(a);
    maudDestroyEchoCanceller(b);
}

// With nothing played, the capture comes out as the notch leaves it: a
// tone at 1 kHz keeps its level.
static void TestSilentRender(void)
{
    const uint32_t rate = 16000;
    uint32_t frames = rate;
    for (uint32_t i = 0; i < frames; ++i)
    {
        s_render[i] = 0.0f;
        s_capture[i] = 0.1f * sinf(2.0f * 3.14159265f * 1000.0f * (float)i / (float)rate);
        s_other[i] = s_capture[i];
    }
    maudEchoCanceller* c = Create(rate);
    (void)Run(c, s_capture, frames, 160);
    double change = 10.0 * log10(Energy(s_capture, rate / 2, frames) /
                                 Energy(s_other, rate / 2 - 128, frames - 128));
    printf("no render: a 1 kHz tone changed by %.2f dB\n", change);
    CHECK(fabs(change) < 0.5, "the capture kept");
    bool silent = true;
    for (uint32_t i = 0; i < 128; ++i)
    {
        silent = silent && s_capture[i] == 0.0f;
    }
    CHECK(silent && s_capture[129] != 0.0f, "a block late: the first block out is silence");
    maudDestroyEchoCanceller(c);
}

static void TestRefused(void)
{
    maudEchoCanceller* c = nullptr;
    maudEchoCancellerDef def = maudDefaultEchoCancellerDef();
    CHECK(maudCreateEchoCanceller(nullptr, &c) == maud_errorInvalid && c == nullptr, "no def");
    CHECK(maudCreateEchoCanceller(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    def.cookie = 0;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_errorInvalid, "no cookie");
    def = maudDefaultEchoCancellerDef();
    def.sampleRate = 7999;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_errorInvalid, "a rate too low");
    def = maudDefaultEchoCancellerDef();
    def.tailSeconds = 1.5f;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_errorInvalid, "a path too long");
    def.tailSeconds = NAN;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_errorInvalid, "a path not a number");
    c = Create(16000);
    float x = 0.0f;
    CHECK(maudCancelEcho(nullptr, &x, &x, 1, nullptr) == maud_errorInvalid, "no canceller");
    CHECK(maudCancelEcho(c, nullptr, &x, 1, nullptr) == maud_errorInvalid, "no capture");
    CHECK(maudCancelEcho(c, &x, nullptr, 1, nullptr) == maud_errorInvalid, "no render");
    CHECK(maudCancelEcho(c, nullptr, nullptr, 0, nullptr) == maud_success, "no frames");
    maudDestroyEchoCanceller(c);
    maudDestroyEchoCanceller(nullptr);
}

int main(void)
{
    TestConverges(16000, 128);
    TestConverges(48000, 512);
    TestDoubleTalk();
    TestFraming();
    TestSilentRender();
    TestRefused();
    return s_failures == 0 ? 0 : 1;
}
