// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The AAudio backend's Java half: the library's maul.audio.Devices
// object, reached through JNI from the control thread only.

#ifndef MAUL_AUDIO_SRC_AAUDIO_JAVA_H
#define MAUL_AUDIO_SRC_AAUDIO_JAVA_H

#include "aaudio_core.h"

// One device Java listed: its AAudio id, its AudioDeviceInfo type, its
// most channels and its rate range (0 where it takes any), its address
// and product name.
typedef struct maudAaudioListing
{
    int32_t id;
    int32_t type;
    int32_t channels;
    int32_t lowRate;
    int32_t highRate;
    const char* address;
    const char* product;
} maudAaudioListing;

// Loads the Devices class through the Context's class loader, registers
// its native method and makes the object, which raises aaudio->changed
// whenever Android's devices change. false when any of it fails.
bool maudAaudioOpenJava(maudAaudio* aaudio, void* vm, void* context);

// Closes the object and drops the references.
void maudAaudioCloseJava(maudAaudio* aaudio);

// Calls listed for each device of a direction Java lists.
void maudAaudioListJava(maudAaudio* aaudio, maudDirection direction,
                        void (*listed)(maudAaudio* aaudio, maudDirection direction,
                                       const maudAaudioListing* listing));

// Whether the application holds the microphone permission; true when
// Java cannot say, so that AAudio's own refusal stands.
bool maudAaudioMayRecord(maudAaudio* aaudio);

// Asks for the microphone, once, where the Context is an Activity.
void maudAaudioAskToRecord(maudAaudio* aaudio);

#endif // MAUL_AUDIO_SRC_AAUDIO_JAVA_H
