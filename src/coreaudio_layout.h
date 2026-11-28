// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A CoreAudio device's preferred channel layout, read as the labels of
// its channels in one direction.

#ifndef MAUL_AUDIO_SRC_COREAUDIO_LAYOUT_H
#define MAUL_AUDIO_SRC_COREAUDIO_LAYOUT_H

#include "coreaudio_core.h"

// The most device channels whose labels are read; a device with more
// is taken as naming none.
#define MAUD_COREAUDIO_MAX_LABELS 128u

// The labels of the device's preferred channel layout in scope, a named
// tag expanded by the system, into labelsOut of MAUD_COREAUDIO_MAX_LABELS
// entries. The count, or 0 when the device has none, the system cannot
// expand its tag, or it has more channels than the room.
uint32_t maudCoreAudioReadLabels(const maudCoreAudioHal* hal, AudioObjectID object,
                                 AudioObjectPropertyScope scope, uint32_t* labelsOut);

#endif // MAUL_AUDIO_SRC_COREAUDIO_LAYOUT_H
