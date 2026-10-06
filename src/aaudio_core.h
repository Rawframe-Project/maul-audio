// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AAudio backend's state, shared by its context and stream modules.

#ifndef MAUL_AUDIO_SRC_AAUDIO_CORE_H
#define MAUL_AUDIO_SRC_AAUDIO_CORE_H

#include "context_core.h"

#include <aaudio/AAudio.h>
#include <stdatomic.h>

// A stream's AAudio stream, whose data callback runs on AAudio's thread
// while it plays.
typedef struct maudAaudioStream
{
    maudStreamCore* core;
    AAudioStream* stream;
    bool playing;
    // The stream's xrun count when the callback last read it.
    int32_t xruns;
    // Raised by the error callback when the stream's device went away or
    // the stream failed; the drain opens the stream again.
    atomic_bool lost;
} maudAaudioStream;

typedef struct maudAaudio
{
    maudContext* context;
    maudAaudioStream* streams;
    size_t bytes;
} maudAaudio;

#endif // MAUL_AUDIO_SRC_AAUDIO_CORE_H
