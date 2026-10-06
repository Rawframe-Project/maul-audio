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
// spans -5 to -25 dB of a double slope.

#include "reverb_estimate.h"
#include "test_harness.h"

#include "maul-audio/scene.h"

#include <math.h>
#include <stdlib.h>

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

// A floor as two triangles 2 km wide, facing down (away from the
// listener above it): one lossless, fully scattering bounce sends the
// listener, from rays over the sphere, N / (4 pi) times the integral
// over the lower hemisphere of (cos / pi) / (4 pi max(h / cos, 1)^2),
// which is (N / 4 pi) (1 / 2 pi) (h^2 / 4 + (1 - h^2) / 2) for h below
// 1 m and (N / 4 pi) / (8 pi h^2) above.
static void TestSingleBounce(void)
{
    static const maudVector3 v[4] = {
        {-1000, -1000, 0}, {1000, -1000, 0}, {-1000, 1000, 0}, {1000, 1000, 0}};
    static const uint32_t faces[6] = {0, 2, 1, 1, 2, 3};
    static const uint32_t materials[2] = {0, 0};
    maudMesh mesh = {v, 4, faces, materials, 2};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* floor = nullptr;
    CHECK(maudCreateAcousticScene(&def, &floor) == maud_success, "a floor");
    maudAcousticMaterial lossless = Material(0.0f, 1.0f);
    const double heights[2] = {0.5, 2.0};
    for (int k = 0; k < 2; ++k)
    {
        double h = heights[k];
        maudReverbTrace trace = {maudSceneClosestHit, maudSceneAnyHit,         floor, &lossless, 1,
                                 {0, 0, 0},           {0.3f, -0.2f, (float)h}, 4096,  1};
        double total = 0.0;
        uint32_t first = MAUD_REVERB_BINS;
        for (uint32_t b = 0; b < 64; ++b)
        {
            maudTraceReverbBatch(&trace, b, &s_histograms[0]);
            for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
            {
                double e = (double)s_histograms[0].energy[1][i];
                total += e;
                first = e > 0.0 && i < first ? i : first;
            }
        }
        double integral =
            h < 1.0 ? (h * h / 4.0 + (1.0 - h * h) / 2.0) / (2.0 * PI) : 1.0 / (8.0 * PI * h * h);
        double expected = 4096.0 / (4.0 * PI) * integral;
        printf("floor at %.1f m: energy %.4f, expected %.4f\n", h, total, expected);
        CHECK(fabs(total / expected - 1.0) < 0.02, "the shading integral");
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
                             {1.85f, 1.64f, 1.35f}, 64,     512};
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
    maudReverbTrace trace = {maudSceneClosestHit, maudSceneAnyHit,       box,  m, 6,
                             {0, 0, 0},           {1.85f, 1.64f, 1.35f}, 1024, 64};
    for (uint32_t b = 0; b < 16; ++b)
    {
        maudTraceReverbBatch(&trace, b, &s_histograms[b]);
    }
    CHECK(s_histograms[0].truncated < 1.0f, "rays were cut");
    float times[3];
    maudFitReverb(s_histograms, 16, times);
    printf("office cut at 64 bounces: %.3f s\n", (double)times[1]);
    CHECK(fabs((double)times[1] / 0.99 - 1.0) < 0.05, "the truncated decay's time");
    maudDestroyAcousticScene(box);
}

// Synthetic histograms: an exponential of 1.5 s; a decay at 0.5 s to
// -15 dB and then at 3 s, whose -5 to -25 dB fit lies well above the
// first slope (a fit only to -15 dB would read it).
static void TestFit(void)
{
    for (uint32_t i = 0; i < MAUD_REVERB_BINS; ++i)
    {
        double t = (double)i * 0.01;
        s_histograms[0].energy[0][i] = (float)pow(10.0, -6.0 * t / 1.5);
        double knee = 0.5 * 15.0 / 60.0;
        double db = t < knee ? -60.0 * t / 0.5 : -15.0 - 60.0 * (t - knee) / 3.0;
        s_histograms[0].energy[1][i] = (float)pow(10.0, db / 10.0);
        s_histograms[0].energy[2][i] = 0.0f;
    }
    s_histograms[0].truncated = INFINITY;
    float times[3];
    maudFitReverb(s_histograms, 1, times);
    printf("fits: exponential %.3f s, double slope %.3f s\n", (double)times[0], (double)times[1]);
    CHECK(fabs((double)times[0] / 1.5 - 1.0) < 0.01, "an exponential's time");
    CHECK(times[1] > 0.75f, "the double slope's -5 to -25 dB");
    CHECK(times[2] == 0.1f, "no energy: the floor");
}

int main(void)
{
    TestSingleBounce();
    TestBlocked();
    TestTruncated();
    TestFit();
    TestRooms();
    TestBandsAndAir();
    TestLimits();
    TestOrder();
    return s_failures == 0 ? 0 : 1;
}
