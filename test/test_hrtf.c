// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// HRTF sets: the shipped SADIE II KU100 file loads with its name and
// license, its interaural delay on the right side, and resampled to 44.1
// and 96 kHz with its response at 1 kHz kept (the resampled responses
// start 24 input samples early, a constant latency); crafted files
// with a valid checksum are refused for each rule they break; a seeded
// sweep of mutations loads exactly the well-formed ones and never
// crashes; an allocator that fails is a capacity error with nothing
// leaked.

// fopen reads the shipped set; the C runtime's warning that it is unsafe
// is about the Annex K alternative, which the family does not use.
#define _CRT_SECURE_NO_WARNINGS

#include "hrtf_core.h"
#include "hrtf_file.h"
#include "hrtf_resample.h"
#include "test_harness.h"

#include "maul-audio/hrtf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

static long s_live;
static long s_failAfter = -1;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    if (s_failAfter == 0)
    {
        return nullptr;
    }
    s_failAfter -= s_failAfter > 0 ? 1 : 0;
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

static maudResult Load(const void* bytes, size_t count, uint32_t rate, maudHrtf** hrtfOut)
{
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = bytes;
    def.byteCount = count;
    def.sampleRate = rate;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    return maudLoadHrtf(&def, hrtfOut);
}

// The file's bytes in a buffer the caller frees, or NULL.
static unsigned char* ReadFile(const char* path, size_t* countOut)
{
    FILE* file = fopen(path, "rb");
    if (file == nullptr)
    {
        return nullptr;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char* bytes = size > 0 ? malloc((size_t)size) : nullptr;
    *countOut = bytes != nullptr ? fread(bytes, 1, (size_t)size, file) : 0;
    fclose(file);
    return bytes;
}

// The magnitude of a response at a frequency.
static double MagnitudeAt(const float* taps, uint32_t count, double frequency, double rate)
{
    double re = 0.0;
    double im = 0.0;
    for (uint32_t n = 0; n < count; ++n)
    {
        re += (double)taps[n] * cos(2.0 * PI * frequency * n / rate);
        im -= (double)taps[n] * sin(2.0 * PI * frequency * n / rate);
    }
    return sqrt(re * re + im * im);
}

// The direction on the horizontal ring at an azimuth in degrees.
static uint32_t Horizontal(const maudHrtf* hrtf, uint32_t azimuthDegrees)
{
    for (uint32_t ring = 0; ring < hrtf->ringCount; ++ring)
    {
        if (hrtf->elevations[ring] == 0.0f)
        {
            return hrtf->firstDirection[ring] + azimuthDegrees * hrtf->azimuths[ring] / 360u;
        }
    }
    return 0;
}

static void TestShippedSet(void)
{
#if defined(__EMSCRIPTEN__)
    // Node runs the test without the host's file system.
    return;
#endif
    size_t count = 0;
    unsigned char* bytes = ReadFile(MAUD_DATA_DIR "/hrtf/sadie2-ku100-48k.maudhrtf", &count);
    CHECK(bytes != nullptr && count == 853896, "the shipped file");
    if (bytes == nullptr)
    {
        return;
    }
    maudHrtf* hrtf = nullptr;
    CHECK(Load(bytes, count, 0, &hrtf) == maud_success, "loads");
    maudHrtfInfo info = {0};
    CHECK(maudGetHrtfInfo(hrtf, &info) == maud_success && info.sampleRate == 48000 &&
              info.taps == 128 && info.directionCount == 1652 && info.ringCount == 37 &&
              info.distance == 1.2f,
          "at 48 kHz, 128 taps, 1,652 directions on 37 rings, measured at 1.2 m");
    CHECK(info.nameLength > 8 && memcmp(info.name, "SADIE II", 8) == 0, "named");
    CHECK(info.licenseLength > 0 && strstr(info.license, "Apache License") != nullptr,
          "with its license");
    uint32_t left = Horizontal(hrtf, 90);
    uint32_t right = Horizontal(hrtf, 270);
    CHECK(hrtf->delays[2 * left] + 30.0f < hrtf->delays[2 * left + 1] &&
              hrtf->delays[2 * right + 1] + 30.0f < hrtf->delays[2 * right],
          "the nearer ear hears first, by more than 30 samples at the side");
    const float* ahead = hrtf->responses + 2u * Horizontal(hrtf, 0) * hrtf->taps;
    double at48 = MagnitudeAt(ahead, hrtf->taps, 1000.0, 48000.0);
    uint32_t rates[2] = {44100, 96000};
    uint32_t expectedTaps[2] = {118 + 23, 256 + 48};
    for (int i = 0; i < 2; ++i)
    {
        maudHrtf* other = nullptr;
        CHECK(Load(bytes, count, rates[i], &other) == maud_success &&
                  other->taps == expectedTaps[i],
              "resampled to another rate, with taps in proportion");
        const float* moved = other->responses + 2u * Horizontal(other, 0) * other->taps;
        double ratio = MagnitudeAt(moved, other->taps, 1000.0, rates[i]) / at48;
        CHECK(fabs(20.0 * log10(ratio)) < 0.1, "keeping the response at 1 kHz within 0.1 dB");
        float scaled = other->delays[2 * left + 1] / hrtf->delays[2 * left + 1];
        CHECK(fabsf(scaled - (float)rates[i] / 48000.0f) < 1e-4f, "and the delays in proportion");
        maudDestroyHrtf(other);
    }
    maudDestroyHrtf(hrtf);
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = bytes;
    def.byteCount = count;
    def.maxDirections = 1000;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_errorCapacity && hrtf == nullptr,
          "more directions than the limit");
    def.maxDirections = 65536;
    def.maxTaps = 100;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_errorCapacity, "more taps than the limit");
    def.maxTaps = 200;
    def.sampleRate = 96000;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_errorCapacity, "or than it after resampling");
    maudDestroyHrtf(nullptr);
    free(bytes);
}

// A small file: two rings (-90 with one azimuth, 90 with three), 8 taps.
typedef struct Crafted
{
    // The file, and room for one byte past it.
    unsigned char bytes[48 + 4 + 8 + 16 + 16 + 4 * 2 * 8 * 2 + 1];
    size_t count;
} Crafted;

static void Put32(unsigned char* at, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
    {
        at[i] = (unsigned char)(value >> (8 * i));
    }
}

static void PutFloat(unsigned char* at, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    Put32(at, bits);
}

static void Seal(Crafted* file)
{
    Put32(file->bytes + 44, maudCrc32(file->bytes + 48, file->count - 48));
}

static Crafted Craft(void)
{
    Crafted file = {.count = sizeof(file.bytes) - 1};
    memcpy(file.bytes, "MAUDHRTF", 8);
    Put32(file.bytes + 8, 2);
    Put32(file.bytes + 12, 48000);
    Put32(file.bytes + 16, 8);
    Put32(file.bytes + 20, 2);
    Put32(file.bytes + 24, 4);
    PutFloat(file.bytes + 28, 1.0f / 32768.0f);
    PutFloat(file.bytes + 32, 1.5f);
    Put32(file.bytes + 36, 4);
    Put32(file.bytes + 40, 8);
    memcpy(file.bytes + 48, "Test", 4);
    memcpy(file.bytes + 52, "License.", 8);
    PutFloat(file.bytes + 60, -90.0f);
    Put32(file.bytes + 64, 1);
    PutFloat(file.bytes + 68, 90.0f);
    Put32(file.bytes + 72, 3);
    for (size_t i = 76; i < file.count; ++i)
    {
        file.bytes[i] = (unsigned char)(i * 7u);
    }
    Seal(&file);
    return file;
}

static maudResult LoadCrafted(const Crafted* file)
{
    maudHrtf* hrtf = nullptr;
    maudResult result = Load(file->bytes, file->count, 0, &hrtf);
    maudDestroyHrtf(hrtf);
    return result;
}

static void TestCraftedFiles(void)
{
    Crafted file = Craft();
    CHECK(LoadCrafted(&file) == maud_success, "a well-formed small file");
    file = Craft();
    file.count--;
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "one byte short");
    file = Craft();
    file.count++;
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "one byte long");
    file = Craft();
    file.bytes[100] ^= 1;
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a flipped bit the checksum catches");
    file = Craft();
    file.bytes[0] = 'X';
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a wrong magic");
    file = Craft();
    Put32(file.bytes + 8, 1);
    CHECK(LoadCrafted(&file) == maud_errorUnsupported, "version 1, never released");
    file = Craft();
    Put32(file.bytes + 8, 3);
    CHECK(LoadCrafted(&file) == maud_errorUnsupported, "a later version");
    file = Craft();
    PutFloat(file.bytes + 68, -90.0f);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "elevations that do not rise");
    file = Craft();
    PutFloat(file.bytes + 68, 91.0f);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "an elevation past 90");
    file = Craft();
    Put32(file.bytes + 72, 2);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "azimuths not adding up to the directions");
    file = Craft();
    Put32(file.bytes + 72, 4);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "nor adding up to more");
    file = Craft();
    Put32(file.bytes + 64, 0);
    Put32(file.bytes + 72, 4);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a ring without an azimuth");
    file = Craft();
    file.bytes[49] = 0xC0;
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a name that is not UTF-8");
    // Well-shaped sequences whose values UTF-8 excludes, in the name.
    const char* excluded[3][2] = {{"\xC1\xBF", "an overlong character"},
                                  {"\xED\xA0\x80", "a surrogate"},
                                  {"\xF4\x90\x80\x80", "a character past U+10FFFF"}};
    for (int i = 0; i < 3; ++i)
    {
        file = Craft();
        memcpy(file.bytes + 48, excluded[i][0], strlen(excluded[i][0]));
        Seal(&file);
        CHECK(LoadCrafted(&file) == maud_errorInvalid, excluded[i][1]);
    }
    file = Craft();
    file.bytes[59] = 0xE2;
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a license cut inside a character");
    // A lead byte last, and a continuation byte right after the text, in
    // the first ring's elevation (-89.99999): the character may not borrow
    // it.
    file = Craft();
    file.bytes[59] = 0xC3;
    Put32(file.bytes + 60, 0xC2B3FFA9u);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a character reaching past its text");
    file = Craft();
    PutFloat(file.bytes + 28, NAN);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a scale that is not a number");
    // Four taps, the file cut to match (16 bytes of taps per tap gone).
    file = Craft();
    Put32(file.bytes + 16, 4);
    file.count -= 4 * 16;
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "fewer taps than the format allows");
    file = Craft();
    PutFloat(file.bytes + 28, INFINITY);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "an infinite scale");
    // Distances just outside the bounds, and not a number.
    float distances[3] = {0.049f, 100.5f, NAN};
    for (int i = 0; i < 3; ++i)
    {
        file = Craft();
        PutFloat(file.bytes + 32, distances[i]);
        Seal(&file);
        CHECK(LoadCrafted(&file) == maud_errorInvalid, "a measurement distance out of bounds");
    }
    file = Craft();
    Put32(file.bytes + 40, 0xFFFFFFF0u);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a license length past the bound");
    maudHrtf* hrtf = (maudHrtf*)&file;
    maudHrtfDef def = maudDefaultHrtfDef();
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_errorInvalid && hrtf == nullptr, "no bytes");
    def.bytes = file.bytes;
    def.cookie = 0;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_errorInvalid, "no cookie");
    CHECK(maudLoadHrtf(nullptr, &hrtf) == maud_errorInvalid, "no def");
    CHECK(maudGetHrtfInfo(nullptr, &(maudHrtfInfo){0}) == maud_errorInvalid, "no set");
}

// Seeded mutations of the small file, half of them sealed again so they
// reach the checks past the checksum: each loads or is refused, and what
// loads is well-formed by the reader's own account.
static void TestMutations(void)
{
    uint32_t state = 0x2545F491u;
    uint32_t loaded = 0;
    for (int round = 0; round < 20000; ++round)
    {
        Crafted file = Craft();
        int edits = 1 + (int)(state % 4);
        for (int edit = 0; edit < edits; ++edit)
        {
            state = state * 1664525u + 1013904223u;
            size_t at = (state >> 8) % file.count;
            file.bytes[at] = (unsigned char)(state >> 24);
        }
        state = state * 1664525u + 1013904223u;
        if ((state & 7u) == 0)
        {
            file.count = (state >> 8) % file.count;
        }
        if ((state & 16u) != 0 && file.count >= 48)
        {
            Seal(&file);
        }
        maudResult result = LoadCrafted(&file);
        maudHrtfFile view;
        bool wellFormed = maudReadHrtfFile(file.bytes, file.count, &view) == maud_success;
        loaded += result == maud_success ? 1u : 0u;
        if ((result == maud_success) != wellFormed)
        {
            CHECK(false, "a mutation loads exactly when it is well-formed");
            return;
        }
    }
    printf("mutations loaded: %u of 20000\n", loaded);
    CHECK(loaded > 0, "some mutations stay well-formed");
}

// Energy of count samples.
static double Energy(const float* samples, uint32_t count)
{
    double sum = 0.0;
    for (uint32_t i = 0; i < count; ++i)
    {
        sum += (double)samples[i] * (double)samples[i];
    }
    return sum;
}

// A Hann-windowed tone burst at 48 kHz, resampled to 44.1 kHz: at 10 and
// 20 kHz it keeps its energy within 0.1 dB (scaled by the rate, as a
// filter's taps are); at 23.5 kHz, past the new Nyquist frequency, it is
// 30 dB down rather than folded to 20.6 kHz. The windowed sinc measures
// within 0.01 dB and at -42.7 dB; an unwindowed one ripples by 0.3 dB at
// 20 kHz and lets 23.5 kHz through at -27 dB.
static void TestResampler(void)
{
    enum
    {
        TAPS = 1024
    };
    static float in[TAPS];
    static float out[TAPS + 64];
    double frequencies[3] = {10000.0, 20000.0, 23500.0};
    for (int f = 0; f < 3; ++f)
    {
        for (uint32_t n = 0; n < TAPS; ++n)
        {
            double window = 0.5 - 0.5 * cos(2.0 * PI * n / (TAPS - 1));
            in[n] = (float)(window * cos(2.0 * PI * frequencies[f] * n / 48000.0));
        }
        uint32_t taps = maudResampledTaps(TAPS, 48000, 44100);
        CHECK(taps <= TAPS + 64, "room for the resampled burst");
        maudResampleResponse(in, TAPS, 48000, out, taps, 44100);
        double ratio = Energy(out, taps) / (Energy(in, TAPS) * 48000.0 / 44100.0);
        if (f < 2)
        {
            CHECK(fabs(ratio - 1.0) < 0.02,
                  "a tone below both Nyquist frequencies keeps its energy");
        }
        else
        {
            CHECK(ratio < 0.001, "a tone past the new Nyquist frequency is removed, not folded");
        }
    }
}

static void TestFailingAllocator(void)
{
    Crafted file = Craft();
    for (long failAt = 0; failAt < 2; ++failAt)
    {
        s_failAfter = failAt;
        maudHrtf* hrtf = nullptr;
        CHECK(Load(file.bytes, file.count, 0, &hrtf) == maud_errorCapacity && hrtf == nullptr,
              "a failing allocator is a capacity error");
    }
    s_failAfter = -1;
    CHECK(s_live == 0, "and leaves nothing behind");
}

int main(void)
{
    TestShippedSet();
    TestCraftedFiles();
    TestMutations();
    TestResampler();
    TestFailingAllocator();
    CHECK(s_live == 0, "every block returned");
    return s_failures == 0 ? 0 : 1;
}
