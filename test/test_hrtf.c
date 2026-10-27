// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// HRTF sets: the shipped SADIE II KU100 file loads with its name and
// license, its interaural delay on the right side, and resampled to 44.1
// and 96 kHz with its response at 1 kHz kept (the resampled responses
// start 24 input samples early, a constant latency); crafted files
// with a valid checksum are refused for each rule they break, a field
// only a later version writes is refused as such and an absent name and
// license read as empty, and one with the most taps, 1024, is taken; one
// whose resampled responses pass a 32-bit address space is refused as
// capacity without a smaller block; a seeded sweep of mutations
// loads exactly the well-formed ones and never crashes; an allocator that fails is a capacity error
// with nothing leaked.

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
    CHECK(bytes != nullptr && count == 853944, "the shipped file");
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
    def.maxTaps = UINT32_MAX;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_success, "loaded under the largest limit");
    uint32_t resampled = hrtf != nullptr ? hrtf->taps : 0;
    maudDestroyHrtf(hrtf);
    def.maxTaps = resampled;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_success, "and under a limit of its taps exactly");
    maudDestroyHrtf(hrtf);
    def.maxTaps = resampled - 1;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_errorCapacity && hrtf == nullptr,
          "refused one tap below");
    maudDestroyHrtf(nullptr);
    free(bytes);
}

// The parts of a file, in the order a writer puts them, and their tags.
enum
{
    PART_HEAD,
    PART_NAME,
    PART_LICENSE,
    PART_RINGS,
    PART_DELAYS,
    PART_TAPS,
    PART_PARTS
};

static const char* const s_tags[PART_PARTS] = {"head", "name", "lics", "ring", "dlay", "taps"};

// The largest request BoundedAlloc saw.
static size_t s_largest;

// Refuses a request past a gigabyte and keeps the largest, so that a set
// past the address space is never made where size_t has 64 bits.
static void* BoundedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_largest = size > s_largest ? size : s_largest;
    if (size > ((size_t)1 << 30))
    {
        return nullptr;
    }
    s_live++;
    return malloc(size);
}

// What a crafted file holds: two rings (-90 with one azimuth, 90 with
// the rest) or one at the horizon, delays and taps from a byte pattern.
// A NULL name or license leaves its field out.
typedef struct Spec
{
    uint32_t rate;
    uint32_t taps;
    uint32_t directions;
    bool horizon;
    float distance;
    const char* name;
    size_t nameBytes;
    const char* license;
    size_t licenseBytes;
} Spec;

static Spec SmallSpec(void)
{
    return (Spec){.rate = 48000,
                  .taps = 8,
                  .directions = 4,
                  .distance = 1.5f,
                  .name = "Test",
                  .nameBytes = 4,
                  .license = "License.",
                  .licenseBytes = 8};
}

static void Put32(unsigned char* at, uint32_t value)
{
    for (int i = 0; i < 4; ++i)
    {
        at[i] = (unsigned char)(value >> (8 * i));
    }
}

static uint32_t Get32(const unsigned char* at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static void PutFloat(unsigned char* at, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    Put32(at, bits);
}

// A field of length bytes: frame, payload, and zero bytes to a multiple
// of 8.
static size_t FieldBytes(size_t length)
{
    return 8 + (length + 7) / 8 * 8;
}

static size_t SizeOf(const Spec* spec)
{
    size_t rings = spec->horizon ? 1 : 2;
    return 16 + FieldBytes(24) + (spec->name != nullptr ? FieldBytes(spec->nameBytes) : 0) +
           (spec->license != nullptr ? FieldBytes(spec->licenseBytes) : 0) + FieldBytes(8 * rings) +
           FieldBytes((size_t)4 * spec->directions) +
           FieldBytes((size_t)4 * spec->directions * spec->taps);
}

// Puts a field at bytes + *count, frame and padding, and moves *count
// past it; where its payload starts.
static size_t PutField(unsigned char* bytes, size_t* count, const char* tag, size_t length)
{
    memcpy(bytes + *count, tag, 4);
    Put32(bytes + *count + 4, (uint32_t)length);
    size_t at = *count + 8;
    memset(bytes + at, 0, FieldBytes(length) - 8);
    *count += FieldBytes(length);
    return at;
}

static void Seal(unsigned char* bytes, size_t count)
{
    Put32(bytes + 12, maudCrc32(bytes + 16, count - 16));
}

// Writes the file spec describes into bytes, SizeOf(spec) of them, and
// seals it; at gets each payload's offset, 0 for a part left out.
static size_t Write(const Spec* spec, unsigned char* bytes, size_t at[PART_PARTS])
{
    memcpy(bytes, "MAUDHRTF", 8);
    Put32(bytes + 8, 3);
    size_t count = 16;
    for (int part = 0; part < PART_PARTS; ++part)
    {
        at[part] = 0;
    }
    at[PART_HEAD] = PutField(bytes, &count, s_tags[PART_HEAD], 24);
    Put32(bytes + at[PART_HEAD], spec->rate);
    Put32(bytes + at[PART_HEAD] + 4, spec->taps);
    Put32(bytes + at[PART_HEAD] + 8, spec->horizon ? 1 : 2);
    Put32(bytes + at[PART_HEAD] + 12, spec->directions);
    PutFloat(bytes + at[PART_HEAD] + 16, 1.0f / 32768.0f);
    PutFloat(bytes + at[PART_HEAD] + 20, spec->distance);
    if (spec->name != nullptr)
    {
        at[PART_NAME] = PutField(bytes, &count, s_tags[PART_NAME], spec->nameBytes);
        memcpy(bytes + at[PART_NAME], spec->name, spec->nameBytes);
    }
    if (spec->license != nullptr)
    {
        at[PART_LICENSE] = PutField(bytes, &count, s_tags[PART_LICENSE], spec->licenseBytes);
        memcpy(bytes + at[PART_LICENSE], spec->license, spec->licenseBytes);
    }
    at[PART_RINGS] = PutField(bytes, &count, s_tags[PART_RINGS], spec->horizon ? 8 : 16);
    if (spec->horizon)
    {
        PutFloat(bytes + at[PART_RINGS], 0.0f);
        Put32(bytes + at[PART_RINGS] + 4, spec->directions);
    }
    else
    {
        PutFloat(bytes + at[PART_RINGS], -90.0f);
        Put32(bytes + at[PART_RINGS] + 4, 1);
        PutFloat(bytes + at[PART_RINGS] + 8, 90.0f);
        Put32(bytes + at[PART_RINGS] + 12, spec->directions - 1);
    }
    at[PART_DELAYS] = PutField(bytes, &count, s_tags[PART_DELAYS], (size_t)4 * spec->directions);
    at[PART_TAPS] =
        PutField(bytes, &count, s_tags[PART_TAPS], (size_t)4 * spec->directions * spec->taps);
    size_t patterned[2][2] = {{at[PART_DELAYS], (size_t)4 * spec->directions},
                              {at[PART_TAPS], (size_t)4 * spec->directions * spec->taps}};
    for (int part = 0; part < 2; ++part)
    {
        for (size_t i = patterned[part][0]; i < patterned[part][0] + patterned[part][1]; ++i)
        {
            bytes[i] = (unsigned char)(i * 7u);
        }
    }
    Seal(bytes, count);
    return count;
}

// The small file, and room for one byte past it.
typedef struct Crafted
{
    unsigned char bytes[400];
    size_t count;
    size_t at[PART_PARTS];
} Crafted;

static Crafted CraftSpec(const Spec* spec)
{
    Crafted file = {0};
    file.count = Write(spec, file.bytes, file.at);
    return file;
}

static Crafted Craft(void)
{
    Spec spec = SmallSpec();
    return CraftSpec(&spec);
}

static maudResult LoadCrafted(const Crafted* file)
{
    maudHrtf* hrtf = nullptr;
    maudResult result = Load(file->bytes, file->count, 0, &hrtf);
    maudDestroyHrtf(hrtf);
    return result;
}

// The file spec describes, on the heap; the caller frees it.
static unsigned char* WriteSpec(const Spec* spec, size_t* countOut)
{
    unsigned char* bytes = calloc(SizeOf(spec), 1);
    if (bytes != nullptr)
    {
        size_t at[PART_PARTS];
        *countOut = Write(spec, bytes, at);
    }
    return bytes;
}

// The most taps a response may have, 1024, are taken.
static void TestMostTaps(void)
{
    Spec spec = SmallSpec();
    spec.taps = 1024;
    size_t count = 0;
    unsigned char* bytes = WriteSpec(&spec, &count);
    maudHrtf* hrtf = nullptr;
    CHECK(bytes != nullptr && Load(bytes, count, 0, &hrtf) == maud_success && hrtf != nullptr &&
              hrtf->taps == 1024,
          "1024 taps");
    maudDestroyHrtf(hrtf);
    free(bytes);
}

// A set whose responses pass a 32-bit address space once resampled:
// 43,000 directions of 1024 taps at 8 kHz, which at 384 kHz have 50,304
// taps each, about 17 GB of responses. Refused as capacity, and never
// made over a block smaller than its responses.
static void TestResampledPastAddressSpace(void)
{
    Spec spec = {.rate = 8000,
                 .taps = 1024,
                 .directions = 43000,
                 .horizon = true,
                 .distance = 1.5f,
                 .name = "T",
                 .nameBytes = 1,
                 .license = "L",
                 .licenseBytes = 1};
    size_t count = 0;
    unsigned char* bytes = WriteSpec(&spec, &count);
    CHECK(bytes != nullptr, "the set made");
    if (bytes == nullptr)
    {
        return;
    }
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = bytes;
    def.byteCount = count;
    def.sampleRate = 384000;
    def.maxTaps = UINT32_MAX;
    def.allocator = (maudAllocator){BoundedAlloc, CountedFree, nullptr};
    s_largest = 0;
    maudHrtf* hrtf = (maudHrtf*)&spec;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_errorCapacity && hrtf == nullptr,
          "refused as capacity");
    uint64_t responses = 2u * (uint64_t)spec.directions * 50304u * sizeof(float);
    CHECK(s_largest == 0 || s_largest >= responses, "no block smaller than the responses");
    CHECK(s_live == 0, "nothing kept");
    free(bytes);
}

// The small file with one more field after the rest, sealed.
static Crafted CraftWithField(const char* tag, size_t length)
{
    Crafted file = Craft();
    PutField(file.bytes, &file.count, tag, length);
    Seal(file.bytes, file.count);
    return file;
}

// The small file without a part: the parts are written again without
// it, sealed.
static Crafted CraftWithout(int part)
{
    Crafted file = Craft();
    Crafted without = {0};
    memcpy(without.bytes, file.bytes, 16);
    without.count = 16;
    for (int p = 0; p < PART_PARTS; ++p)
    {
        if (p == part || file.at[p] == 0)
        {
            continue;
        }
        size_t length = Get32(file.bytes + file.at[p] - 4);
        without.at[p] = PutField(without.bytes, &without.count, s_tags[p], length);
        memcpy(without.bytes + without.at[p], file.bytes + file.at[p], length);
    }
    Seal(without.bytes, without.count);
    return without;
}

// Fields: one a later version writes is refused as such, an absent name
// and license read as empty, a part with no default is required, and the
// framing is checked: a tag twice, padding that is not zero.
static void TestFields(void)
{
    Crafted file = CraftWithField("zzzz", 4);
    CHECK(LoadCrafted(&file) == maud_errorVersion, "a field only a later version writes");
    file = CraftWithField("name", 4);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a field written twice");
    Spec spec = SmallSpec();
    spec.name = nullptr;
    spec.license = nullptr;
    file = CraftSpec(&spec);
    maudHrtf* hrtf = nullptr;
    maudHrtfInfo info = {0};
    CHECK(Load(file.bytes, file.count, 0, &hrtf) == maud_success &&
              maudGetHrtfInfo(hrtf, &info) == maud_success && info.nameLength == 0 &&
              info.licenseLength == 0,
          "no name and no license field: both empty, their default");
    maudDestroyHrtf(hrtf);
    const int required[4] = {PART_HEAD, PART_RINGS, PART_DELAYS, PART_TAPS};
    for (int i = 0; i < 4; ++i)
    {
        file = CraftWithout(required[i]);
        CHECK(LoadCrafted(&file) == maud_errorInvalid, "a part with no default left out");
    }
    file = Craft();
    file.bytes[file.at[PART_NAME] + 4] = 1;
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "padding that is not zero");
    file = Craft();
    Put32(file.bytes + file.at[PART_HEAD] - 4, 20);
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a head of another length");
    file = Craft();
    Put32(file.bytes + file.at[PART_LICENSE] - 4, 0xFFFFFFF0u);
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a field longer than the file");
}

static void TestCraftedFiles(void)
{
    Crafted file = Craft();
    CHECK(LoadCrafted(&file) == maud_success, "a well-formed small file");
    file = Craft();
    file.count--;
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "one byte short");
    file = Craft();
    file.count++;
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "one byte long");
    file = Craft();
    file.bytes[file.at[PART_TAPS] + 20] ^= 1;
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a flipped bit the checksum catches");
    file = Craft();
    file.bytes[0] = 'X';
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a wrong magic");
    file = Craft();
    Put32(file.bytes + 8, 2);
    CHECK(LoadCrafted(&file) == maud_errorVersion, "version 2, the fixed layout before fields");
    file = Craft();
    Put32(file.bytes + 8, 4);
    CHECK(LoadCrafted(&file) == maud_errorVersion, "a later version");
    file = Craft();
    PutFloat(file.bytes + file.at[PART_RINGS] + 8, -90.0f);
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "elevations that do not rise");
    file = Craft();
    PutFloat(file.bytes + file.at[PART_RINGS] + 8, 91.0f);
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "an elevation past 90");
    file = Craft();
    Put32(file.bytes + file.at[PART_RINGS] + 12, 2);
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "azimuths not adding up to the directions");
    file = Craft();
    Put32(file.bytes + file.at[PART_RINGS] + 12, 4);
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "nor adding up to more");
    file = Craft();
    Put32(file.bytes + file.at[PART_RINGS] + 4, 0);
    Put32(file.bytes + file.at[PART_RINGS] + 12, 4);
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a ring without an azimuth");
    file = Craft();
    file.bytes[file.at[PART_NAME] + 1] = 0xC0;
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a name that is not UTF-8");
    // Well-shaped sequences whose values UTF-8 excludes, in the name.
    const char* excluded[3][2] = {{"\xC1\xBF", "an overlong character"},
                                  {"\xED\xA0\x80", "a surrogate"},
                                  {"\xF4\x90\x80\x80", "a character past U+10FFFF"}};
    for (int i = 0; i < 3; ++i)
    {
        file = Craft();
        memcpy(file.bytes + file.at[PART_NAME], excluded[i][0], strlen(excluded[i][0]));
        Seal(file.bytes, file.count);
        CHECK(LoadCrafted(&file) == maud_errorInvalid, excluded[i][1]);
    }
    file = Craft();
    file.bytes[file.at[PART_LICENSE] + 7] = 0xE2;
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a license cut inside a character");
    file = Craft();
    PutFloat(file.bytes + file.at[PART_HEAD] + 16, NAN);
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a scale that is not a number");
    Spec fewer = SmallSpec();
    fewer.taps = 4;
    file = CraftSpec(&fewer);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "fewer taps than the format allows");
    file = Craft();
    Put32(file.bytes + file.at[PART_HEAD] + 4, 4);
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "a taps field longer than the head says");
    file = Craft();
    PutFloat(file.bytes + file.at[PART_HEAD] + 16, INFINITY);
    Seal(file.bytes, file.count);
    CHECK(LoadCrafted(&file) == maud_errorInvalid, "an infinite scale");
    // Distances just outside the bounds, and not a number.
    float distances[3] = {0.049f, 100.5f, NAN};
    for (int i = 0; i < 3; ++i)
    {
        file = Craft();
        PutFloat(file.bytes + file.at[PART_HEAD] + 20, distances[i]);
        Seal(file.bytes, file.count);
        CHECK(LoadCrafted(&file) == maud_errorInvalid, "a measurement distance out of bounds");
    }
    maudHrtf* hrtf = (maudHrtf*)&file;
    maudHrtfDef def = maudDefaultHrtfDef();
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_errorInvalid && hrtf == nullptr, "no bytes");
    def.bytes = file.bytes;
    def.cookie = 0;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_errorInvalid, "no cookie");
    CHECK(maudLoadHrtf(nullptr, &hrtf) == maud_errorInvalid, "no def");
    CHECK(maudGetHrtfInfo(nullptr, &(maudHrtfInfo){0}) == maud_errorInvalid, "no set");
}

// A file of one ring at the horizon with directions azimuths, 8 taps, a
// one-byte license, and the given rate, distance and name, loaded with a
// def's limit of maxDirections.
static maudResult LoadBuilt(uint32_t rate, uint32_t directions, float distance, const char* name,
                            size_t nameBytes, uint32_t maxDirections)
{
    Spec spec = {.rate = rate,
                 .taps = 8,
                 .directions = directions,
                 .horizon = true,
                 .distance = distance,
                 .name = name,
                 .nameBytes = nameBytes,
                 .license = "L",
                 .licenseBytes = 1};
    size_t count = 0;
    unsigned char* bytes = WriteSpec(&spec, &count);
    if (bytes == nullptr)
    {
        return maud_errorCapacity;
    }
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = bytes;
    def.byteCount = count;
    def.maxDirections = maxDirections;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudHrtf* hrtf = nullptr;
    maudResult result = maudLoadHrtf(&def, &hrtf);
    maudDestroyHrtf(hrtf);
    free(bytes);
    return result;
}

// What the format allows is taken, to its ends: characters of every
// length in the name, the highest rate, distance, name length and
// direction count, and a def's limit met exactly. Text that is not
// UTF-8 in two more ways is refused.
static void TestBounds(void)
{
    const char text[] = "T\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80\xF4\x8F\xBF\xBF";
    CHECK(LoadBuilt(48000, 4, 1.5f, text, sizeof(text) - 1, 65536) == maud_success,
          "characters of two, three and four bytes, U+10FFFF the last");
    CHECK(LoadBuilt(48000, 4, 1.5f, "T\x80", 2, 65536) == maud_errorInvalid,
          "a continuation byte with no lead");
    CHECK(LoadBuilt(48000, 4, 1.5f, "T\xE2\x82\x41", 4, 65536) == maud_errorInvalid,
          "a character whose last byte does not continue it");
    CHECK(LoadBuilt(384000, 4, 100.0f, "T", 1, 65536) == maud_success,
          "the highest rate and distance");
    char name[256];
    memset(name, 'n', sizeof(name));
    CHECK(LoadBuilt(48000, 4, 1.5f, name, 256, 65536) == maud_success, "the longest name");
    char longer[257];
    memset(longer, 'n', sizeof(longer));
    CHECK(LoadBuilt(48000, 4, 1.5f, longer, 257, 65536) == maud_errorInvalid,
          "a name past 256 bytes");
    CHECK(LoadBuilt(48000, 1, 1.5f, "T", 1, 1) == maud_success, "one direction, the def's limit");
    CHECK(LoadBuilt(48000, 4, 1.5f, "T", 1, 4) == maud_success, "four, the def's limit");
    CHECK(LoadBuilt(48000, 5, 1.5f, "T", 1, 4) == maud_errorCapacity, "five, past it");
    CHECK(LoadBuilt(48000, 65536, 1.5f, "T", 1, 65536) == maud_success,
          "the most directions, in one ring");
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
        if ((state & 16u) != 0 && file.count >= 16)
        {
            Seal(file.bytes, file.count);
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
        static double weights[(TAPS + 64) * 80];
        CHECK(maudResampleWidth(48000, 44100) <= 80, "room for the weights");
        maudResampleWeights(TAPS, 48000, taps, 44100, weights);
        maudResampleResponse(in, TAPS, 48000, out, taps, 44100, weights);
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
    TestFields();
    TestMostTaps();
    TestResampledPastAddressSpace();
    TestMutations();
    TestBounds();
    TestResampler();
    TestFailingAllocator();
    CHECK(s_live == 0, "every block returned");
    return s_failures == 0 ? 0 : 1;
}
