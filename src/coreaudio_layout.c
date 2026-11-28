// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Preferred channel layouts. A layout comes as its labels, a bitmap or
// a named tag; the system fills a named tag's descriptions, keeping the
// tag.

#include "coreaudio_layout.h"

#include "coreaudio_labels.h"

#include <stddef.h>

// The bytes before an AudioChannelLayout's descriptions.
#define LAYOUT_HEAD offsetof(AudioChannelLayout, mChannelDescriptions)

// Room for a channel layout of the most labeled channels.
typedef union LayoutRoom
{
    AudioChannelLayout layout;
    unsigned char bytes[LAYOUT_HEAD + MAUD_COREAUDIO_MAX_LABELS * sizeof(AudioChannelDescription)];
} LayoutRoom;

uint32_t maudCoreAudioReadLabels(const maudCoreAudioHal* hal, AudioObjectID object,
                                 AudioObjectPropertyScope scope, uint32_t* labelsOut)
{
    AudioObjectPropertyAddress address =
        maudCoreAudioAddress(kAudioDevicePropertyPreferredChannelLayout, scope);
    LayoutRoom read;
    UInt32 size = sizeof(read);
    if (hal->getData(object, &address, 0, nullptr, &size, &read) != noErr || size < LAYOUT_HEAD ||
        size > sizeof(read))
    {
        return 0;
    }
    const AudioChannelLayout* layout = &read.layout;
    AudioChannelLayoutTag tag = layout->mChannelLayoutTag;
    if (tag == kAudioChannelLayoutTag_UseChannelBitmap)
    {
        return maudCoreAudioLabelsOf(MAUD_LABEL_TAG_BITMAP, layout->mChannelBitmap, nullptr, 0,
                                     labelsOut, MAUD_COREAUDIO_MAX_LABELS);
    }
    // The system fills a named tag's descriptions and keeps the tag.
    LayoutRoom expanded;
    if (tag != kAudioChannelLayoutTag_UseChannelDescriptions)
    {
        size = sizeof(expanded);
        if (AudioFormatGetProperty(kAudioFormatProperty_ChannelLayoutForTag, sizeof(tag), &tag,
                                   &size, &expanded) != noErr ||
            size < LAYOUT_HEAD || size > sizeof(expanded))
        {
            return 0;
        }
        layout = &expanded.layout;
    }
    uint32_t count = layout->mNumberChannelDescriptions;
    if (count > MAUD_COREAUDIO_MAX_LABELS ||
        size < LAYOUT_HEAD + count * sizeof(AudioChannelDescription))
    {
        return 0;
    }
    uint32_t described[MAUD_COREAUDIO_MAX_LABELS] = {0};
    for (uint32_t i = 0; i < count; ++i)
    {
        described[i] = layout->mChannelDescriptions[i].mChannelLabel;
    }
    return maudCoreAudioLabelsOf(MAUD_LABEL_TAG_DESCRIPTIONS, 0, described, count, labelsOut,
                                 MAUD_COREAUDIO_MAX_LABELS);
}
