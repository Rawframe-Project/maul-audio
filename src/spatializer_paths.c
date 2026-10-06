// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A spatializer's probe sets and pathing (spatializer_state.h): sets
// are made, read and destroyed here, one is chosen for pathing, and the
// direct step hands its occluded sources to the pathing.

#include "spatializer_state.h"

#include <string.h>

#define PROBE_SET_DEF_COOKIE 0x6D617062u

maudProbeQueries maudSpatializerQueries(const maudSpatializer* s)
{
    return (maudProbeQueries){
        .anyHit = s->anyHit,
        .closestHit = s->closestHit,
        .rayContext = s->rayContext,
        .enqueueTask = s->enqueueTask,
        .finishTask = s->finishTask,
        .userTaskContext = s->userTaskContext,
        .allocator = &s->allocator,
        .maxProbes = s->maxProbes,
        .maxPairs = s->maxProbePairs,
    };
}

void maudPathSources(maudSpatializer* s, const maudPose* listener, Entry* entries)
{
    const maudProbeGraph* graph = nullptr;
    if (s->pathing == nullptr || s->pathSet.index1 == 0 ||
        maudFindProbeSet(s->probeSets, s->pathSet, &graph) != maud_success)
    {
        return;
    }
    maudPathJob* jobs = maudPathJobs(s->pathing);
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->capacity && count < s->maxPaths; ++i)
    {
        const Slot* slot = &s->slots[i];
        if (slot->live && slot->pathing && entries[i].result.occlusion > 0.0f)
        {
            jobs[count++] = (maudPathJob){&slot->pose, &slot->directivity, &entries[i].result};
        }
    }
    if (count > 0)
    {
        maudProbeQueries queries = maudSpatializerQueries(s);
        maudRunPathing(s->pathing, graph, &queries, listener, count);
    }
}

maudProbeSetDef maudDefaultProbeSetDef(void)
{
    return (maudProbeSetDef){
        .cookie = PROBE_SET_DEF_COOKIE,
        .points = nullptr,
        .pointCount = 0,
        .boxMin = {0.0f, 0.0f, 0.0f},
        .boxMax = {0.0f, 0.0f, 0.0f},
        .spacing = 2.0f,
        .height = 1.5f,
        .range = 5.0f,
    };
}

maudResult maudCreateProbeSet(maudSpatializer* spatializer, const maudProbeSetDef* def,
                              maudProbeSetId* setOut)
{
    if (setOut != nullptr)
    {
        *setOut = (maudProbeSetId){0, 0};
    }
    if (spatializer == nullptr || def == nullptr || setOut == nullptr ||
        def->cookie != PROBE_SET_DEF_COOKIE || !maudProbeSetDefValid(def))
    {
        return maud_errorInvalid;
    }
    maudSpatializer* s = spatializer;
    if (s->probeSets == nullptr)
    {
        return maud_errorCapacity;
    }
    maudProbeQueries queries = maudSpatializerQueries(s);
    return maudAddProbeSet(s->probeSets, &queries, def, setOut);
}

maudResult maudDestroyProbeSet(maudSpatializer* spatializer, maudProbeSetId set)
{
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    maudResult result = maudRemoveProbeSet(spatializer->probeSets, set);
    if (result == maud_success && set.index1 == spatializer->pathSet.index1 &&
        set.generation == spatializer->pathSet.generation)
    {
        spatializer->pathSet = (maudProbeSetId){0, 0};
    }
    return result;
}

maudResult maudSetPathing(maudSpatializer* spatializer, maudProbeSetId set)
{
    if (spatializer == nullptr)
    {
        return maud_errorInvalid;
    }
    if (set.index1 != 0 || set.generation != 0)
    {
        const maudProbeGraph* graph = nullptr;
        maudResult result = maudFindProbeSet(spatializer->probeSets, set, &graph);
        if (result != maud_success)
        {
            return result;
        }
    }
    spatializer->pathSet = set;
    return maud_success;
}

maudResult maudGetProbeSet(const maudSpatializer* spatializer, maudProbeSetId set,
                           maudProbeSetInfo* infoOut, uint32_t first, uint32_t count,
                           maudVector3* points)
{
    if (spatializer == nullptr || infoOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudProbeGraph* graph = nullptr;
    maudResult result = maudFindProbeSet(spatializer->probeSets, set, &graph);
    if (result != maud_success)
    {
        return result;
    }
    if (points != nullptr && (first > graph->count || count > graph->count - first))
    {
        return maud_errorInvalid;
    }
    *infoOut = (maudProbeSetInfo){graph->count, graph->links};
    if (points != nullptr && count > 0)
    {
        memcpy(points, graph->points + first, (size_t)count * sizeof(maudVector3));
    }
    return maud_success;
}
