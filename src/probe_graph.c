// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The probe sets (probe_graph.h). Generation walks every column twice,
// once to count its floors and once to place them where the counts say,
// so its memory is a byte and a word a column. Pairs within range come
// from a grid of cells a little wider than the range, each probe tested
// against its own cell and the neighbouring ones, and are sorted by
// their indices; each is one any-hit ray; the links fill compressed rows
// in pair order, which leaves every row ascending.

#include "probe_graph.h"

#include "allocator.h"
#include "fpenv.h"
#include "host_tasks.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// A floor's normal is within 45 degrees of vertical.
#define FLOOR_NORMAL 0.70710678f
// Past a surface, the next ray starts this much further down.
#define STEP_PAST   1e-4f
#define MIN_SPACING 0.25f
#define MAX_SPACING 100.0f
#define MIN_HEIGHT  0.1f
#define MAX_HEIGHT  10.0f
#define MIN_RANGE   0.1f
#define MAX_RANGE   1000.0f
// A cell is this much wider than the range: two probes whose distance
// rounds to within range are less than a cell apart on every axis.
#define CELL_MARGIN (1.0 + 1.0 / 1024.0)
// The cells' numbers stop here, far past any scene's.
#define CELL_LIMIT 4611686018427387904.0

typedef struct Pair
{
    uint32_t i;
    uint32_t j;
} Pair;

// A probe's cell, numbered along x, y and z, and the probe.
typedef struct Key
{
    int64_t cell[3];
    uint32_t index;
} Key;

// A generation's columns: the grid, each column's floors and, on the
// second walk, where its probes go.
typedef struct Columns
{
    const maudProbeQueries* queries;
    const maudProbeSetDef* def;
    uint32_t across;
    uint32_t count;
    float x0;
    float z0;
    uint8_t* floors;
    const uint32_t* firsts;
    maudVector3* points;
} Columns;

// A column's walk: where its next ray starts, the surface above as a
// height, the floors so far, whether it goes on.
typedef struct Walk
{
    float past;
    float ceiling;
    uint32_t found;
    bool going;
} Walk;

// The pairs' rays.
typedef struct Links
{
    const maudProbeQueries* queries;
    const maudVector3* points;
    const Pair* pairs;
    uint32_t count;
    uint8_t* linked;
} Links;

static bool Finite(maudVector3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}

static bool Within(float v, float low, float high)
{
    return v >= low && v <= high;
}

bool maudProbeSetDefValid(const maudProbeSetDef* def)
{
    if (!Within(def->range, MIN_RANGE, MAX_RANGE))
    {
        return false;
    }
    if (def->points != nullptr)
    {
        for (uint32_t i = 0; i < def->pointCount; ++i)
        {
            if (!Finite(def->points[i]))
            {
                return false;
            }
        }
        return true;
    }
    return def->pointCount == 0 && Within(def->spacing, MIN_SPACING, MAX_SPACING) &&
           Within(def->height, MIN_HEIGHT, MAX_HEIGHT) && Finite(def->boxMin) &&
           Finite(def->boxMax) && def->boxMin.x <= def->boxMax.x && def->boxMin.y < def->boxMax.y &&
           def->boxMin.z <= def->boxMax.z;
}

static void Run(const maudProbeQueries* q, maudTaskFunction* task, uint32_t items, void* context)
{
    maudHostTasks hooks = {q->enqueueTask, q->finishTask, q->userTaskContext};
    maudRunHostTasks(&hooks, task, items, context);
}

static maudVector3 ColumnTop(const Columns* c, uint32_t column)
{
    uint32_t i = column % c->across;
    uint32_t k = column / c->across;
    return (maudVector3){c->x0 + (float)i * c->def->spacing, c->def->boxMax.y,
                         c->z0 + (float)k * c->def->spacing};
}

// Takes a column's next surface: a floor if horizontal with the height
// free above it, then the surface above for what lies below.
static void Visit(const Columns* c, uint32_t column, const maudRay* ray, const maudRayHit* hit,
                  Walk* walk)
{
    if (!(hit->distance >= ray->minDistance && hit->distance <= ray->maxDistance))
    {
        walk->going = false;
        return;
    }
    float y = ray->origin.y - hit->distance;
    if (fabsf(hit->normal.y) >= FLOOR_NORMAL && walk->ceiling - y > c->def->height)
    {
        if (c->firsts != nullptr)
        {
            c->points[c->firsts[column] + walk->found] =
                (maudVector3){ray->origin.x, y + c->def->height, ray->origin.z};
        }
        walk->found += 1;
        walk->going = walk->found < MAUD_COLUMN_FLOORS;
    }
    walk->ceiling = y;
    walk->past = hit->distance + STEP_PAST;
}

// Walks columns [first, first + n) down in lockstep: each query asks
// every column still going for its next surface.
static void WalkColumns(const Columns* c, uint32_t first, uint32_t n)
{
    maudRay rays[MAUD_PROBE_BATCH];
    maudRayHit hits[MAUD_PROBE_BATCH];
    uint32_t which[MAUD_PROBE_BATCH];
    Walk walks[MAUD_PROBE_BATCH];
    float depth = c->def->boxMax.y - c->def->boxMin.y;
    for (uint32_t i = 0; i < n; ++i)
    {
        walks[i] = (Walk){0.0f, c->def->boxMax.y, 0, true};
    }
    for (uint32_t round = 0; round < MAUD_COLUMN_HITS; ++round)
    {
        uint32_t asked = 0;
        for (uint32_t i = 0; i < n; ++i)
        {
            if (walks[i].going)
            {
                which[asked] = i;
                rays[asked] =
                    (maudRay){ColumnTop(c, first + i), {0.0f, -1.0f, 0.0f}, walks[i].past, depth};
                asked += 1;
            }
        }
        if (asked == 0)
        {
            break;
        }
        c->queries->closestHit(rays, asked, hits, c->queries->rayContext);
        for (uint32_t a = 0; a < asked; ++a)
        {
            Visit(c, first + which[a], &rays[a], &hits[a], &walks[which[a]]);
        }
    }
    for (uint32_t i = 0; i < n; ++i)
    {
        c->floors[first + i] = (uint8_t)walks[i].found;
    }
}

static MAUD_NOINLINE void ColumnBatches(uint32_t start, uint32_t end, void* context)
{
    const Columns* c = context;
    for (uint32_t b = start; b < end; ++b)
    {
        uint32_t first = b * MAUD_PROBE_BATCH;
        uint32_t n = c->count - first < MAUD_PROBE_BATCH ? c->count - first : MAUD_PROBE_BATCH;
        WalkColumns(c, first, n);
    }
}

// The columns along one side: as many whole spacings as fit, at least
// one, centred.
static uint32_t Across(float low, float high, float spacing, float* first)
{
    double count = fmax(1.0, floor(((double)high - (double)low) / (double)spacing));
    *first = (float)(((double)low + (double)high) * 0.5 - (count - 1.0) * 0.5 * (double)spacing);
    return count > (double)MAUD_MAX_PROBE_COLUMNS ? MAUD_MAX_PROBE_COLUMNS + 1 : (uint32_t)count;
}

// Generates the probes into *points (allocated here, *count of them).
static maudResult Generate(const maudProbeQueries* q, const maudProbeSetDef* def,
                           maudVector3** points, uint32_t* count)
{
    Columns c = {.queries = q, .def = def};
    c.across = Across(def->boxMin.x, def->boxMax.x, def->spacing, &c.x0);
    uint32_t along = Across(def->boxMin.z, def->boxMax.z, def->spacing, &c.z0);
    if ((uint64_t)c.across * along > MAUD_MAX_PROBE_COLUMNS)
    {
        return maud_errorLimit;
    }
    c.count = c.across * along;
    size_t bytes = (size_t)c.count * (sizeof(uint8_t) + sizeof(uint32_t));
    uint8_t* memory = maudAllocate(q->allocator, bytes, alignof(uint32_t));
    if (memory == nullptr)
    {
        return maud_errorCapacity;
    }
    uint32_t* firsts = (uint32_t*)memory;
    c.floors = memory + (size_t)c.count * sizeof(uint32_t);
    uint32_t batches = (c.count + MAUD_PROBE_BATCH - 1) / MAUD_PROBE_BATCH;
    Run(q, ColumnBatches, batches, &c);
    uint64_t total = 0;
    for (uint32_t i = 0; i < c.count; ++i)
    {
        firsts[i] = (uint32_t)(total < UINT32_MAX ? total : UINT32_MAX);
        total += c.floors[i];
    }
    maudResult result = maud_errorCapacity;
    *points =
        total <= q->maxProbes && total > 0
            ? maudAllocate(q->allocator, (size_t)total * sizeof(maudVector3), alignof(maudVector3))
            : nullptr;
    if (total == 0 || *points != nullptr)
    {
        c.firsts = firsts;
        c.points = *points;
        if (total > 0)
        {
            Run(q, ColumnBatches, batches, &c);
        }
        *count = (uint32_t)total;
        result = maud_success;
    }
    maudRelease(q->allocator, memory, bytes, alignof(uint32_t));
    return result;
}

// The cell along one axis of a coordinate, cells width wide.
static int64_t CellOf(float v, double width)
{
    double cell = floor((double)v / width);
    cell = cell < -CELL_LIMIT ? -CELL_LIMIT : cell > CELL_LIMIT ? CELL_LIMIT : cell;
    return (int64_t)cell;
}

static int CompareCells(const int64_t* l, const int64_t* r)
{
    for (int k = 0; k < 3; ++k)
    {
        if (l[k] != r[k])
        {
            return l[k] < r[k] ? -1 : 1;
        }
    }
    return 0;
}

static int ByCell(const void* a, const void* b)
{
    const Key* l = a;
    const Key* r = b;
    int cells = CompareCells(l->cell, r->cell);
    if (cells != 0)
    {
        return cells;
    }
    return l->index < r->index ? -1 : l->index > r->index ? 1 : 0;
}

static int ByIndices(const void* a, const void* b)
{
    const Pair* l = a;
    const Pair* r = b;
    if (l->i != r->i)
    {
        return l->i < r->i ? -1 : 1;
    }
    return l->j < r->j ? -1 : l->j > r->j ? 1 : 0;
}

static float Distance(maudVector3 a, maudVector3 b)
{
    float x = b.x - a.x;
    float y = b.y - a.y;
    float z = b.z - a.z;
    return sqrtf(x * x + y * y + z * z);
}

// The 13 neighbouring cells that come after a cell in the keys' order:
// with the cell itself, a pair in two cells is tested once, from the
// earlier.
static const int8_t s_after[13][3] = {
    {0, 0, 1},  {0, 1, -1}, {0, 1, 0}, {0, 1, 1},  {1, -1, -1}, {1, -1, 0}, {1, -1, 1},
    {1, 0, -1}, {1, 0, 0},  {1, 0, 1}, {1, 1, -1}, {1, 1, 0},   {1, 1, 1},
};

// The first key at or after cell.
static uint32_t FirstIn(const Key* keys, uint32_t count, const int64_t* cell)
{
    uint32_t low = 0;
    uint32_t high = count;
    while (low < high)
    {
        uint32_t mid = low + (high - low) / 2;
        if (CompareCells(keys[mid].cell, cell) < 0)
        {
            low = mid + 1;
        }
        else
        {
            high = mid;
        }
    }
    return low;
}

// The keys of one cell, from *from up to *to.
static void CellRun(const Key* keys, uint32_t count, const int64_t* cell, uint32_t* from,
                    uint32_t* to)
{
    uint32_t b = FirstIn(keys, count, cell);
    *from = b;
    while (b < count && CompareCells(keys[b].cell, cell) == 0)
    {
        ++b;
    }
    *to = b;
}

// Tests key a's probe against the keys from from up to to: total, with
// each pair within range counted, and written when pairs is given.
static uint64_t TestAgainst(const Key* keys, const maudVector3* points, float range, uint32_t a,
                            const uint32_t run[2], Pair* pairs, uint64_t total)
{
    for (uint32_t b = run[0]; b < run[1]; ++b)
    {
        uint32_t i = keys[a].index;
        uint32_t j = keys[b].index;
        if (Distance(points[i], points[j]) <= range)
        {
            if (pairs != nullptr)
            {
                pairs[total] = i < j ? (Pair){i, j} : (Pair){j, i};
            }
            total += 1;
        }
    }
    return total;
}

// The pairs within range: each cell's probes tested against the later
// probes of the cell and every probe of the cells after it, whose runs
// of keys are found once a cell. Counted without pairs, written with
// them (unsorted), stopping past limit.
static uint64_t Sweep(const Key* keys, const maudVector3* points, uint32_t count, float range,
                      uint64_t limit, Pair* pairs)
{
    uint64_t total = 0;
    uint32_t end = 0;
    for (uint32_t first = 0; first < count && total <= limit; first = end)
    {
        const int64_t* own = keys[first].cell;
        uint32_t runs[14][2];
        CellRun(keys, count, own, &runs[0][0], &runs[0][1]);
        end = runs[0][1];
        for (int n = 0; n < 13; ++n)
        {
            int64_t cell[3] = {own[0] + s_after[n][0], own[1] + s_after[n][1],
                               own[2] + s_after[n][2]};
            CellRun(keys, count, cell, &runs[n + 1][0], &runs[n + 1][1]);
        }
        for (uint32_t a = first; a < end && total <= limit; ++a)
        {
            runs[0][0] = a + 1;
            for (int n = 0; n < 14; ++n)
            {
                total = TestAgainst(keys, points, range, a, runs[n], pairs, total);
            }
        }
    }
    return total;
}

static MAUD_NOINLINE void LinkBatches(uint32_t start, uint32_t end, void* context)
{
    const Links* l = context;
    maudRay rays[MAUD_PROBE_BATCH];
    uint8_t occluded[MAUD_PROBE_BATCH];
    for (uint32_t b = start; b < end; ++b)
    {
        uint32_t first = b * MAUD_PROBE_BATCH;
        uint32_t n = l->count - first < MAUD_PROBE_BATCH ? l->count - first : MAUD_PROBE_BATCH;
        for (uint32_t k = 0; k < n; ++k)
        {
            maudVector3 from = l->points[l->pairs[first + k].i];
            maudVector3 to = l->points[l->pairs[first + k].j];
            float length = Distance(from, to);
            maudVector3 d = length > 0.0f
                                ? (maudVector3){(to.x - from.x) / length, (to.y - from.y) / length,
                                                (to.z - from.z) / length}
                                : (maudVector3){0.0f, 1.0f, 0.0f};
            rays[k] = (maudRay){from, d, 0.0f, length};
        }
        if (l->queries->anyHit != nullptr)
        {
            l->queries->anyHit(rays, n, occluded, l->queries->rayContext);
        }
        else
        {
            memset(occluded, 0, n);
        }
        for (uint32_t k = 0; k < n; ++k)
        {
            l->linked[first + k] = occluded[k] == 0 ? 1 : 0;
        }
    }
}

// Lays the graph's block out; false if it does not fit.
bool maudLayProbeGraph(const maudAllocator* allocator, maudProbeGraph* g, uint32_t count,
                       uint32_t links)
{
    maudLayout layout = {0};
    size_t points = maudLayoutAdd(&layout, count, sizeof(maudVector3), alignof(maudVector3));
    size_t offsets = maudLayoutAdd(&layout, (size_t)count + 1, sizeof(uint32_t), alignof(uint32_t));
    size_t neighbours =
        maudLayoutAdd(&layout, 2 * (size_t)links, sizeof(uint32_t), alignof(uint32_t));
    size_t lengths = maudLayoutAdd(&layout, 2 * (size_t)links, sizeof(float), alignof(float));
    if (layout.overflow)
    {
        return false;
    }
    g->bytes = layout.size;
    g->memory = maudAllocate(allocator, g->bytes, alignof(maudVector3));
    if (g->memory == nullptr)
    {
        return false;
    }
    maudLayoutPoison(&layout, g->memory);
    unsigned char* base = g->memory;
    g->points = (maudVector3*)(base + points);
    g->offsets = (uint32_t*)(base + offsets);
    g->neighbours = (uint32_t*)(base + neighbours);
    g->lengths = (float*)(base + lengths);
    g->count = count;
    g->links = links;
    return true;
}

// Fills the rows from the linked pairs in pair order: each row's
// offset serves as its cursor and is shifted back after.
static void Fill(maudProbeGraph* g, const Pair* pairs, const uint8_t* linked, uint32_t count)
{
    memset(g->offsets, 0, ((size_t)g->count + 1) * sizeof(uint32_t));
    for (uint32_t p = 0; p < count; ++p)
    {
        if (linked[p] != 0)
        {
            g->offsets[pairs[p].i + 1] += 1;
            g->offsets[pairs[p].j + 1] += 1;
        }
    }
    for (uint32_t i = 1; i <= g->count; ++i)
    {
        g->offsets[i] += g->offsets[i - 1];
    }
    for (uint32_t p = 0; p < count; ++p)
    {
        if (linked[p] != 0)
        {
            uint32_t i = pairs[p].i;
            uint32_t j = pairs[p].j;
            float length = Distance(g->points[i], g->points[j]);
            g->neighbours[g->offsets[i]] = j;
            g->lengths[g->offsets[i]++] = length;
            g->neighbours[g->offsets[j]] = i;
            g->lengths[g->offsets[j]++] = length;
        }
    }
    for (uint32_t i = g->count; i > 0; --i)
    {
        g->offsets[i] = g->offsets[i - 1];
    }
    g->offsets[0] = 0;
}

// The temporaries of linking.
typedef struct Scratch
{
    Key* keys;
    Pair* pairs;
    uint8_t* linked;
    uint32_t count;
    uint64_t pairCount;
} Scratch;

static void ReleaseScratch(const maudAllocator* allocator, Scratch* t)
{
    if (t->keys != nullptr)
    {
        maudRelease(allocator, t->keys, (size_t)t->count * sizeof(Key), alignof(Key));
    }
    if (t->pairs != nullptr)
    {
        maudRelease(allocator, t->pairs, (size_t)t->pairCount * sizeof(Pair), alignof(Pair));
    }
    if (t->linked != nullptr)
    {
        maudRelease(allocator, t->linked, (size_t)t->pairCount, 1);
    }
}

// Links the probes: the pairs within range, a ray each, the rows.
static maudResult Connect(const maudProbeQueries* q, float range, const maudVector3* points,
                          uint32_t count, maudProbeGraph* graph)
{
    Scratch t = {.count = count};
    t.keys =
        count > 0 ? maudAllocate(q->allocator, (size_t)count * sizeof(Key), alignof(Key)) : nullptr;
    if (count > 0 && t.keys == nullptr)
    {
        return maud_errorCapacity;
    }
    double width = (double)range * CELL_MARGIN;
    for (uint32_t i = 0; i < count; ++i)
    {
        maudVector3 p = points[i];
        t.keys[i] = (Key){{CellOf(p.x, width), CellOf(p.y, width), CellOf(p.z, width)}, i};
    }
    if (count > 1)
    {
        qsort(t.keys, count, sizeof(Key), ByCell);
    }
    uint64_t total = Sweep(t.keys, points, count, range, q->maxPairs, nullptr);
    if (total > q->maxPairs)
    {
        ReleaseScratch(q->allocator, &t);
        return maud_errorCapacity;
    }
    t.pairCount = total;
    if (total > 0)
    {
        t.pairs = maudAllocate(q->allocator, (size_t)total * sizeof(Pair), alignof(Pair));
        t.linked = maudAllocate(q->allocator, (size_t)total, 1);
    }
    if (total > 0 && (t.pairs == nullptr || t.linked == nullptr))
    {
        ReleaseScratch(q->allocator, &t);
        return maud_errorCapacity;
    }
    Sweep(t.keys, points, count, range, total, t.pairs);
    if (total > 1)
    {
        qsort(t.pairs, (size_t)total, sizeof(Pair), ByIndices);
    }
    Links l = {q, points, t.pairs, (uint32_t)total, t.linked};
    Run(q, LinkBatches, (uint32_t)((total + MAUD_PROBE_BATCH - 1) / MAUD_PROBE_BATCH), &l);
    uint32_t links = 0;
    for (uint64_t p = 0; p < total; ++p)
    {
        links += t.linked[p];
    }
    maudResult result = maud_errorCapacity;
    if (maudLayProbeGraph(q->allocator, graph, count, links))
    {
        if (count > 0)
        {
            memcpy(graph->points, points, (size_t)count * sizeof(maudVector3));
        }
        Fill(graph, t.pairs, t.linked, (uint32_t)total);
        result = maud_success;
    }
    ReleaseScratch(q->allocator, &t);
    return result;
}

maudResult maudBuildProbeGraph(const maudProbeQueries* queries, const maudProbeSetDef* def,
                               maudProbeGraph* graph)
{
    *graph = (maudProbeGraph){0};
    const maudVector3* points = def->points;
    uint32_t count = def->pointCount;
    maudVector3* generated = nullptr;
    if (points == nullptr)
    {
        if (queries->closestHit == nullptr)
        {
            return maud_errorUnsupported;
        }
        maudResult result = Generate(queries, def, &generated, &count);
        if (result != maud_success)
        {
            return result;
        }
        points = generated;
    }
    else if (count > queries->maxProbes)
    {
        return maud_errorCapacity;
    }
    maudResult result = Connect(queries, def->range, points, count, graph);
    graph->range = result == maud_success ? def->range : 0.0f;
    if (generated != nullptr)
    {
        maudRelease(queries->allocator, generated, (size_t)count * sizeof(maudVector3),
                    alignof(maudVector3));
    }
    return result;
}

void maudReleaseProbeGraph(const maudAllocator* allocator, maudProbeGraph* graph)
{
    if (graph->memory != nullptr)
    {
        maudRelease(allocator, graph->memory, graph->bytes, alignof(maudVector3));
    }
    *graph = (maudProbeGraph){0};
}
