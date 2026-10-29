// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A microphone's capture on the web whose track the browser ends (the
// microphone unplugged or its permission withdrawn, which the page
// stands in for by stopping the track and sending its ended event), as
// test/web/run_page.js runs it with its fake microphone: the stream
// waits for the permission while the library opens the capture again,
// then resumes and captures on.

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
static uint32_t s_endedAt;
static bool s_ended;
static bool s_waited;
static bool s_resumed;
static double s_since;
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
// Ends every capture's audio track as the browser would: stopped, with
// its ended event, which a page's own stop does not send.
EM_JS(void, EndCaptureTracks, (void), {
    globalThis.maudWeb.nodes.forEach(function (record) {
        if (record !== null && record.stream) {
            record.stream.getAudioTracks().forEach(function (track) {
                track.stop();
                track.dispatchEvent(new Event("ended"));
            });
        }
    });
});
// clang-format on

EMSCRIPTEN_KEEPALIVE void TestGesture(void);

void TestGesture(void)
{
    CHECK(maudContext_Unlock(s_context) == maud_success, "resume from the gesture");
    s_since = emscripten_get_now();
}

static void Finish(void)
{
    printf("ended: %u blocks before, %u in all\n", s_endedAt, s_blocks);
    CHECK(s_waited, "it waited for the permission while the capture opened again");
    CHECK(s_resumed && s_blocks >= s_endedAt + 20, "then resumed and captured on");
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
        bool mine = record.stream.index1 == s_capture.index1 &&
                    record.stream.generation == s_capture.generation;
        s_resumed = s_resumed || (mine && s_ended && record.kind == maud_notifyStreamResumed);
    }
    if (s_since == 0.0)
    {
        return;
    }
    if (!s_ended && s_blocks >= 20)
    {
        s_ended = true;
        s_endedAt = s_blocks;
        EndCaptureTracks();
        maudStreamStatus status = {0};
        s_waited = maudStream_GetStatus(s_context, s_capture, &status) == maud_success &&
                   status.suspension == maud_suspendPermission;
    }
    double waited = emscripten_get_now() - s_since;
    if ((s_resumed && s_blocks >= s_endedAt + 20) || waited > 10000.0)
    {
        Finish();
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
          "a capture");
    ListenForGesture();
    printf("MAUD_TEST_WAITING_FOR_GESTURE\n");
    s_interval = emscripten_set_interval(Step_, 10.0, nullptr);
    return 0;
}
