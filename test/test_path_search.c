// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Path search (whitebox): in an L-shaped corridor, and between ends
// scattered over random open fields, the path found costs what Bellman-
// Ford finds over the same links and ends; its vertices run from the
// listener to the source through linked probes; a blocked link is left
// out; ends that see no probe find no path, and neither does one
// through more probes than a path holds; an end attaches to the set's
// probes only; scratch an allocator refuses or a size cannot count is
// refused.

#include "allocator.h"
#include "path_search.h"
#include "test_harness.h"

#include "maul-audio/scene.h"

#include <math.h>

static const maudAllocator s_allocator = {nullptr, nullptr, nullptr};

static maudScene* Corridor(void)
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
    maudSceneDef def = maudDefaultSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "a corridor");
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
    maudScene* scene = Corridor();
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
    maudProbeQueries q = {.anyHit = maudFindAnyHit,
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
    maudAttachPath(&g, g.range, listener, maudFindAnyHit, scene, &to);
    maudAttachPath(&g, g.range, source, maudFindAnyHit, scene, &from);
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
    maudAttachPath(&g, 3.0f, beside, maudFindAnyHit, scene, &near);
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
        maudFindAnyHit(&ray, 1, &occluded, scene);
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
    maudDestroyScene(scene);
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
    bool within = from.count > 0;
    for (uint32_t k = 0; k < from.count; ++k)
    {
        within = within && from.probes[k] < g.count;
    }
    CHECK(within, "an end attached to probes of the set only");
    // Every probe of the line in range: the nearest are kept, nearest
    // first.
    maudPathEnd wide;
    maudAttachPath(&g, 1000.0f, (maudVector3){40.2f, 0.0f, 0.0f}, nullptr, nullptr, &wide);
    CHECK(wide.count == MAUD_PATH_ATTACH && wide.probes[0] == 40 && wide.probes[1] == 41 &&
              wide.probes[2] == 39,
          "a hundred probes in range: the nearest, in order");
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

static uint32_t s_seed = 2026;

static float Random(float span)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return span * (float)(s_seed >> 8) / 16777216.0f;
}

// Random open fields, every pair in range linked: for ends scattered over
// each, the path found costs what Bellman-Ford finds, so the search
// takes the probes in cost order however its heap fills.
static void TestRandom(void)
{
    maudPathSearch search;
    CHECK(maudCreatePathSearch(&s_allocator, 512, &search), "scratch");
    uint32_t compared = 0;
    uint32_t wrong = 0;
    for (int field = 0; field < 8; ++field)
    {
        static maudVector3 points[120];
        for (int i = 0; i < 120; ++i)
        {
            points[i] = (maudVector3){Random(12.0f), 1.5f, Random(12.0f)};
        }
        maudProbeQueries q = {.allocator = &s_allocator, .maxProbes = 512, .maxPairs = 1u << 16};
        maudProbeSetDef def = {.points = points, .pointCount = 120, .range = 2.0f};
        maudProbeGraph g;
        CHECK(maudBuildProbeGraph(&q, &def, &g) == maud_success, "a field");
        for (int trial = 0; trial < 10; ++trial)
        {
            maudVector3 a = {Random(12.0f), 1.5f, Random(12.0f)};
            maudVector3 b = {Random(12.0f), 1.5f, Random(12.0f)};
            maudPathEnd from;
            maudPathEnd to;
            maudAttachPath(&g, g.range, a, nullptr, nullptr, &from);
            maudAttachPath(&g, g.range, b, nullptr, nullptr, &to);
            maudVector3 v[MAUD_PATH_VERTICES];
            uint32_t probes[MAUD_PATH_VERTICES];
            uint32_t n = maudSearchPath(&search, &g, &from, &to, a, b, v, probes);
            float best = Best(&g, &from, &to);
            if (n == 0 || best == INFINITY)
            {
                continue;
            }
            float total = 0.0f;
            for (uint32_t k = 0; k + 1 < n; ++k)
            {
                total += Distance(v[k], v[k + 1]);
            }
            compared += 1;
            wrong += fabsf(total - best) > 1e-3f * best ? 1 : 0;
        }
        maudReleaseProbeGraph(&s_allocator, &g);
    }
    printf("random fields: %u paths, %u not the cheapest\n", compared, wrong);
    CHECK(compared >= 40 && wrong == 0, "every path the cheapest");
    maudDestroyPathSearch(&s_allocator, &search);
}

static float LinkLength(const maudProbeGraph* g, uint32_t a, uint32_t b)
{
    for (uint32_t e = g->offsets[a]; e < g->offsets[a + 1]; ++e)
    {
        if (g->neighbours[e] == b)
        {
            return g->lengths[e];
        }
    }
    return INFINITY;
}

static float EndLength(const maudPathEnd* end, uint32_t probe)
{
    for (uint32_t k = 0; k < end->count; ++k)
    {
        if (end->probes[k] == probe)
        {
            return end->lengths[k];
        }
    }
    return INFINITY;
}

// Small graphs whose links have any lengths, not their probes'
// distances: the path found, by the lengths it uses, costs what
// Bellman-Ford finds, and there is none exactly when Bellman-Ford finds
// none. Shortest paths over such lengths take the probes from the heap
// in cost order.
static void TestAnyLengths(void)
{
    maudPathSearch search;
    CHECK(maudCreatePathSearch(&s_allocator, 16, &search), "scratch");
    uint32_t compared = 0;
    uint32_t wrong = 0;
    for (int trial = 0; trial < 200; ++trial)
    {
        float length[12][12] = {{0}};
        uint32_t links = 0;
        for (uint32_t i = 0; i < 12; ++i)
        {
            for (uint32_t j = i + 1; j < 12; ++j)
            {
                if (Random(1.0f) < 0.35f)
                {
                    length[i][j] = 1.0f + floorf(Random(9.0f));
                    length[j][i] = length[i][j];
                    links += 1;
                }
            }
        }
        maudProbeGraph g;
        CHECK(maudLayProbeGraph(&s_allocator, &g, 12, links), "a graph");
        uint32_t e = 0;
        for (uint32_t i = 0; i < 12; ++i)
        {
            g.points[i] = (maudVector3){(float)i, 1.5f, 0.0f};
            g.offsets[i] = e;
            for (uint32_t j = 0; j < 12; ++j)
            {
                if (length[i][j] > 0.0f)
                {
                    g.neighbours[e] = j;
                    g.lengths[e++] = length[i][j];
                }
            }
        }
        g.offsets[12] = e;
        g.range = 100.0f;
        maudPathEnd from = {3, {0, 1, 2}, {0}};
        maudPathEnd to = {3, {9, 10, 11}, {0}};
        for (int k = 0; k < 3; ++k)
        {
            from.lengths[k] = 0.5f + floorf(Random(5.0f));
            to.lengths[k] = 0.5f + floorf(Random(5.0f));
        }
        maudVector3 v[MAUD_PATH_VERTICES];
        uint32_t probes[MAUD_PATH_VERTICES];
        uint32_t n = maudSearchPath(&search, &g, &from, &to, (maudVector3){-1.0f, 1.5f, 0.0f},
                                    (maudVector3){13.0f, 1.5f, 0.0f}, v, probes);
        float best = Best(&g, &from, &to);
        if (best == INFINITY || n < 3)
        {
            wrong += (best == INFINITY) != (n == 0) ? 1 : 0;
            maudReleaseProbeGraph(&s_allocator, &g);
            continue;
        }
        float cost = EndLength(&to, probes[0]) + EndLength(&from, probes[n - 3]);
        for (uint32_t k = 0; k + 3 < n; ++k)
        {
            cost += LinkLength(&g, probes[k], probes[k + 1]);
        }
        compared += 1;
        wrong += fabsf(cost - best) > 1e-4f ? 1 : 0;
        maudReleaseProbeGraph(&s_allocator, &g);
    }
    printf("any lengths: %u paths, %u wrong\n", compared, wrong);
    CHECK(compared >= 100 && wrong == 0, "every path the cheapest by its lengths");
    maudDestroyPathSearch(&s_allocator, &search);
}

// A graph where taking a probe out of cost order costs the path: the
// source's end at probes 0 (0 m) and 1 (2 m), the listener's at 8 (3 m)
// and 7 (1 m); the cheapest path costs 5, through 1, 8 and 7, and a
// heap that lets a probe out of cost order finds one of 6.
static void TestHeapOrder(void)
{
    static const uint32_t links[][3] = {{0, 1, 4}, {0, 2, 3}, {0, 4, 7}, {0, 6, 7}, {0, 8, 8},
                                        {1, 4, 8}, {1, 7, 5}, {1, 8, 1}, {2, 6, 1}, {2, 7, 2},
                                        {2, 8, 8}, {3, 5, 8}, {3, 6, 9}, {4, 5, 6}, {4, 7, 3},
                                        {5, 6, 5}, {7, 8, 1}};
    const uint32_t count = sizeof(links) / sizeof(links[0]);
    maudProbeGraph g;
    CHECK(maudLayProbeGraph(&s_allocator, &g, 9, count), "a graph");
    uint32_t e = 0;
    for (uint32_t i = 0; i < 9; ++i)
    {
        g.points[i] = (maudVector3){(float)i, 1.5f, 0.0f};
        g.offsets[i] = e;
        for (uint32_t j = 0; j < 9; ++j)
        {
            for (uint32_t k = 0; k < count; ++k)
            {
                if ((links[k][0] == i && links[k][1] == j) ||
                    (links[k][0] == j && links[k][1] == i))
                {
                    g.neighbours[e] = j;
                    g.lengths[e++] = (float)links[k][2];
                }
            }
        }
    }
    g.offsets[9] = e;
    g.range = 100.0f;
    const maudPathEnd from = {2, {0, 1}, {0.0f, 2.0f}};
    const maudPathEnd to = {2, {8, 7}, {3.0f, 1.0f}};
    maudPathSearch search;
    CHECK(maudCreatePathSearch(&s_allocator, 9, &search), "scratch");
    maudVector3 v[MAUD_PATH_VERTICES];
    uint32_t probes[MAUD_PATH_VERTICES];
    uint32_t n = maudSearchPath(&search, &g, &from, &to, (maudVector3){-1.0f, 1.5f, 0.0f},
                                (maudVector3){10.0f, 1.5f, 0.0f}, v, probes);
    float cost = n >= 3 ? EndLength(&to, probes[0]) + EndLength(&from, probes[n - 3]) : INFINITY;
    for (uint32_t k = 0; k + 3 < n; ++k)
    {
        cost += LinkLength(&g, probes[k], probes[k + 1]);
    }
    printf("heap order: a path of %.0f, Bellman-Ford %.0f\n", (double)cost,
           (double)Best(&g, &from, &to));
    CHECK(cost == 5.0f && Best(&g, &from, &to) == 5.0f, "the cheapest path, 5");
    maudDestroyPathSearch(&s_allocator, &search);
    maudReleaseProbeGraph(&s_allocator, &g);
}

static uint32_t s_badDirections;

// A host's any-hit query that counts rays without a unit direction and
// finds nothing in the way.
static void Clear(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    (void)context;
    for (uint32_t i = 0; i < count; ++i)
    {
        maudVector3 d = rays[i].direction;
        float length = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        s_badDirections += fabsf(length - 1.0f) < 1e-5f ? 0 : 1;
        occluded[i] = 0;
    }
}

// A point on a probe attaches to it at 0 m, its query asked with a unit
// direction as every other.
static void TestOnProbe(void)
{
    static maudVector3 points[3] = {{0.0f, 1.5f, 0.0f}, {1.0f, 1.5f, 0.0f}, {2.0f, 1.5f, 0.0f}};
    maudProbeQueries q = {.allocator = &s_allocator, .maxProbes = 8, .maxPairs = 16};
    maudProbeSetDef def = {.points = points, .pointCount = 3, .range = 2.5f};
    maudProbeGraph g;
    CHECK(maudBuildProbeGraph(&q, &def, &g) == maud_success, "a set");
    maudPathEnd end;
    maudAttachPath(&g, g.range, points[1], Clear, nullptr, &end);
    CHECK(end.count == 3 && end.probes[0] == 1 && end.lengths[0] == 0.0f,
          "the probe it is on first, at 0 m");
    CHECK(s_badDirections == 0, "every query with a unit direction");
    maudReleaseProbeGraph(&s_allocator, &g);
}

static void* Refuse(size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    return nullptr;
}

static void Ignore(void* memory, size_t size, size_t alignment, void* context)
{
    (void)memory;
    (void)size;
    (void)alignment;
    (void)context;
}

// Scratch the allocator refuses is refused, and so is scratch for the
// most probes there can be, past what a size counts on a 32-bit target.
static void TestRefused(void)
{
    const maudAllocator refusing = {Refuse, Ignore, nullptr};
    maudPathSearch search;
    CHECK(!maudCreatePathSearch(&refusing, 16, &search) && search.memory == nullptr,
          "a refusing allocator");
    CHECK(!maudCreatePathSearch(&refusing, UINT32_MAX, &search) && search.memory == nullptr,
          "the most probes");
}

int main(void)
{
    TestCorridor();
    TestTooLong();
    TestRandom();
    TestAnyLengths();
    TestHeapOrder();
    TestOnProbe();
    TestRefused();
    return s_failures == 0 ? 0 : 1;
}
