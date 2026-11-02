// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Instanced meshes: four instances of a box, turned, scaled and moved,
// over a static floor answer 4,000 random rays as a static scene of the
// same triangles carried into the world does (any-hit and closest-hit
// alike but for rays grazing an edge, distances within 1e-4, the same
// normals and materials), and so do 36, which the instances' own tree
// holds in more than one level; a ray onto a box's top lands at the exact
// distance; a quarter turn about +y carries +x to -z; changes wait for
// a commit; ties go to the static scene, then the lower slot; a ray
// ending exactly on an instance hits it; limits, stale ids and
// transforms out of range are refused, and only the invalid arguments
// count as misuse; a freed slot is taken again
// under a new id; an allocator that fails at any of the scene's blocks
// is a capacity error with nothing leaked.

#include "test_harness.h"

#include "maul-audio/scene.h"

#include <float.h>
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

#define MANY 36

// The first four by hand; the rest on a 4 by 8 grid above them, each
// turned and scaled its own way (Fill).
static maudSceneInstanceTransform s_transforms[MANY] = {
    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 1.0f},
    {{3.0f, 2.0f, -2.0f}, {0.2f, 0.7f, -0.1f, 0.6f}, 2.5f},
    {{-4.0f, 1.5f, 3.0f}, {-0.5f, 0.1f, 0.3f, 0.8f}, 0.5f},
    {{1.0f, 4.0f, 4.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, 1.7f}};

static void Fill(void)
{
    for (int i = 4; i < MANY; ++i)
    {
        int k = i - 4;
        float a = 0.37f * (float)k;
        float x = sinf(a);
        float y = cosf(a * 1.3f);
        float z = sinf(a * 0.7f);
        float w = 1.0f + 0.1f * (float)(k % 5);
        float n = sqrtf(x * x + y * y + z * z + w * w);
        s_transforms[i] = (maudSceneInstanceTransform){{-7.0f + 2.0f * (float)(k % 8),
                                                        5.5f + 0.4f * (float)(k % 3),
                                                        -6.0f + 3.0f * (float)(k / 8)},
                                                       {x / n, y / n, z / n, w / n},
                                                       0.4f + 0.05f * (float)(k % 7)};
    }
}

static Box s_unit;
static Box s_floor;

static long s_live;
static long s_allocations;
static long s_failAfter = -1;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    if (s_failAfter == 0)
    {
        return nullptr;
    }
    s_failAfter -= s_failAfter > 0 ? 1 : 0;
    s_live++;
    s_allocations++;
    return malloc(size);
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    s_live--;
    free(memory);
}

static maudScene* Instanced(int count)
{
    maudSceneDef def = maudDefaultSceneDef();
    def.meshes = &s_floor.mesh;
    def.meshCount = 1;
    def.instanceMeshes = &s_unit.mesh;
    def.instanceMeshCount = 1;
    def.instanceCapacity = (uint32_t)count;
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "an instanced scene");
    for (int i = 0; i < count; ++i)
    {
        maudSceneInstanceId id;
        CHECK(maudCreateSceneInstance(scene, 0, &s_transforms[i], &id) == maud_success,
              "an instance");
    }
    CHECK(maudScene_Commit(scene) == maud_success, "committed");
    return scene;
}

// The same triangles carried into the world, as static meshes.
static maudScene* Flattened(int count)
{
    static Box boxes[MANY];
    maudMesh meshes[1 + MANY] = {s_floor.mesh};
    for (int i = 0; i < count; ++i)
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
    maudSceneDef def = maudDefaultSceneDef();
    def.meshes = meshes;
    def.meshCount = 1 + (uint32_t)count;
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "a flattened scene");
    return scene;
}

static float Random(float low, float high)
{
    return low + (high - low) * (float)rand() / (float)RAND_MAX;
}

static void TestAgainstFlattened(int count)
{
    maudScene* a = Instanced(count);
    maudScene* b = Flattened(count);
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
        maudFindAnyHit(&ray, 1, &oa, a);
        maudFindAnyHit(&ray, 1, &ob, b);
        anyWrong += oa != ob ? 1 : 0;
        maudRayHit ha;
        maudRayHit hb;
        maudFindClosestHit(&ray, 1, &ha, a);
        maudFindClosestHit(&ray, 1, &hb, b);
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
    printf("%d instances against flattened: %u hits; any-hit %u differ, closest %u differ; "
           "worst distance %.2e m\n",
           count, hits, anyWrong, hitWrong, worst);
    CHECK(hits > 1000 && anyWrong <= 4 && hitWrong <= 4 && worst < 1e-4, "as flattened");
    maudDestroyScene(b);
    maudDestroyScene(a);
}

static maudRayHit Down(maudScene* scene, float x, float z)
{
    maudRay ray = {{x, 10.0f, z}, {0.0f, -1.0f, 0.0f}, 0.0f, 100.0f};
    maudRayHit hit;
    maudFindClosestHit(&ray, 1, &hit, scene);
    return hit;
}

static void TestExactAndTurn(void)
{
    // A bar along local +x from 0 to 2, 0.2 thick.
    Box bar;
    MakeBox(&bar, (maudVector3){0.0f, -0.1f, -0.1f}, (maudVector3){2.0f, 0.1f, 0.1f}, 0);
    maudMesh meshes[2] = {s_unit.mesh, bar.mesh};
    maudSceneDef def = maudDefaultSceneDef();
    def.instanceMeshes = meshes;
    def.instanceMeshCount = 2;
    def.instanceCapacity = 2;
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "a scene");
    const maudSceneInstanceTransform box = {{5.0f, 3.0f, 5.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};
    const maudSceneInstanceTransform turned = {
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.70710678f, 0.0f, 0.70710678f}, 1.0f};
    maudSceneInstanceId a;
    maudSceneInstanceId b;
    CHECK(maudCreateSceneInstance(scene, 0, &box, &a) == maud_success &&
              maudCreateSceneInstance(scene, 1, &turned, &b) == maud_success,
          "two instances");
    CHECK(Down(scene, 5.0f, 5.0f).distance == INFINITY, "nothing before the commit");
    CHECK(maudScene_Commit(scene) == maud_success, "committed");
    maudRayHit top = Down(scene, 5.2f, 4.9f);
    printf("onto the box: %.7f m, normal y %.6f\n", (double)top.distance, (double)top.normal.y);
    CHECK(fabsf(top.distance - 6.0f) < 1e-5f && fabsf(fabsf(top.normal.y) - 1.0f) < 1e-6f,
          "the exact distance");
    CHECK(Down(scene, 0.0f, -1.5f).distance != INFINITY &&
              Down(scene, 1.5f, 0.0f).distance == INFINITY,
          "a quarter turn about +y carries +x to -z");
    const maudSceneInstanceTransform moved = {{-5.0f, 3.0f, 5.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 2.0f};
    CHECK(maudSceneInstance_Move(scene, a, &moved) == maud_success &&
              Down(scene, 5.0f, 5.0f).distance != INFINITY,
          "a move waits for the commit");
    CHECK(maudScene_Commit(scene) == maud_success && Down(scene, 5.0f, 5.0f).distance == INFINITY &&
              Down(scene, -5.0f, 5.0f).distance != INFINITY,
          "and then takes effect");
    CHECK(maudDestroySceneInstance(scene, a) == maud_success &&
              Down(scene, -5.0f, 5.0f).distance != INFINITY &&
              maudScene_Commit(scene) == maud_success &&
              Down(scene, -5.0f, 5.0f).distance == INFINITY,
          "destroyed, gone at the commit");
    CHECK(maudSceneInstance_Move(scene, a, &moved) == maud_errorStale &&
              maudDestroySceneInstance(scene, a) == maud_errorStale,
          "a destroyed instance's id is stale");
    maudDestroyScene(scene);
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
    const maudSceneInstanceTransform at = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
    maudSceneDef def = maudDefaultSceneDef();
    def.instanceMeshes = instancing;
    def.instanceMeshCount = 2;
    def.instanceCapacity = 2;
    maudScene* scene = nullptr;
    maudSceneInstanceId first;
    maudSceneInstanceId second;
    CHECK(maudCreateScene(&def, &scene) == maud_success &&
              maudCreateSceneInstance(scene, 1, &at, &first) == maud_success &&
              maudCreateSceneInstance(scene, 0, &at, &second) == maud_success &&
              maudScene_Commit(scene) == maud_success,
          "two coincident instances");
    CHECK(Down(scene, 0.3f, 0.2f).material == 7, "the lower slot wins a tie");
    // A ray ending exactly on the instances: a hit within [tMin, tMax].
    maudRay exact = {{0.3f, 10.0f, 0.2f}, {0.0f, -1.0f, 0.0f}, 0.0f, 10.0f};
    uint8_t occluded = 0;
    maudFindAnyHit(&exact, 1, &occluded, scene);
    CHECK(occluded == 1, "a hit at the ray's end");
    maudDestroyScene(scene);
    def.meshes = &statics;
    def.meshCount = 1;
    CHECK(maudCreateScene(&def, &scene) == maud_success &&
              maudCreateSceneInstance(scene, 0, &at, &first) == maud_success &&
              maudScene_Commit(scene) == maud_success,
          "an instance on the static floor");
    CHECK(Down(scene, 0.3f, 0.2f).material == 3, "the static scene wins a tie");
    maudDestroyScene(scene);
}

static void TestRefused(void)
{
    maudSceneDef def = maudDefaultSceneDef();
    def.instanceMeshes = &s_unit.mesh;
    def.instanceMeshCount = 1;
    def.instanceCapacity = 1;
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "a scene");
    maudSceneInstanceTransform t = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
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
    CHECK(maudDestroySceneInstance(scene, id) == maud_success &&
              maudCreateSceneInstance(scene, 0, &t, &other) == maud_success &&
              maudDestroySceneInstance(scene, id) == maud_errorStale,
          "the freed slot taken again, the old id stale");
    t.scale = 1000.0f;
    CHECK(maudSceneInstance_Move(scene, other, &t) == maud_success, "the largest scale");
    t.scale = 1000.5f;
    CHECK(maudSceneInstance_Move(scene, other, &t) == maud_errorInvalid, "past it");
    const maudSceneInstanceId none = {0, 0};
    CHECK(maudDestroySceneInstance(scene, none) == maud_errorInvalid &&
              maudScene_Commit(nullptr) == maud_errorInvalid,
          "0 and NULL");
    CHECK(maudScene_GetMisuse(scene) == 6,
          "each invalid argument counted; the capacity and the stale id not");
    maudDestroyScene(scene);
    def.maxTriangles = 11;
    CHECK(maudCreateScene(&def, &scene) == maud_errorCapacity, "past maxTriangles");
    def.maxTriangles = 12;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "maxTriangles met exactly");
    maudDestroyScene(scene);
    def.instanceCapacity = 65537;
    CHECK(maudCreateScene(&def, &scene) == maud_errorLimit, "too many instances");
}

// A static mesh with no triangles may leave its arrays out; every
// allocation the scene makes fails in turn.
// Where a ray down onto the instance meets it, at four points.
static void HitsOfBar(maudScene* scene, maudRayHit hits[4])
{
    static const float at[4][2] = {{1.0f, 0.0f}, {1.5f, 0.05f}, {0.0f, -1.5f}, {0.05f, -0.5f}};
    for (int i = 0; i < 4; ++i)
    {
        hits[i] = Down(scene, at[i][0], at[i][1]);
    }
}

static bool SameHits(const maudRayHit* a, const maudRayHit* b)
{
    bool same = true;
    for (int i = 0; i < 4; ++i)
    {
        same = same && a[i].distance == b[i].distance && a[i].normal.x == b[i].normal.x &&
               a[i].normal.y == b[i].normal.y && a[i].normal.z == b[i].normal.z;
    }
    return same;
}

// An orientation of any finite nonzero length, made or moved to, turns
// as its unit quaternion does, to the bit: (1.1574725e-42, 0, 0, 0),
// which the scene fuzz target was refused, and others at both ends of
// the floats. Next to 2^100, 2^-100 is below a float's precision and
// turns nothing.
static void TestAnyLength(void)
{
    // A bar along local +x from 0 to 2, 0.2 thick.
    Box bar;
    MakeBox(&bar, (maudVector3){0.0f, -0.1f, -0.1f}, (maudVector3){2.0f, 0.1f, 0.1f}, 0);
    maudSceneDef def = maudDefaultSceneDef();
    def.instanceMeshes = &bar.mesh;
    def.instanceMeshCount = 1;
    def.instanceCapacity = 1;
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "a scene");
    // A half turn about x and a quarter turn about y, each beside the
    // same turn at other lengths.
    static const maudQuaternion half = {1.0f, 0.0f, 0.0f, 0.0f};
    static const maudQuaternion quarter = {0.0f, 1.0f, 0.0f, 1.0f};
    static const struct
    {
        const maudQuaternion* unit;
        maudQuaternion other;
    } cases[] = {
        {&half, {1.1574725e-42f, 0.0f, 0.0f, 0.0f}},
        {&quarter, {0.0f, 0x1p-149f, 0.0f, 0x1p-149f}},
        {&quarter, {0.0f, 0x1p-126f, 0.0f, 0x1p-126f}},
        {&half, {0x1p-126f, 0.0f, 0.0f, 0.0f}},
        {&half, {1.9e19f, 0.0f, 0.0f, 0.0f}},
        {&quarter, {0.0f, 0x1p64f, 0.0f, 0x1p64f}},
        {&half, {FLT_MAX, 0.0f, 0.0f, 0.0f}},
        {&quarter, {0.0f, 0x1p127f, 0.0f, 0x1p127f}},
        {&half, {0x1p100f, 0.0f, 0x1p-100f, 0.0f}},
    };
    for (size_t k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k)
    {
        maudSceneInstanceTransform t = {{0.0f, 0.0f, 0.0f}, *cases[k].unit, 1.0f};
        maudSceneInstanceId id = {0, 0};
        CHECK(maudCreateSceneInstance(scene, 0, &t, &id) == maud_success &&
                  maudScene_Commit(scene) == maud_success,
              "an instance turned by a unit quaternion");
        maudRayHit unit[4];
        HitsOfBar(scene, unit);
        CHECK(unit[0].distance != INFINITY || unit[2].distance != INFINITY, "the bar is hit");
        t.orientation = cases[k].other;
        maudResult moved = maudSceneInstance_Move(scene, id, &t);
        maudRayHit hits[4];
        CHECK(maudScene_Commit(scene) == maud_success, "committed");
        HitsOfBar(scene, hits);
        bool movedSame = moved == maud_success && SameHits(hits, unit);
        CHECK(maudDestroySceneInstance(scene, id) == maud_success, "destroyed");
        maudResult made = maudCreateSceneInstance(scene, 0, &t, &id);
        CHECK(maudScene_Commit(scene) == maud_success, "committed");
        HitsOfBar(scene, hits);
        bool madeSame = made == maud_success && SameHits(hits, unit);
        printf("orientation (%g %g %g %g): moved %d, made %d\n", (double)t.orientation.x,
               (double)t.orientation.y, (double)t.orientation.z, (double)t.orientation.w,
               (int)moved, (int)made);
        CHECK(movedSame, "moved to it, the instance turns as by its unit quaternion");
        CHECK(madeSame, "made with it, the instance turns as by its unit quaternion");
        CHECK(made != maud_success || maudDestroySceneInstance(scene, id) == maud_success,
              "destroyed");
    }
    CHECK(maudScene_GetMisuse(scene) == 0, "no call refused");
    maudDestroyScene(scene);
}

static void TestFailingAllocator(void)
{
    maudMesh empty = {0};
    maudMesh meshes[2] = {s_floor.mesh, empty};
    maudSceneDef def = maudDefaultSceneDef();
    def.meshes = meshes;
    def.meshCount = 2;
    def.instanceMeshes = &s_unit.mesh;
    def.instanceMeshCount = 1;
    def.instanceCapacity = 2;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "the scene");
    maudDestroyScene(scene);
    long blocks = s_allocations;
    CHECK(blocks > 3 && s_live == 0, "made of several blocks, all returned");
    for (long failAt = 0; failAt < blocks; ++failAt)
    {
        s_failAfter = failAt;
        scene = (maudScene*)&def;
        CHECK(maudCreateScene(&def, &scene) == maud_errorCapacity && scene == nullptr &&
                  s_live == 0,
              "any block failing is a capacity error, nothing leaked");
    }
    s_failAfter = -1;
    CHECK(s_live == 0, "every block returned");
}

int main(void)
{
    MakeBox(&s_unit, (maudVector3){-0.5f, -0.5f, -0.5f}, (maudVector3){0.5f, 0.5f, 0.5f}, 10);
    MakeBox(&s_floor, (maudVector3){-10.0f, -0.2f, -10.0f}, (maudVector3){10.0f, 0.0f, 10.0f}, 0);
    Fill();
    TestAgainstFlattened(4);
    TestAgainstFlattened(MANY);
    TestExactAndTurn();
    TestTies();
    TestRefused();
    TestAnyLength();
    TestFailingAllocator();
    return s_failures == 0 ? 0 : 1;
}
