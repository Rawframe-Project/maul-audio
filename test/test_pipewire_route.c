// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// PipeWire card routes without a card: Route params built as a card's
// Device emits them are read into a direction, a profile device and the
// form of the port type, and a node on that profile device takes the
// route's form over its own form factor, in its device's info too, until
// the card goes. A card is added through a stand-in registry and device:
// it takes a free slot, binds, listens and subscribes to its routes.

#include "pipewire_card.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"

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

static int s_destroyed;

static void Destroyed(struct pw_proxy* proxy)
{
    (void)proxy;
    s_destroyed++;
}

static maudDeviceForm FormOf(const maudContext* context, maudDeviceId device)
{
    maudDeviceInfo info = {0};
    return maudDevice_GetInfo(context, device, &info) == maud_success ? info.form
                                                                      : maud_formUnknown;
}

// A jack switch on a card reaches the device of the node on its profile
// device, and a card that goes leaves the node its own form factor.
static void TestApply(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    maudContext* context = nullptr;
    maudDeviceId device = {0, 0};
    CHECK(maudCreateContext(&def, &context) == maud_success &&
              maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral,
                                           &device) == maud_success,
          "a device");
    maudPipewireNode node = {.device = device,
                             .direction = maud_directionOutput,
                             .factorForm = maud_formSpeakers,
                             .hasCard = true,
                             .cardId = 40,
                             .profileDevice = 3,
                             .used = true};
    maudPipewireCard card = {.globalId = 40, .used = true};
    spa_list_init(&card.listener.link);
    maudPipewire pipewire = {.api = {.proxyDestroy = Destroyed},
                             .context = context,
                             .nodes = &node,
                             .nodeCapacity = 1,
                             .cards = &card,
                             .cardCapacity = 1};
    node.owner = &pipewire;
    card.owner = &pipewire;
    uint8_t buffer[1024];
    maudPipewireTakeRoute(
        &card, SPA_PARAM_Route,
        BuildRoute(buffer, sizeof(buffer), SPA_DIRECTION_OUTPUT, 3, "headphones"));
    CHECK(FormOf(context, device) == maud_formHeadphones, "the route's form reaches the device");
    maudPipewireTakeRoute(&card, SPA_PARAM_Profile,
                          BuildRoute(buffer, sizeof(buffer), SPA_DIRECTION_OUTPUT, 3, "speaker"));
    CHECK(FormOf(context, device) == maud_formHeadphones, "only a Route param is a route");
    maudPipewireCard other = {.owner = &pipewire, .globalId = 41, .used = true};
    maudPipewireTakeRoute(&other, SPA_PARAM_Route,
                          BuildRoute(buffer, sizeof(buffer), SPA_DIRECTION_OUTPUT, 3, "speaker"));
    CHECK(FormOf(context, device) == maud_formHeadphones, "another card's route leaves it");
    maudPipewireRemoveCard(&pipewire, 41);
    CHECK(s_destroyed == 0 && card.used, "an unknown card is not forgotten");
    maudPipewireRemoveCard(&pipewire, 40);
    CHECK(s_destroyed == 1 && !card.used && FormOf(context, device) == maud_formSpeakers,
          "a card that goes leaves its node's own form factor");
    CHECK(maudDestroyContext(context) == maud_success, "destroyed");
}

// A stand-in registry whose bind hands out the stand-in device below, or
// nothing when told to fail, and a device that keeps its listeners and
// the param it was subscribed to.
static struct spa_hook_list s_deviceHooks;
static int s_binds;
static bool s_bindFails;
static uint32_t s_subscribed;
static struct spa_interface s_device;

static void* Bind(void* object, uint32_t id, const char* type, uint32_t version, size_t size)
{
    (void)object;
    (void)id;
    (void)type;
    (void)version;
    (void)size;
    s_binds++;
    return s_bindFails ? nullptr : &s_device;
}

static int AddListener(void* object, struct spa_hook* listener,
                       const struct pw_device_events* events, void* data)
{
    (void)object;
    spa_hook_list_append(&s_deviceHooks, listener, events, data);
    return 0;
}

static int Subscribe(void* object, uint32_t* ids, uint32_t count)
{
    (void)object;
    s_subscribed = count == 1 ? ids[0] : SPA_ID_INVALID;
    return 0;
}

static const struct pw_registry_methods s_registryMethods = {
    .version = PW_VERSION_REGISTRY_METHODS,
    .bind = Bind,
};

static const struct pw_device_methods s_deviceMethods = {
    .version = PW_VERSION_DEVICE_METHODS,
    .add_listener = AddListener,
    .subscribe_params = Subscribe,
};

// A card takes the first free slot and hears its routes; a full table
// or a bind that fails keeps no card.
static void TestAdd(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    maudContext* context = nullptr;
    maudDeviceId device = {0, 0};
    CHECK(maudCreateContext(&def, &context) == maud_success &&
              maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral,
                                           &device) == maud_success,
          "a device");
    spa_hook_list_init(&s_deviceHooks);
    struct spa_interface registry = SPA_INTERFACE_INIT(
        PW_TYPE_INTERFACE_Registry, PW_VERSION_REGISTRY, &s_registryMethods, nullptr);
    s_device = (struct spa_interface)SPA_INTERFACE_INIT(PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE,
                                                        &s_deviceMethods, nullptr);
    maudPipewireNode node = {.device = device,
                             .direction = maud_directionOutput,
                             .factorForm = maud_formSpeakers,
                             .hasCard = true,
                             .cardId = 51,
                             .profileDevice = 2,
                             .used = true};
    maudPipewireCard cards[2] = {{.globalId = 50, .used = true}};
    spa_list_init(&cards[0].listener.link);
    maudPipewire pipewire = {.api = {.proxyDestroy = Destroyed},
                             .context = context,
                             .nodes = &node,
                             .nodeCapacity = 1,
                             .cards = cards,
                             .cardCapacity = 2};
    pipewire.connection.registry = (struct pw_registry*)&registry;
    node.owner = &pipewire;
    s_subscribed = SPA_ID_INVALID;
    maudPipewireAddCard(&pipewire, 51);
    CHECK(cards[1].used && cards[1].globalId == 51 && cards[1].owner == &pipewire &&
              cards[1].proxy == (struct pw_proxy*)&s_device,
          "a card takes the free slot, bound");
    CHECK(s_subscribed == SPA_PARAM_Route, "subscribed to its routes");
    uint8_t buffer[1024];
    spa_hook_list_call(&s_deviceHooks, struct pw_device_events, param, 0, 0, SPA_PARAM_Route, 0, 1,
                       BuildRoute(buffer, sizeof(buffer), SPA_DIRECTION_OUTPUT, 2, "headphones"));
    CHECK(FormOf(context, device) == maud_formHeadphones, "the card's route reaches its node");
    maudPipewireAddCard(&pipewire, 52);
    CHECK(s_binds == 1 && cards[0].globalId == 50 && cards[1].globalId == 51,
          "a full table keeps no card");
    int destroyed = s_destroyed;
    maudPipewireRemoveCard(&pipewire, 51);
    CHECK(s_destroyed == destroyed + 1 && !cards[1].used &&
              FormOf(context, device) == maud_formSpeakers,
          "the card goes");
    s_bindFails = true;
    maudPipewireAddCard(&pipewire, 53);
    CHECK(s_binds == 2 && !cards[1].used, "a bind that fails keeps no card");
    CHECK(maudDestroyContext(context) == maud_success, "destroyed");
}

int main(void)
{
    TestRead();
    TestStore();
    TestNodeForm();
    TestApply();
    TestAdd();
    return s_failures == 0 ? 0 : 1;
}
