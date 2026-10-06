// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Instanced meshes: four instances of a box, turned, scaled and moved,
// over a static floor answer 4,000 random rays as a static scene of the
// same triangles carried into the world does (any-hit and closest-hit
// alike but for rays grazing an edge, distances within 1e-4, the same
// normals and materials); a ray onto a box's top lands at the exact
// distance; a quarter turn about +y carries +x to -z; changes wait for
// a commit; ties go to the static scene, then the lower slot; limits,
// stale ids and transforms out of range are refused.

#include "test_harness.h"

#include "maul-audio/scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t s_faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                     2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};

typedef struct Box
{
    maudVector3 v[8];
    uint32_t materials[12];
    maudMesh mesh;
} Box;

static void MakeBox(Box* b, maudVector3 low, maudVector3 high, uint32_t material)
{
    for (int i = 0; i < 8; ++i)
    {
        b->v[i] = (maudVector3){(i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y,
                                (i & 4) ? high.z : low.z};
    }
    for (int i = 0; i < 12; ++i)
    {
        b->materials[i] = material + (uint32_t)i / 2;
    }
    b->mesh = (maudMesh){b->v, 8, s_faces, b->materials, 12};
}

// p turned by the unit quaternion q, in double (Hamilton's product).
static void Turn(maudQuaternion q, const double* p, double* out)
{
    double qx = (double)q.x;
    double qy = (double)q.y;
    double qz = (double)q.z;
    double qw = (double)q.w;
    double n = sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    double x = qx / n;
    double y = qy / n;
    double z = qz / n;
    double w = qw / n;
    double t[3] = {2.0 * (y * p[2] - z * p[1]), 2.0 * (z * p[0] - x * p[2]),
                   2.0 * (x * p[1] - y * p[0])};
    out[0] = p[0] + w * t[0] + (y * t[2] - z * t[1]);
    out[1] = p[1] + w * t[1] + (z * t[0] - x * t[2]);
    out[2] = p[2] + w * t[2] + (x * t[1] - y * t[0]);
}

static const maudInstanceTransform s_transforms[4] = {
    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 1.0f},
    {{3.0f, 2.0f, -2.0f}, {0.2f, 0.7f, -0.1f, 0.6f}, 2.5f},
    {{-4.0f, 1.5f, 3.0f}, {-0.5f, 0.1f, 0.3f, 0.8f}, 0.5f},
    {{1.0f, 4.0f, 4.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, 1.7f}};

static Box s_unit;
static Box s_floor;

static maudAcousticScene* Instanced(void)
{
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &s_floor.mesh;
    def.meshCount = 1;
    def.instanceMeshes = &s_unit.mesh;
    def.instanceMeshCount = 1;
    def.instanceCapacity = 4;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "an instanced scene");
    for (int i = 0; i < 4; ++i)
    {
        maudSceneInstanceId id;
        CHECK(maudCreateSceneInstance(scene, 0, &s_transforms[i], &id) == maud_success,
              "an instance");
    }
    CHECK(maudCommitAcousticScene(scene) == maud_success, "committed");
    return scene;
}

// The same triangles carried into the world, as static meshes.
static maudAcousticScene* Flattened(void)
{
    static Box boxes[4];
    maudMesh meshes[5] = {s_floor.mesh};
    for (int i = 0; i < 4; ++i)
    {
        boxes[i] = s_unit;
        for (int k = 0; k < 8; ++k)
        {
            double p[3] = {(double)s_unit.v[k].x, (double)s_unit.v[k].y, (double)s_unit.v[k].z};
            double turned[3];
            Turn(s_transforms[i].orientation, p, turned);
            const maudVector3 at = s_transforms[i].position;
            double s = (double)s_transforms[i].scale;
            boxes[i].v[k] = (maudVector3){(float)((double)at.x + s * turned[0]),
                                          (float)((double)at.y + s * turned[1]),
                                          (float)((double)at.z + s * turned[2])};
        }
        boxes[i].mesh = (maudMesh){boxes[i].v, 8, s_faces, boxes[i].materials, 12};
        meshes[1 + i] = boxes[i].mesh;
    }
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = meshes;
    def.meshCount = 5;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a flattened scene");
    return scene;
}

static float Random(float low, float high)
{
    return low + (high - low) * (float)rand() / (float)RAND_MAX;
}

static void TestAgainstFlattened(void)
{
    maudAcousticScene* a = Instanced();
    maudAcousticScene* b = Flattened();
    srand(5);
    uint32_t anyWrong = 0;
    uint32_t hitWrong = 0;
    double worst = 0.0;
    uint32_t hits = 0;
    for (int i = 0; i < 4000; ++i)
    {
        maudVector3 o = {Random(-8.0f, 8.0f), Random(0.2f, 8.0f), Random(-8.0f, 8.0f)};
        maudVector3 to = {Random(-6.0f, 6.0f), Random(-1.0f, 6.0f), Random(-6.0f, 6.0f)};
        maudVector3 d = {to.x - o.x, to.y - o.y, to.z - o.z};
        float n = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        maudRay ray = {o, {d.x / n, d.y / n, d.z / n}, 0.0f, 30.0f};
        uint8_t oa = 0;
        uint8_t ob = 0;
        maudSceneAnyHit(&ray, 1, &oa, a);
        maudSceneAnyHit(&ray, 1, &ob, b);
        anyWrong += oa != ob ? 1 : 0;
        maudRayHit ha;
        maudRayHit hb;
        maudSceneClosestHit(&ray, 1, &ha, a);
        maudSceneClosestHit(&ray, 1, &hb, b);
        bool both = ha.distance != INFINITY && hb.distance != INFINITY;
        if (!both)
        {
            hitWrong += (ha.distance != INFINITY) != (hb.distance != INFINITY) ? 1 : 0;
            continue;
        }
        hits += 1;
        double dot = (double)ha.normal.x * (double)hb.normal.x +
                     (double)ha.normal.y * (double)hb.normal.y +
                     (double)ha.normal.z * (double)hb.normal.z;
        worst = fmax(worst, fabs((double)ha.distance - (double)hb.distance));
        hitWrong += ha.material != hb.material || dot < 0.9999 ? 1 : 0;
    }
    printf("against flattened: %u hits; any-hit %u differ, closest %u differ; worst distance "
           "%.2e m\n",
           hits, anyWrong, hitWrong, worst);
    CHECK(hits > 1000 && anyWrong <= 4 && hitWrong <= 4 && worst < 1e-4, "as flattened");
    maudDestroyAcousticScene(b);
    maudDestroyAcousticScene(a);
}

static maudRayHit Down(maudAcousticScene* scene, float x, float z)
{
    maudRay ray = {{x, 10.0f, z}, {0.0f, -1.0f, 0.0f}, 0.0f, 100.0f};
    maudRayHit hit;
    maudSceneClosestHit(&ray, 1, &hit, scene);
    return hit;
}

static void TestExactAndTurn(void)
{
    // A bar along local +x from 0 to 2, 0.2 thick.
    Box bar;
    MakeBox(&bar, (maudVector3){0.0f, -0.1f, -0.1f}, (maudVector3){2.0f, 0.1f, 0.1f}, 0);
    maudMesh meshes[2] = {s_unit.mesh, bar.mesh};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.instanceMeshes = meshes;
    def.instanceMeshCount = 2;
    def.instanceCapacity = 2;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a scene");
    const maudInstanceTransform box = {{5.0f, 3.0f, 5.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};
    const maudInstanceTransform turned = {
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.70710678f, 0.0f, 0.70710678f}, 1.0f};
    maudSceneInstanceId a;
    maudSceneInstanceId b;
    CHECK(maudCreateSceneInstance(scene, 0, &box, &a) == maud_success &&
              maudCreateSceneInstance(scene, 1, &turned, &b) == maud_success,
          "two instances");
    CHECK(Down(scene, 5.0f, 5.0f).distance == INFINITY, "nothing before the commit");
    CHECK(maudCommitAcousticScene(scene) == maud_success, "committed");
    maudRayHit top = Down(scene, 5.2f, 4.9f);
    printf("onto the box: %.7f m, normal y %.6f\n", (double)top.distance, (double)top.normal.y);
    CHECK(fabsf(top.distance - 6.0f) < 1e-5f && fabsf(fabsf(top.normal.y) - 1.0f) < 1e-6f,
          "the exact distance");
    CHECK(Down(scene, 0.0f, -1.5f).distance != INFINITY &&
              Down(scene, 1.5f, 0.0f).distance == INFINITY,
          "a quarter turn about +y carries +x to -z");
    const maudInstanceTransform moved = {{-5.0f, 3.0f, 5.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};
    CHECK(maudMoveSceneInstance(scene, a, &moved) == maud_success &&
              Down(scene, 5.0f, 5.0f).distance != INFINITY,
          "a move waits for the commit");
    CHECK(maudCommitAcousticScene(scene) == maud_success &&
              Down(scene, 5.0f, 5.0f).distance == INFINITY &&
              Down(scene, -5.0f, 5.0f).distance != INFINITY,
          "and then takes effect");
    CHECK(maudDestroySceneInstance(scene, a) == maud_success &&
              Down(scene, -5.0f, 5.0f).distance != INFINITY &&
              maudCommitAcousticScene(scene) == maud_success &&
              Down(scene, -5.0f, 5.0f).distance == INFINITY,
          "destroyed, gone at the commit");
    CHECK(maudMoveSceneInstance(scene, a, &moved) == maud_errorStale &&
              maudDestroySceneInstance(scene, a) == maud_errorStale,
          "a destroyed instance's id is stale");
    maudDestroyAcousticScene(scene);
}

static void TestTies(void)
{
    // A floor quad at y = 0, static and as two instancing meshes of other
    // materials.
    static const maudVector3 v[4] = {{-1, 0, -1}, {1, 0, -1}, {-1, 0, 1}, {1, 0, 1}};
    static const uint32_t quad[6] = {0, 2, 1, 1, 2, 3};
    static const uint32_t m3[2] = {3, 3};
    static const uint32_t m5[2] = {5, 5};
    static const uint32_t m7[2] = {7, 7};
    const maudMesh statics = {v, 4, quad, m3, 2};
    const maudMesh instancing[2] = {{v, 4, quad, m5, 2}, {v, 4, quad, m7, 2}};
    const maudInstanceTransform at = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.instanceMeshes = instancing;
    def.instanceMeshCount = 2;
    def.instanceCapacity = 2;
    maudAcousticScene* scene = nullptr;
    maudSceneInstanceId first;
    maudSceneInstanceId second;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success &&
              maudCreateSceneInstance(scene, 1, &at, &first) == maud_success &&
              maudCreateSceneInstance(scene, 0, &at, &second) == maud_success &&
              maudCommitAcousticScene(scene) == maud_success,
          "two coincident instances");
    CHECK(Down(scene, 0.3f, 0.2f).material == 7, "the lower slot wins a tie");
    maudDestroyAcousticScene(scene);
    def.meshes = &statics;
    def.meshCount = 1;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success &&
              maudCreateSceneInstance(scene, 0, &at, &first) == maud_success &&
              maudCommitAcousticScene(scene) == maud_success,
          "an instance on the static floor");
    CHECK(Down(scene, 0.3f, 0.2f).material == 3, "the static scene wins a tie");
    maudDestroyAcousticScene(scene);
}

static void TestRefused(void)
{
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.instanceMeshes = &s_unit.mesh;
    def.instanceMeshCount = 1;
    def.instanceCapacity = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a scene");
    maudInstanceTransform t = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
    maudSceneInstanceId id = {7, 7};
    CHECK(maudCreateSceneInstance(scene, 1, &t, &id) == maud_errorInvalid && id.index1 == 0,
          "a mesh out of range");
    t.scale = 0.0f;
    CHECK(maudCreateSceneInstance(scene, 0, &t, &id) == maud_errorInvalid, "no scale");
    t.scale = 1.0f;
    t.orientation = (maudQuaternion){0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(maudCreateSceneInstance(scene, 0, &t, &id) == maud_errorInvalid, "no orientation");
    t.orientation.w = 1.0f;
    t.position.x = NAN;
    CHECK(maudCreateSceneInstance(scene, 0, &t, &id) == maud_errorInvalid, "a position not finite");
    t.position.x = 0.0f;
    maudSceneInstanceId other;
    CHECK(maudCreateSceneInstance(scene, 0, &t, &id) == maud_success &&
              maudCreateSceneInstance(scene, 0, &t, &other) == maud_errorCapacity,
          "past the capacity");
    const maudSceneInstanceId none = {0, 0};
    CHECK(maudDestroySceneInstance(scene, none) == maud_errorInvalid &&
              maudCommitAcousticScene(nullptr) == maud_errorInvalid,
          "0 and NULL");
    maudDestroyAcousticScene(scene);
    def.maxTriangles = 11;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_errorCapacity, "past maxTriangles");
    def.maxTriangles = 12;
    def.instanceCapacity = 65537;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_errorInvalid, "too many instances");
}

int main(void)
{
    MakeBox(&s_unit, (maudVector3){-0.5f, -0.5f, -0.5f}, (maudVector3){0.5f, 0.5f, 0.5f}, 10);
    MakeBox(&s_floor, (maudVector3){-10.0f, -0.2f, -10.0f}, (maudVector3){10.0f, 0.0f, 10.0f}, 0);
    TestAgainstFlattened();
    TestExactAndTurn();
    TestTies();
    TestRefused();
    return s_failures == 0 ? 0 : 1;
}
