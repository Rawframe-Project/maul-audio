// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on PipeWire. Each stream is a pw_stream processed on
// libpipewire's real-time data thread: its process callback moves
// whatever size PipeWire asks for through the stream's fixed-period
// adapter, so the host's callback always sees whole periods.

#include "pipewire_stream.h"

#include "context.h"
#include "period.h"
#include "pipewire_core.h"
#include "thread.h"

#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <string.h>

// Bytes of the pod that describes a stream's format.
#define FORMAT_POD_BYTES 1024

static maudPipewireStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudPipewire* pipewire = context->native;
    return &pipewire->streams[slot - context->streams.slots];
}

// The PipeWire channel position of each speaker.
static uint32_t PositionOf(maudSpeaker speaker)
{
    static const uint32_t positions[] = {
        [maud_speakerNone] = SPA_AUDIO_CHANNEL_UNKNOWN,
        [maud_speakerFrontLeft] = SPA_AUDIO_CHANNEL_FL,
        [maud_speakerFrontRight] = SPA_AUDIO_CHANNEL_FR,
        [maud_speakerFrontCenter] = SPA_AUDIO_CHANNEL_FC,
        [maud_speakerLowFrequency] = SPA_AUDIO_CHANNEL_LFE,
        [maud_speakerBackLeft] = SPA_AUDIO_CHANNEL_RL,
        [maud_speakerBackRight] = SPA_AUDIO_CHANNEL_RR,
        [maud_speakerSideLeft] = SPA_AUDIO_CHANNEL_SL,
        [maud_speakerSideRight] = SPA_AUDIO_CHANNEL_SR,
        [maud_speakerTopFrontLeft] = SPA_AUDIO_CHANNEL_TFL,
        [maud_speakerTopFrontRight] = SPA_AUDIO_CHANNEL_TFR,
        [maud_speakerTopBackLeft] = SPA_AUDIO_CHANNEL_TRL,
        [maud_speakerTopBackRight] = SPA_AUDIO_CHANNEL_TRR,
    };
    return positions[speaker];
}

// Builds the stream's format into buffer: interleaved 32-bit float at
// its rate, with its layout's positions; a mono stream is MONO.
static const struct spa_pod* BuildFormat(const maudStreamCore* core, uint8_t* buffer)
{
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, FORMAT_POD_BYTES);
    uint32_t channels = maudGetLayoutChannelCount(core->format.layout);
    struct spa_audio_info_raw info = {
        .format = SPA_AUDIO_FORMAT_F32,
        .rate = core->format.sampleRate,
        .channels = channels,
    };
    for (uint32_t c = 0; c < channels; ++c)
    {
        info.position[c] = channels == 1 ? SPA_AUDIO_CHANNEL_MONO
                                         : PositionOf(maudGetLayoutSpeaker(core->format.layout, c));
    }
    return spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
}

static void OnStateChanged(void* data, enum pw_stream_state old, enum pw_stream_state state,
                           const char* error)
{
    (void)old;
    (void)error;
    maudPipewireStream* entry = data;
    entry->state = state;
}

// Runs on libpipewire's data thread: no allocation, lock or wait.
static void OnProcess(void* data)
{
    maudPipewireStream* entry = data;
    maudStreamCore* core = entry->core;
    const maudPipewireApi* api = &entry->owner->api;
    struct pw_buffer* buffer = api->streamDequeueBuffer(entry->stream);
    if (buffer == nullptr)
    {
        return;
    }
    struct spa_data* plane = &buffer->buffer->datas[0];
    uint32_t stride = core->period.channelCount * (uint32_t)sizeof(float);
    bool output = core->def.direction == maud_directionOutput;
    uint32_t frames = output ? plane->maxsize / stride : plane->chunk->size / stride;
    if (output && buffer->requested != 0 && buffer->requested < frames)
    {
        frames = (uint32_t)buffer->requested;
    }
    if (plane->data != nullptr && frames != 0)
    {
        bool running =
            atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
        atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
        core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
        float* samples = (float*)((uint8_t*)plane->data + (output ? 0 : plane->chunk->offset));
        if (output && running)
        {
            maudPullPeriod(&core->period, samples, frames);
        }
        else if (output)
        {
            memset(samples, 0, (size_t)frames * stride);
        }
        else if (running)
        {
            maudPushPeriod(&core->period, samples, frames);
        }
        atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
        atomic_fetch_add_explicit(&core->position, frames, memory_order_release);
    }
    if (output)
    {
        plane->chunk->offset = 0;
        plane->chunk->stride = (int32_t)stride;
        plane->chunk->size = frames * stride;
    }
    api->streamQueueBuffer(entry->stream, buffer);
}

static const struct pw_stream_events s_streamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = OnStateChanged,
    .process = OnProcess,
};

// The stream's properties: what it is, its period as a latency hint,
// and, for a stream opened on a device, that device and no other.
static struct pw_properties* StreamProperties(const maudContext* context,
                                              const maudStreamCore* core)
{
    const maudPipewireApi* api = &((maudPipewire*)context->native)->api;
    bool output = core->def.direction == maud_directionOutput;
    struct pw_properties* props = api->propertiesNew(
        PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, output ? "Playback" : "Capture",
        PW_KEY_MEDIA_ROLE, core->def.role == maud_roleCommunications ? "Communication" : "Game",
        nullptr);
    if (props == nullptr)
    {
        return nullptr;
    }
    api->propertiesSetf(props, PW_KEY_NODE_LATENCY, "%u/%u", core->format.periodFrames,
                        core->format.sampleRate);
    const maudDeviceSlot* device = maudFindDevice(context, core->binding.requested);
    if (device != nullptr)
    {
        api->propertiesSetf(props, PW_KEY_TARGET_OBJECT, "%.*s", (int)device->key.length,
                            device->key.bytes);
        api->propertiesSetf(props, PW_KEY_NODE_DONT_RECONNECT, "true");
    }
    return props;
}

// Iterates the loop until the stream has a format or failed, up to the
// deadline.
static bool WaitForFormat(maudPipewire* pipewire, const maudPipewireStream* entry)
{
    int64_t deadline = maudPipewireNow() + MAUD_PIPEWIRE_DEADLINE_NS;
    maudPipewireConnection* connection = &pipewire->connection;
    pw_loop_enter(connection->loop);
    while (entry->state != PW_STREAM_STATE_PAUSED && entry->state != PW_STREAM_STATE_STREAMING &&
           entry->state != PW_STREAM_STATE_ERROR && !connection->lost)
    {
        int64_t remaining = deadline - maudPipewireNow();
        if (remaining <= 0)
        {
            break;
        }
        pw_loop_iterate(connection->loop, (int)(remaining / 1000000 + 1));
    }
    pw_loop_leave(connection->loop);
    return entry->state == PW_STREAM_STATE_PAUSED || entry->state == PW_STREAM_STATE_STREAMING;
}

// Creates and connects the slot's pw_stream, inactive. With wait, it
// waits up to the deadline for PipeWire to accept the format, unless
// the stream has no device to negotiate with yet.
static maudResult ConnectStream(maudContext* context, maudStreamSlot* slot, bool wait)
{
    maudPipewire* pipewire = context->native;
    maudPipewireStream* entry = EntryOf(context, slot);
    maudStreamCore* core = &slot->core;
    // While the daemon is away the stream waits without a pw_stream; it
    // gets one when a new core connects.
    if (pipewire->connection.core == nullptr)
    {
        return maud_success;
    }
    struct pw_properties* props = StreamProperties(context, core);
    if (props == nullptr)
    {
        return maud_errorPlatform;
    }
    *entry = (maudPipewireStream){.owner = pipewire, .core = core};
    entry->stream = pipewire->api.streamNew(pipewire->connection.core, "Maul Audio", props);
    if (entry->stream == nullptr)
    {
        return maud_errorPlatform;
    }
    entry->used = true;
    pipewire->api.streamAddListener(entry->stream, &entry->listener, &s_streamEvents, entry);
    uint8_t buffer[FORMAT_POD_BYTES];
    const struct spa_pod* params[] = {BuildFormat(core, buffer)};
    enum pw_stream_flags flags = PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                 PW_STREAM_FLAG_RT_PROCESS | PW_STREAM_FLAG_INACTIVE;
    bool output = core->def.direction == maud_directionOutput;
    int connected = pipewire->api.streamConnect(entry->stream,
                                                output ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT,
                                                PW_ID_ANY, flags, params, 1);
    bool waiting = !wait || core->binding.current.index1 == 0;
    if (connected < 0 || (!waiting && !WaitForFormat(pipewire, entry)))
    {
        maudPipewireDetachStream(context, slot);
        return maud_errorPlatform;
    }
    return maud_success;
}

maudResult maudPipewireAttachStream(maudContext* context, maudStreamSlot* slot)
{
    return ConnectStream(context, slot, true);
}

void maudPipewireDetachStream(maudContext* context, maudStreamSlot* slot)
{
    maudPipewire* pipewire = context->native;
    maudPipewireStream* entry = EntryOf(context, slot);
    if (!entry->used)
    {
        return;
    }
    spa_hook_remove(&entry->listener);
    pipewire->api.streamDestroy(entry->stream);
    *entry = (maudPipewireStream){0};
}

void maudPipewireSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    maudPipewireStream* entry = EntryOf(context, slot);
    if (entry->used)
    {
        entry->owner->api.streamSetActive(entry->stream, active);
    }
}

void maudPipewireRetargetStream(maudContext* context, maudStreamSlot* slot)
{
    // A negotiated pw_stream keeps its format when it is offered others,
    // so a new rate takes a new stream.
    if (!EntryOf(context, slot)->used)
    {
        return;
    }
    maudPipewireDetachStream(context, slot);
    if (ConnectStream(context, slot, false) == maud_success &&
        atomic_load_explicit(&slot->core.state, memory_order_acquire) == maud_streamRunning)
    {
        maudPipewireSetStreamActive(context, slot, true);
    }
}

void maudPipewireDropStreams(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        if (context->streams.slots[i].live)
        {
            maudPipewireDetachStream(context, &context->streams.slots[i]);
        }
    }
}

void maudPipewireReconnectStreams(maudContext* context)
{
    for (uint32_t i = 0; i < context->streams.capacity; ++i)
    {
        maudStreamSlot* slot = &context->streams.slots[i];
        if (slot->live && slot->core.binding.requested.index1 == 0 &&
            !EntryOf(context, slot)->used && ConnectStream(context, slot, false) == maud_success &&
            atomic_load_explicit(&slot->core.state, memory_order_acquire) == maud_streamRunning)
        {
            maudPipewireSetStreamActive(context, slot, true);
        }
    }
}
