// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Core Audio's channel labels against a stream's layout: which device
// channel carries each of the stream's channels. Kept apart from the
// Core Audio headers, by the labels' values, so that every platform
// tests it.

#ifndef MAUL_AUDIO_SRC_COREAUDIO_LABELS_H
#define MAUL_AUDIO_SRC_COREAUDIO_LABELS_H

#include "maul-audio/layout.h"

#include <stdbool.h>
#include <stdint.h>

// The tags of an AudioChannelLayout that the layout itself spells out:
// kAudioChannelLayoutTag_UseChannelDescriptions and
// kAudioChannelLayoutTag_UseChannelBitmap. Any other tag names a layout
// that the platform expands into descriptions.
#define MAUD_LABEL_TAG_DESCRIPTIONS 0u
#define MAUD_LABEL_TAG_BITMAP       (1u << 16)

// The labels (kAudioChannelLabel_*) the stream's speakers are found by.
#define MAUD_LABEL_LEFT                  1u
#define MAUD_LABEL_RIGHT                 2u
#define MAUD_LABEL_CENTER                3u
#define MAUD_LABEL_LFE_SCREEN            4u
#define MAUD_LABEL_LEFT_SURROUND         5u
#define MAUD_LABEL_RIGHT_SURROUND        6u
#define MAUD_LABEL_LEFT_SURROUND_DIRECT  10u
#define MAUD_LABEL_RIGHT_SURROUND_DIRECT 11u
#define MAUD_LABEL_VERTICAL_HEIGHT_LEFT  13u
#define MAUD_LABEL_VERTICAL_HEIGHT_RIGHT 15u
#define MAUD_LABEL_TOP_BACK_LEFT         16u
#define MAUD_LABEL_TOP_BACK_RIGHT        18u
#define MAUD_LABEL_REAR_SURROUND_LEFT    33u
#define MAUD_LABEL_REAR_SURROUND_RIGHT   34u
#define MAUD_LABEL_LFE2                  37u
#define MAUD_LABEL_MONO                  42u
#define MAUD_LABEL_LEFT_TOP_REAR         52u
#define MAUD_LABEL_RIGHT_TOP_REAR        54u

// The most channels a stream's layout has.
#define MAUD_LABEL_MAX_STREAM_CHANNELS 12u

// The labels of a layout's channels, in channel order, into labelsOut of
// capacity entries: the descriptionCount labels of its descriptions for
// MAUD_LABEL_TAG_DESCRIPTIONS, one per set bit of bitmap
// (kAudioChannelBit_*, Left to TopBackRight) for MAUD_LABEL_TAG_BITMAP.
// The count, or 0 for any other tag, a bitmap with a bit past
// TopBackRight, or more labels than capacity.
MAUD_NODISCARD uint32_t maudCoreAudioLabelsOf(uint32_t tag, uint32_t bitmap,
                                              const uint32_t* descriptionLabels,
                                              uint32_t descriptionCount, uint32_t* labelsOut,
                                              uint32_t capacity);

// Finds, for each of the layout's channels, the device channel whose
// label names its speaker among labelCount labels, one device channel
// per stream channel: deviceOut[stream] = device. The surrounds (Ls,
// Rs) carry the sides of a layout without backs; in one with backs they
// carry whichever pair finds no label of its own, rear surrounds (Rls,
// Rrs) for the backs and side surround directs (Lsd, Rsd) for the
// sides. False when a channel has no match, when the layout is none or
// has more than MAUD_LABEL_MAX_STREAM_CHANNELS channels.
MAUD_NODISCARD bool maudCoreAudioChannelsByLabel(maudChannelLayout layout, const uint32_t* labels,
                                                 uint32_t labelCount, uint32_t* deviceOut);

// Whether deviceOf sends every one of count channels to its own index.
MAUD_NODISCARD bool maudCoreAudioChannelsInOrder(const uint32_t* deviceOf, uint32_t count);

// The channel map of an AUHAL unit (kAudioOutputUnitProperty_ChannelMap)
// from deviceOf, count stream channels against deviceChannels device
// channels. For output, one entry per device channel: the stream
// channel it plays, or -1. For input, one entry per stream channel: the
// device channel it captures. False when a stream channel's device
// channel is past deviceChannels.
MAUD_NODISCARD bool maudCoreAudioChannelMapOf(const uint32_t* deviceOf, uint32_t count,
                                              uint32_t deviceChannels, bool output,
                                              int32_t* mapOut);

#endif // MAUL_AUDIO_SRC_COREAUDIO_LABELS_H
