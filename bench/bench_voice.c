// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The voice processing's cost per 10 ms block: the noise suppressor at
// 16 and 48 kHz, mono and stereo, on white noise with tone bursts.
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

int main(void)
{
    Run(16000, maud_layoutMono, 1, "noise.16k.mono.us");
    Run(48000, maud_layoutMono, 1, "noise.48k.mono.us");
    Run(48000, maud_layoutStereo, 2, "noise.48k.stereo.us");
    return 0;
}
