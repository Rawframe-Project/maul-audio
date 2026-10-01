// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The automatic gain control on synthetic speech and noise: quiet and
// loud speech brought toward the target, noise kept under its ceiling,
// the gain's pace, the limiter, layouts, chunking and allocation.

#include "test_harness.h"
#include "test_signals.h"

#include "maul-audio/voice.h"

#include <stdio.h>

static long s_live;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_live++;
    return malloc(size);
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    s_live--;
    free(memory);
}

// The gain after each 10 ms frame, from running a signal through in
// 10 ms chunks; the signal is changed in place.
typedef struct Trace
{
    float* gainDb;
    uint32_t frames;
    maudGainState last;
} Trace;

static Trace Control(Signal* signal, const maudGainControlDef* base)
{
    maudGainControlDef def = *base;
    def.sampleRate = signal->rate;
    maudGainControl* gain = nullptr;
    CHECK(maudCreateGainControl(&def, &gain) == maud_success, "create a gain control");
    Trace trace = {.gainDb = calloc(signal->frames + 1, sizeof(float))};
    uint32_t chunk = signal->rate / 100;
    for (uint32_t at = 0; at + chunk <= signal->count; at += chunk)
    {
        CHECK(maudApplyGainControl(gain, &signal->samples[at], chunk, &trace.last) == maud_success,
              "apply");
        trace.gainDb[trace.frames++] = trace.last.gainDb;
    }
    maudDestroyGainControl(gain);
    return trace;
}

// The RMS level in dBFS of the frames that sound speech (or, with
// speech false, those that do not) from `from` seconds on.
static double LevelOf(const Signal* signal, double from, bool speech)
{
    uint32_t chunk = signal->rate / 100;
    double energy = 0.0;
    uint64_t count = 0;
    for (uint32_t f = (uint32_t)(from * 100); f < signal->frames; ++f)
    {
        if (signal->speech[f] != speech)
        {
            continue;
        }
        for (uint32_t i = f * chunk; i < (f + 1) * chunk; ++i)
        {
            energy += (double)signal->samples[i] * (double)signal->samples[i];
            count++;
        }
    }
    return count > 0 ? 10.0 * log10(energy / (double)count + 1e-20) : -200.0;
}

static float Peak(const Signal* signal)
{
    float peak = 0.0f;
    for (uint32_t i = 0; i < signal->count; ++i)
    {
        float magnitude = fabsf(signal->samples[i]);
        peak = magnitude > peak ? magnitude : peak;
    }
    return peak;
}

// A signal of speech from 1 s at a level over noise, through a default
// gain control.
static Trace Run(Signal* signal, double speechDb, double noiseDb)
{
    AddNoise(signal, noiseDb, 77);
    if (speechDb > -200.0)
    {
        AddSpeech(signal, speechDb, 1.0, signal->count / (double)signal->rate);
    }
    maudGainControlDef def = maudDefaultGainControlDef();
    return Control(signal, &def);
}

// Quiet speech is raised and loud speech lowered until its measured
// level, after the gain, is the target's.
static void TestTarget(void)
{
    Signal quiet = MakeSignal(48000, 20.0);
    Trace up = Run(&quiet, -45, -80);
    CHECK(up.last.speechReliable, "the quiet speech's level is known");
    CHECK(fabsf(up.last.speechDbfs + up.last.gainDb - -25.0f) <= 1.0f, "and brought to the target");
    double out = LevelOf(&quiet, 15.0, true);
    CHECK(out > -28.0 && out < -19.0, "the output sounds near it");
    Signal loud = MakeSignal(48000, 20.0);
    Trace down = Run(&loud, -10, -80);
    CHECK(fabsf(down.last.gainDb - -10.0f) <= 0.1f, "loud speech gets the least gain");
    out = LevelOf(&loud, 15.0, true);
    CHECK(out > -23.0 && out < -17.0, "and comes out 10 dB lower");
    free(up.gainDb);
    free(down.gainDb);
    FreeSignal(&quiet);
    FreeSignal(&loud);
}

// Noise alone never raises the gain, which falls until the noise sits at
// its ceiling; speech in noise is raised only as far as the noise lets.
static void TestNoise(void)
{
    Signal noise = MakeSignal(48000, 10.0);
    Trace alone = Run(&noise, -300, -60);
    float highest = 0.0f;
    for (uint32_t f = 0; f < alone.frames; ++f)
    {
        highest = alone.gainDb[f] > highest ? alone.gainDb[f] : highest;
    }
    CHECK(highest <= 15.0f, "noise alone never raises the gain");
    CHECK(!alone.last.speechReliable, "nor counts as speech");
    CHECK(LevelOf(&noise, 5.0, false) <= -49.0, "the noise ends at its ceiling");
    Signal noisy = MakeSignal(48000, 20.0);
    Trace capped = Run(&noisy, -40, -55);
    CHECK(capped.last.gainDb <= 5.5f, "speech in noise is raised only as far as the noise allows");
    CHECK(LevelOf(&noisy, 15.0, false) <= -49.0, "and the noise stays at its ceiling");
    free(alone.gainDb);
    free(capped.gainDb);
    FreeSignal(&noise);
    FreeSignal(&noisy);
}

// The gain moves by at most 6 dB per second, but for one catch-up of 12
// steps when a run of speech has proved itself; before speech it does
// not rise.
static void TestPace(void)
{
    Signal signal = MakeSignal(48000, 20.0);
    Trace trace = Run(&signal, -50, -80);
    uint32_t catchUps = 0;
    bool paced = true;
    for (uint32_t f = 1; f < trace.frames; ++f)
    {
        float change = trace.gainDb[f] - trace.gainDb[f - 1];
        if (fabsf(change) > 0.0601f)
        {
            catchUps++;
            paced = paced && change <= 0.7201f;
        }
        paced = paced && (f >= 100 || trace.gainDb[f] <= 15.0f);
    }
    CHECK(paced, "the gain keeps its pace and waits for speech");
    CHECK(catchUps >= 1 && catchUps <= 20, "catching up only after runs of speech");
    free(trace.gainDb);
    FreeSignal(&signal);
}

// Whatever comes in, no sample leaves above -1 dBFS.
static void TestLimiter(void)
{
    Signal clicks = MakeSignal(48000, 6.0);
    AddSpeech(&clicks, -3, 0.5, 6.0);
    for (uint32_t i = 48000; i < clicks.count; i += 48000)
    {
        clicks.samples[i] = 1.0f;
    }
    maudGainControlDef def = maudDefaultGainControlDef();
    Trace limited = Control(&clicks, &def);
    CHECK(Peak(&clicks) <= 0.8912510f, "no output sample passes -1 dBFS");
    free(limited.gainDb);
    FreeSignal(&clicks);
}

// Stereo of identical channels comes out as mono does, whatever the
// chunks, and nothing allocates after creation.
static void TestLayoutAndChunks(void)
{
    Signal signal = MakeSignal(48000, 4.0);
    AddNoise(&signal, -70, 5);
    AddSpeech(&signal, -40, 0.5, 4.0);
    float* stereo = Interleave(&signal, 2);
    maudGainControlDef def = maudDefaultGainControlDef();
    maudGainControl* mono = nullptr;
    CHECK(maudCreateGainControl(&def, &mono) == maud_success, "a mono gain control");
    for (uint32_t i = 0; i < signal.count; ++i)
    {
        CHECK(maudApplyGainControl(mono, &signal.samples[i], 1, nullptr) == maud_success,
              "apply sample by sample");
    }
    maudDestroyGainControl(mono);
    def.layout = maud_layoutStereo;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudGainControl* gain = nullptr;
    CHECK(maudCreateGainControl(&def, &gain) == maud_success, "a stereo gain control");
    for (uint32_t at = 0; at < signal.count; at += 997)
    {
        uint32_t count = signal.count - at < 997 ? signal.count - at : 997;
        CHECK(maudApplyGainControl(gain, stereo + 2 * (size_t)at, count, nullptr) == maud_success,
              "apply in chunks");
    }
    CHECK(s_live == 1, "nothing allocated while applying");
    maudDestroyGainControl(gain);
    CHECK(s_live == 0, "and the block returned");
    uint32_t different = 0;
    for (uint32_t i = 0; i < signal.count; ++i)
    {
        different += stereo[2 * i] != signal.samples[i] || stereo[2 * i + 1] != signal.samples[i];
    }
    CHECK(different == 0, "stereo in chunks comes out as mono sample by sample");
    free(stereo);
    FreeSignal(&signal);
}

static void TestDefs(void)
{
    maudGainControl* gain = (maudGainControl*)&s_live;
    maudGainControlDef def = maudDefaultGainControlDef();
    def.targetDbfs = -41.0f;
    CHECK(maudCreateGainControl(&def, &gain) == maud_errorInvalid && gain == nullptr,
          "a target too low, and the out pointer cleared");
    def = maudDefaultGainControlDef();
    def.initialGainDb = 51.0f;
    CHECK(maudCreateGainControl(&def, &gain) == maud_errorInvalid, "a start past the range");
    def = maudDefaultGainControlDef();
    def.minGainDb = 1.0f;
    CHECK(maudCreateGainControl(&def, &gain) == maud_errorInvalid, "a minimum above 0 dB");
    def = maudDefaultGainControlDef();
    def.maxChangeDbPerSecond = 0.5f;
    CHECK(maudCreateGainControl(&def, &gain) == maud_errorInvalid, "a pace too slow");
    def = maudDefaultGainControlDef();
    def.maxNoiseDbfs = -10.0f;
    CHECK(maudCreateGainControl(&def, &gain) == maud_errorInvalid, "a noise ceiling too high");
    def = maudDefaultGainControlDef();
    def.aggressiveness = 4;
    CHECK(maudCreateGainControl(&def, &gain) == maud_errorInvalid, "aggressiveness past 3");
    def = maudDefaultGainControlDef();
    def.sampleRate = 384001;
    CHECK(maudCreateGainControl(&def, &gain) == maud_errorInvalid, "a rate too high");
    def = maudDefaultGainControlDef();
    def.cookie = 0;
    CHECK(maudCreateGainControl(&def, &gain) == maud_errorInvalid, "no cookie");
    CHECK(maudCreateGainControl(nullptr, &gain) == maud_errorInvalid, "no def");
    def = maudDefaultGainControlDef();
    CHECK(maudCreateGainControl(&def, &gain) == maud_success, "a good def");
    CHECK(maudApplyGainControl(gain, nullptr, 1, nullptr) == maud_errorInvalid, "frames missing");
    CHECK(maudApplyGainControl(gain, nullptr, 0, nullptr) == maud_success, "none needed for 0");
    CHECK(maudApplyGainControl(nullptr, nullptr, 0, nullptr) == maud_errorInvalid, "no gain");
    maudDestroyGainControl(gain);
    maudDestroyGainControl(nullptr);
}

int main(void)
{
    TestTarget();
    TestNoise();
    TestPace();
    TestLimiter();
    TestLayoutAndChunks();
    TestDefs();
    return s_failures == 0 ? 0 : 1;
}
