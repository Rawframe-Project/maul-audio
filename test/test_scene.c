// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Acoustic scenes: queries on a random soup of triangles match a brute
// force over every triangle (an independent test in double); rays
// exactly through a diagonal two triangles share, or a vertex eight
// share, hit; equal distances go to the triangle listed first; normals
// face the counterclockwise side; an empty scene misses; plugged into a
// spatializer's hooks, a wall occludes and its two faces transmit; bad
// meshes are refused.

#include "test_harness.h"

#include "maul-audio/scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t s_seed = 3;

static float Uniform(float low, float high)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return low + (high - low) * (float)(s_seed >> 8) / 16777216.0f;
}

static maudAcousticScene* Build(const maudMesh* meshes, uint32_t count)
{
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = meshes;
    def.meshCount = count;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a scene");
    return scene;
}

static void Load(maudVector3 v, double* out)
{
    out[0] = (double)v.x;
    out[1] = (double)v.y;
    out[2] = (double)v.z;
}

static void Cross(const double* a, const double* b, double* out)
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static double Dot(const double* a, const double* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Moller-Trumbore in double: the distance, or -1.
static double Brute(const maudVector3* v, const uint32_t* t, const maudRay* ray)
{
    double a[3];
    double b[3];
    double c[3];
    double d[3];
    double o[3];
    Load(v[t[0]], a);
    Load(v[t[1]], b);
    Load(v[t[2]], c);
    Load(ray->direction, d);
    Load(ray->origin, o);
    double e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    double e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    double p[3];
    Cross(d, e2, p);
    double det = Dot(e1, p);
    if (fabs(det) < 1e-12)
    {
        return -1.0;
    }
    double s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
    double q[3];
    Cross(s, e1, q);
    double u = Dot(s, p) / det;
    double w = Dot(d, q) / det;
    double distance = Dot(e2, q) / det;
    if (u < 0.0 || w < 0.0 || u + w > 1.0 || distance < (double)ray->minDistance ||
        distance > (double)ray->maxDistance)
    {
        return -1.0;
    }
    return distance;
}

enum
{
    TRIANGLES = 3000,
    RAYS = 3000
};

static void TestSoup(void)
{
    static maudVector3 vertices[3 * TRIANGLES];
    static uint32_t indices[3 * TRIANGLES];
    static uint32_t materials[TRIANGLES];
    for (uint32_t i = 0; i < TRIANGLES; ++i)
    {
        maudVector3 c = {Uniform(-10.0f, 10.0f), Uniform(-10.0f, 10.0f), Uniform(-10.0f, 10.0f)};
        for (int k = 0; k < 3; ++k)
        {
            vertices[3 * i + k] = (maudVector3){
                c.x + Uniform(-1.0f, 1.0f), c.y + Uniform(-1.0f, 1.0f), c.z + Uniform(-1.0f, 1.0f)};
            indices[3 * i + k] = 3 * i + (uint32_t)k;
        }
        materials[i] = i;
    }
    maudMesh mesh = {vertices, 3 * TRIANGLES, indices, materials, TRIANGLES};
    maudAcousticScene* scene = Build(&mesh, 1);
    static maudRay rays[RAYS];
    for (int i = 0; i < RAYS; ++i)
    {
        maudVector3 d = {Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f)};
        float n = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        rays[i] =
            (maudRay){{Uniform(-12.0f, 12.0f), Uniform(-12.0f, 12.0f), Uniform(-12.0f, 12.0f)},
                      {d.x / n, d.y / n, d.z / n},
                      Uniform(0.0f, 2.0f),
                      Uniform(2.0f, 30.0f)};
    }
    static uint8_t occluded[RAYS];
    static maudRayHit hits[RAYS];
    maudSceneAnyHit(rays, RAYS, occluded, scene);
    maudSceneClosestHit(rays, RAYS, hits, scene);
    int anyWrong = 0;
    int closestWrong = 0;
    int hitCount = 0;
    for (int i = 0; i < RAYS; ++i)
    {
        double best = -1.0;
        uint32_t which = 0;
        for (uint32_t t = 0; t < TRIANGLES; ++t)
        {
            double d = Brute(vertices, &indices[3 * t], &rays[i]);
            if (d >= 0.0 && (best < 0.0 || d < best))
            {
                best = d;
                which = t;
            }
        }
        hitCount += best >= 0.0;
        anyWrong += (best >= 0.0) != (occluded[i] != 0);
        bool same = best < 0.0 ? isinf(hits[i].distance)
                               : hits[i].material == which &&
                                     fabs((double)hits[i].distance - best) < 1e-4 * (1.0 + best);
        closestWrong += !same;
    }
    printf("%d of %d rays hit; any-hit wrong %d, closest-hit wrong %d\n", hitCount, RAYS, anyWrong,
           closestWrong);
    CHECK(hitCount > RAYS / 10 && hitCount < RAYS, "a mix of hits and misses");
    CHECK(anyWrong == 0, "any-hit matches brute force");
    CHECK(closestWrong == 0, "closest-hit matches brute force");
    maudDestroyAcousticScene(scene);
}

// A unit square at z = 0 as two triangles over the diagonal x = y, and
// a fan of eight triangles around (0.5, 0.5) at z = 1.
static void TestWatertight(void)
{
    static const maudVector3 vertices[] = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.5f, 0.5f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f},
        {1.0f, 0.5f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.5f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f},
        {0.0f, 0.5f, 1.0f},
    };
    static const uint32_t square[] = {0, 1, 2, 0, 2, 3};
    static const uint32_t fan[] = {4, 5, 6,  4, 6,  7,  4, 7,  8,  4, 8,  9,
                                   4, 9, 10, 4, 10, 11, 4, 11, 12, 4, 12, 5};
    static const uint32_t materials[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    maudMesh meshes[2] = {{vertices, 13, square, materials, 2}, {vertices, 13, fan, materials, 8}};
    maudAcousticScene* scene = Build(meshes, 2);
    bool all = true;
    for (int k = 0; k <= 64; ++k)
    {
        float p = (float)k / 64.0f;
        maudRay down = {{p, p, 0.5f}, {0.0f, 0.0f, -1.0f}, 0.0f, 1.0f};
        uint8_t hit = 0;
        maudSceneAnyHit(&down, 1, &hit, scene);
        all = all && hit == 1;
        // Slanted rays through the diagonal too.
        maudRay slant = {{p - 0.25f, p, 0.5f}, {0.4472136f, 0.0f, -0.8944272f}, 0.0f, 2.0f};
        maudSceneAnyHit(&slant, 1, &hit, scene);
        // At the square's corners a slanted ray may land just outside.
        all = all && (hit == 1 || k == 0 || k == 64);
    }
    CHECK(all, "no ray slips through the shared diagonal");
    maudRay centre = {{0.5f, 0.5f, 2.0f}, {0.0f, 0.0f, -1.0f}, 0.0f, 1.5f};
    maudRayHit h;
    maudSceneClosestHit(&centre, 1, &h, scene);
    CHECK(fabsf(h.distance - 1.0f) < 1e-6f, "nor through the vertex eight triangles share");
    maudDestroyAcousticScene(scene);
}

static void TestTiesAndNormals(void)
{
    static const maudVector3 v[] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    static const uint32_t up[] = {0, 1, 2};
    static const uint32_t down[] = {0, 2, 1};
    static const uint32_t first[] = {7};
    static const uint32_t second[] = {9};
    maudMesh meshes[2] = {{v, 3, down, second, 1}, {v, 3, up, first, 1}};
    maudAcousticScene* scene = Build(meshes, 2);
    maudRay ray = {{0.2f, 0.2f, 1.0f}, {0.0f, 0.0f, -1.0f}, 0.0f, 5.0f};
    maudRayHit h;
    maudSceneClosestHit(&ray, 1, &h, scene);
    CHECK(h.distance == 1.0f && h.material == 9, "equal distances: the first listed");
    CHECK(h.normal.x == 0.0f && h.normal.y == 0.0f && h.normal.z == -1.0f,
          "its normal faces its counterclockwise side");
    maudDestroyAcousticScene(scene);
    // Twenty copies of one triangle span several leaves: the tie still
    // goes to the first listed, whichever leaf is reached first.
    static uint32_t copies[60];
    static uint32_t numbers[20];
    for (uint32_t i = 0; i < 20; ++i)
    {
        copies[3 * i] = 0;
        copies[3 * i + 1] = 1;
        copies[3 * i + 2] = 2;
        numbers[i] = 100 + i;
    }
    maudMesh stack = {v, 3, copies, numbers, 20};
    scene = Build(&stack, 1);
    maudSceneClosestHit(&ray, 1, &h, scene);
    CHECK(h.material == 100, "a tie across leaves: the first listed");
    maudDestroyAcousticScene(scene);
    maudMesh one = {v, 3, up, first, 1};
    scene = Build(&one, 1);
    maudSceneClosestHit(&ray, 1, &h, scene);
    CHECK(h.normal.z == 1.0f, "the other winding, the other side");
    maudDestroyAcousticScene(scene);
    scene = Build(nullptr, 0);
    uint8_t hit = 9;
    maudSceneAnyHit(&ray, 1, &hit, scene);
    maudSceneClosestHit(&ray, 1, &h, scene);
    CHECK(hit == 0 && isinf(h.distance), "an empty scene misses");
    maudDestroyAcousticScene(scene);
}

// A wall of two faces (a box 0.2 m thick) between listener and source,
// through a spatializer's hooks.
static void TestHooks(void)
{
    static const maudVector3 v[] = {
        {-5.0f, -5.0f, -4.9f}, {5.0f, -5.0f, -4.9f}, {5.0f, 5.0f, -4.9f}, {-5.0f, 5.0f, -4.9f},
        {-5.0f, -5.0f, -5.1f}, {5.0f, -5.0f, -5.1f}, {5.0f, 5.0f, -5.1f}, {-5.0f, 5.0f, -5.1f},
    };
    static const uint32_t faces[] = {0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6};
    static const uint32_t materials[] = {0, 0, 0, 0};
    maudMesh wall = {v, 8, faces, materials, 4};
    maudAcousticScene* scene = Build(&wall, 1);
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudSceneAnyHit;
    def.closestHit = maudSceneClosestHit;
    def.rayContext = scene;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudAcousticMaterial brick = {{0.1f, 0.1f, 0.1f}, 0.1f, {0.3f, 0.2f, 0.1f}};
    CHECK(maudSetMaterials(s, &brick, 1) == maud_success, "a material");
    maudSourceDef sd = maudDefaultSourceDef();
    maudSourceId id = {0, 0};
    CHECK(maudCreateSource(s, &sd, &id) == maud_success, "a source");
    maudPose source = {{0.0f, 0.0f, -10.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudPose listener = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudDirectResult r = {0};
    CHECK(maudSetSourcePose(s, id, &source) == maud_success &&
              maudSimulateDirect(s, &listener) == maud_success && maudLatchResults(s) > 0 &&
              maudGetDirectResult(s, id, &r) == maud_success,
          "a result");
    CHECK(r.occlusion == 1.0f && r.surfaces == 2 && fabsf(r.transmission[0] - 0.09f) < 1e-6f &&
              fabsf(r.transmission[2] - 0.01f) < 1e-6f,
          "the wall occludes, its two faces transmit");
    maudDestroySpatializer(s);
    maudDestroyAcousticScene(scene);
}

static void TestMisuse(void)
{
    static const maudVector3 v[] = {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, NAN, 0.0f}};
    static const uint32_t bad[] = {0, 1, 3};
    static const uint32_t good[] = {0, 1, 2};
    static const uint32_t materials[] = {0};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    maudAcousticScene* scene = (maudAcousticScene*)&def;
    maudMesh mesh = {v, 2, good, materials, 1};
    def.meshes = &mesh;
    def.meshCount = 1;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_errorInvalid && scene == nullptr,
          "an index past the vertices");
    mesh = (maudMesh){v, 3, bad, materials, 1};
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_errorInvalid, "another");
    mesh = (maudMesh){v, 3, good, materials, 1};
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_errorInvalid, "a NaN vertex");
    mesh = (maudMesh){v, 2, good, nullptr, 1};
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_errorInvalid, "no materials");
    def.meshes = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_errorInvalid, "no meshes");
    def = maudDefaultAcousticSceneDef();
    def.cookie = 0;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_errorInvalid, "no cookie");
    maudDestroyAcousticScene(nullptr);
}

int main(void)
{
    TestSoup();
    TestWatertight();
    TestTiesAndNormals();
    TestHooks();
    TestMisuse();
    return s_failures == 0 ? 0 : 1;
}
