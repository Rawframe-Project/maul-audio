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

#include <stdatomic.h>
#include <string.h>

#define SPATIALIZER_DEF_COOKIE 0x6D617370u
#define SOURCE_DEF_COOKIE      0x6D61736Fu
#define MAX_SOURCES            65536u
#define FRESH                  4u

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
} Slot;

struct maudSpatializer
{
    maudAllocator allocator;
    uint32_t capacity;
    Slot* slots;
    // Free slots' indices, the next one taken from the end.
    uint32_t* free;
    uint32_t freeCount;
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
        .allocator = {nullptr, nullptr, nullptr},
    };
}

maudSourceDef maudDefaultSourceDef(void)
{
    return (maudSourceDef){
        .cookie = SOURCE_DEF_COOKIE,
        .directivity = {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
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

static void Release(maudSpatializer* s)
{
    maudAllocator allocator = s->allocator;
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
    *s = (maudSpatializer){.allocator = def->allocator, .capacity = def->sourceCapacity};
    s->slots = maudAllocate(&def->allocator, SlotBytes(s->capacity), alignof(Slot));
    s->free = maudAllocate(&def->allocator, FreeBytes(s->capacity), alignof(uint32_t));
    bool all = s->slots != nullptr && s->free != nullptr;
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
        def->cookie != SOURCE_DEF_COOKIE || !PatternValid(&def->directivity))
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
