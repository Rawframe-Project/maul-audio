// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AAudio backend's context and devices, on Android 11 (API 30) and
// later. AAudio lists no devices and reports no changes to them: the
// context has one output and one input, the platform's defaults, which
// streams follow as Android moves them. The output's rate and channels
// are those of an AAudio stream opened on it once, at the start; the
// input is described from the output, since opening one would show the
// microphone in use.

#include "aaudio_core.h"
#include "aaudio_stream.h"
#include "backend.h"
#include "context.h"
#include "device.h"
#include "layout.h"

#include <string.h>

// The rate a device is taken to run at when the probe cannot tell.
#define FALLBACK_RATE 48000u

// The default output's rate and channel count, from a stream opened on
// it and closed; false when AAudio cannot open one.
static bool Probe(uint32_t* rate, uint32_t* channels)
{
    AAudioStreamBuilder* builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
    {
        return false;
    }
    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStream* stream = nullptr;
    aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &stream);
    AAudioStreamBuilder_delete(builder);
    if (opened != AAUDIO_OK)
    {
        return false;
    }
    int32_t probedRate = AAudioStream_getSampleRate(stream);
    int32_t probedChannels = AAudioStream_getChannelCount(stream);
    aaudio_result_t closed = AAudioStream_close(stream);
    (void)closed;
    *rate = probedRate > 0 ? (uint32_t)probedRate : FALLBACK_RATE;
    *channels = probedChannels > 0 ? (uint32_t)probedChannels : 2u;
    return true;
}

// Adds the default device of a direction, the default of both roles.
static maudResult AddDefault(maudContext* context, maudDirection direction, uint32_t rate,
                             maudChannelLayout layout)
{
    bool output = direction == maud_directionOutput;
    const char* name = output ? "Default output" : "Default input";
    maudDeviceSpec spec = {
        .info =
            {
                .direction = direction,
                .nativeLayout = layout,
                .nativeSampleRate = rate,
                .minSampleRate = rate,
                .maxSampleRate = rate,
            },
        .name = name,
        .nameLength = maudCutUtf8(name, context->def.limits.deviceTextBytes),
        .key = "default",
        .keyLength = 7,
    };
    maudDeviceId device;
    return maudAddDevice(context, &spec, &device);
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t streams = context->def.limits.streams;
    size_t bytes = sizeof(maudAaudio) + (size_t)streams * sizeof(maudAaudioStream);
    maudAaudio* aaudio = maudContextAllocate(context, bytes, alignof(maudAaudio));
    if (aaudio == nullptr)
    {
        return maud_errorCapacity;
    }
    *aaudio = (maudAaudio){.context = context, .bytes = bytes};
    aaudio->streams = (maudAaudioStream*)(aaudio + 1);
    memset(aaudio->streams, 0, (size_t)streams * sizeof(maudAaudioStream));
    context->native = aaudio;
    uint32_t rate = FALLBACK_RATE;
    uint32_t channels = 2;
    maudResult result = Probe(&rate, &channels) ? maud_success : maud_errorUnsupported;
    if (result == maud_success)
    {
        result = AddDefault(context, maud_directionOutput, rate, maudLayoutWithChannels(channels));
    }
    // The microphone, mono until a stream asks for more; AAudio converts.
    if (result == maud_success)
    {
        result = AddDefault(context, maud_directionInput, rate, maud_layoutMono);
    }
    if (result != maud_success)
    {
        maudContextRelease(context, aaudio, bytes, alignof(maudAaudio));
        context->native = nullptr;
    }
    return result;
}

static void CloseContext(maudContext* context)
{
    maudAaudio* aaudio = context->native;
    maudContextRelease(context, aaudio, aaudio->bytes, alignof(maudAaudio));
    context->native = nullptr;
}

// AAudio converts the rate in shared mode: a native stream takes the
// device's, a required rate must be it, and a converted stream any. A
// stream's period defaults to 10 ms.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    uint32_t native = device != nullptr && device->nativeSampleRate != 0 ? device->nativeSampleRate
                                                                         : FALLBACK_RATE;
    if (def->mode == maud_modePull ||
        (def->ratePolicy == maud_rateRequired && def->sampleRate != native))
    {
        return maud_errorUnsupported;
    }
    uint32_t rate = def->ratePolicy == maud_rateNative ? native : def->sampleRate;
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : rate / 100,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

static const maudBackend s_aaudio = {
    .kind = maud_backendAaudio,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = maudAaudioResumeStreams,
    .openStream = OpenStream,
    .attachStream = maudAaudioAttachStream,
    .detachStream = maudAaudioDetachStream,
    .setStreamActive = maudAaudioSetStreamActive,
    .exclusive = true,
};

const maudBackend* maudGetAaudioBackend(void)
{
    return &s_aaudio;
}
