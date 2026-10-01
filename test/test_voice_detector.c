// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The voice activity detector on synthetic signals: silence, white
// noise, tones, and speech in quiet and in noise, at several rates and
// layouts and in chunks of any size.

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

// The decisions, one per 10 ms frame, from feeding the signal one
// sample at a time.
typedef struct Run
{
    bool* active;
    float* probability;
    uint32_t frames;
} Run;

static Run Detect(const Signal* signal, uint8_t aggressiveness, uint32_t chunk)
{
    maudVoiceDetectorDef def = maudDefaultVoiceDetectorDef();
    def.sampleRate = signal->rate;
    def.aggressiveness = aggressiveness;
    maudVoiceDetector* detector = nullptr;
    CHECK(maudCreateVoiceDetector(&def, &detector) == maud_success, "create a detector");
    Run run = {.active = calloc(signal->frames + 1, sizeof(bool)),
               .probability = calloc(signal->frames + 1, sizeof(float))};
    for (uint32_t at = 0; at < signal->count; at += chunk)
    {
        uint32_t count = signal->count - at < chunk ? signal->count - at : chunk;
        maudVoiceState state = {0};
        CHECK(maudDetectVoice(detector, &signal->samples[at], count, &state) == maud_success,
              "detect");
        if (state.frames > run.frames)
        {
            run.active[state.frames - 1] = state.active;
            run.probability[state.frames - 1] = state.probability;
            run.frames = (uint32_t)state.frames;
        }
    }
    maudDestroyVoiceDetector(detector);
    return run;
}

static void FreeRun(Run* run)
{
    free(run->active);
    free(run->probability);
}

// Fractions over the frames from `from` seconds: of speech frames
// detected, and of frames more than `grace` frames past any speech
// detected anyway.
typedef struct Score
{
    double hit;
    double falseAlarm;
} Score;

static Score Measure(const Signal* signal, const Run* run, double from, uint32_t grace)
{
    uint32_t speech = 0;
    uint32_t hits = 0;
    uint32_t quiet = 0;
    uint32_t alarms = 0;
    uint32_t since = grace + 1;
    for (uint32_t f = (uint32_t)(from * 100); f < run->frames; ++f)
    {
        since = signal->speech[f] ? 0 : since + 1;
        if (signal->speech[f])
        {
            speech++;
            hits += run->active[f] ? 1u : 0u;
        }
        else if (since > grace)
        {
            quiet++;
            alarms += run->active[f] ? 1u : 0u;
        }
    }
    return (Score){speech > 0 ? (double)hits / speech : 1.0,
                   quiet > 0 ? (double)alarms / quiet : 0.0};
}

// Speech from 2 s to the end over noise at a level (or none), scored
// from 2 s on with the hangover's 250 ms as grace.
static Score SpeechScore(uint32_t rate, double speechDb, double noiseDb, uint8_t aggressiveness)
{
    Signal signal = MakeSignal(rate, 10.0);
    if (noiseDb > -200.0)
    {
        AddNoise(&signal, noiseDb, 12345);
    }
    if (speechDb > -200.0)
    {
        AddSpeech(&signal, speechDb, 2.0, 10.0);
    }
    Run run = Detect(&signal, aggressiveness, rate / 100);
    Score score = Measure(&signal, &run, 2.0, 25);
    FreeRun(&run);
    FreeSignal(&signal);
    return score;
}

static void TestQuietAndNoise(void)
{
    Score silence = SpeechScore(48000, -300, -300, 1);
    CHECK(silence.falseAlarm == 0.0, "silence is never voice");
    Score noise = SpeechScore(48000, -300, -40, 1);
    CHECK(noise.falseAlarm == 0.0, "nor is steady noise");
}

static void TestSpeech(void)
{
    static const double noises[3] = {-70.0, -45.0, -35.0};
    for (int n = 0; n < 3; ++n)
    {
        Score score = SpeechScore(48000, -30, noises[n], 1);
        if (score.hit < 0.95 || score.falseAlarm > 0.02)
        {
            printf("speech -30 in %.0f: hit %.3f false %.3f\n", noises[n], score.hit,
                   score.falseAlarm);
        }
        CHECK(score.hit >= 0.95, "speech is found");
        CHECK(score.falseAlarm <= 0.02, "and the pauses are not");
    }
    static const uint32_t rates[4] = {8000, 16000, 44100, 96000};
    for (int r = 0; r < 4; ++r)
    {
        Score score = SpeechScore(rates[r], -30, -60, 1);
        CHECK(score.hit >= 0.95 && score.falseAlarm <= 0.02, "at every rate");
    }
    Score lenient = SpeechScore(48000, -30, -30, 0);
    Score strict = SpeechScore(48000, -30, -30, 3);
    CHECK(lenient.hit > strict.hit, "aggressiveness asks for more evidence");
}

// A detector run over signal kinds, scored from 2 s on.
static Score KindScore(Voicing voicing, double speechDb, double noiseDb, uint8_t aggressiveness)
{
    Signal signal = MakeSignal(48000, 10.0);
    AddNoise(&signal, noiseDb, 5);
    AddVoicing(&signal, speechDb, 2.0, 10.0, voicing);
    Run run = Detect(&signal, aggressiveness, 480);
    Score score = Measure(&signal, &run, 2.0, 25);
    FreeRun(&run);
    FreeSignal(&signal);
    return score;
}

// A nasal sounds only below 450 Hz, where the bands' mean barely moves:
// one band's margin finds it. A whisper is noise a little above the
// noise in every band: only the mean finds it, and the higher
// aggressiveness asks too much.
static void TestVoicings(void)
{
    Score nasal = KindScore(voicingNasal, -45, -40, 1);
    CHECK(nasal.hit >= 0.85, "a faint nasal is found by its band");
    Score whisper = KindScore(voicingWhisper, -38, -40, 1);
    CHECK(whisper.hit >= 0.85, "a whisper 2 dB over the noise is found");
    Score strict = KindScore(voicingWhisper, -38, -40, 3);
    CHECK(strict.hit <= 0.1, "but not at aggressiveness 3");
}

// A 5 ms click wakes the lenient detector, whose onset is one frame, and
// not the strict one, whose onset is two.
static void TestClicks(void)
{
    Score lenient = {0};
    Score strict = {0};
    for (uint8_t a = 1; a <= 3; a += 2)
    {
        Signal signal = MakeSignal(48000, 10.0);
        AddNoise(&signal, -60, 5);
        AddClicks(&signal, -40);
        Run run = Detect(&signal, a, 480);
        *(a == 1 ? &lenient : &strict) = Measure(&signal, &run, 0.0, 25);
        FreeRun(&run);
        FreeSignal(&signal);
    }
    CHECK(lenient.falseAlarm > 0.1, "a click passes a one-frame onset");
    CHECK(strict.falseAlarm == 0.0, "not a two-frame one");
}

// A 30 Hz rumble swelling to -30 dBFS is not voice: the high-pass takes
// it out, and the lowest band cannot decide alone.
static void TestRumble(void)
{
    Signal signal = MakeSignal(48000, 10.0);
    AddNoise(&signal, -70, 5);
    AddRumble(&signal, 30.0, -30);
    Run run = Detect(&signal, 1, 480);
    CHECK(Measure(&signal, &run, 2.0, 25).falseAlarm == 0.0, "rumble is not voice");
    FreeRun(&run);
    FreeSignal(&signal);
}

// Each word turns the detector on within 30 ms, and the last one leaves
// it on for the hangover and no more than 30 ms past it.
static void TestOnsetAndHangover(void)
{
    Signal signal = MakeSignal(48000, 6.0);
    AddNoise(&signal, -60, 9);
    AddSpeech(&signal, -30, 1.0, 2.6);
    Run run = Detect(&signal, 1, 480);
    uint32_t worst = 0;
    for (uint32_t f = 1; f < run.frames; ++f)
    {
        if (signal.speech[f] && !signal.speech[f - 1] && (f < 2 || !signal.speech[f - 2]))
        {
            uint32_t delay = 0;
            while (f + delay < run.frames && !run.active[f + delay])
            {
                delay++;
            }
            worst = delay > worst ? delay : worst;
        }
    }
    CHECK(worst <= 3, "words are caught within 30 ms");
    uint32_t last = 0;
    for (uint32_t f = 0; f < run.frames; ++f)
    {
        last = signal.speech[f] ? f : last;
    }
    uint32_t on = 0;
    while (last + 1 + on < run.frames && run.active[last + 1 + on])
    {
        on++;
    }
    CHECK(on >= 19 && on <= 23, "the hangover holds 200 ms after the last word");
    FreeRun(&run);
    FreeSignal(&signal);
}

// Noise jumping from -60 to -20 dBFS reads as voice until the floor
// catches up, within 1.5 s and the hangover.
static void TestNoiseStep(void)
{
    Signal signal = MakeSignal(48000, 8.0);
    AddNoise(&signal, -60, 1);
    for (uint32_t i = 4 * 48000; i < signal.count; ++i)
    {
        signal.samples[i] *= 100.0f;
    }
    Run run = Detect(&signal, 1, 480);
    uint32_t lastActive = 0;
    for (uint32_t f = 0; f < run.frames; ++f)
    {
        lastActive = run.active[f] ? f : lastActive;
    }
    CHECK(lastActive < 400 + 175, "a louder floor is learned within 1.75 s");
    FreeRun(&run);
    FreeSignal(&signal);
}

static void TestDefs(void)
{
    maudVoiceDetector* detector = (maudVoiceDetector*)&s_live;
    maudVoiceDetectorDef def = maudDefaultVoiceDetectorDef();
    def.sampleRate = 7999;
    CHECK(maudCreateVoiceDetector(&def, &detector) == maud_errorInvalid && detector == nullptr,
          "a rate too low, and the out pointer cleared");
    def = maudDefaultVoiceDetectorDef();
    def.aggressiveness = 4;
    CHECK(maudCreateVoiceDetector(&def, &detector) == maud_errorInvalid, "aggressiveness past 3");
    def = maudDefaultVoiceDetectorDef();
    def.hangoverMilliseconds = 10001;
    CHECK(maudCreateVoiceDetector(&def, &detector) == maud_errorInvalid, "a hangover too long");
    def = maudDefaultVoiceDetectorDef();
    def.layout = maud_layoutNone;
    CHECK(maudCreateVoiceDetector(&def, &detector) == maud_errorInvalid, "no layout");
    def = maudDefaultVoiceDetectorDef();
    def.cookie = 0;
    CHECK(maudCreateVoiceDetector(&def, &detector) == maud_errorInvalid, "no cookie");
    CHECK(maudCreateVoiceDetector(nullptr, &detector) == maud_errorInvalid, "no def");
    def = maudDefaultVoiceDetectorDef();
    CHECK(maudCreateVoiceDetector(&def, &detector) == maud_success, "a good def");
    CHECK(maudDetectVoice(detector, nullptr, 1, nullptr) == maud_errorInvalid, "frames missing");
    CHECK(maudDetectVoice(detector, nullptr, 0, nullptr) == maud_success, "none needed for 0");
    CHECK(maudDetectVoice(nullptr, nullptr, 0, nullptr) == maud_errorInvalid, "no detector");
    maudDestroyVoiceDetector(detector);
    maudDestroyVoiceDetector(nullptr);
}

// A 1 kHz tone from 3 s on: it may wake the detector at its start, but
// the floor rises to it within 1.5 s.
static void TestTone(void)
{
    Signal signal = MakeSignal(48000, 8.0);
    AddNoise(&signal, -60, 7);
    AddTone(&signal, 1000.0, -30, 3.0);
    Run run = Detect(&signal, 1, 480);
    uint32_t late = 0;
    for (uint32_t f = 500; f < run.frames; ++f)
    {
        late += run.active[f] ? 1u : 0u;
    }
    CHECK(late == 0, "a steady tone is not voice once learned");
    FreeRun(&run);
    FreeSignal(&signal);
}

// The same speech, in stereo and fed in chunks of 997 frames, gives the
// same decisions as mono fed one frame at a time; and nothing allocates
// after creation.
static void TestLayoutAndChunks(void)
{
    Signal signal = MakeSignal(48000, 6.0);
    AddNoise(&signal, -55, 3);
    AddSpeech(&signal, -30, 1.0, 6.0);
    Run mono = Detect(&signal, 1, 1);
    float* stereo = Interleave(&signal, 2);
    maudVoiceDetectorDef def = maudDefaultVoiceDetectorDef();
    def.layout = maud_layoutStereo;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudVoiceDetector* detector = nullptr;
    CHECK(maudCreateVoiceDetector(&def, &detector) == maud_success, "a stereo detector");
    CHECK(s_live == 1, "one block");
    uint32_t mismatches = 0;
    for (uint32_t at = 0; at < signal.count; at += 997)
    {
        uint32_t count = signal.count - at < 997 ? signal.count - at : 997;
        maudVoiceState state = {0};
        CHECK(maudDetectVoice(detector, stereo + 2 * (size_t)at, count, &state) == maud_success,
              "detect stereo");
        if (state.frames > 0 && (state.active != mono.active[state.frames - 1] ||
                                 state.probability != mono.probability[state.frames - 1]))
        {
            mismatches++;
        }
    }
    CHECK(mismatches == 0, "stereo in chunks decides as mono sample by sample");
    CHECK(s_live == 1, "nothing allocated while detecting");
    maudDestroyVoiceDetector(detector);
    CHECK(s_live == 0, "and the block returned");
    free(stereo);
    FreeRun(&mono);
    FreeSignal(&signal);
}

int main(void)
{
    TestQuietAndNoise();
    TestSpeech();
    TestVoicings();
    TestClicks();
    TestRumble();
    TestOnsetAndHangover();
    TestNoiseStep();
    TestTone();
    TestLayoutAndChunks();
    TestDefs();
    return s_failures == 0 ? 0 : 1;
}
