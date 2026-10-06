// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ambisonic bed: encoding gains against a general SN3D spherical
// harmonic computed independently (associated Legendre functions in
// double) and against AmbiX's axes; rotating a direction's encoding
// equals encoding the rotated direction, at every order, still or at the
// end of a ramp; encoding adds and ramps; rotation leaves channel 0
// alone; bad calls write nothing.

#include "test_harness.h"

#include "maul-audio/ambisonics.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846

static uint32_t s_seed = 0x1234567u;

static double Random(void)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return (double)(s_seed >> 8) / 16777216.0;
}

// The real spherical harmonic of degree l and order m, SN3D without the
// Condon-Shortley phase (AmbiX), at azimuth a (counterclockwise from
// ahead) and elevation e, in radians.
static double Harmonic(int l, int m, double a, double e)
{
    int am = m < 0 ? -m : m;
    double x = sin(e);
    // P_am^am, then up to P_l^am, by the standard recurrences (no phase).
    double p = 1.0;
    double root = sqrt(1.0 - x * x);
    for (int i = 1; i <= am; ++i)
    {
        p *= (2.0 * i - 1.0) * root;
    }
    double previous = 0.0;
    for (int degree = am; degree < l; ++degree)
    {
        double next =
            ((2.0 * degree + 1.0) * x * p - (degree + am) * previous) / (degree - am + 1.0);
        previous = p;
        p = next;
    }
    double ratio = 1.0;
    for (int i = l - am + 1; i <= l + am; ++i)
    {
        ratio /= i;
    }
    double norm = sqrt((m == 0 ? 1.0 : 2.0) * ratio);
    return norm * p * (m >= 0 ? cos(am * a) : sin(am * a));
}

// A listener-frame direction's azimuth and elevation in AmbiX terms.
static void Angles(maudVector3 v, double* a, double* e)
{
    double x = (double)v.x, y = (double)v.y, z = (double)v.z;
    double length = sqrt(x * x + y * y + z * z);
    *a = atan2(-x, -z);
    *e = asin(y / length);
}

static maudVector3 RandomDirection(void)
{
    double z = 2.0 * Random() - 1.0;
    double a = 2.0 * PI * Random();
    double r = sqrt(1.0 - z * z);
    return (maudVector3){(float)(r * cos(a)), (float)z, (float)(r * sin(a))};
}

static void TestGains(void)
{
    bool match = true;
    for (int trial = 0; trial < 200; ++trial)
    {
        maudVector3 d = RandomDirection();
        float gains[16];
        CHECK(maudGetAmbisonicGains(3, d, gains) == maud_success, "gains");
        double a;
        double e;
        Angles(d, &a, &e);
        for (int l = 0, c = 0; l <= 3; ++l)
        {
            for (int m = -l; m <= l; ++m, ++c)
            {
                match = match && fabs((double)gains[c] - Harmonic(l, m, a, e)) < 1e-5;
            }
        }
    }
    CHECK(match, "the gains are SN3D harmonics in ACN order");
    float g[4];
    CHECK(maudGetAmbisonicGains(1, (maudVector3){0.0f, 0.0f, -1.0f}, g) == maud_success &&
              g[0] == 1.0f && g[1] == 0.0f && g[2] == 0.0f && g[3] == 1.0f,
          "ahead is +x of the field (ACN 3)");
    CHECK(maudGetAmbisonicGains(1, (maudVector3){-2.0f, 0.0f, 0.0f}, g) == maud_success &&
              g[1] == 1.0f && g[3] == 0.0f,
          "the left is +y (ACN 1)");
    CHECK(maudGetAmbisonicGains(1, (maudVector3){0.0f, 1.0f, 0.0f}, g) == maud_success &&
              g[2] == 1.0f,
          "up is +z (ACN 2)");
    CHECK(maudGetAmbisonicGains(1, (maudVector3){0.0f, 0.0f, 0.0f}, g) == maud_success &&
              g[3] == 1.0f,
          "a zero direction is ahead");
    CHECK(maudGetAmbisonicChannelCount(1) == 4 && maudGetAmbisonicChannelCount(3) == 16 &&
              maudGetAmbisonicChannelCount(0) == 0 && maudGetAmbisonicChannelCount(4) == 0,
          "channel counts");
}

// v rotated by the unit quaternion q: v + 2w (u x v) + 2 u x (u x v).
static maudVector3 Rotated(maudQuaternion q, maudVector3 v)
{
    double qx = (double)q.x, qy = (double)q.y, qz = (double)q.z, qw = (double)q.w;
    double n = sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    double x = qx / n, y = qy / n, z = qz / n, w = qw / n;
    double vx = (double)v.x, vy = (double)v.y, vz = (double)v.z;
    double cx = y * vz - z * vy, cy = z * vx - x * vz, cz = x * vy - y * vx;
    double ccx = y * cz - z * cy, ccy = z * cx - x * cz, ccz = x * cy - y * cx;
    return (maudVector3){(float)(vx + 2.0 * (w * cx + ccx)), (float)(vy + 2.0 * (w * cy + ccy)),
                         (float)(vz + 2.0 * (w * cz + ccz))};
}

static maudQuaternion RandomRotation(void)
{
    return (maudQuaternion){(float)(2.0 * Random() - 1.0), (float)(2.0 * Random() - 1.0),
                            (float)(2.0 * Random() - 1.0), (float)(2.0 * Random() - 1.0)};
}

// Rotating the encoding of d equals encoding q d: still, and on the last
// frame of a ramp from no rotation.
static void TestRotation(void)
{
    const maudQuaternion none = {0.0f, 0.0f, 0.0f, 1.0f};
    for (uint32_t order = 1; order <= 3; ++order)
    {
        uint32_t channels = maudGetAmbisonicChannelCount(order);
        bool still = true;
        bool ramped = true;
        for (int trial = 0; trial < 100; ++trial)
        {
            maudQuaternion q = RandomRotation();
            maudVector3 d = RandomDirection();
            float expected[16];
            CHECK(maudGetAmbisonicGains(order, Rotated(q, d), expected) == maud_success, "gains");
            float frames[16][2];
            float* bed[16];
            for (uint32_t c = 0; c < channels; ++c)
            {
                bed[c] = frames[c];
            }
            float gains[16];
            CHECK(maudGetAmbisonicGains(order, d, gains) == maud_success, "gains");
            for (uint32_t c = 0; c < channels; ++c)
            {
                frames[c][0] = gains[c];
                frames[c][1] = gains[c];
            }
            CHECK(maudRotateAmbisonic(order, &q, &q, bed, 1) == maud_success, "rotate");
            float* second[16];
            for (uint32_t c = 0; c < channels; ++c)
            {
                second[c] = frames[c] + 1;
            }
            CHECK(maudRotateAmbisonic(order, &none, &q, second, 1) == maud_success, "rotate");
            for (uint32_t c = 0; c < channels; ++c)
            {
                still = still && fabsf(frames[c][0] - expected[c]) < 1e-4f;
                ramped = ramped && fabsf(frames[c][1] - expected[c]) < 1e-4f;
            }
        }
        CHECK(still, "a rotated encoding is the rotated direction's");
        CHECK(ramped, "and so is the end of a ramp to it");
    }
}

// Halfway through a ramp from no rotation to a quarter turn, each frame
// is the mean of the two rotated fields; channel 0 never changes.
static void TestRotationRamp(void)
{
    const maudQuaternion none = {0.0f, 0.0f, 0.0f, 1.0f};
    // A quarter turn about +y (up): ahead goes to the left... or right; the
    // test only compares with the two fields' mean.
    const maudQuaternion turn = {0.0f, 0.70710678f, 0.0f, 0.70710678f};
    float channels[4][4];
    float* bed[4] = {channels[0], channels[1], channels[2], channels[3]};
    float gains[4];
    CHECK(maudGetAmbisonicGains(1, (maudVector3){0.0f, 0.0f, -1.0f}, gains) == maud_success,
          "gains");
    for (int c = 0; c < 4; ++c)
    {
        for (int n = 0; n < 4; ++n)
        {
            channels[c][n] = gains[c];
        }
    }
    float turned[4];
    CHECK(maudGetAmbisonicGains(1, Rotated(turn, (maudVector3){0.0f, 0.0f, -1.0f}), turned) ==
              maud_success,
          "gains");
    CHECK(maudRotateAmbisonic(1, &none, &turn, bed, 4) == maud_success, "rotate");
    bool blend = true;
    for (int n = 0; n < 4; ++n)
    {
        float t = (float)(n + 1) / 4.0f;
        for (int c = 0; c < 4; ++c)
        {
            blend =
                blend && fabsf(channels[c][n] - (gains[c] + t * (turned[c] - gains[c]))) < 1e-5f;
        }
    }
    CHECK(blend, "a ramp crossfades the rotated fields");
    CHECK(channels[0][0] == 1.0f && channels[0][3] == 1.0f, "channel 0 untouched");
}

// Encoding adds into the bed, its gain moving across the call.
static void TestEncode(void)
{
    float channels[16][8];
    float* bed[16];
    for (int c = 0; c < 16; ++c)
    {
        bed[c] = channels[c];
        for (int n = 0; n < 8; ++n)
        {
            channels[c][n] = 0.5f;
        }
    }
    float in[8] = {1.0f, -1.0f, 2.0f, 0.5f, 1.0f, 1.0f, -2.0f, 3.0f};
    maudVector3 d = {0.3f, 0.4f, -0.5f};
    maudPanSource from = {d, 0.0f};
    maudPanSource to = {d, 2.0f};
    CHECK(maudEncodeAmbisonic(3, &from, &to, in, bed, 8) == maud_success, "encode");
    float gains[16];
    CHECK(maudGetAmbisonicGains(3, d, gains) == maud_success, "gains");
    bool added = true;
    for (int c = 0; c < 16; ++c)
    {
        for (int n = 0; n < 8; ++n)
        {
            float gain = 2.0f * (float)(n + 1) / 8.0f;
            added = added && fabsf(channels[c][n] - (0.5f + gains[c] * gain * in[n])) < 1e-5f;
        }
    }
    CHECK(added, "encoding adds, its gain ramping");
}

static void TestMisuse(void)
{
    float channel[4] = {7.0f, 7.0f, 7.0f, 7.0f};
    float* bed[4] = {channel, channel, channel, nullptr};
    float in[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    maudPanSource source = {{0.0f, 0.0f, -1.0f}, 1.0f};
    CHECK(maudEncodeAmbisonic(1, &source, &source, in, bed, 4) == maud_errorInvalid,
          "a missing channel");
    bed[3] = channel;
    maudPanSource bad = {{NAN, 0.0f, 0.0f}, 1.0f};
    CHECK(maudEncodeAmbisonic(1, &bad, &source, in, bed, 4) == maud_errorInvalid, "NaN");
    CHECK(maudEncodeAmbisonic(4, &source, &source, in, bed, 4) == maud_errorInvalid, "order 4");
    CHECK(maudEncodeAmbisonic(1, &source, &source, nullptr, bed, 4) == maud_errorInvalid,
          "no input");
    maudQuaternion zero = {0.0f, 0.0f, 0.0f, 0.0f};
    maudQuaternion none = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(maudRotateAmbisonic(1, &zero, &none, bed, 4) == maud_errorInvalid, "a zero quaternion");
    CHECK(maudRotateAmbisonic(0, &none, &none, bed, 4) == maud_errorInvalid, "order 0");
    CHECK(channel[0] == 7.0f && channel[3] == 7.0f, "nothing written by a bad call");
    CHECK(maudGetAmbisonicGains(2, source.direction, nullptr) == maud_errorInvalid, "no gains");
}

int main(void)
{
    TestGains();
    TestRotation();
    TestRotationRamp();
    TestEncode();
    TestMisuse();
    return s_failures == 0 ? 0 : 1;
}
