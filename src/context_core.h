// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a context and its streams hold. The context composes its parts:
// the def it was made with, its backend, its device table, its stream
// table and its notification queue.

#ifndef MAUL_AUDIO_SRC_CONTEXT_CORE_H
#define MAUL_AUDIO_SRC_CONTEXT_CORE_H

#include "period.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <stdatomic.h>
#include <stdbool.h>

typedef struct maudBackend maudBackend;

// The run state the rendering thread reads.
enum
{
    maud_streamIdle = 0,
    maud_streamRunning = 1,
};

// Where a stream stands, as the control thread keeps it.
typedef struct maudStreamBinding
{
    // The device it was opened on; the null id when it follows a default.
    maudDeviceId requested;
    // The device it is on; the null id while it has none.
    maudDeviceId current;
    bool started;
    maudSuspendReason suspension;
} maudStreamBinding;

// A stream: what it was asked for, what it runs at, where it stands, and
// the state the rendering thread and the control thread share.
typedef struct maudStreamCore
{
    maudStreamDef def;
    maudStreamFormat format;
    maudStreamBinding binding;
    maudPeriod period;
    // Bytes of period samples, as allocated.
    size_t sampleBytes;
    // Running when started and not suspended.
    _Atomic(uint8_t) state;
    // The rate blocks carry, published by the control thread.
    _Atomic(uint32_t) blockRate;
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

// A device's name or key: bytes in the context's text storage.
typedef struct maudDeviceText
{
    char* bytes;
    uint32_t length;
} maudDeviceText;

typedef struct maudDeviceSlot
{
    // The default flags are not kept here; the table's defaults are.
    maudDeviceInfo info;
    maudDeviceText name;
    maudDeviceText key;
    uint32_t generation;
    bool live;
} maudDeviceSlot;

typedef struct maudDeviceTable
{
    maudDeviceSlot* slots;
    uint32_t capacity;
    // The default device per direction and role.
    maudDeviceId defaults[2][2];
} maudDeviceTable;

typedef struct maudNotificationQueue
{
    maudNotification* records;
    uint32_t capacity;
    uint32_t head;
    uint32_t count;
} maudNotificationQueue;

struct maudContext
{
    maudContextDef def;
    const maudBackend* backend;
    maudDeviceTable devices;
    maudStreamTable streams;
    maudNotificationQueue notifications;
    // The backend's own state, or NULL.
    void* native;
    // Bytes of the context's block, as allocated.
    size_t bytes;
    // The platform holds the context's audio until the user acts.
    bool held;
    _Atomic(uint64_t) misuse;
};

#endif // MAUL_AUDIO_SRC_CONTEXT_CORE_H
