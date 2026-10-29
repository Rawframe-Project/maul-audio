// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Stream clocks on the web, as test/web/run_page.js runs it with its fake
// microphone: an output and a capture each stamp when a frame is heard or
// was captured, and that time less the frame's own place in the stream
// holds within 5 ms of Chrome's own timeline over three seconds with no
// underrun or lost quantum, however the browser batches its pulls (by
// 40 ms in headless Chrome). The timeline is the earliest time Chrome's
// output timestamp has placed context frame 0 at, less the context's
// latencies for a capture, which takes its frames that long before they
// are heard: through the sound server's PulseAudio sink it moves by 5 to
// 35 ms in a context's first seconds, which a clock follows when it moves
// earlier (#3). An underrun moves when frames are heard, so it starts
// that stream's window again; a loaded machine gets up to twelve seconds.
// A runner's Chrome opens its context at 44.1 kHz, so a frame's place is
// read at the stream's own rate.

#include "../test_harness.h"
#include "maul-audio/context.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <stdio.h>

// One stream's window: the spread of its frame-0 time against Chrome's
// timeline, and its latencies, since its last underrun or lost quantum.
typedef struct Watch
{
    maudStreamId stream;
    const char* name;
    uint32_t rate;
    double lowest;
    double highest;
    double lowestLatency;
    double highestLatency;
    // Chrome's timeline: the earliest time it placed context frame 0 at,
    // and how far that moved over the window.
    double timeline;
    double timelineFirst;
    uint32_t samples;
    // The largest move of frame 0's time from one sample to the next, at
    // which sample, and the latencies either side of it: how a spread
    // came about, a jump or a slide.
    double previous;
    double previousLatency;
    double step;
    uint32_t stepAt;
    double stepFrom;
    double stepTo;
    uint64_t faults;
    uint32_t restarts;
    bool capture;
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
    CHECK(maudContext_Unlock(s_context) == maud_success, "resume from the gesture");
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
    watch->step = 0.0;
    watch->stepAt = 0;
}

// When Chrome places context frame 0, in nanoseconds on performance.now's
// clock: its output timestamp's time less its context time, and for a
// capture less the context's base and output latencies, as long before
// as the graph took a frame. Not a number before its first timestamp,
// in the context's first quarter second (its first stamps run early) or
// without the method.
// clang-format off
EM_JS(double, ChromeTimeline, (int capture), {
    const entry = globalThis.maudWeb.contexts.filter(function (e) { return e !== null; })[0];
    const context = entry.context;
    const stamp = context.getOutputTimestamp ? context.getOutputTimestamp() : null;
    if (!stamp || !stamp.performanceTime || stamp.contextTime < 0.25) {
        return NaN;
    }
    const latency = capture ? context.baseLatency + (context.outputLatency || 0) : 0;
    return (stamp.performanceTime / 1000 - stamp.contextTime - latency) * 1e9;
});
// clang-format on

// Samples a stream's clock, at the stream's own rate: the context's, 48 kHz
// in a local headless Chrome but 44.1 kHz on a CI runner's, where a fixed
// 48 kHz put frame 0's time 8 % of the window apart.
static void Sample(Watch* watch)
{
    maudStreamClock clock = {0};
    maudStreamStatus status = {0};
    maudStreamFormat format = {0};
    if (maudStream_GetFormat(s_context, watch->stream, &format) != maud_success ||
        format.sampleRate == 0 ||
        maudStream_GetClock(s_context, watch->stream, &clock) != maud_success ||
        clock.hostNanoseconds == 0 ||
        maudStream_GetStatus(s_context, watch->stream, &status) != maud_success)
    {
        return;
    }
    double timeline = ChromeTimeline(watch->capture ? 1 : 0);
    if (timeline != timeline)
    {
        return;
    }
    uint64_t faults = status.underruns + status.overruns;
    if (faults != watch->faults)
    {
        watch->faults = faults;
        Restart(watch);
    }
    watch->rate = format.sampleRate;
    if (watch->samples == 0)
    {
        watch->timeline = timeline;
        watch->timelineFirst = timeline;
    }
    watch->timeline = timeline < watch->timeline ? timeline : watch->timeline;
    double origin = (double)clock.hostNanoseconds -
                    (double)clock.position * 1e9 / (double)format.sampleRate - watch->timeline;
    double latency = (double)clock.latencyNanoseconds;
    watch->lowest = origin < watch->lowest ? origin : watch->lowest;
    watch->highest = origin > watch->highest ? origin : watch->highest;
    watch->lowestLatency = latency < watch->lowestLatency ? latency : watch->lowestLatency;
    watch->highestLatency = latency > watch->highestLatency ? latency : watch->highestLatency;
    double step = origin - watch->previous;
    if (watch->samples != 0 &&
        (step < 0.0 ? -step : step) > (watch->step < 0.0 ? -watch->step : watch->step))
    {
        watch->step = step;
        watch->stepAt = watch->samples;
        watch->stepFrom = watch->previousLatency;
        watch->stepTo = latency;
    }
    watch->previous = origin;
    watch->previousLatency = latency;
    watch->samples++;
}

static void Finish(void)
{
    for (int i = 0; i < 2; ++i)
    {
        const Watch* watch = &s_watches[i];
        printf("clock: %s at %u Hz, %u stamps, spread %.1f ms against Chrome's timeline, which "
               "moved %+.1f ms, latency %.1f to %.1f ms, %u restarts after underruns\n",
               watch->name, watch->rate, watch->samples, (watch->highest - watch->lowest) / 1e6,
               (watch->timeline - watch->timelineFirst) / 1e6, watch->lowestLatency / 1e6,
               watch->highestLatency / 1e6, watch->restarts);
        if (watch->highest - watch->lowest > 5e6)
        {
            printf("clock: %s, largest step %.1f ms at stamp %u, latency %.1f then %.1f ms\n",
                   watch->name, watch->step / 1e6, watch->stepAt, watch->stepFrom / 1e6,
                   watch->stepTo / 1e6);
        }
        CHECK(watch->samples >= 100, "the clock was stamped while it ran");
        CHECK(watch->highest - watch->lowest <= 5e6,
              "a frame's time holds within 5 ms of Chrome's timeline");
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
    while (maudContext_NextNotification(s_context, &record) == maud_success)
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
    watch->capture = direction == maud_directionInput;
    Restart(watch);
    watch->restarts = 0;
    CHECK(maudCreateStream(s_context, &def, &watch->stream) == maud_success &&
              maudStream_Start(s_context, watch->stream) == maud_success,
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
