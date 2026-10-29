// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A display grant without audio, as test/web/run_page.js runs it: the
// page answers getDisplayMedia with a video track only, as a browser
// does when the user picks a window or a screen whose sound it cannot
// share. A loopback stream made in the gesture's handler takes that as a
// refusal: it waits for the permission, the video track is stopped, and
// nothing is left to throw.

#include "../test_harness.h"
#include "maul-audio/context.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <stdio.h>

static maudContext* s_context;
static maudStreamId s_loop;
static double s_since;
static long s_interval;

static void Hear(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

EM_JS(void, ListenForGesture, (void),
      { document.addEventListener("click", function() { _TestGesture(); }); });

// clang-format off
// Answers display capture with a canvas's video track alone, and counts
// the promise rejections nothing handled.
EM_JS(void, GrantVideoOnly, (void), {
    globalThis.unhandled = 0;
    globalThis.addEventListener("unhandledrejection", function () { globalThis.unhandled++; });
    navigator.mediaDevices.getDisplayMedia = function () {
        const canvas = document.createElement("canvas");
        globalThis.granted = canvas.captureStream(10);
        return Promise.resolve(globalThis.granted);
    };
});

EM_JS(int, VideoStopped, (void), {
    return globalThis.granted !== undefined &&
           globalThis.granted.getVideoTracks().every(function (track) { return track.readyState === "ended"; })
        ? 1 : 0;
});

EM_JS(int, Unhandled, (void), { return globalThis.unhandled; });
// clang-format on

EMSCRIPTEN_KEEPALIVE void TestGesture(void);

void TestGesture(void)
{
    CHECK(maudContext_Unlock(s_context) == maud_success, "resume from the gesture");
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionLoopback;
    def.callback = Hear;
    CHECK(maudCreateStream(s_context, &def, &s_loop) == maud_success &&
              maudStream_Start(s_context, s_loop) == maud_success,
          "a loopback stream made in the gesture");
    s_since = emscripten_get_now();
}

static void Step_(void* user)
{
    (void)user;
    maudNotification record;
    while (maudContext_NextNotification(s_context, &record) == maud_success)
    {
    }
    if (s_since == 0.0 || emscripten_get_now() - s_since < 1500.0)
    {
        return;
    }
    maudStreamStatus status = {0};
    CHECK(maudStream_GetStatus(s_context, s_loop, &status) == maud_success &&
              status.suspension == maud_suspendPermission,
          "a grant without audio leaves it waiting for the permission");
    CHECK(VideoStopped() == 1, "the video track is stopped");
    CHECK(Unhandled() == 0, "and nothing threw");
    printf("display: suspension %d, video stopped %d, unhandled %d\n", status.suspension,
           VideoStopped(), Unhandled());
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy the context");
    emscripten_clear_interval(s_interval);
    s_since = 0.0;
    printf("MAUD_TEST_RESULT %s\n", s_failures == 0 ? "pass" : "fail");
}

int main(void)
{
    GrantVideoOnly();
    maudContextDef def = maudDefaultContextDef();
    CHECK(maudCreateContext(&def, &s_context) == maud_success, "a context on the page");
    ListenForGesture();
    printf("MAUD_TEST_WAITING_FOR_GESTURE\n");
    s_interval = emscripten_set_interval(Step_, 10.0, nullptr);
    return 0;
}
