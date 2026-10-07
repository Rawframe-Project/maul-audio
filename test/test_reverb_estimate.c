// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reverberation times from geometry (whitebox), in closed boxes of the
// library's own scene, against references traced with 100,000 rays 200
// bounces deep by the same model: an office with even absorption, the
// office with an absorbent floor and smooth walls (where Sabine and
// Eyring read a third short), a hall, a corridor; each within 6 %.
// Diffuse rooms need few rays (1024 here); the smooth room's late decay
// rides on a few grazing paths, so its estimate scatters by up to 9 %
// below about 6000 rays and takes 16,384 here. Bands absorbing
// differently get their own times; the air shortens a hall's as
// Eyring's air term says; an open field gives the floor, a box without
// absorption the ceiling (its rays cut by the bounce cap, the tail
// compensated), and a room whose rays are cut mid-decay its time once
// the tail is filled in; batches split any way sum the same; unknown
// materials end a ray. One bounce off a lossless diffuse floor gives the
// energy the shading integral gives, in the bin of its path; blocked
// shadow rays bring nothing; the fit reads an exponential's time and
// spans -5 to -25 dB of a double slope. An office's estimate with the
// default air, field and all, hashes to the same bits on every platform
// (the bake's promise).

#include "air_absorption.h"
#include "reverb_estimate.h"
#include "test_harness.h"

#include "maul-audio/scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846

// A closed box from the origin to size, a material per face: x low and
// high, y low and high, z low and high.
static maudAcousticScene* Box(float x, float y, float z)
{
    static maudVector3 v[8];
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[12] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
    for (int i = 0; i < 8; ++i)
    {
        v[i] = (maudVector3){(i & 1) ? x : 0.0f, (i & 2) ? y : 0.0f, (i & 4) ? z : 0.0f};
    }
    maudMesh mesh = {v, 8, faces, materials, 12};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a box");
    return scene;
}

static maudReverbHistogram s_histograms[256];

// The fitted times alone, as most checks take them.
static void FitTimes(maudReverbHistogram* histograms, uint32_t count, float* times)
{
    maudReverbFit fit;
    maudFitReverb(histograms, count, &fit);
    memcpy(times, fit.times, sizeof(fit.times));
}

static void Estimate(maudAcousticScene* scene, const maudAcousticMaterial* materials,
                     maudVector3 listener, const float* air, uint32_t rays, float* times)
{
    maudReverbTrace trace = {maudSceneClosestHit,
                             maudSceneAnyHit,
                             scene,
                             materials,
                             6,
                             {air[0], air[1], air[2]},
                             listener,
                             rays,
                             512,
                             0,
                             0};
    uint32_t batches = maudReverbBatches(rays);
    for (uint32_t b = 0; b < batches; ++b)
    {
        maudTraceReverbBatch(&trace, b, &s_histograms[b]);
    }
    FitTimes(s_histograms, batches, times);
}

static maudAcousticMaterial Material(float absorption, float scattering)
{
    return (maudAcousticMaterial){{absorption, absorption, absorption}, scattering, {0, 0, 0}};
}

static void TestRooms(void)
{
    typedef struct Room
    {
        const char* name;
        float size[3];
        float absorption[6];
        float scattering;
        double reference;
        uint32_t rays;
    } Room;
    const Room rooms[] = {
        {"office", {5, 4, 3}, {0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f}, 0.5f, 0.99, 1024},
        {"office, absorbent floor",
         {5, 4, 3},
         {0.05f, 0.05f, 0.05f, 0.05f, 0.6f, 0.05f},
         0.1f,
         0.86,
         16384},
        {"hall", {30, 20, 12}, {0.15f, 0.15f, 0.15f, 0.15f, 0.15f, 0.15f}, 0.5f, 3.05, 1024},
        {"corridor", {30, 2, 3}, {0.1f, 0.1f, 0.1f, 0.1f, 0.1f, 0.1f}, 0.5f, 0.87, 1024},
    };
    const float none[3] = {0.0f, 0.0f, 0.0f};
    for (size_t r = 0; r < sizeof(rooms) / sizeof(rooms[0]); ++r)
    {
        const Room* room = &rooms[r];
        maudAcousticScene* scene = Box(room->size[0], room->size[1], room->size[2]);
        maudAcousticMaterial materials[6];
        for (int f = 0; f < 6; ++f)
        {
            materials[f] = Material(room->absorption[f], room->scattering);
        }
        maudVector3 listener = {0.37f * room->size[0], 0.41f * room->size[1],
                                0.45f * room->size[2]};
        float times[3];
        Estimate(scene, materials, listener, none, room->rays, times);
        printf("%s: %.3f s (reference %.2f)\n", room->name, (double)times[1], room->reference);
        CHECK(fabs((double)times[1] / room->reference - 1.0) < 0.06, "the reference's time");
        CHECK(times[0] == times[1] && times[1] == times[2], "equal bands, equal times");
        maudDestroyAcousticScene(scene);
    }
}

static double Eyring(double volume, double area, double absorption, double air)
{
    return 0.161 * volume / (-area * log(1.0 - absorption) + 8.0 * air * volume);
}

static void TestBandsAndAir(void)
{
    maudAcousticScene* office = Box(5, 4, 3);
    maudAcousticMaterial banded[6];
    for (int f = 0; f < 6; ++f)
    {
        banded[f] = (maudAcousticMaterial){{0.05f, 0.1f, 0.3f}, 0.5f, {0, 0, 0}};
    }
    const float none[3] = {0.0f, 0.0f, 0.0f};
    float times[3];
    Estimate(office, banded, (maudVector3){1.85f, 1.64f, 1.35f}, none, 1024, times);
    const double absorption[3] = {0.05, 0.1, 0.3};
    for (int b = 0; b < 3; ++b)
    {
        double eyring = Eyring(60.0, 94.0, absorption[b], 0.0);
        printf("office band %d: %.3f s (Eyring %.3f)\n", b, (double)times[b], eyring);
        CHECK(fabs((double)times[b] / eyring - 1.0) < 0.08, "each band its own time");
    }
    maudDestroyAcousticScene(office);
    // The air (amplitude 0.003 per metre, energy twice that) in a diffuse
    // hall: Eyring's 4 m V term with m the energy exponent.
    maudAcousticScene* hall = Box(30, 20, 12);
    maudAcousticMaterial even[6];
    for (int f = 0; f < 6; ++f)
    {
        even[f] = Material(0.15f, 0.5f);
    }
    const float air[3] = {0.0f, 0.003f, 0.0f};
    Estimate(hall, even, (maudVector3){11.1f, 8.2f, 5.4f}, air, 1024, times);
    double still = 3.05;
    double ratio = Eyring(7200.0, 2640.0, 0.15, 0.003) / Eyring(7200.0, 2640.0, 0.15, 0.0);
    printf("hall with air: %.3f s, without %.3f s (Eyring's ratio %.3f)\n", (double)times[1],
           (double)times[0], ratio);
    CHECK(fabs((double)times[1] / (still * ratio) - 1.0) < 0.06, "the air shortens the time");
    CHECK(fabs((double)times[0] / still - 1.0) < 0.06, "only in its band");
    maudDestroyAcousticScene(hall);
}

// The ray hook for an open field: nothing anywhere.
static void Nothing(const maudRay* rays, uint32_t count, maudRayHit* hits, void* context)
{
    (void)rays;
    (void)context;
    for (uint32_t i = 0; i < count; ++i)
    {
        hits[i] = (maudRayHit){INFINITY, {0, 0, 1}, 0};
    }
}

static void Never(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    (void)rays;
    (void)context;
    for (uint32_t i = 0; i < count; ++i)
    {
        occluded[i] = 0;
    }
}

static void TestLimits(void)
{
    maudAcousticMaterial hard = Material(0.0f, 0.5f);
    maudReverbTrace open = {Nothing,   Never, nullptr, &hard, 1, {0, 0, 0},
                            {0, 0, 0}, 128,   512,     0,     0};
    float times[3];
    for (uint32_t b = 0; b < 2; ++b)
    {
        maudTraceReverbBatch(&open, b, &s_histograms[b]);
    }
    FitTimes(s_histograms, 2, times);
    CHECK(times[0] == 0.1f && times[2] == 0.1f, "an open field: the floor");
    maudAcousticScene* box = Box(5, 4, 3);
    maudAcousticMaterial lossless[6];
    for (int f = 0; f < 6; ++f)
    {
        lossless[f] = hard;
    }
    const float none[3] = {0.0f, 0.0f, 0.0f};
    Estimate(box, lossless, (maudVector3){1.85f, 1.64f, 1.35f}, none, 128, times);
    CHECK(times[1] == 20.0f, "no absorption: the ceiling");
    // An unknown material ends the ray where it hits: nothing comes back.
    maudReverbTrace unknown = {maudSceneClosestHit,
                               maudSceneAnyHit,
                               box,
                               lossless,
                               0,
                               {0, 0, 0},
                               {1.85f, 1.64f, 1.35f},
                               64,
                               512,
                               0,
                               0};
    maudTraceReverbBatch(&unknown, 0, &s_histograms[0]);
    double total = 0.0;
    for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
    {
        total += (double)s_histograms[0].energy[1][i];
    }
    CHECK(total == 0.0, "unknown materials reflect nothing");
    maudDestroyAcousticScene(box);
}

// Batches traced in any order, then summed in order, give the same
// times; a batch's histogram does not depend on the others.
static void TestOrder(void)
{
    maudAcousticScene* box = Box(5, 4, 3);
    maudAcousticMaterial m[6];
    for (int f = 0; f < 6; ++f)
    {
        m[f] = Material(0.1f, 0.5f);
    }
    maudReverbTrace trace = {maudSceneClosestHit,
                             maudSceneAnyHit,
                             box,
                             m,
                             6,
                             {0, 0, 0},
                             {1.85f, 1.64f, 1.35f},
                             256,
                             512,
                             0,
                             0};
    float forward[3];
    float backward[3];
    for (uint32_t b = 0; b < 4; ++b)
    {
        maudTraceReverbBatch(&trace, b, &s_histograms[b]);
    }
    FitTimes(s_histograms, 4, forward);
    for (uint32_t b = 4; b-- > 0;)
    {
        maudTraceReverbBatch(&trace, b, &s_histograms[b]);
    }
    FitTimes(s_histograms, 4, backward);
    CHECK(forward[1] == backward[1], "the same times in any order");
    CHECK(maudReverbBatches(256) == 4 && maudReverbBatches(257) == 5 && maudReverbBatches(0) == 0,
          "batches of 64");
    maudDestroyAcousticScene(box);
}

// A floor (y = 0) as two triangles 2 km wide, facing down (away from
// the listener above it): one lossless, fully scattering bounce sends
// the listener the integral over the lower hemisphere of (cos / pi) /
// (4 pi max(h / cos, 1)^2), which is (1 / 2 pi) (h^2 / 4 + (1 - h^2) / 2)
// for h below 1 m and 1 / (8 pi h^2) above. Its field arrives from
// below: above 1 m the energy-weighted mean of the cosine is (1 / 5) /
// (1 / 4), so Z over W is -0.8, X and Y nothing.
static void TestSingleBounce(void)
{
    static const maudVector3 v[4] = {
        {-1000, 0, -1000}, {1000, 0, -1000}, {-1000, 0, 1000}, {1000, 0, 1000}};
    static const uint32_t faces[6] = {0, 1, 2, 1, 3, 2};
    static const uint32_t materials[2] = {0, 0};
    maudMesh mesh = {v, 4, faces, materials, 2};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* floor = nullptr;
    CHECK(maudCreateAcousticScene(&def, &floor) == maud_success, "a floor");
    maudAcousticMaterial lossless = Material(0.0f, 1.0f);
    static float field[4 * 3 * 100];
    const double heights[2] = {0.5, 2.0};
    for (int k = 0; k < 2; ++k)
    {
        double h = heights[k];
        maudReverbTrace trace = {maudSceneClosestHit,
                                 maudSceneAnyHit,
                                 floor,
                                 &lossless,
                                 1,
                                 {0, 0, 0},
                                 {0.3f, (float)h, -0.2f},
                                 4096,
                                 1,
                                 1,
                                 100};
        double total = 0.0;
        double channels[4] = {0.0};
        uint32_t first = MAUD_REVERB_BINS;
        for (uint32_t b = 0; b < 64; ++b)
        {
            s_histograms[0].field = field;
            maudTraceReverbBatch(&trace, b, &s_histograms[0]);
            for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
            {
                double e = (double)s_histograms[0].energy[1][i];
                total += e;
                first = e > 0.0 && i < first ? i : first;
            }
            for (int c = 0; c < 4; ++c)
            {
                for (uint32_t i = 0; i < 100; ++i)
                {
                    channels[c] += (double)field[(c * 3 + 1) * 100 + i];
                }
            }
        }
        double integral =
            h < 1.0 ? (h * h / 4.0 + (1.0 - h * h) / 2.0) / (2.0 * PI) : 1.0 / (8.0 * PI * h * h);
        printf("floor at %.1f m: energy %.5f, expected %.5f; field W %.5f Y %.5f Z %.5f X %.5f\n",
               h, total, integral, channels[0], channels[1], channels[2], channels[3]);
        CHECK(fabs(total / integral - 1.0) < 0.02, "the shading integral");
        CHECK(fabs(channels[0] / total - 1.0) < 1e-4, "the field's W is the energy");
        CHECK(fabs(channels[1] / total) < 0.01 && fabs(channels[3] / total) < 0.01,
              "nothing from the sides");
        if (h > 1.0)
        {
            CHECK(fabs(channels[2] / total + 0.8) < 0.01, "from below");
        }
        // The straight path down and back: 2h at 343 m/s.
        CHECK(first == (uint32_t)(2.0 * h / 343.0 / 0.01), "the first bin");
    }
    maudDestroyAcousticScene(floor);
}

// Every shadow blocked: nothing reaches the listener.
static void Always(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    (void)rays;
    (void)context;
    for (uint32_t i = 0; i < count; ++i)
    {
        occluded[i] = 1;
    }
}

static void TestBlocked(void)
{
    maudAcousticScene* box = Box(5, 4, 3);
    maudAcousticMaterial m[6];
    for (int f = 0; f < 6; ++f)
    {
        m[f] = Material(0.1f, 0.5f);
    }
    maudReverbTrace trace = {maudSceneClosestHit,   Always, box, m, 6, {0, 0, 0},
                             {1.85f, 1.64f, 1.35f}, 64,     512, 0, 0};
    maudTraceReverbBatch(&trace, 0, &s_histograms[0]);
    double total = 0.0;
    for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
    {
        total += (double)s_histograms[0].energy[0][i];
    }
    CHECK(total == 0.0, "blocked shadows bring nothing");
    maudDestroyAcousticScene(box);
}

// Rays cut at 64 bounces (0.4 s into a 1 s office): the bins past the
// cut filled in at the rate of those before give the time (measured:
// 1.007 s; 0.83 s with nothing filled in).
static void TestTruncated(void)
{
    maudAcousticScene* box = Box(5, 4, 3);
    maudAcousticMaterial m[6];
    for (int f = 0; f < 6; ++f)
    {
        m[f] = Material(0.1f, 0.5f);
    }
    maudReverbTrace trace = {maudSceneClosestHit,
                             maudSceneAnyHit,
                             box,
                             m,
                             6,
                             {0, 0, 0},
                             {1.85f, 1.64f, 1.35f},
                             1024,
                             64,
                             0,
                             0};
    for (uint32_t b = 0; b < 16; ++b)
    {
        maudTraceReverbBatch(&trace, b, &s_histograms[b]);
    }
    CHECK(s_histograms[0].truncated < 1.0f, "rays were cut");
    float times[3];
    FitTimes(s_histograms, 16, times);
    printf("office cut at 64 bounces: %.3f s\n", (double)times[1]);
    CHECK(fabs((double)times[1] / 0.99 - 1.0) < 0.05, "the truncated decay's time");
    maudDestroyAcousticScene(box);
}

// Synthetic histograms: an exponential of 1.5 s, one slope; bins falling
// at 0.5 s to -25 dB and then at 3 s, two slopes (one fitted from -5 to
// -25 dB would read 2.03 s), the slower with a few percent of the
// energy.
static void TestFit(void)
{
    for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
    {
        double t = (double)i * 0.01;
        s_histograms[0].energy[0][i] = (float)pow(10.0, -6.0 * t / 1.5);
        double knee = 0.5 * 25.0 / 60.0;
        double db = t < knee ? -60.0 * t / 0.5 : -25.0 - 60.0 * (t - knee) / 3.0;
        s_histograms[0].energy[1][i] = (float)pow(10.0, db / 10.0);
        s_histograms[0].energy[2][i] = 0.0f;
    }
    s_histograms[0].truncated = INFINITY;
    maudReverbFit fit;
    maudFitReverb(s_histograms, 1, &fit);
    printf("fits: exponential %.3f s, double slope %.3f s and %.3f s (%.1f %%)\n",
           (double)fit.times[0], (double)fit.times[1], (double)fit.tailTimes[1],
           100.0 * (double)fit.tailShares[1]);
    CHECK(fabs((double)fit.times[0] / 1.5 - 1.0) < 0.01 && fit.tailTimes[0] == 0.0f &&
              fit.tailShares[0] == 0.0f,
          "an exponential: its time, no tail");
    CHECK(fit.times[1] > 0.4f && fit.times[1] < 0.55f && fit.tailTimes[1] > 2.7f &&
              fit.tailTimes[1] < 3.3f,
          "the double slope's two times");
    CHECK(fit.tailShares[1] > 0.02f && fit.tailShares[1] < 0.07f, "the slower's share");
    CHECK(fit.times[2] == 0.1f && fit.tailTimes[2] == 0.0f, "no energy: the floor, no tail");
}

// Two slopes' bins, 0.4 s and 2 s, the slower 20 dB down: each slope's
// level makes the reverb give that slope's energy at the matching time.
static void TestTailLevels(void)
{
    const double k = 13.815510557964274;
    for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
    {
        double t = ((double)i + 0.5) * 0.01;
        for (int b = 0; b < 3; ++b)
        {
            s_histograms[0].energy[b][i] = (float)(exp(-k * t / 0.4) + 0.01 * exp(-k * t / 2.0));
        }
    }
    s_histograms[0].truncated = INFINITY;
    maudReverbFit fit;
    maudFitReverb(s_histograms, 1, &fit);
    float levels[3];
    float tailLevels[3];
    maudReverbLevels(&s_histograms[0], &fit, 0.1f, 0.0f, levels, tailLevels);
    // The reverb gives 0.0144 exp(-k (t - delay) / T) per bin at level 0.
    double fast =
        0.0144 * exp(-k * 0.1 / (double)fit.times[1]) * pow(10.0, (double)levels[1] / 10.0);
    double slow =
        0.0144 * exp(-k * 0.1 / (double)fit.tailTimes[1]) * pow(10.0, (double)tailLevels[1] / 10.0);
    printf("tail levels: %.3f s at %.2f dB, %.3f s at %.2f dB\n", (double)fit.times[1],
           (double)levels[1], (double)fit.tailTimes[1], (double)tailLevels[1]);
    CHECK(fabs((double)fit.times[1] / 0.4 - 1.0) < 0.05 &&
              fabs((double)fit.tailTimes[1] / 2.0 - 1.0) < 0.05,
          "both times found");
    CHECK(fabs(10.0 * log10(fast / exp(-k * 0.1 / 0.4))) < 1.0 &&
              fabs(10.0 * log10(slow / (0.01 * exp(-k * 0.1 / 2.0)))) < 1.0,
          "each slope's level gives its energy");
}

// Research 34's two rooms: one live (absorption 0.05 to 0.08), one
// damped (0.4 to 0.6), 6 by 3 by 4 m each, joined by a door 1 m wide and
// 2.1 m high. In the damped room the decay has the live room's slow
// slope behind its own fast one, and two slopes follow it to -40 dB
// within a few dB; in the live room one slope stays.
static maudAcousticScene* CoupledRooms(void)
{
    static maudVector3 v[8 * 13];
    static uint32_t indices[36 * 13];
    static uint32_t materials[12 * 13];
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const float boxes[13][7] = {
        {0, -0.1f, 0, 6, 0, 4, 0},
        {6, -0.1f, 0, 12, 0, 4, 1},
        {0, 3, 0, 6, 3.1f, 4, 0},
        {6, 3, 0, 12, 3.1f, 4, 1},
        {0, 0, -0.1f, 6, 3, 0, 0},
        {6, 0, -0.1f, 12, 3, 0, 1},
        {0, 0, 4, 6, 3, 4.1f, 0},
        {6, 0, 4, 12, 3, 4.1f, 1},
        {-0.1f, 0, 0, 0, 3, 4, 0},
        {12, 0, 0, 12.1f, 3, 4, 1},
        {5.95f, 0, 0, 6.05f, 3, 1.5f, 0},
        {5.95f, 0, 2.5f, 6.05f, 3, 4, 0},
        {5.95f, 2.1f, 1.5f, 6.05f, 3, 2.5f, 0},
    };
    for (uint32_t n = 0; n < 13; ++n)
    {
        const float* b = boxes[n];
        for (uint32_t i = 0; i < 8; ++i)
        {
            v[n * 8 + i] =
                (maudVector3){(i & 1) ? b[3] : b[0], (i & 2) ? b[4] : b[1], (i & 4) ? b[5] : b[2]};
        }
        for (uint32_t i = 0; i < 36; ++i)
        {
            indices[n * 36 + i] = n * 8 + faces[i];
        }
        for (uint32_t i = 0; i < 12; ++i)
        {
            materials[n * 12 + i] = (uint32_t)b[6];
        }
    }
    maudMesh mesh = {v, 8 * 13, indices, materials, 12 * 13};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "the two rooms");
    return scene;
}

// How far two slopes' decay is from a band's traced decay down to -40 dB
// (dB, RMS).
static double MissOf(const float* energy, float time, float tailTime, float share)
{
    double decay[MAUD_REVERB_BINS];
    double sum = 0.0;
    for (uint32_t i = MAUD_REVERB_BINS; i-- > 0;)
    {
        sum += (double)energy[i];
        decay[i] = sum;
    }
    double squares = 0.0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < MAUD_REVERB_BINS && decay[i] / decay[0] >= 1e-4; ++i, ++n)
    {
        double t = (double)i * 0.01;
        double model = (1.0 - (double)share) * exp(-13.815510557964274 * t / (double)time);
        model +=
            tailTime > 0.0f ? (double)share * exp(-13.815510557964274 * t / (double)tailTime) : 0.0;
        double r = 10.0 * log10(model / (decay[i] / decay[0]));
        squares += r * r;
    }
    return n > 0 ? sqrt(squares / (double)n) : 0.0;
}

static void TestCoupledRooms(void)
{
    maudAcousticScene* rooms = CoupledRooms();
    maudAcousticMaterial materials[2] = {{{0.05f, 0.05f, 0.08f}, 0.3f, {0, 0, 0}},
                                         {{0.4f, 0.5f, 0.6f}, 0.3f, {0, 0, 0}}};
    const maudVector3 at[2] = {{8.1f, 1.5f, 2.0f}, {2.0f, 1.5f, 2.0f}};
    for (int room = 0; room < 2; ++room)
    {
        maudReverbTrace trace = {maudSceneClosestHit,
                                 maudSceneAnyHit,
                                 rooms,
                                 materials,
                                 2,
                                 {0, 0, 0},
                                 at[room],
                                 4096,
                                 512,
                                 0,
                                 0};
        uint32_t batches = maudReverbBatches(trace.rays);
        for (uint32_t b = 0; b < batches; ++b)
        {
            maudTraceReverbBatch(&trace, b, &s_histograms[b]);
        }
        maudReverbFit fit;
        maudFitReverb(s_histograms, batches, &fit);
        for (int b = 0; b < 2; ++b)
        {
            double miss = MissOf(s_histograms[0].energy[b], fit.times[b], fit.tailTimes[b],
                                 fit.tailShares[b]);
            printf("%s room, band %d: %.3f s, tail %.3f s (%.2f %%), %.2f dB from the decay\n",
                   room == 0 ? "damped" : "live", b, (double)fit.times[b], (double)fit.tailTimes[b],
                   100.0 * (double)fit.tailShares[b], miss);
            if (room == 0)
            {
                CHECK(fit.times[b] < 0.35f && fit.tailTimes[b] > 0.7f && fit.tailTimes[b] < 1.6f,
                      "the damped room's own slope and the live room's behind it");
                CHECK(miss < 4.5, "two slopes follow the decay to -40 dB");
            }
        }
        if (room == 1)
        {
            CHECK(fit.tailTimes[1] == 0.0f, "the live room keeps one slope");
        }
    }
    maudDestroyAcousticScene(rooms);
}

static uint64_t Hash(uint64_t hash, const void* bytes, size_t size)
{
    const unsigned char* b = bytes;
    for (size_t i = 0; i < size; ++i)
    {
        hash = (hash ^ b[i]) * 1099511628211u;
    }
    return hash;
}

static void TestSealed(void)
{
    maudAcousticScene* scene = Box(5.0f, 3.0f, 4.0f);
    maudAcousticMaterial materials[6];
    for (int m = 0; m < 6; ++m)
    {
        materials[m] = Material(0.05f + 0.07f * (float)m, 0.2f + 0.1f * (float)m);
    }
    float air[3];
    maudAirAbsorptionOf(20.0, 50.0, air);
    static float field[4 * 3 * 100 * 8];
    maudReverbTrace trace = {maudSceneClosestHit,
                             maudSceneAnyHit,
                             scene,
                             materials,
                             6,
                             {air[0], air[1], air[2]},
                             {2.1f, 1.4f, 1.7f},
                             512,
                             512,
                             1,
                             100};
    uint32_t batches = maudReverbBatches(trace.rays);
    for (uint32_t b = 0; b < batches; ++b)
    {
        s_histograms[b].field = field + (size_t)b * 4 * 3 * 100;
        maudTraceReverbBatch(&trace, b, &s_histograms[b]);
    }
    uint64_t hash = 1469598103934665603u;
    hash = Hash(hash, air, sizeof(air));
    for (uint32_t b = 0; b < batches; ++b)
    {
        hash = Hash(hash, s_histograms[b].energy, sizeof(s_histograms[b].energy));
    }
    hash = Hash(hash, field, (size_t)batches * 4 * 3 * 100 * sizeof(float));
    maudReverbFit fit;
    maudFitReverb(s_histograms, batches, &fit);
    float times[3];
    memcpy(times, fit.times, sizeof(times));
    maudSumReverbFields(&trace, s_histograms, batches);
    float levels[3];
    float tailLevels[3];
    maudReverbLevels(&s_histograms[0], &fit, 0.1f, 0.0f, levels, tailLevels);
    hash = Hash(hash, times, sizeof(times));
    hash = Hash(hash, levels, sizeof(levels));
    printf("sealed: %.4f / %.4f / %.4f s, %.3f / %.3f / %.3f dB, hash %016llx\n", (double)times[0],
           (double)times[1], (double)times[2], (double)levels[0], (double)levels[1],
           (double)levels[2], (unsigned long long)hash);
    CHECK(hash == 0x5d10e77d43f2b628u, "the same bits on every platform");
    for (uint32_t b = 0; b < batches; ++b)
    {
        s_histograms[b].field = nullptr;
    }
    maudDestroyAcousticScene(scene);
}

int main(void)
{
    TestSingleBounce();
    TestBlocked();
    TestTruncated();
    TestFit();
    TestTailLevels();
    TestCoupledRooms();
    TestRooms();
    TestBandsAndAir();
    TestLimits();
    TestOrder();
    TestSealed();
    return s_failures == 0 ? 0 : 1;
}
