// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AAudio backend's state, shared by its context and stream modules.

#ifndef MAUL_AUDIO_SRC_AAUDIO_CORE_H
#define MAUL_AUDIO_SRC_AAUDIO_CORE_H

#include "context_core.h"
#include "device.h"

#include <aaudio/AAudio.h>
#include <jni.h>
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

// Bytes kept for a listed device's key and for its name, in UTF-8.
#define MAUD_AAUDIO_KEY_BYTES  128u
#define MAUD_AAUDIO_NAME_BYTES 128u

// One device of a scan: its AAudio id (0 for the default, which a
// stream opens without one) and the text its spec points into.
typedef struct maudAaudioEndpoint
{
    maudDirection direction;
    int32_t id;
    char key[MAUD_AAUDIO_KEY_BYTES];
    char name[MAUD_AAUDIO_NAME_BYTES];
} maudAaudioEndpoint;

// The Java half (aaudio_java.c), present when the host gave a Java VM
// and an Android Context: global references to the Context and to the
// library's maul.audio.Devices object, and the class's methods.
typedef struct maudAaudioJava
{
    JavaVM* vm;
    jobject context;
    jobject devices;
    jmethodID list;
    jmethodID mayRecord;
    jmethodID askToRecord;
    jmethodID close;
} maudAaudioJava;

typedef struct maudAaudio
{
    maudContext* context;
    maudAaudioStream* streams;
    // The default output's rate and channels, from the probe.
    uint32_t rate;
    uint32_t channels;
    // Raised by the Java half when Android's devices changed; the drain
    // lists them again.
    atomic_bool changed;
    bool hasJava;
    maudAaudioJava java;
    // Room for a scan.
    maudAaudioEndpoint* endpoints;
    maudDeviceSpec* specs;
    uint32_t endpointCount;
    size_t bytes;
} maudAaudio;

#endif // MAUL_AUDIO_SRC_AAUDIO_CORE_H
