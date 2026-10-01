// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Speaker masks against layouts.

#include "test_harness.h"
#include "wasapi_format.h"

int main(void)
{
    CHECK(maudWasapiMaskOfLayout(maud_layoutNone) == 0, "no layout, no mask");
    CHECK(maudWasapiMaskOfLayout(maud_layoutMono) == 0x4, "mono is the front center");
    CHECK(maudWasapiMaskOfLayout(maud_layoutStereo) == 0x3, "KSAUDIO_SPEAKER_STEREO");
    CHECK(maudWasapiMaskOfLayout(maud_layoutQuad) == 0x33, "KSAUDIO_SPEAKER_QUAD");
    CHECK(maudWasapiMaskOfLayout(maud_layout5Point1) == 0x60F, "KSAUDIO_SPEAKER_5POINT1_SURROUND");
    CHECK(maudWasapiMaskOfLayout(maud_layout7Point1) == 0x63F, "KSAUDIO_SPEAKER_7POINT1_SURROUND");
    CHECK(maudWasapiMaskOfLayout(maud_layout7Point1Point4) == 0x2D63F, "7.1.4");
    for (uint32_t layout = maud_layoutMono; layout <= maud_layout7Point1Point4; ++layout)
    {
        CHECK(maudWasapiLayoutOfFormat(maudWasapiMaskOfLayout((maudChannelLayout)layout),
                                       maudGetLayoutChannelCount((maudChannelLayout)layout)) ==
                  layout,
              "each mask names its layout");
    }
    CHECK(maudWasapiLayoutOfFormat(0x3F, 6) == maud_layout5Point1,
          "5.1 with back speakers falls back to its channel count");
    CHECK(maudWasapiLayoutOfFormat(0, 2) == maud_layoutStereo, "no mask: by channel count");
    CHECK(maudWasapiLayoutOfFormat(0x7, 3) == maud_layoutNone, "no layout of three channels");
    return s_failures == 0 ? 0 : 1;
}
