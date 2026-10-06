// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend's context and devices, on iOS 15 and later. iOS
// routes audio itself and lets an application choose no output: the
// context has one output and one input, the session's, keyed "default",
// which streams follow wherever iOS routes them. Their rate and channels
// are the session's when the context opens. Streams run on RemoteIO
// units (ios_stream.c); the session's category follows them and the
// host's focus requests (ios_session.m).

#include "backend.h"
#include "context.h"
#include "device.h"
#include "focus.h"
#include "ios_core.h"
#include "ios_session.h"
#include "ios_stream.h"
#include "layout.h"

#include <mach/mach_time.h>
#include <string.h>

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
    size_t bytes = sizeof(maudIos) + (size_t)streams * sizeof(maudIosStream);
    maudIos* ios = maudContextAllocate(context, bytes, alignof(maudIos));
    if (ios == nullptr)
    {
        return maud_errorCapacity;
    }
    *ios = (maudIos){.context = context, .bytes = bytes};
    ios->streams = (maudIosStream*)(ios + 1);
    memset(ios->streams, 0, (size_t)streams * sizeof(maudIosStream));
    context->native = ios;
    mach_timebase_info_data_t timebase = {0};
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.denom == 0)
    {
        timebase = (mach_timebase_info_data_t){1, 1};
    }
    ios->timebaseNumer = timebase.numer;
    ios->timebaseDenom = timebase.denom;
    maudIosSessionFormat(&ios->rate, &ios->channels);
    // The microphone, mono until a stream asks for more; the unit
    // converts.
    maudResult result =
        AddDefault(context, maud_directionOutput, ios->rate, maudLayoutWithChannels(ios->channels));
    if (result == maud_success)
    {
        result = AddDefault(context, maud_directionInput, ios->rate, maud_layoutMono);
    }
    if (result != maud_success)
    {
        maudContextRelease(context, ios, bytes, alignof(maudIos));
        context->native = nullptr;
    }
    return result;
}

static void CloseContext(maudContext* context)
{
    maudIos* ios = context->native;
    // No stream runs: the session is deactivated, letting others resume.
    ios->session.focus = maud_focusRelease;
    bool updated = maudIosSessionUpdate(ios, false, false);
    (void)updated;
    maudContextRelease(context, ios, ios->bytes, alignof(maudIos));
    context->native = nullptr;
}

// RemoteIO converts the rate: a native stream takes the session's, a
// required rate must be it, and a converted stream any. A stream's
// period defaults to 10 ms.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    uint32_t native =
        device != nullptr && device->nativeSampleRate != 0 ? device->nativeSampleRate : 48000u;
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

// Focus is the session's: asking sets whether it mixes with others and
// activates it; releasing mixes again and deactivates it when no stream
// runs.
static maudResult RequestFocus(maudContext* context, maudFocusRequest request, maudDeviceRole role)
{
    (void)role;
    maudIos* ios = context->native;
    maudFocusRequest previous = ios->session.focus;
    ios->session.focus = request;
    if (!maudIosUpdateSession(context))
    {
        ios->session.focus = previous;
        return maud_errorPlatform;
    }
    maudReportFocus(context, request == maud_focusRelease ? maud_focusNone : maud_focusHeld);
    return maud_success;
}

static const maudBackend s_ios = {
    .kind = maud_backendCoreAudio,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .openStream = OpenStream,
    .attachStream = maudIosAttachStream,
    .detachStream = maudIosDetachStream,
    .setStreamActive = maudIosSetStreamActive,
    .requestFocus = RequestFocus,
};

const maudBackend* maudGetIosBackend(void)
{
    return &s_ios;
}
