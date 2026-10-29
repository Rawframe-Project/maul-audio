// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host code the spatializer runs calling back into it, in an office
// (5 x 3 x 4 m): from the any-hit and closest-hit queries, from both
// task hooks and from the allocator (making, baking, loading and
// destroying a probe set, and destroying the spatializer), every call
// that changes the spatializer is refused with maud_errorBusy and
// counted as misuse, maudDestroySpatializer does nothing, and the outer
// call goes on as if nothing happened. The calls the hooks may make (the
// rendering side, the misuse count) work from them. Each invalid
// argument to the functions that change it counts once; a stale id, a
// malformed bake and the queries do not.

#include "test_harness.h"

#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// The calls TryAll makes that return a status, and with the
// spatializer's destruction the misuse they count.
#define CALLS  12
#define COUNTS 13

typedef enum Hook
{
    hookNone,
    hookAnyHit,
    hookClosestHit,
    hookEnqueue,
    hookFinish,
    hookAllocator,
} Hook;

static maudAcousticScene* s_scene;
static maudSpatializer* s_s;
static maudSourceId s_source;
static maudProbeSetId s_set;
static maudVector3 s_points[4];
static uint8_t* s_bytes;
static size_t s_size;
// The hook armed to call back in once, and what it found.
static Hook s_armed;
static int s_refused;
static uint64_t s_counted;
static bool s_allowed;

static maudProbeSetDef SetDef(void)
{
    maudProbeSetDef def = maudDefaultProbeSetDef();
    def.points = s_points;
    def.pointCount = 4;
    def.range = 3.0f;
    return def;
}

// Makes every call that changes the spatializer, each one expected to
// be refused, then the calls a hook may make.
static void TryAll(void)
{
    maudSpatializer* s = s_s;
    uint64_t before = maudSpatializer_GetMisuse(s);
    maudSourceDef sourceDef = maudDefaultSourceDef();
    maudSourceId source = {0, 0};
    maudPose pose = {{1.0f, 1.5f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudAcousticMaterial material = {{0.1f, 0.1f, 0.1f}, 0.3f, {0.0f, 0.0f, 0.0f}};
    maudProbeSetDef setDef = SetDef();
    maudProbeSetId set = {0, 0};
    int refused = 0;
    refused += maudCreateSource(s, &sourceDef, &source) == maud_errorBusy;
    refused += maudDestroySource(s, s_source) == maud_errorBusy;
    refused += maudSource_SetPose(s, s_source, &pose) == maud_errorBusy;
    refused += maudSpatializer_SimulateDirect(s, &pose) == maud_errorBusy;
    refused += maudSpatializer_SetMaterials(s, &material, 1) == maud_errorBusy;
    refused += maudSpatializer_SimulateReverb(s, &pose) == maud_errorBusy;
    refused += maudCreateProbeSet(s, &setDef, &set) == maud_errorBusy;
    refused += maudDestroyProbeSet(s, s_set) == maud_errorBusy;
    refused += maudProbeSet_UseForPathing(s, s_set) == maud_errorBusy;
    refused += maudProbeSet_Bake(s, s_set) == maud_errorBusy;
    refused += maudProbeSet_UseBakedReverb(s, s_set) == maud_errorBusy;
    refused += maudLoadProbeSet(s, s_bytes, s_size, &set) == maud_errorBusy;
    maudDestroySpatializer(s);
    s_refused = refused;
    s_counted = maudSpatializer_GetMisuse(s) - before;
    maudReverbResult reverb;
    maudDirectResult direct;
    (void)maudSpatializer_LatchResults(s);
    s_allowed = maudSpatializer_GetReverbResult(s, &reverb) != maud_errorBusy &&
                maudSource_GetDirectResult(s, s_source, &direct) != maud_errorBusy &&
                maudSpatializer_GetMisuse(s) == before + COUNTS;
}

static void Fire(Hook hook)
{
    if (s_armed == hook)
    {
        s_armed = hookNone;
        TryAll();
    }
}

static void AnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    Fire(hookAnyHit);
    maudFindAnyHit(rays, count, occluded, context);
}

static void ClosestHit(const maudRay* rays, uint32_t count, maudRayHit* hits, void* context)
{
    Fire(hookClosestHit);
    maudFindClosestHit(rays, count, hits, context);
}

static void* Enqueue(maudTaskFunction* task, uint32_t itemCount, uint32_t minRange,
                     void* taskContext, void* userContext)
{
    (void)minRange;
    Fire(hookEnqueue);
    task(0, itemCount, taskContext);
    return userContext;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
    Fire(hookFinish);
}

// Aligned by hand: Windows' C library has no aligned_alloc.
static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    Fire(hookAllocator);
    unsigned char* raw = malloc(size + alignment + sizeof(void*));
    if (raw == nullptr)
    {
        return nullptr;
    }
    uintptr_t start = (uintptr_t)(raw + sizeof(void*));
    unsigned char* aligned =
        (unsigned char*)((start + alignment - 1) & ~(uintptr_t)(alignment - 1));
    memcpy(aligned - sizeof(void*), &raw, sizeof(void*));
    return aligned;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    Fire(hookAllocator);
    void* raw;
    memcpy(&raw, (unsigned char*)memory - sizeof(void*), sizeof(void*));
    free(raw);
}

static maudAcousticScene* Office(void)
{
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[12] = {0};
    maudVector3 v[8];
    for (int i = 0; i < 8; ++i)
    {
        v[i] = (maudVector3){(i & 1) ? 5.0f : 0.0f, (i & 2) ? 3.0f : 0.0f, (i & 4) ? 4.0f : 0.0f};
    }
    maudMesh mesh = {v, 8, faces, materials, 12};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "an office");
    return scene;
}

static maudSpatializer* Create(void)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = AnyHit;
    def.closestHit = ClosestHit;
    def.rayContext = s_scene;
    def.reverbRays = 256;
    def.probeSetCapacity = 4;
    def.enqueueTask = Enqueue;
    def.finishTask = Finish;
    def.allocator = (maudAllocator){Alloc, Free, nullptr};
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudAcousticMaterial material = {{0.1f, 0.1f, 0.1f}, 0.3f, {0.0f, 0.0f, 0.0f}};
    CHECK(maudSpatializer_SetMaterials(s, &material, 1) == maud_success, "a material");
    return s;
}

static const maudPose s_listener = {{2.5f, 1.5f, 2.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};

static bool Fired(void)
{
    return s_armed == hookNone && s_refused == CALLS && s_counted == COUNTS && s_allowed;
}

static void Arm(Hook hook)
{
    s_armed = hook;
    s_refused = 0;
    s_counted = 0;
    s_allowed = false;
}

static void TestHooks(void)
{
    Arm(hookAnyHit);
    CHECK(maudSpatializer_SimulateDirect(s_s, &s_listener) == maud_success && Fired(),
          "from the any-hit query: every call refused and counted, the step goes on");
    Arm(hookClosestHit);
    CHECK(maudSpatializer_SimulateReverb(s_s, &s_listener) == maud_success && Fired(),
          "from the closest-hit query");
    Arm(hookEnqueue);
    CHECK(maudSpatializer_SimulateReverb(s_s, &s_listener) == maud_success && Fired(),
          "from enqueueTask");
    Arm(hookFinish);
    CHECK(maudSpatializer_SimulateReverb(s_s, &s_listener) == maud_success && Fired(),
          "from finishTask");
    maudDirectResult direct;
    CHECK(maudSpatializer_LatchResults(s_s) > 0 &&
              maudSource_GetDirectResult(s_s, s_source, &direct) == maud_success,
          "the steps published");
}

static void TestAllocator(void)
{
    maudProbeSetDef def = SetDef();
    maudProbeSetId set = {0, 0};
    Arm(hookAllocator);
    CHECK(maudCreateProbeSet(s_s, &def, &set) == maud_success && Fired(),
          "from the allocator making a set");
    Arm(hookAllocator);
    CHECK(maudProbeSet_Bake(s_s, set) == maud_success && Fired(), "baking it");
    Arm(hookAllocator);
    CHECK(maudDestroyProbeSet(s_s, set) == maud_success && Fired(), "destroying it");
    Arm(hookAllocator);
    CHECK(maudLoadProbeSet(s_s, s_bytes, s_size, &set) == maud_success && Fired(),
          "loading a bake");
    CHECK(maudDestroyProbeSet(s_s, set) == maud_success, "the loaded set destroyed");
}

// Each invalid argument to a function that changes a spatializer,
// refused and counted once.
#define COUNTED(s, call, msg)                                                                      \
    do                                                                                             \
    {                                                                                              \
        uint64_t before_ = maudSpatializer_GetMisuse(s);                                           \
        CHECK((call) == maud_errorInvalid && maudSpatializer_GetMisuse(s) == before_ + 1, msg);    \
    } while (0)

static void TestEachCounted(void)
{
    maudSourceId source = {0, 0};
    maudProbeSetId set = {0, 0};
    maudAcousticMaterial wrong = {{2.0f, 0.1f, 0.1f}, 0.3f, {0.0f, 0.0f, 0.0f}};
    const maudProbeSetId unknown = {99, 1};
    COUNTED(s_s, maudCreateSource(s_s, nullptr, &source), "a source without a def");
    COUNTED(s_s, maudDestroySource(s_s, (maudSourceId){0, 0}), "a 0 source id");
    COUNTED(s_s, maudSource_SetPose(s_s, s_source, nullptr), "no pose");
    COUNTED(s_s, maudSpatializer_SetMaterials(s_s, nullptr, 1), "no materials");
    COUNTED(s_s, maudSpatializer_SetMaterials(s_s, &wrong, 1), "an absorption past 1");
    COUNTED(s_s, maudSpatializer_SimulateDirect(s_s, nullptr), "a direct step without a listener");
    COUNTED(s_s, maudSpatializer_SimulateReverb(s_s, nullptr), "a reverberation step without one");
    COUNTED(s_s, maudCreateProbeSet(s_s, nullptr, &set), "a set without a def");
    COUNTED(s_s, maudDestroyProbeSet(s_s, unknown), "destroying an unknown set");
    COUNTED(s_s, maudProbeSet_UseForPathing(s_s, unknown), "pathing over one");
    COUNTED(s_s, maudProbeSet_Bake(s_s, unknown), "baking one");
    COUNTED(s_s, maudProbeSet_UseBakedReverb(s_s, unknown), "using one's bake");
    COUNTED(s_s, maudLoadProbeSet(s_s, nullptr, 0, &set), "loading no bytes");
    // The rendering side counts its invalid arguments too.
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.reverbRays = 64;
    def.reflectionOrder = 1;
    def.reflectionDuration = 0.05f;
    def.reflectionRate = 48000;
    maudSpatializer* r = nullptr;
    CHECK(maudCreateSpatializer(&def, &r) == maud_success, "a spatializer with reflections");
    const maudQuaternion none = {0.0f, 0.0f, 0.0f, 0.0f};
    const maudQuaternion facing = {0.0f, 0.0f, 0.0f, 1.0f};
    float send[4] = {0};
    float channel[4][4] = {{0}};
    float* bed[4] = {channel[0], channel[1], channel[2], channel[3]};
    float* gap[4] = {channel[0], nullptr, channel[2], channel[3]};
    COUNTED(r, maudSpatializer_RenderReflections(r, &none, send, bed, 4), "a zero orientation");
    COUNTED(r, maudSpatializer_RenderReflections(r, &facing, nullptr, bed, 4), "no send");
    COUNTED(r, maudSpatializer_RenderReflections(r, &facing, send, gap, 4),
            "a bed channel missing");
    maudDestroySpatializer(r);
}

static void TestCounts(void)
{
    uint64_t before = maudSpatializer_GetMisuse(s_s);
    maudPose bad = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}};
    maudProbeSetId set = {0, 0};
    uint8_t junk[16] = {0};
    CHECK(maudSpatializer_SimulateDirect(s_s, &bad) == maud_errorInvalid &&
              maudSource_SetPose(s_s, (maudSourceId){0, 0}, &s_listener) == maud_errorInvalid &&
              maudDestroyProbeSet(s_s, (maudProbeSetId){0, 0}) == maud_errorInvalid,
          "invalid arguments refused");
    CHECK(maudSpatializer_GetMisuse(s_s) == before + 3, "each counted");
    maudSourceDef sourceDef = maudDefaultSourceDef();
    maudSourceId gone = {0, 0};
    CHECK(maudCreateSource(s_s, &sourceDef, &gone) == maud_success &&
              maudDestroySource(s_s, gone) == maud_success,
          "a source made and destroyed");
    maudProbeSetInfo info;
    maudDirectResult direct;
    CHECK(maudDestroySource(s_s, gone) == maud_errorStale &&
              maudLoadProbeSet(s_s, junk, sizeof(junk), &set) == maud_errorFormat &&
              maudProbeSet_GetInfo(s_s, (maudProbeSetId){0, 0}, 0, &info, nullptr, 0) ==
                  maud_errorInvalid &&
              maudSource_GetDirectResult(s_s, (maudSourceId){0, 0}, &direct) == maud_errorInvalid,
          "a stale id, a malformed bake and the queries refused");
    CHECK(maudSpatializer_GetMisuse(s_s) == before + 3, "none of them counted");
}

static void TestDestroy(void)
{
    // The allocator fires on the first block the destruction releases;
    // what it calls is refused while the spatializer is still whole.
    Arm(hookAllocator);
    maudDestroySpatializer(s_s);
    CHECK(s_armed == hookNone && s_refused == CALLS && s_counted == COUNTS,
          "from the allocator destroying the spatializer");
    s_s = nullptr;
}

int main(void)
{
    s_scene = Office();
    s_s = Create();
    maudSourceDef sourceDef = maudDefaultSourceDef();
    sourceDef.occlusion = maud_occlusionRay;
    maudPose pose = {{1.0f, 1.5f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudCreateSource(s_s, &sourceDef, &s_source) == maud_success &&
              maudSource_SetPose(s_s, s_source, &pose) == maud_success,
          "an occluded-ray source");
    for (int i = 0; i < 4; ++i)
    {
        s_points[i] = (maudVector3){1.0f + (float)i, 1.5f, 2.0f};
    }
    maudProbeSetDef def = SetDef();
    CHECK(maudCreateProbeSet(s_s, &def, &s_set) == maud_success &&
              maudProbeSet_Bake(s_s, s_set) == maud_success &&
              maudProbeSet_Save(s_s, s_set, nullptr, 0, &s_size) == maud_success,
          "a baked set");
    s_bytes = malloc(s_size);
    CHECK(s_bytes != nullptr &&
              maudProbeSet_Save(s_s, s_set, s_bytes, s_size, &s_size) == maud_success,
          "saved");
    CHECK(maudSpatializer_GetMisuse(s_s) == 0, "no misuse yet");
    TestHooks();
    TestAllocator();
    TestEachCounted();
    TestCounts();
    TestDestroy();
    free(s_bytes);
    maudDestroyAcousticScene(s_scene);
    return s_failures == 0 ? 0 : 1;
}
