// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the geometry a host hands over: an acoustic scene's mesh, the
// rays it answers, and a probe set's points or generation box. The
// first byte chooses the range and whether the probes are the input's
// points or generated over the scene; the next two the vertex and
// triangle counts. Then come the vertices as raw floats, a byte per
// index (vertexCount itself one past the last vertex), a generation's
// box corners, spacing and height as raw floats, and the points as raw
// floats. Each call is held to its header's promises:
//
// - a scene is made exactly when every vertex is finite and every index
//   names a vertex;
// - a hit lies within its ray's distances with a unit normal, and
//   any-hit agrees with closest-hit;
// - a probe set's rows are ascending and symmetric, each link within
//   range and measured, and the set saved as a bake reads back the same;
// - a generation's def is valid exactly when its values are in their
//   ranges and its box is finite and has height, and it is refused with
//   maud_errorLimit exactly when its box holds too many columns.

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
    MAX_PAIRS = 4096,
    // Generations of more columns, but within the limit, are not built:
    // each column walks up to 32 rays, too slow for a fuzzer.
    FUZZ_COLUMNS = 1024
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

// A generation's box, as the input gives it.
typedef struct Box
{
    maudVector3 low;
    maudVector3 high;
    float spacing;
    float height;
} Box;

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

// Whether a generation's def holds values the header allows.
static bool BoxValid(const Box* box)
{
    return box->spacing >= 0.25f && box->spacing <= 100.0f && box->height >= 0.1f &&
           box->height <= 10.0f && Finite(box->low) && Finite(box->high) &&
           box->low.x <= box->high.x && box->low.y < box->high.y && box->low.z <= box->high.z;
}

// The columns along one side: as many whole spacings as fit, at least
// one.
static double Columns(float low, float high, float spacing)
{
    return fmax(1.0, floor(((double)high - (double)low) / (double)spacing));
}

// Builds a set from the points, or generated over the scene in the box
// the input gives.
static void CheckProbes(maudScene* scene, const maudVector3* points, uint32_t count, const Box* box,
                        float range, bool generate)
{
    const maudAllocator allocator = {nullptr, nullptr, nullptr};
    maudProbeSetDef def = maudDefaultProbeSetDef();
    def.range = range;
    bool expected = true;
    double columns = 0.0;
    if (generate)
    {
        def.boxMin = box->low;
        def.boxMax = box->high;
        def.spacing = box->spacing;
        def.height = box->height;
        expected = BoxValid(box);
        columns = expected ? Columns(box->low.x, box->high.x, box->spacing) *
                                 Columns(box->low.z, box->high.z, box->spacing)
                           : 0.0;
    }
    else
    {
        def.points = points;
        def.pointCount = count;
        for (uint32_t i = 0; i < count; ++i)
        {
            expected = expected && Finite(points[i]);
        }
    }
    bool valid = maudProbeSetDefValid(&def);
    Expect(valid == expected);
    bool over = columns > (double)MAUD_MAX_PROBE_COLUMNS;
    if (!valid || (!over && columns > FUZZ_COLUMNS))
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
    Expect(over ? built == maud_errorLimit : built == maud_success || built == maud_errorCapacity);
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
    Box box;
    box.low = Vector(&in);
    box.high = Vector(&in);
    box.spacing = Float(&in);
    box.height = Float(&in);
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
    CheckProbes(scene, points, count, &box, range, generate);
    maudDestroyScene(scene);
    return 0;
}
