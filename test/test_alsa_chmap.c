// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// ALSA channel orders against the stream's layouts.

#include "alsa_chmap.h"
#include "test_harness.h"

#include <alsa/asoundlib.h>
#include <string.h>

static bool Same(const uint8_t* order, const uint8_t* expected, uint32_t count)
{
    return memcmp(order, expected, count) == 0;
}

// Frames move between the stream's order and the PCM's, both ways, and
// only the frames asked for.
static void TestReorder(void)
{
    // A PCM in the order FR FL C: pcm[0] carries stream channel 1.
    const uint8_t order[3] = {1, 0, 2};
    float stream[2 * 3] = {1, 2, 3, 4, 5, 6};
    float pcm[3 * 3] = {0};
    pcm[6] = pcm[7] = pcm[8] = -1.0f;
    maudAlsaReorder(order, 3, 2, stream, pcm, true);
    const float played[9] = {2, 1, 3, 5, 4, 6, -1, -1, -1};
    CHECK(memcmp(pcm, played, sizeof(played)) == 0, "playback into the PCM's order");
    float captured[2 * 3] = {0};
    maudAlsaReorder(order, 3, 2, captured, pcm, false);
    const float back[6] = {1, 2, 3, 4, 5, 6};
    CHECK(memcmp(captured, back, sizeof(back)) == 0, "capture back into the stream's");
}

int main(void)
{
    TestReorder();
    uint8_t order[MAUD_ALSA_MAX_CHANNELS];
    static const uint8_t identity[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    CHECK(maudAlsaChannelOrder(maud_layoutStereo, nullptr, 2, order) && Same(order, identity, 2),
          "stereo in the standard order");
    CHECK(maudAlsaOrderIsIdentity(order, 2), "is the identity");
    static const unsigned int mono[] = {SND_CHMAP_MONO};
    CHECK(maudAlsaChannelOrder(maud_layoutMono, mono, 1, order) && order[0] == 0, "mono");
    static const uint8_t fivePointOne[] = {0, 1, 4, 5, 2, 3};
    CHECK(maudAlsaChannelOrder(maud_layout5Point1, nullptr, 6, order) &&
              Same(order, fivePointOne, 6),
          "5.1 from ALSA's order, its rear pair as the sides");
    CHECK(!maudAlsaOrderIsIdentity(order, 6), "is not the identity");
    static const uint8_t sevenPointOne[] = {0, 1, 4, 5, 2, 3, 6, 7};
    CHECK(maudAlsaChannelOrder(maud_layout7Point1, nullptr, 8, order) &&
              Same(order, sevenPointOne, 8),
          "7.1 from ALSA's order");
    CHECK(maudAlsaChannelOrder(maud_layoutQuad, nullptr, 4, order) && Same(order, identity, 4),
          "quad matches");
    static const unsigned int swapped[] = {SND_CHMAP_FR, SND_CHMAP_FL};
    static const uint8_t reversed[] = {1, 0};
    CHECK(maudAlsaChannelOrder(maud_layoutStereo, swapped, 2, order) && Same(order, reversed, 2),
          "a reported map is followed");
    static const unsigned int sides[] = {SND_CHMAP_FL, SND_CHMAP_FR, SND_CHMAP_SL, SND_CHMAP_SR};
    CHECK(maudAlsaChannelOrder(maud_layoutQuad, sides, 4, order) && Same(order, identity, 4),
          "sides stand in for rears");
    static const unsigned int unknown[] = {SND_CHMAP_FL, SND_CHMAP_TC};
    CHECK(!maudAlsaChannelOrder(maud_layoutStereo, unknown, 2, order), "a position with no match");
    static const unsigned int twice[] = {SND_CHMAP_FL, SND_CHMAP_FL};
    CHECK(!maudAlsaChannelOrder(maud_layoutStereo, twice, 2, order), "a position used twice");
    CHECK(!maudAlsaChannelOrder(maud_layoutStereo, nullptr, 6, order), "a count not the layout's");
    CHECK(maudAlsaChannelOrder(maud_layout7Point1Point4, nullptr, 12, order) &&
              Same(order, identity, 12),
          "no standard order: the stream's own");
    return s_failures == 0 ? 0 : 1;
}
