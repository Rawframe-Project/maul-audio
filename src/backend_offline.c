// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The offline backend: no device and no thread. Its device runs at any
// rate, so a required rate is always native; it has no converter. The
// caller's thread renders its streams, so only pull mode exists.

#include "backend.h"

static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             maudStreamFormat* formatOut)
{
    if (def->mode != maud_modePull || def->ratePolicy == maud_ratePlatformConverted)
    {
        return maud_errorUnsupported;
    }
    uint32_t rate =
        def->ratePolicy == maud_rateNative ? context->def.offlineSampleRate : def->sampleRate;
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : rate / 100,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

static const maudBackend s_offline = {
    .openStream = OpenStream,
    .rendersOnCaller = true,
};

const maudBackend* maudGetOfflineBackend(void)
{
    return &s_offline;
}
