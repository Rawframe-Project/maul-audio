// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on PipeWire: the backend hooks that connect, start, retarget
// and destroy a pw_stream for a stream slot.

#ifndef MAUL_AUDIO_SRC_PIPEWIRE_STREAM_H
#define MAUL_AUDIO_SRC_PIPEWIRE_STREAM_H

#include "context_core.h"

// Connects a pw_stream for a slot whose core is set up, inactive, and
// waits up to the deadline for PipeWire to accept its format.
maudResult maudPipewireAttachStream(maudContext* context, maudStreamSlot* slot);

// Destroys the slot's pw_stream. libpipewire waits for its data thread
// to leave the stream before this returns.
void maudPipewireDetachStream(maudContext* context, maudStreamSlot* slot);

// Activates or deactivates the slot's pw_stream.
void maudPipewireSetStreamActive(maudContext* context, maudStreamSlot* slot, bool active);

// Offers the stream's current format again, after a move changed its
// rate.
void maudPipewireRetargetStream(maudContext* context, maudStreamSlot* slot);

#endif // MAUL_AUDIO_SRC_PIPEWIRE_STREAM_H
