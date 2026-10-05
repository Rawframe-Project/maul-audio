// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// HRTF sets: the shipped SADIE II KU100 file loads with its name and
// license, its interaural delay on the right side, and resampled to 44.1
// and 96 kHz with its response at 1 kHz kept (the resampled responses
// start early by the sinc's reach, a constant latency); crafted files
// with a valid checksum are refused for each rule they break; a seeded
// sweep of mutations loads exactly the well-formed ones and never
// crashes; an allocator that fails is a capacity error with nothing
// leaked.

#include "hrtf_core.h"
#include "hrtf_file.h"
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
    CHECK(bytes != nullptr && count == 853892, "the shipped file");
    if (bytes == nullptr)
    {
        return;
    }
    maudHrtf* hrtf = nullptr;
    CHECK(Load(bytes, count, 0, &hrtf) == maud_success, "loads");
    maudHrtfInfo info = {0};
    CHECK(maudGetHrtfInfo(hrtf, &info) == maud_success && info.sampleRate == 48000 &&
              info.taps == 128 && info.directionCount == 1652 && info.ringCount == 37,
          "at 48 kHz, 128 taps, 1,652 directions on 37 rings");
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
    uint32_t expectedTaps[2] = {118 + 16, 256 + 32};
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
    unsigned char bytes[44 + 4 + 8 + 16 + 16 + 4 * 2 * 8 * 2];
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
    Put32(file->bytes + 40, maudCrc32(file->bytes + 44, file->count - 44));
}

static Crafted Craft(void)
{
    Crafted file = {.count = sizeof(file.bytes)};
    memcpy(file.bytes, "MAUDHRTF", 8);
    Put32(file.bytes + 8, 1);
    Put32(file.bytes + 12, 48000);
    Put32(file.bytes + 16, 8);
    Put32(file.bytes + 20, 2);
    Put32(file.bytes + 24, 4);
    PutFloat(file.bytes + 28, 1.0f / 32768.0f);
    Put32(file.bytes + 32, 4);
    Put32(file.bytes + 36, 8);
    memcpy(file.bytes + 44, "Test", 4);
    memcpy(file.bytes + 48, "License.", 8);
    PutFloat(file.bytes + 56, -90.0f);
    Put32(file.bytes + 60, 1);
    PutFloat(file.bytes + 64, 90.0f);
    Put32(file.bytes + 68, 3);
    for (size_t i = 72; i < file.count; ++i)
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
    file.bytes[100] ^= 1;
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a flipped bit the checksum catches");
    file = Craft();
    file.bytes[0] = 'X';
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a wrong magic");
    file = Craft();
    Put32(file.bytes + 8, 2);
    CHECK(LoadCrafted(&file) == maud_errorUnsupported, "another version");
    file = Craft();
    PutFloat(file.bytes + 64, -90.0f);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "elevations that do not rise");
    file = Craft();
    PutFloat(file.bytes + 64, 91.0f);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "an elevation past 90");
    file = Craft();
    Put32(file.bytes + 68, 2);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "azimuths not adding up to the directions");
    file = Craft();
    Put32(file.bytes + 60, 0);
    Put32(file.bytes + 68, 4);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a ring without an azimuth");
    file = Craft();
    file.bytes[45] = 0xC0;
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a name that is not UTF-8");
    file = Craft();
    file.bytes[55] = 0xE2;
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a license cut inside a character");
    file = Craft();
    PutFloat(file.bytes + 28, NAN);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a scale that is not a number");
    file = Craft();
    Put32(file.bytes + 16, 4);
    Seal(&file);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "fewer taps than the format allows");
    file = Craft();
    Put32(file.bytes + 36, 0xFFFFFFF0u);
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
        if ((state & 16u) != 0 && file.count >= 44)
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
    TestFailingAllocator();
    CHECK(s_live == 0, "every block returned");
    return s_failures == 0 ? 0 : 1;
}
