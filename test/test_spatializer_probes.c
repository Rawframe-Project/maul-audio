// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Probe sets through the spatializer: a set from points reports its
// probes and links and copies its points; a generated set lands its
// probes on the floor; a destroyed set's id is stale and its slot is
// reused under a new generation; the capacity, the limits, defs out of
// range, generation without a closest-hit query and probes past the
// set's are refused.

#include "test_harness.h"

#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <math.h>

static maudAcousticScene* Room(void)
{
    // A floor slab, 10 x 10 m, its top at y = 0, and a wall across x = 5
    // standing 3 m.
    static maudVector3 v[16];
    static uint32_t faces[72];
    static const uint32_t box[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                     2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[24] = {0};
    const maudVector3 low[2] = {{0.0f, -0.2f, 0.0f}, {4.9f, 0.0f, 0.0f}};
    const maudVector3 high[2] = {{10.0f, 0.0f, 10.0f}, {5.1f, 3.0f, 10.0f}};
    for (uint32_t b = 0; b < 2; ++b)
    {
        for (uint32_t i = 0; i < 8; ++i)
        {
            v[b * 8 + i] =
                (maudVector3){(i & 1) ? high[b].x : low[b].x, (i & 2) ? high[b].y : low[b].y,
                              (i & 4) ? high[b].z : low[b].z};
        }
        for (uint32_t k = 0; k < 36; ++k)
        {
            faces[b * 36 + k] = b * 8 + box[k];
        }
    }
    maudMesh mesh = {v, 16, faces, materials, 24};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a room");
    return scene;
}

static maudSpatializer* Create(maudAcousticScene* scene, uint32_t sets)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudSceneAnyHit;
    def.closestHit = maudSceneClosestHit;
    def.rayContext = scene;
    def.probeSetCapacity = sets;
    def.maxProbes = 64;
    def.maxProbePairs = 1024;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    return s;
}

static void TestPoints(maudSpatializer* s)
{
    // Two probes either side of the wall, one more beside the first.
    const maudVector3 points[3] = {{2.0f, 1.5f, 5.0f}, {8.0f, 1.5f, 5.0f}, {2.0f, 1.5f, 7.0f}};
    maudProbeSetDef def = maudDefaultProbeSetDef();
    def.points = points;
    def.pointCount = 3;
    def.range = 10.0f;
    maudProbeSetId set = {0, 0};
    CHECK(maudCreateProbeSet(s, &def, &set) == maud_success && set.index1 == 1, "a set");
    maudProbeSetInfo info = {0};
    maudVector3 copied[2];
    CHECK(maudGetProbeSet(s, set, &info, 1, 2, copied) == maud_success, "its info");
    printf("points: %u probes, %u links\n", info.probes, info.links);
    CHECK(info.probes == 3 && info.links == 1, "only the probes on one side linked");
    CHECK(copied[0].x == 8.0f && copied[1].z == 7.0f, "its points copied");
    CHECK(maudGetProbeSet(s, set, &info, 2, 2, copied) == maud_errorInvalid,
          "probes past the set's");
    CHECK(maudGetProbeSet(s, set, &info, 3, 0, copied) == maud_success, "none at the end");
    CHECK(maudDestroyProbeSet(s, set) == maud_success, "destroyed");
    CHECK(maudDestroyProbeSet(s, set) == maud_errorStale &&
              maudGetProbeSet(s, set, &info, 0, 0, nullptr) == maud_errorStale,
          "a destroyed set's id is stale");
    maudProbeSetId again = {0, 0};
    CHECK(maudCreateProbeSet(s, &def, &again) == maud_success && again.index1 == set.index1 &&
              again.generation != set.generation,
          "the slot reused under a new generation");
    maudProbeSetId other = {0, 0};
    maudProbeSetId third = {0, 0};
    CHECK(maudCreateProbeSet(s, &def, &other) == maud_success &&
              maudCreateProbeSet(s, &def, &third) == maud_errorCapacity && third.index1 == 0,
          "past the capacity");
    CHECK(maudDestroyProbeSet(s, again) == maud_success &&
              maudDestroyProbeSet(s, other) == maud_success,
          "both destroyed");
}

static void TestGenerated(maudSpatializer* s)
{
    maudProbeSetDef def = maudDefaultProbeSetDef();
    def.boxMin = (maudVector3){0.0f, -1.0f, 0.0f};
    def.boxMax = (maudVector3){10.0f, 4.0f, 10.0f};
    def.spacing = 2.5f;
    maudProbeSetId set = {0, 0};
    CHECK(maudCreateProbeSet(s, &def, &set) == maud_success, "generated");
    maudProbeSetInfo info = {0};
    maudVector3 points[16];
    CHECK(maudGetProbeSet(s, set, &info, 0, 16, points) == maud_success, "its probes");
    bool floor = info.probes == 16;
    for (uint32_t i = 0; i < 16 && floor; ++i)
    {
        floor = fabsf(points[i].y - 1.5f) < 1e-5f;
    }
    // The wall's top is 0.2 m wide, under the spacing's columns.
    printf("generated: %u probes, %u links\n", info.probes, info.links);
    CHECK(floor, "16 probes 1.5 m over the floor");
    CHECK(info.links > 0, "linked on each side");
    CHECK(maudDestroyProbeSet(s, set) == maud_success, "destroyed");
    def.spacing = 0.25f;
    CHECK(maudCreateProbeSet(s, &def, &set) == maud_errorCapacity, "past maxProbes");
}

static void TestMisuse(maudSpatializer* s)
{
    maudProbeSetDef def = maudDefaultProbeSetDef();
    maudProbeSetId set = {0, 0};
    CHECK(maudCreateProbeSet(s, &def, &set) == maud_errorInvalid, "neither points nor a box");
    def.boxMax = (maudVector3){1.0f, 1.0f, 1.0f};
    def.cookie = 0;
    CHECK(maudCreateProbeSet(s, &def, &set) == maud_errorInvalid, "no cookie");
    def = maudDefaultProbeSetDef();
    def.boxMax = (maudVector3){1.0f, 1.0f, 1.0f};
    def.range = 0.0f;
    CHECK(maudCreateProbeSet(s, &def, &set) == maud_errorInvalid, "no range");
    def.range = 5.0f;
    def.spacing = 0.1f;
    CHECK(maudCreateProbeSet(s, &def, &set) == maud_errorInvalid, "too fine a grid");
    const maudVector3 bad[1] = {{NAN, 0.0f, 0.0f}};
    def = maudDefaultProbeSetDef();
    def.points = bad;
    def.pointCount = 1;
    CHECK(maudCreateProbeSet(s, &def, &set) == maud_errorInvalid, "a point not finite");
    CHECK(maudCreateProbeSet(s, nullptr, &set) == maud_errorInvalid &&
              maudCreateProbeSet(nullptr, &def, &set) == maud_errorInvalid,
          "NULL");
    maudProbeSetId none = {0, 0};
    maudProbeSetId unknown = {9, 1};
    maudProbeSetInfo info;
    CHECK(maudDestroyProbeSet(s, none) == maud_errorInvalid &&
              maudDestroyProbeSet(s, unknown) == maud_errorInvalid &&
              maudGetProbeSet(s, none, &info, 0, 0, nullptr) == maud_errorInvalid,
          "0 and unknown ids");
    // Generation needs the closest-hit query; a spatializer without sets
    // has no room for one; limits out of range are refused.
    maudSpatializerDef sd = maudDefaultSpatializerDef();
    sd.probeSetCapacity = 1;
    maudSpatializer* blind = nullptr;
    CHECK(maudCreateSpatializer(&sd, &blind) == maud_success, "no queries");
    def = maudDefaultProbeSetDef();
    def.boxMax = (maudVector3){1.0f, 1.0f, 1.0f};
    CHECK(maudCreateProbeSet(blind, &def, &set) == maud_errorState, "generation without hits");
    maudDestroySpatializer(blind);
    sd.probeSetCapacity = 0;
    CHECK(maudCreateSpatializer(&sd, &blind) == maud_success &&
              maudCreateProbeSet(blind, &def, &set) == maud_errorCapacity,
          "no sets asked for");
    maudDestroySpatializer(blind);
    maudProbeSetId junk = {7, 7};
    CHECK(maudCreateProbeSet(nullptr, &def, &junk) == maud_errorInvalid && junk.index1 == 0 &&
              junk.generation == 0 &&
              maudCreateProbeSet(nullptr, &def, nullptr) == maud_errorInvalid,
          "a refused set's id cleared; no id to write");
    sd.probeSetCapacity = 65;
    CHECK(maudCreateSpatializer(&sd, &blind) == maud_errorInvalid, "too many sets");
    sd.probeSetCapacity = 1;
    sd.maxProbes = 0;
    CHECK(maudCreateSpatializer(&sd, &blind) == maud_errorInvalid, "no probes");
    sd.maxProbes = 1;
    sd.maxProbePairs = 1u << 24;
    CHECK(maudCreateSpatializer(&sd, &blind) == maud_success, "the most pairs");
    maudDestroySpatializer(blind);
    sd.maxProbePairs = (1u << 24) + 1;
    CHECK(maudCreateSpatializer(&sd, &blind) == maud_errorInvalid, "too many pairs");
}

int main(void)
{
    maudAcousticScene* scene = Room();
    maudSpatializer* s = Create(scene, 2);
    TestPoints(s);
    TestGenerated(s);
    TestMisuse(s);
    maudDestroySpatializer(s);
    maudDestroyAcousticScene(scene);
    return s_failures == 0 ? 0 : 1;
}
