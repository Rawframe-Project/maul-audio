// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend's state, shared by its context, stream and session
// modules.

#ifndef MAUL_AUDIO_SRC_IOS_CORE_H
#define MAUL_AUDIO_SRC_IOS_CORE_H

#include "context_core.h"

#include <AudioToolbox/AudioToolbox.h>
#include <stdatomic.h>

// A stream's RemoteIO unit, run by the system's IO thread while it
// plays.
typedef struct maudIosStream
{
    maudStreamCore* core;
    AudioComponentInstance unit;
    bool playing;
    // What the session adds between a buffer's host time and the
    // speaker, or between the microphone and it, in nanoseconds.
    int64_t sessionLatency;
    // An input stream's buffer list, with room for the most frames one
    // render brings, as allocated.
    AudioBufferList* captured;
    size_t capturedBytes;
    // The context, for the host time base.
    const struct maudIos* owner;
    // The sample time the next IO cycle should start at, or a negative
    // value before the first: a later start skipped cycles.
    Float64 nextSampleTime;
} maudIosStream;

// The session as the context last set it (ios_session.m).
typedef struct maudIosSession
{
    // The session's category and options were set, and whether it is
    // active.
    bool configured;
    bool active;
    // The focus the host asked for, which decides whether the
    // session mixes with others.
    maudFocusRequest focus;
    // The category's inputs: whether input and output streams run.
    bool inputs;
    bool outputs;
} maudIosSession;

// What the session reports on the main thread, for the drain: whether
// an interruption began since the last drain, the last interruption
// event (0 none since the last drain, 1 began, 2 ended with the hint to
// resume, 3 ended without it), and a route change. An interruption that
// begins and ends between two drains still holds and releases the
// streams, as iOS stopped their units meanwhile.
typedef struct maudIosSignals
{
    atomic_bool began;
    atomic_int interruption;
    atomic_bool routeChanged;
} maudIosSignals;

// The signals' values.
#define MAUD_IOS_INTERRUPTION_BEGAN   1
#define MAUD_IOS_INTERRUPTION_RESUME  2
#define MAUD_IOS_INTERRUPTION_STOPPED 3

typedef struct maudIos
{
    maudContext* context;
    maudIosStream* streams;
    maudIosSession session;
    maudIosSignals signals;
    // The session observer (ios_session.m), retained.
    void* observer;
    // The session's rate and output channels when the context opened.
    uint32_t rate;
    uint32_t channels;
    // mach_absolute_time's units, for the units' host times.
    uint32_t timebaseNumer;
    uint32_t timebaseDenom;
    size_t bytes;
} maudIos;

#endif // MAUL_AUDIO_SRC_IOS_CORE_H
