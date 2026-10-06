// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The spatializer's reverberation estimates: in an office of the
// library's own scene the published times are the estimator's (within
// 3 % of 0.99 s); a task system running batches last first gives the
// same times bit for bit; the times wait for the next direct step; before
// any estimate they are 0.1 s with none counted; without a closest-hit
// query an estimate gives 0.1 s; a listener outside the room hears no
// reverberation, and with a host's any-hit query that
// blocks every path, which lights nothing, too; a def asking for no
// estimates refuses
// them; ray counts out of range and bad air are refused.

#include "test_harness.h"

#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <math.h>

static maudAcousticScene* Office(void)
{
    static maudVector3 v[8];
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[12] = {0};
    for (int i = 0; i < 8; ++i)
    {
        v[i] = (maudVector3){(i & 1) ? 5.0f : 0.0f, (i & 2) ? 4.0f : 0.0f, (i & 4) ? 3.0f : 0.0f};
    }
    maudMesh mesh = {v, 8, faces, materials, 12};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "an office");
    return scene;
}

// A task system that runs ranges of one item, last first.
static long s_enqueued;

static void* Enqueue(maudTaskFn* task, uint32_t itemCount, uint32_t minRange, void* taskContext,
                     void* userContext)
{
    (void)minRange;
    (void)userContext;
    s_enqueued += 1;
    for (uint32_t i = itemCount; i-- > 0;)
    {
        task(i, i + 1, taskContext);
    }
    return &s_enqueued;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
}

static maudSpatializer* Create(maudAcousticScene* scene, bool tasks)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudSceneAnyHit;
    def.closestHit = maudSceneClosestHit;
    def.rayContext = scene;
    def.reverbRays = 1024;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        def.airAbsorption[b] = 0.0f;
    }
    def.enqueueTask = tasks ? Enqueue : nullptr;
    def.finishTask = tasks ? Finish : nullptr;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudAcousticMaterial wall = {{0.1f, 0.1f, 0.1f}, 0.5f, {0, 0, 0}};
    CHECK(maudSetMaterials(s, &wall, 1) == maud_success, "materials");
    return s;
}

static const maudPose s_listener = {{1.85f, 1.64f, 1.35f}, {0.0f, 0.0f, 0.0f, 1.0f}};

static void TestEstimate(void)
{
    maudAcousticScene* scene = Office();
    maudSpatializer* plain = Create(scene, false);
    maudSpatializer* tasked = Create(scene, true);
    maudReverbResult result;
    CHECK(maudGetReverbResult(plain, &result) == maud_errorInvalid, "nothing latched yet");
    CHECK(maudSimulateDirect(plain, &s_listener) == maud_success, "a direct step");
    CHECK(maudLatchResults(plain) == 1, "latched");
    CHECK(maudGetReverbResult(plain, &result) == maud_success && result.estimates == 0 &&
              result.reverbTime[1] == 0.1f,
          "before an estimate: 0.1 s, none counted");
    CHECK(maudSimulateReverb(plain, &s_listener) == maud_success, "an estimate");
    CHECK(maudLatchResults(plain) == 1 && maudGetReverbResult(plain, &result) == maud_success &&
              result.estimates == 0,
          "it waits for the next direct step");
    CHECK(maudSimulateDirect(plain, &s_listener) == maud_success, "a direct step");
    CHECK(maudLatchResults(plain) == 2 && maudGetReverbResult(plain, &result) == maud_success,
          "latched");
    printf("office: %.3f / %.3f / %.3f s after %u estimate\n", (double)result.reverbTime[0],
           (double)result.reverbTime[1], (double)result.reverbTime[2], result.estimates);
    CHECK(result.estimates == 1, "one estimate");
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        CHECK(fabs((double)result.reverbTime[b] / 0.99 - 1.0) < 0.03, "the office's time");
    }
    long before = s_enqueued;
    CHECK(maudSimulateReverb(tasked, &s_listener) == maud_success, "an estimate on tasks");
    CHECK(s_enqueued == before + 1, "through the task hooks");
    CHECK(maudSimulateDirect(tasked, &s_listener) == maud_success, "a direct step");
    maudReverbResult viaTasks;
    CHECK(maudLatchResults(tasked) == 1 && maudGetReverbResult(tasked, &viaTasks) == maud_success,
          "latched");
    bool same = true;
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        same = same && viaTasks.reverbTime[b] == result.reverbTime[b];
    }
    CHECK(same, "the same times however the tasks run");
    // Outside the closed office, rays strike its outer walls and leave:
    // hardly any decay.
    maudPose outside = {{20.0f, 2.0f, 1.5f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSimulateReverb(plain, &outside) == maud_success &&
              maudSimulateDirect(plain, &s_listener) == maud_success &&
              maudLatchResults(plain) == 3 && maudGetReverbResult(plain, &result) == maud_success,
          "an estimate outside");
    printf("outside: %.3f s\n", (double)result.reverbTime[1]);
    CHECK(result.reverbTime[1] < 0.2f, "outside the room, no reverberation");
    maudDestroySpatializer(plain);
    maudDestroySpatializer(tasked);
    maudDestroyAcousticScene(scene);
}

static void Block(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    (void)rays;
    (void)context;
    for (uint32_t i = 0; i < count; ++i)
    {
        occluded[i] = 1;
    }
}

static void TestBlocked(void)
{
    maudAcousticScene* scene = Office();
    maudSpatializer* s = nullptr;
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = Block;
    def.closestHit = maudSceneClosestHit;
    def.rayContext = scene;
    def.reverbRays = 256;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudAcousticMaterial wall = {{0.1f, 0.1f, 0.1f}, 0.5f, {0, 0, 0}};
    CHECK(maudSetMaterials(s, &wall, 1) == maud_success, "materials");
    maudReverbResult result;
    CHECK(maudSimulateReverb(s, &s_listener) == maud_success &&
              maudSimulateDirect(s, &s_listener) == maud_success && maudLatchResults(s) == 1 &&
              maudGetReverbResult(s, &result) == maud_success,
          "an estimate");
    CHECK(result.reverbTime[1] == 0.1f, "every path blocked: nothing lit");
    maudDestroySpatializer(s);
    maudDestroyAcousticScene(scene);
}

static void TestMisuse(void)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    CHECK(def.reverbRays == 2048 && def.airAbsorption[2] > def.airAbsorption[0] &&
              def.airAbsorption[0] > 0.0f,
          "the default: 2048 rays through ordinary air");
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "no hooks");
    CHECK(maudSimulateReverb(s, &s_listener) == maud_success, "no geometry");
    CHECK(maudSimulateDirect(s, &s_listener) == maud_success, "a direct step");
    maudReverbResult result;
    CHECK(maudLatchResults(s) == 1 && maudGetReverbResult(s, &result) == maud_success &&
              result.estimates == 1 && result.reverbTime[0] == 0.1f && result.reverbTime[2] == 0.1f,
          "no geometry: 0.1 s");
    maudPose bad = s_listener;
    bad.position.x = NAN;
    CHECK(maudSimulateReverb(s, &bad) == maud_errorInvalid, "a listener not finite");
    CHECK(maudSimulateReverb(nullptr, &s_listener) == maud_errorInvalid, "NULL");
    CHECK(maudGetReverbResult(s, nullptr) == maud_errorInvalid, "NULL out");
    maudDestroySpatializer(s);
    def.reverbRays = 0;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "no estimates");
    CHECK(maudSimulateReverb(s, &s_listener) == maud_errorState, "estimates refused");
    maudDestroySpatializer(s);
    maudSpatializer* none = nullptr;
    def.reverbRays = 63;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid && none == nullptr, "63 rays");
    def.reverbRays = 16385;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid, "16,385 rays");
    def.reverbRays = 64;
    def.airAbsorption[1] = -0.001f;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid, "negative air");
    def.airAbsorption[1] = INFINITY;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid, "infinite air");
}

int main(void)
{
    TestEstimate();
    TestBlocked();
    TestMisuse();
    return s_failures == 0 ? 0 : 1;
}
