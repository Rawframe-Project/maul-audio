// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The interface every backend implements. The context and stream
// modules validate what callers pass; a backend settles what the device
// can do and refuses the rest.

#ifndef MAUL_AUDIO_SRC_BACKEND_H
#define MAUL_AUDIO_SRC_BACKEND_H

#include "context_core.h"

struct maudBackend
{
    // Settles a stream's format from a def already checked for ranges, or
    // refuses it: maud_errorUnsupported for a mode, policy or format the
    // backend cannot run.
    maudResult (*openStream)(const maudContext* context, const maudStreamDef* def,
                             maudStreamFormat* formatOut);
    // Whether the caller's thread renders the streams (maudRenderStream
    // and maudFeedStream), as on the offline backend.
    bool rendersOnCaller;
};

// The offline backend.
const maudBackend* maudGetOfflineBackend(void);

#endif // MAUL_AUDIO_SRC_BACKEND_H
