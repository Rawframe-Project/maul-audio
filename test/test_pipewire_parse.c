// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What the PipeWire backend reads out of the daemon's objects, without a
// daemon: a channel choice's default with other values after it, a
// node's card and profile device, a default's node name out of the
// metadata's JSON (and what is not that JSON), the graph's rate, and a
// stream's format positions and media properties.

#include "pipewire_parse.h"
#include "test_harness.h"

#include <pipewire/keys.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <string.h>

static void TestChoice(void)
{
    uint8_t buffer[256];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_pod_frame frame;
    spa_pod_builder_push_choice(&builder, &frame, SPA_CHOICE_Range, 0);
    spa_pod_builder_int(&builder, 2);
    spa_pod_builder_int(&builder, 1);
    spa_pod_builder_int(&builder, 8);
    const struct spa_pod* range = spa_pod_builder_pop(&builder, &frame);
    CHECK(maudPipewireChoiceDefault(range) == 2, "a range's default");
    struct spa_pod_builder plain = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod_builder_int(&plain, 6);
    CHECK(maudPipewireChoiceDefault((const struct spa_pod*)buffer) == 6, "a plain Int");
    struct spa_pod_builder text = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_pod_builder_string(&text, "6");
    CHECK(maudPipewireChoiceDefault((const struct spa_pod*)buffer) == 0, "not an Int");
}

static void TestCard(void)
{
    struct spa_dict_item items[2] = {SPA_DICT_ITEM_INIT(PW_KEY_DEVICE_ID, "42"),
                                     SPA_DICT_ITEM_INIT("card.profile.device", "3")};
    struct spa_dict both = SPA_DICT_INIT(items, 2);
    uint32_t card = 7;
    int32_t device = 7;
    CHECK(maudPipewireNodeCard(&both, &card, &device) && card == 42 && device == 3,
          "a node on a card's profile device");
    struct spa_dict onlyCard = SPA_DICT_INIT(items, 1);
    CHECK(!maudPipewireNodeCard(&onlyCard, &card, &device), "a card without a profile device");
    struct spa_dict onlyDevice = SPA_DICT_INIT(items + 1, 1);
    CHECK(!maudPipewireNodeCard(&onlyDevice, &card, &device), "a profile device without a card");
    struct spa_dict_item bad[2] = {SPA_DICT_ITEM_INIT(PW_KEY_DEVICE_ID, "4x"),
                                   SPA_DICT_ITEM_INIT("card.profile.device", "3")};
    struct spa_dict partial = SPA_DICT_INIT(bad, 2);
    CHECK(!maudPipewireNodeCard(&partial, &card, &device), "an id that does not read whole");
}

static bool NameIs(const char* value, const char* expected)
{
    char name[16];
    memset(name, 'x', sizeof(name));
    maudPipewireParseDefaultName(value, name, sizeof(name));
    return strcmp(name, expected) == 0;
}

static void TestDefaultName(void)
{
    CHECK(NameIs("{\"name\": \"sink\"}", "sink"), "the name");
    CHECK(NameIs("{\"x\": 1, \"name\": \"sink\"}", "sink"), "the name after another key");
    CHECK(NameIs("{\"x\": {\"name\": \"no\"}, \"name\": \"sink\"}", "sink"),
          "not a nested object's name");
    CHECK(NameIs("{\"x\": 1}", ""), "no name");
    CHECK(NameIs("\"name\"", ""), "not an object");
    CHECK(NameIs("[\"name\", \"sink\"]", ""), "an array");
    CHECK(NameIs("{\"x\"", ""), "a key without a value");
    CHECK(NameIs(nullptr, ""), "no value");
    CHECK(NameIs("{\"name\": \"a-name-far-too-long\"}", ""), "a name past the buffer");
}

static void TestGraphRate(void)
{
    CHECK(maudPipewireGraphRate(96000, 44100, 48000) == 96000, "the forced rate first");
    CHECK(maudPipewireGraphRate(0, 44100, 48000) == 44100, "then the clock's");
    CHECK(maudPipewireGraphRate(0, 0, 48000) == 48000, "then the fallback");
}

// The positions a format names, from its pod.
static bool Positions(maudChannelLayout layout, const uint32_t* expected, uint32_t count)
{
    uint8_t buffer[1024];
    const struct spa_pod* format = maudPipewireBuildFormat(layout, 44100, buffer, sizeof(buffer));
    struct spa_audio_info_raw info = {0};
    if (format == nullptr || spa_format_audio_raw_parse(format, &info) < 0 ||
        info.format != SPA_AUDIO_FORMAT_F32 || info.rate != 44100 || info.channels != count)
    {
        return false;
    }
    return memcmp(info.position, expected, count * sizeof(uint32_t)) == 0;
}

static void TestFormat(void)
{
    const uint32_t mono[1] = {SPA_AUDIO_CHANNEL_MONO};
    const uint32_t stereo[2] = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR};
    const uint32_t surround[6] = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                  SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                                  SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR};
    CHECK(Positions(maud_layoutMono, mono, 1), "mono is MONO");
    CHECK(Positions(maud_layoutStereo, stereo, 2), "stereo is FL FR");
    CHECK(Positions(maud_layout5Point1, surround, 6), "5.1 in its order");
    CHECK(strcmp(maudPipewireMediaCategory(maud_directionOutput), "Playback") == 0 &&
              strcmp(maudPipewireMediaCategory(maud_directionInput), "Capture") == 0,
          "the media category");
    CHECK(strcmp(maudPipewireMediaRole(maud_roleCommunications), "Communication") == 0 &&
              strcmp(maudPipewireMediaRole(maud_roleGeneral), "Game") == 0,
          "the media role");
}

int main(void)
{
    TestChoice();
    TestCard();
    TestDefaultName();
    TestGraphRate();
    TestFormat();
    return s_failures == 0 ? 0 : 1;
}
