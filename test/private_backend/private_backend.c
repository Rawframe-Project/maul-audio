// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A private backend as a console's would be, built from outside the
// library against src/backend.h: one output device, rendered by the
// caller's thread, at its native rate or a required one. Each stream
// holds a block from the context's allocator, as platform backends hold
// their buffers, given back only once the stream is released.

#include "backend.h"
#include "context.h"
#include "device.h"

#include <stddef.h>

#define PRIVATE_RATE 48000u
#define STREAM_BYTES 64u

// The block each stream slot holds while attached.
static void** Blocks(maudContext* context)
{
    return context->native;
}

static maudResult OpenContext(maudContext* context)
{
    static const char name[] = "Private output";
    static const char key[] = "private-output";
    maudDeviceSpec spec = {
        .info = {.direction = maud_directionOutput,
                 .nativeLayout = maud_layoutStereo,
                 .nativeSampleRate = PRIVATE_RATE,
                 .minSampleRate = PRIVATE_RATE,
                 .maxSampleRate = PRIVATE_RATE,
                 .spatializer = maud_spatializerNone},
        .name = name,
        .nameLength = sizeof(name) - 1,
        .key = key,
        .keyLength = sizeof(key) - 1,
    };
    size_t bytes = (size_t)context->streams.capacity * sizeof(void*);
    void** blocks = maudContextAllocate(context, bytes, alignof(void*));
    if (blocks == nullptr)
    {
        return maud_errorCapacity;
    }
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        blocks[i] = nullptr;
    }
    context->native = blocks;
    maudDeviceId id;
    return maudAddDevice(context, &spec, &id);
}

static void CloseContext(maudContext* context)
{
    if (context->native != nullptr)
    {
        maudContextRelease(context, context->native,
                           (size_t)context->streams.capacity * sizeof(void*), alignof(void*));
        context->native = nullptr;
    }
}

static maudResult AttachStream(maudContext* context, maudStreamSlot* slot)
{
    void* block = maudContextAllocate(context, STREAM_BYTES, alignof(max_align_t));
    if (block == nullptr)
    {
        return maud_errorCapacity;
    }
    Blocks(context)[slot - context->streams.slots] = block;
    return maud_success;
}

static void ReleaseStream(maudContext* context, maudStreamSlot* slot)
{
    void** held = &Blocks(context)[slot - context->streams.slots];
    maudContextRelease(context, *held, STREAM_BYTES, alignof(max_align_t));
    *held = nullptr;
}

static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    (void)device;
    if (def->mode != maud_modePull || def->ratePolicy == maud_ratePlatformConverted ||
        (def->ratePolicy != maud_rateNative && def->sampleRate != PRIVATE_RATE))
    {
        return maud_errorUnsupported;
    }
    *formatOut = (maudStreamFormat){
        .sampleRate = PRIVATE_RATE,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : PRIVATE_RATE / 100,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

static const maudBackend s_private = {
    .kind = maud_backendPrivate,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .openStream = OpenStream,
    .attachStream = AttachStream,
    .releaseStream = ReleaseStream,
    .rendersOnCaller = true,
    .hasNoVoice = true,
};

const maudBackend* maudGetPrivateBackend(void)
{
    return &s_private;
}
