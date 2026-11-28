// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Core Audio channel layouts against the stream's layouts, by their
// labels' values, on every platform.

#include "coreaudio_labels.h"
#include "test_harness.h"

#include <string.h>

enum
{
    L = MAUD_LABEL_LEFT,
    R = MAUD_LABEL_RIGHT,
    C = MAUD_LABEL_CENTER,
    LFE = MAUD_LABEL_LFE_SCREEN,
    LS = MAUD_LABEL_LEFT_SURROUND,
    RS = MAUD_LABEL_RIGHT_SURROUND,
    LSD = MAUD_LABEL_LEFT_SURROUND_DIRECT,
    RSD = MAUD_LABEL_RIGHT_SURROUND_DIRECT,
    RLS = MAUD_LABEL_REAR_SURROUND_LEFT,
    RRS = MAUD_LABEL_REAR_SURROUND_RIGHT,
    VHL = MAUD_LABEL_VERTICAL_HEIGHT_LEFT,
    VHR = MAUD_LABEL_VERTICAL_HEIGHT_RIGHT,
    LTR = MAUD_LABEL_LEFT_TOP_REAR,
    RTR = MAUD_LABEL_RIGHT_TOP_REAR,
    MONO = MAUD_LABEL_MONO,
    UNUSED = 0,
    UNKNOWN = 0xFFFFFFFFu,
};

// The kAudioChannelBit_* of a label from Left to TopBackRight.
#define BIT(label) (1u << ((label) - 1u))

// Whether the layout's channels, against the labels as an
// AudioChannelLayout of descriptions, go to the device channels
// expected.
static bool Maps(maudChannelLayout layout, const uint32_t* labels, uint32_t count,
                 const uint32_t* expected)
{
    uint32_t read[16];
    uint32_t deviceOf[MAUD_LABEL_MAX_STREAM_CHANNELS];
    uint32_t n = maudCoreAudioLabelsOf(MAUD_LABEL_TAG_DESCRIPTIONS, 0, labels, count, read, 16);
    return n == count && maudCoreAudioChannelsByLabel(layout, read, n, deviceOf) &&
           memcmp(deviceOf, expected, maudGetLayoutChannelCount(layout) * sizeof(uint32_t)) == 0;
}

// Whether no channel order is found for the layout against the labels.
static bool Refused(maudChannelLayout layout, const uint32_t* labels, uint32_t count)
{
    uint32_t deviceOf[MAUD_LABEL_MAX_STREAM_CHANNELS];
    return !maudCoreAudioChannelsByLabel(layout, labels, count, deviceOf);
}

// 7.1 as Core Audio lists it, sides before backs (MPEG 7.1 C, the
// AudioUnit 7.1 layout), and as WAV does; 7.1.4 as Atmos lists it.
static void TestSevenOne(void)
{
    static const uint32_t sidesFirst[] = {L, R, C, LFE, LS, RS, RLS, RRS};
    static const uint32_t sidesFirstMap[] = {0, 1, 2, 3, 6, 7, 4, 5};
    CHECK(Maps(maud_layout7Point1, sidesFirst, 8, sidesFirstMap),
          "7.1 sides before backs: the backs on the rear surrounds");
    static const uint32_t wave[] = {L, R, C, LFE, RLS, RRS, LS, RS};
    static const uint32_t identity[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    CHECK(Maps(maud_layout7Point1, wave, 8, identity), "7.1 in WAV's order");
    static const uint32_t direct[] = {L, R, C, LFE, LS, RS, LSD, RSD};
    static const uint32_t directMap[] = {0, 1, 2, 3, 4, 5, 6, 7};
    CHECK(Maps(maud_layout7Point1, direct, 8, directMap),
          "7.1 with direct sides: the surrounds stand in for the backs");
    // A device that labels its channels in Core Audio's label order
    // from Left, as BlackHole does.
    uint32_t counting[16];
    for (uint32_t i = 0; i < 16; ++i)
    {
        counting[i] = L + i;
    }
    static const uint32_t countingMap[] = {0, 1, 2, 3, 4, 5, 9, 10};
    CHECK(Maps(maud_layout7Point1, counting, 16, countingMap),
          "7.1 on labels counted from Left: the sides on the side directs");
    CHECK(Refused(maud_layout7Point1Point4, counting, 16),
          "7.1.4 on labels counted from Left: no top back right");
    static const uint32_t atmos[] = {L, R, C, LFE, LS, RS, RLS, RRS, VHL, VHR, LTR, RTR};
    static const uint32_t atmosMap[] = {0, 1, 2, 3, 6, 7, 4, 5, 8, 9, 10, 11};
    CHECK(Maps(maud_layout7Point1Point4, atmos, 12, atmosMap), "7.1.4 as Atmos lists it");
    static const uint32_t twoSurrounds[] = {L, R, C, LFE, LS, RS, UNKNOWN, UNKNOWN};
    CHECK(Refused(maud_layout7Point1, twoSurrounds, 8),
          "7.1 against one pair of surrounds: each label serves one channel");
}

// 5.1 in Core Audio's orders, quad, and the surrounds standing in for
// the sides or the backs whichever the stream has.
static void TestFiveOne(void)
{
    static const uint32_t a[] = {L, R, C, LFE, LS, RS};
    static const uint32_t identity[] = {0, 1, 2, 3, 4, 5};
    CHECK(Maps(maud_layout5Point1, a, 6, identity), "5.1 A: the stream's own order");
    static const uint32_t c[] = {L, C, R, LS, RS, LFE};
    static const uint32_t cMap[] = {0, 2, 1, 5, 3, 4};
    CHECK(Maps(maud_layout5Point1, c, 6, cMap), "5.1 C: center second, LFE last");
    static const uint32_t rears[] = {L, R, C, LFE, RLS, RRS};
    CHECK(Maps(maud_layout5Point1, rears, 6, identity), "5.1 sides on rear surrounds");
    static const uint32_t both[] = {L, R, C, LFE, LSD, RSD, LS, RS};
    static const uint32_t bothMap[] = {0, 1, 2, 3, 6, 7};
    CHECK(Maps(maud_layout5Point1, both, 8, bothMap),
          "5.1 sides on the surrounds before the side directs");
    static const uint32_t quad[] = {L, R, LS, RS};
    CHECK(Maps(maud_layoutQuad, quad, 4, identity), "quad backs on the surrounds");
    static const uint32_t stereoAt[] = {UNUSED, UNUSED, L, R};
    static const uint32_t stereoAtMap[] = {2, 3};
    CHECK(Maps(maud_layoutStereo, stereoAt, 4, stereoAtMap),
          "stereo on a wider device: the channels labeled left and right");
}

static void TestMono(void)
{
    static const uint32_t mono[] = {MONO};
    static const uint32_t center[] = {C};
    static const uint32_t zero[] = {0};
    CHECK(Maps(maud_layoutMono, mono, 1, zero), "mono on a mono label");
    CHECK(Maps(maud_layoutMono, center, 1, zero), "mono on a center label");
    static const uint32_t stereo[] = {L, R};
    CHECK(Refused(maud_layoutMono, stereo, 2), "mono against left and right: no center");
}

// A bitmap lists its channels by bit, lowest first.
static void TestBitmap(void)
{
    uint32_t labels[8];
    uint32_t n = maudCoreAudioLabelsOf(MAUD_LABEL_TAG_BITMAP,
                                       BIT(L) | BIT(R) | BIT(C) | BIT(LFE) | BIT(LS) | BIT(RS),
                                       nullptr, 0, labels, 8);
    static const uint32_t fiveOne[] = {L, R, C, LFE, LS, RS};
    CHECK(n == 6 && memcmp(labels, fiveOne, sizeof(fiveOne)) == 0, "a 5.1 bitmap");
    uint32_t deviceOf[MAUD_LABEL_MAX_STREAM_CHANNELS];
    static const uint32_t identity[] = {0, 1, 2, 3, 4, 5};
    CHECK(maudCoreAudioChannelsByLabel(maud_layout5Point1, labels, n, deviceOf) &&
              memcmp(deviceOf, identity, sizeof(identity)) == 0,
          "5.1 against a 5.1 bitmap");
    n = maudCoreAudioLabelsOf(MAUD_LABEL_TAG_BITMAP, BIT(L) | BIT(R) | BIT(LS) | BIT(RS), nullptr,
                              0, labels, 8);
    CHECK(maudCoreAudioChannelsByLabel(maud_layoutQuad, labels, n, deviceOf) &&
              memcmp(deviceOf, identity, 4 * sizeof(uint32_t)) == 0,
          "quad against a quad bitmap");
    CHECK(maudCoreAudioLabelsOf(MAUD_LABEL_TAG_BITMAP, 1u << 18, nullptr, 0, labels, 8) == 0,
          "a bitmap past top back right: not read");
    CHECK(maudCoreAudioLabelsOf(MAUD_LABEL_TAG_BITMAP, BIT(L) | BIT(R) | BIT(C), nullptr, 0, labels,
                                2) == 0,
          "a bitmap past the room: not read");
}

// A tag the caller did not expand, and labels that name nothing.
static void TestUnknown(void)
{
    uint32_t labels[8];
    static const uint32_t some[] = {L, R};
    CHECK(maudCoreAudioLabelsOf((128u << 16) | 8u, 0, some, 2, labels, 8) == 0,
          "a named tag: the platform expands it");
    CHECK(maudCoreAudioLabelsOf(MAUD_LABEL_TAG_DESCRIPTIONS, 0, some, 2, labels, 1) == 0,
          "descriptions past the room: not read");
    CHECK(Refused(maud_layoutStereo, nullptr, 0), "no labels");
    static const uint32_t unknown[] = {UNKNOWN, UNKNOWN};
    CHECK(Refused(maud_layoutStereo, unknown, 2), "unknown labels");
    static const uint32_t discrete[] = {(1u << 16) | 0u, (1u << 16) | 1u};
    CHECK(Refused(maud_layoutStereo, discrete, 2), "discrete labels");
    CHECK(Refused(maud_layoutNone, some, 2), "no layout");
}

// The unit's channel map from the device channel of each stream
// channel, and the order that needs none.
static void TestChannelMap(void)
{
    static const uint32_t deviceOf[] = {0, 1, 2, 3, 6, 7, 4, 5};
    int32_t map[10];
    CHECK(maudCoreAudioChannelMapOf(deviceOf, 8, 10, true, map), "an output map");
    static const int32_t played[] = {0, 1, 2, 3, 6, 7, 4, 5, -1, -1};
    CHECK(memcmp(map, played, sizeof(played)) == 0,
          "output: each device channel's stream channel, -1 for those it lacks");
    static const uint32_t stereoAt[] = {2, 3};
    CHECK(maudCoreAudioChannelMapOf(stereoAt, 2, 4, true, map), "a wider device's output map");
    static const int32_t wide[] = {-1, -1, 0, 1};
    CHECK(memcmp(map, wide, sizeof(wide)) == 0, "output: the device's other channels silent");
    CHECK(maudCoreAudioChannelMapOf(stereoAt, 2, 4, false, map) && map[0] == 2 && map[1] == 3,
          "input: each stream channel's device channel");
    CHECK(!maudCoreAudioChannelMapOf(deviceOf, 8, 6, true, map),
          "a device channel past the device's");
    static const uint32_t identity[] = {0, 1, 2, 3};
    CHECK(maudCoreAudioChannelsInOrder(identity, 4), "in order");
    CHECK(!maudCoreAudioChannelsInOrder(deviceOf, 8), "out of order");
}

int main(void)
{
    TestSevenOne();
    TestFiveOne();
    TestMono();
    TestBitmap();
    TestUnknown();
    TestChannelMap();
    return s_failures == 0 ? 0 : 1;
}
