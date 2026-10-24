// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A host's floating-point mode changes no bit the Spatial simulation
// promises. In a room split by a wall, with probes on a 2 m grid: the
// scene and the hits of the host's own queries, the probe set, its bake
// with reflections (saved bytes), a traced and a baked reverberation
// estimate, a source pathed around the wall and one in sight off a plane
// by a subnormal hash the same when the host runs with flush-to-zero
// and denormals-are-zero, with rounding toward zero, and with both, as
// in the default mode; and the same again when the task hooks run the
// tasks on threads each in another of those modes. Every call and every
// task leaves the thread's mode as it found it.

#include "test_float_host.h"
#include "test_harness.h"

#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#define HAVE_THREADS 1
#else
#define HAVE_THREADS 0
#endif

static TestFloatSetting s_default;

// The default mode with flush-to-zero and denormals-are-zero, rounding
// toward zero, or both; the default where the platform has neither.
static TestFloatSetting Mode(bool flush, bool towardZero)
{
    TestFloatSetting setting = s_default;
#if TEST_HAS_FLUSH
    if (flush)
    {
        setting.control |= TEST_FLUSH_BITS;
    }
#ifdef FE_TOWARDZERO
    if (towardZero)
    {
        setting.rounding = FE_TOWARDZERO;
        setting.control |= TEST_ROUNDING_BITS;
    }
#endif
#else
    (void)flush;
    (void)towardZero;
#endif
    return setting;
}

// Calls or tasks that left the thread's mode changed.
static int s_changed;

static void Left(TestFloatSetting before)
{
    s_changed += TestFloatSame(TestFloatGet(), before) ? 0 : 1;
}

enum
{
    WORKERS = 3
};

// The task hooks: item i runs on worker i % 3, the workers in the modes
// flush, toward zero and both, each item checked to leave its worker's
// mode as it was. Without threads, the items run on the calling thread,
// switched to the worker's mode for each and back after.
typedef struct Work
{
    maudTaskFn* task;
    void* context;
    uint32_t items;
    uint32_t worker;
    TestFloatSetting mode;
    int changed;
} Work;

static void RunWork(Work* w)
{
    for (uint32_t i = w->worker; i < w->items; i += WORKERS)
    {
        w->task(i, i + 1, w->context);
        w->changed += TestFloatSame(TestFloatGet(), w->mode) ? 0 : 1;
    }
}

#if HAVE_THREADS
static void* Worker(void* argument)
{
    Work* w = argument;
    TestFloatSet(w->mode);
    RunWork(w);
    return nullptr;
}
#endif

static void* Spread(maudTaskFn* task, uint32_t itemCount, uint32_t minRange, void* taskContext,
                    void* userContext)
{
    (void)minRange;
    Work work[WORKERS];
    for (uint32_t t = 0; t < WORKERS; ++t)
    {
        work[t] = (Work){task, taskContext, itemCount, t, Mode(t != 1, t != 0), 0};
    }
#if HAVE_THREADS
    pthread_t threads[WORKERS];
    for (uint32_t t = 0; t < WORKERS; ++t)
    {
        CHECK(pthread_create(&threads[t], nullptr, Worker, &work[t]) == 0, "a worker");
    }
    for (uint32_t t = 0; t < WORKERS; ++t)
    {
        pthread_join(threads[t], nullptr);
    }
#else
    TestFloatSetting caller = TestFloatGet();
    for (uint32_t t = 0; t < WORKERS; ++t)
    {
        TestFloatSet(work[t].mode);
        RunWork(&work[t]);
    }
    TestFloatSet(caller);
#endif
    for (uint32_t t = 0; t < WORKERS; ++t)
    {
        s_changed += work[t].changed;
    }
    return userContext;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
}

// A room 8 x 3 x 6 m from z = -3, a wall across it from z = -3 to 1 at
// x = 4.
static maudAcousticScene* Room(void)
{
    TestFloatSetting before = TestFloatGet();
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[24] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5,
                                           6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6};
    const maudVector3 low[2] = {{0.0f, 0.0f, -3.0f}, {3.9f, 0.0f, -3.0f}};
    const maudVector3 high[2] = {{8.0f, 3.0f, 3.0f}, {4.1f, 3.0f, 1.0f}};
    maudVector3 v[16];
    uint32_t indices[72];
    for (uint32_t b = 0; b < 2; ++b)
    {
        for (uint32_t i = 0; i < 8; ++i)
        {
            v[b * 8 + i] =
                (maudVector3){(i & 1) ? high[b].x : low[b].x, (i & 2) ? high[b].y : low[b].y,
                              (i & 4) ? high[b].z : low[b].z};
        }
        for (uint32_t k = 0; k < 36; ++k)
        {
            indices[b * 36 + k] = b * 8 + faces[k];
        }
    }
    maudMesh mesh = {v, 16, indices, materials, 24};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a room");
    Left(before);
    return scene;
}

static uint64_t Hash(uint64_t hash, const void* bytes, size_t size)
{
    const unsigned char* b = bytes;
    for (size_t i = 0; i < size; ++i)
    {
        hash = (hash ^ b[i]) * 1099511628211u;
    }
    return hash;
}

// The materials and probe points, computed once in the default mode so
// every run has the same inputs.
static maudAcousticMaterial s_materials[7];
static maudVector3 s_points[12];
static maudRay s_rays[16];

static void Inputs(void)
{
    for (int i = 0; i < 7; ++i)
    {
        s_materials[i] = (maudAcousticMaterial){
            {0.04f + 0.03f * (float)i, 0.06f + 0.04f * (float)i, 0.1f + 0.05f * (float)i},
            0.3f,
            {0.0f, 0.0f, 0.0f}};
    }
    uint32_t n = 0;
    for (int x = 0; x < 4; ++x)
    {
        for (int z = 0; z < 3; ++z)
        {
            s_points[n++] = (maudVector3){2.0f * (float)x + 0.5f, 1.5f, 2.0f * (float)z - 2.5f};
        }
    }
    for (int i = 0; i < 16; ++i)
    {
        float a = 0.39269908f * (float)i;
        maudVector3 d = {cosf(a), 0.3f, sinf(a)};
        float length = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        s_rays[i] =
            (maudRay){{2.0f, 1.5f, -2.0f}, {d.x / length, d.y / length, d.z / length}, 0.0f, 20.0f};
    }
}

// One step at the listener's pose, the latched results of the pathed
// source and the one in sight hashed.
static uint64_t Step(maudSpatializer* s, const maudSourceId* sources, maudPose listener,
                     uint64_t hash)
{
    TestFloatSetting before = TestFloatGet();
    CHECK(maudSimulateReverb(s, &listener) == maud_success, "an estimate");
    Left(before);
    CHECK(maudSimulateDirect(s, &listener) == maud_success, "a direct step");
    Left(before);
    (void)maudLatchResults(s);
    maudReverbResult reverb = {0};
    maudDirectResult direct[2];
    memset(direct, 0, sizeof(direct));
    CHECK(maudGetReverbResult(s, &reverb) == maud_success &&
              maudGetDirectResult(s, sources[0], &direct[0]) == maud_success &&
              maudGetDirectResult(s, sources[1], &direct[1]) == maud_success && direct[0].pathed &&
              !direct[1].pathed,
          "the results, the source behind the wall pathed");
    hash = Hash(hash, &reverb, sizeof(reverb));
    hash = Hash(hash, &direct[0], offsetof(maudDirectResult, pathed));
    return Hash(hash, &direct[1], offsetof(maudDirectResult, pathed));
}

// Everything the simulation promises the bits of, hashed, in the
// calling thread's mode, the tasks spread over the workers or not.
static uint64_t Simulate(bool spread)
{
    maudAcousticScene* scene = Room();
    maudRayHit hits[16];
    uint8_t occluded[16];
    TestFloatSetting before = TestFloatGet();
    maudSceneClosestHit(s_rays, 16, hits, scene);
    maudSceneAnyHit(s_rays, 16, occluded, scene);
    Left(before);
    uint64_t hash = Hash(1469598103934665603u, hits, sizeof(hits));
    hash = Hash(hash, occluded, sizeof(occluded));

    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudSceneAnyHit;
    def.closestHit = maudSceneClosestHit;
    def.rayContext = scene;
    def.reverbRays = 256;
    def.probeSetCapacity = 1;
    def.reflectionOrder = 1;
    def.reflectionDuration = 0.2f;
    def.maxPaths = 1;
    def.enqueueTask = spread ? Spread : nullptr;
    def.finishTask = spread ? Finish : nullptr;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    CHECK(maudSetMaterials(s, s_materials, 7) == maud_success, "materials");
    maudProbeSetDef pd = maudDefaultProbeSetDef();
    pd.points = s_points;
    pd.pointCount = 12;
    pd.range = 3.0f;
    maudProbeSetId set = {0, 0};
    CHECK(maudCreateProbeSet(s, &pd, &set) == maud_success, "a set");
    Left(before);
    CHECK(maudSetPathing(s, set) == maud_success, "pathing");
    Left(before);
    CHECK(maudBakeProbeSet(s, set) == maud_success, "a bake");
    Left(before);
    size_t size = 0;
    CHECK(maudSaveProbeSet(s, set, nullptr, 0, &size) == maud_success, "the bake's size");
    unsigned char* bytes = malloc(size);
    CHECK(bytes != nullptr && maudSaveProbeSet(s, set, bytes, size, &size) == maud_success,
          "the bake");
    Left(before);
    hash = Hash(hash, bytes, size);
    free(bytes);

    // Behind the wall; in sight, off the plane z = 0 by a subnormal,
    // which a host's denormals-are-zero would read as 0.
    maudSourceDef sd = maudDefaultSourceDef();
    sd.pathing = true;
    maudSourceId sources[2] = {{0, 0}, {0, 0}};
    const maudPose at[2] = {{{6.0f, 1.5f, -2.0f}, {0.0f, 0.0f, 0.0f, 1.0f}},
                            {{2.5f, 1.5f, 1e-39f}, {0.0f, 0.0f, 0.0f, 1.0f}}};
    for (int i = 0; i < 2; ++i)
    {
        CHECK(maudCreateSource(s, &sd, &sources[i]) == maud_success &&
                  maudSetSourcePose(s, sources[i], &at[i]) == maud_success,
              "a source");
    }
    // Traced, then baked: where it traced, and on z = 0.
    maudPose listener = {{2.0f, 1.5f, -2.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    hash = Step(s, sources, listener, hash);
    CHECK(maudUseBakedReverb(s, set) == maud_success, "the bake in use");
    hash = Step(s, sources, listener, hash);
    listener.position = (maudVector3){1.5f, 1.5f, 0.0f};
    hash = Step(s, sources, listener, hash);
    maudDestroySpatializer(s);
    maudDestroyAcousticScene(scene);
    return hash;
}

int main(void)
{
    s_default = TestFloatGet();
    Inputs();
    uint64_t expected = Simulate(false);
    printf("default: %016llx\n", (unsigned long long)expected);
    CHECK(Simulate(true) == expected, "the default mode, tasks on the workers");
    static const char* const names[3] = {"flush", "toward zero", "both"};
    for (int k = 0; k < 3; ++k)
    {
        TestFloatSetting mode = Mode(k != 1, k != 0);
        TestFloatSet(mode);
        uint64_t alone = Simulate(false);
        uint64_t spread = Simulate(true);
        TestFloatSet(s_default);
        printf("%s: %016llx, spread %016llx\n", names[k], (unsigned long long)alone,
               (unsigned long long)spread);
        CHECK(alone == expected, "the same bits in the host's mode");
        CHECK(spread == expected, "the same bits, tasks on workers in other modes");
    }
    CHECK(s_changed == 0, "every call and task left the mode as it found it");
    return s_failures == 0 ? 0 : 1;
}
