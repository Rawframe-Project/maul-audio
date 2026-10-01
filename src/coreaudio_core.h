// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The CoreAudio backend's state, shared by its context and stream
// modules.

#ifndef MAUL_AUDIO_SRC_COREAUDIO_CORE_H
#define MAUL_AUDIO_SRC_COREAUDIO_CORE_H

#include "context_core.h"
#include "device.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <dispatch/dispatch.h>
#include <stdatomic.h>

// Bytes kept for a device's UID and for its name, in UTF-8.
#define MAUD_COREAUDIO_KEY_BYTES  256u
#define MAUD_COREAUDIO_NAME_BYTES 256u
// Bytes for one property read of variable size: a device's stream
// configuration or its rate ranges.
#define MAUD_COREAUDIO_SCRATCH_BYTES 4096u

// The main element of every HAL property: kAudioObjectPropertyElementMain
// (macOS 12) by its value, so older deployment targets build.
#define MAUD_COREAUDIO_ELEMENT_MAIN 0u

// One scanned device direction's text, which its spec points into.
typedef struct maudCoreAudioEndpoint
{
    AudioObjectID object;
    char key[MAUD_COREAUDIO_KEY_BYTES];
    char name[MAUD_COREAUDIO_NAME_BYTES];
} maudCoreAudioEndpoint;

// A stream's AUHAL unit, run by the HAL's IO thread while it plays.
typedef struct maudCoreAudioStream
{
    maudStreamCore* core;
    AudioComponentInstance unit;
    bool playing;
} maudCoreAudioStream;

typedef struct maudCoreAudio
{
    maudContext* context;
    // The queue the HAL's change blocks run on, and the blocks.
    dispatch_queue_t queue;
    AudioObjectPropertyListenerBlock listener;
    bool listening;
    // Raised by a change block; the drain rescans.
    atomic_bool changed;
    // Room for a scan: device objects, endpoints, specs, one read.
    AudioObjectID* objects;
    uint32_t objectCapacity;
    maudCoreAudioEndpoint* endpoints;
    maudDeviceSpec* specs;
    unsigned char* scratch;
    maudCoreAudioStream* streams;
    size_t bytes;
} maudCoreAudio;

// A HAL property's address on the main element.
static inline AudioObjectPropertyAddress maudCoreAudioAddress(AudioObjectPropertySelector selector,
                                                              AudioObjectPropertyScope scope)
{
    return (AudioObjectPropertyAddress){selector, scope, MAUD_COREAUDIO_ELEMENT_MAIN};
}

#endif // MAUL_AUDIO_SRC_COREAUDIO_CORE_H
