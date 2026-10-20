// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The voice processing's cost per 10 ms block: the noise suppressor at
// 16 and 48 kHz, mono and stereo, on white noise with tone bursts; the
// echo canceller at 16 and 48 kHz with a 0.2 s path, on a render of
// noise and its echo; the voice detector and the gain control at 16 and
// 48 kHz, mono; and each of the four at 48 kHz on digital silence after a
// signal, its decaying states long settled (a state left to decay into
// the subnormal range shows there first).
// Prints the best of five runs in microseconds per block, each row
// beside its recorded baseline.

// fopen reads the baseline; the C runtime's warning that it is unsafe
// is about the Annex K alternative, which the family does not use.
#define _CRT_SECURE_NO_WARNINGS

#include "baseline.h"

#include "maul-audio/voice.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#define BLOCKS 3000

static float s_frames[2 * 480 * 10];
static float s_render[480];

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static void Run(uint32_t rate, maudChannelLayout layout, uint32_t channels, const char* key)
{
    maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();
    def.sampleRate = rate;
    def.layout = layout;
    maudNoiseSuppressor* s = nullptr;
    if (maudCreateNoiseSuppressor(&def, &s) != maud_success)
    {
        return;
    }
    uint32_t block = rate / 100;
    uint32_t state = 1;
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int b = 0; b < BLOCKS; ++b)
        {
            for (uint32_t i = 0; i < block * channels; ++i)
            {
                state = state * 1664525u + 1013904223u;
                float noise = ((float)(state >> 8) / 16777216.0f - 0.5f) * 0.02f;
                s_frames[i] = noise + ((b / 30) % 2 ? 0.2f * sinf(0.07f * (float)i) : 0.0f);
            }
            if (maudSuppressNoise(s, s_frames, block, nullptr) != maud_success)
            {
                return;
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    double micro = best / BLOCKS * 1e6;
    printf("noise suppressor, %3u kHz, %u channel%s %7.2f us per 10 ms block\n", rate / 1000,
           channels, channels > 1 ? "s" : " ", micro);
    Against(key, micro, false);
    maudDestroyNoiseSuppressor(s);
}

static void RunEcho(uint32_t rate, const char* key)
{
    maudEchoCancellerDef def = maudDefaultEchoCancellerDef();
    def.sampleRate = rate;
    maudEchoCanceller* c = nullptr;
    if (maudCreateEchoCanceller(&def, &c) != maud_success)
    {
        return;
    }
    uint32_t block = rate / 100;
    uint32_t state = 1;
    float echo = 0.0f;
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int b = 0; b < BLOCKS; ++b)
        {
            for (uint32_t i = 0; i < block; ++i)
            {
                state = state * 1664525u + 1013904223u;
                s_render[i] = ((float)(state >> 8) / 16777216.0f - 0.5f) * 0.2f;
                echo = 0.9f * echo + 0.05f * s_render[i];
                s_frames[i] = echo;
            }
            if (maudCancelEcho(c, s_frames, s_render, block, nullptr) != maud_success)
            {
                return;
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    double micro = best / BLOCKS * 1e6;
    printf("echo canceller,   %3u kHz, mono      %7.2f us per 10 ms block\n", rate / 1000, micro);
    Against(key, micro, false);
    maudDestroyEchoCanceller(c);
}

// The voice detector or the gain control, mono, on noise with tone
// bursts as the suppressor's.
static void RunLevel(uint32_t rate, bool gain, const char* key)
{
    maudVoiceDetectorDef detectorDef = maudDefaultVoiceDetectorDef();
    maudGainControlDef gainDef = maudDefaultGainControlDef();
    detectorDef.sampleRate = rate;
    gainDef.sampleRate = rate;
    maudVoiceDetector* detector = nullptr;
    maudGainControl* control = nullptr;
    if ((gain ? maudCreateGainControl(&gainDef, &control)
              : maudCreateVoiceDetector(&detectorDef, &detector)) != maud_success)
    {
        return;
    }
    uint32_t block = rate / 100;
    uint32_t state = 1;
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int b = 0; b < BLOCKS; ++b)
        {
            for (uint32_t i = 0; i < block; ++i)
            {
                state = state * 1664525u + 1013904223u;
                float noise = ((float)(state >> 8) / 16777216.0f - 0.5f) * 0.02f;
                s_frames[i] = noise + ((b / 30) % 2 ? 0.2f * sinf(0.07f * (float)i) : 0.0f);
            }
            if ((gain ? maudApplyGainControl(control, s_frames, block, nullptr)
                      : maudDetectVoice(detector, s_frames, block, nullptr)) != maud_success)
            {
                return;
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    double micro = best / BLOCKS * 1e6;
    printf("%s %3u kHz, mono      %7.2f us per 10 ms block\n",
           gain ? "gain control,    " : "voice detector,  ", rate / 1000, micro);
    Against(key, micro, false);
    maudDestroyGainControl(control);
    maudDestroyVoiceDetector(detector);
}

// The four processors, by row.
typedef enum Part
{
    PART_NOISE,
    PART_ECHO,
    PART_DETECTOR,
    PART_GAIN
} Part;

typedef struct Processors
{
    maudNoiseSuppressor* noise;
    maudEchoCanceller* echo;
    maudVoiceDetector* detector;
    maudGainControl* gain;
} Processors;

static maudResult Step(Processors* p, Part part, uint32_t block)
{
    switch (part)
    {
    case PART_NOISE:
        return maudSuppressNoise(p->noise, s_frames, block, nullptr);
    case PART_ECHO:
        return maudCancelEcho(p->echo, s_frames, s_render, block, nullptr);
    case PART_DETECTOR:
        return maudDetectVoice(p->detector, s_frames, block, nullptr);
    case PART_GAIN:
        return maudApplyGainControl(p->gain, s_frames, block, nullptr);
    }
    return maud_errorInvalid;
}

// One processor at 48 kHz on silence: 3 s of noise with tone bursts (and
// for the echo canceller a render of noise and its echo), 60 s of zeros
// for its slowest states to settle, then the timed blocks of zeros.
static void RunSilent(Part part, const char* name, const char* key)
{
    const uint32_t rate = 48000;
    const uint32_t block = rate / 100;
    maudNoiseSuppressorDef noiseDef = maudDefaultNoiseSuppressorDef();
    maudEchoCancellerDef echoDef = maudDefaultEchoCancellerDef();
    maudVoiceDetectorDef detectorDef = maudDefaultVoiceDetectorDef();
    maudGainControlDef gainDef = maudDefaultGainControlDef();
    noiseDef.sampleRate = rate;
    echoDef.sampleRate = rate;
    detectorDef.sampleRate = rate;
    gainDef.sampleRate = rate;
    Processors p = {nullptr, nullptr, nullptr, nullptr};
    maudResult made = part == PART_NOISE      ? maudCreateNoiseSuppressor(&noiseDef, &p.noise)
                      : part == PART_ECHO     ? maudCreateEchoCanceller(&echoDef, &p.echo)
                      : part == PART_DETECTOR ? maudCreateVoiceDetector(&detectorDef, &p.detector)
                                              : maudCreateGainControl(&gainDef, &p.gain);
    if (made != maud_success)
    {
        return;
    }
    uint32_t state = 1;
    float echo = 0.0f;
    bool failed = false;
    for (int b = 0; b < 6300 && !failed; ++b)
    {
        bool loud = b < 300;
        for (uint32_t i = 0; i < block; ++i)
        {
            state = state * 1664525u + 1013904223u;
            float noise = ((float)(state >> 8) / 16777216.0f - 0.5f) * 0.2f;
            s_render[i] = loud ? noise : 0.0f;
            echo = loud ? 0.9f * echo + 0.05f * noise : 0.0f;
            float burst = (b / 30) % 2 ? 0.2f * sinf(0.07f * (float)i) : 0.0f;
            s_frames[i] = loud ? (part == PART_ECHO ? echo : 0.1f * noise + burst) : 0.0f;
        }
        failed = Step(&p, part, block) != maud_success;
    }
    double best = 1e9;
    for (int run = 0; run < 5 && !failed; ++run)
    {
        double start = Seconds();
        for (int b = 0; b < BLOCKS && !failed; ++b)
        {
            for (uint32_t i = 0; i < block; ++i)
            {
                s_frames[i] = 0.0f;
            }
            failed = Step(&p, part, block) != maud_success;
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    if (!failed)
    {
        double micro = best / BLOCKS * 1e6;
        printf("%-17s  48 kHz, silent    %7.2f us per 10 ms block\n", name, micro);
        Against(key, micro, false);
    }
    maudDestroyNoiseSuppressor(p.noise);
    maudDestroyEchoCanceller(p.echo);
    maudDestroyVoiceDetector(p.detector);
    maudDestroyGainControl(p.gain);
}

int main(void)
{
    Run(16000, maud_layoutMono, 1, "noise.16k.mono.us");
    Run(48000, maud_layoutMono, 1, "noise.48k.mono.us");
    Run(48000, maud_layoutStereo, 2, "noise.48k.stereo.us");
    RunEcho(16000, "echo.16k.us");
    RunEcho(48000, "echo.48k.us");
    RunLevel(16000, false, "detector.16k.us");
    RunLevel(48000, false, "detector.48k.us");
    RunLevel(16000, true, "gain.16k.us");
    RunLevel(48000, true, "gain.48k.us");
    RunSilent(PART_NOISE, "noise suppressor,", "noise.48k.silent.us");
    RunSilent(PART_ECHO, "echo canceller,", "echo.48k.silent.us");
    RunSilent(PART_DETECTOR, "voice detector,", "detector.48k.silent.us");
    RunSilent(PART_GAIN, "gain control,", "gain.48k.silent.us");
    return 0;
}
