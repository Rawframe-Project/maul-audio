// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the voice processors (maul-audio/voice.h): the first byte
// chooses the detector, the gain control, the noise suppressor or the
// echo canceller; then come its def's fields, each either drawn within
// its range or taken as raw bits, the frame count, the signal's level,
// shape and seed, and the lengths of the cuts; for the echo canceller,
// before them, the echo's path and a change of it part way. The same
// frames go through one processor whole and through another in those
// cuts, and each call is held to its header's promises:
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
//
// A broken promise prints one line, "test/fuzz_voice.c:LINE: expectation
// failed: " with the promise and the values that broke it, never the
// input's bytes: the line a finding's report keeps (tools/fuzz_target.sh).

#include "maul-audio/voice.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
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

// Prints the promise a line broke and the values that broke it, flushed
// before the abort, and aborts.
[[noreturn]] [[gnu::format(printf, 2, 3)]] static void Broken(int line, const char* format, ...)
{
    fprintf(stderr, "test/fuzz_voice.c:%d: expectation failed: ", line);
    va_list values;
    va_start(values, format);
    vfprintf(stderr, format, values);
    va_end(values);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
}

#define EXPECT(condition, ...) ((condition) ? (void)0 : Broken(__LINE__, __VA_ARGS__))

// A call that must succeed, its result printed when it does not.
#define EXPECT_SUCCESS(call, promise)                                                              \
    do                                                                                             \
    {                                                                                              \
        maudResult result_ = (call);                                                               \
        EXPECT(result_ == maud_success, promise ", result %d", (int)result_);                      \
    } while (0)

// The first sample at which two outputs differ, or count when none does.
static size_t FirstDifference(const float* a, const float* b, size_t count)
{
    size_t at = 0;
    while (at < count && memcmp(&a[at], &b[at], sizeof(float)) == 0)
    {
        ++at;
    }
    return at;
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
    EXPECT(valid ? made == maud_success : made == maud_errorInvalid && whole == nullptr,
           "a processor is made exactly when its def is valid, result %d, valid %d", (int)made,
           valid);
    if (!valid || channels > MAX_CHANNELS)
    {
        maudDestroyVoiceDetector(whole);
        return;
    }
    EXPECT_SUCCESS(maudCreateVoiceDetector(&def, &cut), "the second processor is made");
    Signal signal;
    Fill(in, &signal, channels);
    int before = s_allocations;
    maudVoiceState one;
    maudVoiceState two;
    EXPECT_SUCCESS(maudVoiceDetector_Detect(whole, s_whole, signal.frames, &one),
                   "the whole signal is detected");
    EXPECT_SUCCESS(maudVoiceDetector_Detect(cut, s_cut, 0, &two), "an empty call is detected");
    for (uint32_t at = 0, c = 0; at < signal.frames; ++c)
    {
        uint32_t n = CutLength(&signal, c, signal.frames - at);
        EXPECT_SUCCESS(maudVoiceDetector_Detect(cut, s_cut + (size_t)at * channels, n, &two),
                       "a cut of the signal is detected");
        at += n;
    }
    EXPECT(s_allocations == before, "processing allocates nothing, %d allocations",
           s_allocations - before);
    EXPECT(one.active == two.active && memcmp(&one.probability, &two.probability, 4) == 0 &&
               memcmp(&one.levelDbfs, &two.levelDbfs, 4) == 0 &&
               memcmp(&one.noiseDbfs, &two.noiseDbfs, 4) == 0 && one.frameCount == two.frameCount,
           "detection does not depend on how the signal is cut, active %d and %d, probability %a "
           "and %a, level %a and %a, noise %a and %a, frames %llu and %llu",
           one.active, two.active, (double)one.probability, (double)two.probability,
           (double)one.levelDbfs, (double)two.levelDbfs, (double)one.noiseDbfs,
           (double)two.noiseDbfs, (unsigned long long)one.frameCount,
           (unsigned long long)two.frameCount);
    EXPECT(Within(one.probability, 0.0f, 1.0f) && isfinite(one.levelDbfs) &&
               isfinite(one.noiseDbfs),
           "a probability within [0, 1] and finite levels, %a, level %a, noise %a",
           (double)one.probability, (double)one.levelDbfs, (double)one.noiseDbfs);
    EXPECT(one.frameCount == signal.frames / (def.sampleRate / 100),
           "every whole 10 ms frame is counted, %llu of %u frames at %u Hz",
           (unsigned long long)one.frameCount, signal.frames, def.sampleRate);
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
    EXPECT(valid ? made == maud_success : made == maud_errorInvalid && whole == nullptr,
           "a processor is made exactly when its def is valid, result %d, valid %d", (int)made,
           valid);
    if (!valid || channels > MAX_CHANNELS)
    {
        maudDestroyGainControl(whole);
        return;
    }
    EXPECT_SUCCESS(maudCreateGainControl(&def, &cut), "the second processor is made");
    Signal signal;
    Fill(in, &signal, channels);
    int before = s_allocations;
    maudGainState one;
    maudGainState two;
    EXPECT_SUCCESS(maudGainControl_Apply(whole, s_whole, signal.frames, &one),
                   "the whole signal is applied");
    EXPECT_SUCCESS(maudGainControl_Apply(cut, s_cut, 0, &two), "an empty call is applied");
    for (uint32_t at = 0, c = 0; at < signal.frames; ++c)
    {
        uint32_t n = CutLength(&signal, c, signal.frames - at);
        EXPECT_SUCCESS(maudGainControl_Apply(cut, s_cut + (size_t)at * channels, n, &two),
                       "a cut of the signal is applied");
        at += n;
    }
    EXPECT(s_allocations == before, "processing allocates nothing, %d allocations",
           s_allocations - before);
    size_t count = (size_t)signal.frames * channels;
    size_t differs = FirstDifference(s_whole, s_cut, count);
    EXPECT(differs == count && memcmp(&one, &two, sizeof(one)) == 0,
           "the output and state do not depend on how the signal is cut, first difference at "
           "sample %zu of %zu, states %s",
           differs, count, memcmp(&one, &two, sizeof(one)) == 0 ? "equal" : "differ");
    EXPECT(Finite(s_whole, count), "the output is finite, %zu samples", count);
    for (size_t i = 0; i < count; ++i)
    {
        EXPECT(fabsf(s_whole[i]) <= CEILING, "the output stays under the ceiling, sample %zu, %a",
               i, (double)s_whole[i]);
    }
    EXPECT(Within(one.gainDb, def.minGainDb, def.maxGainDb) && isfinite(one.noiseDbfs),
           "the gain within its range and a finite noise floor, %a in [%a, %a], noise %a",
           (double)one.gainDb, (double)def.minGainDb, (double)def.maxGainDb, (double)one.noiseDbfs);
    EXPECT(one.speechReliable ? isfinite(one.speechDbfs) : one.speechDbfs == -100.0f,
           "a speech level finite when reliable, -100 otherwise, reliable %d, %a",
           one.speechReliable, (double)one.speechDbfs);
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
    EXPECT(valid ? made == maud_success : made == maud_errorInvalid && whole == nullptr,
           "a processor is made exactly when its def is valid, result %d, valid %d", (int)made,
           valid);
    if (!valid || channels > MAX_CHANNELS)
    {
        maudDestroyNoiseSuppressor(whole);
        return;
    }
    EXPECT_SUCCESS(maudCreateNoiseSuppressor(&def, &cut), "the second processor is made");
    Signal signal;
    Fill(in, &signal, channels);
    int before = s_allocations;
    maudNoiseState one;
    maudNoiseState two;
    EXPECT_SUCCESS(maudNoiseSuppressor_Suppress(whole, s_whole, signal.frames, &one),
                   "the whole signal is suppressed");
    EXPECT_SUCCESS(maudNoiseSuppressor_Suppress(cut, s_cut, 0, &two),
                   "an empty call is suppressed");
    for (uint32_t at = 0, c = 0; at < signal.frames; ++c)
    {
        uint32_t n = CutLength(&signal, c, signal.frames - at);
        EXPECT_SUCCESS(maudNoiseSuppressor_Suppress(cut, s_cut + (size_t)at * channels, n, &two),
                       "a cut of the signal is suppressed");
        at += n;
    }
    EXPECT(s_allocations == before, "processing allocates nothing, %d allocations",
           s_allocations - before);
    size_t count = (size_t)signal.frames * channels;
    size_t differs = FirstDifference(s_whole, s_cut, count);
    EXPECT(differs == count && memcmp(&one, &two, sizeof(one)) == 0,
           "the output and state do not depend on how the signal is cut, first difference at "
           "sample %zu of %zu, states %s",
           differs, count, memcmp(&one, &two, sizeof(one)) == 0 ? "equal" : "differ");
    EXPECT(Finite(s_whole, count), "the output is finite, %zu samples", count);
    EXPECT(Within(one.speechProbability, 0.0f, 1.0f) && isfinite(one.noiseDbfs),
           "a speech probability within [0, 1] and a finite noise floor, %a, noise %a",
           (double)one.speechProbability, (double)one.noiseDbfs);
    EXPECT(one.frameCount == signal.frames / (def.sampleRate / 100),
           "every whole 10 ms frame is counted, %llu of %u frames at %u Hz",
           (unsigned long long)one.frameCount, signal.frames, def.sampleRate);
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
    EXPECT(valid ? made == maud_success : made == maud_errorInvalid && whole == nullptr,
           "a processor is made exactly when its def is valid, result %d, valid %d", (int)made,
           valid);
    if (!valid)
    {
        maudDestroyEchoCanceller(whole);
        return;
    }
    EXPECT_SUCCESS(maudCreateEchoCanceller(&def, &cut), "the second processor is made");
    // The echo's path: its gain over the render, from 2^-8 to 2^16 (above
    // 1 the render played that much quieter, below it the echo, so that
    // both stay within the samples' range); with bit 5 no near end, only
    // the echo; with bit 6 the capture muted, zeros; with bit 7 the render
    // stopping for stretches, as a far end that pauses.
    uint8_t path = Byte(in);
    float gain = ldexpf(1.0f, (int)((path & 31u) % 25u) - 8);
    // A change part way, from the frame the top six bits name: with bit 0
    // the capture muted for half of what is left, as in a call; with bit 1
    // the echo's path 40 dB quieter and 32 frames later, as a speaker
    // turned down and set aside.
    uint8_t change = Byte(in);
    Signal signal;
    Fill(in, &signal, 1);
    uint32_t from = (uint32_t)((uint64_t)signal.frames * (change >> 2u) / 64u);
    uint32_t to = (change & 1u) != 0 ? from + (signal.frames - from) / 2u : from;
    bool moved = (change & 2u) != 0;
    // The capture holds the render's echo, as a loudspeaker's, over the
    // near end: 0.8 of the near end and 0.15 of the level at most, within
    // the level and so within 2^64; the render is at most 0.5 of it.
    for (uint32_t i = 0; i < signal.frames; ++i)
    {
        bool stopped = (path & 0x80u) != 0 && (i / 1000u) % 3u == 0;
        s_render[i] = stopped ? 0.0f : s_render[i] / fmaxf(gain, 1.0f);
        float echo = 0.3f * gain * s_render[i];
        if (moved && i >= from)
        {
            echo = i >= 32u ? 0.003f * gain * s_render[i - 32u] : 0.0f;
        }
        bool muted = (path & 0x40u) != 0 || (i >= from && i < to);
        s_whole[i] = muted ? 0.0f : ((path & 0x20u) != 0 ? 0.0f : 0.8f * s_whole[i]) + echo;
        s_cut[i] = s_whole[i];
    }
    int before = s_allocations;
    maudEchoState one;
    maudEchoState two;
    EXPECT_SUCCESS(maudEchoCanceller_Cancel(whole, s_whole, s_render, signal.frames, &one),
                   "the whole signal is cancelled");
    EXPECT_SUCCESS(maudEchoCanceller_Cancel(cut, s_cut, s_render, 0, &two),
                   "an empty call is cancelled");
    for (uint32_t at = 0, c = 0; at < signal.frames; ++c)
    {
        uint32_t n = CutLength(&signal, c, signal.frames - at);
        EXPECT_SUCCESS(maudEchoCanceller_Cancel(cut, s_cut + at, s_render + at, n, &two),
                       "a cut of the signal is cancelled");
        at += n;
    }
    EXPECT(s_allocations == before, "processing allocates nothing, %d allocations",
           s_allocations - before);
    size_t differs = FirstDifference(s_whole, s_cut, signal.frames);
    EXPECT(differs == signal.frames && memcmp(&one, &two, sizeof(one)) == 0,
           "the output and state do not depend on how the signal is cut, first difference at "
           "sample %zu of %u, states %s",
           differs, signal.frames, memcmp(&one, &two, sizeof(one)) == 0 ? "equal" : "differ");
    EXPECT(Finite(s_whole, signal.frames), "the output is finite, %u samples", signal.frames);
    // The probability of speech is 0 until two blocks have been
    // processed: blocks of the smallest power of two of frames lasting
    // 8 ms or more.
    uint32_t block = 1;
    while ((double)block < 0.008 * def.sampleRate)
    {
        block *= 2;
    }
    EXPECT(signal.frames < 2 * block ? one.speechProbability == 0.0f
                                     : Within(one.speechProbability, 0.1f, 1.0f),
           "a speech probability of 0 before two blocks, within [0.1, 1] after, %a after %u frames "
           "of blocks of %u",
           (double)one.speechProbability, signal.frames, block);
    EXPECT(Within(one.leakage, 0.005f, 1.0f) && one.frameCount == signal.frames,
           "a leakage within [0.005, 1] and every frame counted, %a, %llu of %u frames",
           (double)one.leakage, (unsigned long long)one.frameCount, signal.frames);
    // voice.h: a capture that holds none of the render leaves adapted false.
    EXPECT((path & 0x40u) == 0 || !one.adapted,
           "a capture that holds none of the render leaves adapted false, adapted %d", one.adapted);
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
