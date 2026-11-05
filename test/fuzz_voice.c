// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the voice processors (maul-audio/voice.h): the first byte
// chooses the detector, the gain control, the noise suppressor or the
// echo canceller; then come its def's fields, each either drawn within
// its range or taken as raw bits, the frame count, the signal's level,
// shape and seed, and the lengths of the cuts; for the echo canceller,
// before them, the echo's path. The same frames go
// through one processor whole and through another in those cuts, and
// each call is held to its header's promises:
//
// - a processor is made exactly when its def is within the ranges its
//   header gives, and refused as invalid otherwise;
// - its output and state do not depend on how the frames are cut: the
//   two agree bit for bit;
// - processing allocates nothing;
// - samples within 2^64 of zero keep the output and the state finite,
//   and the state within its documented ranges: a probability from 0 to
//   1 (the echo canceller's 0 until two blocks have been processed and
//   from 0.1 after, its leakage from 0.005), the gain within the def's
//   range and its speech level -100 until reliable, the frames counted;
// - an echo canceller whose capture holds none of the render (muted)
//   does not report having adapted;
// - the gain control lets no sample out above -1 dBFS.

#include "maul-audio/voice.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

enum
{
    MAX_FRAMES = 8192,
    MAX_CHANNELS = 16,
    MAX_CUTS = 64
};

// The ceiling the gain control holds, -1 dBFS, as its suite reads it.
#define CEILING 0.8912510f

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// The input read from its start, zeros past its end.
typedef struct Input
{
    const uint8_t* data;
    size_t size;
    size_t at;
} Input;

static uint8_t Byte(Input* in)
{
    return in->at < in->size ? in->data[in->at++] : 0;
}

static uint32_t Word(Input* in)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
    {
        value = value << 8 | Byte(in);
    }
    return value;
}

// A value within [low, high], or, for one byte in five, raw bits.
static float Field(Input* in, float low, float high)
{
    if (Byte(in) >= 205)
    {
        uint32_t bits = Word(in);
        float raw = 0.0f;
        memcpy(&raw, &bits, sizeof(raw));
        return raw;
    }
    return low + (high - low) * (float)(Word(in) % 1000001u) / 1000000.0f;
}

static uint32_t Rate(Input* in)
{
    static const uint32_t common[8] = {8000, 11025, 16000, 22050, 44100, 48000, 96000, 384000};
    uint8_t pick = Byte(in);
    if (pick < 128)
    {
        return common[pick % 8];
    }
    return pick < 230 ? 8000 + Word(in) % 376001u : Word(in);
}

static bool Within(float value, float low, float high)
{
    return value >= low && value <= high;
}

static bool RateValid(uint32_t rate)
{
    return rate >= 8000 && rate <= 384000;
}

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

static const maudAllocator s_counted = {Counted, Release, nullptr};

static float s_whole[MAX_FRAMES * MAX_CHANNELS];
static float s_cut[MAX_FRAMES * MAX_CHANNELS];
static float s_render[MAX_FRAMES];

// The frames and the cuts.
typedef struct Signal
{
    uint32_t frames;
    uint32_t channels;
    uint8_t cuts[MAX_CUTS];
    uint32_t cutCount;
} Signal;

// Bursts of noise or of a tone at a level from 2^-24 to 2^64, the same
// samples in both buffers; the render, for the echo canceller, a
// quieter noise.
static void Fill(Input* in, Signal* signal, uint32_t channels)
{
    signal->channels = channels;
    signal->frames = Word(in) % (MAX_FRAMES + 1u);
    float level = ldexpf(1.0f, (int)(Byte(in) % 89u) - 24);
    uint8_t shape = Byte(in);
    uint32_t seed = Word(in) | 1u;
    for (uint32_t i = 0; i < signal->frames * channels; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        float noise = (float)(int32_t)seed / 2147483648.0f;
        float tone = sinf((float)i * (0.01f + (float)(shape & 15u) * 0.02f));
        float burst = ((i / (64u + (shape >> 4) * 300u)) & 1u) != 0 ? 1.0f : 0.05f;
        s_whole[i] = level * burst * ((shape & 0x80u) != 0 ? tone : noise);
        s_cut[i] = s_whole[i];
    }
    for (uint32_t i = 0; i < signal->frames; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        s_render[i] = 0.5f * level * (float)(int32_t)seed / 2147483648.0f;
    }
    signal->cutCount = 0;
    while (signal->cutCount < MAX_CUTS && in->at < in->size)
    {
        signal->cuts[signal->cutCount++] = Byte(in);
    }
    if (signal->cutCount == 0)
    {
        signal->cuts[signal->cutCount++] = 37;
    }
}

// The length of cut c, from 1 to 1786 frames and within those left.
static uint32_t CutLength(const Signal* signal, uint32_t c, uint32_t left)
{
    uint32_t n = 1u + signal->cuts[c % signal->cutCount] * 7u;
    return n < left ? n : left;
}

static bool Finite(const float* samples, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (!isfinite(samples[i]))
        {
            return false;
        }
    }
    return true;
}

static void Detector(Input* in)
{
    maudVoiceDetectorDef def = maudDefaultVoiceDetectorDef();
    def.sampleRate = Rate(in);
    def.layout = (maudChannelLayout)Byte(in);
    def.aggressiveness = Byte(in) % 5u;
    def.hangover = Field(in, 0.0f, 10.0f);
    def.allocator = s_counted;
    uint32_t channels = maudGetLayoutChannelCount(def.layout);
    bool valid = RateValid(def.sampleRate) && channels != 0 && def.aggressiveness <= 3 &&
                 Within(def.hangover, 0.0f, 10.0f);
    maudVoiceDetector* whole = nullptr;
    maudVoiceDetector* cut = nullptr;
    maudResult made = maudCreateVoiceDetector(&def, &whole);
    Expect(valid ? made == maud_success : made == maud_errorInvalid && whole == nullptr);
    if (!valid || channels > MAX_CHANNELS)
    {
        maudDestroyVoiceDetector(whole);
        return;
    }
    Expect(maudCreateVoiceDetector(&def, &cut) == maud_success);
    Signal signal;
    Fill(in, &signal, channels);
    int before = s_allocations;
    maudVoiceState one;
    maudVoiceState two;
    Expect(maudVoiceDetector_Detect(whole, s_whole, signal.frames, &one) == maud_success);
    Expect(maudVoiceDetector_Detect(cut, s_cut, 0, &two) == maud_success);
    for (uint32_t at = 0, c = 0; at < signal.frames; ++c)
    {
        uint32_t n = CutLength(&signal, c, signal.frames - at);
        Expect(maudVoiceDetector_Detect(cut, s_cut + (size_t)at * channels, n, &two) ==
               maud_success);
        at += n;
    }
    Expect(s_allocations == before);
    Expect(one.active == two.active && memcmp(&one.probability, &two.probability, 4) == 0 &&
           memcmp(&one.levelDbfs, &two.levelDbfs, 4) == 0 &&
           memcmp(&one.noiseDbfs, &two.noiseDbfs, 4) == 0 && one.frameCount == two.frameCount);
    Expect(Within(one.probability, 0.0f, 1.0f) && isfinite(one.levelDbfs) &&
           isfinite(one.noiseDbfs));
    Expect(one.frameCount == signal.frames / (def.sampleRate / 100));
    maudDestroyVoiceDetector(whole);
    maudDestroyVoiceDetector(cut);
}

static void Gain(Input* in)
{
    maudGainControlDef def = maudDefaultGainControlDef();
    def.sampleRate = Rate(in);
    def.layout = (maudChannelLayout)Byte(in);
    def.targetDbfs = Field(in, -40.0f, -6.0f);
    def.minGainDb = Field(in, -30.0f, 0.0f);
    def.maxGainDb = Field(in, 0.0f, 60.0f);
    def.initialGainDb = Field(in, -30.0f, 60.0f);
    def.maxChangeDbPerSecond = Field(in, 1.0f, 30.0f);
    def.maxNoiseDbfs = Field(in, -80.0f, -20.0f);
    def.aggressiveness = Byte(in) % 5u;
    def.allocator = s_counted;
    uint32_t channels = maudGetLayoutChannelCount(def.layout);
    bool valid = RateValid(def.sampleRate) && channels != 0 &&
                 Within(def.targetDbfs, -40.0f, -6.0f) && Within(def.minGainDb, -30.0f, 0.0f) &&
                 Within(def.maxGainDb, 0.0f, 60.0f) &&
                 Within(def.initialGainDb, def.minGainDb, def.maxGainDb) &&
                 Within(def.maxChangeDbPerSecond, 1.0f, 30.0f) &&
                 Within(def.maxNoiseDbfs, -80.0f, -20.0f) && def.aggressiveness <= 3;
    maudGainControl* whole = nullptr;
    maudGainControl* cut = nullptr;
    maudResult made = maudCreateGainControl(&def, &whole);
    Expect(valid ? made == maud_success : made == maud_errorInvalid && whole == nullptr);
    if (!valid || channels > MAX_CHANNELS)
    {
        maudDestroyGainControl(whole);
        return;
    }
    Expect(maudCreateGainControl(&def, &cut) == maud_success);
    Signal signal;
    Fill(in, &signal, channels);
    int before = s_allocations;
    maudGainState one;
    maudGainState two;
    Expect(maudGainControl_Apply(whole, s_whole, signal.frames, &one) == maud_success);
    Expect(maudGainControl_Apply(cut, s_cut, 0, &two) == maud_success);
    for (uint32_t at = 0, c = 0; at < signal.frames; ++c)
    {
        uint32_t n = CutLength(&signal, c, signal.frames - at);
        Expect(maudGainControl_Apply(cut, s_cut + (size_t)at * channels, n, &two) == maud_success);
        at += n;
    }
    Expect(s_allocations == before);
    size_t count = (size_t)signal.frames * channels;
    Expect(memcmp(s_whole, s_cut, count * sizeof(float)) == 0 &&
           memcmp(&one, &two, sizeof(one)) == 0);
    Expect(Finite(s_whole, count));
    for (size_t i = 0; i < count; ++i)
    {
        Expect(fabsf(s_whole[i]) <= CEILING);
    }
    Expect(Within(one.gainDb, def.minGainDb, def.maxGainDb) && isfinite(one.noiseDbfs));
    Expect(one.speechReliable ? isfinite(one.speechDbfs) : one.speechDbfs == -100.0f);
    maudDestroyGainControl(whole);
    maudDestroyGainControl(cut);
}

static void Noise(Input* in)
{
    maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();
    def.sampleRate = Rate(in);
    // Most rates a multiple of 100, as it asks.
    def.sampleRate -= Byte(in) < 200 ? def.sampleRate % 100 : 0;
    def.layout = (maudChannelLayout)Byte(in);
    def.floorDb = Field(in, -40.0f, -6.0f);
    def.highPassHz = Byte(in) < 64 ? 0.0f : Field(in, 20.0f, 400.0f);
    def.allocator = s_counted;
    uint32_t channels = maudGetLayoutChannelCount(def.layout);
    bool valid = RateValid(def.sampleRate) && def.sampleRate % 100 == 0 && channels != 0 &&
                 Within(def.floorDb, -40.0f, -6.0f) &&
                 (def.highPassHz == 0.0f || Within(def.highPassHz, 20.0f, 400.0f));
    maudNoiseSuppressor* whole = nullptr;
    maudNoiseSuppressor* cut = nullptr;
    maudResult made = maudCreateNoiseSuppressor(&def, &whole);
    Expect(valid ? made == maud_success : made == maud_errorInvalid && whole == nullptr);
    if (!valid || channels > MAX_CHANNELS)
    {
        maudDestroyNoiseSuppressor(whole);
        return;
    }
    Expect(maudCreateNoiseSuppressor(&def, &cut) == maud_success);
    Signal signal;
    Fill(in, &signal, channels);
    int before = s_allocations;
    maudNoiseState one;
    maudNoiseState two;
    Expect(maudNoiseSuppressor_Suppress(whole, s_whole, signal.frames, &one) == maud_success);
    Expect(maudNoiseSuppressor_Suppress(cut, s_cut, 0, &two) == maud_success);
    for (uint32_t at = 0, c = 0; at < signal.frames; ++c)
    {
        uint32_t n = CutLength(&signal, c, signal.frames - at);
        Expect(maudNoiseSuppressor_Suppress(cut, s_cut + (size_t)at * channels, n, &two) ==
               maud_success);
        at += n;
    }
    Expect(s_allocations == before);
    size_t count = (size_t)signal.frames * channels;
    Expect(memcmp(s_whole, s_cut, count * sizeof(float)) == 0 &&
           memcmp(&one, &two, sizeof(one)) == 0);
    Expect(Finite(s_whole, count));
    Expect(Within(one.speechProbability, 0.0f, 1.0f) && isfinite(one.noiseDbfs));
    Expect(one.frameCount == signal.frames / (def.sampleRate / 100));
    maudDestroyNoiseSuppressor(whole);
    maudDestroyNoiseSuppressor(cut);
}

static void Echo(Input* in)
{
    maudEchoCancellerDef def = maudDefaultEchoCancellerDef();
    def.sampleRate = Rate(in);
    def.tailTime = Field(in, 0.05f, 1.0f);
    def.floorDb = Field(in, -40.0f, -6.0f);
    def.allocator = s_counted;
    bool valid = RateValid(def.sampleRate) && Within(def.tailTime, 0.05f, 1.0f) &&
                 Within(def.floorDb, -40.0f, -6.0f);
    maudEchoCanceller* whole = nullptr;
    maudEchoCanceller* cut = nullptr;
    maudResult made = maudCreateEchoCanceller(&def, &whole);
    Expect(valid ? made == maud_success : made == maud_errorInvalid && whole == nullptr);
    if (!valid)
    {
        maudDestroyEchoCanceller(whole);
        return;
    }
    Expect(maudCreateEchoCanceller(&def, &cut) == maud_success);
    // The echo's path: its gain over the render, from 2^-8 to 2^16 (above
    // 1 the render played that much quieter, below it the echo, so that
    // both stay within the samples' range); with bit 5 no near end, only
    // the echo; with bit 6 the capture muted, zeros; with bit 7 the render
    // stopping for stretches, as a far end that pauses.
    uint8_t path = Byte(in);
    float gain = ldexpf(1.0f, (int)((path & 31u) % 25u) - 8);
    Signal signal;
    Fill(in, &signal, 1);
    // The capture holds the render's echo, as a loudspeaker's, over the
    // near end: 0.8 of the near end and 0.15 of the level at most, within
    // the level and so within 2^64; the render is at most 0.5 of it.
    for (uint32_t i = 0; i < signal.frames; ++i)
    {
        bool stopped = (path & 0x80u) != 0 && (i / 1000u) % 3u == 0;
        s_render[i] = stopped ? 0.0f : s_render[i] / fmaxf(gain, 1.0f);
        s_whole[i] = ((path & 0x20u) != 0 ? 0.0f : 0.8f * s_whole[i]) + 0.3f * gain * s_render[i];
        s_whole[i] = (path & 0x40u) != 0 ? 0.0f : s_whole[i];
        s_cut[i] = s_whole[i];
    }
    int before = s_allocations;
    maudEchoState one;
    maudEchoState two;
    Expect(maudEchoCanceller_Cancel(whole, s_whole, s_render, signal.frames, &one) == maud_success);
    Expect(maudEchoCanceller_Cancel(cut, s_cut, s_render, 0, &two) == maud_success);
    for (uint32_t at = 0, c = 0; at < signal.frames; ++c)
    {
        uint32_t n = CutLength(&signal, c, signal.frames - at);
        Expect(maudEchoCanceller_Cancel(cut, s_cut + at, s_render + at, n, &two) == maud_success);
        at += n;
    }
    Expect(s_allocations == before);
    Expect(memcmp(s_whole, s_cut, (size_t)signal.frames * sizeof(float)) == 0 &&
           memcmp(&one, &two, sizeof(one)) == 0);
    Expect(Finite(s_whole, signal.frames));
    // The probability of speech is 0 until two blocks have been
    // processed: blocks of the smallest power of two of frames lasting
    // 8 ms or more.
    uint32_t block = 1;
    while ((double)block < 0.008 * def.sampleRate)
    {
        block *= 2;
    }
    Expect(signal.frames < 2 * block ? one.speechProbability == 0.0f
                                     : Within(one.speechProbability, 0.1f, 1.0f));
    Expect(Within(one.leakage, 0.005f, 1.0f) && one.frameCount == signal.frames);
    // voice.h: a capture that holds none of the render leaves adapted false.
    Expect((path & 0x40u) == 0 || !one.adapted);
    maudDestroyEchoCanceller(whole);
    maudDestroyEchoCanceller(cut);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Input in = {data, size, 0};
    switch (Byte(&in) % 4u)
    {
    case 0:
        Detector(&in);
        break;
    case 1:
        Gain(&in);
        break;
    case 2:
        Noise(&in);
        break;
    default:
        Echo(&in);
        break;
    }
    return 0;
}
