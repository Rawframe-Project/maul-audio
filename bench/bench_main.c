// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Throughput of interleaving and deinterleaving 480-frame blocks (10 ms
// at 48 kHz) in stereo, 5.1 and 7.1.4. Prints the best of five runs in
// millions of samples per second.

#include "maul-audio/buffer.h"

#include <stdio.h>
#include <time.h>

#define FRAMES       480
#define MAX_CHANNELS 12
#define BLOCKS       20000

static float s_planar[MAX_CHANNELS][FRAMES];
static float s_interleaved[MAX_CHANNELS * FRAMES];

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static double Run(uint32_t channelCount, bool interleave)
{
    const float* in[MAX_CHANNELS];
    float* out[MAX_CHANNELS];
    for (uint32_t c = 0; c < channelCount; ++c)
    {
        in[c] = s_planar[c];
        out[c] = s_planar[c];
    }
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int block = 0; block < BLOCKS; ++block)
        {
            maudResult result = interleave
                                    ? maudInterleave(in, channelCount, FRAMES, s_interleaved)
                                    : maudDeinterleave(s_interleaved, channelCount, FRAMES, out);
            if (result != maud_success)
            {
                return 0.0;
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    return (double)channelCount * FRAMES * BLOCKS / best / 1e6;
}

int main(void)
{
    for (uint32_t c = 0; c < MAX_CHANNELS; ++c)
    {
        for (uint32_t i = 0; i < FRAMES; ++i)
        {
            s_planar[c][i] = (float)(c + i);
        }
    }
    static const uint32_t counts[] = {2, 6, 12};
    for (size_t k = 0; k < sizeof(counts) / sizeof(counts[0]); ++k)
    {
        printf("interleave   %2u channels: %8.0f Msamples/s\n", counts[k], Run(counts[k], true));
        printf("deinterleave %2u channels: %8.0f Msamples/s\n", counts[k], Run(counts[k], false));
    }
    return 0;
}
