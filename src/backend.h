// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The interface every backend implements. The context and stream
// modules validate what callers pass; a backend reports its devices and
// settles what a device can do, refusing the rest.

#ifndef MAUL_AUDIO_SRC_BACKEND_H
#define MAUL_AUDIO_SRC_BACKEND_H

#include "context_core.h"

struct maudBackend
{
    maudBackendKind kind;
    // Connects a new context to the platform and fills its device table.
    // maud_errorUnsupported when the platform's service is missing,
    // maud_errorCapacity when the context's limits cannot hold the
    // starting devices.
    maudResult (*openContext)(maudContext* context);
    // Releases what openContext set up. May be NULL.
    void (*closeContext)(maudContext* context);
    // Takes in the platform's pending reports, without blocking. May be
    // NULL.
    void (*pump)(maudContext* context);
    // Settles a stream's format from a def already checked for ranges, on
    // device (NULL while the stream has none), or refuses it:
    // maud_errorUnsupported for a mode, policy or format the backend
    // cannot run.
    maudResult (*openStream)(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut);
    // Connects a stream's platform side once its slot's core is set up.
    // May be NULL.
    maudResult (*attachStream)(maudContext* context, maudStreamSlot* slot);
    // Releases what attachStream set up; afterwards no platform thread
    // touches the core. May be NULL.
    void (*detachStream)(maudContext* context, maudStreamSlot* slot);
    // Lets the platform run the stream or holds it. May be NULL.
    void (*setStreamActive)(maudContext* context, maudStreamSlot* slot, bool active);
    // Offers the stream's format again after a move changed its rate.
    // May be NULL.
    void (*retargetStream)(maudContext* context, maudStreamSlot* slot);
    // Whether the caller's thread renders the streams (maudRenderStream
    // and maudFeedStream), as on the offline backend.
    bool rendersOnCaller;
};

// The offline backend.
const maudBackend* maudGetOfflineBackend(void);

// The PipeWire backend, in builds with MAUL_AUDIO_PIPEWIRE.
const maudBackend* maudGetPipewireBackend(void);

// The PulseAudio backend, in builds with MAUL_AUDIO_PULSE.
const maudBackend* maudGetPulseBackend(void);

#endif // MAUL_AUDIO_SRC_BACKEND_H
