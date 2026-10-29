// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Web outputs on devices that go, as test/web/run_page.js runs it with
// its fake devices. The page lists devices of its own (enumerateDevices
// overridden, then devicechange sent): an output the browser does not
// have, on which setSinkId fails, and later the list without a real fake
// output. A stream pinned to the first is suspended as a lost device
// rather than running on a sink it never reached; a stream pinned to the
// second plays until the device leaves the list, then is suspended,
// while a capture following the default runs on. The browser cannot say
// whether it spatializes an output, so a marked one's fate is unknown.

#include "../test_harness.h"
#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <stdio.h>
#include <string.h>

static maudContext* s_context;
static maudStreamId s_capture;
static maudStreamId s_pinned;
static uint32_t s_blocks;
static int s_phase;
static double s_since;
static double s_phaseSince;
static long s_interval;
static bool s_refused;
static bool s_played;
static bool s_lost;
static bool s_captureRuns;

static void Count(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

static void Play(const maudStreamBlock* block, void* user)
{
    (void)user;
    memset(block->output, 0, sizeof(float) * 2 * block->frameCount);
    s_blocks++;
}

EM_JS(void, ListenForGesture, (void),
      { document.addEventListener("click", function() { _TestGesture(); }); });

// clang-format off
// Lists the browser's devices with an output it does not have added, or
// without its first fake output, and tells the page they changed.
EM_JS(void, ListDevices, (int phantom, int dropFirst), {
    const devices = navigator.mediaDevices;
    if (!devices.realEnumerate) {
        devices.realEnumerate = devices.enumerateDevices.bind(devices);
    }
    devices.enumerateDevices = function () {
        return devices.realEnumerate().then(function (list) {
            const kept = list.filter(function (device) {
                return !(dropFirst !== 0 && device.label === "Fake Audio Output 1");
            });
            return phantom !== 0
                ? kept.concat([{deviceId: "phantom-sink", groupId: "", kind: "audiooutput", label: "Phantom"}])
                : kept;
        });
    };
    devices.dispatchEvent(new Event("devicechange"));
});
// clang-format on

EMSCRIPTEN_KEEPALIVE void TestGesture(void);

void TestGesture(void)
{
    CHECK(maudContext_Unlock(s_context) == maud_success, "resume from the gesture");
    s_since = emscripten_get_now();
    s_phaseSince = s_since;
    s_phase = 1;
}

// The output device named name, or the null id.
static maudDeviceId Named(const char* name)
{
    maudDeviceId ids[8];
    uint32_t count = 0;
    if (maudContext_GetDevices(s_context, maud_directionOutput, ids, 8, &count) != maud_success)
    {
        return (maudDeviceId){0, 0};
    }
    for (uint32_t i = 0; i < count && i < 8; ++i)
    {
        char text[64];
        size_t length = 0;
        if (maudDevice_GetName(s_context, ids[i], text, sizeof(text) - 1, &length) ==
                maud_success &&
            length == strlen(name) && memcmp(text, name, length) == 0)
        {
            return ids[i];
        }
    }
    return (maudDeviceId){0, 0};
}

static maudStreamId PinOutput(maudDeviceId device)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.device = device;
    def.layout = maud_layoutStereo;
    def.callback = Play;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(s_context, &def, &stream) == maud_success &&
              maudStream_Start(s_context, stream) == maud_success,
          "an output pinned to a listed device");
    return stream;
}

static maudSuspendReason Suspension(maudStreamId stream)
{
    maudStreamStatus status = {0};
    CHECK(maudStream_GetStatus(s_context, stream, &status) == maud_success, "status");
    return status.suspension;
}

static void Next(int phase)
{
    s_phase = phase;
    s_phaseSince = emscripten_get_now();
}

static void Finish(void)
{
    printf("sink: refused %d, played %u blocks, lost %d, capture runs %d\n", s_refused, s_blocks,
           s_lost, s_captureRuns);
    CHECK(s_refused, "an output on a sink the browser refuses is suspended as lost");
    CHECK(s_played, "an output on a real fake device plays");
    CHECK(s_lost && s_captureRuns,
          "it is lost when its device leaves the list, the capture runs on");
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
    }
    if (s_phase == 0)
    {
        return;
    }
    double now = emscripten_get_now();
    double inPhase = now - s_phaseSince;
    if (s_phase == 1 && Named("Fake Audio Output 1").index1 != 0)
    {
        ListDevices(1, 0);
        Next(2);
    }
    else if (s_phase == 2 && Named("Phantom").index1 != 0)
    {
        s_pinned = PinOutput(Named("Phantom"));
        Next(3);
    }
    else if (s_phase == 3 && (Suspension(s_pinned) == maud_suspendDeviceLost || inPhase > 3000.0))
    {
        s_refused = Suspension(s_pinned) == maud_suspendDeviceLost;
        CHECK(maudDestroyStream(s_context, s_pinned) == maud_success, "destroy it");
        s_blocks = 0;
        s_pinned = PinOutput(Named("Fake Audio Output 1"));
        Next(4);
    }
    else if (s_phase == 4 && inPhase > 1000.0)
    {
        s_played = s_blocks > 10 && Suspension(s_pinned) == maud_suspendNone;
        ListDevices(0, 1);
        Next(5);
    }
    else if (s_phase == 5 && (Suspension(s_pinned) == maud_suspendDeviceLost || inPhase > 3000.0))
    {
        s_lost = Suspension(s_pinned) == maud_suspendDeviceLost;
        s_captureRuns = Suspension(s_capture) == maud_suspendNone;
        Finish();
        s_phase = 0;
    }
    else if (now - s_since > 15000.0)
    {
        Finish();
        s_phase = 0;
    }
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    CHECK(maudCreateContext(&def, &s_context) == maud_success, "a context on the page");
    maudStreamDef captureDef = maudDefaultStreamDef();
    captureDef.direction = maud_directionInput;
    captureDef.callback = Count;
    CHECK(maudCreateStream(s_context, &captureDef, &s_capture) == maud_success &&
              maudStream_Start(s_context, s_capture) == maud_success,
          "a capture following the default, whose grant names the outputs");
    maudStreamDef marked = maudDefaultStreamDef();
    marked.callback = Play;
    marked.contentSpatialized = true;
    maudStreamId markedId = {0, 0};
    maudStreamStatus status = {0};
    CHECK(maudCreateStream(s_context, &marked, &markedId) == maud_success &&
              maudStream_GetStatus(s_context, markedId, &status) == maud_success &&
              status.spatialMark == maud_markUnknown &&
              maudDestroyStream(s_context, markedId) == maud_success,
          "a marked output's fate is unknown");
    ListenForGesture();
    printf("MAUD_TEST_WAITING_FOR_GESTURE\n");
    s_interval = emscripten_set_interval(Step_, 10.0, nullptr);
    return 0;
}
