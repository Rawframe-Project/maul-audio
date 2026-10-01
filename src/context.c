// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Contexts: creation from a def, the stream table, and the checks that
// keep control calls and library allocations off the audio thread.

#include "context.h"

#include "allocator.h"
#include "backend.h"
#include "invariant.h"
#include "thread.h"

#define CONTEXT_DEF_COOKIE 0x6D616378u
#define MIN_RATE           8000u
#define MAX_RATE           384000u

maudContextDef maudDefaultContextDef(void)
{
    return (maudContextDef){
        .cookie = CONTEXT_DEF_COOKIE,
        .allocator = {0},
        .limits = {.streams = 8,
                   .periodFrames = 8192,
                   .devices = 32,
                   .notifications = 256,
                   .deviceTextBytes = 256},
        .backend = maud_backendNative,
        .offlineSampleRate = 48000,
    };
}

static bool DefValid(const maudContextDef* def)
{
    return def->cookie == CONTEXT_DEF_COOKIE && maudIsAllocatorValid(&def->allocator) &&
           def->limits.streams != 0 && def->limits.periodFrames != 0 && def->limits.devices != 0 &&
           def->limits.notifications >= 2 && def->limits.deviceTextBytes != 0 &&
           def->offlineSampleRate >= MIN_RATE && def->offlineSampleRate <= MAX_RATE &&
           (def->backend == maud_backendNative || def->backend == maud_backendOffline);
}

static void InitStreams(maudStreamTable* streams)
{
    for (uint32_t i = 0; i < streams->capacity; ++i)
    {
        maudStreamSlot* slot = &streams->slots[i];
        slot->generation = 1;
        slot->live = false;
        atomic_init(&slot->core.state, maud_streamIdle);
        atomic_init(&slot->core.blockRate, 0);
        atomic_init(&slot->core.renderingThread, 0);
        atomic_init(&slot->core.position, 0);
    }
}

// Gives each device slot its two places in the text storage.
static void InitDevices(maudDeviceTable* devices, char* text, uint32_t textBytes)
{
    for (uint32_t i = 0; i < devices->capacity; ++i)
    {
        devices->slots[i] = (maudDeviceSlot){
            .name = {.bytes = text + (size_t)i * 2u * textBytes},
            .key = {.bytes = text + ((size_t)i * 2u + 1u) * textBytes},
            .generation = 1,
        };
    }
}

maudResult maudCreateContext(const maudContextDef* def, maudContext** contextOut)
{
    if (contextOut == nullptr)
    {
        return maud_errorInvalid;
    }
    *contextOut = nullptr;
    if (def == nullptr || !DefValid(def))
    {
        return maud_errorInvalid;
    }
    // No native backend exists in this build yet.
    if (def->backend != maud_backendOffline)
    {
        return maud_errorUnsupported;
    }
    maudLayout layout = {0};
    size_t contextOffset = maudLayoutAdd(&layout, 1, sizeof(maudContext), alignof(maudContext));
    size_t streamsOffset = maudLayoutAdd(&layout, def->limits.streams, sizeof(maudStreamSlot),
                                         alignof(maudStreamSlot));
    size_t devicesOffset = maudLayoutAdd(&layout, def->limits.devices, sizeof(maudDeviceSlot),
                                         alignof(maudDeviceSlot));
    size_t recordsOffset = maudLayoutAdd(&layout, def->limits.notifications,
                                         sizeof(maudNotification), alignof(maudNotification));
    size_t textOffset =
        maudLayoutAdd(&layout, (size_t)def->limits.devices * 2u, def->limits.deviceTextBytes, 1);
    MAUD_ASSERT(!layout.overflow);
    unsigned char* block = maudAllocate(&def->allocator, layout.size, alignof(maudContext));
    if (block == nullptr)
    {
        return maud_errorCapacity;
    }
    maudContext* context = (maudContext*)(block + contextOffset);
    *context = (maudContext){
        .def = *def,
        .backend = maudGetOfflineBackend(),
        .devices = {.slots = (maudDeviceSlot*)(block + devicesOffset),
                    .capacity = def->limits.devices},
        .streams = {.slots = (maudStreamSlot*)(block + streamsOffset),
                    .capacity = def->limits.streams},
        .notifications = {.records = (maudNotification*)(block + recordsOffset),
                          .capacity = def->limits.notifications},
        .bytes = layout.size,
    };
    atomic_init(&context->misuse, 0);
    InitStreams(&context->streams);
    InitDevices(&context->devices, (char*)(block + textOffset), def->limits.deviceTextBytes);
    maudResult result = context->backend->openContext(context);
    if (result != maud_success)
    {
        maudRelease(&context->def.allocator, block, layout.size, alignof(maudContext));
        return result;
    }
    // A new context reports changes from here on, not its starting devices.
    context->notifications.count = 0;
    *contextOut = context;
    return maud_success;
}

maudResult maudDestroyContext(maudContext* context)
{
    if (context == nullptr)
    {
        return maud_success;
    }
    if (maudIsRenderingThread(context))
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        if (context->streams.slots[i].live)
        {
            maudReleaseStream(context, &context->streams.slots[i]);
        }
    }
    maudAllocator allocator = context->def.allocator;
    maudRelease(&allocator, context, context->bytes, alignof(maudContext));
    return maud_success;
}

maudBackendKind maudGetContextBackend(const maudContext* context)
{
    return context->def.backend;
}

uint64_t maudGetContextMisuse(const maudContext* context)
{
    return atomic_load_explicit(&context->misuse, memory_order_relaxed);
}

void* maudContextAllocate(maudContext* context, size_t size, size_t alignment)
{
    MAUD_ASSERT(!maudIsRenderingThread(context));
    return maudAllocate(&context->def.allocator, size, alignment);
}

void maudContextRelease(maudContext* context, void* memory, size_t size, size_t alignment)
{
    MAUD_ASSERT(!maudIsRenderingThread(context));
    maudRelease(&context->def.allocator, memory, size, alignment);
}

bool maudIsRenderingThread(const maudContext* context)
{
    uintptr_t self = maudCurrentThread();
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        // Only the atomic is read: the audio thread may ask while the
        // control thread creates or destroys another stream.
        const maudStreamSlot* slot = &context->streams.slots[i];
        if (atomic_load_explicit(&slot->core.renderingThread, memory_order_acquire) == self)
        {
            return true;
        }
    }
    return false;
}

void maudCountMisuse(maudContext* context)
{
    atomic_fetch_add_explicit(&context->misuse, 1, memory_order_relaxed);
}

maudStreamSlot* maudFindStream(const maudContext* context, maudStreamId stream)
{
    if (stream.index1 == 0 || stream.index1 > context->streams.capacity)
    {
        return nullptr;
    }
    maudStreamSlot* slot = &context->streams.slots[stream.index1 - 1];
    return slot->live && slot->generation == stream.generation ? slot : nullptr;
}

maudDeviceSlot* maudFindDevice(const maudContext* context, maudDeviceId device)
{
    if (device.index1 == 0 || device.index1 > context->devices.capacity)
    {
        return nullptr;
    }
    maudDeviceSlot* slot = &context->devices.slots[device.index1 - 1];
    return slot->live && slot->generation == device.generation ? slot : nullptr;
}

maudStreamSlot* maudFindFreeStreamSlot(const maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        if (!context->streams.slots[i].live)
        {
            return &context->streams.slots[i];
        }
    }
    return nullptr;
}

maudStreamId maudStreamIdOf(const maudContext* context, const maudStreamSlot* slot)
{
    uint32_t index = (uint32_t)(slot - context->streams.slots);
    return (maudStreamId){index + 1, slot->generation};
}

void maudReleaseStream(maudContext* context, maudStreamSlot* slot)
{
    MAUD_ASSERT(slot->live);
    maudContextRelease(context, slot->core.period.samples, slot->core.sampleBytes, alignof(float));
    slot->live = false;
    // A generation of 0 never names a stream, so it is skipped on wrap.
    slot->generation = slot->generation == UINT32_MAX ? 1 : slot->generation + 1;
}
