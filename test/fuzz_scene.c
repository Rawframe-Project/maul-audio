// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the geometry a host hands over: an acoustic scene's mesh, the
// rays it answers, and a probe set's points or generation box. The
// first byte chooses the range and whether the probes are the input's
// points or generated over the scene; the next two the vertex and
// triangle counts. Then come the vertices as raw floats, a byte per
// index (vertexCount itself one past the last vertex), six bytes that
// place a generation's box, and the points as raw floats. Each call is
// held to its header's promises:
//
// - a scene is made exactly when every vertex is finite and every index
//   names a vertex;
// - a hit lies within its ray's distances with a unit normal, and
//   any-hit agrees with closest-hit;
// - a probe set's rows are ascending and symmetric, each link within
//   range and measured, and the set saved as a bake reads back the same.

#include "bake_file.h"
#include "fpenv.h"
#include "probe_graph.h"

#include "maul-audio/scene.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

enum
{
    MAX_VERTICES = 64,
    MAX_TRIANGLES = 32,
    MAX_POINTS = 64,
    MAX_PROBES = 256,
    MAX_PAIRS = 4096
};

static const float s_ranges[4] = {0.5f, 1.5f, 5.0f, 40.0f};

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// The input read from its start, zeros past its end.
typedef struct Input
{
    const uint8_t* data;
    size_t size;
    size_t at;
} Input;

static uint8_t Byte(Input* in)
{
    return in->at < in->size ? in->data[in->at++] : 0;
}

static float Float(Input* in)
{
    uint32_t bits = 0;
    for (int k = 0; k < 4; ++k)
    {
        bits |= (uint32_t)Byte(in) << (8 * k);
    }
    float v = 0.0f;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

static maudVector3 Vector(Input* in)
{
    float x = Float(in);
    float y = Float(in);
    float z = Float(in);
    return (maudVector3){x, y, z};
}

static bool Finite(maudVector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static float Distance(maudVector3 a, maudVector3 b)
{
    float x = b.x - a.x;
    float y = b.y - a.y;
    float z = b.z - a.z;
    return sqrtf(x * x + y * y + z * z);
}

// The ray from a to b, if its length is finite and not zero.
static bool RayBetween(maudVector3 a, maudVector3 b, maudRay* ray)
{
    float length = Distance(a, b);
    if (!(length > 0.0f && length < INFINITY))
    {
        return false;
    }
    maudVector3 d = {(b.x - a.x) / length, (b.y - a.y) / length, (b.z - a.z) / length};
    *ray = (maudRay){a, d, 0.0f, length};
    return Finite(d);
}

// Rays between consecutive points: each hit within its distances with a
// unit normal, and any-hit the same answer.
static void CheckRays(maudScene* scene, const maudVector3* points, uint32_t count)
{
    for (uint32_t i = 0; i + 1 < count; ++i)
    {
        maudRay ray;
        if (!RayBetween(points[i], points[i + 1], &ray))
        {
            continue;
        }
        maudRayHit hit;
        uint8_t occluded = 2;
        maudFindClosestHit(&ray, 1, &hit, scene);
        maudFindAnyHit(&ray, 1, &occluded, scene);
        bool missed = hit.distance == INFINITY;
        Expect(missed || (hit.distance >= ray.minDistance && hit.distance <= ray.maxDistance &&
                          fabsf(sqrtf(hit.normal.x * hit.normal.x + hit.normal.y * hit.normal.y +
                                      hit.normal.z * hit.normal.z) -
                                1.0f) < 1e-3f));
        Expect(occluded == (missed ? 0 : 1));
    }
}

// A built set's rows: ascending, without the probe itself, listed from
// both ends, each link within range and measured.
static void CheckGraph(const maudProbeGraph* g)
{
    Expect(g->offsets[0] == 0 && g->offsets[g->count] == 2 * g->links);
    for (uint32_t i = 0; i < g->count; ++i)
    {
        for (uint32_t e = g->offsets[i]; e < g->offsets[i + 1]; ++e)
        {
            uint32_t j = g->neighbours[e];
            Expect(j < g->count && j != i);
            Expect(e == g->offsets[i] || g->neighbours[e - 1] < j);
            float length = Distance(g->points[i], g->points[j]);
            Expect(length <= g->range && g->lengths[e] == length);
            bool back = false;
            for (uint32_t f = g->offsets[j]; f < g->offsets[j + 1]; ++f)
            {
                back = back || g->neighbours[f] == i;
            }
            Expect(back);
        }
    }
}

// The set saved as a bake of probes and links alone, read back: the
// same arrays.
static void CheckSaved(const maudProbeGraph* g, const maudAllocator* allocator)
{
    size_t bytes = maudBakeFileBytes(g, nullptr, 0, 0);
    Expect(bytes > 0);
    uint8_t* file = malloc(bytes);
    Expect(file != nullptr);
    maudWriteBakeFile(g, nullptr, 0, 0, file);
    const maudBakeLimits limits = {MAX_PROBES, MAX_PAIRS, 0, 0};
    maudProbeGraph read;
    maudProbeBake bake;
    Expect(maudReadBakeFile(file, bytes, &limits, allocator, &read, &bake) == maud_success);
    Expect(read.count == g->count && read.links == g->links && read.range == g->range);
    Expect(memcmp(read.points, g->points, (size_t)g->count * sizeof(maudVector3)) == 0 &&
           memcmp(read.offsets, g->offsets, ((size_t)g->count + 1) * sizeof(uint32_t)) == 0 &&
           memcmp(read.neighbours, g->neighbours, 2 * (size_t)g->links * sizeof(uint32_t)) == 0 &&
           memcmp(read.lengths, g->lengths, 2 * (size_t)g->links * sizeof(float)) == 0);
    maudReleaseProbeGraph(allocator, &read);
    free(file);
}

// Builds a set from the points, or generated over the scene in a box
// the input places, small enough that a run stays quick.
static void CheckProbes(maudScene* scene, const maudVector3* points, uint32_t count,
                        const uint8_t box[6], float range, bool generate)
{
    const maudAllocator allocator = {nullptr, nullptr, nullptr};
    maudProbeSetDef def = maudDefaultProbeSetDef();
    def.range = range;
    if (generate)
    {
        float x = (float)(box[0] % 16) - 8.0f;
        float y = (float)(box[1] % 16) - 8.0f;
        float z = (float)(box[2] % 16) - 8.0f;
        float extent = 1.0f + (float)(box[3] % 16);
        def.boxMin = (maudVector3){x, y, z};
        def.boxMax = (maudVector3){x + extent, y + extent, z + extent};
        def.spacing = 0.5f + 0.25f * (float)(box[4] % 16);
        def.height = 0.1f + 0.5f * (float)(box[5] % 16);
    }
    else
    {
        def.points = points;
        def.pointCount = count;
    }
    bool valid = maudProbeSetDefValid(&def);
    bool finite = true;
    for (uint32_t i = 0; i < count && !generate; ++i)
    {
        finite = finite && Finite(points[i]);
    }
    Expect(valid == (generate || finite));
    if (!valid)
    {
        return;
    }
    maudProbeQueries queries = {.anyHit = maudFindAnyHit,
                                .closestHit = maudFindClosestHit,
                                .rayContext = scene,
                                .allocator = &allocator,
                                .maxProbes = MAX_PROBES,
                                .maxPairs = MAX_PAIRS};
    maudProbeGraph graph;
    uint64_t token = maudFloatEnvEnter();
    maudResult built = maudBuildProbeGraph(&queries, &def, &graph);
    maudFloatEnvLeave(token);
    Expect(built == maud_success || built == maud_errorCapacity);
    if (built == maud_success)
    {
        CheckGraph(&graph);
        CheckSaved(&graph, &allocator);
        maudReleaseProbeGraph(&allocator, &graph);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Input in = {data, size, 0};
    uint8_t mode = Byte(&in);
    float range = s_ranges[mode & 3];
    bool generate = (mode & 4) != 0;
    uint32_t vertexCount = 1u + Byte(&in) % MAX_VERTICES;
    uint32_t triangleCount = Byte(&in) % (MAX_TRIANGLES + 1);
    static maudVector3 vertices[MAX_VERTICES];
    static uint32_t indices[3 * MAX_TRIANGLES];
    static uint32_t materials[MAX_TRIANGLES];
    static maudVector3 points[MAX_POINTS];
    bool valid = true;
    for (uint32_t i = 0; i < vertexCount; ++i)
    {
        vertices[i] = Vector(&in);
        valid = valid && (triangleCount == 0 || Finite(vertices[i]));
    }
    for (uint32_t i = 0; i < 3 * triangleCount; ++i)
    {
        indices[i] = Byte(&in) % (vertexCount + 1);
        valid = valid && indices[i] < vertexCount;
    }
    uint8_t box[6];
    for (int k = 0; k < 6; ++k)
    {
        box[k] = Byte(&in);
    }
    uint32_t count = 0;
    while (count < MAX_POINTS && in.at < in.size)
    {
        points[count++] = Vector(&in);
    }
    maudMesh mesh = {vertices, vertexCount, indices, materials, triangleCount};
    maudSceneDef def = maudDefaultSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudScene* scene = nullptr;
    maudResult made = maudCreateScene(&def, &scene);
    Expect(made == (valid ? maud_success : maud_errorInvalid));
    CheckRays(scene, points, count);
    CheckProbes(scene, points, count, box, range, generate);
    maudDestroyScene(scene);
    return 0;
}
