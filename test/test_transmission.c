// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Transmission through the host's closest-hit query, against an analytic
// scene: a half-wall at z = -5 (x < 0, material 0) and a full wall at
// z = -7 (material 1, or an index past the table). An occluded path
// passes the product of its surfaces' transmission; the surface limit
// stops the walk and says so; an unknown material lets nothing through;
// clear paths and sources without a walk cast no closest-hit rays, and
// an occluded path without one passes nothing; a split step gives the
// same bytes; bad materials are refused.

#include "test_harness.h"

#include "maul-audio/spatializer.h"

#include <math.h>
#include <string.h>

static uint32_t s_farMaterial = 1;
// A careless host that ignores rays' minimum distances.
static bool s_careless;
static long s_closest;

static void AnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    (void)context;
    for (uint32_t i = 0; i < count; ++i)
    {
        const maudRay* r = &rays[i];
        double dz = (double)r->direction.z;
        bool hit = false;
        if (dz != 0.0)
        {
            double t = (-5.0 - (double)r->origin.z) / dz;
            hit = t > (double)r->minDistance && t <= (double)r->maxDistance &&
                  (double)r->origin.x + t * (double)r->direction.x < 0.0;
            double u = (-7.0 - (double)r->origin.z) / dz;
            hit = hit || (u > (double)r->minDistance && u <= (double)r->maxDistance);
        }
        occluded[i] = hit;
    }
}

static void ClosestHit(const maudRay* rays, uint32_t count, maudRayHit* hits, void* context)
{
    (void)context;
    s_closest += count;
    for (uint32_t i = 0; i < count; ++i)
    {
        const maudRay* r = &rays[i];
        hits[i] = (maudRayHit){INFINITY, {0.0f, 0.0f, 1.0f}, 0};
        double dz = (double)r->direction.z;
        if (dz == 0.0)
        {
            continue;
        }
        double t = (-5.0 - (double)r->origin.z) / dz;
        double least = s_careless ? 0.0 : (double)r->minDistance;
        if (t >= least && t <= (double)r->maxDistance &&
            (double)r->origin.x + t * (double)r->direction.x < 0.0)
        {
            hits[i].distance = (float)t;
            continue;
        }
        double u = (-7.0 - (double)r->origin.z) / dz;
        if (u >= (double)r->minDistance && u <= (double)r->maxDistance)
        {
            hits[i].distance = (float)u;
            hits[i].material = s_farMaterial;
        }
    }
}

static void* Enqueue(maudTaskFn* task, uint32_t itemCount, uint32_t minRange, void* taskContext,
                     void* userContext)
{
    (void)minRange;
    (void)userContext;
    for (uint32_t i = itemCount; i-- > 0;)
    {
        task(i, i + 1, taskContext);
    }
    return taskContext;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
}

static const maudAcousticMaterial s_materials[2] = {
    {{0.1f, 0.1f, 0.1f}, 0.2f, {0.5f, 0.4f, 0.3f}},
    {{0.1f, 0.1f, 0.1f}, 0.2f, {0.9f, 0.8f, 0.7f}},
};

static maudSpatializer* Create(uint32_t maxSurfaces, bool tasks, bool closest)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = AnyHit;
    def.closestHit = closest ? ClosestHit : nullptr;
    def.maxSurfaces = maxSurfaces;
    def.enqueueTask = tasks ? Enqueue : nullptr;
    def.finishTask = tasks ? Finish : nullptr;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    CHECK(maudSetMaterials(s, s_materials, 2) == maud_success, "materials");
    return s;
}

static maudDirectResult Result(maudSpatializer* s, maudSourceId id, float x, float z)
{
    maudPose pose = {{x, 0.0f, z}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudPose listener = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudDirectResult r = {0};
    CHECK(maudSetSourcePose(s, id, &pose) == maud_success &&
              maudSimulateDirect(s, &listener) == maud_success && maudLatchResults(s) > 0 &&
              maudGetDirectResult(s, id, &r) == maud_success,
          "a result");
    return r;
}

static maudSourceId Source(maudSpatializer* s, bool transmission)
{
    maudSourceDef def = maudDefaultSourceDef();
    def.transmission = transmission;
    maudSourceId id = {0, 0};
    CHECK(maudCreateSource(s, &def, &id) == maud_success, "a source");
    return id;
}

static bool Bands(const maudDirectResult* r, float a, float b, float c)
{
    return fabsf(r->transmission[0] - a) < 1e-6f && fabsf(r->transmission[1] - b) < 1e-6f &&
           fabsf(r->transmission[2] - c) < 1e-6f;
}

static void TestWalk(void)
{
    maudSpatializer* s = Create(4, false, true);
    maudSourceId id = Source(s, true);
    maudDirectResult r = Result(s, id, -2.0f, -10.0f);
    CHECK(r.occlusion == 1.0f && r.surfaces == 2 && Bands(&r, 0.45f, 0.32f, 0.21f),
          "two walls: the product of both");
    r = Result(s, id, 2.0f, -10.0f);
    CHECK(r.surfaces == 1 && Bands(&r, 0.9f, 0.8f, 0.7f), "beside the half-wall: one wall");
    s_closest = 0;
    r = Result(s, id, 2.0f, -6.0f);
    CHECK(r.occlusion == 0.0f && Bands(&r, 1.0f, 1.0f, 1.0f) && s_closest == 0,
          "a clear path walks nothing");
    s_farMaterial = 7;
    r = Result(s, id, 2.0f, -10.0f);
    s_farMaterial = 1;
    CHECK(Bands(&r, 0.0f, 0.0f, 0.0f), "an unknown material lets nothing through");
    CHECK(maudDestroySource(s, id) == maud_success, "destroy");
    maudSourceId quiet = Source(s, false);
    s_closest = 0;
    r = Result(s, quiet, -2.0f, -10.0f);
    CHECK(r.occlusion == 1.0f && Bands(&r, 0.0f, 0.0f, 0.0f) && s_closest == 0,
          "a source without a walk passes nothing when occluded");
    maudDestroySpatializer(s);
    s = Create(1, false, true);
    id = Source(s, true);
    r = Result(s, id, -2.0f, -10.0f);
    CHECK(r.surfaces == 1 && Bands(&r, 0.5f, 0.4f, 0.3f), "the limit stops the walk, and says so");
    maudDestroySpatializer(s);
    // A hit before the ray's minimum distance is no hit: a careless host
    // returning the first wall again ends the walk rather than counting
    // it over and over.
    s = Create(4, false, true);
    id = Source(s, true);
    s_careless = true;
    r = Result(s, id, -2.0f, -10.0f);
    s_careless = false;
    CHECK(r.surfaces == 1 && Bands(&r, 0.5f, 0.4f, 0.3f), "a surface is not crossed twice");
    maudDestroySpatializer(s);
    s = Create(4, false, false);
    id = Source(s, true);
    r = Result(s, id, -2.0f, -10.0f);
    CHECK(Bands(&r, 0.0f, 0.0f, 0.0f), "no closest-hit query: nothing passes");
    maudDestroySpatializer(s);
}

static void TestSplit(void)
{
    enum
    {
        COUNT = 100
    };
    maudSpatializer* a = Create(4, false, true);
    maudSpatializer* b = Create(4, true, true);
    static maudSourceId ia[COUNT];
    static maudSourceId ib[COUNT];
    for (int i = 0; i < COUNT; ++i)
    {
        ia[i] = Source(a, true);
        ib[i] = Source(b, true);
    }
    static maudDirectResult x[COUNT];
    static maudDirectResult y[COUNT];
    maudPose listener = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    for (int i = 0; i < COUNT; ++i)
    {
        float angle = (float)i * 0.37f;
        maudPose pose = {{8.0f * sinf(angle), 0.1f * (float)i, -6.0f - 6.0f * cosf(angle)},
                         {0.0f, 0.0f, 0.0f, 1.0f}};
        CHECK(maudSetSourcePose(a, ia[i], &pose) == maud_success &&
                  maudSetSourcePose(b, ib[i], &pose) == maud_success,
              "pose");
    }
    CHECK(maudSimulateDirect(a, &listener) == maud_success && maudLatchResults(a) > 0 &&
              maudSimulateDirect(b, &listener) == maud_success && maudLatchResults(b) > 0,
          "steps");
    for (int i = 0; i < COUNT; ++i)
    {
        CHECK(maudGetDirectResult(a, ia[i], &x[i]) == maud_success &&
                  maudGetDirectResult(b, ib[i], &y[i]) == maud_success,
              "results");
    }
    CHECK(memcmp(x, y, sizeof(x)) == 0, "a split step gives the same bytes");
    maudDestroySpatializer(a);
    maudDestroySpatializer(b);
}

static void TestMaterials(void)
{
    maudSpatializer* s = Create(4, false, true);
    maudAcousticMaterial bad = s_materials[0];
    bad.transmission[1] = 1.5f;
    maudAcousticMaterial table[2] = {s_materials[0], bad};
    CHECK(maudSetMaterials(s, table, 2) == maud_errorInvalid, "a transmission above 1");
    bad = s_materials[0];
    bad.scattering = NAN;
    table[1] = bad;
    CHECK(maudSetMaterials(s, table, 2) == maud_errorInvalid, "NaN scattering");
    static maudAcousticMaterial many[65];
    for (int i = 0; i < 65; ++i)
    {
        many[i] = s_materials[0];
    }
    CHECK(maudSetMaterials(s, many, 65) == maud_errorCapacity, "past the capacity");
    maudSourceId id = Source(s, true);
    maudDirectResult r = Result(s, id, -2.0f, -10.0f);
    CHECK(Bands(&r, 0.45f, 0.32f, 0.21f), "a refused table changes nothing");
    CHECK(maudSetMaterials(s, nullptr, 0) == maud_success, "an empty table");
    r = Result(s, id, -2.0f, -10.0f);
    CHECK(Bands(&r, 0.0f, 0.0f, 0.0f), "and every material is unknown");
    CHECK(maudSetMaterials(s, nullptr, 1) == maud_errorInvalid, "no materials");
    maudDestroySpatializer(s);
    maudSpatializerDef def = maudDefaultSpatializerDef();
    maudSpatializer* none = nullptr;
    def.maxSurfaces = 17;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid, "too many surfaces");
    def = maudDefaultSpatializerDef();
    def.materialCapacity = 0;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid, "no material room");
}

int main(void)
{
    TestWalk();
    TestSplit();
    TestMaterials();
    return s_failures == 0 ? 0 : 1;
}
