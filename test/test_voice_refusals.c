// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every def refusal voice.h documents, each made on its own: the voice
// detector's, the gain control's, the noise suppressor's and the echo
// canceller's fields one at a time just past their ranges, each
// maud_errorInvalid with nothing made, and their bounds taken.

#include "test_harness.h"

#include "maul-audio/voice.h"

#include <stdlib.h>

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    return malloc(size);
}

static const maudAllocator s_half = {Alloc, nullptr, nullptr};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void TestVoiceDetector(void)
{
    maudVoiceDetectorDef def = maudDefaultVoiceDetectorDef();
    maudVoiceDetector* d = (maudVoiceDetector*)&d;
    CHECK(maudCreateVoiceDetector(nullptr, &d) == maud_errorInvalid && d == nullptr, "no def");
    CHECK(maudCreateVoiceDetector(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudVoiceDetectorDef bad[7] = {def, def, def, def, def, def, def};
    bad[0].cookie = 0;
    bad[1].sampleRate = 7999;
    bad[2].sampleRate = 384001;
    bad[3].layout = maud_layoutNone;
    bad[4].aggressiveness = 4;
    bad[5].hangoverMilliseconds = 10001;
    bad[6].allocator = s_half;
    bool refused = true;
    for (int i = 0; i < COUNT(bad); ++i)
    {
        d = (maudVoiceDetector*)&d;
        bool one = maudCreateVoiceDetector(&bad[i], &d) == maud_errorInvalid && d == nullptr;
        if (!one)
        {
            printf("voice detector def %d not refused\n", i);
            maudDestroyVoiceDetector(d);
        }
        refused = refused && one;
    }
    CHECK(refused, "a detector def field past its range");
    maudVoiceDetectorDef bounds[4] = {def, def, def, def};
    bounds[0].sampleRate = 8000;
    bounds[1].sampleRate = 384000;
    bounds[2].aggressiveness = 3;
    bounds[3].hangoverMilliseconds = 10000;
    bool taken = true;
    for (int i = 0; i < COUNT(bounds); ++i)
    {
        bool one = maudCreateVoiceDetector(&bounds[i], &d) == maud_success;
        taken = taken && one;
        if (one)
        {
            maudDestroyVoiceDetector(d);
        }
    }
    CHECK(taken, "a detector def's bounds taken");
}

static void TestGainControl(void)
{
    maudGainControlDef def = maudDefaultGainControlDef();
    maudGainControl* g = (maudGainControl*)&g;
    CHECK(maudCreateGainControl(nullptr, &g) == maud_errorInvalid && g == nullptr, "no def");
    CHECK(maudCreateGainControl(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudGainControlDef bad[18];
    for (int i = 0; i < COUNT(bad); ++i)
    {
        bad[i] = def;
    }
    bad[0].cookie = 0;
    bad[1].sampleRate = 7999;
    bad[2].sampleRate = 384001;
    bad[3].layout = maud_layoutNone;
    bad[4].targetDbfs = -40.5f;
    bad[5].targetDbfs = -5.5f;
    bad[6].minGainDb = -30.5f;
    bad[7].minGainDb = 0.5f;
    bad[8].maxGainDb = -0.5f;
    bad[9].maxGainDb = 60.5f;
    bad[10].initialGainDb = def.minGainDb - 0.5f;
    bad[11].initialGainDb = def.maxGainDb + 0.5f;
    bad[12].maxChangeDbPerSecond = 0.5f;
    bad[13].maxChangeDbPerSecond = 30.5f;
    bad[14].maxNoiseDbfs = -80.5f;
    bad[15].maxNoiseDbfs = -19.5f;
    bad[16].aggressiveness = 4;
    bad[17].allocator = s_half;
    bool refused = true;
    for (int i = 0; i < COUNT(bad); ++i)
    {
        g = (maudGainControl*)&g;
        bool one = maudCreateGainControl(&bad[i], &g) == maud_errorInvalid && g == nullptr;
        if (!one)
        {
            printf("gain control def %d not refused\n", i);
            maudDestroyGainControl(g);
        }
        refused = refused && one;
    }
    CHECK(refused, "a gain control def field past its range");
    maudGainControlDef bounds[6] = {def, def, def, def, def, def};
    bounds[0].targetDbfs = -40.0f;
    bounds[1].targetDbfs = -6.0f;
    bounds[2].minGainDb = -30.0f;
    bounds[2].initialGainDb = -30.0f;
    bounds[3].maxGainDb = 60.0f;
    bounds[3].initialGainDb = 60.0f;
    bounds[4].maxChangeDbPerSecond = 1.0f;
    bounds[4].maxNoiseDbfs = -80.0f;
    bounds[5].maxChangeDbPerSecond = 30.0f;
    bounds[5].maxNoiseDbfs = -20.0f;
    bool taken = true;
    for (int i = 0; i < COUNT(bounds); ++i)
    {
        bool one = maudCreateGainControl(&bounds[i], &g) == maud_success;
        if (!one)
        {
            printf("gain control bound %d not taken\n", i);
        }
        taken = taken && one;
        if (one)
        {
            maudDestroyGainControl(g);
        }
    }
    CHECK(taken, "a gain control def's bounds taken");
}

static void TestNoiseSuppressor(void)
{
    maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();
    maudNoiseSuppressor* n = (maudNoiseSuppressor*)&n;
    CHECK(maudCreateNoiseSuppressor(nullptr, &n) == maud_errorInvalid && n == nullptr, "no def");
    CHECK(maudCreateNoiseSuppressor(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudNoiseSuppressorDef bad[10];
    for (int i = 0; i < COUNT(bad); ++i)
    {
        bad[i] = def;
    }
    bad[0].cookie = 0;
    bad[1].sampleRate = 7900;
    bad[2].sampleRate = 384100;
    bad[3].sampleRate = 44150;
    bad[4].floorDb = -40.5f;
    bad[5].floorDb = -5.5f;
    bad[6].highPassHz = 19.5f;
    bad[7].highPassHz = 400.5f;
    bad[8].layout = maud_layoutNone;
    bad[9].allocator = s_half;
    bool refused = true;
    for (int i = 0; i < COUNT(bad); ++i)
    {
        n = (maudNoiseSuppressor*)&n;
        bool one = maudCreateNoiseSuppressor(&bad[i], &n) == maud_errorInvalid && n == nullptr;
        if (!one)
        {
            printf("noise suppressor def %d not refused\n", i);
            maudDestroyNoiseSuppressor(n);
        }
        refused = refused && one;
    }
    CHECK(refused, "a noise suppressor def field past its range");
    maudNoiseSuppressorDef bounds[5] = {def, def, def, def, def};
    bounds[0].sampleRate = 8000;
    bounds[0].highPassHz = 0.0f;
    bounds[1].sampleRate = 384000;
    bounds[2].floorDb = -40.0f;
    bounds[2].highPassHz = 20.0f;
    bounds[3].floorDb = -6.0f;
    bounds[3].highPassHz = 400.0f;
    bounds[4].sampleRate = 44100;
    bool taken = true;
    for (int i = 0; i < COUNT(bounds); ++i)
    {
        bool one = maudCreateNoiseSuppressor(&bounds[i], &n) == maud_success;
        if (!one)
        {
            printf("noise suppressor bound %d not taken\n", i);
        }
        taken = taken && one;
        if (one)
        {
            maudDestroyNoiseSuppressor(n);
        }
    }
    CHECK(taken, "a noise suppressor def's bounds taken");
}

static void TestEchoCanceller(void)
{
    maudEchoCancellerDef def = maudDefaultEchoCancellerDef();
    maudEchoCanceller* c = (maudEchoCanceller*)&c;
    CHECK(maudCreateEchoCanceller(nullptr, &c) == maud_errorInvalid && c == nullptr, "no def");
    CHECK(maudCreateEchoCanceller(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudEchoCancellerDef bad[8];
    for (int i = 0; i < COUNT(bad); ++i)
    {
        bad[i] = def;
    }
    bad[0].cookie = 0;
    bad[1].sampleRate = 7999;
    bad[2].sampleRate = 384001;
    bad[3].tailSeconds = 0.049f;
    bad[4].tailSeconds = 1.01f;
    bad[5].floorDb = -40.5f;
    bad[6].floorDb = -5.5f;
    bad[7].allocator = s_half;
    bool refused = true;
    for (int i = 0; i < COUNT(bad); ++i)
    {
        c = (maudEchoCanceller*)&c;
        bool one = maudCreateEchoCanceller(&bad[i], &c) == maud_errorInvalid && c == nullptr;
        if (!one)
        {
            printf("echo canceller def %d not refused\n", i);
            maudDestroyEchoCanceller(c);
        }
        refused = refused && one;
    }
    CHECK(refused, "an echo canceller def field past its range");
    maudEchoCancellerDef bounds[4] = {def, def, def, def};
    bounds[0].sampleRate = 8000;
    bounds[0].tailSeconds = 0.05f;
    bounds[1].sampleRate = 384000;
    bounds[1].tailSeconds = 0.05f;
    bounds[2].tailSeconds = 1.0f;
    bounds[2].floorDb = -40.0f;
    bounds[3].floorDb = -6.0f;
    bool taken = true;
    for (int i = 0; i < COUNT(bounds); ++i)
    {
        bool one = maudCreateEchoCanceller(&bounds[i], &c) == maud_success;
        if (!one)
        {
            printf("echo canceller bound %d not taken\n", i);
        }
        taken = taken && one;
        if (one)
        {
            maudDestroyEchoCanceller(c);
        }
    }
    CHECK(taken, "an echo canceller def's bounds taken");
}

int main(void)
{
    TestVoiceDetector();
    TestGainControl();
    TestNoiseSuppressor();
    TestEchoCanceller();
    return s_failures == 0 ? 0 : 1;
}
