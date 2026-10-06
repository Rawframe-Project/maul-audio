// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Binaural decoders on the shipped SADIE II set. An impulse encoded from
// every seventh measured direction and decoded comes within the measured
// MagLS error of the set's own response (magnitudes, 100 Hz to 16 kHz;
// research measured 3.9, 4.5 and 5.5 dB RMS at orders 3, 2 and 1 over
// every direction); a source on the left is louder on the left and
// ahead there at low frequencies; one
// call or many decode the same samples; decoding never allocates, and
// the set may go once the decoder exists; bad calls write nothing.

#define _CRT_SECURE_NO_WARNINGS

#include "hrtf_core.h"
#include "test_harness.h"

#include "maul-audio/ambisonics.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__

int main(void)
{
    printf("skipped on the web: no file system\n");
    return 0;
}

#else

#define PI 3.14159265358979323846

static long s_live;
static long s_allocations;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_live += 1;
    s_allocations += 1;
    return malloc(size);
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    s_live -= 1;
    free(memory);
}

static maudBinauralDecoder* Create(const maudHrtf* hrtf, uint32_t order, uint32_t maxFrames)
{
    maudBinauralDecoderDef def = maudDefaultBinauralDecoderDef();
    def.hrtf = hrtf;
    def.order = order;
    def.maxFrames = maxFrames;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudBinauralDecoder* decoder = nullptr;
    CHECK(maudCreateBinauralDecoder(&def, &decoder) == maud_success, "a decoder");
    return decoder;
}

// The magnitude in dB of count samples at a frequency, by a direct DFT.
static double Level(const float* samples, int count, double hz)
{
    double re = 0.0;
    double im = 0.0;
    for (int n = 0; n < count; ++n)
    {
        double angle = -2.0 * PI * hz * n / 48000.0;
        re += (double)samples[n] * cos(angle);
        im += (double)samples[n] * sin(angle);
    }
    return 10.0 * log10(re * re + im * im + 1e-24);
}

// A measured direction of the set as a listener-frame vector.
static maudVector3 Direction(const maudHrtf* hrtf, uint32_t index)
{
    uint32_t ring = 0;
    while (ring + 1 < hrtf->ringCount && hrtf->firstDirection[ring + 1] <= index)
    {
        ++ring;
    }
    double azimuth =
        2.0 * PI * (double)(index - hrtf->firstDirection[ring]) / (double)hrtf->azimuths[ring];
    double elevation = (double)hrtf->elevations[ring] * PI / 180.0;
    return (maudVector3){(float)(-cos(elevation) * sin(azimuth)), (float)sin(elevation),
                         (float)(-cos(elevation) * cos(azimuth))};
}

enum
{
    FRAMES = 256
};

static float s_bed[16][FRAMES];
static float s_left[FRAMES];
static float s_right[FRAMES];

// Decodes an impulse from a direction into s_left and s_right.
static void DecodeImpulse(maudBinauralDecoder* decoder, uint32_t order, maudVector3 direction)
{
    float gains[16];
    CHECK(maudGetAmbisonicGains(order, direction, gains) == maud_success, "gains");
    const float* bed[16];
    for (uint32_t c = 0; c < 16; ++c)
    {
        memset(s_bed[c], 0, sizeof(s_bed[c]));
        s_bed[c][0] = gains[c];
        bed[c] = s_bed[c];
    }
    float* out[2] = {s_left, s_right};
    CHECK(maudResetBinauralDecoder(decoder) == maud_success, "reset");
    CHECK(maudDecodeBinaural(decoder, bed, out, FRAMES) == maud_success, "decode");
}

static void TestAccuracy(const maudHrtf* hrtf)
{
    const double limits[4] = {0.0, 6.2, 5.2, 4.5};
    for (uint32_t order = 1; order <= 3; ++order)
    {
        maudBinauralDecoder* decoder = Create(hrtf, order, FRAMES);
        double sum = 0.0;
        int count = 0;
        for (uint32_t d = 0; d < hrtf->directionCount; d += 7)
        {
            DecodeImpulse(decoder, order, Direction(hrtf, d));
            for (uint32_t ear = 0; ear < 2; ++ear)
            {
                const float* response = hrtf->responses + (2u * (size_t)d + ear) * hrtf->taps;
                const float* decoded = ear == 0 ? s_left : s_right;
                for (double hz = 187.5; hz <= 16000.0; hz += 187.5)
                {
                    double error =
                        Level(decoded, FRAMES, hz) - Level(response, (int)hrtf->taps, hz);
                    sum += error * error;
                    count += 1;
                }
            }
        }
        double rms = sqrt(sum / count);
        printf("order %u: %.2f dB RMS against the set\n", order, rms);
        CHECK(rms < limits[order], "within the measured MagLS error");
        maudDestroyBinauralDecoder(decoder);
    }
}

static double Energy(const float* samples, int count)
{
    double sum = 0.0;
    for (int n = 0; n < count; ++n)
    {
        sum += (double)samples[n] * (double)samples[n];
    }
    return sum;
}

// The phase of count samples at a frequency, by a direct DFT.
static double Phase(const float* samples, int count, double hz)
{
    double re = 0.0;
    double im = 0.0;
    for (int n = 0; n < count; ++n)
    {
        double angle = -2.0 * PI * hz * n / 48000.0;
        re += (double)samples[n] * cos(angle);
        im += (double)samples[n] * sin(angle);
    }
    return atan2(im, re);
}

// A source on the left: the left ear louder, and ahead in phase at
// 500 Hz as the set's own responses and delays are.
// MagLS keeps the time difference below its 1.5 kHz cutoff only, so the
// broadband onsets of the two ears are alike and not compared.
static void TestEars(const maudHrtf* hrtf)
{
    maudBinauralDecoder* decoder = Create(hrtf, 3, FRAMES);
    DecodeImpulse(decoder, 3, (maudVector3){-1.0f, 0.0f, 0.0f});
    CHECK(Energy(s_left, FRAMES) > 4.0 * Energy(s_right, FRAMES), "the left ear louder");
    double lead = Phase(s_left, FRAMES, 500.0) - Phase(s_right, FRAMES, 500.0);
    // The set's own lead there: the measured direction straight left (a
    // quarter round the horizontal ring), its responses and delays.
    uint32_t ring = 0;
    while (hrtf->elevations[ring] < 0.0f)
    {
        ++ring;
    }
    uint32_t d = hrtf->firstDirection[ring] + hrtf->azimuths[ring] / 4;
    const float* left = hrtf->responses + 2u * (size_t)d * hrtf->taps;
    const float* right = left + hrtf->taps;
    double measured =
        Phase(left, (int)hrtf->taps, 500.0) - Phase(right, (int)hrtf->taps, 500.0) +
        2.0 * PI * 500.0 * (double)(hrtf->delays[2u * d + 1] - hrtf->delays[2u * d]) / 48000.0;
    double error = fmod(lead - measured + 3.0 * PI, 2.0 * PI) - PI;
    printf("left ear's lead at 500 Hz: %.2f rad, the set's %.2f\n", lead, measured);
    CHECK(fabs(error) < 0.25, "ahead in phase at low frequencies as the set is");
    maudDestroyBinauralDecoder(decoder);
}

static void Noise(float* samples, int count, uint32_t seed)
{
    for (int i = 0; i < count; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        samples[i] = (float)(int32_t)(seed >> 8) / 8388608.0f - 1.0f;
    }
}

// One call of 256 frames against calls of 1, 100 and 155, and no
// allocation while decoding.
static void TestBlocks(const maudHrtf* hrtf)
{
    maudBinauralDecoder* whole = Create(hrtf, 2, FRAMES);
    maudBinauralDecoder* parts = Create(hrtf, 2, FRAMES);
    for (uint32_t c = 0; c < 9; ++c)
    {
        Noise(s_bed[c], FRAMES, 3 + c);
    }
    static float left[FRAMES];
    static float right[FRAMES];
    const float* bed[9];
    for (uint32_t c = 0; c < 9; ++c)
    {
        bed[c] = s_bed[c];
    }
    float* out[2] = {s_left, s_right};
    long before = s_allocations;
    CHECK(maudDecodeBinaural(whole, bed, out, FRAMES) == maud_success, "decode");
    uint32_t sizes[3] = {1, 100, 155};
    uint32_t done = 0;
    for (int i = 0; i < 3; ++i)
    {
        const float* at[9];
        for (uint32_t c = 0; c < 9; ++c)
        {
            at[c] = s_bed[c] + done;
        }
        float* to[2] = {left + done, right + done};
        CHECK(maudDecodeBinaural(parts, at, to, sizes[i]) == maud_success, "decode");
        done += sizes[i];
    }
    CHECK(memcmp(s_left, left, sizeof(left)) == 0 && memcmp(s_right, right, sizeof(right)) == 0,
          "one call or many");
    CHECK(s_allocations == before, "decoding allocates nothing");
    maudDestroyBinauralDecoder(whole);
    maudDestroyBinauralDecoder(parts);
}

static void TestMisuse(const maudHrtf* hrtf)
{
    maudBinauralDecoderDef def = maudDefaultBinauralDecoderDef();
    maudBinauralDecoder* none = (maudBinauralDecoder*)&def;
    CHECK(maudCreateBinauralDecoder(&def, &none) == maud_errorInvalid && none == nullptr, "no set");
    def.hrtf = hrtf;
    def.order = 4;
    CHECK(maudCreateBinauralDecoder(&def, &none) == maud_errorInvalid, "order 4");
    def.order = 0;
    CHECK(maudCreateBinauralDecoder(&def, &none) == maud_errorInvalid, "order 0");
    def.order = 1;
    def.maxFrames = 0;
    CHECK(maudCreateBinauralDecoder(&def, &none) == maud_errorInvalid, "no frames");
    def.maxFrames = 64;
    def.cookie = 0;
    CHECK(maudCreateBinauralDecoder(&def, &none) == maud_errorInvalid, "no cookie");
    maudBinauralDecoder* decoder = Create(hrtf, 1, 64);
    for (int n = 0; n < 64; ++n)
    {
        s_left[n] = 7.0f;
    }
    const float* bed[4] = {s_bed[0], s_bed[1], s_bed[2], nullptr};
    float* out[2] = {s_left, s_right};
    CHECK(maudDecodeBinaural(decoder, bed, out, 64) == maud_errorInvalid, "a missing channel");
    bed[3] = s_bed[3];
    CHECK(maudDecodeBinaural(decoder, bed, out, 65) == maud_errorInvalid, "too many frames");
    CHECK(s_left[0] == 7.0f, "nothing written by a bad call");
    CHECK(maudDecodeBinaural(decoder, nullptr, out, 0) == maud_success, "no frames");
    CHECK(maudResetBinauralDecoder(nullptr) == maud_errorInvalid, "reset nothing");
    maudDestroyBinauralDecoder(decoder);
    maudDestroyBinauralDecoder(nullptr);
}

int main(void)
{
    FILE* file = fopen(MAUD_DATA_DIR "/hrtf/sadie2-ku100-48k.maudhrtf", "rb");
    CHECK(file != nullptr, "the shipped set");
    if (file == nullptr)
    {
        return 1;
    }
    static unsigned char bytes[900000];
    size_t count = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = bytes;
    def.byteCount = count;
    maudHrtf* hrtf = nullptr;
    CHECK(maudLoadHrtf(&def, &hrtf) == maud_success, "loads");
    if (hrtf == nullptr)
    {
        return 1;
    }
    TestAccuracy(hrtf);
    TestEars(hrtf);
    TestBlocks(hrtf);
    TestMisuse(hrtf);
    // The decoder keeps its own filters: the set may go first.
    long live = s_live;
    maudBinauralDecoder* decoder = Create(hrtf, 3, FRAMES);
    CHECK(s_live == live + 1, "creation keeps one block, its work given back");
    maudDestroyHrtf(hrtf);
    DecodeImpulse(decoder, 3, (maudVector3){0.0f, 0.0f, -1.0f});
    CHECK(Energy(s_left, FRAMES) > 0.0, "decoding after the set is gone");
    maudDestroyBinauralDecoder(decoder);
    CHECK(s_live == live, "nothing left");
    return s_failures == 0 ? 0 : 1;
}

#endif
