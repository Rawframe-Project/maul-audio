// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a context and its streams hold. The context composes its parts:
// the def it was made with, its backend, and its stream table.

#ifndef MAUL_AUDIO_SRC_CONTEXT_CORE_H
#define MAUL_AUDIO_SRC_CONTEXT_CORE_H

#include "period.h"

#include "maul-audio/context.h"
#include "maul-audio/stream.h"

#include <stdatomic.h>
#include <stdbool.h>

typedef struct maudBackend maudBackend;

// The run state of a stream.
enum
{
    maud_streamStopped = 0,
    maud_streamStarted = 1,
};

// A stream: what it was asked for, what it runs at, and the state the
// rendering thread and the control thread share.
typedef struct maudStreamCore
{
    maudStreamDef def;
    maudStreamFormat format;
    maudPeriod period;
    // Bytes of period samples, as allocated.
    size_t sampleBytes;
    _Atomic(uint8_t) state;
    // The thread rendering the stream, or 0.
    _Atomic(uintptr_t) renderingThread;
    // Frames moved to or from the device.
    _Atomic(uint64_t) position;
} maudStreamCore;

typedef struct maudStreamSlot
{
    maudStreamCore core;
    uint32_t generation;
    bool live;
} maudStreamSlot;

typedef struct maudStreamTable
{
    maudStreamSlot* slots;
    uint32_t capacity;
} maudStreamTable;

struct maudContext
{
    maudContextDef def;
    const maudBackend* backend;
    maudStreamTable streams;
    // Bytes of the context's block, as allocated.
    size_t bytes;
    _Atomic(uint64_t) misuse;
};

#endif // MAUL_AUDIO_SRC_CONTEXT_CORE_H
