// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// PipeWire card routes without a card: Route params built as a card's
// Device emits them are read into a direction, a profile device and the
// form of the port type, and a node on that profile device takes the
// route's form over its own form factor.

#include "pipewire_card.h"
#include "test_harness.h"

#include <spa/param/route.h>
#include <spa/pod/builder.h>

// Builds a Route param in buffer: direction, profile device, and an info
// struct with a port type, or none for NULL.
static const struct spa_pod* BuildRoute(uint8_t* buffer, size_t size, enum spa_direction direction,
                                        int32_t device, const char* portType)
{
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, (uint32_t)size);
    struct spa_pod_frame object;
    struct spa_pod_frame info;
    spa_pod_builder_push_object(&builder, &object, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);
    spa_pod_builder_add(&builder, SPA_PARAM_ROUTE_index, SPA_POD_Int(0), SPA_PARAM_ROUTE_direction,
                        SPA_POD_Id(direction), SPA_PARAM_ROUTE_device, SPA_POD_Int(device), 0);
    if (portType != nullptr)
    {
        spa_pod_builder_prop(&builder, SPA_PARAM_ROUTE_info, 0);
        spa_pod_builder_push_struct(&builder, &info);
        spa_pod_builder_int(&builder, 2);
        spa_pod_builder_string(&builder, "port.availability-group");
        spa_pod_builder_string(&builder, "Legacy 1");
        spa_pod_builder_string(&builder, "port.type");
        spa_pod_builder_string(&builder, portType);
        spa_pod_builder_pop(&builder, &info);
    }
    return spa_pod_builder_pop(&builder, &object);
}

static void TestRead(void)
{
    uint8_t buffer[1024];
    maudPipewireRoute route = {0};
    CHECK(maudPipewireReadRoute(
              BuildRoute(buffer, sizeof(buffer), SPA_DIRECTION_OUTPUT, 3, "headphones"), &route) &&
              route.device == 3 && route.direction == maud_directionOutput &&
              route.form == maud_formHeadphones,
          "a playback route to headphones");
    CHECK(maudPipewireReadRoute(BuildRoute(buffer, sizeof(buffer), SPA_DIRECTION_INPUT, 4, "mic"),
                                &route) &&
              route.device == 4 && route.direction == maud_directionInput &&
              route.form == maud_formMicrophone,
          "a capture route from a microphone");
    CHECK(maudPipewireReadRoute(
              BuildRoute(buffer, sizeof(buffer), SPA_DIRECTION_OUTPUT, 5, nullptr), &route) &&
              route.form == maud_formUnknown,
          "a route without info has no form");
    CHECK(maudPipewireReadRoute(BuildRoute(buffer, sizeof(buffer), SPA_DIRECTION_OUTPUT, 6, "usb"),
                                &route) &&
              route.form == maud_formUnknown,
          "a connection port type says nothing");
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    struct spa_pod_frame frame;
    spa_pod_builder_push_object(&builder, &frame, SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile);
    CHECK(!maudPipewireReadRoute(spa_pod_builder_pop(&builder, &frame), &route) &&
              !maudPipewireReadRoute(nullptr, &route),
          "not a route");
}

static void TestNodeForm(void)
{
    maudPipewireCard card = {.globalId = 40, .routeCount = 3, .used = true};
    card.routes[0] = (maudPipewireRoute){3, maud_directionOutput, maud_formHeadphones};
    card.routes[1] = (maudPipewireRoute){4, maud_directionInput, maud_formUnknown};
    card.routes[2] = (maudPipewireRoute){5, maud_directionInput, maud_formHeadset};
    maudPipewire pipewire = {.cards = &card, .cardCapacity = 1};
    maudPipewireNode node = {.direction = maud_directionOutput,
                             .factorForm = maud_formSpeakers,
                             .hasCard = true,
                             .cardId = 40,
                             .profileDevice = 3};
    CHECK(maudPipewireNodeForm(&pipewire, &node) == maud_formHeadphones,
          "a node takes its route's form over its form factor");
    node.profileDevice = 7;
    CHECK(maudPipewireNodeForm(&pipewire, &node) == maud_formSpeakers,
          "another profile device keeps its form factor");
    node.profileDevice = 5;
    CHECK(maudPipewireNodeForm(&pipewire, &node) == maud_formSpeakers,
          "and so does a route of the other direction");
    node.profileDevice = 4;
    node.direction = maud_directionInput;
    node.factorForm = maud_formMicrophone;
    CHECK(maudPipewireNodeForm(&pipewire, &node) == maud_formMicrophone,
          "a route of unknown form says nothing");
    node = (maudPipewireNode){.direction = maud_directionOutput,
                              .factorForm = maud_formLine,
                              .hasCard = true,
                              .cardId = 41,
                              .profileDevice = 3};
    CHECK(maudPipewireNodeForm(&pipewire, &node) == maud_formLine, "an unknown card");
    node.cardId = 40;
    node.hasCard = false;
    CHECK(maudPipewireNodeForm(&pipewire, &node) == maud_formLine, "a node on no card");
}

// A card keeps one route per profile device and direction: a jack
// switch replaces it, and a full card refuses another.
static void TestStore(void)
{
    maudPipewireCard card = {0};
    maudPipewireRoute speakers = {3, maud_directionOutput, maud_formSpeakers};
    maudPipewireRoute mic = {3, maud_directionInput, maud_formMicrophone};
    CHECK(maudPipewireStoreRoute(&card, &speakers) && maudPipewireStoreRoute(&card, &mic) &&
              card.routeCount == 2,
          "a route per direction of one profile device");
    maudPipewireRoute headphones = {3, maud_directionOutput, maud_formHeadphones};
    CHECK(maudPipewireStoreRoute(&card, &headphones) && card.routeCount == 2 &&
              card.routes[0].form == maud_formHeadphones &&
              card.routes[1].form == maud_formMicrophone,
          "a jack switch replaces the output's route only");
    for (int32_t device = 10; card.routeCount < MAUD_PIPEWIRE_CARD_ROUTES; ++device)
    {
        maudPipewireRoute other = {device, maud_directionOutput, maud_formLine};
        CHECK(maudPipewireStoreRoute(&card, &other), "more profile devices");
    }
    maudPipewireRoute extra = {99, maud_directionOutput, maud_formLine};
    CHECK(!maudPipewireStoreRoute(&card, &extra) && card.routeCount == MAUD_PIPEWIRE_CARD_ROUTES,
          "a full card refuses another");
    CHECK(maudPipewireStoreRoute(&card, &speakers) && card.routes[0].form == maud_formSpeakers,
          "but still takes a known one's change");
}

int main(void)
{
    TestRead();
    TestStore();
    TestNodeForm();
    return s_failures == 0 ? 0 : 1;
}
