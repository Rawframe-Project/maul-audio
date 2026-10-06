// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Acoustic scene rays per second on a generated level: a grid of 32 by
// 32 rooms, 4 m square and 3 m high, joined by doorways (about 17,000
// triangles). Rays of up to 16 m between random points, queried for any
// hit and for the closest hit directly; then a spatializer's volumetric
// occlusion and transmission steps through the scene as its hooks; then
// the level with an instance of a 6,000-triangle object in every room,
// committed, queried and moved.

#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define GRID 32
#define ROOM 4.0f
#define TALL 3.0f
#define RAYS 200000

static maudVector3* s_vertices;
static uint32_t* s_indices;
static uint32_t s_vertexCount;
static uint32_t s_triangleCount;

static double Seconds(void)
{
    struct timespec t;
    timespec_get(&t, TIME_UTC);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

// A vertical quad from (x0, z0) to (x1, z1) between heights y0 and y1.
static void Quad(float x0, float z0, float x1, float z1, float y0, float y1)
{
    uint32_t v = s_vertexCount;
    s_vertices[v] = (maudVector3){x0, y0, z0};
    s_vertices[v + 1] = (maudVector3){x1, y0, z1};
    s_vertices[v + 2] = (maudVector3){x1, y1, z1};
    s_vertices[v + 3] = (maudVector3){x0, y1, z0};
    uint32_t* t = &s_indices[3 * s_triangleCount];
    t[0] = v;
    t[1] = v + 1;
    t[2] = v + 2;
    t[3] = v;
    t[4] = v + 2;
    t[5] = v + 3;
    s_vertexCount += 4;
    s_triangleCount += 2;
}

static void Floor(float x, float z, float y)
{
    uint32_t v = s_vertexCount;
    s_vertices[v] = (maudVector3){x, y, z};
    s_vertices[v + 1] = (maudVector3){x + ROOM, y, z};
    s_vertices[v + 2] = (maudVector3){x + ROOM, y, z + ROOM};
    s_vertices[v + 3] = (maudVector3){x, y, z + ROOM};
    uint32_t* t = &s_indices[3 * s_triangleCount];
    t[0] = v;
    t[1] = v + 2;
    t[2] = v + 1;
    t[3] = v;
    t[4] = v + 3;
    t[5] = v + 2;
    s_vertexCount += 4;
    s_triangleCount += 2;
}

// A wall segment of one room's length; inner ones have a doorway.
static void Wall(float x0, float z0, float x1, float z1, bool door)
{
    if (!door)
    {
        Quad(x0, z0, x1, z1, 0.0f, TALL);
        return;
    }
    float ax = x0 + (x1 - x0) * 0.375f;
    float az = z0 + (z1 - z0) * 0.375f;
    float bx = x0 + (x1 - x0) * 0.625f;
    float bz = z0 + (z1 - z0) * 0.625f;
    Quad(x0, z0, ax, az, 0.0f, TALL);
    Quad(bx, bz, x1, z1, 0.0f, TALL);
    Quad(ax, az, bx, bz, 2.2f, TALL);
}

static void Level(void)
{
    size_t quads = 2 * GRID * GRID + 2 * (GRID + 1) * GRID * 3;
    s_vertices = malloc(quads * 4 * sizeof(maudVector3));
    s_indices = malloc(quads * 6 * sizeof(uint32_t));
    for (int i = 0; i < GRID; ++i)
    {
        for (int j = 0; j < GRID; ++j)
        {
            Floor(ROOM * (float)i, ROOM * (float)j, 0.0f);
            Floor(ROOM * (float)i, ROOM * (float)j, TALL);
        }
    }
    for (int line = 0; line <= GRID; ++line)
    {
        bool inner = line > 0 && line < GRID;
        for (int k = 0; k < GRID; ++k)
        {
            float a = ROOM * (float)k;
            float l = ROOM * (float)line;
            Wall(l, a, l, a + ROOM, inner);
            Wall(a, l, a + ROOM, l, inner);
        }
    }
}

static uint32_t s_seed = 1;

static float Uniform(float low, float high)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return low + (high - low) * (float)(s_seed >> 8) / 16777216.0f;
}

static maudRay s_rays[RAYS];

static void MakeRays(void)
{
    float size = ROOM * GRID;
    for (int i = 0; i < RAYS; ++i)
    {
        float angle = Uniform(0.0f, 6.2831853f);
        float pitch = Uniform(-0.3f, 0.3f);
        s_rays[i] = (maudRay){{Uniform(0.0f, size), Uniform(0.2f, 2.8f), Uniform(0.0f, size)},
                              {cosf(angle) * cosf(pitch), sinf(pitch), sinf(angle) * cosf(pitch)},
                              0.0f,
                              Uniform(1.0f, 16.0f)};
    }
}

static long s_hookRays;

static void CountAny(const maudRay* rays, uint32_t count, uint8_t* occluded, void* scene)
{
    s_hookRays += count;
    maudSceneAnyHit(rays, count, occluded, scene);
}

static void CountClosest(const maudRay* rays, uint32_t count, maudRayHit* hits, void* scene)
{
    s_hookRays += count;
    maudSceneClosestHit(rays, count, hits, scene);
}

static void Steps(maudAcousticScene* scene)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = CountAny;
    def.closestHit = CountClosest;
    def.rayContext = scene;
    maudSpatializer* s = nullptr;
    if (maudCreateSpatializer(&def, &s) != maud_success)
    {
        return;
    }
    maudAcousticMaterial wall = {{0.1f, 0.1f, 0.1f}, 0.1f, {0.3f, 0.2f, 0.1f}};
    maudSourceDef sd = maudDefaultSourceDef();
    sd.occlusion = maud_occlusionVolumetric;
    if (maudSetMaterials(s, &wall, 1) != maud_success)
    {
        return;
    }
    for (int i = 0; i < 256; ++i)
    {
        maudSourceId id;
        maudPose pose = {{Uniform(40.0f, 88.0f), 1.5f, Uniform(40.0f, 88.0f)},
                         {0.0f, 0.0f, 0.0f, 1.0f}};
        if (maudCreateSource(s, &sd, &id) != maud_success ||
            maudSetSourcePose(s, id, &pose) != maud_success)
        {
            return;
        }
    }
    maudPose listener = {{64.0f, 1.6f, 64.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    s_hookRays = 0;
    double start = Seconds();
    for (int step = 0; step < 20; ++step)
    {
        if (maudSimulateDirect(s, &listener) != maud_success)
        {
            return;
        }
    }
    double elapsed = Seconds() - start;
    printf("spatializer steps, 256 volumetric sources: %.2f ms per step, %.2f M rays/s\n",
           elapsed / 20 * 1e3, (double)s_hookRays / elapsed * 1e-6);
    // One reverberation estimate of the default 2048 rays on one thread.
    s_hookRays = 0;
    start = Seconds();
    maudReverbResult reverb;
    if (maudSimulateReverb(s, &listener) != maud_success ||
        maudSimulateDirect(s, &listener) != maud_success || maudLatchResults(s) == 0 ||
        maudGetReverbResult(s, &reverb) != maud_success)
    {
        return;
    }
    elapsed = Seconds() - start;
    printf("reverberation estimate, 2048 rays: %.0f ms with a step, %.2f M ray queries, "
           "%.2f / %.2f / %.2f s\n",
           elapsed * 1e3, (double)s_hookRays * 1e-6, (double)reverb.reverbTime[0],
           (double)reverb.reverbTime[1], (double)reverb.reverbTime[2]);
    maudDestroySpatializer(s);
}

static const uint32_t s_boxFaces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                        2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};

#define OBJECT_BOXES 500

// An object of 500 boxes of 10 cm in a 1 m cube about its origin.
static maudMesh Object(void)
{
    static maudVector3 v[OBJECT_BOXES * 8];
    static uint32_t indices[OBJECT_BOXES * 36];
    static uint32_t materials[OBJECT_BOXES * 12];
    for (uint32_t b = 0; b < OBJECT_BOXES; ++b)
    {
        float c[3] = {Uniform(-0.45f, 0.45f), Uniform(-0.45f, 0.45f), Uniform(-0.45f, 0.45f)};
        for (uint32_t i = 0; i < 8; ++i)
        {
            v[b * 8 + i] =
                (maudVector3){c[0] + ((i & 1) ? 0.05f : -0.05f), c[1] + ((i & 2) ? 0.05f : -0.05f),
                              c[2] + ((i & 4) ? 0.05f : -0.05f)};
        }
        for (uint32_t k = 0; k < 36; ++k)
        {
            indices[b * 36 + k] = b * 8 + s_boxFaces[k];
        }
    }
    return (maudMesh){v, OBJECT_BOXES * 8, indices, materials, OBJECT_BOXES * 12};
}

// Where the object stands in room (x, z) at a time: turning on the spot.
static maudInstanceTransform Placed(uint32_t x, uint32_t z, float time)
{
    float angle = 0.5f * (time + (float)(x * GRID + z));
    return (maudInstanceTransform){{((float)x + 0.5f) * ROOM, 1.0f, ((float)z + 0.5f) * ROOM},
                                   {0.0f, sinf(angle), 0.0f, cosf(angle)},
                                   1.0f};
}

static double BestClosest(maudAcousticScene* scene, maudRayHit* hits)
{
    double best = 1e9;
    for (int run = 0; run < 3; ++run)
    {
        double start = Seconds();
        maudSceneClosestHit(s_rays, RAYS, hits, scene);
        double e = Seconds() - start;
        best = e < best ? e : best;
    }
    return best;
}

static void Instanced(const maudMesh* level, maudRayHit* hits)
{
    maudMesh object = Object();
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = level;
    def.meshCount = 1;
    def.instanceMeshes = &object;
    def.instanceMeshCount = 1;
    def.instanceCapacity = GRID * GRID;
    maudAcousticScene* scene = nullptr;
    if (maudCreateAcousticScene(&def, &scene) != maud_success)
    {
        return;
    }
    static maudSceneInstanceId ids[GRID * GRID];
    for (uint32_t i = 0; i < GRID * GRID; ++i)
    {
        maudInstanceTransform t = Placed(i / GRID, i % GRID, 0.0f);
        if (maudCreateSceneInstance(scene, 0, &t, &ids[i]) != maud_success)
        {
            maudDestroyAcousticScene(scene);
            return;
        }
    }
    double start = Seconds();
    (void)maudCommitAcousticScene(scene);
    printf("instanced:   %u instances of %u triangles, committed in %.3f ms\n", GRID * GRID,
           object.triangleCount, (Seconds() - start) * 1e3);
    printf("closest hit: %.2f M rays/s among them\n", RAYS / BestClosest(scene, hits) * 1e-6);
    double moving = 0.0;
    for (int frame = 1; frame <= 10; ++frame)
    {
        start = Seconds();
        for (uint32_t i = 0; i < GRID * GRID; ++i)
        {
            maudInstanceTransform t = Placed(i / GRID, i % GRID, (float)frame);
            (void)maudMoveSceneInstance(scene, ids[i], &t);
        }
        (void)maudCommitAcousticScene(scene);
        moving += Seconds() - start;
    }
    printf("moving:      all %u moved and committed in %.3f ms a frame\n", GRID * GRID,
           moving / 10.0 * 1e3);
    maudDestroyAcousticScene(scene);
}

int main(void)
{
    Level();
    static uint32_t materials[1 << 16];
    maudMesh mesh = {s_vertices, s_vertexCount, s_indices, materials, s_triangleCount};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    double start = Seconds();
    if (maudCreateAcousticScene(&def, &scene) != maud_success)
    {
        return 1;
    }
    printf("level: %u triangles, built in %.1f ms\n", s_triangleCount, (Seconds() - start) * 1e3);
    MakeRays();
    static uint8_t occluded[RAYS];
    static maudRayHit hits[RAYS];
    double best = 1e9;
    for (int run = 0; run < 3; ++run)
    {
        start = Seconds();
        maudSceneAnyHit(s_rays, RAYS, occluded, scene);
        double e = Seconds() - start;
        best = e < best ? e : best;
    }
    long hit = 0;
    for (int i = 0; i < RAYS; ++i)
    {
        hit += occluded[i];
    }
    printf("any hit:     %.2f M rays/s (%.0f %% hit)\n", RAYS / best * 1e-6, 100.0 * hit / RAYS);
    best = 1e9;
    for (int run = 0; run < 3; ++run)
    {
        start = Seconds();
        maudSceneClosestHit(s_rays, RAYS, hits, scene);
        double e = Seconds() - start;
        best = e < best ? e : best;
    }
    printf("closest hit: %.2f M rays/s\n", RAYS / best * 1e-6);
    Steps(scene);
    Instanced(&mesh, hits);
    maudDestroyAcousticScene(scene);
    free(s_vertices);
    free(s_indices);
    return 0;
}
