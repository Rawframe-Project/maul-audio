// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Renders a source circling the listener once every four seconds, at
// ear height and 2 m away, through the binaural effect, into a 16-bit
// stereo WAV file: `sample_binaural <set.maudhrtf> <out.wav>`. No device
// is opened; it uses the Spatial part alone. The shipped set is
// data/hrtf/sadie2-ku100-48k.maudhrtf.

#define _CRT_SECURE_NO_WARNINGS

#include "maul-audio/binaural.h"

#include "maul-audio/hrtf.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RATE    48000
#define BLOCK   480
#define SECONDS 8

static const double s_pi = 3.14159265358979323846;

// The whole file into memory; nullptr when it cannot be read.
static uint8_t* ReadFile(const char* path, size_t* sizeOut)
{
    FILE* file = fopen(path, "rb");
    if (file == nullptr || fseek(file, 0, SEEK_END) != 0)
    {
        if (file != nullptr)
        {
            fclose(file);
        }
        return nullptr;
    }
    long size = ftell(file);
    uint8_t* bytes = size > 0 ? malloc((size_t)size) : nullptr;
    bool read = bytes != nullptr && fseek(file, 0, SEEK_SET) == 0 &&
                fread(bytes, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    if (!read)
    {
        free(bytes);
        return nullptr;
    }
    *sizeOut = (size_t)size;
    return bytes;
}

static void Put16(FILE* file, uint32_t value)
{
    fputc((int)(value & 0xFF), file);
    fputc((int)((value >> 8) & 0xFF), file);
}

static void Put32(FILE* file, uint32_t value)
{
    Put16(file, value & 0xFFFF);
    Put16(file, value >> 16);
}

// A canonical WAV header for frames of 16-bit stereo at RATE.
static void WriteHeader(FILE* file, uint32_t frames)
{
    uint32_t data = frames * 4;
    fwrite("RIFF", 1, 4, file);
    Put32(file, 36 + data);
    fwrite("WAVEfmt ", 1, 8, file);
    Put32(file, 16);
    Put16(file, 1);
    Put16(file, 2);
    Put32(file, RATE);
    Put32(file, RATE * 4);
    Put16(file, 4);
    Put16(file, 16);
    fwrite("data", 1, 4, file);
    Put32(file, data);
}

static int16_t ToPcm(float value)
{
    float clipped = value > 1.0f ? 1.0f : (value < -1.0f ? -1.0f : value);
    return (int16_t)lrintf(clipped * 32767.0f);
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <set.maudhrtf> <out.wav>\n", argv[0]);
        return 2;
    }
    size_t size = 0;
    uint8_t* bytes = ReadFile(argv[1], &size);
    if (bytes == nullptr)
    {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    maudHrtfDef hrtfDef = maudDefaultHrtfDef();
    hrtfDef.bytes = bytes;
    hrtfDef.byteCount = size;
    maudHrtf* hrtf = nullptr;
    maudResult result = maudLoadHrtf(&hrtfDef, &hrtf);
    free(bytes);
    if (result != maud_success)
    {
        fprintf(stderr, "the set does not load: %s\n", maudResultName(result));
        return 1;
    }
    maudBinauralDef def = maudDefaultBinauralDef();
    def.hrtf = hrtf;
    maudBinaural* effect = nullptr;
    result = maudCreateBinaural(&def, &effect);
    FILE* out = result == maud_success ? fopen(argv[2], "wb") : nullptr;
    if (out == nullptr)
    {
        fprintf(stderr, "cannot start: %s\n", maudResultName(result));
        maudDestroyBinaural(effect);
        maudDestroyHrtf(hrtf);
        return 1;
    }
    WriteHeader(out, RATE * SECONDS);
    // Noise bursts of 100 ms every 250 ms, which the ear places better
    // than a tone; a fixed seed keeps the file the same each run.
    static float in[BLOCK];
    static float left[BLOCK];
    static float right[BLOCK];
    float* ears[2] = {left, right};
    uint32_t seed = 1;
    for (uint32_t at = 0; at < RATE * SECONDS && result == maud_success; at += BLOCK)
    {
        for (uint32_t i = 0; i < BLOCK; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            bool on = (at + i) % (RATE / 4) < RATE / 10;
            in[i] = on ? 0.25f * ((float)(seed >> 8) / 8388608.0f - 1.0f) : 0.0f;
        }
        double angle = 2.0 * s_pi * at / (4.0 * RATE);
        // Ahead is -Z, right is +X, in the listener's frame.
        maudBinauralParams params = {{(float)(2.0 * sin(angle)), 0.0f, (float)(-2.0 * cos(angle))},
                                     1.0f};
        result = maudProcessBinaural(effect, &params, in, ears, BLOCK);
        for (uint32_t i = 0; i < BLOCK; ++i)
        {
            int16_t frame[2] = {ToPcm(left[i]), ToPcm(right[i])};
            Put16(out, (uint16_t)frame[0]);
            Put16(out, (uint16_t)frame[1]);
        }
    }
    fclose(out);
    maudDestroyBinaural(effect);
    maudDestroyHrtf(hrtf);
    if (result != maud_success)
    {
        fprintf(stderr, "rendering failed: %s\n", maudResultName(result));
        return 1;
    }
    printf("wrote %s: %d s, a source circling once every 4 s\n", argv[2], SECONDS);
    return 0;
}
