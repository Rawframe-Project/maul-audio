// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's capture, run under Node on a simulated Web Audio: a
// mock microphone whose getUserMedia settles after the test's first
// step, and a mock AudioWorklet that runs the backend's own capture
// processor, with the device's quanta and every message's delivery in the
// test's hands. Where the browser page (test/web/page_clock.c) can only
// bound what a loaded machine lets it see, this checks exactly, on the
// ring and on posted chunks: the wait for the permission, the frames in
// order, quanta the ring could not fit counted as overruns, and the
// clock the stream is stamped with, with and without the context's
// output timestamp and with a stale one. The grant settles on a promise,
// so the checks run from timers and the result is the process's exit
// code.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/eventloop.h>

#define RATE     48000
#define RAMP     1000
#define QUANTUM  128
#define CAPACITY 4096
#define LATENCY  5000000LL // the track's own, 5 ms

// clang-format off

// Installs the mock: an AudioContext at 48 kHz with base and output
// latencies of 16 and 10 ms, worklet modules evaluated from their Blob
// source, a microphone whose track says it is 5 ms late, and ports whose
// messages wait until the test delivers them.
EM_JS(void, InstallWeb, (int isolated), {
    const sim = {toMain: [], nodes: [], frame: 0, sources: {}, processors: {}, port: null, ramp: 0};
    globalThis.maudSim = sim;
    globalThis.maudWeb = undefined;
    globalThis.crossOriginIsolated = isolated !== 0;
    function Port(queue) { this.onmessage = null; this.peer = null; this.queue = queue; }
    Port.prototype.postMessage = function (data) { this.queue.push({port: this.peer, data: data}); };
    globalThis.Blob = function (parts) { this.text = parts.join(""); };
    URL.createObjectURL = function (blob) {
        const key = "mock:" + Object.keys(sim.sources).length;
        sim.sources[key] = blob.text;
        return key;
    };
    globalThis.AudioWorkletProcessor = function () { this.port = sim.port; };
    globalThis.registerProcessor = function (name, kind) { sim.processors[name] = kind; };
    Object.defineProperty(globalThis, "currentFrame", {get: function () { return sim.frame; }, configurable: true});
    globalThis.sampleRate = 48000;
    const track = {
        getSettings: function () { return {latency: 0.005, echoCancellation: false, noiseSuppression: false,
                                            autoGainControl: false, groupId: "mic"}; },
        addEventListener: function () {},
        stop: function () {},
    };
    const stream = {
        getAudioTracks: function () { return [track]; },
        getVideoTracks: function () { return []; },
        getTracks: function () { return [track]; },
    };
    const devices = {
        addEventListener: function () {},
        removeEventListener: function () {},
        enumerateDevices: function () { return Promise.resolve([]); },
        getUserMedia: function () { return Promise.resolve(stream); },
    };
    Object.defineProperty(globalThis.navigator, "mediaDevices", {configurable: true, value: devices});
    globalThis.AudioContext = function () {
        this.sampleRate = 48000;
        this.baseLatency = 0.016;
        this.outputLatency = 0.01;
        this.state = "running";
        this.destination = {};
        this.audioWorklet = {addModule: function (url) {
            (new Function(sim.sources[url]))();
            return Promise.resolve();
        }};
        this.createMediaStreamSource = function () {
            return {connect: function (node) { sim.capture = node; }, disconnect: function () {}};
        };
        this.resume = function () { return Promise.resolve(); };
        this.suspend = function () { return Promise.resolve(); };
        this.close = function () { return Promise.resolve(); };
    };
    function seconds() { return sim.frame / 48000; }
    Object.defineProperty(globalThis.AudioContext.prototype, "currentTime", {get: seconds});
    globalThis.AudioWorkletNode = function (context, name, options) {
        const main = new Port(null);
        const worklet = new Port(sim.toMain);
        main.peer = worklet;
        worklet.peer = main;
        this.port = main;
        sim.port = worklet;
        this.name = name;
        this.processor = new sim.processors[name](options);
        this.connect = function () {};
        this.disconnect = function () {};
        sim.nodes.push(this);
    };
});

// Gives the context an output timestamp, or takes it away: with 1, the
// frame the graph took last is heard 20 ms from now; with 2, the same
// timestamp taken a second earlier, as a starved device's.
EM_JS(void, Stamp, (int on), {
    if (on === 0) {
        delete globalThis.AudioContext.prototype.getOutputTimestamp;
        return;
    }
    globalThis.AudioContext.prototype.getOutputTimestamp = function () {
        const old = on === 1 ? 0 : 1;
        return {contextTime: globalThis.maudSim.frame / 48000 - old,
                performanceTime: performance.now() + 20 - old * 1000};
    };
});

// The device takes quanta from the microphone, a ramp, into the capture
// node.
EM_JS(void, Pull, (int quanta), {
    const sim = globalThis.maudSim;
    for (let q = 0; q < quanta; ++q) {
        const samples = new Float32Array(128);
        for (let i = 0; i < 128; ++i) {
            samples[i] = (sim.ramp++ % 1000) / 1000;
        }
        sim.capture.processor.process([[samples]]);
        sim.frame += 128;
    }
});

// Delivers the oldest report alone; 0 when none waits.
EM_JS(int, DeliverOneToMain, (void), {
    const queue = globalThis.maudSim.toMain;
    if (queue.length === 0) {
        return 0;
    }
    const message = queue.shift();
    if (message.port.onmessage !== null) {
        message.port.onmessage({data: message.data});
    }
    return 1;
});

EM_JS(void, DropReports, (void), {
    globalThis.maudSim.toMain = [];
    globalThis.maudSim.port.queue = globalThis.maudSim.toMain;
});

EM_JS(void, ExitWith, (int code), {
    process.exitCode = code;
});

// clang-format on

static maudContext* s_context;
static maudStreamId s_stream;
static bool s_isolated;
static uint64_t s_frames;
static int s_disorder;

// Counts the frames and checks they continue the ramp.
static void Take(const maudStreamBlock* block, void* user)
{
    (void)user;
    for (uint32_t i = 0; i < block->frameCount; ++i)
    {
        int step = (int)(block->input[i] * RAMP + 0.5f);
        s_disorder += step != (int)((s_frames + i) % RAMP);
    }
    s_frames += block->frameCount;
}

static void Drain(void)
{
    maudNotification record;
    while (maudNextNotification(s_context, &record) == maud_success)
    {
    }
}

static int64_t Latency(void)
{
    maudStreamClock clock = {0};
    CHECK(maudGetStreamClock(s_context, s_stream, &clock) == maud_success, "the clock");
    return clock.latencyNanoseconds;
}

static bool Near(int64_t value, int64_t expected, int64_t tolerance, const char* what, int report)
{
    if (value >= expected - tolerance && value <= expected + tolerance)
    {
        return true;
    }
    printf("%s, report %d: %lld ns, expected %lld\n", what, report, (long long)value,
           (long long)expected);
    return false;
}

static void Begin(bool isolated);

// After the grant: the frames, the clock three ways, and the ring's
// overruns.
static void Granted(void* user)
{
    (void)user;
    Drain();
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
              status.suspension == maud_suspendNone,
          "granted, the capture runs");

    // Without a timestamp: each delivery as if its last frame were taken
    // now, after the track's own latency.
    Pull(3);
    for (int report = 0; DeliverOneToMain() == 1; ++report)
    {
        // The ring's first report reads the three quanta; posted, each
        // report brings its own.
        if (s_isolated && report > 0)
        {
            continue;
        }
        int64_t frames = s_isolated ? 3 * QUANTUM : QUANTUM;
        CHECK(Near(Latency(), LATENCY + frames * 1000000000LL / RATE, 2, "unstamped", report),
              "unstamped, the frames as if the last were taken now");
    }
    CHECK(s_frames == 3u * QUANTUM && s_disorder == 0, "the frames, in order");

    // With the context's output timestamp: the first frame delivered was
    // taken at its context frame, which the graph processed 26 ms before
    // it is heard; the frame taken last is heard 20 ms from now. The
    // clock's filter holds the batch's first report, so a later one is
    // off by the page's time since (a millisecond allowed).
    Stamp(1);
    Pull(3);
    for (int report = 0; DeliverOneToMain() == 1; ++report)
    {
        if (s_isolated && report > 0)
        {
            continue;
        }
        // The first frame's distance back from the frame taken last.
        int64_t back = s_isolated ? 3 * QUANTUM : (3 - report) * QUANTUM;
        int64_t expected = back * 1000000000LL / RATE - 20000000LL + 26000000LL + LATENCY;
        CHECK(Near(Latency(), expected, 1000000, "stamped", report),
              "stamped, at the context frame that took the first frame");
    }

    // A timestamp a second old is no measure: the unstamped figure again.
    Stamp(2);
    Pull(3);
    for (int report = 0; DeliverOneToMain() == 1; ++report)
    {
        if (s_isolated && report > 0)
        {
            continue;
        }
        int64_t frames = s_isolated ? 3 * QUANTUM : QUANTUM;
        CHECK(Near(Latency(), LATENCY + frames * 1000000000LL / RATE, 2, "stale", report),
              "a stale timestamp leaves the unstamped figure");
    }
    Stamp(0);
    CHECK(s_disorder == 0, "the frames still in order");

    // The main thread away while the ring fills: each quantum past its
    // capacity is lost and counted, once delivered; posted chunks queue.
    DropReports();
    Pull(CAPACITY / QUANTUM + 3);
    (void)DeliverOneToMain();
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success, "status");
    CHECK(status.overruns == (s_isolated ? 3u : 0u),
          "quanta past the ring's capacity counted as overruns");

    CHECK(maudDestroyStream(s_context, s_stream) == maud_success, "destroy the capture");
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy the context");
    if (s_isolated)
    {
        Begin(false);
        return;
    }
    printf("capture: %s\n", s_failures == 0 ? "pass" : "fail");
    ExitWith(s_failures == 0 ? 0 : 1);
}

// Opens a capture; it waits for the permission until getUserMedia's
// promise settles, after this step.
static void Begin(bool isolated)
{
    s_isolated = isolated;
    s_frames = 0;
    s_disorder = 0;
    InstallWeb(isolated ? 1 : 0);
    maudContextDef def = maudDefaultContextDef();
    s_context = nullptr;
    CHECK(maudCreateContext(&def, &s_context) == maud_success, "a context");
    if (s_context == nullptr)
    {
        ExitWith(1);
        return;
    }
    maudStreamDef stream = maudDefaultStreamDef();
    stream.direction = maud_directionInput;
    stream.layout = maud_layoutMono;
    stream.callback = Take;
    CHECK(maudCreateStream(s_context, &stream, &s_stream) == maud_success &&
              maudStartStream(s_context, s_stream) == maud_success,
          "a capture");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
              status.suspension == maud_suspendPermission,
          "it waits for the permission");
    emscripten_set_timeout(Granted, 0, nullptr);
}

int main(void)
{
    ExitWith(1);
    Begin(true);
    return 0;
}
