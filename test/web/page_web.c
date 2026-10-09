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

#include "../test_clock.h"
#include "../test_harness.h"
#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/layout.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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
    stepHostSuspended,
    stepHostResumed,
    stepDevices,
    stepDeviceCapture,
    stepSink,
    stepDone,
} Step;

// The ramp's length in frames: each channel plays (position % RAMP) / RAMP.
#define RAMP 4800

static maudContext* s_context;
static maudStreamId s_stream;
static Step s_step;
// The interval that runs Step_, cleared once the context is gone.
static long s_interval;
static uint32_t s_blocks;
static uint32_t s_blocksAtSuspend;
static uint32_t s_wrongSize;
static double s_since;
static uint64_t s_position;
static int s_window;
static double s_best;
static bool s_gestured;
static int s_shortsAtMeasure;
static int s_shortsAtRestart;
static int s_targetBeforeStall;
static maudStreamClock s_clock;
static bool s_clockSound;
// The capture stream on the browser's (fake) microphone.
static maudStreamId s_capture;
// A second capture asking for all of the browser's voice processing.
static maudStreamId s_voiced;
static uint32_t s_captured;
static float s_loudest;
// Captured samples that were not finite numbers (a ring read past its
// end gives NaN).
static uint32_t s_notFinite;
static maudStreamClock s_captureClock;
static uint32_t s_capturedAtStop;
// The browser refuses the microphone on this run.
static bool s_denied;
// A capture on the second fake microphone, chosen by its device.
static maudStreamId s_chosen;
static uint32_t s_chosenBlocks;
// An output on the first fake output device, once Default is gone.
static maudStreamId s_sinkStream;

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

// The voiced capture's callback: only its report is checked.
static void IgnoreBlocks(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

// Counts captured blocks and their samples that are not finite, and
// keeps the loudest sample.
static void CaptureBlocks(const maudStreamBlock* block, void* user)
{
    (void)user;
    uint32_t samples = block->frameCount * maudGetLayoutChannelCount(block->layout);
    for (uint32_t i = 0; block->input != nullptr && i < samples; ++i)
    {
        s_notFinite += isfinite(block->input[i]) ? 0u : 1u;
        float sample = block->input[i] < 0.0f ? -block->input[i] : block->input[i];
        s_loudest = sample > s_loudest ? sample : s_loudest;
    }
    s_captured++;
}

// clang-format off

// 1 when the playback and the capture nodes each use a SharedArrayBuffer
// ring, which they must exactly on a cross-origin isolated page; -1
// when they do not. Playback records have a fill target.
EM_JS(int, RingMatchesPage, (void), {
    const nodes = globalThis.maudWeb.nodes.filter(function (record) { return record !== null; });
    const isolated = globalThis.crossOriginIsolated === true;
    const playback = nodes.filter(function (record) { return record.target !== undefined; });
    const matches = nodes.every(function (record) { return (record.ring !== null) === isolated; });
    return nodes.length === 3 && playback.length === 1 && matches ? 1 : -1;
});

// The stream's quanta played short so far, and its fill target.
EM_JS(int, Shorts, (void), {
    return globalThis.maudWeb.nodes.filter(function (record) { return record !== null && record.target !== undefined; })[0].shortSeen;
});

EM_JS(int, Isolated, (void), { return globalThis.crossOriginIsolated === true ? 1 : 0; });

EM_JS(int, SinkIs, (const char* key), {
    const entry = globalThis.maudWeb.contexts.filter(function (e) { return e !== null; })[0];
    return entry.context.sinkId === UTF8ToString(key) ? 1 : 0;
});

// Whether the newest capture's track came from the device key.
EM_JS(int, CaptureDeviceIs, (const char* key), {
    const records = globalThis.maudWeb.nodes.filter(function (r) { return r !== null && r.stream; });
    const track = records[records.length - 1].stream.getAudioTracks()[0];
    return track.getSettings().deviceId === UTF8ToString(key) ? 1 : 0;
});

EM_JS(int, ContextSuspended, (void), {
    const entry = globalThis.maudWeb.contexts.filter(function (e) { return e !== null; })[0];
    return entry.context.state === "suspended" ? 1 : 0;
});

EM_JS(int, Target, (void), {
    return globalThis.maudWeb.nodes.filter(function (record) { return record !== null && record.target !== undefined; })[0].target;
});

// Connects a probe worklet behind the stream's node. While counting, it
// counts the samples of the first channel that do not continue the
// ramp, a run of silence (a short quantum) once, since the ramp never
// holds 0 twice in a row; told to stop, it posts the count.
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
        "      const silent = k === 0 && this.last === 0;",
        "      if (this.counting && this.last >= 0 && !silent && k !== (this.last + 1) % " + ramp + ") { this.breaks += 1; }",
        "      this.last = k;",
        "    }",
        "    return true;",
        "  }",
        "}",
        "registerProcessor('maud-probe', MaudProbe);",
    ].join("\n");
    const web = globalThis.maudWeb;
    const context = web.contexts.filter(function (entry) { return entry !== null; })[0].context;
    const record = web.nodes.filter(function (entry) { return entry !== null && entry.target !== undefined; })[0];
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

// Whether the driver had the browser refuse the microphone.
EM_JS(int, MicrophoneDenied, (void), {
    return new URLSearchParams(location.search).get("deny") === "1" ? 1 : 0;
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
    CHECK(maudDestroyStream(s_context, s_capture) == maud_success, "destroy the capture");
    CHECK(maudDestroyStream(s_context, s_voiced) == maud_success, "destroy the voiced one");
    if (s_stream.index1 != 0)
    {
        CHECK(maudDestroyStream(s_context, s_stream) == maud_success, "destroy while running");
    }
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionDuplex;
    def.callback = CountBlocks;
    maudStreamId duplex = {0, 0};
    maudStreamStatus status = {0};
    CHECK(maudCreateStream(s_context, &def, &duplex) == maud_success, "a duplex stream");
    CHECK(maudGetStreamStatus(s_context, duplex, &status) == maud_success &&
              status.drift == maud_driftNone,
          "one AudioContext, one clock");
    CHECK(maudDestroyStream(s_context, duplex) == maud_success, "destroy the duplex");
    emscripten_clear_interval(s_interval);
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy");
    s_context = nullptr;
    printf("MAUD_TEST_RESULT %s\n", s_failures == 0 ? "pass" : "fail");
    s_step = stepDone;
}

// Refused, the capture waits for the permission and hears nothing;
// granted, it captures the fake device's sound with a sound clock.
static void CheckCapture(uint32_t rate)
{
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(s_context, s_capture, &status) == maud_success, "capture status");
    maudStreamStatus voiced = {0};
    CHECK(maudGetStreamStatus(s_context, s_voiced, &voiced) == maud_success, "voiced status");
    if (s_denied)
    {
        CHECK(status.suspension == maud_suspendPermission && s_captured == 0,
              "a refused capture waits, hearing nothing");
        CHECK(!status.voiceReported && !voiced.voiceReported, "and nothing is reported");
        return;
    }
    CHECK(status.suspension == maud_suspendNone, "the granted capture runs");
    CHECK(status.voiceReported && status.voiceActive == maud_voiceNone,
          "without voice processing, as asked");
    CHECK(voiced.voiceReported &&
              voiced.voiceActive ==
                  (maud_voiceEchoCancellation | maud_voiceNoiseSuppression | maud_voiceGainControl),
          "and the browser's voice processing on, as asked");
    maudStreamClock capture = {0};
    CHECK(maudGetStreamClock(s_context, s_capture, &capture) == maud_success, "capture clock");
    CHECK(ClockIsSound(&s_captureClock, &capture, false, true, (double)rate,
                       maudGetHostNanoseconds()),
          "the capture clock maps frames to host time");
    if (!(s_captured > 100 && s_loudest > 0.1f))
    {
        printf("captured %u blocks, loudest %f\n", s_captured, (double)s_loudest);
    }
    CHECK(s_captured > 100, "the microphone's blocks arrive");
    CHECK(s_loudest > 0.1f, "with the fake device's sound");
    CHECK(s_notFinite == 0, "every sample a finite number");
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
    // The clock over the same window; one sound window is enough, as
    // for the rate.
    maudStreamClock clock = {0};
    CHECK(maudGetStreamClock(s_context, s_stream, &clock) == maud_success, "clock");
    maudStreamFormat current = {0};
    CHECK(maudGetStreamFormat(s_context, s_stream, &current) == maud_success, "format");
    s_clockSound =
        s_clockSound || ClockIsSound(&s_clock, &clock, true, true, (double)current.sampleRate,
                                     maudGetHostNanoseconds());
    s_clock = clock;
    if (++s_window < 3)
    {
        return;
    }
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(s_context, s_stream, &format) == maud_success, "format");
    // What the docs quote for each transport, isolated or not.
    printf("output latency %.1f ms, fill target %d frames\n",
           (double)clock.latencyNanoseconds / 1e6, Target());
    if (!(s_best > format.sampleRate * 0.94 && s_best < format.sampleRate * 1.02))
    {
        printf("measured %.0f frames/s, expected %u\n", s_best, format.sampleRate);
        CHECK(false, "at the context's rate");
    }
    CHECK(s_wrongSize == 0, "in whole stereo periods");
    CHECK(s_clockSound, "its clock maps frames to host time");
    CheckCapture(format.sampleRate);
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
    CHECK(maudStopStream(s_context, s_capture) == maud_success, "stop the capture");
    s_capturedAtStop = s_captured;
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

// The stall played quanta short: underruns, as many as the worklet
// counted. On an isolated page it also overflowed the capture ring, 85 ms
// long; posted chunks wait instead, and lose nothing.
static void CheckXruns(void)
{
    maudStreamStatus played = {0};
    maudStreamStatus heard = {0};
    CHECK(maudGetStreamStatus(s_context, s_stream, &played) == maud_success &&
              played.underruns == (uint64_t)Shorts() && played.underruns >= 1,
          "every short quantum is an underrun");
    CHECK(maudGetStreamStatus(s_context, s_capture, &heard) == maud_success, "capture status");
    if (s_denied)
    {
        CHECK(heard.overruns == 0, "a refused capture loses nothing");
        return;
    }
    CHECK(Isolated() ? heard.overruns >= 1 : heard.overruns == 0,
          "the stall overflows the capture ring, and only the ring");
}

// The host's tab is hidden: the streams wait for it, and the
// AudioContext suspends.
static void SuspendForHost(void)
{
    CHECK(maudSetContextSuspended(s_context, true) == maud_success, "suspend for the host");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
              status.suspension == maud_suspendHost,
          "the stream waits for the host");
    s_blocksAtSuspend = s_blocks;
    s_since = emscripten_get_now();
    s_step = stepHostSuspended;
}

static void CountChosen(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
    s_chosenBlocks++;
}

// How many devices of a direction the context lists.
static uint32_t Listed(maudDirection direction)
{
    uint32_t count = 0;
    CHECK(maudGetDevices(s_context, direction, nullptr, 0, &count) == maud_success, "count");
    return count;
}

// The device of a direction named name, or the null id.
static maudDeviceId Named(maudDirection direction, const char* name)
{
    maudDeviceId ids[8];
    uint32_t count = 0;
    CHECK(maudGetDevices(s_context, direction, ids, 8, &count) == maud_success, "list");
    for (uint32_t i = 0; i < count && i < 8; ++i)
    {
        char text[128];
        size_t length = 0;
        if (maudGetDeviceName(s_context, ids[i], text, sizeof(text) - 1, &length) == maud_success &&
            length == strlen(name) && memcmp(text, name, length) == 0)
        {
            return ids[i];
        }
    }
    return (maudDeviceId){0, 0};
}

static bool KeyOf(maudDeviceId device, char* key, size_t capacity)
{
    size_t length = 0;
    bool ok = maudGetDeviceKey(s_context, device, key, capacity - 1, &length) == maud_success;
    key[ok ? length : 0] = '\0';
    return ok;
}

static maudStreamId OpenOn(maudDirection direction, maudDeviceId device, maudResult* resultOut)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = direction;
    def.device = device;
    def.layout = direction == maud_directionInput ? maud_layoutMono : maud_layoutStereo;
    def.callback = direction == maud_directionInput ? CountChosen : CountBlocks;
    maudStreamId stream = {0, 0};
    *resultOut = maudCreateStream(s_context, &def, &stream);
    return stream;
}

// The browser's fake devices are listed by name; while the Default
// output plays, another output device is refused; a capture opens on the
// second fake microphone.
// Every listed device has its direction's layout: the browser plays in
// stereo and captures in mono.
static void CheckLayouts(void)
{
    for (maudDirection direction = maud_directionOutput; direction <= maud_directionInput;
         ++direction)
    {
        maudDeviceId ids[8];
        uint32_t count = 0;
        CHECK(maudGetDevices(s_context, direction, ids, 8, &count) == maud_success, "list");
        for (uint32_t i = 0; i < count && i < 8; ++i)
        {
            maudDeviceInfo info = {0};
            CHECK(maudGetDeviceInfo(s_context, ids[i], &info) == maud_success &&
                      info.nativeLayout ==
                          (direction == maud_directionOutput ? maud_layoutStereo : maud_layoutMono),
                  "stereo outputs and mono inputs");
        }
    }
}

static void ChooseDevices(void)
{
    maudDeviceId output = Named(maud_directionOutput, "Fake Audio Output 1");
    maudDeviceId input = Named(maud_directionInput, "Fake Audio Input 2");
    CHECK(output.index1 != 0 && input.index1 != 0 &&
              Named(maud_directionOutput, "Default").index1 != 0,
          "the fake devices by their labels, and Default");
    maudResult result = maud_success;
    (void)OpenOn(maud_directionOutput, output, &result);
    CHECK(result == maud_errorUnsupported, "another output while Default plays is refused");
    s_chosen = OpenOn(maud_directionInput, input, &result);
    CHECK(result == maud_success && maudStartStream(s_context, s_chosen) == maud_success,
          "a capture on a chosen microphone");
}

// With Default gone, an output on a fake device takes the
// AudioContext's sink (checked once setSinkId settles); another device
// is refused, the same one allowed.
static void StartSink(void)
{
    CHECK(maudDestroyStream(s_context, s_stream) == maud_success, "destroy while running");
    s_stream = (maudStreamId){0, 0};
    maudDeviceId first = Named(maud_directionOutput, "Fake Audio Output 1");
    maudDeviceId second = Named(maud_directionOutput, "Fake Audio Output 2");
    maudResult result = maud_success;
    s_sinkStream = OpenOn(maud_directionOutput, first, &result);
    CHECK(result == maud_success && maudStartStream(s_context, s_sinkStream) == maud_success,
          "an output on a chosen device");
    (void)OpenOn(maud_directionOutput, second, &result);
    CHECK(result == maud_errorUnsupported, "a second device is refused");
    maudStreamId again = OpenOn(maud_directionOutput, first, &result);
    CHECK(result == maud_success && maudDestroyStream(s_context, again) == maud_success,
          "the same device is not");
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
        // The capture clock's first reading waits for the capture to run:
        // posted chunks may start well after playback on a slow machine.
        if (s_blocks >= 20 && ProbeReady() == 1 && (s_denied || s_captured > 0))
        {
            ProbeCount(1);
            CHECK(maudGetStreamClock(s_context, s_capture, &s_captureClock) == maud_success,
                  "capture clock");
            s_shortsAtMeasure = Shorts();
            s_since = now;
            s_position = Position();
            CHECK(maudGetStreamClock(s_context, s_stream, &s_clock) == maud_success, "clock");
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
            CHECK(s_captured == s_capturedAtStop, "nor captured blocks");
            CHECK(maudStartStream(s_context, s_stream) == maud_success, "start again");
            CHECK(maudStartStream(s_context, s_capture) == maud_success, "capture again");
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
            CheckXruns();
            SuspendForHost();
        }
        break;
    case stepHostSuspended:
        if (now - s_since > 400.0)
        {
            CHECK(ContextSuspended() == 1, "the AudioContext suspends with the host");
            CHECK(s_blocks == s_blocksAtSuspend, "and nothing plays");
            CHECK(maudSetContextSuspended(s_context, false) == maud_success, "resume the host");
            s_since = now;
            s_step = stepHostResumed;
        }
        break;
    case stepHostResumed:
        if (now - s_since > 600.0)
        {
            maudStreamStatus status = {0};
            CHECK(ContextSuspended() == 0, "the AudioContext runs again");
            CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
                      status.suspension == maud_suspendNone && s_blocks > s_blocksAtSuspend,
                  "and the stream with it");
            s_since = now;
            s_step = s_denied ? stepDone : stepDevices;
            if (s_denied)
            {
                Finish();
            }
        }
        break;
    case stepDevices:
        if (Listed(maud_directionOutput) == 3 && Listed(maud_directionInput) == 3)
        {
            CheckLayouts();
            ChooseDevices();
            s_since = now;
            s_step = stepDeviceCapture;
        }
        else if (now - s_since > 3000.0)
        {
            CHECK(false, "the fake devices are listed");
            Finish();
        }
        break;
    case stepDeviceCapture:
        if (s_chosenBlocks > 20 || now - s_since > 3000.0)
        {
            char key[128];
            CHECK(s_chosenBlocks > 20, "the chosen microphone captures");
            CHECK(KeyOf(Named(maud_directionInput, "Fake Audio Input 2"), key, sizeof(key)) &&
                      CaptureDeviceIs(key) == 1,
                  "from the device asked for");
            CHECK(maudDestroyStream(s_context, s_chosen) == maud_success, "destroy it");
            StartSink();
            s_since = now;
            s_step = stepSink;
        }
        break;
    case stepSink:
    {
        char key[128];
        bool moved = KeyOf(Named(maud_directionOutput, "Fake Audio Output 1"), key, sizeof(key)) &&
                     SinkIs(key) == 1;
        if (moved || now - s_since > 2000.0)
        {
            CHECK(moved, "the AudioContext plays to the chosen device");
            CHECK(maudDestroyStream(s_context, s_sinkStream) == maud_success, "destroy it");
            Finish();
        }
        break;
    }
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
    def.direction = maud_directionInput;
    CHECK(maudCreateStream(s_context, &def, &stream) == maud_errorUnsupported,
          "nor a capture at another rate");
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
    s_denied = MicrophoneDenied() != 0;
    maudStreamDef captureDef = maudDefaultStreamDef();
    captureDef.direction = maud_directionInput;
    captureDef.callback = CaptureBlocks;
    CHECK(maudCreateStream(s_context, &captureDef, &s_capture) == maud_success, "create a capture");
    CHECK(maudStartStream(s_context, s_capture) == maud_success, "start it");
    captureDef.callback = IgnoreBlocks;
    captureDef.voice =
        maud_voiceEchoCancellation | maud_voiceNoiseSuppression | maud_voiceGainControl;
    CHECK(maudCreateStream(s_context, &captureDef, &s_voiced) == maud_success,
          "a capture with voice processing");
    maudStreamStatus captureStatus;
    CHECK(maudGetStreamStatus(s_context, s_capture, &captureStatus) == maud_success &&
              captureStatus.suspension == maud_suspendPolicy,
          "the capture waits for the policy too");
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
    s_interval = emscripten_set_interval(Step_, 10.0, nullptr);
    return 0;
}
