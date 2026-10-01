// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend in a browser, as test/web/run_page.js runs it. The
// page holds audio until a gesture: the test makes a stream, finds it
// held by the policy, logs that it waits, and the driver's click calls
// maudResumeContext. The stream then plays a ramp, which a probe
// worklet behind it checks for continuity; its rate, short quanta,
// stopping, refilling after a restart, the fill target's growth after
// a stall of the main thread, and refusals are checked, and the result
// is logged for the driver, which runs the page with and without
// cross-origin isolation.

#include "../test_harness.h"
#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <stdio.h>

typedef enum Step
{
    stepWaitForGesture,
    stepWaitForResume,
    stepWaitForBlocks,
    stepMeasure,
    stepBreaks,
    stepStopped,
    stepRestarted,
    stepStalled,
    stepDone,
} Step;

// The ramp's length in frames: each channel plays (position % RAMP) / RAMP.
#define RAMP 4800

static maudContext* s_context;
static maudStreamId s_stream;
static Step s_step;
static uint32_t s_blocks;
static uint32_t s_wrongSize;
static double s_since;
static uint64_t s_position;
static int s_window;
static double s_best;
static bool s_gestured;
static int s_shortsAtMeasure;
static int s_shortsAtRestart;
static int s_targetBeforeStall;

static void CountBlocks(const maudStreamBlock* block, void* user)
{
    (void)user;
    s_wrongSize += block->frameCount != 256 || block->layout != maud_layoutStereo ? 1u : 0u;
    for (uint32_t i = 0; i < block->frameCount && block->output != nullptr; ++i)
    {
        float value = (float)((block->position + i) % RAMP) / (float)RAMP;
        block->output[2 * i] = value;
        block->output[2 * i + 1] = value;
    }
    s_blocks++;
}

// clang-format off

// 1 when the stream's node reads a SharedArrayBuffer ring, which it
// must exactly on a cross-origin isolated page; -1 when it does not.
EM_JS(int, RingMatchesPage, (void), {
    const nodes = globalThis.maudWeb.nodes.filter(function (record) { return record !== null; });
    const isolated = globalThis.crossOriginIsolated === true;
    return nodes.length === 1 && (nodes[0].ring !== null) === isolated ? 1 : -1;
});

// The stream's quanta played short so far, and its fill target.
EM_JS(int, Shorts, (void), {
    return globalThis.maudWeb.nodes.filter(function (record) { return record !== null; })[0].shortSeen;
});

EM_JS(int, Target, (void), {
    return globalThis.maudWeb.nodes.filter(function (record) { return record !== null; })[0].target;
});

// Connects a probe worklet behind the stream's node. While counting, it
// counts the samples of the first channel that do not continue the
// ramp; told to stop, it posts the count.
EM_JS(void, AttachProbe, (int ramp), {
    const source = [
        "class MaudProbe extends AudioWorkletProcessor {",
        "  constructor() {",
        "    super();",
        "    this.counting = false;",
        "    this.breaks = 0;",
        "    this.last = -1;",
        "    const self = this;",
        "    this.port.onmessage = function (event) {",
        "      self.counting = event.data;",
        "      self.last = -1;",
        "      if (event.data) { self.breaks = 0; } else { self.port.postMessage(self.breaks); }",
        "    };",
        "  }",
        "  process(inputs) {",
        "    const input = inputs[0];",
        "    for (let i = 0; input.length > 0 && i < input[0].length; ++i) {",
        "      const k = Math.round(input[0][i] * " + ramp + ");",
        "      if (this.counting && this.last >= 0 && k !== (this.last + 1) % " + ramp + ") { this.breaks += 1; }",
        "      this.last = k;",
        "    }",
        "    return true;",
        "  }",
        "}",
        "registerProcessor('maud-probe', MaudProbe);",
    ].join("\n");
    const web = globalThis.maudWeb;
    const context = web.contexts.filter(function (entry) { return entry !== null; })[0].context;
    const record = web.nodes.filter(function (entry) { return entry !== null; })[0];
    const probe = {node: null, breaks: -1};
    web.probe = probe;
    context.audioWorklet.addModule(URL.createObjectURL(new Blob([source], {type: "text/javascript"}))).then(function () {
        probe.node = new AudioWorkletNode(context, "maud-probe", {numberOfInputs: 1, numberOfOutputs: 1});
        probe.node.port.onmessage = function (event) { probe.breaks = event.data; };
        record.node.connect(probe.node);
        probe.node.connect(context.destination);
    });
});

EM_JS(int, ProbeReady, (void), {
    return globalThis.maudWeb.probe.node !== null ? 1 : 0;
});

EM_JS(void, ProbeCount, (int counting), {
    globalThis.maudWeb.probe.breaks = -1;
    globalThis.maudWeb.probe.node.port.postMessage(counting !== 0);
});

// The breaks counted, or -1 until the probe has posted them.
EM_JS(int, ProbeBreaks, (void), {
    return globalThis.maudWeb.probe.breaks;
});

// Keeps the main thread busy, as a long frame of a game would.
EM_JS(void, Stall, (int milliseconds), {
    const end = performance.now() + milliseconds;
    while (performance.now() < end) {
    }
});

// clang-format on

// Makes the page's clicks call TestGesture.
EM_JS(void, ListenForGesture, (void),
      { document.addEventListener("click", function() { _TestGesture(); }); });

EMSCRIPTEN_KEEPALIVE void TestGesture(void);

void TestGesture(void)
{
    s_gestured = true;
    CHECK(maudResumeContext(s_context) == maud_success, "resume from the gesture");
}

static uint64_t Position(void)
{
    uint64_t position = 0;
    CHECK(maudGetStreamPosition(s_context, s_stream, &position) == maud_success, "position");
    return position;
}

static void Finish(void)
{
    CHECK(maudDestroyStream(s_context, s_stream) == maud_success, "destroy while running");
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy");
    printf("MAUD_TEST_RESULT %s\n", s_failures == 0 ? "pass" : "fail");
    s_step = stepDone;
}

// Three windows of two seconds, long enough that the browser's bursts
// and the 10 ms steps blur little; the best is the rate (a stalled
// clock only lowers one), at most 2% above and 6% below.
static void Measure(double now)
{
    if (now - s_since < 2000.0)
    {
        return;
    }
    uint64_t position = Position();
    double rate = (double)(position - s_position) * 1000.0 / (now - s_since);
    s_best = rate > s_best ? rate : s_best;
    s_position = position;
    s_since = now;
    if (++s_window < 3)
    {
        return;
    }
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(s_context, s_stream, &format) == maud_success, "format");
    if (!(s_best > format.sampleRate * 0.94 && s_best < format.sampleRate * 1.02))
    {
        printf("measured %.0f frames/s, expected %u\n", s_best, format.sampleRate);
        CHECK(false, "at the context's rate");
    }
    CHECK(s_wrongSize == 0, "in whole stereo periods");
    ProbeCount(0);
    s_step = stepBreaks;
}

// The ramp arrived whole, but where a quantum ran short (each costs at
// most two breaks); the browser's latency was kept from the start.
static void CheckBreaks(void)
{
    int breaks = ProbeBreaks();
    if (breaks < 0)
    {
        return;
    }
    int shorts = Shorts() - s_shortsAtMeasure;
    if (breaks > 2 * shorts || Shorts() > 4)
    {
        printf("breaks %d, short quanta %d while measuring, %d in all\n", breaks, shorts, Shorts());
    }
    CHECK(breaks <= 2 * shorts, "the ramp plays in order");
    CHECK(Shorts() <= 4, "few short quanta from the start");
    CHECK(maudStopStream(s_context, s_stream) == maud_success, "stop");
    s_blocks = 0;
    s_since = emscripten_get_now();
    s_step = stepStopped;
}

// After a restart the stream refills; then a stall of 200 ms starves
// it, and the target grows.
static void Restarted(double now)
{
    if (s_blocks < 20 || now - s_since < 500.0)
    {
        return;
    }
    CHECK(Shorts() - s_shortsAtRestart <= 2, "refilled after a restart");
    s_targetBeforeStall = Target();
    Stall(200);
    s_since = emscripten_get_now();
    s_step = stepStalled;
}

static void Step_(void* user)
{
    (void)user;
    double now = emscripten_get_now();
    maudNotification record;
    bool resumed = false;
    while (maudNextNotification(s_context, &record) == maud_success)
    {
        resumed = resumed || record.kind == maud_notifyStreamResumed;
    }
    switch (s_step)
    {
    case stepWaitForResume:
        CHECK(!resumed || s_gestured, "held until the gesture");
        if (resumed)
        {
            maudStreamStatus status;
            CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
                      status.suspension == maud_suspendNone,
                  "the stream runs after the gesture");
            CHECK(RingMatchesPage() == 1, "a ring exactly on an isolated page");
            AttachProbe(RAMP);
            s_step = stepWaitForBlocks;
        }
        break;
    case stepWaitForBlocks:
        if (s_blocks >= 20 && ProbeReady() == 1)
        {
            ProbeCount(1);
            s_shortsAtMeasure = Shorts();
            s_since = now;
            s_position = Position();
            s_step = stepMeasure;
        }
        break;
    case stepMeasure:
        Measure(now);
        break;
    case stepBreaks:
        CheckBreaks();
        break;
    case stepStopped:
        if (now - s_since > 300.0)
        {
            CHECK(s_blocks == 0, "no callbacks once stopped");
            CHECK(maudStartStream(s_context, s_stream) == maud_success, "start again");
            s_shortsAtRestart = Shorts();
            s_since = now;
            s_step = stepRestarted;
        }
        break;
    case stepRestarted:
        Restarted(now);
        break;
    case stepStalled:
        if (now - s_since > 300.0)
        {
            CHECK(Target() > s_targetBeforeStall, "the target grows after a stall");
            Finish();
        }
        break;
    default:
        break;
    }
}

static void TestRefusals(void)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    maudStreamId stream;
    def.ratePolicy = maud_rateRequired;
    def.sampleRate = 22050;
    CHECK(maudCreateStream(s_context, &def, &stream) == maud_errorUnsupported,
          "a rate the context does not run at");
    def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.direction = maud_directionInput;
    CHECK(maudCreateStream(s_context, &def, &stream) == maud_errorUnsupported, "no capture yet");
    def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.mode = maud_modePull;
    CHECK(maudCreateStream(s_context, &def, &stream) == maud_errorUnsupported, "no pull mode");
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    CHECK(maudCreateContext(&def, &s_context) == maud_success, "a context on the page");
    CHECK(maudGetContextBackend(s_context) == maud_backendWeb, "Web Audio");
    maudDeviceId device = {0, 0};
    maudDeviceInfo info = {0};
    CHECK(maudGetDefaultDevice(s_context, maud_directionOutput, maud_roleGeneral, &device) ==
                  maud_success &&
              maudGetDeviceInfo(s_context, device, &info) == maud_success &&
              info.nativeSampleRate >= 8000,
          "one output at the context's rate");
    TestRefusals();
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.periodFrames = 256;
    streamDef.callback = CountBlocks;
    CHECK(maudCreateStream(s_context, &streamDef, &s_stream) == maud_success, "create");
    CHECK(maudStartStream(s_context, s_stream) == maud_success, "start");
    maudStreamStatus status;
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
              status.suspension == maud_suspendPolicy,
          "held by the autoplay policy");
    // The drain reads the browser's hold and keeps the stream waiting.
    maudNotification record;
    while (maudNextNotification(s_context, &record) == maud_success)
    {
        CHECK(record.kind != maud_notifyStreamResumed, "no resume before the gesture");
    }
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
              status.suspension == maud_suspendPolicy,
          "still held after a drain");
    ListenForGesture();
    printf("MAUD_TEST_WAITING_FOR_GESTURE\n");
    s_step = stepWaitForResume;
    emscripten_set_interval(Step_, 10.0, nullptr);
    return 0;
}
