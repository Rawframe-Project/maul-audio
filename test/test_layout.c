// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Channel layouts: counts, speakers in the Windows mask order, and the
// BS.2051 positions.

#include "test_harness.h"

#include "maul-audio/layout.h"

static void TestChannelCounts(void)
{
    CHECK(maudGetLayoutChannelCount(maud_layoutNone) == 0, "none has no channels");
    CHECK(maudGetLayoutChannelCount(maud_layoutMono) == 1, "mono");
    CHECK(maudGetLayoutChannelCount(maud_layoutStereo) == 2, "stereo");
    CHECK(maudGetLayoutChannelCount(maud_layoutQuad) == 4, "quad");
    CHECK(maudGetLayoutChannelCount(maud_layout5Point1) == 6, "5.1");
    CHECK(maudGetLayoutChannelCount(maud_layout7Point1) == 8, "7.1");
    CHECK(maudGetLayoutChannelCount(maud_layout7Point1Point4) == 12, "7.1.4");
    CHECK(maudGetLayoutChannelCount(7) == 0, "past the last layout");
    CHECK(maudGetLayoutChannelCount(255) == 0, "far past the last layout");
}

static void TestSpeakersFollowTheMaskOrder(void)
{
    static const maudSpeaker expected[] = {
        maud_speakerFrontLeft,     maud_speakerFrontRight,  maud_speakerFrontCenter,
        maud_speakerLowFrequency,  maud_speakerBackLeft,    maud_speakerBackRight,
        maud_speakerSideLeft,      maud_speakerSideRight,   maud_speakerTopFrontLeft,
        maud_speakerTopFrontRight, maud_speakerTopBackLeft, maud_speakerTopBackRight,
    };
    for (uint32_t c = 0; c < 12; ++c)
    {
        CHECK(maudGetLayoutSpeaker(maud_layout7Point1Point4, c) == expected[c], "7.1.4 order");
    }
    for (uint32_t c = 0; c < 8; ++c)
    {
        CHECK(maudGetLayoutSpeaker(maud_layout7Point1, c) == expected[c], "7.1 order");
    }
    CHECK(maudGetLayoutSpeaker(maud_layout5Point1, 3) == maud_speakerLowFrequency, "5.1 lfe");
    CHECK(maudGetLayoutSpeaker(maud_layout5Point1, 4) == maud_speakerSideLeft, "5.1 side left");
    CHECK(maudGetLayoutSpeaker(maud_layout5Point1, 5) == maud_speakerSideRight, "5.1 side right");
    CHECK(maudGetLayoutSpeaker(maud_layoutQuad, 2) == maud_speakerBackLeft, "quad back left");
    CHECK(maudGetLayoutSpeaker(maud_layoutMono, 0) == maud_speakerFrontCenter, "mono center");
}

static void TestOutOfRangeChannelsReportNothing(void)
{
    CHECK(maudGetLayoutSpeaker(maud_layoutStereo, 2) == maud_speakerNone, "past stereo");
    CHECK(maudGetLayoutSpeaker(maud_layout7Point1Point4, 12) == maud_speakerNone, "past 7.1.4");
    CHECK(maudGetLayoutSpeaker(maud_layoutNone, 0) == maud_speakerNone, "none layout");
    CHECK(maudGetLayoutSpeaker(200, 0) == maud_speakerNone, "unknown layout");
    maudSpeakerPosition position = maudGetLayoutSpeakerPosition(maud_layoutQuad, 4);
    CHECK(position.azimuthDegrees == 0.0f && position.elevationDegrees == 0.0f, "past quad");
    position = maudGetLayoutSpeakerPosition(9, 0);
    CHECK(position.azimuthDegrees == 0.0f && position.elevationDegrees == 0.0f, "unknown");
}

static bool At(maudChannelLayout layout, uint32_t channel, float azimuth, float elevation)
{
    maudSpeakerPosition position = maudGetLayoutSpeakerPosition(layout, channel);
    return position.azimuthDegrees == azimuth && position.elevationDegrees == elevation;
}

static void TestPositionsFollowBs2051(void)
{
    CHECK(At(maud_layoutStereo, 0, 30.0f, 0.0f), "stereo left at +30");
    CHECK(At(maud_layoutStereo, 1, -30.0f, 0.0f), "stereo right at -30");
    CHECK(At(maud_layout5Point1, 4, 110.0f, 0.0f), "5.1 left surround at +110");
    CHECK(At(maud_layout5Point1, 5, -110.0f, 0.0f), "5.1 right surround at -110");
    CHECK(At(maud_layout5Point1, 3, 0.0f, 0.0f), "lfe has no direction");
    CHECK(At(maud_layout7Point1, 4, 135.0f, 0.0f), "7.1 back left at +135");
    CHECK(At(maud_layout7Point1, 7, -90.0f, 0.0f), "7.1 side right at -90");
    CHECK(At(maud_layout7Point1Point4, 8, 45.0f, 30.0f), "top front left");
    CHECK(At(maud_layout7Point1Point4, 11, -135.0f, 30.0f), "top back right");
    CHECK(At(maud_layoutQuad, 3, -135.0f, 0.0f), "quad back right");
}

int main(void)
{
    TestChannelCounts();
    TestSpeakersFollowTheMaskOrder();
    TestOutOfRangeChannelsReportNothing();
    TestPositionsFollowBs2051();
    return s_failures == 0 ? 0 : 1;
}
