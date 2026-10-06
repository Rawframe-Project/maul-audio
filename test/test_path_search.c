// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Path search (whitebox): in an L-shaped corridor the path found costs
// what Bellman-Ford finds over the same links and ends, its vertices
// run from the listener to the source through linked probes; a blocked
// link is left out; ends that see no probe find no path, and neither
// does one through more probes than a path holds.

#include "allocator.h"
#include "path_search.h"
#include "test_harness.h"

#include "maul-audio/scene.h"

#include <math.h>

static const maudAllocator s_allocator = {nullptr, nullptr, nullptr};

static maudAcousticScene* Corridor(void)
{
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[12] = {0};
    static maudVector3 v[8];
    for (uint32_t i = 0; i < 8; ++i)
    {
        v[i] = (maudVector3){(i & 1) ? 17.0f : 0.0f, (i & 2) ? 3.0f : 0.0f, (i & 4) ? 20.0f : 3.0f};
    }
    maudMesh mesh = {v, 8, faces, materials, 12};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a corridor");
    return scene;
}

static float Distance(maudVector3 a, maudVector3 b)
{
    return sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
}

// Bellman-Ford over the links from the source's end; the best total to
// the listener's end.
static float Best(const maudProbeGraph* g, const maudPathEnd* from, const maudPathEnd* to)
{
    static float cost[512];
    for (uint32_t i = 0; i < g->count; ++i)
    {
        cost[i] = INFINITY;
    }
    for (uint32_t k = 0; k < from->count; ++k)
    {
        cost[from->probes[k]] = fminf(cost[from->probes[k]], from->lengths[k]);
    }
    for (uint32_t round = 0; round < g->count; ++round)
    {
        for (uint32_t u = 0; u < g->count; ++u)
        {
            for (uint32_t e = g->offsets[u]; e < g->offsets[u + 1]; ++e)
            {
                cost[g->neighbours[e]] = fminf(cost[g->neighbours[e]], cost[u] + g->lengths[e]);
            }
        }
    }
    float best = INFINITY;
    for (uint32_t k = 0; k < to->count; ++k)
    {
        best = fminf(best, cost[to->probes[k]] + to->lengths[k]);
    }
    return best;
}

static bool Linked(const maudProbeGraph* g, uint32_t a, uint32_t b)
{
    for (uint32_t e = g->offsets[a]; e < g->offsets[a + 1]; ++e)
    {
        if (g->neighbours[e] == b)
        {
            return true;
        }
    }
    return false;
}

static void TestCorridor(void)
{
    maudAcousticScene* scene = Corridor();
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
    maudProbeQueries q = {.anyHit = maudSceneAnyHit,
                          .rayContext = scene,
                          .allocator = &s_allocator,
                          .maxProbes = 512,
                          .maxPairs = 1u << 16};
    maudProbeSetDef def = {.points = points, .pointCount = count, .range = 3.0f};
    maudProbeGraph g;
    CHECK(maudBuildProbeGraph(&q, &def, &g) == maud_success, "a set");
    maudPathSearch search;
    CHECK(maudCreatePathSearch(&s_allocator, 512, &search), "scratch");
    const maudVector3 listener = {18.5f, 1.5f, 12.0f};
    const maudVector3 source = {6.0f, 1.5f, 1.5f};
    maudPathEnd to;
    maudPathEnd from;
    maudAttachPath(&g, g.range, listener, maudSceneAnyHit, scene, &to);
    maudAttachPath(&g, g.range, source, maudSceneAnyHit, scene, &from);
    CHECK(to.count == MAUD_PATH_ATTACH && from.count == MAUD_PATH_ATTACH, "both ends attached");
    maudVector3 v[MAUD_PATH_VERTICES];
    uint32_t probes[MAUD_PATH_VERTICES];
    uint32_t n = maudSearchPath(&search, &g, &from, &to, source, listener, v, probes);
    float total = 0.0f;
    bool linked = n >= 3;
    for (uint32_t k = 0; k + 1 < n; ++k)
    {
        total += Distance(v[k], v[k + 1]);
    }
    for (uint32_t k = 0; k + 4 <= n; ++k)
    {
        linked = linked && Linked(&g, probes[k], probes[k + 1]);
    }
    float best = Best(&g, &from, &to);
    printf("corridor: %u vertices, %.4f m, the best %.4f\n", n, (double)total, (double)best);
    CHECK(fabsf(total - best) < 1e-3f, "the shortest path");
    CHECK(linked && v[0].x == listener.x && v[n - 1].x == source.x, "from listener to source");
    // Its first link blocked: the next search leaves it out.
    search.blocked[0] = probes[0] < probes[1] ? probes[0] : probes[1];
    search.blocked[1] = probes[0] < probes[1] ? probes[1] : probes[0];
    search.blockedCount = 1;
    uint32_t first[2] = {probes[0], probes[1]};
    uint32_t m = maudSearchPath(&search, &g, &from, &to, source, listener, v, probes);
    bool avoided = m >= 3;
    for (uint32_t k = 0; k + 4 <= m; ++k)
    {
        avoided = avoided && !((probes[k] == first[0] && probes[k + 1] == first[1]) ||
                               (probes[k] == first[1] && probes[k + 1] == first[0]));
    }
    CHECK(avoided, "a blocked link left out");
    search.blockedCount = 0;
    // Beside the corner some probes in range are behind the block: only
    // the ones in sight attach.
    const maudVector3 beside = {17.5f, 1.5f, 3.5f};
    maudPathEnd near;
    maudAttachPath(&g, 3.0f, beside, maudSceneAnyHit, scene, &near);
    // The spec by brute force: the 32 nearest in range by distance
    // (then index), the ones in sight of them, the first 8.
    uint32_t order[400];
    uint32_t inRange = 0;
    for (uint32_t i = 0; i < g.count; ++i)
    {
        if (Distance(beside, g.points[i]) <= 3.0f)
        {
            uint32_t k = inRange++;
            for (;
                 k > 0 && Distance(beside, g.points[order[k - 1]]) > Distance(beside, g.points[i]);
                 --k)
            {
                order[k] = order[k - 1];
            }
            order[k] = i;
        }
    }
    uint32_t want[MAUD_PATH_ATTACH];
    uint32_t wanted = 0;
    uint32_t hidden = 0;
    for (uint32_t k = 0; k < inRange && k < MAUD_PATH_CANDIDATES && wanted < MAUD_PATH_ATTACH; ++k)
    {
        maudVector3 p = g.points[order[k]];
        float d = Distance(beside, p);
        maudRay ray = {
            beside, {(p.x - beside.x) / d, (p.y - beside.y) / d, (p.z - beside.z) / d}, 0.0f, d};
        uint8_t occluded = 1;
        maudSceneAnyHit(&ray, 1, &occluded, scene);
        hidden += occluded;
        if (occluded == 0)
        {
            want[wanted++] = order[k];
        }
    }
    bool same = near.count == wanted;
    for (uint32_t k = 0; k < wanted && same; ++k)
    {
        same = near.probes[k] == want[k];
    }
    printf("beside the corner: %u attached, %u hidden among the nearest\n", near.count, hidden);
    CHECK(same && hidden > 0, "the nearest probes in sight");
    maudPathEnd none = {0};
    CHECK(maudSearchPath(&search, &g, &none, &to, source, listener, v, probes) == 0,
          "an end seeing nothing");
    maudDestroyPathSearch(&s_allocator, &search);
    maudReleaseProbeGraph(&s_allocator, &g);
    maudDestroyAcousticScene(scene);
}

static void TestTooLong(void)
{
    static maudVector3 points[100];
    for (uint32_t i = 0; i < 100; ++i)
    {
        points[i] = (maudVector3){(float)i, 0.0f, 0.0f};
    }
    maudProbeQueries q = {.allocator = &s_allocator, .maxProbes = 100, .maxPairs = 1000};
    maudProbeSetDef def = {.points = points, .pointCount = 100, .range = 1.2f};
    maudProbeGraph g;
    CHECK(maudBuildProbeGraph(&q, &def, &g) == maud_success, "a line");
    maudPathSearch search;
    CHECK(maudCreatePathSearch(&s_allocator, 100, &search), "scratch");
    maudPathEnd from;
    maudPathEnd to;
    maudAttachPath(&g, g.range, (maudVector3){-0.5f, 0.0f, 0.0f}, nullptr, nullptr, &from);
    maudAttachPath(&g, g.range, (maudVector3){99.5f, 0.0f, 0.0f}, nullptr, nullptr, &to);
    maudVector3 v[MAUD_PATH_VERTICES];
    uint32_t probes[MAUD_PATH_VERTICES];
    CHECK(maudSearchPath(&search, &g, &from, &to, (maudVector3){-0.5f, 0.0f, 0.0f},
                         (maudVector3){99.5f, 0.0f, 0.0f}, v, probes) == 0,
          "through more probes than a path holds");
    maudAttachPath(&g, g.range, (maudVector3){60.5f, 0.0f, 0.0f}, nullptr, nullptr, &to);
    uint32_t n = maudSearchPath(&search, &g, &from, &to, (maudVector3){-0.5f, 0.0f, 0.0f},
                                (maudVector3){60.5f, 0.0f, 0.0f}, v, probes);
    CHECK(n == 63, "62 probes fit");
    maudDestroyPathSearch(&s_allocator, &search);
    maudReleaseProbeGraph(&s_allocator, &g);
}

int main(void)
{
    TestCorridor();
    TestTooLong();
    return s_failures == 0 ? 0 : 1;
}
