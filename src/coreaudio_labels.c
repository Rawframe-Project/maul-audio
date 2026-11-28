// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Channel labels. Core Audio calls the speakers behind the listener
// surrounds (Ls, Rs) where a layout has one pair: a 5.1 layout's sides,
// a quad layout's backs. A layout with two pairs adds rear surrounds
// (Rls, Rrs) behind the surrounds, or side surround directs (Lsd, Rsd)
// beside them. So a stream's sides take the surrounds when it has no
// backs, else the side directs, and its backs the rear surrounds;
// whichever finds no label of its own takes the surrounds. A speaker is
// matched first by the labels that name it, then by the ones that stand
// in for it.

#include "coreaudio_labels.h"

#include <stddef.h>

// The highest bit of a channel bitmap read: kAudioChannelBit_TopBackRight.
// Bit n is the label n + 1.
#define LAST_BITMAP_BIT 17u

// The most labels a speaker is found by, and the end of a list.
#define MAX_CHOICES 2u
#define NO_LABEL    0u

typedef struct Choices
{
    uint32_t exact[MAX_CHOICES];
    uint32_t stand[MAX_CHOICES];
} Choices;

// The labels that name each speaker, then those that stand in for it,
// in a layout with back speakers or without.
static Choices ChoicesOf(maudSpeaker speaker, bool backs)
{
    static const Choices choices[] = {
        [maud_speakerFrontLeft] = {{MAUD_LABEL_LEFT}, {NO_LABEL}},
        [maud_speakerFrontRight] = {{MAUD_LABEL_RIGHT}, {NO_LABEL}},
        [maud_speakerFrontCenter] = {{MAUD_LABEL_CENTER, MAUD_LABEL_MONO}, {NO_LABEL}},
        [maud_speakerLowFrequency] = {{MAUD_LABEL_LFE_SCREEN, MAUD_LABEL_LFE2}, {NO_LABEL}},
        [maud_speakerBackLeft] = {{MAUD_LABEL_REAR_SURROUND_LEFT},
                                  {MAUD_LABEL_LEFT_SURROUND, MAUD_LABEL_LEFT_SURROUND_DIRECT}},
        [maud_speakerBackRight] = {{MAUD_LABEL_REAR_SURROUND_RIGHT},
                                   {MAUD_LABEL_RIGHT_SURROUND, MAUD_LABEL_RIGHT_SURROUND_DIRECT}},
        [maud_speakerSideLeft] = {{MAUD_LABEL_LEFT_SURROUND_DIRECT, MAUD_LABEL_LEFT_SURROUND},
                                  {MAUD_LABEL_REAR_SURROUND_LEFT}},
        [maud_speakerSideRight] = {{MAUD_LABEL_RIGHT_SURROUND_DIRECT, MAUD_LABEL_RIGHT_SURROUND},
                                   {MAUD_LABEL_REAR_SURROUND_RIGHT}},
        [maud_speakerTopFrontLeft] = {{MAUD_LABEL_VERTICAL_HEIGHT_LEFT}, {NO_LABEL}},
        [maud_speakerTopFrontRight] = {{MAUD_LABEL_VERTICAL_HEIGHT_RIGHT}, {NO_LABEL}},
        [maud_speakerTopBackLeft] = {{MAUD_LABEL_TOP_BACK_LEFT, MAUD_LABEL_LEFT_TOP_REAR},
                                     {NO_LABEL}},
        [maud_speakerTopBackRight] = {{MAUD_LABEL_TOP_BACK_RIGHT, MAUD_LABEL_RIGHT_TOP_REAR},
                                      {NO_LABEL}},
    };
    if (speaker <= maud_speakerNone || (size_t)speaker >= sizeof(choices) / sizeof(choices[0]))
    {
        return (Choices){{NO_LABEL}, {NO_LABEL}};
    }
    Choices found = choices[speaker];
    if (!backs && (speaker == maud_speakerSideLeft || speaker == maud_speakerSideRight))
    {
        // Sides without backs are the surrounds, before the side directs.
        uint32_t direct = found.exact[0];
        found.exact[0] = found.exact[1];
        found.exact[1] = direct;
    }
    return found;
}

// Whether the layout has a back speaker.
MAUD_NODISCARD static bool HasBacks(maudChannelLayout layout, uint32_t count)
{
    for (uint32_t c = 0; c < count; ++c)
    {
        maudSpeaker speaker = maudGetLayoutSpeaker(layout, c);
        if (speaker == maud_speakerBackLeft || speaker == maud_speakerBackRight)
        {
            return true;
        }
    }
    return false;
}

uint32_t maudCoreAudioLabelsOf(uint32_t tag, uint32_t bitmap, const uint32_t* descriptionLabels,
                               uint32_t descriptionCount, uint32_t* labelsOut, uint32_t capacity)
{
    if (tag == MAUD_LABEL_TAG_DESCRIPTIONS)
    {
        if (descriptionCount > capacity || (descriptionCount > 0 && descriptionLabels == nullptr))
        {
            return 0;
        }
        for (uint32_t i = 0; i < descriptionCount; ++i)
        {
            labelsOut[i] = descriptionLabels[i];
        }
        return descriptionCount;
    }
    if (tag != MAUD_LABEL_TAG_BITMAP || (bitmap >> (LAST_BITMAP_BIT + 1u)) != 0)
    {
        return 0;
    }
    uint32_t count = 0;
    for (uint32_t bit = 0; bit <= LAST_BITMAP_BIT; ++bit)
    {
        if ((bitmap & (1u << bit)) == 0)
        {
            continue;
        }
        if (count == capacity)
        {
            return 0;
        }
        labelsOut[count++] = bit + 1u;
    }
    return count;
}

// Whether one of the first matched stream channels has the device
// channel.
MAUD_NODISCARD static bool Taken(const uint32_t* deviceOut, const bool* matched, uint32_t count,
                                 uint32_t device)
{
    for (uint32_t c = 0; c < count; ++c)
    {
        if (matched[c] && deviceOut[c] == device)
        {
            return true;
        }
    }
    return false;
}

// The first device channel, not yet taken, with one of the labels in
// the order given, or labelCount.
static uint32_t Find(const uint32_t* choices, const uint32_t* labels, uint32_t labelCount,
                     const uint32_t* deviceOut, const bool* matched, uint32_t count)
{
    for (uint32_t k = 0; k < MAX_CHOICES && choices[k] != NO_LABEL; ++k)
    {
        for (uint32_t device = 0; device < labelCount; ++device)
        {
            if (labels[device] == choices[k] && !Taken(deviceOut, matched, count, device))
            {
                return device;
            }
        }
    }
    return labelCount;
}

bool maudCoreAudioChannelsByLabel(maudChannelLayout layout, const uint32_t* labels,
                                  uint32_t labelCount, uint32_t* deviceOut)
{
    uint32_t count = maudGetLayoutChannelCount(layout);
    if (count == 0 || count > MAUD_LABEL_MAX_STREAM_CHANNELS ||
        (labelCount > 0 && labels == nullptr))
    {
        return false;
    }
    bool matched[MAUD_LABEL_MAX_STREAM_CHANNELS] = {false};
    bool backs = HasBacks(layout, count);
    // Every speaker by the labels that name it first, so that a label
    // standing in for one speaker never takes another's own.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (uint32_t c = 0; c < count; ++c)
        {
            if (matched[c])
            {
                continue;
            }
            Choices choices = ChoicesOf(maudGetLayoutSpeaker(layout, c), backs);
            uint32_t device = Find(pass == 0 ? choices.exact : choices.stand, labels, labelCount,
                                   deviceOut, matched, count);
            if (device < labelCount)
            {
                deviceOut[c] = device;
                matched[c] = true;
            }
        }
    }
    for (uint32_t c = 0; c < count; ++c)
    {
        if (!matched[c])
        {
            return false;
        }
    }
    return true;
}

maudChannelLayout maudCoreAudioLayoutOfLabels(const uint32_t* labels, uint32_t labelCount)
{
    static const maudChannelLayout largestFirst[] = {
        maud_layout7Point1Point4, maud_layout7Point1, maud_layout5Point1,
        maud_layoutQuad,          maud_layoutStereo,  maud_layoutMono,
    };
    uint32_t deviceOf[MAUD_LABEL_MAX_STREAM_CHANNELS];
    for (size_t i = 0; i < sizeof(largestFirst) / sizeof(largestFirst[0]); ++i)
    {
        if (maudCoreAudioChannelsByLabel(largestFirst[i], labels, labelCount, deviceOf))
        {
            return largestFirst[i];
        }
    }
    return maud_layoutNone;
}

bool maudCoreAudioChannelsInOrder(const uint32_t* deviceOf, uint32_t count)
{
    for (uint32_t c = 0; c < count; ++c)
    {
        if (deviceOf[c] != c)
        {
            return false;
        }
    }
    return true;
}

bool maudCoreAudioChannelMapOf(const uint32_t* deviceOf, uint32_t count, uint32_t deviceChannels,
                               bool output, int32_t* mapOut)
{
    for (uint32_t c = 0; c < count; ++c)
    {
        if (deviceOf[c] >= deviceChannels || deviceOf[c] > INT32_MAX)
        {
            return false;
        }
    }
    if (!output)
    {
        for (uint32_t c = 0; c < count; ++c)
        {
            mapOut[c] = (int32_t)deviceOf[c];
        }
        return true;
    }
    for (uint32_t device = 0; device < deviceChannels; ++device)
    {
        mapOut[device] = -1;
    }
    for (uint32_t c = 0; c < count; ++c)
    {
        mapOut[deviceOf[c]] = (int32_t)c;
    }
    return true;
}
