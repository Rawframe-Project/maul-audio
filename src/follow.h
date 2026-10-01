// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Keeping streams on their devices: binding a new stream, moving the
// streams that follow a default when it changes, and suspending the
// streams whose device disappeared.

#ifndef MAUL_AUDIO_SRC_FOLLOW_H
#define MAUL_AUDIO_SRC_FOLLOW_H

#include "context_core.h"

// Puts a new stream on its requested device, or on the default it
// follows, or suspends it when there is none. Posts nothing.
void maudBindNewStream(maudContext* context, maudStreamSlot* slot);

// Moves every stream following the default of direction for role to
// that default, or suspends it when the default is the null id.
void maudFollowDefault(maudContext* context, maudDirection direction, maudDeviceRole role);

// Suspends every stream opened on a device that disappeared.
void maudLoseDevice(maudContext* context, maudDeviceId device);

// Starts or stops a stream as the host asks; it runs only when it is
// also not suspended.
void maudSetStreamStarted(maudContext* context, maudStreamSlot* slot, bool started);

#endif // MAUL_AUDIO_SRC_FOLLOW_H
