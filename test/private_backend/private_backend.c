// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A private backend as a console's would be, built from outside the
// library against src/backend.h: one output device, rendered by the
// caller's thread, at its native rate or a required one.

#include "backend.h"
#include "device.h"

#define PRIVATE_RATE 48000u

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
    maudDeviceId id;
    return maudAddDevice(context, &spec, &id);
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
    .openStream = OpenStream,
    .rendersOnCaller = true,
    .hasNoVoice = true,
};

const maudBackend* maudGetPrivateBackend(void)
{
    return &s_private;
}
