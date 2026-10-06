// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spatializers. Sources live in slots named by index and generation; a
// destroyed slot's generation moves on, so its old ids are stale at
// once, and the slot may be reused at once: every published entry
// carries the generation it was computed for, and the rendering side
// holds nothing of a source beyond that copy. Steps are published
// through three buffers (one being written, one latched by the
// rendering side, one waiting): `shared` holds the waiting buffer's
// index and whether it is newer than the latched one, and each side
// swaps its own buffer with it atomically.

#include "maul-audio/spatializer.h"

#include "allocator.h"
#include "direct_step.h"
#include "occlusion.h"
#include "transmission.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#define SPATIALIZER_DEF_COOKIE 0x6D617370u
#define SOURCE_DEF_COOKIE      0x6D61736Fu
#define MAX_SOURCES            65536u
#define FRESH                  4u
#define MAX_SAMPLES            1024u
#define MAX_SURFACES           16u
#define MAX_MATERIALS          65536u
// Rays per round of queries, and per query.
#define ROUND 4096u
#define BATCH 64u

typedef struct Entry
{
    // The source's generation when the step ran; 0 for no source.
    uint32_t generation;
    maudDirectResult result;
} Entry;

typedef struct Buffer
{
    uint64_t step;
    Entry* entries;
} Buffer;

typedef struct Slot
{
    uint32_t generation;
    bool live;
    maudPose pose;
    maudDirectivityPattern directivity;
    maudOcclusionMethod occlusion;
    float radius;
    uint32_t samples;
    bool transmission;
} Slot;

struct maudSpatializer
{
    maudAllocator allocator;
    uint32_t capacity;
    Slot* slots;
    // Free slots' indices, the next one taken from the end.
    uint32_t* free;
    uint32_t freeCount;
    uint32_t maxSamples;
    maudAnyHitFn* anyHit;
    maudClosestHitFn* closestHit;
    void* rayContext;
    uint32_t maxSurfaces;
    uint32_t materialCapacity;
    uint32_t materialCount;
    maudAcousticMaterial* materials;
    // Whether the round's rays go to the closest-hit query.
    bool closest;
    maudRayHit* hits;
    // The sources walking a transmission path, and how far each got.
    uint32_t* walkers;
    float* walked;
    maudEnqueueTaskFn* enqueueTask;
    maudFinishTaskFn* finishTask;
    void* userTaskContext;
    // The unit ball's points, a round's rays and answers, and where each
    // source's rays start in the round.
    maudVector3* points;
    maudRay* rays;
    uint8_t* occluded;
    uint32_t* offsets;
    uint32_t roundRays;
    Buffer buffers[3];
    // The simulation side's buffer and step count.
    uint32_t back;
    uint64_t steps;
    // The rendering side's latched buffer.
    uint32_t front;
    _Atomic uint32_t shared;
};

maudSpatializerDef maudDefaultSpatializerDef(void)
{
    return (maudSpatializerDef){
        .cookie = SPATIALIZER_DEF_COOKIE,
        .sourceCapacity = 256,
        .maxOcclusionSamples = 64,
        .anyHit = nullptr,
        .closestHit = nullptr,
        .rayContext = nullptr,
        .maxSurfaces = 4,
        .materialCapacity = 64,
        .enqueueTask = nullptr,
        .finishTask = nullptr,
        .userTaskContext = nullptr,
        .allocator = {nullptr, nullptr, nullptr},
    };
}

maudSourceDef maudDefaultSourceDef(void)
{
    return (maudSourceDef){
        .cookie = SOURCE_DEF_COOKIE,
        .directivity = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
        .occlusion = maud_occlusionRay,
        .occlusionRadius = 1.0f,
        .occlusionSamples = 32,
        .transmission = true,
    };
}

static size_t SlotBytes(uint32_t capacity)
{
    return (size_t)capacity * sizeof(Slot);
}

static size_t FreeBytes(uint32_t capacity)
{
    return (size_t)capacity * sizeof(uint32_t);
}

static size_t EntryBytes(uint32_t capacity)
{
    return (size_t)capacity * sizeof(Entry);
}

static void Free(maudAllocator* allocator, void* memory, size_t bytes, size_t alignment)
{
    if (memory != nullptr)
    {
        maudRelease(allocator, memory, bytes, alignment);
    }
}

static void Release(maudSpatializer* s)
{
    maudAllocator allocator = s->allocator;
    Free(&allocator, s->points, (size_t)s->maxSamples * sizeof(maudVector3), alignof(maudVector3));
    Free(&allocator, s->rays, ROUND * sizeof(maudRay), alignof(maudRay));
    Free(&allocator, s->occluded, ROUND, 1);
    Free(&allocator, s->offsets, FreeBytes(s->capacity), alignof(uint32_t));
    Free(&allocator, s->materials, (size_t)s->materialCapacity * sizeof(maudAcousticMaterial),
         alignof(maudAcousticMaterial));
    Free(&allocator, s->hits, ROUND * sizeof(maudRayHit), alignof(maudRayHit));
    Free(&allocator, s->walkers, FreeBytes(s->capacity), alignof(uint32_t));
    Free(&allocator, s->walked, (size_t)s->capacity * sizeof(float), alignof(float));
    for (int b = 0; b < 3; ++b)
    {
        if (s->buffers[b].entries != nullptr)
        {
            maudRelease(&allocator, s->buffers[b].entries, EntryBytes(s->capacity), alignof(Entry));
        }
    }
    if (s->free != nullptr)
    {
        maudRelease(&allocator, s->free, FreeBytes(s->capacity), alignof(uint32_t));
    }
    if (s->slots != nullptr)
    {
        maudRelease(&allocator, s->slots, SlotBytes(s->capacity), alignof(Slot));
    }
    maudRelease(&allocator, s, sizeof(maudSpatializer), alignof(maudSpatializer));
}

maudResult maudCreateSpatializer(const maudSpatializerDef* def, maudSpatializer** spatializerOut)
{
    if (spatializerOut != nullptr)
    {
        *spatializerOut = nullptr;
    }
    if (def == nullptr || spatializerOut == nullptr || def->cookie != SPATIALIZER_DEF_COOKIE ||
        def->sourceCapacity == 0 || def->sourceCapacity > MAX_SOURCES ||
        def->maxOcclusionSamples == 0 || def->maxOcclusionSamples > MAX_SAMPLES ||
        def->maxSurfaces == 0 || def->maxSurfaces > MAX_SURFACES || def->materialCapacity == 0 ||
        def->materialCapacity > MAX_MATERIALS ||
        (def->enqueueTask == nullptr) != (def->finishTask == nullptr) ||
        !maudIsAllocatorValid(&def->allocator))
    {
        return maud_errorInvalid;
    }
    maudSpatializer* s =
        maudAllocate(&def->allocator, sizeof(maudSpatializer), alignof(maudSpatializer));
    if (s == nullptr)
    {
        return maud_errorCapacity;
    }
    *s = (maudSpatializer){
        .allocator = def->allocator,
        .capacity = def->sourceCapacity,
        .maxSamples = def->maxOcclusionSamples,
        .anyHit = def->anyHit,
        .closestHit = def->closestHit,
        .rayContext = def->rayContext,
        .maxSurfaces = def->maxSurfaces,
        .materialCapacity = def->materialCapacity,
        .enqueueTask = def->enqueueTask,
        .finishTask = def->finishTask,
        .userTaskContext = def->userTaskContext,
    };
    s->points = maudAllocate(&def->allocator, (size_t)s->maxSamples * sizeof(maudVector3),
                             alignof(maudVector3));
    s->rays = maudAllocate(&def->allocator, ROUND * sizeof(maudRay), alignof(maudRay));
    s->occluded = maudAllocate(&def->allocator, ROUND, 1);
    s->offsets = maudAllocate(&def->allocator, FreeBytes(s->capacity), alignof(uint32_t));
    s->materials =
        maudAllocate(&def->allocator, (size_t)s->materialCapacity * sizeof(maudAcousticMaterial),
                     alignof(maudAcousticMaterial));
    s->hits = maudAllocate(&def->allocator, ROUND * sizeof(maudRayHit), alignof(maudRayHit));
    s->walkers = maudAllocate(&def->allocator, FreeBytes(s->capacity), alignof(uint32_t));
    s->walked = maudAllocate(&def->allocator, (size_t)s->capacity * sizeof(float), alignof(float));
    s->slots = maudAllocate(&def->allocator, SlotBytes(s->capacity), alignof(Slot));
    s->free = maudAllocate(&def->allocator, FreeBytes(s->capacity), alignof(uint32_t));
    bool all = s->slots != nullptr && s->free != nullptr && s->points != nullptr &&
               s->rays != nullptr && s->occluded != nullptr && s->offsets != nullptr &&
               s->materials != nullptr && s->hits != nullptr && s->walkers != nullptr &&
               s->walked != nullptr;
    for (int b = 0; b < 3; ++b)
    {
        s->buffers[b].entries =
            maudAllocate(&def->allocator, EntryBytes(s->capacity), alignof(Entry));
        all = all && s->buffers[b].entries != nullptr;
    }
    if (!all)
    {
        Release(s);
        return maud_errorCapacity;
    }
    for (uint32_t i = 0; i < s->capacity; ++i)
    {
        s->slots[i] = (Slot){.generation = 1};
        // Taken from the end: the lowest index first.
        s->free[i] = s->capacity - 1 - i;
    }
    s->freeCount = s->capacity;
    maudBallPoints(s->maxSamples, s->points);
    for (int b = 0; b < 3; ++b)
    {
        memset(s->buffers[b].entries, 0, EntryBytes(s->capacity));
    }
    s->back = 0;
    s->front = 1;
    atomic_init(&s->shared, 2u);
    *spatializerOut = s;
    return maud_success;
}

void maudDestroySpatializer(maudSpatializer* spatializer)
{
    if (spatializer != nullptr)
    {
        Release(spatializer);
    }
}

static bool PatternValid(const maudDirectivityPattern* pattern)
{
    float out[MAUD_DIRECT_BANDS];
    return maudGetDirectivity(pattern, (maudVector3){0.0f, 0.0f, -1.0f}, out) == maud_success;
}

maudResult maudCreateSource(maudSpatializer* spatializer, const maudSourceDef* def,
                            maudSourceId* sourceOut)
{
    if (sourceOut != nullptr)
    {
        *sourceOut = (maudSourceId){0, 0};
    }
    if (spatializer == nullptr || def == nullptr || sourceOut == nullptr ||
        def->cookie != SOURCE_DEF_COOKIE || !PatternValid(&def->directivity) ||
        def->occlusion > maud_occlusionVolumetric ||
        (def->occlusion == maud_occlusionVolumetric &&
         (!(def->occlusionRadius > 0.0f) || !isfinite(def->occlusionRadius) ||
          def->occlusionSamples == 0 || def->occlusionSamples > spatializer->maxSamples)))
    {
        return maud_errorInvalid;
    }
    if (spatializer->freeCount == 0)
    {
        return maud_errorCapacity;
    }
    uint32_t index = spatializer->free[--spatializer->freeCount];
    Slot* slot = &spatializer->slots[index];
    slot->live = true;
    slot->pose = (maudPose){{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    slot->directivity = def->directivity;
    slot->occlusion = def->occlusion;
    slot->radius = def->occlusionRadius;
    slot->samples = def->occlusionSamples;
    slot->transmission = def->transmission;
    *sourceOut = (maudSourceId){index + 1, slot->generation};
    return maud_success;
}

// The live slot an id names, or the reason there is none.
static maudResult Find(maudSpatializer* spatializer, maudSourceId source, Slot** slotOut)
{
    if (spatializer == nullptr || source.index1 == 0 || source.index1 > spatializer->capacity ||
        source.generation == 0)
    {
        return maud_errorInvalid;
    }
    Slot* slot = &spatializer->slots[source.index1 - 1];
    if (!slot->live || slot->generation != source.generation)
    {
        return maud_errorStale;
    }
    *slotOut = slot;
    return maud_success;
}

maudResult maudDestroySource(maudSpatializer* spatializer, maudSourceId source)
{
    Slot* slot = nullptr;
    maudResult result = Find(spatializer, source, &slot);
    if (result != maud_success)
    {
        return result;
    }
    slot->live = false;
    slot->generation = slot->generation == UINT32_MAX ? 1 : slot->generation + 1;
    spatializer->free[spatializer->freeCount++] = source.index1 - 1;
    return maud_success;
}

maudResult maudSetSourcePose(maudSpatializer* spatializer, maudSourceId source,
                             const maudPose* pose)
{
    Slot* slot = nullptr;
    maudResult result = Find(spatializer, source, &slot);
    if (result != maud_success)
    {
        return result;
    }
    if (pose == nullptr || !maudPoseValid(pose))
    {
        return maud_errorInvalid;
    }
    slot->pose = *pose;
    return maud_success;
}

// Queries batches [start, end) of the round's rays.
static void QueryBatches(uint32_t start, uint32_t end, void* context)
{
    maudSpatializer* s = context;
    for (uint32_t b = start; b < end; ++b)
    {
        uint32_t first = b * BATCH;
        uint32_t count = s->roundRays - first < BATCH ? s->roundRays - first : BATCH;
        if (s->closest)
        {
            s->closestHit(s->rays + first, count, s->hits + first, s->rayContext);
        }
        else
        {
            s->anyHit(s->rays + first, count, s->occluded + first, s->rayContext);
        }
    }
}

static void Query(maudSpatializer* s)
{
    uint32_t batches = (s->roundRays + BATCH - 1) / BATCH;
    if (batches == 0)
    {
        return;
    }
    if (s->enqueueTask != nullptr)
    {
        void* task = s->enqueueTask(QueryBatches, batches, 1, s, s->userTaskContext);
        s->finishTask(task, s->userTaskContext);
    }
    else
    {
        QueryBatches(0, batches, s);
    }
}

static uint32_t RaysOf(const maudSpatializer* s, const Slot* slot)
{
    return slot->live && s->anyHit != nullptr
               ? maudOcclusionRayCount(slot->occlusion, slot->samples)
               : 0;
}

// The occlusion of every source: rounds of whole sources' rays, in slot
// order, each queried and then reduced into the buffer's entries.
static void Occlude(maudSpatializer* s, const maudPose* listener, Entry* entries)
{
    uint32_t next = 0;
    while (next < s->capacity)
    {
        uint32_t first = next;
        s->roundRays = 0;
        s->closest = false;
        for (; next < s->capacity; ++next)
        {
            const Slot* slot = &s->slots[next];
            uint32_t count = RaysOf(s, slot);
            if (s->roundRays + count > ROUND)
            {
                break;
            }
            s->offsets[next] = s->roundRays;
            if (count > 0)
            {
                maudOcclusionRays(listener, &slot->pose, slot->occlusion, slot->radius,
                                  slot->samples, s->points, s->rays + s->roundRays);
            }
            s->roundRays += count;
        }
        Query(s);
        for (uint32_t i = first; i < next; ++i)
        {
            const Slot* slot = &s->slots[i];
            if (RaysOf(s, slot) > 0)
            {
                entries[i].result.occlusion =
                    maudOcclusionOf(slot->occlusion, slot->samples, s->occluded + s->offsets[i]);
            }
        }
    }
}

// One surface of every walking source's path, in rounds of ROUND
// sources; the walkers still going stay listed in order.
static void Cross(maudSpatializer* s, const maudPose* listener, Entry* entries, uint32_t* walking)
{
    uint32_t kept = 0;
    for (uint32_t first = 0; first < *walking; first += ROUND)
    {
        uint32_t count = *walking - first < ROUND ? *walking - first : ROUND;
        for (uint32_t k = 0; k < count; ++k)
        {
            uint32_t i = s->walkers[first + k];
            s->rays[k] = maudPathRay(listener, &s->slots[i].pose, s->walked[i]);
        }
        s->roundRays = count;
        s->closest = true;
        Query(s);
        for (uint32_t k = 0; k < count; ++k)
        {
            uint32_t i = s->walkers[first + k];
            maudDirectResult* r = &entries[i].result;
            if (maudCrossSurface(&s->hits[k], &s->rays[k], s->materials, s->materialCount,
                                 r->transmission, &s->walked[i]))
            {
                r->surfaces += 1;
                s->walkers[kept++] = i;
            }
        }
    }
    *walking = kept;
}

// The transmission of every occluded source: walked surface by surface
// where the source asks and the host answers closest hits, otherwise 0.
static void Transmit(maudSpatializer* s, const maudPose* listener, Entry* entries)
{
    uint32_t walking = 0;
    for (uint32_t i = 0; i < s->capacity; ++i)
    {
        const Slot* slot = &s->slots[i];
        maudDirectResult* r = &entries[i].result;
        if (!slot->live || r->occlusion == 0.0f)
        {
            continue;
        }
        if (slot->transmission && s->closestHit != nullptr)
        {
            s->walkers[walking++] = i;
            s->walked[i] = 0.0f;
            continue;
        }
        for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
        {
            r->transmission[b] = 0.0f;
        }
    }
    for (uint32_t surface = 0; surface < s->maxSurfaces && walking > 0; ++surface)
    {
        Cross(s, listener, entries, &walking);
    }
}

static bool Unit(float v)
{
    return v >= 0.0f && v <= 1.0f;
}

static bool MaterialValid(const maudAcousticMaterial* m)
{
    bool valid = Unit(m->scattering);
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        valid = valid && Unit(m->absorption[b]) && Unit(m->transmission[b]);
    }
    return valid;
}

maudResult maudSetMaterials(maudSpatializer* spatializer, const maudAcousticMaterial* materials,
                            uint32_t count)
{
    if (spatializer == nullptr || (count > 0 && materials == nullptr))
    {
        return maud_errorInvalid;
    }
    if (count > spatializer->materialCapacity)
    {
        return maud_errorCapacity;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        if (!MaterialValid(&materials[i]))
        {
            return maud_errorInvalid;
        }
    }
    if (count > 0)
    {
        memcpy(spatializer->materials, materials, (size_t)count * sizeof(maudAcousticMaterial));
    }
    spatializer->materialCount = count;
    return maud_success;
}

maudResult maudSimulateDirect(maudSpatializer* spatializer, const maudPose* listener)
{
    if (spatializer == nullptr || listener == nullptr || !maudPoseValid(listener))
    {
        return maud_errorInvalid;
    }
    Buffer* buffer = &spatializer->buffers[spatializer->back];
    for (uint32_t i = 0; i < spatializer->capacity; ++i)
    {
        const Slot* slot = &spatializer->slots[i];
        Entry* entry = &buffer->entries[i];
        if (!slot->live)
        {
            entry->generation = 0;
            continue;
        }
        entry->generation = slot->generation;
        maudDirectGeometry(listener, &slot->pose, &slot->directivity, &entry->result);
    }
    Occlude(spatializer, listener, buffer->entries);
    Transmit(spatializer, listener, buffer->entries);
    buffer->step = ++spatializer->steps;
    // Publish: the written buffer waits, marked newer; the one that was
    // waiting becomes the next to write.
    uint32_t old = atomic_exchange_explicit(&spatializer->shared, spatializer->back | FRESH,
                                            memory_order_acq_rel);
    spatializer->back = old & (FRESH - 1);
    return maud_success;
}

uint64_t maudLatchResults(maudSpatializer* spatializer)
{
    if (spatializer == nullptr)
    {
        return 0;
    }
    if ((atomic_load_explicit(&spatializer->shared, memory_order_relaxed) & FRESH) != 0)
    {
        uint32_t old = atomic_exchange_explicit(&spatializer->shared, spatializer->front,
                                                memory_order_acq_rel);
        spatializer->front = old & (FRESH - 1);
    }
    return spatializer->buffers[spatializer->front].step;
}

maudResult maudGetDirectResult(const maudSpatializer* spatializer, maudSourceId source,
                               maudDirectResult* resultOut)
{
    if (spatializer == nullptr || resultOut == nullptr || source.index1 == 0 ||
        source.index1 > spatializer->capacity || source.generation == 0)
    {
        return maud_errorInvalid;
    }
    const Buffer* buffer = &spatializer->buffers[spatializer->front];
    if (buffer->step == 0)
    {
        return maud_errorInvalid;
    }
    const Entry* entry = &buffer->entries[source.index1 - 1];
    if (entry->generation != source.generation)
    {
        return maud_errorStale;
    }
    *resultOut = entry->result;
    return maud_success;
}
