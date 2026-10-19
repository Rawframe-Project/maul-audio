// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend's transport, run under Node on a simulated Web Audio:
// a mock AudioContext and AudioWorklet that run the backend's own
// processor source, with the device's pulls and every message's delivery
// in the test's hands. Where the browser page (test/web/page_web.c) can
// only bound what a loaded machine lets it see, this checks the fill
// rule exactly, on the ring and on posted chunks: the start target, the
// refill to the target after each pull, the samples in order, short
// quanta when the main thread is away and the target's growth to the
// ring's capacity, the drop when the stream stops, and the latency the
// callback is told.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>

#define RATE    48000
#define RAMP    1000
#define BASE    768 // 0.016 s at 48 kHz, in whole quanta
#define QUANTUM 128

// clang-format off

// Installs the mock: an AudioContext at 48 kHz with a base latency of
// 16 ms, worklet modules evaluated from their Blob source, and ports
// whose messages wait in two queues until the test delivers them.
EM_JS(void, InstallWeb, (int isolated), {
    const sim = {toMain: [], toWorklet: [], nodes: [], frame: 0, sources: {}, processors: {}, port: null, played: []};
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
    globalThis.AudioContext = function () {
        this.sampleRate = 48000;
        this.baseLatency = 0.016;
        this.outputLatency = 0.01;
        this.state = "running";
        this.destination = {};
        this.audioWorklet = {addModule: function (url) {
            (new Function(sim.sources[url]))();
            return {then: function (done) { done(); return this; }};
        }};
        this.resume = function () { return Promise.resolve(); };
        this.suspend = function () { return Promise.resolve(); };
        this.close = function () { return Promise.resolve(); };
    };
    function seconds() { return sim.frame / 48000; }
    Object.defineProperty(globalThis.AudioContext.prototype, "currentTime", {get: seconds});
    globalThis.AudioWorkletNode = function (context, name, options) {
        const main = new Port(sim.toWorklet);
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

// Plays quanta: the device pulls them from every playback node in turn,
// the first channel's samples kept in order.
EM_JS(void, Pull, (int quanta), {
    const sim = globalThis.maudSim;
    for (let q = 0; q < quanta; ++q) {
        for (const node of sim.nodes) {
            if (node.name !== "maud-stream") {
                continue;
            }
            const out = [new Float32Array(128), new Float32Array(128)];
            node.processor.process([], [out]);
            for (let i = 0; i < 128; ++i) {
                sim.played.push(out[0][i]);
            }
        }
        sim.frame += 128;
    }
});

// Delivers what the main thread posted to the worklet (chunks, drops).
EM_JS(void, DeliverToWorklet, (void), {
    const queue = globalThis.maudSim.toWorklet;
    while (queue.length > 0) {
        const message = queue.shift();
        message.port.onmessage({data: message.data});
    }
});

// Delivers the worklet's reports to the main thread, which refills.
EM_JS(void, DeliverToMain, (void), {
    const queue = globalThis.maudSim.toMain;
    while (queue.length > 0) {
        const message = queue.shift();
        if (message.port.onmessage !== null) {
            message.port.onmessage({data: message.data});
        }
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

EM_JS(int, Target, (void), {
    const records = globalThis.maudWeb.nodes.filter(function (r) { return r !== null && r.target !== undefined; });
    return records[records.length - 1].target;
});

// The frames the worklet holds: the ring's, or the posted chunks it has
// taken and not played.
EM_JS(int, Held, (void), {
    const sim = globalThis.maudSim;
    const node = sim.nodes.filter(function (n) { return n.name === "maud-stream"; }).pop();
    const p = node.processor;
    if (p.index) {
        return (Atomics.load(p.index, 0) - Atomics.load(p.index, 1)) | 0;
    }
    let frames = -p.offset;
    for (const chunk of p.chunks) {
        frames += chunk.length / p.channels;
    }
    return frames;
});

EM_JS(int, WorkletShorts, (void), {
    const node = globalThis.maudSim.nodes.filter(function (n) { return n.name === "maud-stream"; }).pop();
    return node.processor.short;
});

EM_JS(int, PlayedCount, (void), {
    return globalThis.maudSim.played.length;
});

// The played sample at an index, as a ramp step.
EM_JS(int, PlayedStep, (int index), {
    return Math.round(globalThis.maudSim.played[index] * 1000);
});

EM_JS(void, ForgetPlayed, (void), {
    globalThis.maudSim.played = [];
});

// clang-format on

static uint64_t s_rendered;

static void Ramp(const maudStreamBlock* block, void* user)
{
    (void)user;
    for (uint32_t i = 0; i < block->frameCount; ++i)
    {
        float value = (float)((block->position + i) % RAMP) / (float)RAMP;
        block->output[2 * i] = value;
        block->output[2 * i + 1] = value;
    }
    s_rendered += block->frameCount;
}

// The device pulls one base latency, then the main thread answers every
// report and the worklet takes every chunk.
static void Cycle(void)
{
    Pull(BASE / QUANTUM);
    DeliverToMain();
    DeliverToWorklet();
}

// Whether the samples played since the last ForgetPlayed continue the
// ramp, after any silence before the stream started.
static bool InOrder(void)
{
    int count = PlayedCount();
    int i = 0;
    while (i < count && PlayedStep(i) == 0)
    {
        ++i;
    }
    int first = i;
    for (; i < count; ++i)
    {
        if (PlayedStep(i) != (PlayedStep(first) + i - first) % RAMP)
        {
            return false;
        }
    }
    return true;
}

static void RunTransport(bool isolated)
{
    InstallWeb(isolated ? 1 : 0);
    s_rendered = 0;
    maudContextDef cdef = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&cdef, &context) == maud_success, "a context on the mock");
    if (context == nullptr)
    {
        return;
    }
    CHECK(maudGetContextBackend(context) == maud_backendWeb, "the web backend");
    maudStreamDef def = maudDefaultStreamDef();
    def.layout = maud_layoutStereo;
    def.callback = Ramp;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "a stream");
    DeliverToWorklet();
    CHECK(Target() == BASE + 2 * QUANTUM, "the target starts two quanta past the base latency");
    CHECK(Held() == Target(), "the worklet holds the target from the start");
    CHECK(maudStartStream(context, stream) == maud_success, "start");

    // Steady pulls: refilled to the target after each, nothing short.
    ForgetPlayed();
    for (int i = 0; i < 20; ++i)
    {
        Cycle();
        CHECK(Held() == Target(), "refilled to the target after each pull");
    }
    CHECK(WorkletShorts() == 0 && Target() == BASE + 2 * QUANTUM, "no short quantum while on time");
    CHECK(s_rendered == 20u * BASE, "rendered what was played");
    CHECK(InOrder(), "the samples play in order");
    maudStreamClock clock = {0};
    CHECK(maudGetStreamClock(context, stream, &clock) == maud_success, "the clock");
    // On the ring, what plays first is read as the frames are written:
    // the two quanta left after the pull. Posted, it is what the report
    // being answered said, one quantum into the pull.
    int64_t ahead = isolated ? 2 * QUANTUM : BASE + QUANTUM;
    int64_t told = ahead * 1000000000LL / RATE + 26000000LL;
    if (clock.latencyNanoseconds < told - 2 || clock.latencyNanoseconds > told + 2)
    {
        printf("latency %lld ns, expected %lld\n", (long long)clock.latencyNanoseconds,
               (long long)told);
        CHECK(false, "the latency told: what plays first, then the base and output latencies");
    }

    // Each report answered alone: the clock's latency is right for the
    // frames just written at every report, not only at the batch's last.
    // On the ring the first report writes the pull's frames behind the
    // two quanta left; posted, each writes a quantum, behind one more
    // quantum than the last.
    Pull(BASE / QUANTUM);
    for (int report = 0; DeliverOneToMain() == 1; ++report)
    {
        int64_t frames = isolated ? 2 * QUANTUM : (2 + report) * QUANTUM;
        int64_t expected = frames * 1000000000LL / RATE + 26000000LL;
        CHECK(maudGetStreamClock(context, stream, &clock) == maud_success, "the clock");
        if (clock.latencyNanoseconds < expected - 2 || clock.latencyNanoseconds > expected + 2)
        {
            printf("report %d: latency %lld ns, expected %lld\n", report,
                   (long long)clock.latencyNanoseconds, (long long)expected);
            CHECK(false, "the latency is right at every report of a batch");
        }
    }
    DeliverToWorklet();

    // The main thread away for two pulls: the second plays two quanta of
    // the buffer and four short; the target grows a quantum for each.
    Pull(BASE / QUANTUM);
    Pull(BASE / QUANTUM);
    CHECK(WorkletShorts() == 4, "four quanta short after a missed refill");
    // The first report answered now is older than the whole buffer: the
    // worklet ran dry, so nothing plays before the frames it brings.
    DeliverOneToMain();
    CHECK(maudGetStreamClock(context, stream, &clock) == maud_success &&
              clock.latencyNanoseconds >= 26000000LL - 2 &&
              clock.latencyNanoseconds <= 26000000LL + 2,
          "after running dry, only the base and output latencies");
    DeliverToMain();
    DeliverToWorklet();
    CHECK(Target() == BASE + 6 * QUANTUM, "the target grew a quantum for each short one");
    CHECK(Held() == Target(), "refilled to the grown target");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success && status.underruns == 4,
          "each short quantum an underrun");

    // Missed refills until the target reaches the ring's capacity.
    for (int i = 0; i < 12; ++i)
    {
        Pull(Target() / QUANTUM + BASE / QUANTUM);
        DeliverToMain();
        DeliverToWorklet();
    }
    CHECK(Target() == 4096, "the target stops at the ring's capacity");
    CHECK(Held() == 4096, "and holds it");

    // Posted chunks still in flight when the device pulls play short;
    // the ring has no flight.
    if (!isolated)
    {
        int shorts = WorkletShorts();
        Pull(BASE / QUANTUM);
        DeliverToMain();
        Pull(Target() / QUANTUM);
        CHECK(WorkletShorts() - shorts == BASE / QUANTUM, "chunks in flight miss the pull");
        DeliverToWorklet();
        DeliverToMain();
        DeliverToWorklet();
        CHECK(Held() == Target(), "refilled once they arrive");
    }

    // Stopping drops what the worklet holds and refills with silence.
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    DeliverToWorklet();
    CHECK(Held() == 0, "the stop drops the buffer");
    DeliverToMain();
    DeliverToWorklet();
    CHECK(Held() == Target(), "refilled, with silence");
    ForgetPlayed();
    Pull(Target() / QUANTUM);
    bool silent = true;
    for (int i = 0; i < PlayedCount(); ++i)
    {
        silent = silent && PlayedStep(i) == 0;
    }
    CHECK(silent, "a stopped stream plays silence");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
}

int main(void)
{
    RunTransport(true);
    RunTransport(false);
    return s_failures == 0 ? 0 : 1;
}
