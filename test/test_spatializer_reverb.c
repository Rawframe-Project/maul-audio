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
// blocks every path, which lights nothing, too; without an any-hit
// query every path is clear, which in the convex office gives the
// scene query's times bit for bit; a def asking for no estimates
// refuses them; ray counts out of range and bad air are refused.

#include "test_harness.h"

#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <math.h>
#include <string.h>

static maudScene* Office(void)
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
    maudSceneDef def = maudDefaultSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "an office");
    return scene;
}

// A task system that runs ranges of one item, last first.
static long s_enqueued;

static void* Enqueue(maudTaskFunction* task, uint32_t itemCount, uint32_t minRange,
                     void* taskContext, void* userContext)
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

static maudSpatializer* Create(maudScene* scene, bool tasks, maudAnyHitFunction* anyHit)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = anyHit;
    def.closestHit = maudFindClosestHit;
    def.rayContext = scene;
    def.reverbRays = 1024;
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        def.airAbsorption[b] = 0.0f;
    }
    def.enqueueTask = tasks ? Enqueue : nullptr;
    def.finishTask = tasks ? Finish : nullptr;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudAcousticMaterial wall = {{0.1f, 0.1f, 0.1f}, 0.5f, {0, 0, 0}};
    CHECK(maudSpatializer_SetMaterials(s, &wall, 1) == maud_success, "materials");
    return s;
}

static const maudPose s_listener = {{1.85f, 1.64f, 1.35f}, {0.0f, 0.0f, 0.0f, 1.0f}};

static void TestEstimate(void)
{
    maudScene* scene = Office();
    maudSpatializer* plain = Create(scene, false, maudFindAnyHit);
    maudSpatializer* tasked = Create(scene, true, maudFindAnyHit);
    maudReverbResult result;
    CHECK(maudSpatializer_GetReverbResult(plain, &result) == maud_errorInvalid,
          "nothing latched yet");
    CHECK(maudSpatializer_SimulateDirect(plain, &s_listener) == maud_success, "a direct step");
    CHECK(maudSpatializer_LatchResults(plain) == 1, "latched");
    CHECK(maudSpatializer_GetReverbResult(plain, &result) == maud_success &&
              result.estimates == 0 && result.reverbTime[1] == 0.1f,
          "before an estimate: 0.1 s, none counted");
    CHECK(maudSpatializer_SimulateReverb(plain, &s_listener) == maud_success, "an estimate");
    CHECK(maudSpatializer_LatchResults(plain) == 1 &&
              maudSpatializer_GetReverbResult(plain, &result) == maud_success &&
              result.estimates == 0,
          "it waits for the next direct step");
    CHECK(maudSpatializer_SimulateDirect(plain, &s_listener) == maud_success, "a direct step");
    CHECK(maudSpatializer_LatchResults(plain) == 2 &&
              maudSpatializer_GetReverbResult(plain, &result) == maud_success,
          "latched");
    printf("office: %.3f / %.3f / %.3f s after %u estimate\n", (double)result.reverbTime[0],
           (double)result.reverbTime[1], (double)result.reverbTime[2], result.estimates);
    CHECK(result.estimates == 1, "one estimate");
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        CHECK(fabs((double)result.reverbTime[b] / 0.99 - 1.0) < 0.03, "the office's time");
    }
    long before = s_enqueued;
    CHECK(maudSpatializer_SimulateReverb(tasked, &s_listener) == maud_success,
          "an estimate on tasks");
    CHECK(s_enqueued == before + 1, "through the task hooks");
    CHECK(maudSpatializer_SimulateDirect(tasked, &s_listener) == maud_success, "a direct step");
    maudReverbResult viaTasks;
    CHECK(maudSpatializer_LatchResults(tasked) == 1 &&
              maudSpatializer_GetReverbResult(tasked, &viaTasks) == maud_success,
          "latched");
    bool same = true;
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        same = same && viaTasks.reverbTime[b] == result.reverbTime[b];
    }
    CHECK(same, "the same times however the tasks run");
    // Outside the closed office, rays strike its outer walls and leave:
    // hardly any decay.
    maudPose outside = {{20.0f, 2.0f, 1.5f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSpatializer_SimulateReverb(plain, &outside) == maud_success &&
              maudSpatializer_SimulateDirect(plain, &s_listener) == maud_success &&
              maudSpatializer_LatchResults(plain) == 3 &&
              maudSpatializer_GetReverbResult(plain, &result) == maud_success,
          "an estimate outside");
    printf("outside: %.3f s\n", (double)result.reverbTime[1]);
    CHECK(result.reverbTime[1] < 0.2f, "outside the room, no reverberation");
    maudDestroySpatializer(plain);
    maudDestroySpatializer(tasked);
    maudDestroyScene(scene);
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
    maudScene* scene = Office();
    maudSpatializer* s = nullptr;
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = Block;
    def.closestHit = maudFindClosestHit;
    def.rayContext = scene;
    def.reverbRays = 256;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudAcousticMaterial wall = {{0.1f, 0.1f, 0.1f}, 0.5f, {0, 0, 0}};
    CHECK(maudSpatializer_SetMaterials(s, &wall, 1) == maud_success, "materials");
    maudReverbResult result;
    CHECK(maudSpatializer_SimulateReverb(s, &s_listener) == maud_success &&
              maudSpatializer_SimulateDirect(s, &s_listener) == maud_success &&
              maudSpatializer_LatchResults(s) == 1 &&
              maudSpatializer_GetReverbResult(s, &result) == maud_success,
          "an estimate");
    CHECK(result.reverbTime[1] == 0.1f, "every path blocked: nothing lit");
    maudDestroySpatializer(s);
    maudDestroyScene(scene);
}

// The office is convex: from inside, nothing stands between a wall's
// point and the listener, so an any-hit query that clears every path is
// the scene's own.
static void TestNoAnyHit(void)
{
    maudScene* scene = Office();
    maudSpatializer* queried = Create(scene, false, maudFindAnyHit);
    maudSpatializer* clear = Create(scene, false, nullptr);
    maudReverbResult a;
    maudReverbResult b;
    CHECK(maudSpatializer_SimulateReverb(queried, &s_listener) == maud_success &&
              maudSpatializer_SimulateDirect(queried, &s_listener) == maud_success &&
              maudSpatializer_LatchResults(queried) == 1 &&
              maudSpatializer_GetReverbResult(queried, &a) == maud_success,
          "an estimate with the scene's any-hit query");
    CHECK(maudSpatializer_SimulateReverb(clear, &s_listener) == maud_success &&
              maudSpatializer_SimulateDirect(clear, &s_listener) == maud_success &&
              maudSpatializer_LatchResults(clear) == 1 &&
              maudSpatializer_GetReverbResult(clear, &b) == maud_success,
          "an estimate without one");
    CHECK(memcmp(a.reverbTime, b.reverbTime, sizeof(a.reverbTime)) == 0 &&
              memcmp(a.level, b.level, sizeof(a.level)) == 0 && b.reverbTime[1] > 0.5f,
          "every path clear: the scene's times and levels");
    maudDestroySpatializer(queried);
    maudDestroySpatializer(clear);
    maudDestroyScene(scene);
}

static void TestMisuse(void)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    CHECK(def.reverbRays == 2048 && def.airAbsorption[2] > def.airAbsorption[0] &&
              def.airAbsorption[0] > 0.0f,
          "the default: 2048 rays through ordinary air");
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "no hooks");
    CHECK(maudSpatializer_SimulateReverb(s, &s_listener) == maud_success, "no geometry");
    CHECK(maudSpatializer_SimulateDirect(s, &s_listener) == maud_success, "a direct step");
    maudReverbResult result;
    CHECK(maudSpatializer_LatchResults(s) == 1 &&
              maudSpatializer_GetReverbResult(s, &result) == maud_success &&
              result.estimates == 1 && result.reverbTime[0] == 0.1f && result.reverbTime[2] == 0.1f,
          "no geometry: 0.1 s");
    maudPose bad = s_listener;
    bad.position.x = NAN;
    CHECK(maudSpatializer_SimulateReverb(s, &bad) == maud_errorInvalid, "a listener not finite");
    CHECK(maudSpatializer_SimulateReverb(nullptr, &s_listener) == maud_errorInvalid, "NULL");
    CHECK(maudSpatializer_GetReverbResult(s, nullptr) == maud_errorInvalid, "NULL out");
    maudDestroySpatializer(s);
    def.reverbRays = 0;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "no estimates");
    CHECK(maudSpatializer_SimulateReverb(s, &s_listener) == maud_errorUnsupported,
          "estimates refused");
    maudDestroySpatializer(s);
    maudSpatializer* none = nullptr;
    def.reverbRays = 63;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorLimit && none == nullptr, "63 rays");
    def.reverbRays = 16384;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "16,384 rays, the most");
    maudDestroySpatializer(s);
    def.reverbRays = 16385;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorLimit, "16,385 rays");
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
    TestNoAnyHit();
    TestMisuse();
    return s_failures == 0 ? 0 : 1;
}
