// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every refusal reverb.h and direct.h document, each made on its own:
// defs with a rate, delay or air absorption out of range, parameters
// with one band out of range, a directivity pattern out of range, and
// each pointer a function needs, NULL; the bounds themselves taken.

#include "test_harness.h"

#include "maul-audio/direct.h"
#include "maul-audio/reverb.h"

#include <math.h>
#include <stdlib.h>

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    return malloc(size);
}

static const maudAllocator s_half = {Alloc, nullptr, nullptr};

static maudReverbParams ReverbParams(void)
{
    maudReverbParams p = {0};
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        p.reverbTime[b] = 1.0f;
    }
    return p;
}

static void TestReverb(void)
{
    maudReverbDef def = maudDefaultReverbDef();
    maudReverb* r = (maudReverb*)&r;
    CHECK(maudCreateReverb(nullptr, &r) == maud_errorInvalid && r == nullptr, "no def");
    CHECK(maudCreateReverb(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudReverbDef bad[7] = {def, def, def, def, def, def, def};
    bad[0].cookie = 0;
    bad[1].sampleRate = 44099;
    bad[2].sampleRate = 384001;
    bad[3].maxDelay = -0.001f;
    bad[4].maxDelay = 4.001f;
    bad[5].maxDelay = NAN;
    bad[6].allocator = s_half;
    bool refused = true;
    for (int i = 0; i < 7; ++i)
    {
        r = (maudReverb*)&r;
        bool one = maudCreateReverb(&bad[i], &r) == maud_errorInvalid && r == nullptr;
        if (!one)
        {
            printf("reverb def %d not refused\n", i);
            maudDestroyReverb(r);
        }
        refused = refused && one;
    }
    CHECK(refused, "a cookie, rate, delay or allocator out of range");
    maudReverbDef edge = def;
    edge.sampleRate = 44100;
    edge.maxDelay = 4.0f;
    CHECK(maudCreateReverb(&edge, &r) == maud_success, "44.1 kHz and 4 s taken");
    maudDestroyReverb(r);
    def.maxDelay = 0.1f;
    CHECK(maudCreateReverb(&def, &r) == maud_success, "a reverb");
    float in[16] = {0};
    float b0[16];
    float b1[16];
    float b2[16];
    float b3[16];
    float* bed[4] = {b0, b1, b2, b3};
    maudReverbParams good = ReverbParams();
    CHECK(maudProcessReverb(r, &good, in, bed, 16) == maud_success, "valid parameters");
    maudReverbParams params[11];
    for (int i = 0; i < 11; ++i)
    {
        params[i] = good;
    }
    params[0].reverbTime[0] = 0.099f;
    params[1].reverbTime[1] = 20.01f;
    params[2].level[2] = -96.01f;
    params[3].level[0] = 24.01f;
    params[4].tailTime[1] = 0.05f;
    params[5].tailTime[2] = 20.5f;
    params[6].tailLevel[0] = -97.0f;
    params[7].tailLevel[1] = 24.5f;
    params[8].delay = -0.001f;
    params[9].delay = 0.2f;
    params[10].reverbTime[2] = NAN;
    refused = true;
    for (int i = 0; i < 11; ++i)
    {
        bool one = maudProcessReverb(r, &params[i], in, bed, 16) == maud_errorInvalid;
        if (!one)
        {
            printf("reverb parameters %d not refused\n", i);
        }
        refused = refused && one;
    }
    CHECK(refused, "each band's time, level, tail and the delay out of range");
    maudReverbParams bounds = good;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        bounds.reverbTime[b] = b == 0 ? 0.1f : 20.0f;
        bounds.level[b] = b == 0 ? -96.0f : 24.0f;
        bounds.tailTime[b] = b == 2 ? 0.0f : 0.1f;
        bounds.tailLevel[b] = b == 1 ? -96.0f : 24.0f;
    }
    CHECK(maudProcessReverb(r, &bounds, in, bed, 16) == maud_success, "the bounds taken");
    float* holed[4] = {b0, b1, nullptr, b3};
    CHECK(maudProcessReverb(nullptr, &good, in, bed, 16) == maud_errorInvalid &&
              maudProcessReverb(r, nullptr, in, bed, 16) == maud_errorInvalid &&
              maudProcessReverb(r, &good, nullptr, bed, 16) == maud_errorInvalid &&
              maudProcessReverb(r, &good, in, nullptr, 16) == maud_errorInvalid &&
              maudProcessReverb(r, &good, in, holed, 16) == maud_errorInvalid,
          "no reverb, parameters, input, bed or a bed channel");
    CHECK(maudProcessReverb(r, &good, nullptr, nullptr, 0) == maud_success,
          "no frames need no buffers");
    CHECK(maudResetReverb(nullptr) == maud_errorInvalid && maudResetReverb(r) == maud_success,
          "a reset needs a reverb");
    maudDestroyReverb(r);
}

static void TestDirect(void)
{
    maudDirectEffectDef def = maudDefaultDirectEffectDef();
    maudDirectEffect* e = (maudDirectEffect*)&e;
    CHECK(maudCreateDirectEffect(nullptr, &e) == maud_errorInvalid && e == nullptr, "no def");
    CHECK(maudCreateDirectEffect(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudDirectEffectDef bad[6] = {def, def, def, def, def, def};
    bad[0].cookie = 0;
    bad[1].sampleRate = 31999;
    bad[2].sampleRate = 384001;
    bad[3].airAbsorption[1] = -0.001f;
    bad[4].airAbsorption[2] = INFINITY;
    bad[5].allocator = s_half;
    bool refused = true;
    for (int i = 0; i < 6; ++i)
    {
        e = (maudDirectEffect*)&e;
        bool one = maudCreateDirectEffect(&bad[i], &e) == maud_errorInvalid && e == nullptr;
        if (!one)
        {
            printf("direct def %d not refused\n", i);
            maudDestroyDirectEffect(e);
        }
        refused = refused && one;
    }
    CHECK(refused, "a cookie, rate, air or allocator out of range");
    maudDirectEffectDef edge = def;
    edge.sampleRate = 32000;
    CHECK(maudCreateDirectEffect(&edge, &e) == maud_success, "32 kHz taken");
    maudDestroyDirectEffect(e);
    edge.sampleRate = 384000;
    CHECK(maudCreateDirectEffect(&edge, &e) == maud_success, "384 kHz taken");
    maudDestroyDirectEffect(e);
    CHECK(maudCreateDirectEffect(&def, &e) == maud_success, "an effect");
    maudDirectParams good = maudDefaultDirectParams();
    float in[16] = {0};
    float out[16];
    CHECK(maudProcessDirect(e, &good, in, out, 16) == maud_success, "valid parameters");
    maudDirectParams params[9];
    for (int i = 0; i < 9; ++i)
    {
        params[i] = good;
    }
    params[0].gain = -0.001f;
    params[1].gain = INFINITY;
    params[2].distance = -1.0f;
    params[3].distance = NAN;
    params[4].occlusion = 1.001f;
    params[5].occlusion = -0.001f;
    params[6].transmission[1] = 1.5f;
    params[7].directivity[2] = -0.5f;
    params[8].transmission[0] = NAN;
    refused = true;
    for (int i = 0; i < 9; ++i)
    {
        bool one = maudProcessDirect(e, &params[i], in, out, 16) == maud_errorInvalid;
        if (!one)
        {
            printf("direct parameters %d not refused\n", i);
        }
        refused = refused && one;
    }
    CHECK(refused, "gain, distance, occlusion and each band out of range");
    CHECK(maudProcessDirect(nullptr, &good, in, out, 16) == maud_errorInvalid &&
              maudProcessDirect(e, nullptr, in, out, 16) == maud_errorInvalid &&
              maudProcessDirect(e, &good, nullptr, out, 16) == maud_errorInvalid &&
              maudProcessDirect(e, &good, in, nullptr, 16) == maud_errorInvalid,
          "no effect, parameters, input or output");
    CHECK(maudProcessDirect(e, &good, nullptr, nullptr, 0) == maud_success,
          "no frames need no buffers");
    maudDestroyDirectEffect(e);
    maudDirectivityPattern pattern = {{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}};
    float gains[MAUD_DIRECT_BANDS];
    maudVector3 ahead = {0.0f, 0.0f, -1.0f};
    CHECK(maudGetDirectivity(&pattern, ahead, gains) == maud_success, "a pattern");
    maudDirectivityPattern patterns[4] = {pattern, pattern, pattern, pattern};
    patterns[0].weight[0] = 1.01f;
    patterns[1].weight[2] = -0.01f;
    patterns[2].power[1] = -1.0f;
    patterns[3].power[0] = INFINITY;
    refused = true;
    for (int i = 0; i < 4; ++i)
    {
        refused = refused && maudGetDirectivity(&patterns[i], ahead, gains) == maud_errorInvalid;
    }
    CHECK(refused, "a weight or power out of range");
    maudVector3 nowhere = {0.0f, NAN, 0.0f};
    CHECK(maudGetDirectivity(nullptr, ahead, gains) == maud_errorInvalid &&
              maudGetDirectivity(&pattern, ahead, nullptr) == maud_errorInvalid &&
              maudGetDirectivity(&pattern, nowhere, gains) == maud_errorInvalid,
          "no pattern, nowhere to write, or a direction not finite");
}

int main(void)
{
    TestReverb();
    TestDirect();
    return s_failures == 0 ? 0 : 1;
}
