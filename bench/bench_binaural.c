// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The binaural effect's cost per source on the shipped SADIE II set at
// 48 kHz (128 taps), in 480-frame blocks (10 ms): a source standing
// still without the near field and with it, and one whose direction
// changes every block, so that it fades in every block. Prints the best of five runs as
// microseconds per block and as sources per millisecond of one core for each 10 ms of audio.

#define _CRT_SECURE_NO_WARNINGS

#include "maul-audio/binaural.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

#define FRAMES 480
#define BLOCKS 20000

static unsigned char s_bytes[900000];
static float s_in[FRAMES];
static float s_left[FRAMES];
static float s_right[FRAMES];

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static double Run(const maudHrtf* hrtf, bool moving, bool nearField)
{
    maudBinauralDef def = maudDefaultBinauralDef();
    def.hrtf = hrtf;
    def.maxFrames = FRAMES;
    def.nearField = nearField;
    maudBinaural* effect = nullptr;
    if (maudCreateBinaural(&def, &effect) != maud_success)
    {
        return 0.0;
    }
    float* out[2] = {s_left, s_right};
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int block = 0; block < BLOCKS; ++block)
        {
            float angle = moving ? 0.01f * (float)block : 0.5f;
            maudBinauralParams params = {{sinf(angle), 0.2f, -cosf(angle)}, 1.0f};
            if (maudProcessBinaural(effect, &params, s_in, out, FRAMES) != maud_success)
            {
                return 0.0;
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    maudDestroyBinaural(effect);
    return best / BLOCKS * 1e6;
}

int main(void)
{
    FILE* file = fopen(MAUD_DATA_DIR "/hrtf/sadie2-ku100-48k.maudhrtf", "rb");
    if (file == nullptr)
    {
        printf("no data file\n");
        return 1;
    }
    size_t count = fread(s_bytes, 1, sizeof(s_bytes), file);
    fclose(file);
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = s_bytes;
    def.byteCount = count;
    maudHrtf* hrtf = nullptr;
    if (maudLoadHrtf(&def, &hrtf) != maud_success)
    {
        printf("the set does not load\n");
        return 1;
    }
    for (int i = 0; i < FRAMES; ++i)
    {
        s_in[i] = sinf(0.05f * (float)i);
    }
    const char* names[3] = {"still, no near field", "still", "moving"};
    for (int row = 0; row < 3; ++row)
    {
        double micro = Run(hrtf, row == 2, row != 0);
        printf("binaural, %-20s %7.2f us per 10 ms block, %6.1f sources per ms of a core\n",
               names[row], micro, micro > 0.0 ? 1000.0 / micro : 0.0);
    }
    maudDestroyHrtf(hrtf);
    return 0;
}
