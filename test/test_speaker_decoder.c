// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Speaker decoders: over sources covering the sphere, the energy vector
// of an encoded and decoded source points near it and is as long as the
// research measured (7.1.4 above -12 degrees: 7.7, 6.4 and 6.2 degrees
// mean error at orders 3, 2 and 1; 5.1 on the horizontal: 7.1 degrees at
// order 1); energy averages 1 and varies within the measured 3.2 dB; the
// LFE stays silent; a mirrored source gets mirrored speakers; decoding
// is the matrix, written over the output; bad calls write nothing.

#include "test_harness.h"

#include "maul-audio/ambisonics.h"

#include <math.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

static long s_live;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_live += 1;
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

static maudSpeakerDecoder* Create(maudChannelLayout layout, uint32_t order)
{
    maudSpeakerDecoderDef def = maudDefaultSpeakerDecoderDef();
    def.layout = layout;
    def.order = order;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudSpeakerDecoder* decoder = nullptr;
    CHECK(maudCreateSpeakerDecoder(&def, &decoder) == maud_success, "a decoder");
    return decoder;
}

static maudVector3 Towards(double azimuth, double elevation)
{
    double a = azimuth * PI / 180.0;
    double e = elevation * PI / 180.0;
    return (maudVector3){(float)(-cos(e) * sin(a)), (float)sin(e), (float)(-cos(e) * cos(a))};
}

static uint32_t s_seed = 7;

static double Random(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (double)(s_seed >> 8) / 16777216.0;
}

// A source's speaker signals: its gains in the bed, decoded one frame.
static void Decode(const maudSpeakerDecoder* decoder, uint32_t order, maudVector3 direction,
                   float* speakers)
{
    float gains[16];
    CHECK(maudGetAmbisonicGains(order, direction, gains) == maud_success, "gains");
    const float* bed[16];
    float* out[12];
    for (int c = 0; c < 16; ++c)
    {
        bed[c] = &gains[c];
    }
    for (int s = 0; s < 12; ++s)
    {
        out[s] = &speakers[s];
    }
    CHECK(maudDecodeToSpeakers(decoder, bed, out, 1) == maud_success, "decode");
}

typedef struct Measure
{
    double error;
    double length;
    double energy;
    double low;
    double high;
} Measure;

static Measure Measured(maudChannelLayout layout, uint32_t order, bool horizontal)
{
    maudSpeakerDecoder* decoder = Create(layout, order);
    uint32_t speakers = maudGetLayoutChannelCount(layout);
    Measure m = {0.0, 0.0, 0.0, 1e9, -1e9};
    int count = 0;
    for (int trial = 0; trial < 2000; ++trial)
    {
        double azimuth = 360.0 * Random();
        double elevation = horizontal ? 0.0 : asin(-0.2 + 1.2 * Random()) * 180.0 / PI;
        maudVector3 u = Towards(azimuth, elevation);
        float out[12];
        Decode(decoder, order, u, out);
        double energy = 0.0;
        double v[3] = {0.0, 0.0, 0.0};
        for (uint32_t s = 0; s < speakers; ++s)
        {
            maudSpeakerPosition at = maudGetLayoutSpeakerPosition(layout, s);
            maudVector3 p = Towards((double)at.azimuthDegrees, (double)at.elevationDegrees);
            double e = (double)out[s] * (double)out[s];
            energy += e;
            v[0] += e * (double)p.x;
            v[1] += e * (double)p.y;
            v[2] += e * (double)p.z;
        }
        double length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        double dot = (v[0] * (double)u.x + v[1] * (double)u.y + v[2] * (double)u.z) / length;
        m.error += acos(fmin(dot, 1.0)) * 180.0 / PI;
        m.length += length / energy;
        m.energy += energy;
        double level = 10.0 * log10(energy);
        m.low = fmin(m.low, level);
        m.high = fmax(m.high, level);
        count += 1;
    }
    m.error /= count;
    m.length /= count;
    m.energy /= count;
    maudDestroySpeakerDecoder(decoder);
    return m;
}

static void TestLocalization(void)
{
    struct
    {
        maudChannelLayout layout;
        uint32_t order;
        bool horizontal;
        double error;
        double length;
    } cases[] = {
        {maud_layout7Point1Point4, 3, false, 7.7, 0.81},
        {maud_layout7Point1Point4, 2, false, 6.4, 0.74},
        {maud_layout7Point1Point4, 1, false, 6.2, 0.58},
        {maud_layout5Point1, 1, true, 7.1, 0.55},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        Measure m = Measured(cases[i].layout, cases[i].order, cases[i].horizontal);
        printf("layout %u order %u: rE %.1f degrees off, length %.2f; energy %.2f dB mean, "
               "%.1f dB range\n",
               (unsigned)cases[i].layout, cases[i].order, m.error, m.length, 10.0 * log10(m.energy),
               m.high - m.low);
        CHECK(m.error < cases[i].error + 1.5, "the energy vector points near the source");
        CHECK(m.length > cases[i].length - 0.05, "and is as long as measured");
        CHECK(fabs(10.0 * log10(m.energy)) < 0.5, "unit energy on average");
        CHECK(m.high - m.low < 4.0, "within the measured energy range");
    }
}

static void TestSilenceAndMirror(void)
{
    maudChannelLayout layout = maud_layout7Point1Point4;
    maudSpeakerDecoder* decoder = Create(layout, 3);
    float matrix[12 * 16];
    CHECK(maudGetSpeakerDecoderMatrix(decoder, matrix) == maud_success, "the matrix");
    bool silent = true;
    bool mirrored = true;
    for (uint32_t s = 0; s < 12; ++s)
    {
        maudSpeakerPosition at = maudGetLayoutSpeakerPosition(layout, s);
        bool lfe = maudGetLayoutSpeaker(layout, s) == maud_speakerLowFrequency;
        for (uint32_t c = 0; c < 16; ++c)
        {
            silent = silent && (!lfe || matrix[s * 16 + c] == 0.0f);
        }
        for (uint32_t k = 0; k < 12 && !lfe; ++k)
        {
            maudSpeakerPosition there = maudGetLayoutSpeakerPosition(layout, k);
            if (there.azimuthDegrees != -at.azimuthDegrees ||
                there.elevationDegrees != at.elevationDegrees ||
                maudGetLayoutSpeaker(layout, k) == maud_speakerLowFrequency)
            {
                continue;
            }
            for (int trial = 0; trial < 20; ++trial)
            {
                double azimuth = 360.0 * Random();
                double elevation = 140.0 * Random() - 70.0;
                float a[12];
                float b[12];
                Decode(decoder, 3, Towards(azimuth, elevation), a);
                Decode(decoder, 3, Towards(-azimuth, elevation), b);
                mirrored = mirrored && fabsf(a[s] - b[k]) < 2e-3f;
            }
        }
    }
    CHECK(silent, "the LFE stays silent");
    CHECK(mirrored, "a mirrored source gets mirrored speakers");
    maudDestroySpeakerDecoder(decoder);
}

static void TestDecode(void)
{
    maudSpeakerDecoder* decoder = Create(maud_layout5Point1, 2);
    float matrix[6 * 9];
    CHECK(maudGetSpeakerDecoderMatrix(decoder, matrix) == maud_success, "the matrix");
    float channels[9][5];
    const float* bed[9];
    for (int c = 0; c < 9; ++c)
    {
        for (int n = 0; n < 5; ++n)
        {
            channels[c][n] = (float)(2.0 * Random() - 1.0);
        }
        bed[c] = channels[c];
    }
    float speakers[6][5];
    float* out[6];
    for (int s = 0; s < 6; ++s)
    {
        out[s] = speakers[s];
        for (int n = 0; n < 5; ++n)
        {
            speakers[s][n] = 9.0f;
        }
    }
    CHECK(maudDecodeToSpeakers(decoder, bed, out, 5) == maud_success, "decode");
    bool product = true;
    for (int s = 0; s < 6; ++s)
    {
        for (int n = 0; n < 5; ++n)
        {
            float sum = 0.0f;
            for (int c = 0; c < 9; ++c)
            {
                sum += matrix[s * 9 + c] * channels[c][n];
            }
            product = product && fabsf(speakers[s][n] - sum) < 1e-5f;
        }
    }
    CHECK(product, "decoding is the matrix, written over the output");
    float* missing[6] = {out[0], out[1], out[2], out[3], out[4], nullptr};
    speakers[0][0] = 7.0f;
    CHECK(maudDecodeToSpeakers(decoder, bed, missing, 5) == maud_errorInvalid, "a missing speaker");
    CHECK(speakers[0][0] == 7.0f, "nothing written by a bad call");
    CHECK(maudDecodeToSpeakers(decoder, nullptr, nullptr, 0) == maud_success, "no frames");
    CHECK(maudGetSpeakerDecoderMatrix(decoder, nullptr) == maud_errorInvalid, "no matrix");
    maudDestroySpeakerDecoder(decoder);
    maudSpeakerDecoderDef def = maudDefaultSpeakerDecoderDef();
    maudSpeakerDecoder* none = (maudSpeakerDecoder*)&def;
    def.order = 4;
    CHECK(maudCreateSpeakerDecoder(&def, &none) == maud_errorInvalid && none == nullptr, "order 4");
    def = maudDefaultSpeakerDecoderDef();
    def.layout = maud_layoutNone;
    CHECK(maudCreateSpeakerDecoder(&def, &none) == maud_errorInvalid, "no layout");
    def = maudDefaultSpeakerDecoderDef();
    def.cookie = 0;
    CHECK(maudCreateSpeakerDecoder(&def, &none) == maud_errorInvalid, "no cookie");
    maudDestroySpeakerDecoder(nullptr);
    long live = s_live;
    maudSpeakerDecoder* kept = Create(maud_layout7Point1, 3);
    CHECK(s_live == live + 1, "creation keeps one block, the panner given back");
    maudDestroySpeakerDecoder(kept);
    CHECK(s_live == live, "nothing left");
}

int main(void)
{
    TestLocalization();
    TestSilenceAndMirror();
    TestDecode();
    return s_failures == 0 ? 0 : 1;
}
