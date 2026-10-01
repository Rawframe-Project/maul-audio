// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Deterministic test signals for the voice processors: white noise from
// a seeded generator, tones, and synthetic speech, harmonic "syllables"
// shaped by vowel formants, in words separated by pauses, with the
// truth of each 10 ms frame.

#ifndef MAUL_AUDIO_TEST_SIGNALS_H
#define MAUL_AUDIO_TEST_SIGNALS_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SIGNAL_PI 3.14159265358979323846

typedef struct Signal
{
    float* samples;
    uint32_t count;
    uint32_t rate;
    // Per 10 ms frame: whether a syllable sounds in it.
    bool* speech;
    uint32_t frames;
} Signal;

static uint32_t s_seed = 0x9E3779B9u;

[[maybe_unused]] static float Uniform(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return (float)((double)s_seed / 2147483648.0 - 1.0);
}

[[maybe_unused]] static double DbToAmplitude(double db)
{
    return pow(10.0, db / 20.0);
}

[[maybe_unused]] static Signal MakeSignal(uint32_t rate, double seconds)
{
    Signal signal = {.rate = rate, .count = (uint32_t)(rate * seconds)};
    signal.frames = signal.count / (rate / 100);
    signal.samples = calloc(signal.count, sizeof(float));
    signal.speech = calloc(signal.frames + 1, sizeof(bool));
    return signal;
}

[[maybe_unused]] static void FreeSignal(Signal* signal)
{
    free(signal->samples);
    free(signal->speech);
    *signal = (Signal){0};
}

// Adds white noise at an RMS level in dBFS.
[[maybe_unused]] static void AddNoise(Signal* signal, double dbfs, uint32_t seed)
{
    s_seed = seed;
    float scale = (float)(DbToAmplitude(dbfs) * sqrt(3.0));
    for (uint32_t i = 0; i < signal->count; ++i)
    {
        signal->samples[i] += scale * Uniform();
    }
}

// Adds a sine of a frequency and RMS level from one time on.
[[maybe_unused]] static void AddTone(Signal* signal, double frequency, double dbfs, double from)
{
    double amplitude = DbToAmplitude(dbfs) * sqrt(2.0);
    for (uint32_t i = (uint32_t)(from * signal->rate); i < signal->count; ++i)
    {
        signal->samples[i] +=
            (float)(amplitude * sin(2.0 * SIGNAL_PI * frequency * i / signal->rate));
    }
}

// The gain of the vowel /a/'s formants (700, 1200, 2500 Hz) at a
// frequency, with a falling tilt.
[[maybe_unused]] static double Formants(double frequency)
{
    static const double centres[3] = {700.0, 1200.0, 2500.0};
    static const double widths[3] = {110.0, 120.0, 160.0};
    double gain = 0.0;
    for (int f = 0; f < 3; ++f)
    {
        double d = (frequency - centres[f]) / widths[f];
        gain += exp(-0.5 * d * d) / (1.0 + f);
    }
    return gain + 0.02;
}

// Adds speech from `from` to `to` seconds at an RMS level (while it
// sounds) in dBFS: words of four 200 ms syllables 50 ms apart, then
// 600 ms of pause. Marks the frames that sound.
[[maybe_unused]] static void AddSpeech(Signal* signal, double dbfs, double from, double to)
{
    uint32_t rate = signal->rate;
    uint32_t start = (uint32_t)(from * rate);
    uint32_t end = (uint32_t)(to * rate) < signal->count ? (uint32_t)(to * rate) : signal->count;
    double phase = 0.0;
    double word = 4 * 0.25 + 0.6;
    float* voice = calloc(signal->count, sizeof(float));
    double energy = 0.0;
    uint32_t sounding = 0;
    for (uint32_t i = start; i < end; ++i)
    {
        double t = (double)(i - start) / rate;
        double inWord = fmod(t, word);
        double inSyllable = fmod(inWord, 0.25);
        if (inWord >= 1.0 || inSyllable >= 0.2)
        {
            continue;
        }
        double envelope = sin(SIGNAL_PI * inSyllable / 0.2);
        double pitch = 140.0 + 30.0 * sin(2.0 * SIGNAL_PI * 0.7 * t);
        phase = fmod(phase + 2.0 * SIGNAL_PI * pitch / rate, 2.0 * SIGNAL_PI);
        double sample = 0.0;
        for (int k = 1; k * pitch < 4000.0 && k * pitch < rate / 2.0; ++k)
        {
            sample += Formants(k * pitch) * sin(k * phase);
        }
        voice[i] = (float)(envelope * sample);
        energy += (double)voice[i] * (double)voice[i];
        sounding++;
        signal->speech[i / (rate / 100)] = true;
    }
    double scale = sounding > 0 ? DbToAmplitude(dbfs) / sqrt(energy / sounding) : 0.0;
    for (uint32_t i = start; i < end; ++i)
    {
        signal->samples[i] += (float)((double)voice[i] * scale);
    }
    free(voice);
}

// Copies a mono signal to `channels` interleaved channels.
[[maybe_unused]] static float* Interleave(const Signal* signal, uint32_t channels)
{
    float* frames = malloc((size_t)signal->count * channels * sizeof(float));
    for (uint32_t i = 0; i < signal->count; ++i)
    {
        for (uint32_t c = 0; c < channels; ++c)
        {
            frames[(size_t)i * channels + c] = signal->samples[i];
        }
    }
    return frames;
}

#endif // MAUL_AUDIO_TEST_SIGNALS_H
