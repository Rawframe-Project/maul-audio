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
// compensated); batches split any way sum the same; unknown materials
// end a ray.

#include "reverb_estimate.h"
#include "test_harness.h"

#include "maul-audio/scene.h"

#include <math.h>
#include <stdlib.h>

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

static void Estimate(maudAcousticScene* scene, const maudAcousticMaterial* materials,
                     maudVector3 listener, const float* air, uint32_t rays, float* times)
{
    maudReverbTrace trace = {maudSceneClosestHit,      maudSceneAnyHit, scene, materials, 6,
                             {air[0], air[1], air[2]}, listener,        rays,  512};
    uint32_t batches = maudReverbBatches(rays);
    for (uint32_t b = 0; b < batches; ++b)
    {
        maudTraceReverbBatch(&trace, b, &s_histograms[b]);
    }
    maudFitReverb(s_histograms, batches, times);
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
    maudReverbTrace open = {Nothing, Never, nullptr, &hard, 1, {0, 0, 0}, {0, 0, 0}, 128, 512};
    float times[3];
    for (uint32_t b = 0; b < 2; ++b)
    {
        maudTraceReverbBatch(&open, b, &s_histograms[b]);
    }
    maudFitReverb(s_histograms, 2, times);
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
    maudReverbTrace unknown = {maudSceneClosestHit, maudSceneAnyHit,       box, lossless, 0,
                               {0, 0, 0},           {1.85f, 1.64f, 1.35f}, 64,  512};
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
    maudReverbTrace trace = {maudSceneClosestHit, maudSceneAnyHit,       box, m,  6,
                             {0, 0, 0},           {1.85f, 1.64f, 1.35f}, 256, 512};
    float forward[3];
    float backward[3];
    for (uint32_t b = 0; b < 4; ++b)
    {
        maudTraceReverbBatch(&trace, b, &s_histograms[b]);
    }
    maudFitReverb(s_histograms, 4, forward);
    for (uint32_t b = 4; b-- > 0;)
    {
        maudTraceReverbBatch(&trace, b, &s_histograms[b]);
    }
    maudFitReverb(s_histograms, 4, backward);
    CHECK(forward[1] == backward[1], "the same times in any order");
    CHECK(maudReverbBatches(256) == 4 && maudReverbBatches(257) == 5 && maudReverbBatches(0) == 0,
          "batches of 64");
    maudDestroyAcousticScene(box);
}

int main(void)
{
    TestRooms();
    TestBandsAndAir();
    TestLimits();
    TestOrder();
    return s_failures == 0 ? 0 : 1;
}
