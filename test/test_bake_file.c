// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The bake file (whitebox): a set with reverberation and fields, and one
// of probes and links alone, write and read back to the same memory and
// write again to the same bytes; every malformed part is refused as
// invalid (magic, size, checksum, range, layers, a neighbour out of
// range, a self link, a falling row, rows not spanning the links, a
// link listed from one end only, a link longer than the range, a
// point, a time, a level, a tail's time and level and a field out of
// range), another version as unsupported
// (version 1, without tails, still reads), counts past the limits as
// capacity, and fields of another layout as unsupported; a failing
// allocator is a capacity error with nothing leaked.

#include "allocator.h"
#include "bake_file.h"
#include "crc32.h"
#include "test_harness.h"

#include "maul-audio/scene.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum
{
    ORDER = 1,
    BINS = 6,
    FIELD = 4 * 3 * BINS
};

static const maudAllocator s_allocator = {nullptr, nullptr, nullptr};

static long s_live;
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

// Two rows of probes 2 m apart with a wall between them over x < 1.5:
// some pairs within range are linked, some are not.
static maudProbeGraph Graph(void)
{
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[12] = {0};
    maudVector3 v[8];
    for (int i = 0; i < 8; ++i)
    {
        v[i] = (maudVector3){(i & 1) ? 1.5f : -1.0f, (i & 2) ? 3.0f : 0.0f, (i & 4) ? 1.1f : 0.9f};
    }
    maudMesh mesh = {v, 8, faces, materials, 12};
    maudAcousticSceneDef sd = maudDefaultAcousticSceneDef();
    sd.meshes = &mesh;
    sd.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&sd, &scene) == maud_success, "a wall");
    maudVector3 points[8];
    for (int i = 0; i < 4; ++i)
    {
        points[i] = (maudVector3){(float)i, 1.5f, 0.0f};
        points[4 + i] = (maudVector3){(float)i, 1.5f, 2.0f};
    }
    maudProbeQueries q = {.anyHit = maudSceneAnyHit,
                          .rayContext = scene,
                          .allocator = &s_allocator,
                          .maxProbes = 8,
                          .maxPairs = 64};
    maudProbeSetDef def = {.points = points, .pointCount = 8, .range = 2.5f};
    maudProbeGraph g;
    CHECK(maudBuildProbeGraph(&q, &def, &g) == maud_success, "a graph");
    maudDestroyAcousticScene(scene);
    return g;
}

static maudProbeBake Bake(uint32_t count)
{
    maudProbeBake b;
    CHECK(maudCreateProbeBake(&s_allocator, count, FIELD, &b), "a bake");
    for (uint32_t i = 0; i < count * 3; ++i)
    {
        b.times[i] = 0.3f + 0.01f * (float)i;
        b.levels[i] = -6.0f + 0.5f * (float)i;
        // Every other band with a tail.
        b.tailTimes[i] = i % 2 == 0 ? 1.2f + 0.01f * (float)i : 0.0f;
        b.tailLevels[i] = i % 2 == 0 ? -20.0f + 0.1f * (float)i : -96.0f;
    }
    for (uint32_t i = 0; i < count * FIELD; ++i)
    {
        b.fields[i] = (i % FIELD) < 3 * BINS ? 1e-3f * (float)(i % 17) : -2e-4f * (float)(i % 5);
    }
    return b;
}

typedef struct File
{
    uint8_t* bytes;
    size_t size;
    uint32_t probes;
    uint32_t links;
} File;

static File Write(const maudProbeGraph* g, const maudProbeBake* b)
{
    File f = {nullptr, maudBakeFileBytes(g, b, ORDER, BINS), g->count, g->links};
    f.bytes = malloc(f.size + 1);
    maudWriteBakeFile(g, b, ORDER, BINS, f.bytes);
    return f;
}

static void Seal(File* f)
{
    uint32_t crc = maudCrc32(f->bytes + 40, f->size - 40);
    for (int k = 0; k < 4; ++k)
    {
        f->bytes[36 + k] = (uint8_t)(crc >> (8 * k));
    }
}

static void Put(uint8_t* p, uint32_t v)
{
    for (int k = 0; k < 4; ++k)
    {
        p[k] = (uint8_t)(v >> (8 * k));
    }
}

static uint32_t Get(const uint8_t* p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void PutFloat(uint8_t* p, float v)
{
    uint32_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));
    Put(p, bits);
}

static const maudBakeLimits s_limits = {64, 64, ORDER, BINS};

static maudResult Read(const File* f, const maudBakeLimits* limits)
{
    maudProbeGraph g;
    maudProbeBake b;
    maudResult r = maudReadBakeFile(f->bytes, f->size, limits, &s_allocator, &g, &b);
    maudReleaseProbeGraph(&s_allocator, &g);
    maudReleaseProbeBake(&s_allocator, &b);
    return r;
}

// Sections' offsets, as the format lays them out.
static size_t Rows(const File* f)
{
    return 40 + 12 * (size_t)f->probes;
}

static size_t Neighbours(const File* f)
{
    return Rows(f) + 4 * ((size_t)f->probes + 1);
}

static size_t Times(const File* f)
{
    return Neighbours(f) + 8 * (size_t)f->links;
}

// A copy of the file with one change, resealed.
static File Changed(const File* f)
{
    File c = *f;
    c.bytes = malloc(f->size + 1);
    memcpy(c.bytes, f->bytes, f->size);
    return c;
}

static void Expect(File* c, maudResult want, const char* what)
{
    Seal(c);
    maudResult got = Read(c, &s_limits);
    if (got != want)
    {
        printf("%s: got %d, want %d\n", what, got, want);
    }
    CHECK(got == want, what);
    free(c->bytes);
}

static void TestRoundTrip(const maudProbeGraph* g, const maudProbeBake* b, const File* f)
{
    maudProbeGraph rg;
    maudProbeBake rb;
    CHECK(maudReadBakeFile(f->bytes, f->size, &s_limits, &s_allocator, &rg, &rb) == maud_success,
          "read back");
    CHECK(rg.bytes == g->bytes && memcmp(rg.memory, g->memory, g->bytes) == 0 &&
              rg.range == g->range && rb.bytes == b->bytes &&
              memcmp(rb.memory, b->memory, b->bytes) == 0,
          "the same memory");
    File again = Write(&rg, &rb);
    CHECK(again.size == f->size && memcmp(again.bytes, f->bytes, f->size) == 0, "the same bytes");
    free(again.bytes);
    maudReleaseProbeGraph(&s_allocator, &rg);
    maudReleaseProbeBake(&s_allocator, &rb);
    // Probes and links alone load whatever the fields' layout.
    File bare = Write(g, nullptr);
    const maudBakeLimits other = {64, 64, 3, 401};
    CHECK(bare.size == Times(f) && Read(&bare, &other) == maud_success, "probes and links alone");
    free(bare.bytes);
}

// Version 1, without tails: the file less its tails' sections reads as
// the same set with none.
static void TestVersion1(const maudProbeBake* b, const File* f)
{
    size_t tails = Times(f) + 24 * (size_t)f->probes;
    size_t cut = 24 * (size_t)f->probes;
    File old = {malloc(f->size), f->size - cut, f->probes, f->links};
    memcpy(old.bytes, f->bytes, tails);
    memcpy(old.bytes + tails, f->bytes + tails + cut, f->size - tails - cut);
    Put(old.bytes + 8, 1);
    Seal(&old);
    maudProbeGraph rg;
    maudProbeBake rb;
    CHECK(maudReadBakeFile(old.bytes, old.size, &s_limits, &s_allocator, &rg, &rb) == maud_success,
          "version 1 read");
    size_t values = (size_t)f->probes * 3;
    bool none = true;
    for (size_t i = 0; i < values; ++i)
    {
        none = none && rb.tailTimes[i] == 0.0f && rb.tailLevels[i] == -96.0f;
    }
    CHECK(memcmp(rb.times, b->times, values * sizeof(float)) == 0 &&
              memcmp(rb.levels, b->levels, values * sizeof(float)) == 0 &&
              memcmp(rb.fields, b->fields, values / 3 * FIELD * sizeof(float)) == 0 && none,
          "version 1: the same set, no tails");
    maudReleaseProbeGraph(&s_allocator, &rg);
    maudReleaseProbeBake(&s_allocator, &rb);
    free(old.bytes);
}

// A row's entry replaced by a probe in range it is not linked to, the
// row still rising: listed from one end only.
static bool OneEnded(File* c, const maudProbeGraph* g)
{
    for (uint32_t i = 0; i < g->count; ++i)
    {
        for (uint32_t e = g->offsets[i]; e < g->offsets[i + 1]; ++e)
        {
            uint32_t low = e > g->offsets[i] ? g->neighbours[e - 1] : 0;
            uint32_t high = e + 1 < g->offsets[i + 1] ? g->neighbours[e + 1] : g->count;
            for (uint32_t k = low + 1; k < high; ++k)
            {
                bool linked = false;
                for (uint32_t x = g->offsets[i]; x < g->offsets[i + 1]; ++x)
                {
                    linked = linked || g->neighbours[x] == k;
                }
                float dx = g->points[k].x - g->points[i].x;
                float dz = g->points[k].z - g->points[i].z;
                if (k != i && !linked && sqrtf(dx * dx + dz * dz) <= 2.5f)
                {
                    Put(c->bytes + Neighbours(c) + 4 * (size_t)e, k);
                    return true;
                }
            }
        }
    }
    return false;
}

// The graph with probes 0 and 1 linked to themselves as well: one link
// more, each row still rising, every link listed from both ends; only
// the self links are wrong.
static File SelfLinked(const maudProbeGraph* g, const maudProbeBake* b)
{
    maudProbeGraph s;
    CHECK(maudLayProbeGraph(&s_allocator, &s, g->count, g->links + 1), "a graph");
    memcpy(s.points, g->points, (size_t)g->count * sizeof(maudVector3));
    s.range = g->range;
    uint32_t e = 0;
    for (uint32_t i = 0; i < g->count; ++i)
    {
        s.offsets[i] = e;
        bool placed = i > 1;
        for (uint32_t k = g->offsets[i]; k < g->offsets[i + 1]; ++k)
        {
            if (!placed && g->neighbours[k] > i)
            {
                s.neighbours[e++] = i;
                placed = true;
            }
            s.neighbours[e++] = g->neighbours[k];
        }
        if (!placed)
        {
            s.neighbours[e++] = i;
        }
    }
    s.offsets[g->count] = e;
    File f = Write(&s, b);
    maudReleaseProbeGraph(&s_allocator, &s);
    return f;
}

static void TestRefused(const maudProbeGraph* g, const maudProbeBake* b, const File* f)
{
    File self = SelfLinked(g, b);
    CHECK(Read(&self, &s_limits) == maud_errorInvalid, "probes linked to themselves");
    free(self.bytes);
    File c = Changed(f);
    c.bytes[0] ^= 1;
    Expect(&c, maud_errorInvalid, "magic");
    c = Changed(f);
    Put(c.bytes + 8, 3);
    Expect(&c, maud_errorUnsupported, "a later version");
    c = Changed(f);
    Put(c.bytes + 8, 0);
    Expect(&c, maud_errorUnsupported, "version 0");
    c = Changed(f);
    c.size -= 1;
    Expect(&c, maud_errorInvalid, "a byte short");
    c = Changed(f);
    c.bytes[c.size++] = 0;
    Expect(&c, maud_errorInvalid, "a byte long");
    c = Changed(f);
    Seal(&c);
    c.bytes[c.size - 1] ^= 0x40;
    CHECK(Read(&c, &s_limits) == maud_errorInvalid, "the checksum");
    free(c.bytes);
    c = Changed(f);
    PutFloat(c.bytes + 20, 0.05f);
    Expect(&c, maud_errorInvalid, "the range");
    c = Changed(f);
    Put(c.bytes + 24, 2);
    Expect(&c, maud_errorInvalid, "the layers");
    c = Changed(f);
    Put(c.bytes + Neighbours(&c), f->probes);
    Expect(&c, maud_errorInvalid, "a neighbour out of range");
    c = Changed(f);
    Put(c.bytes + Neighbours(&c), 0);
    Expect(&c, maud_errorInvalid, "a self link");
    c = Changed(f);
    uint32_t first = Get(c.bytes + Neighbours(&c));
    Put(c.bytes + Neighbours(&c), Get(c.bytes + Neighbours(&c) + 4));
    Put(c.bytes + Neighbours(&c) + 4, first);
    Expect(&c, maud_errorInvalid, "a falling row");
    c = Changed(f);
    Put(c.bytes + Rows(&c), 1);
    Expect(&c, maud_errorInvalid, "rows not starting at 0");
    c = Changed(f);
    Put(c.bytes + Rows(&c) + 4 * (size_t)f->probes, 2 * f->links - 1);
    Expect(&c, maud_errorInvalid, "rows not ending at the links");
    c = Changed(f);
    CHECK(OneEnded(&c, g), "a pair in range unlinked");
    Expect(&c, maud_errorInvalid, "a link from one end");
    c = Changed(f);
    PutFloat(c.bytes + 40, -9.0f);
    Expect(&c, maud_errorInvalid, "a link past the range");
    c = Changed(f);
    PutFloat(c.bytes + 44, NAN);
    Expect(&c, maud_errorInvalid, "a point not finite");
    c = Changed(f);
    PutFloat(c.bytes + Times(&c), 0.05f);
    Expect(&c, maud_errorInvalid, "a time");
    c = Changed(f);
    PutFloat(c.bytes + Times(&c) + 12 * (size_t)f->probes, 30.0f);
    Expect(&c, maud_errorInvalid, "a level");
    c = Changed(f);
    PutFloat(c.bytes + Times(&c) + 24 * (size_t)f->probes, 0.05f);
    Expect(&c, maud_errorInvalid, "a tail's time");
    c = Changed(f);
    PutFloat(c.bytes + Times(&c) + 36 * (size_t)f->probes, 30.0f);
    Expect(&c, maud_errorInvalid, "a tail's level");
    c = Changed(f);
    PutFloat(c.bytes + Times(&c) + 48 * (size_t)f->probes, -1.0f);
    Expect(&c, maud_errorInvalid, "a negative W");
    c = Changed(f);
    PutFloat(c.bytes + Times(&c) + 48 * (size_t)f->probes + 4 * 3 * BINS, INFINITY);
    Expect(&c, maud_errorInvalid, "a field not finite");
    const maudBakeLimits fewProbes = {f->probes - 1, 64, ORDER, BINS};
    const maudBakeLimits fewLinks = {64, f->links - 1, ORDER, BINS};
    const maudBakeLimits order = {64, 64, 2, BINS};
    const maudBakeLimits none = {64, 64, 0, 0};
    CHECK(Read(f, &fewProbes) == maud_errorCapacity && Read(f, &fewLinks) == maud_errorCapacity,
          "past the limits");
    CHECK(Read(f, &order) == maud_errorUnsupported && Read(f, &none) == maud_errorUnsupported,
          "fields of another layout");
    maudProbeGraph rg;
    maudProbeBake rb;
    CHECK(maudReadBakeFile(nullptr, 0, &s_limits, &s_allocator, &rg, &rb) == maud_errorInvalid &&
              maudReadBakeFile(f->bytes, 39, &s_limits, &s_allocator, &rg, &rb) ==
                  maud_errorInvalid,
          "no file, a short one");
}

// The set's block failing, then the bake's: a capacity error with
// nothing leaked.
static void TestFailingAllocator(const File* f)
{
    const maudAllocator counted = {CountedAlloc, CountedFree, nullptr};
    for (long failAt = 0; failAt < 2; ++failAt)
    {
        s_failAfter = failAt;
        maudProbeGraph rg;
        maudProbeBake rb;
        CHECK(maudReadBakeFile(f->bytes, f->size, &s_limits, &counted, &rg, &rb) ==
                      maud_errorCapacity &&
                  s_live == 0,
              "a failing allocator is a capacity error, nothing leaked");
    }
    s_failAfter = -1;
}

int main(void)
{
    maudProbeGraph g = Graph();
    maudProbeBake b = Bake(g.count);
    File f = Write(&g, &b);
    printf("file: %u probes, %u links, %zu bytes\n", g.count, g.links, f.size);
    TestRoundTrip(&g, &b, &f);
    TestRefused(&g, &b, &f);
    TestVersion1(&b, &f);
    TestFailingAllocator(&f);
    free(f.bytes);
    maudReleaseProbeBake(&s_allocator, &b);
    maudReleaseProbeGraph(&s_allocator, &g);
    return s_failures == 0 ? 0 : 1;
}
