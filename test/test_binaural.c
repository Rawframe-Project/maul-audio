// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Binaural effects. On a small crafted set with whole-sample delays: an
// impulse from a measured direction comes out as that direction's
// responses at their delays; between two directions, as their blend; a
// change of direction fades into exactly what an effect started there
// renders, the latest of several changes winning; the gain ramps; one
// call or many give the same samples; a reset forgets. On the shipped
// SADIE II set: a source on the right reaches the right ear first and
// louder, and the left one the left. Bad calls write nothing, and
// processing never allocates.

// fopen reads the shipped set; the C runtime's warning that it is unsafe
// is about the Annex K alternative, which the family does not use.
#define _CRT_SECURE_NO_WARNINGS

#include "hrtf_core.h"
#include "test_harness.h"

#include "maul-audio/binaural.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// An allocator that counts its calls.
static long s_allocations;
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
    s_allocations += 1;
    return malloc(size);
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

// The crafted set: rings at -90 (one azimuth), 0 (four: ahead, left,
// behind, right) and 90 (one); 8 taps at 48 kHz; delays of 2 samples
// left and 3 right; direction d's ear e responds with tap d + e at
// (1 + d + 8 e) / 64.
enum
{
    DIRECTIONS = 6,
    TAPS = 8,
    CRAFTED_BYTES = 48 + 4 + 8 + 3 * 8 + DIRECTIONS * 4 + DIRECTIONS * 2 * TAPS * 2
};

static unsigned char s_crafted[CRAFTED_BYTES];

static uint32_t Crc32(const unsigned char* bytes, size_t count)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < count; ++i)
    {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void Put(unsigned char* at, uint32_t value, int bytes)
{
    for (int i = 0; i < bytes; ++i)
    {
        at[i] = (unsigned char)(value >> (8 * i));
    }
}

static void PutFloat(unsigned char* at, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    Put(at, bits, 4);
}

static float Expected(int direction, int ear)
{
    return (float)(1 + direction + 8 * ear) / 64.0f;
}

static void Craft(void)
{
    unsigned char* b = s_crafted;
    memcpy(b, "MAUDHRTF", 8);
    Put(b + 8, 2, 4);
    Put(b + 12, 48000, 4);
    Put(b + 16, TAPS, 4);
    Put(b + 20, 3, 4);
    Put(b + 24, DIRECTIONS, 4);
    PutFloat(b + 28, 1.0f / 2048.0f);
    PutFloat(b + 32, 1.5f);
    Put(b + 36, 4, 4);
    Put(b + 40, 8, 4);
    memcpy(b + 48, "Test", 4);
    memcpy(b + 52, "License.", 8);
    float elevations[3] = {-90.0f, 0.0f, 90.0f};
    uint32_t azimuths[3] = {1, 4, 1};
    unsigned char* at = b + 60;
    for (int ring = 0; ring < 3; ++ring, at += 8)
    {
        PutFloat(at, elevations[ring]);
        Put(at + 4, azimuths[ring], 4);
    }
    for (int d = 0; d < DIRECTIONS; ++d, at += 4)
    {
        Put(at, 2 * 256, 2);
        Put(at + 2, 3 * 256, 2);
    }
    for (int d = 0; d < DIRECTIONS; ++d)
    {
        for (int ear = 0; ear < 2; ++ear, at += 2 * TAPS)
        {
            // The tap's value over the scale, 1 / 2048: (1 + d + 8e) * 32.
            Put(at + 2 * (d + ear), (uint32_t)(1 + d + 8 * ear) * 32u, 2);
        }
    }
    Put(b + 44, Crc32(b + 48, CRAFTED_BYTES - 48), 4);
}

static maudHrtf* LoadCrafted(void)
{
    Craft();
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = s_crafted;
    def.byteCount = CRAFTED_BYTES;
    maudHrtf* hrtf = nullptr;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_success, "the crafted set loads");
    return hrtf;
}

static maudBinaural* Create(const maudHrtf* hrtf, uint32_t maxFrames)
{
    maudBinauralDef def = maudDefaultBinauralDef();
    def.hrtf = hrtf;
    def.maxFrames = maxFrames;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudBinaural* effect = nullptr;
    CHECK(maudCreateBinaural(&def, &effect) == maud_success, "an effect");
    return effect;
}

static maudBinauralParams At(float x, float y, float z)
{
    return (maudBinauralParams){.direction = {x, y, z}, .gain = 1.0f};
}

enum
{
    FRAMES = 64
};

static float s_left[4096];
static float s_right[4096];
static float s_left2[4096];
static float s_right2[4096];
static float s_in[4096];

static maudResult Process(maudBinaural* effect, maudBinauralParams params, const float* in,
                          float* left, float* right, uint32_t frames)
{
    float* out[2] = {left, right};
    return maudProcessBinaural(effect, &params, in, out, frames);
}

// An impulse from the left (direction 2), then from between ahead and
// left (directions 1 and 2, half each).
static void TestExact(const maudHrtf* hrtf)
{
    maudBinaural* effect = Create(hrtf, FRAMES);
    memset(s_in, 0, sizeof(s_in));
    s_in[0] = 1.0f;
    CHECK(Process(effect, At(-1.0f, 0.0f, 0.0f), s_in, s_left, s_right, FRAMES) == maud_success,
          "process");
    // Delay 2 + the base sample, then tap d + e.
    bool exact = true;
    for (int n = 0; n < FRAMES; ++n)
    {
        float left = n == 3 + 2 ? Expected(2, 0) : 0.0f;
        float right = n == 4 + 3 ? Expected(2, 1) : 0.0f;
        exact = exact && fabsf(s_left[n] - left) < 1e-5f && fabsf(s_right[n] - right) < 1e-5f;
    }
    CHECK(exact, "a measured direction's responses at their delays");
    maudDestroyBinaural(effect);

    effect = Create(hrtf, FRAMES);
    float half = sqrtf(0.5f);
    CHECK(Process(effect, At(-half, 0.0f, -half), s_in, s_left, s_right, FRAMES) == maud_success,
          "process");
    bool blended = fabsf(s_left[3 + 1] - 0.5f * Expected(1, 0)) < 1e-5f &&
                   fabsf(s_left[3 + 2] - 0.5f * Expected(2, 0)) < 1e-5f &&
                   fabsf(s_right[4 + 1 + 1] - 0.5f * Expected(1, 1)) < 1e-5f &&
                   fabsf(s_right[4 + 2 + 1] - 0.5f * Expected(2, 1)) < 1e-5f;
    CHECK(blended, "between two directions, half of each");
    maudDestroyBinaural(effect);
}

static void Noise(float* samples, uint32_t count, uint32_t seed)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        samples[i] = (float)(int32_t)(seed >> 8) / 8388608.0f - 1.0f;
    }
}

// Renders 1,024 frames in calls of blocks frames, the direction moving to
// change for the calls from the one holding frame at.
static void RenderMoving(maudBinaural* effect, uint32_t blocks, maudBinauralParams before,
                         maudBinauralParams after, uint32_t at, float* left, float* right)
{
    for (uint32_t done = 0; done < 1024; done += blocks)
    {
        maudBinauralParams params = done + blocks > at ? after : before;
        CHECK(Process(effect, params, s_in + done, left + done, right + done, blocks) ==
                  maud_success,
              "process");
    }
}

// After a change, once the fade (128 frames) and the filters' memory
// (8 taps) have passed, a moving effect renders exactly what one that
// was there all along renders.
static void TestFade(const maudHrtf* hrtf)
{
    Noise(s_in, 1024, 7);
    maudBinaural* moving = Create(hrtf, 1024);
    maudBinaural* settled = Create(hrtf, 1024);
    RenderMoving(moving, 64, At(0.0f, 0.0f, -1.0f), At(1.0f, 0.0f, 0.0f), 256, s_left, s_right);
    RenderMoving(settled, 64, At(1.0f, 0.0f, 0.0f), At(1.0f, 0.0f, 0.0f), 0, s_left2, s_right2);
    bool same = true;
    for (int n = 256 + 128 + TAPS; n < 1024; ++n)
    {
        same = same && s_left[n] == s_left2[n] && s_right[n] == s_right2[n];
    }
    CHECK(same, "a faded effect renders as one that started there");
    // Past the first samples, silent in both while the delays fill.
    bool before = true;
    for (int n = 16; n < 256; ++n)
    {
        before = before && s_left[n] != s_left2[n];
    }
    CHECK(before, "and differed before the change");
    // Changes during a fade: the last one wins.
    CHECK(maudResetBinaural(moving) == maud_success && maudResetBinaural(settled) == maud_success,
          "reset");
    for (uint32_t done = 0; done < 1024; done += 32)
    {
        maudBinauralParams params = done < 256   ? At(0.0f, 0.0f, -1.0f)
                                    : done < 288 ? At(-1.0f, 0.0f, 0.0f)
                                    : done < 320 ? At(0.0f, 0.0f, 1.0f)
                                                 : At(0.0f, 1.0f, 0.0f);
        CHECK(Process(moving, params, s_in + done, s_left + done, s_right + done, 32) ==
                  maud_success,
              "process");
    }
    RenderMoving(settled, 1024, At(0.0f, 1.0f, 0.0f), At(0.0f, 1.0f, 0.0f), 0, s_left2, s_right2);
    same = true;
    for (int n = 256 + 2 * 128 + TAPS; n < 1024; ++n)
    {
        same = same && s_left[n] == s_left2[n] && s_right[n] == s_right2[n];
    }
    CHECK(same, "the latest change wins, after the fade under way");
    maudDestroyBinaural(moving);
    maudDestroyBinaural(settled);
}

// One call of 1,000 frames and calls of 1, 7, 100 and 892 give the same
// samples while nothing changes.
static void TestBlocks(const maudHrtf* hrtf)
{
    Noise(s_in, 1000, 11);
    maudBinaural* whole = Create(hrtf, 1024);
    maudBinaural* parts = Create(hrtf, 1024);
    maudBinauralParams params = At(0.3f, 0.2f, -0.9f);
    CHECK(Process(whole, params, s_in, s_left, s_right, 1000) == maud_success, "process");
    uint32_t sizes[4] = {1, 7, 100, 892};
    uint32_t done = 0;
    for (int i = 0; i < 4; ++i)
    {
        CHECK(Process(parts, params, s_in + done, s_left2 + done, s_right2 + done, sizes[i]) ==
                  maud_success,
              "process");
        done += sizes[i];
    }
    CHECK(memcmp(s_left, s_left2, 1000 * sizeof(float)) == 0 &&
              memcmp(s_right, s_right2, 1000 * sizeof(float)) == 0,
          "one call or many");
    // A reset forgets: the parts effect now renders as a fresh one.
    CHECK(maudResetBinaural(parts) == maud_success, "reset");
    CHECK(Process(parts, params, s_in, s_left2, s_right2, 1000) == maud_success, "process");
    CHECK(memcmp(s_left, s_left2, 1000 * sizeof(float)) == 0, "a reset effect starts afresh");
    maudDestroyBinaural(whole);
    maudDestroyBinaural(parts);
}

// The gain moves from the last call's to this one's: from 1 to 0 over a
// call, against the same effect at 1.
static void TestGain(const maudHrtf* hrtf)
{
    Noise(s_in, 256, 3);
    maudBinaural* ramped = Create(hrtf, 256);
    maudBinaural* steady = Create(hrtf, 256);
    maudBinauralParams params = At(0.0f, 0.0f, -1.0f);
    CHECK(Process(ramped, params, s_in, s_left, s_right, 128) == maud_success, "process");
    CHECK(Process(steady, params, s_in, s_left2, s_right2, 128) == maud_success, "process");
    maudBinauralParams fading = params;
    fading.gain = 0.0f;
    CHECK(Process(ramped, fading, s_in + 128, s_left, s_right, 128) == maud_success, "process");
    CHECK(Process(steady, params, s_in + 128, s_left2, s_right2, 128) == maud_success, "process");
    bool linear = true;
    for (int n = 0; n < 128; ++n)
    {
        float share = 1.0f - (float)(n + 1) / 128.0f;
        linear = linear && fabsf(s_left[n] - share * s_left2[n]) < 1e-6f;
    }
    CHECK(linear && s_left[127] == 0.0f, "the gain ramps across the call");
    maudDestroyBinaural(ramped);
    maudDestroyBinaural(steady);
}

static void TestMisuse(const maudHrtf* hrtf)
{
    maudBinaural* effect = Create(hrtf, 64);
    for (int n = 0; n < 64; ++n)
    {
        s_left[n] = 7.0f;
    }
    float* out[2] = {s_left, s_right};
    maudBinauralParams bad = At(NAN, 0.0f, 0.0f);
    CHECK(maudProcessBinaural(effect, &bad, s_in, out, 64) == maud_errorInvalid, "NaN");
    bad = At(0.0f, 0.0f, -1.0f);
    bad.gain = INFINITY;
    CHECK(maudProcessBinaural(effect, &bad, s_in, out, 64) == maud_errorInvalid, "infinite gain");
    maudBinauralParams params = At(0.0f, 0.0f, -1.0f);
    CHECK(maudProcessBinaural(effect, &params, s_in, out, 65) == maud_errorInvalid,
          "more frames than the def allows");
    CHECK(maudProcessBinaural(effect, &params, nullptr, out, 64) == maud_errorInvalid, "no input");
    float* half[2] = {s_left, nullptr};
    CHECK(maudProcessBinaural(effect, &params, s_in, half, 64) == maud_errorInvalid,
          "one channel missing");
    CHECK(maudProcessBinaural(nullptr, &params, s_in, out, 64) == maud_errorInvalid, "no effect");
    CHECK(s_left[0] == 7.0f && s_left[63] == 7.0f, "nothing written by a bad call");
    CHECK(maudProcessBinaural(effect, &params, nullptr, out, 0) == maud_success, "no frames");
    // A zero direction is straight ahead.
    maudBinaural* ahead = Create(hrtf, 64);
    memset(s_in, 0, 64 * sizeof(float));
    s_in[0] = 1.0f;
    CHECK(Process(effect, At(0.0f, 0.0f, 0.0f), s_in, s_left, s_right, 64) == maud_success,
          "process");
    CHECK(Process(ahead, At(0.0f, 0.0f, -2.0f), s_in, s_left2, s_right2, 64) == maud_success,
          "process");
    CHECK(memcmp(s_left, s_left2, 64 * sizeof(float)) == 0, "a zero direction is ahead");
    maudDestroyBinaural(effect);
    maudDestroyBinaural(ahead);

    maudBinauralDef def = maudDefaultBinauralDef();
    maudBinaural* none = (maudBinaural*)&def;
    CHECK(maudCreateBinaural(&def, &none) == maud_errorInvalid && none == nullptr, "no set");
    def.hrtf = hrtf;
    def.maxFrames = 0;
    CHECK(maudCreateBinaural(&def, &none) == maud_errorInvalid, "no frames");
    def.maxFrames = 16385;
    CHECK(maudCreateBinaural(&def, &none) == maud_errorInvalid, "too many frames");
    def.maxFrames = 64;
    def.cookie = 0;
    CHECK(maudCreateBinaural(&def, &none) == maud_errorInvalid, "no cookie");
    def = maudDefaultBinauralDef();
    def.hrtf = hrtf;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    s_failAfter = 0;
    CHECK(maudCreateBinaural(&def, &none) == maud_errorCapacity && none == nullptr,
          "an allocator that fails");
    s_failAfter = -1;
    CHECK(maudResetBinaural(nullptr) == maud_errorInvalid, "reset nothing");
    maudDestroyBinaural(nullptr);
}

// Creating allocates once; processing, fading and resetting never do.
static void TestNoAllocation(const maudHrtf* hrtf)
{
    long before = s_allocations;
    maudBinaural* effect = Create(hrtf, 512);
    CHECK(s_allocations == before + 1, "one block");
    Noise(s_in, 4096, 5);
    for (uint32_t done = 0; done < 4096; done += 512)
    {
        maudBinauralParams params = At(sinf((float)done), 0.1f, cosf((float)done));
        CHECK(Process(effect, params, s_in + done, s_left, s_right, 512) == maud_success,
              "process");
    }
    CHECK(maudResetBinaural(effect) == maud_success, "reset");
    CHECK(s_allocations == before + 1, "processing allocates nothing");
    maudDestroyBinaural(effect);
}

#ifndef __EMSCRIPTEN__
static double Energy(const float* samples, int count)
{
    double sum = 0.0;
    for (int i = 0; i < count; ++i)
    {
        sum += (double)samples[i] * (double)samples[i];
    }
    return sum;
}

// The sample at which a signal first reaches a tenth of its peak.
static int Onset(const float* samples, int count)
{
    float peak = 0.0f;
    for (int i = 0; i < count; ++i)
    {
        peak = fabsf(samples[i]) > peak ? fabsf(samples[i]) : peak;
    }
    for (int i = 0; i < count; ++i)
    {
        if (fabsf(samples[i]) >= 0.1f * peak)
        {
            return i;
        }
    }
    return count;
}

#endif

// The shipped set: a source on the right (+x) reaches the right ear
// first and louder, one on the left the left ear.
static void TestShippedSet(void)
{
#ifdef __EMSCRIPTEN__
    printf("skipped on the web: no file system\n");
#else
    FILE* file = fopen(MAUD_DATA_DIR "/hrtf/sadie2-ku100-48k.maudhrtf", "rb");
    CHECK(file != nullptr, "the shipped set");
    if (file == nullptr)
    {
        return;
    }
    static unsigned char bytes[900000];
    size_t count = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = bytes;
    def.byteCount = count;
    maudHrtf* hrtf = nullptr;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_success, "loads");
    memset(s_in, 0, 512 * sizeof(float));
    s_in[0] = 1.0f;
    float sides[2] = {1.0f, -1.0f};
    for (int side = 0; side < 2; ++side)
    {
        maudBinaural* effect = Create(hrtf, 512);
        CHECK(Process(effect, At(sides[side], 0.0f, 0.0f), s_in, s_left, s_right, 512) ==
                  maud_success,
              "process");
        const float* near = side == 0 ? s_right : s_left;
        const float* far = side == 0 ? s_left : s_right;
        CHECK(Onset(near, 512) + 20 < Onset(far, 512), "the near ear first, by over 20 samples");
        CHECK(Energy(near, 512) > 4.0 * Energy(far, 512), "and louder, by over 6 dB");
        maudDestroyBinaural(effect);
    }
    maudDestroyHrtf(hrtf);
#endif
}

int main(void)
{
    maudHrtf* hrtf = LoadCrafted();
    if (hrtf != nullptr)
    {
        TestExact(hrtf);
        TestFade(hrtf);
        TestBlocks(hrtf);
        TestGain(hrtf);
        TestMisuse(hrtf);
        TestNoAllocation(hrtf);
        maudDestroyHrtf(hrtf);
    }
    TestShippedSet();
    return s_failures == 0 ? 0 : 1;
}
