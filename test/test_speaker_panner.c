// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Speaker panners: a source at a speaker plays from it alone; gains keep
// the energy and leave the LFE silent; between 7.1.4's two layers the
// velocity vector points at the source exactly (VBAP's property); a
// mirrored source gets mirrored gains; the zenith and nadir share
// equally among the speakers around them; stereo folds the rear to the
// front; mono passes through; panning ramps and adds; bad calls write
// nothing.

#include "test_harness.h"

#include "maul-audio/speakers.h"

#include <math.h>

#define PI 3.14159265358979323846

static const maudChannelLayout s_layouts[] = {maud_layoutStereo, maud_layoutQuad,
                                              maud_layout5Point1, maud_layout7Point1,
                                              maud_layout7Point1Point4};

static maudSpeakerPanner* Create(maudChannelLayout layout)
{
    maudSpeakerPannerDef def = maudDefaultSpeakerPannerDef();
    def.layout = layout;
    maudSpeakerPanner* panner = nullptr;
    CHECK(maudCreateSpeakerPanner(&def, &panner) == maud_success, "a panner");
    return panner;
}

// A direction in the listener's frame from an azimuth (counterclockwise,
// to the left) and an elevation in degrees.
static maudVector3 Towards(double azimuth, double elevation)
{
    double a = azimuth * PI / 180.0;
    double e = elevation * PI / 180.0;
    return (maudVector3){(float)(-cos(e) * sin(a)), (float)sin(e), (float)(-cos(e) * cos(a))};
}

static uint32_t s_seed = 99;

static double Random(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (double)(s_seed >> 8) / 16777216.0;
}

static void TestOnSpeakers(void)
{
    for (size_t l = 0; l < sizeof(s_layouts) / sizeof(s_layouts[0]); ++l)
    {
        maudChannelLayout layout = s_layouts[l];
        maudSpeakerPanner* panner = Create(layout);
        uint32_t channels = maudGetLayoutChannelCount(layout);
        bool alone = true;
        for (uint32_t c = 0; c < channels; ++c)
        {
            if (maudGetLayoutSpeaker(layout, c) == maud_speakerLowFrequency)
            {
                continue;
            }
            maudSpeakerPosition at = maudGetLayoutSpeakerPosition(layout, c);
            float gains[12];
            CHECK(maudGetSpeakerGains(
                      panner, Towards((double)at.azimuthDegrees, (double)at.elevationDegrees),
                      gains) == maud_success,
                  "gains");
            for (uint32_t k = 0; k < channels; ++k)
            {
                alone = alone && fabsf(gains[k] - (k == c ? 1.0f : 0.0f)) < 1e-5f;
            }
        }
        CHECK(alone, "a source at a speaker plays from it alone");
        maudDestroySpeakerPanner(panner);
    }
}

static void TestEnergy(void)
{
    for (size_t l = 0; l < sizeof(s_layouts) / sizeof(s_layouts[0]); ++l)
    {
        maudChannelLayout layout = s_layouts[l];
        maudSpeakerPanner* panner = Create(layout);
        uint32_t channels = maudGetLayoutChannelCount(layout);
        bool kept = true;
        bool silent = true;
        for (int trial = 0; trial < 500; ++trial)
        {
            double z = 2.0 * Random() - 1.0;
            float gains[12];
            CHECK(maudGetSpeakerGains(panner, Towards(360.0 * Random(), asin(z) * 180.0 / PI),
                                      gains) == maud_success,
                  "gains");
            float energy = 0.0f;
            for (uint32_t c = 0; c < channels; ++c)
            {
                energy += gains[c] * gains[c];
                silent = silent && (maudGetLayoutSpeaker(layout, c) != maud_speakerLowFrequency ||
                                    gains[c] == 0.0f);
                kept = kept && gains[c] >= 0.0f;
            }
            kept = kept && fabsf(energy - 1.0f) < 1e-5f;
        }
        // On the edges between ear-level speakers, where the third
        // corner's gain is zero up to rounding.
        for (uint32_t c = 0; c < channels; ++c)
        {
            maudSpeakerPosition at = maudGetLayoutSpeakerPosition(layout, c);
            for (int step = 1; step < 40; ++step)
            {
                float gains[12];
                CHECK(maudGetSpeakerGains(panner,
                                          Towards((double)at.azimuthDegrees + 0.37 * step, 0.0),
                                          gains) == maud_success,
                      "gains");
                for (uint32_t k = 0; k < channels; ++k)
                {
                    kept = kept && gains[k] >= 0.0f;
                }
            }
        }
        CHECK(kept, "gains keep the energy, none negative");
        CHECK(silent, "the LFE gets nothing");
        maudDestroySpeakerPanner(panner);
    }
}

// Between the ear-level and the top layers of 7.1.4 every triangle is
// made of real speakers, so the velocity vector, sum g_i p_i, points
// exactly at the source.
static void TestVelocity(void)
{
    maudChannelLayout layout = maud_layout7Point1Point4;
    maudSpeakerPanner* panner = Create(layout);
    double worst = 0.0;
    for (int trial = 0; trial < 300; ++trial)
    {
        double azimuth = 360.0 * Random() - 180.0;
        double elevation = 3.0 + 24.0 * Random();
        maudVector3 u = Towards(azimuth, elevation);
        float gains[12];
        CHECK(maudGetSpeakerGains(panner, u, gains) == maud_success, "gains");
        double v[3] = {0.0, 0.0, 0.0};
        for (uint32_t c = 0; c < 12; ++c)
        {
            maudSpeakerPosition at = maudGetLayoutSpeakerPosition(layout, c);
            maudVector3 p = Towards((double)at.azimuthDegrees, (double)at.elevationDegrees);
            v[0] += (double)gains[c] * (double)p.x;
            v[1] += (double)gains[c] * (double)p.y;
            v[2] += (double)gains[c] * (double)p.z;
        }
        double length = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        double dot = (v[0] * (double)u.x + v[1] * (double)u.y + v[2] * (double)u.z) / length;
        worst = fmax(worst, acos(fmin(dot, 1.0)) * 180.0 / PI);
    }
    printf("velocity vector within %.4f degrees\n", worst);
    CHECK(worst < 0.05, "the velocity vector points at the source");
    maudDestroySpeakerPanner(panner);
}

// A source mirrored left to right gets the mirrored speakers' gains.
static void TestMirror(void)
{
    for (size_t l = 0; l < sizeof(s_layouts) / sizeof(s_layouts[0]); ++l)
    {
        maudChannelLayout layout = s_layouts[l];
        maudSpeakerPanner* panner = Create(layout);
        uint32_t channels = maudGetLayoutChannelCount(layout);
        bool mirrored = true;
        for (int trial = 0; trial < 100; ++trial)
        {
            double azimuth = 360.0 * Random();
            double elevation = 160.0 * Random() - 80.0;
            float gains[12];
            float other[12];
            CHECK(maudGetSpeakerGains(panner, Towards(azimuth, elevation), gains) == maud_success &&
                      maudGetSpeakerGains(panner, Towards(-azimuth, elevation), other) ==
                          maud_success,
                  "gains");
            for (uint32_t c = 0; c < channels; ++c)
            {
                maudSpeakerPosition at = maudGetLayoutSpeakerPosition(layout, c);
                for (uint32_t k = 0; k < channels; ++k)
                {
                    maudSpeakerPosition there = maudGetLayoutSpeakerPosition(layout, k);
                    if (there.azimuthDegrees == -at.azimuthDegrees &&
                        there.elevationDegrees == at.elevationDegrees &&
                        (maudGetLayoutSpeaker(layout, c) == maud_speakerLowFrequency) ==
                            (maudGetLayoutSpeaker(layout, k) == maud_speakerLowFrequency))
                    {
                        mirrored = mirrored && fabsf(gains[c] - other[k]) < 1e-4f;
                    }
                }
            }
        }
        // On the mirror plane itself, ahead and behind: where 7.1.4's
        // four back speakers share a plane, both ways of splitting it
        // hold the direction, and a choice of one would lean.
        for (int step = -16; step <= 16; ++step)
        {
            for (int behind = 0; behind < 2; ++behind)
            {
                float gains[12];
                CHECK(maudGetSpeakerGains(panner, Towards(180.0 * behind, 5.0 * step), gains) ==
                          maud_success,
                      "gains");
                for (uint32_t c = 0; c < channels; ++c)
                {
                    maudSpeakerPosition at = maudGetLayoutSpeakerPosition(layout, c);
                    for (uint32_t k = 0; k < channels; ++k)
                    {
                        maudSpeakerPosition there = maudGetLayoutSpeakerPosition(layout, k);
                        bool lfe = maudGetLayoutSpeaker(layout, c) == maud_speakerLowFrequency ||
                                   maudGetLayoutSpeaker(layout, k) == maud_speakerLowFrequency;
                        if (!lfe && there.azimuthDegrees == -at.azimuthDegrees &&
                            there.elevationDegrees == at.elevationDegrees)
                        {
                            mirrored = mirrored && fabsf(gains[c] - gains[k]) < 1e-4f;
                        }
                    }
                }
            }
        }
        CHECK(mirrored, "a mirrored source gets mirrored gains");
        maudDestroySpeakerPanner(panner);
    }
}

// The zenith and the nadir: shared equally among the speakers around
// the imaginary ones there.
static void TestPoles(void)
{
    maudSpeakerPanner* tall = Create(maud_layout7Point1Point4);
    float gains[12];
    CHECK(maudGetSpeakerGains(tall, (maudVector3){0.0f, 1.0f, 0.0f}, gains) == maud_success,
          "gains");
    bool top = true;
    for (uint32_t c = 0; c < 12; ++c)
    {
        bool high = maudGetLayoutSpeakerPosition(maud_layout7Point1Point4, c).elevationDegrees > 0;
        top = top && fabsf(gains[c] - (high ? 0.5f : 0.0f)) < 1e-5f;
    }
    CHECK(top, "the zenith shared by the four top speakers");
    CHECK(maudGetSpeakerGains(tall, (maudVector3){0.0f, -1.0f, 0.0f}, gains) == maud_success,
          "gains");
    bool bottom = true;
    for (uint32_t c = 0; c < 12; ++c)
    {
        maudSpeakerPosition at = maudGetLayoutSpeakerPosition(maud_layout7Point1Point4, c);
        bool low = at.elevationDegrees == 0.0f &&
                   maudGetLayoutSpeaker(maud_layout7Point1Point4, c) != maud_speakerLowFrequency;
        bottom = bottom && fabsf(gains[c] - (low ? 1.0f / sqrtf(7.0f) : 0.0f)) < 1e-5f;
    }
    CHECK(bottom, "the nadir shared by the seven ear-level speakers");
    // 60 degrees up ahead: the triangle of the zenith and the two front
    // top speakers, solved here by Cramer's rule; the zenith's share goes
    // to the four top speakers equally, then the gains keep the energy.
    double p[3][3] = {{0.0, 1.0, 0.0}};
    maudVector3 front[2] = {Towards(45.0, 30.0), Towards(-45.0, 30.0)};
    for (int i = 0; i < 2; ++i)
    {
        p[i + 1][0] = (double)front[i].x;
        p[i + 1][1] = (double)front[i].y;
        p[i + 1][2] = (double)front[i].z;
    }
    maudVector3 source = Towards(0.0, 60.0);
    double u[3] = {(double)source.x, (double)source.y, (double)source.z};
    double det = p[0][0] * (p[1][1] * p[2][2] - p[1][2] * p[2][1]) -
                 p[1][0] * (p[0][1] * p[2][2] - p[0][2] * p[2][1]) +
                 p[2][0] * (p[0][1] * p[1][2] - p[0][2] * p[1][1]);
    double solved[3];
    for (int k = 0; k < 3; ++k)
    {
        double m[3][3];
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                m[i][j] = i == k ? u[j] : p[i][j];
            }
        }
        solved[k] = (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                     m[1][0] * (m[0][1] * m[2][2] - m[0][2] * m[2][1]) +
                     m[2][0] * (m[0][1] * m[1][2] - m[0][2] * m[1][1])) /
                    det;
    }
    double expected[12] = {0.0};
    double energy = 0.0;
    for (uint32_t c = 0; c < 12; ++c)
    {
        maudSpeakerPosition at = maudGetLayoutSpeakerPosition(maud_layout7Point1Point4, c);
        if (at.elevationDegrees > 0.0f)
        {
            expected[c] = solved[0] / 4.0;
            expected[c] += at.azimuthDegrees == 45.0f ? solved[1] : 0.0;
            expected[c] += at.azimuthDegrees == -45.0f ? solved[2] : 0.0;
        }
        energy += expected[c] * expected[c];
    }
    CHECK(maudGetSpeakerGains(tall, source, gains) == maud_success, "gains");
    bool shared = true;
    for (uint32_t c = 0; c < 12; ++c)
    {
        shared = shared && fabs((double)gains[c] - expected[c] / sqrt(energy)) < 1e-5;
    }
    CHECK(shared, "an imaginary corner's share goes equally to its neighbours");
    maudDestroySpeakerPanner(tall);
    maudSpeakerPanner* flat = Create(maud_layout5Point1);
    CHECK(maudGetSpeakerGains(flat, (maudVector3){0.0f, 1.0f, 0.0f}, gains) == maud_success,
          "gains");
    bool all = true;
    for (uint32_t c = 0; c < 6; ++c)
    {
        bool lfe = maudGetLayoutSpeaker(maud_layout5Point1, c) == maud_speakerLowFrequency;
        all = all && fabsf(gains[c] - (lfe ? 0.0f : 1.0f / sqrtf(5.0f))) < 1e-5f;
    }
    CHECK(all, "on 5.1, the zenith shared by all five");
    maudDestroySpeakerPanner(flat);
}

static void TestStereoAndMono(void)
{
    maudSpeakerPanner* stereo = Create(maud_layoutStereo);
    float gains[2];
    float behind[2];
    CHECK(maudGetSpeakerGains(stereo, (maudVector3){-1.0f, 0.0f, 0.0f}, gains) == maud_success &&
              fabsf(gains[0] - 1.0f) < 1e-6f && gains[1] == 0.0f,
          "the left side: the left speaker");
    CHECK(maudGetSpeakerGains(stereo, (maudVector3){0.0f, 0.0f, -1.0f}, gains) == maud_success &&
              fabsf(gains[0] - gains[1]) < 1e-6f && fabsf(gains[0] - sqrtf(0.5f)) < 1e-6f,
          "ahead: both alike");
    CHECK(maudGetSpeakerGains(stereo, Towards(15.0, 0.0), gains) == maud_success &&
              maudGetSpeakerGains(stereo, Towards(165.0, 0.0), behind) == maud_success &&
              fabsf(gains[0] - behind[0]) < 1e-6f && fabsf(gains[1] - behind[1]) < 1e-6f,
          "the rear folds to the front");
    CHECK(maudGetSpeakerGains(stereo, (maudVector3){0.0f, 0.0f, 0.0f}, gains) == maud_success &&
              fabsf(gains[0] - gains[1]) < 1e-6f,
          "a zero direction is ahead");
    maudDestroySpeakerPanner(stereo);
    maudSpeakerPanner* mono = Create(maud_layoutMono);
    CHECK(maudGetSpeakerGains(mono, Towards(123.0, 45.0), gains) == maud_success &&
              gains[0] == 1.0f,
          "mono passes through");
    maudDestroySpeakerPanner(mono);
}

// Panning adds into the channels, its gains ramping from the previous
// direction's to this one's.
static void TestPan(void)
{
    maudSpeakerPanner* panner = Create(maud_layout5Point1);
    float channels[6][8];
    float* out[6];
    for (int c = 0; c < 6; ++c)
    {
        out[c] = channels[c];
        for (int n = 0; n < 8; ++n)
        {
            channels[c][n] = 0.25f;
        }
    }
    float in[8] = {1.0f, -1.0f, 0.5f, 2.0f, 1.0f, 0.0f, -2.0f, 1.0f};
    maudPanSource from = {Towards(10.0, 0.0), 1.0f};
    maudPanSource to = {Towards(-60.0, 0.0), 0.5f};
    CHECK(maudPanToSpeakers(panner, &from, &to, in, out, 8) == maud_success, "pan");
    float a[6];
    float b[6];
    CHECK(maudGetSpeakerGains(panner, from.direction, a) == maud_success &&
              maudGetSpeakerGains(panner, to.direction, b) == maud_success,
          "gains");
    bool ramped = true;
    for (int c = 0; c < 6; ++c)
    {
        for (int n = 0; n < 8; ++n)
        {
            float t = (float)(n + 1) / 8.0f;
            float gain = a[c] + t * (0.5f * b[c] - a[c]);
            ramped = ramped && fabsf(channels[c][n] - (0.25f + gain * in[n])) < 1e-5f;
        }
    }
    CHECK(ramped, "panning ramps and adds");
    float* missing[6] = {out[0], out[1], out[2], out[3], out[4], nullptr};
    channels[0][0] = 7.0f;
    CHECK(maudPanToSpeakers(panner, &from, &to, in, missing, 8) == maud_errorInvalid,
          "a missing channel");
    maudPanSource bad = {{NAN, 0.0f, 0.0f}, 1.0f};
    CHECK(maudPanToSpeakers(panner, &bad, &to, in, out, 8) == maud_errorInvalid, "NaN");
    CHECK(channels[0][0] == 7.0f, "nothing written by a bad call");
    CHECK(maudGetSpeakerGains(panner, from.direction, nullptr) == maud_errorInvalid, "no gains");
    maudDestroySpeakerPanner(panner);
    maudSpeakerPannerDef def = maudDefaultSpeakerPannerDef();
    maudSpeakerPanner* none = (maudSpeakerPanner*)&def;
    def.layout = maud_layoutNone;
    CHECK(maudCreateSpeakerPanner(&def, &none) == maud_errorInvalid && none == nullptr,
          "no layout");
    def = maudDefaultSpeakerPannerDef();
    def.cookie = 0;
    CHECK(maudCreateSpeakerPanner(&def, &none) == maud_errorInvalid, "no cookie");
    maudDestroySpeakerPanner(nullptr);
}

int main(void)
{
    TestOnSpeakers();
    TestEnergy();
    TestVelocity();
    TestMirror();
    TestPoles();
    TestStereoAndMono();
    TestPan();
    return s_failures == 0 ? 0 : 1;
}
