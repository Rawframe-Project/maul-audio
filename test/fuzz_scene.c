// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the geometry a host hands over: an acoustic scene's mesh, the
// rays it answers, the instances a host makes of it, and a probe set's
// points or generation box. The first byte chooses the range, whether
// the probes are the input's points or generated over the scene, and
// whether the mesh is also the scene's instancing mesh; the next two
// the vertex and triangle counts. Then come the vertices as raw floats,
// a byte per index (vertexCount itself one past the last vertex), a
// generation's box corners, spacing and height as raw floats, with
// instances their capacity and calls, and the points as raw floats.
// Each call is held to its header's promises:
//
// - a scene is made exactly when every vertex is finite and every index
//   names a vertex;
// - a hit lies within its ray's distances with a unit normal, and
//   any-hit agrees with closest-hit;
// - a probe set's rows are ascending and symmetric, each link within
//   range and measured, and the set saved as a bake reads back the same;
// - a generation's def is valid exactly when its values are in their
//   ranges and its box is finite and has height, and it is refused with
//   maud_errorLimit exactly when its box holds too many columns;
// - making and moving an instance succeed exactly when its mesh is in
//   range and its transform finite with a nonzero orientation and a
//   scale in range, and the placed mesh is surely within the float
//   range; one placed near or past that range may be refused as
//   invalid; past the capacity it is maud_errorCapacity, a
//   destroyed instance's id is maud_errorStale, and every refusal for
//   an invalid argument counts as misuse;
// - with every instance destroyed and committed, the rays answer
//   exactly as before any instance was made.

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
    MAX_INSTANCES = 8,
    MAX_CALLS = 32,
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

// What the input asks of a scene's instances: each call a kind byte,
// a byte choosing the mesh or the id, and a transform as raw floats.
typedef struct Call
{
    uint8_t kind;
    uint8_t which;
    maudSceneInstanceTransform transform;
} Call;

enum
{
    CALL_CREATE,
    CALL_MOVE,
    CALL_DESTROY,
    CALL_COMMIT,
    CALL_KINDS
};

// The ids the calls got, and whether each is still live.
typedef struct Made
{
    maudSceneInstanceId ids[MAX_CALLS];
    bool live[MAX_CALLS];
    uint32_t count;
    uint32_t liveCount;
} Made;

// Whether a transform is one scene.h allows: a finite position, a
// finite orientation of nonzero length, a scale of 0.001 to 1,000.
static bool TransformValid(const maudSceneInstanceTransform* t)
{
    maudQuaternion q = t->orientation;
    return Finite(t->position) && isfinite(q.x) && isfinite(q.y) && isfinite(q.z) &&
           isfinite(q.w) && (q.x != 0.0f || q.y != 0.0f || q.z != 0.0f || q.w != 0.0f) &&
           t->scale >= 0.001f && t->scale <= 1000.0f;
}

// Whether a mesh whose vertices are within reach on every axis, placed
// by a valid transform, is surely within the float range. A turned
// corner is within far = |position| + scale x sqrt(3) x reach on each
// axis. The commit's padding sums high - low, |low| and |high| in float,
// up to 4 x far, before it takes its share, so that sum overflows past a
// quarter of FLT_MAX, about 8.5e37: a bound below it is surely taken.
static bool SurelyInRange(const maudSceneInstanceTransform* t, double reach)
{
    double far = fmax(fmax(fabs((double)t->position.x), fabs((double)t->position.y)),
                      fabs((double)t->position.z)) +
                 (double)t->scale * sqrt(3.0) * reach;
    return far * 1.001 + 1.0 < 8e37;
}

// The id a call names: one an earlier call got, or, past those, 0 or
// one past the capacity, which no instance has.
static maudSceneInstanceId Named(const Made* made, uint8_t which, uint32_t capacity, int* slot)
{
    *slot = -1;
    if (which < made->count)
    {
        *slot = which;
        return made->ids[which];
    }
    return (which & 1) != 0 ? (maudSceneInstanceId){capacity + 1, 1} : (maudSceneInstanceId){0, 0};
}

// Makes an instance of mesh 0 or of mesh 1, which the scene lacks.
static maudResult Create(maudScene* scene, const Call* call, uint32_t capacity, double reach,
                         Made* made)
{
    maudSceneInstanceId id = {7, 7};
    maudResult r = maudCreateSceneInstance(scene, call->which % 2, &call->transform, &id);
    bool valid = call->which % 2 == 0 && TransformValid(&call->transform);
    bool full = made->liveCount == capacity;
    if (!valid)
    {
        // Both refusals apply to an invalid call on a full scene.
        Expect(r == maud_errorInvalid || (full && r == maud_errorCapacity));
    }
    else if (SurelyInRange(&call->transform, reach))
    {
        Expect(r == (full ? maud_errorCapacity : maud_success));
    }
    else
    {
        Expect(r == maud_errorInvalid || r == (full ? maud_errorCapacity : maud_success));
    }
    Expect((r == maud_success) == (id.index1 != 0));
    if (r == maud_success && made->count < MAX_CALLS)
    {
        made->ids[made->count] = id;
        made->live[made->count++] = true;
        made->liveCount += 1;
    }
    return r;
}

static maudResult Change(maudScene* scene, const Call* call, uint32_t capacity, double reach,
                         Made* made)
{
    int slot = -1;
    maudSceneInstanceId id = Named(made, call->which, capacity, &slot);
    bool known = slot >= 0;
    bool live = known && made->live[slot];
    if (call->kind == CALL_MOVE)
    {
        maudResult r = maudSceneInstance_Move(scene, id, &call->transform);
        bool valid = TransformValid(&call->transform);
        bool sure = valid && SurelyInRange(&call->transform, reach);
        if (!known)
        {
            Expect(r == maud_errorInvalid);
        }
        else if (!live)
        {
            // Both refusals apply to an invalid transform for a destroyed id.
            Expect(r == maud_errorStale || (!sure && r == maud_errorInvalid));
        }
        else if (!valid || sure)
        {
            Expect(r == (valid ? maud_success : maud_errorInvalid));
        }
        else
        {
            Expect(r == maud_success || r == maud_errorInvalid);
        }
        return r;
    }
    maudResult r = maudDestroySceneInstance(scene, id);
    Expect(r == (!known ? maud_errorInvalid : live ? maud_success : maud_errorStale));
    if (live)
    {
        made->live[slot] = false;
        made->liveCount -= 1;
    }
    return r;
}

// Runs the calls; a commit asks the rays. Then destroys every instance
// and commits: the rays must answer as they did before.
static void CheckInstances(maudScene* scene, const Call* calls, uint32_t callCount,
                           uint32_t capacity, double reach, const maudVector3* points,
                           uint32_t count)
{
    static maudRay rays[MAX_POINTS];
    static maudRayHit before[MAX_POINTS];
    static maudRayHit after[MAX_POINTS];
    uint32_t rayCount = 0;
    for (uint32_t i = 0; i + 1 < count; ++i)
    {
        rayCount += RayBetween(points[i], points[i + 1], &rays[rayCount]) ? 1 : 0;
    }
    maudFindClosestHit(rays, rayCount, before, scene);
    Made made = {0};
    for (uint32_t c = 0; c < callCount; ++c)
    {
        uint64_t misuse = maudScene_GetMisuse(scene);
        maudResult r = maud_success;
        switch (calls[c].kind)
        {
        case CALL_CREATE:
            r = Create(scene, &calls[c], capacity, reach, &made);
            break;
        case CALL_MOVE:
        case CALL_DESTROY:
            r = Change(scene, &calls[c], capacity, reach, &made);
            break;
        default:
            r = maudScene_Commit(scene);
            Expect(r == maud_success);
            CheckRays(scene, points, count);
            break;
        }
        // Each refusal for an invalid argument counts, nothing else.
        Expect(maudScene_GetMisuse(scene) - misuse == (r == maud_errorInvalid ? 1u : 0u));
    }
    for (uint32_t i = 0; i < made.count; ++i)
    {
        if (made.live[i])
        {
            Expect(maudDestroySceneInstance(scene, made.ids[i]) == maud_success);
        }
    }
    Expect(maudScene_Commit(scene) == maud_success);
    maudFindClosestHit(rays, rayCount, after, scene);
    for (uint32_t i = 0; i < rayCount; ++i)
    {
        Expect(after[i].distance == before[i].distance && after[i].normal.x == before[i].normal.x &&
               after[i].normal.y == before[i].normal.y && after[i].normal.z == before[i].normal.z &&
               after[i].material == before[i].material);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Input in = {data, size, 0};
    uint8_t mode = Byte(&in);
    float range = s_ranges[mode & 3];
    bool generate = (mode & 4) != 0;
    bool instancing = (mode & 8) != 0;
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
    static Call calls[MAX_CALLS];
    uint32_t capacity = 0;
    uint32_t callCount = 0;
    if (instancing)
    {
        capacity = Byte(&in) % (MAX_INSTANCES + 1);
        callCount = Byte(&in) % (MAX_CALLS + 1);
        for (uint32_t c = 0; c < callCount; ++c)
        {
            calls[c].kind = Byte(&in) % CALL_KINDS;
            calls[c].which = Byte(&in);
            calls[c].transform.position = Vector(&in);
            calls[c].transform.orientation.x = Float(&in);
            calls[c].transform.orientation.y = Float(&in);
            calls[c].transform.orientation.z = Float(&in);
            calls[c].transform.orientation.w = Float(&in);
            calls[c].transform.scale = Float(&in);
        }
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
    def.instanceMeshes = instancing ? &mesh : nullptr;
    def.instanceMeshCount = instancing ? 1 : 0;
    def.instanceCapacity = capacity;
    maudScene* scene = nullptr;
    maudResult made = maudCreateScene(&def, &scene);
    Expect(made == (valid ? maud_success : maud_errorInvalid));
    CheckRays(scene, points, count);
    if (scene != nullptr && instancing)
    {
        // The instancing mesh's farthest coordinate, a bound on its box.
        double reach = 0.0;
        for (uint32_t i = 0; i < vertexCount; ++i)
        {
            reach = fmax(reach, fmax(fmax(fabs((double)vertices[i].x), fabs((double)vertices[i].y)),
                                     fabs((double)vertices[i].z)));
        }
        CheckInstances(scene, calls, callCount, capacity, reach, points, count);
    }
    CheckProbes(scene, points, count, &box, range, generate);
    maudDestroyScene(scene);
    return 0;
}
