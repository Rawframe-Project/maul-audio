// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The iOS backend's audio session (AVAudioSession), in Objective-C.

#ifndef MAUL_AUDIO_SRC_IOS_SESSION_H
#define MAUL_AUDIO_SRC_IOS_SESSION_H

#include "ios_core.h"

// The session's current rate and output channels.
void maudIosSessionFormat(uint32_t* rate, uint32_t* channels);

// What the session adds on one side of a buffer, in nanoseconds: its
// output or input latency and its IO buffer.
int64_t maudIosSessionLatency(bool input);

// Sets the category for what runs and activates the session while
// anything runs or focus is asked for, deactivating it, so that others
// resume, otherwise. false when the session refuses.
bool maudIosSessionUpdate(maudIos* ios, bool outputs, bool inputs);

#endif // MAUL_AUDIO_SRC_IOS_SESSION_H
