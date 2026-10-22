// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Stream clocks on the web, as test/web/run_page.js runs it with its fake
// microphone: an output and a capture each stamp when a frame is heard or
// was captured, and that time less the frame's own place in the stream
// stays within 5 ms over three seconds with no underrun or lost quantum,
// however the browser batches its pulls (by 40 ms in headless Chrome).
// An underrun moves when frames are heard, so it starts that stream's
// window again; a loaded machine gets up to twelve seconds. It runs on
// Chrome's own fake output, in the commit gate: through a PulseAudio
// server Chrome's figures wander by 10 to 20 ms, before this clock and
// after it alike, and a CI runner's Chrome jumped by 240 ms with no
// underrun, so the page is no CI check; the transport test checks the
// output's stamped clock exactly everywhere.

#include "../test_harness.h"
#include "maul-audio/context.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <stdio.h>

// One stream's window: the spread of its frame-0 time, and its
// latencies, since its last underrun or lost quantum.
typedef struct Watch
{
    maudStreamId stream;
    const char* name;
    double lowest;
    double highest;
    double lowestLatency;
    double highestLatency;
    uint32_t samples;
    uint64_t faults;
    uint32_t restarts;
} Watch;

static maudContext* s_context;
static Watch s_watches[2];
static double s_since;
static long s_interval;

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

static void Restart(Watch* watch)
{
    watch->restarts += watch->samples != 0 ? 1u : 0u;
    watch->samples = 0;
    watch->lowest = 1e300;
    watch->highest = -1e300;
    watch->lowestLatency = 1e300;
    watch->highestLatency = -1e300;
}

// Samples a stream's clock; the stream runs at the context's rate, 48 kHz
// in headless Chrome.
static void Sample(Watch* watch)
{
    maudStreamClock clock = {0};
    maudStreamStatus status = {0};
    if (maudGetStreamClock(s_context, watch->stream, &clock) != maud_success ||
        clock.hostNanoseconds == 0 ||
        maudGetStreamStatus(s_context, watch->stream, &status) != maud_success)
    {
        return;
    }
    uint64_t faults = status.underruns + status.overruns;
    if (faults != watch->faults)
    {
        watch->faults = faults;
        Restart(watch);
    }
    double origin = (double)clock.hostNanoseconds - (double)clock.position * 1e9 / 48000.0;
    double latency = (double)clock.latencyNanoseconds;
    watch->lowest = origin < watch->lowest ? origin : watch->lowest;
    watch->highest = origin > watch->highest ? origin : watch->highest;
    watch->lowestLatency = latency < watch->lowestLatency ? latency : watch->lowestLatency;
    watch->highestLatency = latency > watch->highestLatency ? latency : watch->highestLatency;
    watch->samples++;
}

static void Finish(void)
{
    for (int i = 0; i < 2; ++i)
    {
        const Watch* watch = &s_watches[i];
        printf("clock: %s, %u stamps, spread %.1f ms, latency %.1f to %.1f ms, %u restarts after "
               "underruns\n",
               watch->name, watch->samples, (watch->highest - watch->lowest) / 1e6,
               watch->lowestLatency / 1e6, watch->highestLatency / 1e6, watch->restarts);
        CHECK(watch->samples >= 100, "the clock was stamped while it ran");
        CHECK(watch->highest - watch->lowest <= 5e6, "a frame's time holds within 5 ms");
        CHECK(maudDestroyStream(s_context, watch->stream) == maud_success, "destroy it");
    }
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
    if (waited > 1000.0)
    {
        Sample(&s_watches[0]);
        Sample(&s_watches[1]);
    }
    if ((s_watches[0].samples >= 300 && s_watches[1].samples >= 300) || waited > 13000.0)
    {
        Finish();
    }
}

static void Open(Watch* watch, maudDirection direction, const char* name)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = direction;
    def.layout = direction == maud_directionInput ? maud_layoutMono : maud_layoutStereo;
    def.callback = Silence;
    watch->name = name;
    Restart(watch);
    watch->restarts = 0;
    CHECK(maudCreateStream(s_context, &def, &watch->stream) == maud_success &&
              maudStartStream(s_context, watch->stream) == maud_success,
          "a stream");
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    CHECK(maudCreateContext(&def, &s_context) == maud_success, "a context on the page");
    Open(&s_watches[0], maud_directionOutput, "output");
    Open(&s_watches[1], maud_directionInput, "capture");
    ListenForGesture();
    printf("MAUD_TEST_WAITING_FOR_GESTURE\n");
    s_interval = emscripten_set_interval(Step_, 10.0, nullptr);
    return 0;
}
