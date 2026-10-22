// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An output stream's clock on the web, as test/web/run_page.js runs it:
// once the stream plays, the time each stamped frame is heard, less the
// frame's own place in the stream, stays within 5 ms over three seconds
// with no underrun, however the browser batches its pulls (by 40 ms in
// headless Chrome). An underrun moves when frames are heard, so it starts
// the window again; a loaded machine gets up to twelve seconds. It runs
// on Chrome's own fake output: through a PulseAudio server Chrome's
// figures wander by 10 to 20 ms, before this clock and after it alike.

#include "../test_harness.h"
#include "maul-audio/context.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <stdio.h>

static maudContext* s_context;
static maudStreamId s_stream;
static double s_since;
static long s_interval;
static double s_lowest = 1e300;
static double s_highest = -1e300;
static double s_lowestLatency = 1e300;
static double s_highestLatency = -1e300;
static uint32_t s_samples;
static uint64_t s_underruns;
static uint32_t s_restarts;

static void Silence(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

EM_JS(void, ListenForGesture, (void),
      { document.addEventListener("click", function() { _TestGesture(); }); });

EMSCRIPTEN_KEEPALIVE void TestGesture(void);

void TestGesture(void)
{
    CHECK(maudResumeContext(s_context) == maud_success, "resume from the gesture");
    s_since = emscripten_get_now();
}

static void Finish(void)
{
    printf("clock: %u stamps, heard-time spread %.1f ms, latency %.1f to %.1f ms, %u restarts "
           "after underruns\n",
           s_samples, (s_highest - s_lowest) / 1e6, s_lowestLatency / 1e6, s_highestLatency / 1e6,
           s_restarts);
    CHECK(s_samples >= 100, "the clock was stamped while it played");
    CHECK(s_highest - s_lowest <= 5e6, "a frame's heard time holds within 5 ms");
    CHECK(maudDestroyStream(s_context, s_stream) == maud_success, "destroy it");
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy the context");
    emscripten_clear_interval(s_interval);
    printf("MAUD_TEST_RESULT %s\n", s_failures == 0 ? "pass" : "fail");
}

static void Step_(void* user)
{
    (void)user;
    maudNotification record;
    while (maudNextNotification(s_context, &record) == maud_success)
    {
    }
    if (s_since == 0.0)
    {
        return;
    }
    double waited = emscripten_get_now() - s_since;
    maudStreamClock clock = {0};
    if (waited > 1000.0 && maudGetStreamClock(s_context, s_stream, &clock) == maud_success &&
        clock.hostNanoseconds != 0)
    {
        // The stream plays at the context's rate, 48 kHz in headless
        // Chrome; the time the stream's frame 0 would be heard.
        maudStreamStatus status = {0};
        (void)maudGetStreamStatus(s_context, s_stream, &status);
        if (status.underruns != s_underruns)
        {
            s_underruns = status.underruns;
            s_restarts += s_samples != 0 ? 1u : 0u;
            s_samples = 0;
            s_lowest = 1e300;
            s_highest = -1e300;
        }
        double origin = (double)clock.hostNanoseconds - (double)clock.position * 1e9 / 48000.0;
        s_lowest = origin < s_lowest ? origin : s_lowest;
        s_highest = origin > s_highest ? origin : s_highest;
        double latency = (double)clock.latencyNanoseconds;
        s_lowestLatency = latency < s_lowestLatency ? latency : s_lowestLatency;
        s_highestLatency = latency > s_highestLatency ? latency : s_highestLatency;
        s_samples++;
    }
    if (s_samples >= 300 || waited > 13000.0)
    {
        Finish();
    }
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    CHECK(maudCreateContext(&def, &s_context) == maud_success, "a context on the page");
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.callback = Silence;
    CHECK(maudCreateStream(s_context, &streamDef, &s_stream) == maud_success &&
              maudStartStream(s_context, s_stream) == maud_success,
          "an output");
    ListenForGesture();
    printf("MAUD_TEST_WAITING_FOR_GESTURE\n");
    s_interval = emscripten_set_interval(Step_, 10.0, nullptr);
    return 0;
}
