// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams: checking defs, settling formats through the backend,
// starting and stopping, and rendering on the caller's thread for
// backends that render there.

#include "maul-audio/stream.h"

#include "backend.h"
#include "context.h"
#include "follow.h"
#include "period.h"
#include "thread.h"

#include <stdckdint.h>

#define STREAM_DEF_COOKIE 0x6D617364u
#define MIN_RATE          8000u
#define MAX_RATE          384000u

maudStreamDef maudDefaultStreamDef(void)
{
    return (maudStreamDef){
        .cookie = STREAM_DEF_COOKIE,
        .direction = maud_directionOutput,
        .mode = maud_modeCallback,
        .ratePolicy = maud_rateNative,
        .layout = maud_layoutStereo,
        .sampleRate = 0,
        .periodFrames = 0,
        .device = {0, 0},
        .role = maud_roleGeneral,
        .callback = nullptr,
        .user = nullptr,
    };
}

static bool DefValid(const maudStreamDef* def)
{
    if (def->cookie != STREAM_DEF_COOKIE || def->callback == nullptr ||
        def->direction > maud_directionInput || def->mode > maud_modePull ||
        def->ratePolicy > maud_ratePlatformConverted || def->role > maud_roleCommunications ||
        maudGetLayoutChannelCount(def->layout) == 0)
    {
        return false;
    }
    if (def->ratePolicy == maud_rateNative)
    {
        return def->sampleRate == 0;
    }
    return def->sampleRate >= MIN_RATE && def->sampleRate <= MAX_RATE;
}

// The device a new stream will start on: its requested device, which
// must be live and of its direction, or the default it follows, which
// may be none.
static maudResult FindStartingDevice(const maudContext* context, const maudStreamDef* def,
                                     const maudDeviceInfo** deviceOut)
{
    maudDeviceId id = def->device;
    if (id.index1 == 0)
    {
        id = context->devices.defaults[def->direction][def->role];
        if (id.index1 == 0)
        {
            *deviceOut = nullptr;
            return maud_success;
        }
    }
    const maudDeviceSlot* slot = maudFindDevice(context, id);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    if (slot->info.direction != def->direction)
    {
        return maud_errorInvalid;
    }
    *deviceOut = &slot->info;
    return maud_success;
}

// Opens the stream's format through the backend and allocates its
// period. The slot is untouched on failure.
static maudResult OpenCore(maudContext* context, const maudStreamDef* def,
                           const maudDeviceInfo* device, maudStreamSlot* slot)
{
    maudStreamFormat format;
    maudResult result = context->backend->openStream(context, def, device, &format);
    if (result != maud_success)
    {
        return result;
    }
    if (format.periodFrames == 0 || format.periodFrames > context->def.limits.periodFrames)
    {
        return maud_errorCapacity;
    }
    size_t bytes;
    if (ckd_mul(&bytes, (size_t)format.periodFrames,
                (size_t)maudGetLayoutChannelCount(format.layout)) ||
        ckd_mul(&bytes, bytes, sizeof(float)))
    {
        return maud_errorCapacity;
    }
    float* samples = maudContextAllocate(context, bytes, alignof(float));
    if (samples == nullptr)
    {
        return maud_errorCapacity;
    }
    maudStreamCore* core = &slot->core;
    core->def = *def;
    core->format = format;
    core->sampleBytes = bytes;
    maudInitPeriod(&core->period, def, &format, samples);
    atomic_store_explicit(&core->blockRate, format.sampleRate, memory_order_relaxed);
    atomic_store_explicit(&core->position, 0, memory_order_relaxed);
    maudBindNewStream(context, core);
    return maud_success;
}

maudResult maudCreateStream(maudContext* context, const maudStreamDef* def,
                            maudStreamId* streamIdOut)
{
    if (streamIdOut != nullptr)
    {
        *streamIdOut = (maudStreamId){0, 0};
    }
    if (context == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudIsRenderingThread(context))
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    if (def == nullptr || streamIdOut == nullptr || !DefValid(def))
    {
        maudCountMisuse(context);
        return maud_errorInvalid;
    }
    const maudDeviceInfo* device = nullptr;
    maudResult result = FindStartingDevice(context, def, &device);
    if (result == maud_errorInvalid)
    {
        maudCountMisuse(context);
    }
    if (result != maud_success)
    {
        return result;
    }
    maudStreamSlot* slot = maudFindFreeStreamSlot(context);
    if (slot == nullptr)
    {
        return maud_errorCapacity;
    }
    result = OpenCore(context, def, device, slot);
    if (result != maud_success)
    {
        return result;
    }
    slot->live = true;
    *streamIdOut = maudStreamIdOf(context, slot);
    return maud_success;
}

// Finds the stream a control call names, refusing calls from a thread
// that renders one of the context's streams.
static maudResult FindForControl(maudContext* context, maudStreamId stream,
                                 maudStreamSlot** slotOut)
{
    if (context == nullptr)
    {
        return maud_errorInvalid;
    }
    if (maudIsRenderingThread(context))
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    *slotOut = maudFindStream(context, stream);
    return *slotOut != nullptr ? maud_success : maud_errorStale;
}

maudResult maudDestroyStream(maudContext* context, maudStreamId stream)
{
    maudStreamSlot* slot = nullptr;
    maudResult result = FindForControl(context, stream, &slot);
    if (result != maud_success)
    {
        return result;
    }
    if (atomic_load_explicit(&slot->core.renderingThread, memory_order_acquire) != 0)
    {
        maudCountMisuse(context);
        return maud_errorState;
    }
    maudReleaseStream(context, slot);
    return maud_success;
}

static maudResult SetStarted(maudContext* context, maudStreamId stream, bool started)
{
    maudStreamSlot* slot = nullptr;
    maudResult result = FindForControl(context, stream, &slot);
    if (result != maud_success)
    {
        return result;
    }
    maudSetStreamStarted(&slot->core, started);
    return maud_success;
}

maudResult maudStartStream(maudContext* context, maudStreamId stream)
{
    return SetStarted(context, stream, true);
}

maudResult maudStopStream(maudContext* context, maudStreamId stream)
{
    return SetStarted(context, stream, false);
}

maudResult maudGetStreamStatus(const maudContext* context, maudStreamId stream,
                               maudStreamStatus* statusOut)
{
    if (context == nullptr || statusOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudStreamSlot* slot = maudFindStream(context, stream);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    const maudStreamBinding* binding = &slot->core.binding;
    *statusOut = (maudStreamStatus){
        .started = binding->started,
        .suspension = binding->suspension,
        .device = binding->current,
    };
    return maud_success;
}

maudResult maudGetStreamFormat(const maudContext* context, maudStreamId stream,
                               maudStreamFormat* formatOut)
{
    if (context == nullptr || formatOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudStreamSlot* slot = maudFindStream(context, stream);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    *formatOut = slot->core.format;
    return maud_success;
}

maudResult maudGetStreamPosition(const maudContext* context, maudStreamId stream,
                                 uint64_t* framesOut)
{
    if (context == nullptr || framesOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudStreamSlot* slot = maudFindStream(context, stream);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    *framesOut = atomic_load_explicit(&slot->core.position, memory_order_acquire);
    return maud_success;
}

// Checks a render or feed call and claims the stream for the calling
// thread. On success the caller must release it with EndRender.
static maudResult BeginRender(maudContext* context, maudStreamId stream, maudDirection direction,
                              bool haveFrames, uint32_t frameCount, maudStreamCore** coreOut)
{
    if (context == nullptr)
    {
        return maud_errorInvalid;
    }
    maudStreamSlot* slot = maudFindStream(context, stream);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    maudStreamCore* core = &slot->core;
    size_t samples;
    if (core->def.direction != direction || (frameCount != 0 && !haveFrames) ||
        ckd_mul(&samples, (size_t)frameCount, (size_t)core->period.channelCount) ||
        ckd_mul(&samples, samples, sizeof(float)))
    {
        maudCountMisuse(context);
        return maud_errorInvalid;
    }
    if (!context->backend->rendersOnCaller)
    {
        return maud_errorUnsupported;
    }
    if (atomic_load_explicit(&core->state, memory_order_acquire) != maud_streamRunning)
    {
        return maud_errorState;
    }
    uintptr_t idle = 0;
    if (!atomic_compare_exchange_strong_explicit(&core->renderingThread, &idle, maudCurrentThread(),
                                                 memory_order_acq_rel, memory_order_acquire))
    {
        return maud_errorState;
    }
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    *coreOut = core;
    return maud_success;
}

static void EndRender(maudStreamCore* core, uint32_t frameCount)
{
    atomic_fetch_add_explicit(&core->position, frameCount, memory_order_release);
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
}

maudResult maudRenderStream(maudContext* context, maudStreamId stream, float* framesOut,
                            uint32_t frameCount)
{
    maudStreamCore* core = nullptr;
    maudResult result =
        BeginRender(context, stream, maud_directionOutput, framesOut != nullptr, frameCount, &core);
    if (result != maud_success)
    {
        return result;
    }
    maudPullPeriod(&core->period, framesOut, frameCount);
    EndRender(core, frameCount);
    return maud_success;
}

maudResult maudFeedStream(maudContext* context, maudStreamId stream, const float* frames,
                          uint32_t frameCount)
{
    maudStreamCore* core = nullptr;
    maudResult result =
        BeginRender(context, stream, maud_directionInput, frames != nullptr, frameCount, &core);
    if (result != maud_success)
    {
        return result;
    }
    maudPushPeriod(&core->period, frames, frameCount);
    EndRender(core, frameCount);
    return maud_success;
}
