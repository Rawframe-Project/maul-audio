// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Occlusion through the host's any-hit query, against an analytic scene:
// a half-wall in the plane z = -5 that blocks where x < 0, and on demand
// a full wall at z = -9.5. One ray gives 0 or 1; a volumetric source
// passing the half-wall's edge turns from occluded to clear steadily and
// within the research's error of the exact share of its sphere; points
// the source itself cannot see do not count, and one that sees none is
// occluded; sources side by side count only their own samples; the
// task hooks, split any way, give the same bytes as a serial step, in
// batches of at most 64; without a query every path is clear.

#include "test_harness.h"

#include "maul-audio/spatializer.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846

static bool s_fullWall;
// Every ray blocked, as for a source buried in solid geometry.
static bool s_buried;
static long s_rays;
static uint32_t s_largestBatch;

// Whether a ray crosses the plane z = at within its distances where
// blocked says it is solid.
static bool Crosses(const maudRay* ray, double at, bool half)
{
    double dz = (double)ray->direction.z;
    if (dz == 0.0)
    {
        return false;
    }
    double t = (at - (double)ray->origin.z) / dz;
    if (!(t > (double)ray->minDistance) || !(t <= (double)ray->maxDistance))
    {
        return false;
    }
    double x = (double)ray->origin.x + t * (double)ray->direction.x;
    return !half || x < 0.0;
}

static void AnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    (void)context;
    s_rays += count;
    s_largestBatch = count > s_largestBatch ? count : s_largestBatch;
    for (uint32_t i = 0; i < count; ++i)
    {
        occluded[i] = s_buried || Crosses(&rays[i], -5.0, true) ||
                      (s_fullWall && Crosses(&rays[i], -9.5, false));
    }
}

// A task system that runs ranges of one item, last first.
static long s_enqueued;

static void* Enqueue(maudTaskFn* task, uint32_t itemCount, uint32_t minRange, void* taskContext,
                     void* userContext)
{
    (void)minRange;
    (void)userContext;
    s_enqueued += 1;
    for (uint32_t i = itemCount; i-- > 0;)
    {
        task(i, i + 1, taskContext);
    }
    return &s_enqueued;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
}

static maudSpatializer* Create(bool tasks, bool query)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.sourceCapacity = 64;
    def.maxOcclusionSamples = 128;
    def.anyHit = query ? AnyHit : nullptr;
    def.enqueueTask = tasks ? Enqueue : nullptr;
    def.finishTask = tasks ? Finish : nullptr;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    return s;
}

static maudSourceId Source(maudSpatializer* s, maudOcclusionMethod method, uint32_t samples)
{
    maudSourceDef def = maudDefaultSourceDef();
    def.occlusion = method;
    def.occlusionSamples = samples;
    maudSourceId id = {0, 0};
    CHECK(maudCreateSource(s, &def, &id) == maud_success, "a source");
    return id;
}

static float Occlusion(maudSpatializer* s, maudSourceId id, maudVector3 at, maudVector3 listener)
{
    maudPose pose = {at, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudPose from = {listener, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudDirectResult r = {0};
    CHECK(maudSetSourcePose(s, id, &pose) == maud_success &&
              maudSimulateDirect(s, &from) == maud_success && maudLatchResults(s) > 0 &&
              maudGetDirectResult(s, id, &r) == maud_success,
          "a result");
    return r.occlusion;
}

static void TestRay(void)
{
    maudSpatializer* s = Create(false, true);
    maudSourceId id = Source(s, maud_occlusionRay, 0);
    maudVector3 origin = {0.0f, 0.0f, 0.0f};
    CHECK(Occlusion(s, id, (maudVector3){-3.0f, 0.0f, -10.0f}, origin) == 1.0f,
          "behind the wall: occluded");
    CHECK(Occlusion(s, id, (maudVector3){3.0f, 0.0f, -10.0f}, origin) == 0.0f, "beside it: clear");
    CHECK(Occlusion(s, id, (maudVector3){-3.0f, 0.0f, -4.0f}, origin) == 0.0f,
          "in front of it: clear");
    maudDestroySpatializer(s);
}

// The share of a unit ball's volume beyond a plane at distance c from
// its centre (c > 0 on the ball's side).
static double Share(double c)
{
    double h = fmin(fmax(1.0 + c, 0.0), 2.0);
    return h * h * (3.0 - h) / 4.0;
}

static void TestVolumetric(void)
{
    maudSpatializer* s = Create(false, true);
    maudSourceId id = Source(s, maud_occlusionVolumetric, 32);
    // The listener far off behind the source's axis: rays nearly parallel.
    maudVector3 listener = {0.0f, 0.0f, 2000.0f};
    float previous = 1.0f;
    bool steady = true;
    double worst = 0.0;
    for (int i = 0; i <= 60; ++i)
    {
        float x = -1.5f + 0.05f * (float)i;
        float occlusion = Occlusion(s, id, (maudVector3){x, 0.0f, -10.0f}, listener);
        steady = steady && occlusion <= previous;
        previous = occlusion;
        worst = fmax(worst, fabs((double)occlusion - Share(-(double)x)));
    }
    printf("volumetric against the exact share: %.3f worst\n", worst);
    CHECK(steady, "passing the edge, the occlusion only falls");
    // 0.085 measured; points crowding the centre (radii not spread by
    // volume) give 0.165.
    CHECK(worst < 0.12, "within the sampled error of the exact share");
    // A wall right in front of the source cuts its sphere: the points
    // beyond the wall do not count, and the source stays occluded.
    s_fullWall = true;
    float cut =
        Occlusion(s, id, (maudVector3){5.0f, 0.0f, -10.0f}, (maudVector3){5.0f, 0.0f, 0.0f});
    s_fullWall = false;
    CHECK(cut == 1.0f, "points the source cannot see do not count");
    maudDestroySpatializer(s);
}

static void Step(maudSpatializer* s, const maudSourceId* ids, int count, maudDirectResult* out)
{
    for (int i = 0; i < count; ++i)
    {
        float a = (float)i * 0.7f;
        maudPose pose = {{6.0f * cosf(a), 0.3f * (float)i, -10.0f + 6.0f * sinf(a)},
                         {0.0f, 0.0f, 0.0f, 1.0f}};
        CHECK(maudSetSourcePose(s, ids[i], &pose) == maud_success, "pose");
    }
    maudPose listener = {{0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSimulateDirect(s, &listener) == maud_success && maudLatchResults(s) > 0, "step");
    for (int i = 0; i < count; ++i)
    {
        CHECK(maudGetDirectResult(s, ids[i], &out[i]) == maud_success, "result");
    }
}

static void TestSplit(void)
{
    enum
    {
        COUNT = 40
    };
    maudSpatializer* serial = Create(false, true);
    maudSpatializer* split = Create(true, true);
    maudSourceId a[COUNT];
    maudSourceId b[COUNT];
    long expected = 0;
    for (int i = 0; i < COUNT; ++i)
    {
        uint32_t samples = 64 + (uint32_t)(i * 7 % 64);
        maudOcclusionMethod method = i % 3 == 0 ? maud_occlusionRay : maud_occlusionVolumetric;
        a[i] = Source(serial, method, samples);
        b[i] = Source(split, method, samples);
        expected += method == maud_occlusionRay ? 1 : 2 * (long)samples;
    }
    // A destroyed source casts nothing.
    maudSourceId gone = Source(serial, maud_occlusionVolumetric, 100);
    CHECK(maudDestroySource(serial, gone) == maud_success, "destroy");
    static maudDirectResult x[COUNT];
    static maudDirectResult y[COUNT];
    s_rays = 0;
    s_largestBatch = 0;
    Step(serial, a, COUNT, x);
    CHECK(s_rays == expected, "every source's rays, once");
    CHECK(s_largestBatch <= 64, "in batches of at most 64");
    s_enqueued = 0;
    Step(split, b, COUNT, y);
    CHECK(s_enqueued > 1, "the rays took more than one round of tasks");
    CHECK(memcmp(x, y, sizeof(x)) == 0, "a split step gives the same bytes");
    maudDestroySpatializer(serial);
    maudDestroySpatializer(split);
}

static void TestNoQuery(void)
{
    maudSpatializer* s = Create(false, false);
    maudSourceId id = Source(s, maud_occlusionVolumetric, 16);
    CHECK(Occlusion(s, id, (maudVector3){-3.0f, 0.0f, -10.0f}, (maudVector3){0.0f, 0.0f, 0.0f}) ==
              0.0f,
          "without a query the path is clear");
    maudSourceDef def = maudDefaultSourceDef();
    def.occlusion = maud_occlusionVolumetric;
    def.occlusionSamples = 129;
    CHECK(maudCreateSource(s, &def, &id) == maud_errorInvalid, "more points than the limit");
    def.occlusionSamples = 8;
    def.occlusionRadius = 0.0f;
    CHECK(maudCreateSource(s, &def, &id) == maud_errorInvalid, "no radius");
    def.occlusionRadius = 1.0f;
    def.occlusion = 3;
    CHECK(maudCreateSource(s, &def, &id) == maud_errorInvalid, "an unknown method");
    maudDestroySpatializer(s);
    maudSpatializerDef bad = maudDefaultSpatializerDef();
    bad.enqueueTask = Enqueue;
    maudSpatializer* none = nullptr;
    CHECK(maudCreateSpatializer(&bad, &none) == maud_errorInvalid, "one task hook alone");
    bad = maudDefaultSpatializerDef();
    bad.maxOcclusionSamples = 2000;
    CHECK(maudCreateSpatializer(&bad, &none) == maud_errorInvalid, "too many points");
}

// Two volumetric sources side by side in the samples' buffer, one clear
// beside the half-wall and one wholly behind it: each counts only its
// own samples.
static void TestNeighbours(void)
{
    maudSpatializer* s = Create(false, true);
    maudSourceId first = Source(s, maud_occlusionVolumetric, 32);
    maudSourceId second = Source(s, maud_occlusionVolumetric, 32);
    maudVector3 origin = {0.0f, 0.0f, 0.0f};
    maudPose behind = {{-5.0f, 0.0f, -10.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudDirectResult r = {0};
    CHECK(maudSetSourcePose(s, second, &behind) == maud_success &&
              Occlusion(s, first, (maudVector3){5.0f, 0.0f, -10.0f}, origin) == 0.0f &&
              maudGetDirectResult(s, second, &r) == maud_success && r.occlusion == 1.0f,
          "the first clear, the second occluded");
    CHECK(maudSetSourcePose(s, first, &behind) == maud_success &&
              Occlusion(s, second, (maudVector3){5.0f, 0.0f, -10.0f}, origin) == 0.0f &&
              maudGetDirectResult(s, first, &r) == maud_success && r.occlusion == 1.0f,
          "and the other way round");
    s_buried = true;
    CHECK(Occlusion(s, second, (maudVector3){5.0f, 0.0f, -10.0f}, origin) == 1.0f,
          "a source that sees none of its sphere: occluded");
    s_buried = false;
    maudDestroySpatializer(s);
}

int main(void)
{
    TestRay();
    TestVolumetric();
    TestNeighbours();
    TestSplit();
    TestNoQuery();
    return s_failures == 0 ? 0 : 1;
}
