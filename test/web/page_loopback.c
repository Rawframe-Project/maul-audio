// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A loopback stream on the web, as test/web/run_page.js runs it in its
// loopback mode: the page plays a 1 kHz tone at 0.5, and once the
// driver clicks, creates a loopback stream in the gesture's handler (the
// browser asks for display capture only then). The driver's browser
// picks this tab by its title, so the stream captures the page's own
// tone, which it must hear at its level. When the browser ends its
// track (the user's "Stop sharing", which the page stands in for by
// stopping the track and sending its ended event), the stream waits for
// the permission withdrawn. Leaving the host's output out is refused.

#include "../test_harness.h"
#include "maul-audio/context.h"
#include "maul-audio/layout.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <math.h>
#include <stdio.h>

static maudContext* s_context;
static maudStreamId s_tone;
static maudStreamId s_loop;
static float s_phase;
static float s_peak;
static uint32_t s_heard;
static double s_since;
static long s_interval;

static void Play(const maudStreamBlock* block, void* user)
{
    (void)user;
    uint32_t channels = maudGetLayoutChannelCount(block->layout);
    for (uint32_t i = 0; i < block->frameCount; ++i)
    {
        float sample = 0.5f * sinf(s_phase);
        s_phase = fmodf(s_phase + 6.2831853f * 1000.0f / (float)block->sampleRate, 6.2831853f);
        for (uint32_t c = 0; c < channels; ++c)
        {
            block->output[i * channels + c] = sample;
        }
    }
}

static void Hear(const maudStreamBlock* block, void* user)
{
    (void)user;
    uint32_t channels = maudGetLayoutChannelCount(block->layout);
    for (uint32_t i = 0; block->input != nullptr && i < block->frameCount * channels; ++i)
    {
        s_peak = fmaxf(s_peak, fabsf(block->input[i]));
    }
    s_heard++;
}

EM_JS(void, ListenForGesture, (void),
      { document.addEventListener("click", function() { _TestGesture(); }); });

EM_JS(void, NameTab, (void), { document.title = "MaudLoopback"; });

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
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionLoopback;
    def.callback = Hear;
    CHECK(maudCreateStream(s_context, &def, &s_loop) == maud_success &&
              maudStream_Start(s_context, s_loop) == maud_success,
          "a loopback stream made in the gesture");
    s_since = emscripten_get_now();
}

static void Finish(void)
{
    printf("loopback: %u blocks, peak %.3f\n", s_heard, (double)s_peak);
    CHECK(s_heard > 0, "its blocks arrive");
    CHECK(s_peak >= 0.4f && s_peak <= 0.6f, "it hears the page's tone at its level");
    EndCaptureTracks();
    maudStreamStatus status = {0};
    CHECK(maudStream_GetStatus(s_context, s_loop, &status) == maud_success &&
              status.suspension == maud_suspendPermission,
          "its sharing stopped, it waits for the permission");
    CHECK(maudDestroyStream(s_context, s_loop) == maud_success &&
              maudDestroyStream(s_context, s_tone) == maud_success,
          "destroy both");
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
    if (s_since == 0.0)
    {
        return;
    }
    double waited = emscripten_get_now() - s_since;
    if ((s_peak >= 0.4f && waited > 2000.0) || waited > 10000.0)
    {
        Finish();
    }
}

int main(void)
{
    NameTab();
    maudContextDef def = maudDefaultContextDef();
    CHECK(maudCreateContext(&def, &s_context) == maud_success, "a context on the page");
    maudStreamDef toneDef = maudDefaultStreamDef();
    toneDef.callback = Play;
    CHECK(maudCreateStream(s_context, &toneDef, &s_tone) == maud_success &&
              maudStream_Start(s_context, s_tone) == maud_success,
          "a tone");
    maudStreamDef refused = maudDefaultStreamDef();
    refused.direction = maud_directionLoopback;
    refused.callback = Hear;
    refused.loopbackExcludesSelf = true;
    maudStreamId none = {0, 0};
    CHECK(maudCreateStream(s_context, &refused, &none) == maud_errorUnsupported,
          "the web cannot leave the host out");
    ListenForGesture();
    printf("MAUD_TEST_WAITING_FOR_GESTURE\n");
    s_interval = emscripten_set_interval(Step_, 10.0, nullptr);
    return 0;
}
