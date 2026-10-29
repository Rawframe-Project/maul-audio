// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A microphone's capture on the default when the system's default
// microphone moves, as test/web/run_page.js runs it with its fake
// microphone: the page stands in for the move by listing a "default"
// entry of another group and sending devicechange. The capture opens
// once again, onto the default, and captures on, and the input's Default
// reports its route changed; another devicechange with the default where
// it was opens nothing and reports nothing. A "default" output entry of
// another group reports the output's Default route changed.

#include "../test_harness.h"
#include "maul-audio/context.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <stdio.h>

static maudContext* s_context;
static maudStreamId s_capture;
static uint32_t s_blocks;
static uint32_t s_movedAt;
static int s_phase;
static double s_since;
static double s_quietFrom;
static int s_opensAfterMove;
static int s_routes[2];
static int s_routesAfterMove;
static long s_interval;

static void Count(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
    s_blocks++;
}

EM_JS(void, ListenForGesture, (void),
      { document.addEventListener("click", function() { _TestGesture(); }); });

// clang-format off
// Counts the page's microphone requests, and lists a "default" entry of
// the group the test names, once it names one, in place of the browser's.
EM_JS(void, StandInForTheDefault, (void), {
    const devices = navigator.mediaDevices;
    const list = devices.enumerateDevices.bind(devices);
    const ask = devices.getUserMedia.bind(devices);
    globalThis.testOpens = 0;
    devices.getUserMedia = function (constraints) {
        globalThis.testOpens++;
        return ask(constraints);
    };
    devices.enumerateDevices = function () {
        return list().then(function (found) {
            const named = [];
            if (globalThis.testGroup !== undefined) {
                named.push({kind: "audioinput", deviceId: "default", groupId: globalThis.testGroup,
                            label: "Default"});
            }
            if (globalThis.testOutputGroup !== undefined) {
                named.push({kind: "audiooutput", deviceId: "default",
                            groupId: globalThis.testOutputGroup, label: "Default"});
            }
            return named.concat(found.filter(function (device) {
                return !named.some(function (entry) {
                    return entry.kind === device.kind && device.deviceId === "default";
                });
            }));
        });
    };
});

EM_JS(void, MoveTheDefault, (int direction, const char* group), {
    if (direction === 1) {
        globalThis.testGroup = UTF8ToString(group);
    } else {
        globalThis.testOutputGroup = UTF8ToString(group);
    }
    navigator.mediaDevices.dispatchEvent(new Event("devicechange"));
});

EM_JS(int, Opens, (void), { return globalThis.testOpens; });

// Whether the library listed both defaults since the grant: a move can
// only be seen from a known default.
EM_JS(int, KnowsTheDefaults, (void), {
    return globalThis.maudWeb.contexts.some(function (entry) {
        return entry && entry.defaultGroups && entry.defaultGroups[0] !== "" &&
               entry.defaultGroups[1] !== "";
    }) ? 1 : 0;
});

EM_JS_DEPS(pageDefault, "$UTF8ToString");
// clang-format on

EMSCRIPTEN_KEEPALIVE void TestGesture(void);

void TestGesture(void)
{
    CHECK(maudContext_Unlock(s_context) == maud_success, "resume from the gesture");
    s_since = emscripten_get_now();
}

static void Finish(void)
{
    printf("default: %u blocks before the move, %u in all, %d requests after it, %d in all; "
           "routes: %d input, %d output\n",
           s_movedAt, s_blocks, s_opensAfterMove, Opens(), s_routes[1], s_routes[0]);
    CHECK(s_opensAfterMove == 2, "the move opened the capture once again");
    CHECK(s_blocks >= s_movedAt + 20, "which captured on");
    CHECK(Opens() == 2, "a devicechange with the default where it was opened nothing");
    CHECK(s_routesAfterMove == 1 && s_routes[1] == 1,
          "the input's Default reported its route changed once, for the move alone");
    CHECK(s_routes[0] == 1, "and the output's when the default output moved");
    CHECK(maudDestroyStream(s_context, s_capture) == maud_success, "destroy it");
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy the context");
    emscripten_clear_interval(s_interval);
    printf("MAUD_TEST_RESULT %s\n", s_failures == 0 ? "pass" : "fail");
}

static void Step_(void* user)
{
    (void)user;
    maudNotification record;
    while (maudContext_NextNotification(s_context, &record) == maud_success)
    {
        if (record.kind == maud_notifyRouteChanged && record.direction <= maud_directionInput)
        {
            maudDeviceId standIn = {0, 0};
            CHECK(maudContext_GetDefaultDevice(s_context, record.direction, maud_roleGeneral,
                                               &standIn) == maud_success &&
                      standIn.index1 == record.device.index1 &&
                      standIn.generation == record.device.generation,
                  "a route change names the Default stand-in");
            s_routes[record.direction]++;
        }
    }
    if (s_since == 0.0)
    {
        return;
    }
    double now = emscripten_get_now();
    if (s_phase == 0 && s_blocks >= 20 && KnowsTheDefaults() != 0)
    {
        s_phase = 1;
        s_movedAt = s_blocks;
        MoveTheDefault(1, "a-moved-default");
    }
    else if (s_phase == 1 && s_blocks >= s_movedAt + 20 && Opens() >= 2)
    {
        s_phase = 2;
        s_opensAfterMove = Opens();
        s_routesAfterMove = s_routes[1];
        MoveTheDefault(1, "a-moved-default");
        s_quietFrom = now;
    }
    else if (s_phase == 2 && now - s_quietFrom > 1000.0)
    {
        s_phase = 3;
        MoveTheDefault(0, "a-moved-output");
        s_quietFrom = now;
    }
    if ((s_phase == 3 && now - s_quietFrom > 1000.0) || now - s_since > 12000.0)
    {
        s_opensAfterMove = s_phase >= 2 ? s_opensAfterMove : Opens();
        Finish();
    }
}

int main(void)
{
    StandInForTheDefault();
    maudContextDef def = maudDefaultContextDef();
    CHECK(maudCreateContext(&def, &s_context) == maud_success, "a context on the page");
    maudStreamDef captureDef = maudDefaultStreamDef();
    captureDef.direction = maud_directionInput;
    captureDef.callback = Count;
    CHECK(maudCreateStream(s_context, &captureDef, &s_capture) == maud_success &&
              maudStream_Start(s_context, s_capture) == maud_success,
          "a capture");
    ListenForGesture();
    printf("MAUD_TEST_WAITING_FOR_GESTURE\n");
    s_interval = emscripten_set_interval(Step_, 10.0, nullptr);
    return 0;
}
