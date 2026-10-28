// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every refusal spatializer.h documents, each made on its own: a def
// with one field just past its range (and nothing else wrong) is
// maud_errorLimit past a published bound and maud_errorInvalid
// otherwise, with no spatializer made, and the field at its
// bound is taken where that costs little; each pointer a function needs
// refused when NULL; ids and poses out of range; a source def's
// volumetric occlusion out of range.

#include "test_harness.h"

#include "maul-audio/spatializer.h"

#include <math.h>
#include <stdlib.h>

static void NoHit(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    (void)rays;
    (void)context;
    for (uint32_t i = 0; i < count; ++i)
    {
        occluded[i] = 0;
    }
}

static void Miss(const maudRay* rays, uint32_t count, maudRayHit* hits, void* context)
{
    (void)rays;
    (void)context;
    for (uint32_t i = 0; i < count; ++i)
    {
        hits[i] = (maudRayHit){.distance = INFINITY};
    }
}

static void* Enqueue(maudTaskFn* task, uint32_t itemCount, uint32_t minRange, void* taskContext,
                     void* userContext)
{
    (void)minRange;
    task(0, itemCount, taskContext);
    return userContext;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
}

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    return malloc(size);
}

static maudSpatializerDef Valid(void)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = NoHit;
    def.closestHit = Miss;
    def.reverbRays = 64;
    return def;
}

// Puts case k's field just past its range; false past the last case.
static bool Spoil(maudSpatializerDef* d, int k, const char** what, maudResult* want)
{
    *want = maud_errorInvalid;
    switch (k)
    {
    case 0:
        d->cookie = 0;
        *what = "no cookie";
        return true;
    case 1:
        d->sourceCapacity = 0;
        *what = "no sources";
        return true;
    case 2:
        d->sourceCapacity = 65537;
        *what = "65,537 sources";
        *want = maud_errorLimit;
        return true;
    case 3:
        d->maxOcclusionSamples = 0;
        *what = "no occlusion samples";
        return true;
    case 4:
        d->maxOcclusionSamples = 1025;
        *what = "1,025 occlusion samples";
        *want = maud_errorLimit;
        return true;
    case 5:
        d->maxSurfaces = 0;
        *what = "no surfaces";
        return true;
    case 6:
        d->maxSurfaces = 17;
        *what = "17 surfaces";
        *want = maud_errorLimit;
        return true;
    case 7:
        d->materialCapacity = 0;
        *what = "no materials";
        return true;
    case 8:
        d->materialCapacity = 65537;
        *what = "65,537 materials";
        *want = maud_errorLimit;
        return true;
    case 9:
        d->enqueueTask = Enqueue;
        *what = "an enqueue hook alone";
        return true;
    case 10:
        d->finishTask = Finish;
        *what = "a finish hook alone";
        return true;
    case 11:
        d->airAbsorption[2] = -0.001f;
        *what = "negative air";
        return true;
    case 12:
        d->airAbsorption[0] = NAN;
        *what = "air not a number";
        return true;
    case 13:
        d->reverbRays = 63;
        *what = "63 reverberation rays";
        *want = maud_errorLimit;
        return true;
    case 14:
        d->reverbRays = 16385;
        *what = "16,385 reverberation rays";
        *want = maud_errorLimit;
        return true;
    case 15:
        d->reflectionOrder = 4;
        d->reflectionDuration = 0.1f;
        *what = "reflections of order 4";
        *want = maud_errorLimit;
        return true;
    case 16:
        d->reflectionOrder = 1;
        d->reflectionDuration = 0.1f;
        d->reverbRays = 0;
        *what = "reflections without reverberation rays";
        return true;
    case 17:
        d->reflectionOrder = 1;
        d->reflectionDuration = 0.049f;
        *what = "reflections over 0.049 s";
        return true;
    case 18:
        d->reflectionOrder = 1;
        d->reflectionDuration = 4.01f;
        *what = "reflections over 4.01 s";
        return true;
    case 19:
        d->reflectionOrder = 1;
        d->reflectionDuration = 0.1f;
        d->reflectionRate = 44099;
        *what = "reflections at 44,099 Hz";
        return true;
    case 20:
        d->reflectionOrder = 1;
        d->reflectionDuration = 0.1f;
        d->reflectionRate = 384001;
        *what = "reflections at 384,001 Hz";
        return true;
    case 21:
        d->probeSetCapacity = 65;
        *what = "65 probe sets";
        *want = maud_errorLimit;
        return true;
    case 22:
        d->maxProbes = 0;
        *what = "no probes";
        return true;
    case 23:
        d->maxProbes = 65537;
        *what = "65,537 probes";
        *want = maud_errorLimit;
        return true;
    case 24:
        d->maxProbePairs = 0;
        *what = "no probe pairs";
        return true;
    case 25:
        d->maxProbePairs = 16777217;
        *what = "16,777,217 probe pairs";
        *want = maud_errorLimit;
        return true;
    case 26:
        d->maxPaths = 4097;
        *what = "4,097 paths";
        *want = maud_errorLimit;
        return true;
    case 27:
        d->allocator = (maudAllocator){Alloc, nullptr, nullptr};
        *what = "an allocator without free";
        return true;
    default:
        return false;
    }
}

// Puts case k's field at its bound, which is taken; false past the last.
static bool Bound(maudSpatializerDef* d, int k, const char** what)
{
    switch (k)
    {
    case 0:
        d->maxOcclusionSamples = 1024;
        *what = "1,024 occlusion samples";
        return true;
    case 1:
        d->maxSurfaces = 16;
        *what = "16 surfaces";
        return true;
    case 2:
        d->reverbRays = 0;
        *what = "no reverberation";
        return true;
    case 3:
        d->reflectionOrder = 3;
        d->reflectionDuration = 0.05f;
        d->reflectionRate = 44100;
        *what = "order 3 over 0.05 s at 44.1 kHz";
        return true;
    case 4:
        d->probeSetCapacity = 64;
        *what = "64 probe sets";
        return true;
    case 5:
        d->enqueueTask = Enqueue;
        d->finishTask = Finish;
        *what = "both hooks";
        return true;
    default:
        return false;
    }
}

static void TestDefs(void)
{
    maudSpatializer* s = (maudSpatializer*)&s;
    maudSpatializerDef def = Valid();
    CHECK(maudCreateSpatializer(nullptr, &s) == maud_errorInvalid && s == nullptr, "no def");
    CHECK(maudCreateSpatializer(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "the valid def");
    maudDestroySpatializer(s);
    const char* what = "";
    for (int k = 0;; ++k)
    {
        def = Valid();
        maudResult want = maud_errorInvalid;
        if (!Spoil(&def, k, &what, &want))
        {
            break;
        }
        s = (maudSpatializer*)&s;
        bool refused = maudCreateSpatializer(&def, &s) == want && s == nullptr;
        if (!refused)
        {
            printf("not refused: %s\n", what);
            maudDestroySpatializer(s);
        }
        CHECK(refused, "a field past its range refused");
    }
    for (int k = 0;; ++k)
    {
        def = Valid();
        if (!Bound(&def, k, &what))
        {
            break;
        }
        bool taken = maudCreateSpatializer(&def, &s) == maud_success;
        if (!taken)
        {
            printf("not taken: %s\n", what);
        }
        CHECK(taken, "a field at its bound taken");
        maudDestroySpatializer(s);
    }
}

static void TestCalls(void)
{
    maudSpatializerDef def = Valid();
    def.maxOcclusionSamples = 8;
    def.reflectionOrder = 1;
    def.reflectionDuration = 0.05f;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudSourceDef sourceDef = maudDefaultSourceDef();
    maudSourceId id = {7, 7};
    CHECK(maudCreateSource(nullptr, &sourceDef, &id) == maud_errorInvalid && id.index1 == 0,
          "a source without a spatializer");
    CHECK(maudCreateSource(s, nullptr, &id) == maud_errorInvalid, "without a def");
    CHECK(maudCreateSource(s, &sourceDef, nullptr) == maud_errorInvalid, "nowhere to put it");
    sourceDef.cookie = 0;
    CHECK(maudCreateSource(s, &sourceDef, &id) == maud_errorInvalid, "a def without its cookie");
    sourceDef = maudDefaultSourceDef();
    sourceDef.occlusion = maud_occlusionVolumetric + 1;
    CHECK(maudCreateSource(s, &sourceDef, &id) == maud_errorInvalid, "an unknown method");
    sourceDef.occlusion = maud_occlusionVolumetric;
    sourceDef.occlusionRadius = 0.0f;
    sourceDef.occlusionSamples = 8;
    CHECK(maudCreateSource(s, &sourceDef, &id) == maud_errorInvalid, "a radius of 0");
    sourceDef.occlusionRadius = INFINITY;
    CHECK(maudCreateSource(s, &sourceDef, &id) == maud_errorInvalid, "an infinite radius");
    sourceDef.occlusionRadius = 0.5f;
    sourceDef.occlusionSamples = 0;
    CHECK(maudCreateSource(s, &sourceDef, &id) == maud_errorInvalid, "no samples");
    sourceDef.occlusionSamples = 9;
    CHECK(maudCreateSource(s, &sourceDef, &id) == maud_errorInvalid,
          "more samples than the spatializer takes");
    sourceDef.occlusionSamples = 8;
    CHECK(maudCreateSource(s, &sourceDef, &id) == maud_success, "its most samples taken");
    maudPose pose = {{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudPose bad = {{NAN, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudSourceId zero = {0, 1};
    maudSourceId past = {def.sourceCapacity + 1, 1};
    maudSourceId unborn = {id.index1, 0};
    CHECK(maudSetSourcePose(nullptr, id, &pose) == maud_errorInvalid &&
              maudSetSourcePose(s, zero, &pose) == maud_errorInvalid &&
              maudSetSourcePose(s, past, &pose) == maud_errorInvalid &&
              maudSetSourcePose(s, unborn, &pose) == maud_errorInvalid,
          "a pose for no spatializer, index 0, past the capacity or generation 0");
    CHECK(maudSetSourcePose(s, id, nullptr) == maud_errorInvalid &&
              maudSetSourcePose(s, id, &bad) == maud_errorInvalid,
          "no pose, or one not finite");
    CHECK(maudDestroySource(nullptr, id) == maud_errorInvalid &&
              maudDestroySource(s, zero) == maud_errorInvalid,
          "destroying no source");
    maudAcousticMaterial wall = {{0.1f, 0.1f, 0.1f}, 0.3f, {0.0f, 0.0f, 0.0f}};
    CHECK(maudSetMaterials(nullptr, &wall, 1) == maud_errorInvalid &&
              maudSetMaterials(s, nullptr, 1) == maud_errorInvalid &&
              maudSetMaterials(s, nullptr, 0) == maud_success,
          "materials: none to set them on, none given for a count, none for none");
    CHECK(maudSimulateDirect(nullptr, &pose) == maud_errorInvalid &&
              maudSimulateDirect(s, nullptr) == maud_errorInvalid &&
              maudSimulateDirect(s, &bad) == maud_errorInvalid,
          "a direct step without a spatializer or a sound listener");
    CHECK(maudSimulateReverb(s, nullptr) == maud_errorInvalid, "an estimate without a listener");
    maudDirectResult direct;
    maudReverbResult reverb;
    CHECK(maudGetDirectResult(nullptr, id, &direct) == maud_errorInvalid &&
              maudGetDirectResult(s, id, nullptr) == maud_errorInvalid &&
              maudGetDirectResult(s, zero, &direct) == maud_errorInvalid &&
              maudGetDirectResult(s, past, &direct) == maud_errorInvalid &&
              maudGetDirectResult(s, unborn, &direct) == maud_errorInvalid,
          "a direct result for nothing, or for no source");
    CHECK(maudGetReverbResult(nullptr, &reverb) == maud_errorInvalid &&
              maudGetReverbResult(s, nullptr) == maud_errorInvalid,
          "a reverb result for nothing or nowhere");
    maudQuaternion facing = {0.0f, 0.0f, 0.0f, 1.0f};
    maudQuaternion none = {0.0f, 0.0f, 0.0f, 0.0f};
    maudQuaternion endless = {INFINITY, 0.0f, 0.0f, 1.0f};
    float send[16] = {0};
    float bed0[16];
    float bed1[16];
    float bed2[16];
    float bed3[16];
    float* bed[4] = {bed0, bed1, bed2, bed3};
    CHECK(maudRenderReflections(nullptr, &facing, send, bed, 16) == maud_errorInvalid &&
              maudRenderReflections(s, nullptr, send, bed, 16) == maud_errorInvalid &&
              maudRenderReflections(s, &none, send, bed, 16) == maud_errorInvalid &&
              maudRenderReflections(s, &endless, send, bed, 16) == maud_errorInvalid,
          "reflections without a spatializer or an orientation that turns");
    CHECK(maudRenderReflections(s, &facing, nullptr, bed, 16) == maud_errorInvalid &&
              maudRenderReflections(s, &facing, send, nullptr, 16) == maud_errorInvalid &&
              maudRenderReflections(s, &facing, nullptr, nullptr, 0) == maud_success,
          "frames without a send or a bed refused, none without either taken");
    maudDestroySpatializer(s);
}

int main(void)
{
    TestDefs();
    TestCalls();
    return s_failures == 0 ? 0 : 1;
}
