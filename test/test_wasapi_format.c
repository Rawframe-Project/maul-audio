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
    // Counter times: 100 ns units, a second either side of now at most.
    const int64_t now = 50000000000;
    CHECK(maudWasapiCounterTime(500000000, now, -1) == now, "a counter time read now");
    CHECK(maudWasapiCounterTime(495000000, now, -1) == now - 500000000, "half a second ago");
    CHECK(maudWasapiCounterTime(0, now, -1) == -1, "no counter time");
    CHECK(maudWasapiCounterTime(489999999, now, -1) == -1 &&
              maudWasapiCounterTime(510000001, now, -1) == -1,
          "more than a second off");
    CHECK(maudWasapiCounterTime(490000000, now, -1) == -1 &&
              maudWasapiCounterTime(510000000, now, -1) == -1,
          "a second off exactly");
    CHECK(maudWasapiCounterTime(0, 0, -1) == -1, "0 even at time 0");
    return s_failures == 0 ? 0 : 1;
}
