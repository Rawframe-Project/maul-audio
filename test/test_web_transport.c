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
// callback is told, with and without the context's output timestamp, and
// the clock's filter holding a drifting timestamp and starting again on a
// new route, a short quantum and a stop.

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
    // The context has run a second, past the quarter second whose stamps
    // the library ignores.
    const sim = {toMain: [], toWorklet: [], nodes: [], frame: 48000, sources: {}, processors: {}, port: null, played: []};
    globalThis.maudSim = sim;
    // The page's clock is the test's: it moves only with the frames the
    // device takes, and on from one install to the next, so the library's
    // host clock (which reads it) and the mock's timestamps agree to the
    // nanosecond however long the test takes to run.
    const before = globalThis.maudClock ? globalThis.maudClock() : 0;
    sim.offset = before + 1 - sim.frame / 48;
    globalThis.maudClock = function () { return sim.offset + sim.frame / 48; };
    performance.now = globalThis.maudClock;
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

// Gives the context an output timestamp, or takes it away: with 1, the
// frame rendered last is heard 40 ms from now; with 2, the timestamp is a
// second old, as a starved device's; with 3, a fresh one 5 ms later at
// each call, as a PulseAudio server's drifts; with 4, a 1 ms drift a call;
// 1 and 4 moved later by Jump's milliseconds.
EM_JS(void, Stamp, (int on), {
    globalThis.maudSim.base = performance.now() - globalThis.maudSim.frame / 48;
    if (on === 0) {
        delete globalThis.AudioContext.prototype.getOutputTimestamp;
        return;
    }
    globalThis.AudioContext.prototype.getOutputTimestamp = function () {
        // The stale one agrees with the fresh one but was taken a second
        // earlier, a second of frames before.
        const old = on === 2 ? 1 : 0;
        const sim = globalThis.maudSim;
        const step = on === 4 ? 1 : 5;
        const late = on >= 3 ? (sim.late = sim.late === undefined ? 0 : sim.late + step) : 0;
        const now = performance.now() + (on === 1 || on === 4 ? sim.jump || 0 : 0);
        return {contextTime: globalThis.maudSim.frame / 48000 - old,
                performanceTime: now + 40 - old * 1000 + late};
    };
});

// Moves the context to a new route, as a sink change does: the clock's
// filter starts again.
EM_JS(void, NewRoute, (void), {
    globalThis.maudWeb.contexts.forEach(function (entry) {
        if (entry !== null) {
            entry.routeEpoch++;
        }
    });
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

// A timestamp `age` seconds old (now less its performanceTime) that
// puts the frame rendered last 100 ms from now: a consistent pair, old
// only by its age.
EM_JS(void, StampAged, (double age), {
    const sim = globalThis.maudSim;
    globalThis.AudioContext.prototype.getOutputTimestamp = function () {
        return {contextTime: sim.frame / 48000 - age - 0.1, performanceTime: performance.now() - age * 1000};
    };
});

// Puts the context `frame` frames into its run.
EM_JS(void, SetFrame, (int frame), {
    const sim = globalThis.maudSim;
    sim.offset += (sim.frame - frame) / 48;
    sim.frame = frame;
});

// Moves Stamp 1's and 4's heard times later by `ms` from now on,
// as frames the device played without the stream (silence it inserted).
EM_JS(void, Jump, (double ms), { globalThis.maudSim.jump = ms; });

// Where the page's clock stood at frame 0 when the timestamp was given,
// in milliseconds: times from it depend only on the frames and drifts.
EM_JS(double, StampBase, (void), { return globalThis.maudSim.base; });

// The short count the oldest waiting report carries; -1 when none waits.
EM_JS(int, NextShort, (void), {
    const queue = globalThis.maudSim.toMain;
    return queue.length === 0 ? -1 : queue[0].data[1];
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

// When a clock says frame 0 is heard: an output's host time is when the
// frame at its position is heard.
static double Heard(const maudStreamClock* clock)
{
    return (double)clock->hostNanoseconds - (double)clock->position * 1e9 / RATE;
}

// A batch's report checked: the first's latency as expected, and every
// report's frame 0 heard when the first's is, which the filter holds to,
// to a microsecond (the page's clock is the test's).
static bool Holds(const maudStreamClock* clock, int report, int64_t expected, double* first)
{
    double heard = Heard(clock);
    *first = report == 0 ? heard : *first;
    bool ok = heard <= *first + 1000.0 && heard >= *first - 1000.0;
    if (report == 0)
    {
        ok = clock->latencyNanoseconds >= expected - 1000 &&
             clock->latencyNanoseconds <= expected + 1000;
    }
    if (!ok)
    {
        printf("report %d: latency %lld ns, expected %lld; frame 0 %.0f ns from the first's\n",
               report, (long long)clock->latencyNanoseconds, (long long)expected, heard - *first);
    }
    return ok;
}

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
    CHECK(maudContext_GetBackend(context) == maud_backendWeb, "the web backend");
    maudStreamDef def = maudDefaultStreamDef();
    def.layout = maud_layoutStereo;
    def.callback = Ramp;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "a stream");
    DeliverToWorklet();
    CHECK(Target() == BASE + 2 * QUANTUM, "the target starts two quanta past the base latency");
    CHECK(Held() == Target(), "the worklet holds the target from the start");
    CHECK(maudStream_Start(context, stream) == maud_success, "start");

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
    CHECK(maudStream_GetClock(context, stream, &clock) == maud_success, "the clock");
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
        CHECK(maudStream_GetClock(context, stream, &clock) == maud_success, "the clock");
        if (clock.latencyNanoseconds < expected - 2 || clock.latencyNanoseconds > expected + 2)
        {
            printf("report %d: latency %lld ns, expected %lld\n", report,
                   (long long)clock.latencyNanoseconds, (long long)expected);
            CHECK(false, "the latency is right at every report of a batch");
        }
    }
    DeliverToWorklet();

    // With the context's output timestamp, each report places the frames
    // it writes at its own context frame plus what was queued ahead of
    // them then: here, the same queue as above, heard 40 ms after the
    // frame rendered last rather than the base and output latencies'
    // 26 ms, at the batch's first report; every later one hears frame 0
    // when the first does, which the clock's filter holds to.
    Stamp(1);
    Pull(BASE / QUANTUM);
    double first = 0.0;
    for (int report = 0; DeliverOneToMain() == 1; ++report)
    {
        int64_t frames = isolated ? 2 * QUANTUM : (2 + report) * QUANTUM;
        int64_t expected = frames * 1000000000LL / RATE + 40000000LL;
        CHECK(maudStream_GetClock(context, stream, &clock) == maud_success, "the clock");
        CHECK(Holds(&clock, report, expected, &first),
              "the output timestamp places every report's frames");
    }
    DeliverToWorklet();

    // A timestamp that drifts late at each report is held at the
    // earliest time frame 0 was heard, on a new route: the same figures
    // as above.
    NewRoute();
    Stamp(3);
    Pull(BASE / QUANTUM);
    first = 0.0;
    for (int report = 0; DeliverOneToMain() == 1; ++report)
    {
        int64_t frames = isolated ? 2 * QUANTUM : (2 + report) * QUANTUM;
        int64_t expected = frames * 1000000000LL / RATE + 40000000LL;
        CHECK(maudStream_GetClock(context, stream, &clock) == maud_success, "the clock");
        CHECK(Holds(&clock, report, expected, &first),
              "a timestamp drifting late is held at the earliest");
    }
    DeliverToWorklet();

    // A timestamp a second old is no measure: the queue and the context's
    // latencies again, as without one.
    Stamp(2);
    Pull(BASE / QUANTUM);
    for (int report = 0; DeliverOneToMain() == 1; ++report)
    {
        int64_t frames = isolated ? 2 * QUANTUM : (2 + report) * QUANTUM;
        int64_t expected = frames * 1000000000LL / RATE + 26000000LL;
        CHECK(maudStream_GetClock(context, stream, &clock) == maud_success, "the clock");
        if (clock.latencyNanoseconds < expected - 2 || clock.latencyNanoseconds > expected + 2)
        {
            printf("stale report %d: latency %lld ns, expected %lld\n", report,
                   (long long)clock.latencyNanoseconds, (long long)expected);
            CHECK(false, "a stale timestamp leaves the queued figure");
        }
    }
    Stamp(0);
    DeliverToWorklet();

    // The main thread away for two pulls: the second plays two quanta of
    // the buffer and four short; the target grows a quantum for each.
    Pull(BASE / QUANTUM);
    Pull(BASE / QUANTUM);
    CHECK(WorkletShorts() == 4, "four quanta short after a missed refill");
    // The first report answered now is older than the whole buffer: the
    // worklet ran dry, so nothing plays before the frames it brings.
    DeliverOneToMain();
    CHECK(maudStream_GetClock(context, stream, &clock) == maud_success &&
              clock.latencyNanoseconds >= 26000000LL - 2 &&
              clock.latencyNanoseconds <= 26000000LL + 2,
          "after running dry, only the base and output latencies");
    DeliverToMain();
    DeliverToWorklet();
    CHECK(Target() == BASE + 6 * QUANTUM, "the target grew a quantum for each short one");
    CHECK(Held() == Target(), "refilled to the grown target");
    maudStreamStatus status = {0};
    CHECK(maudStream_GetStatus(context, stream, &status) == maud_success && status.underruns == 4,
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
    CHECK(maudStream_Stop(context, stream) == maud_success, "stop");
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

// The clock's filter starts again after a short quantum (kind 0) and
// after a stop (kind 1): a timestamp drifting late is held at the
// earliest frame-0 time, then the device runs dry or the stream stops,
// which moves frame 0 later, and the next report is measured. The
// reference run is the same but for a new route just before that report,
// which starts the filter again for certain; the two runs hear frame 0
// at the same time when the path under test starts it too, and apart by
// the silence and the drift (over 10 ms) when it does not.
static double FilterRun(bool isolated, int kind, bool route)
{
    InstallWeb(isolated ? 1 : 0);
    maudContextDef cdef = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&cdef, &context) == maud_success, "a context on the mock");
    if (context == nullptr)
    {
        return 0.0;
    }
    maudStreamDef def = maudDefaultStreamDef();
    def.layout = maud_layoutStereo;
    def.callback = Ramp;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "a stream");
    DeliverToWorklet();
    CHECK(maudStream_Start(context, stream) == maud_success, "start");
    for (int i = 0; i < 4; ++i)
    {
        Cycle();
    }
    NewRoute();
    Stamp(4);
    Cycle();
    if (kind == 0)
    {
        // Two pulls with no refill between: the reports before the first
        // short quantum are answered, and that one is measured.
        Pull(BASE / QUANTUM);
        Pull(BASE / QUANTUM);
        while (NextShort() == 0)
        {
            DeliverOneToMain();
        }
    }
    else
    {
        // Stopped, the worklet drops its frames and is refilled with
        // silence, then plays 32 ms of it, more than it dropped (21 ms)
        // and with no quantum short, so frame 0 moves 11 ms later, under
        // the bound past which frames count as lost; started again, one
        // pull's first report is measured.
        CHECK(maudStream_Stop(context, stream) == maud_success, "stop");
        DeliverToWorklet();
        DeliverToMain();
        DeliverToWorklet();
        for (int i = 0; i < 2; ++i)
        {
            Cycle();
        }
        CHECK(maudStream_Start(context, stream) == maud_success, "start again");
        Pull(BASE / QUANTUM);
    }
    if (route)
    {
        NewRoute();
    }
    DeliverOneToMain();
    maudStreamClock clock = {0};
    CHECK(maudStream_GetClock(context, stream, &clock) == maud_success, "the clock");
    // When frame 0 is heard, from where the page's clock stood at frame 0
    // when the timestamp was given: the pulls and the drift decide it.
    double heard =
        (double)clock.hostNanoseconds - (double)clock.position * 1e9 / RATE - StampBase() * 1e6;
    Stamp(0);
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
    return heard;
}

static void RunFilterResets(bool isolated)
{
    static const char* const kinds[2] = {"a short quantum", "a stop"};
    for (int kind = 0; kind < 2; ++kind)
    {
        double measured = FilterRun(isolated, kind, false);
        double reference = FilterRun(isolated, kind, true);
        if (measured < reference - 1000.0 || measured > reference + 1000.0)
        {
            printf("after %s: frame 0 heard at %.0f ns, with a fresh filter at %.0f\n", kinds[kind],
                   measured, reference);
            CHECK(false, "the clock's filter starts again");
        }
    }
}

// The step the filter watches for lost frames, read at a cycle: the
// heard time of frame 0 against before, and the underruns counted since.
static void Watch(maudContext* context, maudStreamId stream, double* heard, uint64_t* underruns)
{
    maudStreamClock clock = {0};
    maudStreamStatus status = {0};
    CHECK(maudStream_GetClock(context, stream, &clock) == maud_success &&
              maudStream_GetStatus(context, stream, &status) == maud_success,
          "the clock and status");
    *heard = Heard(&clock);
    *underruns = status.underruns;
}

// Frame 0 heard later: 30 ms is within the estimate's drift and stays
// held, uncounted. 70 ms for one cycle and then back is a report the
// browser placed late: held, uncounted. 70 ms for good is frames the
// browser played without the stream: still held and uncounted 2.5 s on,
// then one underrun once it has lasted 5 s, and the clock follows it.
static void RunLoss(bool isolated)
{
    InstallWeb(isolated ? 1 : 0);
    maudContextDef cdef = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&cdef, &context) == maud_success, "a context on the mock");
    if (context == nullptr)
    {
        return;
    }
    maudStreamDef def = maudDefaultStreamDef();
    def.layout = maud_layoutStereo;
    def.callback = Ramp;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "a stream");
    DeliverToWorklet();
    CHECK(maudStream_Start(context, stream) == maud_success, "start");
    Cycle();
    NewRoute();
    Stamp(1);
    Cycle();
    double before = 0.0;
    uint64_t underruns = 0;
    Watch(context, stream, &before, &underruns);
    double heard = 0.0;
    uint64_t counted = 0;
    Jump(30.0);
    Cycle();
    Watch(context, stream, &heard, &counted);
    CHECK(heard <= before + 1000.0 && counted == underruns,
          "30 ms later is drift: held, nothing counted");
    Jump(70.0);
    Cycle();
    Jump(0.0);
    for (int i = 0; i < 400; ++i)
    {
        Cycle();
    }
    Watch(context, stream, &heard, &counted);
    CHECK(heard <= before + 1000.0 && counted == underruns,
          "70 ms late once is a report placed late: held, nothing counted");
    // A cycle is 16 ms of the device's frames.
    Jump(70.0);
    for (int i = 0; i < 156; ++i)
    {
        Cycle();
    }
    Watch(context, stream, &heard, &counted);
    CHECK(heard <= before + 1000.0 && counted == underruns,
          "70 ms later for 2.5 s: still held, nothing counted yet");
    for (int i = 0; i < 188; ++i)
    {
        Cycle();
    }
    Watch(context, stream, &heard, &counted);
    if (heard < before + 60e6 || counted != underruns + 1)
    {
        printf("70 ms later for 5.5 s: frame 0 %.0f ns from before, %llu underruns, %llu before\n",
               heard - before, (unsigned long long)counted, (unsigned long long)underruns);
        CHECK(false, "70 ms later for good is frames lost: one underrun, and the clock follows");
    }
    Jump(0.0);
    Stamp(0);
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
}

// The filter holds the earliest frame-0 time for 30 seconds: a time
// 30 ms later, under the bound for lost frames, is still held 10 s on,
// and followed once the earliest has left the window, by 35 s.
static void RunWindow(void)
{
    InstallWeb(0);
    maudContextDef cdef = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&cdef, &context) == maud_success, "a context on the mock");
    if (context == nullptr)
    {
        return;
    }
    maudStreamDef def = maudDefaultStreamDef();
    def.layout = maud_layoutStereo;
    def.callback = Ramp;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "a stream");
    DeliverToWorklet();
    CHECK(maudStream_Start(context, stream) == maud_success, "start");
    Stamp(1);
    Cycle();
    maudStreamClock clock = {0};
    CHECK(maudStream_GetClock(context, stream, &clock) == maud_success, "the clock");
    double before = Heard(&clock);
    Jump(30.0);
    // A cycle is 16 ms of the device's frames.
    for (int i = 0; i < 625; ++i)
    {
        Cycle();
    }
    CHECK(maudStream_GetClock(context, stream, &clock) == maud_success, "the clock");
    CHECK(Heard(&clock) <= before + 1000.0, "10 s on, the earliest is held");
    for (int i = 0; i < 1563; ++i)
    {
        Cycle();
    }
    CHECK(maudStream_GetClock(context, stream, &clock) == maud_success, "the clock");
    if (Heard(&clock) < before + 30e6 - 1000.0 || Heard(&clock) > before + 30e6 + 1000.0)
    {
        printf("35 s on: frame 0 %.0f ns from the earliest\n", Heard(&clock) - before);
        CHECK(false, "35 s on, the window has moved past the earliest");
    }
    Jump(0.0);
    Stamp(0);
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
}

// The latency the clock tells now.
static int64_t ToldLatency(maudContext* context, maudStreamId stream)
{
    maudStreamClock clock = {0};
    CHECK(maudStream_GetClock(context, stream, &clock) == maud_success, "the clock");
    return clock.latencyNanoseconds;
}

// Which timestamps count: none in the context's first quarter second
// (its first stamps run early), none 100 ms old or more. A stamp here
// puts frames 100 ms or more out, the queue and the context's latencies
// under 50 ms, so the latency tells which figure the clock took.
static void RunGuards(bool isolated)
{
    InstallWeb(isolated ? 1 : 0);
    SetFrame(4800);
    maudContextDef cdef = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&cdef, &context) == maud_success, "a context on the mock");
    if (context == nullptr)
    {
        return;
    }
    maudStreamDef def = maudDefaultStreamDef();
    def.layout = maud_layoutStereo;
    def.callback = Ramp;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "a stream");
    DeliverToWorklet();
    CHECK(maudStream_Start(context, stream) == maud_success, "start");
    StampAged(0.0);
    Cycle();
    CHECK(ToldLatency(context, stream) < 50000000, "a tenth of a second in: no stamp counts");
    for (int i = 0; i < 20; ++i)
    {
        Cycle();
    }
    CHECK(ToldLatency(context, stream) >= 100000000, "past a quarter second: the stamp counts");
    // Far enough in that an old stamp's own context time is past the
    // quarter second too.
    for (int i = 0; i < 20; ++i)
    {
        Cycle();
    }
    StampAged(0.15);
    Cycle();
    CHECK(ToldLatency(context, stream) < 50000000, "a stamp 150 ms old does not count");
    StampAged(0.06);
    Cycle();
    CHECK(ToldLatency(context, stream) >= 100000000, "a stamp 60 ms old counts");
    Stamp(0);
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
}

int main(void)
{
    RunTransport(true);
    RunTransport(false);
    RunFilterResets(true);
    RunFilterResets(false);
    RunLoss(true);
    RunLoss(false);
    RunGuards(true);
    RunGuards(false);
    RunWindow();
    return s_failures == 0 ? 0 : 1;
}
