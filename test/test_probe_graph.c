// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Probe sets (whitebox): in an L-shaped corridor every pair within range
// is linked exactly when a ray between them is clear, the points given
// in any order, each row ascending, symmetric and measured; the set is
// the same byte for byte when the tasks run one item at a time in
// reverse; generation over a
// mezzanine and a low shelf puts a probe on each storey and none under
// the shelf, and on a tower's top eight storeys only; without an
// any-hit query every pair within range is linked; the limits and a
// missing closest-hit query are refused.

#include "allocator.h"
#include "probe_graph.h"
#include "test_harness.h"

#include "maul-audio/scene.h"

#include <math.h>
#include <string.h>

enum
{
    MAX_BOXES = 10
};

static maudVector3 s_vertices[8 * MAX_BOXES];
static uint32_t s_indices[36 * MAX_BOXES];
static uint32_t s_materials[12 * MAX_BOXES];
static uint32_t s_boxes;

// Adds a solid box to the scene being built.
static void Box(maudVector3 low, maudVector3 high)
{
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    uint32_t base = s_boxes * 8;
    for (uint32_t i = 0; i < 8; ++i)
    {
        s_vertices[base + i] = (maudVector3){(i & 1) ? high.x : low.x, (i & 2) ? high.y : low.y,
                                             (i & 4) ? high.z : low.z};
    }
    for (uint32_t k = 0; k < 36; ++k)
    {
        s_indices[s_boxes * 36 + k] = base + faces[k];
    }
    s_boxes += 1;
}

static maudAcousticScene* Scene(void)
{
    maudMesh mesh = {s_vertices, s_boxes * 8, s_indices, s_materials, s_boxes * 12};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a scene");
    s_boxes = 0;
    return scene;
}

static const maudAllocator s_allocator = {nullptr, nullptr, nullptr};

static maudProbeQueries Queries(maudAcousticScene* scene)
{
    return (maudProbeQueries){.anyHit = maudSceneAnyHit,
                              .closestHit = maudSceneClosestHit,
                              .rayContext = scene,
                              .allocator = &s_allocator,
                              .maxProbes = 4096,
                              .maxPairs = 1u << 20};
}

static void* Reverse(maudTaskFn* task, uint32_t itemCount, uint32_t minRange, void* taskContext,
                     void* userContext)
{
    (void)minRange;
    for (uint32_t i = itemCount; i-- > 0;)
    {
        task(i, i + 1, taskContext);
    }
    return userContext;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
}

static bool Linked(const maudProbeGraph* g, uint32_t i, uint32_t j)
{
    for (uint32_t e = g->offsets[i]; e < g->offsets[i + 1]; ++e)
    {
        if (g->neighbours[e] == j)
        {
            return true;
        }
    }
    return false;
}

static float Distance(maudVector3 a, maudVector3 b)
{
    return sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
}

// Every pair within range against one ray of its own; rows ascending
// and measured.
static void CheckLinks(maudAcousticScene* scene, const maudProbeGraph* g, float range)
{
    uint32_t wrong = 0;
    uint32_t links = 0;
    for (uint32_t i = 0; i < g->count; ++i)
    {
        for (uint32_t e = g->offsets[i]; e < g->offsets[i + 1]; ++e)
        {
            bool ascending = e == g->offsets[i] || g->neighbours[e - 1] < g->neighbours[e];
            bool measured = g->lengths[e] == Distance(g->points[i], g->points[g->neighbours[e]]);
            wrong += ascending && measured ? 0 : 1;
        }
        for (uint32_t j = 0; j < g->count; ++j)
        {
            float d = Distance(g->points[i], g->points[j]);
            if (j == i || d > range)
            {
                wrong += Linked(g, i, j) ? 1 : 0;
                continue;
            }
            maudVector3 from = g->points[i];
            maudVector3 to = g->points[j];
            maudRay ray = {
                from, {(to.x - from.x) / d, (to.y - from.y) / d, (to.z - from.z) / d}, 0.0f, d};
            uint8_t occluded = 0;
            maudSceneAnyHit(&ray, 1, &occluded, scene);
            links += occluded == 0 && j > i ? 1 : 0;
            wrong += (occluded == 0) == Linked(g, i, j) ? 0 : 1;
        }
    }
    printf("corridor: %u probes, %u links, %u wrong\n", g->count, g->links, wrong);
    CHECK(wrong == 0 && links == g->links && g->links > 0, "linked when a ray is clear");
}

static void TestCorridor(void)
{
    // The L's inside corner: a solid block, the corridor around it.
    Box((maudVector3){0.0f, 0.0f, 3.0f}, (maudVector3){17.0f, 3.0f, 20.0f});
    maudAcousticScene* scene = Scene();
    static maudVector3 points[400];
    uint32_t count = 0;
    for (int x = 0; x < 20; ++x)
    {
        for (int z = 0; z < 20; ++z)
        {
            if (z < 3 || x >= 17)
            {
                points[count++] = (maudVector3){(float)x + 0.5f, 1.5f, (float)z + 0.5f};
            }
        }
    }
    maudProbeSetDef def = {.points = points, .pointCount = count, .range = 3.0f};
    maudProbeQueries q = Queries(scene);
    maudProbeGraph plain;
    CHECK(maudBuildProbeGraph(&q, &def, &plain) == maud_success, "built");
    CheckLinks(scene, &plain, def.range);
    // The same points shuffled (a stride of 50, prime to the 111 of
    // them): linked alike.
    static maudVector3 shuffled[400];
    for (uint32_t i = 0; i < count; ++i)
    {
        shuffled[i] = points[(i * 50u) % count];
    }
    maudProbeSetDef mixed = {.points = shuffled, .pointCount = count, .range = 3.0f};
    maudProbeGraph other;
    CHECK(count == 111 && maudBuildProbeGraph(&q, &mixed, &other) == maud_success,
          "built from shuffled points");
    CheckLinks(scene, &other, mixed.range);
    maudReleaseProbeGraph(&s_allocator, &other);
    q.enqueueTask = Reverse;
    q.finishTask = Finish;
    maudProbeGraph reversed;
    CHECK(maudBuildProbeGraph(&q, &def, &reversed) == maud_success, "built in reverse");
    CHECK(reversed.bytes == plain.bytes && memcmp(reversed.memory, plain.memory, plain.bytes) == 0,
          "the same set however the tasks run");
    maudReleaseProbeGraph(&s_allocator, &reversed);
    // Without an any-hit query every pair within range is linked.
    q.anyHit = nullptr;
    maudProbeGraph clear;
    CHECK(maudBuildProbeGraph(&q, &def, &clear) == maud_success, "built clear");
    uint32_t pairs = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        for (uint32_t j = i + 1; j < count; ++j)
        {
            pairs += Distance(points[i], points[j]) <= def.range ? 1 : 0;
        }
    }
    CHECK(clear.links == pairs && pairs > plain.links, "every pair without the query");
    // Limits: too few pairs or probes allowed.
    q.maxPairs = pairs - 1;
    maudProbeGraph refused;
    CHECK(maudBuildProbeGraph(&q, &def, &refused) == maud_errorCapacity &&
              refused.memory == nullptr,
          "past maxPairs");
    q.maxPairs = pairs;
    CHECK(maudBuildProbeGraph(&q, &def, &refused) == maud_success, "at maxPairs");
    maudReleaseProbeGraph(&s_allocator, &refused);
    q.maxProbes = count - 1;
    CHECK(maudBuildProbeGraph(&q, &def, &refused) == maud_errorCapacity, "past maxProbes");
    maudReleaseProbeGraph(&s_allocator, &clear);
    maudReleaseProbeGraph(&s_allocator, &plain);
    maudDestroyAcousticScene(scene);
}

static void TestGeneration(void)
{
    // A ground slab over 4 x 4 m; a mezzanine over x < 2 at 3 m; a shelf
    // at 1 m over x > 2, z < 2.
    Box((maudVector3){0.0f, -0.2f, 0.0f}, (maudVector3){4.0f, 0.0f, 4.0f});
    Box((maudVector3){0.0f, 3.0f, 0.0f}, (maudVector3){2.0f, 3.2f, 4.0f});
    Box((maudVector3){2.0f, 1.0f, 0.0f}, (maudVector3){4.0f, 1.1f, 2.0f});
    maudAcousticScene* scene = Scene();
    maudProbeSetDef def = {.boxMin = {0.0f, -1.0f, 0.0f},
                           .boxMax = {4.0f, 6.0f, 4.0f},
                           .spacing = 1.0f,
                           .height = 1.5f,
                           .range = 2.0f};
    CHECK(maudProbeSetDefValid(&def), "a valid def");
    maudProbeQueries q = Queries(scene);
    maudProbeGraph g;
    CHECK(maudBuildProbeGraph(&q, &def, &g) == maud_success, "generated");
    uint32_t wrong = 0;
    for (uint32_t i = 0; i < g.count; ++i)
    {
        maudVector3 p = g.points[i];
        bool upper = p.x < 2.0f && fabsf(p.y - 4.7f) < 1e-4f;
        bool ground = (p.x < 2.0f || p.z > 2.0f) && fabsf(p.y - 1.5f) < 1e-4f;
        bool shelf = p.x > 2.0f && p.z < 2.0f && fabsf(p.y - 2.6f) < 1e-4f;
        bool centred =
            fabsf(p.x - floorf(p.x) - 0.5f) < 1e-6f && fabsf(p.z - floorf(p.z) - 0.5f) < 1e-6f;
        wrong += (upper || ground || shelf) && centred ? 0 : 1;
    }
    printf("generated: %u probes, %u misplaced\n", g.count, wrong);
    CHECK(g.count == 24 && wrong == 0, "a probe on each storey, none under the shelf");
    maudReleaseProbeGraph(&s_allocator, &g);
    q.maxProbes = 23;
    CHECK(maudBuildProbeGraph(&q, &def, &g) == maud_errorCapacity, "past maxProbes");
    q.maxProbes = 24;
    CHECK(maudBuildProbeGraph(&q, &def, &g) == maud_success, "at maxProbes");
    maudReleaseProbeGraph(&s_allocator, &g);
    q.maxProbes = 4096;
    q.closestHit = nullptr;
    CHECK(maudBuildProbeGraph(&q, &def, &g) == maud_errorState, "no closest-hit query");
    def.boxMax.y = def.boxMin.y;
    CHECK(!maudProbeSetDefValid(&def), "a flat box");
    maudDestroyAcousticScene(scene);
}

// A tower of ten storeys over one column: probes on the top eight, the
// most a column holds, in a box flat along z (one row of columns).
static void TestTower(void)
{
    for (int f = 0; f < 10; ++f)
    {
        float y = 3.0f * (float)f;
        Box((maudVector3){0.0f, y - 0.2f, 0.0f}, (maudVector3){1.0f, y, 1.0f});
    }
    maudAcousticScene* scene = Scene();
    maudProbeSetDef def = {.boxMin = {0.0f, -1.0f, 0.5f},
                           .boxMax = {1.0f, 30.0f, 0.5f},
                           .spacing = 1.0f,
                           .height = 1.5f,
                           .range = 2.0f};
    CHECK(maudProbeSetDefValid(&def), "a box flat along z");
    maudProbeQueries q = Queries(scene);
    maudProbeGraph g;
    CHECK(maudBuildProbeGraph(&q, &def, &g) == maud_success, "generated");
    float lowest = INFINITY;
    for (uint32_t i = 0; i < g.count; ++i)
    {
        lowest = fminf(lowest, g.points[i].y);
    }
    printf("tower: %u probes, the lowest at %.1f m\n", g.count, (double)lowest);
    CHECK(g.count == 8 && fabsf(lowest - 7.5f) < 1e-4f, "the top eight storeys");
    maudReleaseProbeGraph(&s_allocator, &g);
    maudDestroyAcousticScene(scene);
}

int main(void)
{
    TestCorridor();
    TestGeneration();
    TestTower();
    return s_failures == 0 ? 0 : 1;
}
