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
// output timestamp and with a stale one; and, when the microphone's
// track ends, a capture opened again whose first block holds none of the
// old track's frames. The grant settles on a promise, so the checks run
// from timers and the result is the process's exit code.

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
#define TAIL     64        // the frames a joined block ends silent over

// clang-format off

// Installs the mock: an AudioContext at 48 kHz with base and output
// latencies of 16 and 10 ms, worklet modules evaluated from their Blob
// source, a microphone whose track says it is 5 ms late, and ports whose
// messages wait until the test delivers them.
EM_JS(void, InstallWeb, (int isolated), {
    // The context has run a second, past the quarter second whose stamps
    // the library ignores.
    const sim = {toMain: [], nodes: [], frame: 48000, sources: {}, processors: {}, port: null, ramp: 0};
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
    const track = {
        getSettings: function () { return {latency: 0.005, echoCancellation: false, noiseSuppression: false,
                                            autoGainControl: false, groupId: "mic"}; },
        addEventListener: function (type, listener) {
            if (type === "ended") {
                sim.ended = listener;
            }
        },
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
// timestamp taken a second earlier, as a starved device's; with 3, the
// frame taken last heard now, 1 ms later at each call, as a PulseAudio
// server's drifts (room for 31 ms of drift before a capture would fall
// in the future); with 4, the frame taken last heard 60 ms ago, which
// leaves a capture room to be placed 70 ms late. Late's milliseconds move
// the next call's alone later.
EM_JS(void, Stamp, (int on), {
    const sim = globalThis.maudSim;
    sim.base = performance.now() - sim.frame / 48;
    if (on === 0) {
        delete globalThis.AudioContext.prototype.getOutputTimestamp;
        return;
    }
    globalThis.AudioContext.prototype.getOutputTimestamp = function () {
        const old = on === 2 ? 1 : 0;
        const late = on === 3 ? (sim.late = sim.late === undefined ? 0 : sim.late + 1) : 0;
        const once = sim.once || 0;
        sim.once = 0;
        return {contextTime: sim.frame / 48000 - old,
                performanceTime: performance.now() + (on === 3 ? 0 : on === 4 ? -60 : 20) - old * 1000 + late + once};
    };
});

// A timestamp `age` seconds old (now less its performanceTime) that puts
// the frame the graph took last 20 ms from now, as Stamp 1 does: a
// consistent pair, old only by its age.
EM_JS(void, StampAged, (double age), {
    const sim = globalThis.maudSim;
    globalThis.AudioContext.prototype.getOutputTimestamp = function () {
        return {contextTime: sim.frame / 48000 - age - 0.02, performanceTime: performance.now() - age * 1000};
    };
});

// Puts the context `frame` frames into its run.
EM_JS(void, SetFrame, (int frame), {
    const sim = globalThis.maudSim;
    sim.offset += (sim.frame - frame) / 48;
    sim.frame = frame;
});

// Places the next delivery `ms` later, once: a timestamp the browser gave
// late.
EM_JS(void, Late, (double ms), { globalThis.maudSim.once = ms; });

// Sets the context's output latency, which Chrome gives in whole
// milliseconds and updates apart from its timestamp.
EM_JS(void, OutputLatency, (double seconds), {
    globalThis.maudWeb.contexts.filter(function (e) { return e !== null; })[0].context.outputLatency = seconds;
});

// The context runs `quanta` while the microphone's frames never reach
// the capture node: frames the browser lost before the stream got them.
EM_JS(void, Skip, (int quanta), { globalThis.maudSim.frame += 128 * quanta; });

// Where the page's clock stood at frame 0 when the timestamp was given,
// in milliseconds: times from it depend only on the frames and drifts.
EM_JS(double, StampBase, (void), { return globalThis.maudSim.base; });

// Moves the context to a new route, as a microphone change does: the
// clock's filter starts again.
EM_JS(void, NewRoute, (void), {
    globalThis.maudWeb.contexts.forEach(function (entry) {
        if (entry !== null) {
            entry.routeEpoch++;
        }
    });
});

// The device takes quanta from the microphone, a ramp, into the capture
// node; silence once its track has ended.
EM_JS(void, Pull, (int quanta), {
    const sim = globalThis.maudSim;
    for (let q = 0; q < quanta; ++q) {
        const samples = new Float32Array(128);
        for (let i = 0; i < 128 && !sim.silent; ++i) {
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

// The microphone's track ends, as when the microphone goes; the one
// granted after it hears silence.
EM_JS(void, EndTrack, (void), {
    globalThis.maudSim.silent = true;
    globalThis.maudSim.ended();
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

static uint32_t s_blocks;
static uint32_t s_joined;
static uint64_t s_firstPosition;

// Counts the blocks, and those that join the ramp's frames to silence:
// loud, and silent over their last TAIL frames, which no block of the
// ramp alone is.
static void Watch(const maudStreamBlock* block, void* user)
{
    (void)user;
    bool loud = false;
    bool tail = false;
    for (uint32_t i = 0; i < block->frameCount; ++i)
    {
        loud = loud || block->input[i] != 0.0f;
        tail = tail || (i >= block->frameCount - TAIL && block->input[i] != 0.0f);
    }
    s_joined += loud && !tail ? 1u : 0u;
    s_firstPosition = s_blocks == 0 ? block->position : s_firstPosition;
    s_blocks++;
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
static void BeginFilter(bool isolated);
static void BeginEnded(bool isolated);

// When the capture's clock says frame 0 was captured, from where the
// page's clock stood at frame 0 when the timestamp was given: a capture's
// host time is when the frame at its position was captured.
static double Taken(void)
{
    maudStreamClock clock = {0};
    CHECK(maudGetStreamClock(s_context, s_stream, &clock) == maud_success, "the clock");
    return (double)clock.hostNanoseconds - (double)clock.position * 1e9 / RATE - StampBase() * 1e6;
}

// Delivers every waiting report; the frame-0 time after the last.
static double TakeAll(void)
{
    while (DeliverOneToMain() == 1)
    {
    }
    return Taken();
}

// Captures for `quanta` (a multiple of 4), the main thread taking what
// arrived every 4 quanta (11 ms); the frame-0 time after the last.
static double Run(int quanta)
{
    for (int i = 0; i < quanta; i += 4)
    {
        Pull(4);
        (void)TakeAll();
    }
    return Taken();
}

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
    // taken at the same frame-0 time.
    Stamp(1);
    Pull(3);
    double first = 0.0;
    for (int report = 0; DeliverOneToMain() == 1; ++report)
    {
        if (s_isolated && report > 0)
        {
            continue;
        }
        // The first frame's distance back from the frame taken last.
        int64_t back = s_isolated ? 3 * QUANTUM : (3 - report) * QUANTUM;
        int64_t expected = back * 1000000000LL / RATE - 20000000LL + 26000000LL + LATENCY;
        maudStreamClock clock = {0};
        CHECK(maudGetStreamClock(s_context, s_stream, &clock) == maud_success, "the clock");
        // A capture's host time is when the frame at its position was
        // captured; the filter holds frame 0's time, to a microsecond (the
        // page's clock is the test's).
        double taken = (double)clock.hostNanoseconds - (double)clock.position * 1e9 / RATE;
        first = report == 0 ? taken : first;
        CHECK((report != 0 || Near(clock.latencyNanoseconds, expected, 1000, "stamped", report)) &&
                  taken <= first + 1000.0 && taken >= first - 1000.0,
              "stamped, at the context frame that took the first frame");
    }

    // A consistent timestamp counts while under 100 ms old: 60 ms, the
    // stamped figure; 150 ms, the unstamped one.
    for (int pass = 0; pass < 2; ++pass)
    {
        StampAged(pass == 0 ? 0.06 : 0.15);
        Pull(3);
        int64_t back = s_isolated ? 3 * QUANTUM : QUANTUM;
        while (DeliverOneToMain() == 1)
        {
        }
        int64_t last = Latency();
        int64_t stamped = back * 1000000000LL / RATE - 20000000LL + 26000000LL + LATENCY;
        int64_t unstamped = LATENCY + back * 1000000000LL / RATE;
        CHECK(Near(last, pass == 0 ? stamped : unstamped, 1000,
                   pass == 0 ? "60 ms old" : "150 ms old", 0),
              pass == 0 ? "a stamp 60 ms old counts" : "a stamp 150 ms old does not count");
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
    BeginFilter(true);
}

// After the grant, the clock's filter: a timestamp drifting late is held
// at the earliest time frame 0 was captured; a new route, and frames
// dropped while the stream was stopped, each start it again, so frame 0
// moves later by the route's time or by what was dropped.
static void FilterGranted(void* user)
{
    (void)user;
    Drain();
    // A tenth of a second into the context's run no stamp counts (its
    // first stamps run early): the unstamped figure.
    SetFrame(4800);
    Stamp(1);
    Pull(3);
    while (DeliverOneToMain() == 1)
    {
    }
    int64_t back = s_isolated ? 3 * QUANTUM : QUANTUM;
    CHECK(Near(Latency(), LATENCY + back * 1000000000LL / RATE, 1000, "early", 0),
          "in the context's first quarter second no stamp counts");
    Skip(100);
    Stamp(3);
    Pull(3);
    double held = TakeAll();
    Pull(3);
    double still = TakeAll();
    if (still < held - 1000.0 || still > held + 1000.0)
    {
        printf("drifting: frame 0 at %.0f ns, held at %.0f\n", still, held);
        CHECK(false, "a timestamp drifting late is held at the earliest");
    }
    // A route that takes 21 ms longer (eight quanta the new route kept):
    // within the drift, so only the new route's own start lets the clock
    // follow.
    Skip(8);
    NewRoute();
    Pull(1);
    double routed = TakeAll();
    if (routed < still + 15e6)
    {
        printf("new route: frame 0 at %.0f ns, held at %.0f\n", routed, still);
        CHECK(false, "a new route starts the filter again");
    }
    // Six quanta dropped while stopped: frame 0 moves 16 ms later.
    CHECK(maudStopStream(s_context, s_stream) == maud_success, "stop");
    Pull(6);
    (void)TakeAll();
    CHECK(maudStartStream(s_context, s_stream) == maud_success, "start again");
    Pull(1);
    double restarted = TakeAll();
    if (restarted < routed + 10e6)
    {
        printf("restarted: frame 0 at %.0f ns, before the stop at %.0f\n", restarted, routed);
        CHECK(false, "frames dropped while stopped start the filter again");
    }
    // On a steady timestamp with room for a late one, from a fresh
    // filter. Frames the browser lost before the stream: 29 ms (11 quanta)
    // is within the estimate's drift, held and uncounted. A delivery
    // placed 70 ms late once, then back, is not a loss: held, uncounted.
    // 32 ms more lost (12 quanta, 61 ms in all) for good: held and
    // uncounted 2.5 s on (940 quanta), then one overrun once it has lasted
    // 5 s, and the clock follows it.
    Stamp(4);
    NewRoute();
    Pull(1);
    double steady = TakeAll();
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success, "status");
    uint64_t overruns = status.overruns;
    Skip(11);
    Pull(1);
    double near = TakeAll();
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success, "status");
    CHECK(near <= steady + 1000.0 && status.overruns == overruns,
          "29 ms later is drift: held, nothing counted");
    Late(70.0);
    Pull(1);
    (void)TakeAll();
    double settled = Run(400);
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success, "status");
    CHECK(settled <= steady + 1000.0 && status.overruns == overruns,
          "70 ms late once is a delivery placed late: held, nothing counted");
    Skip(12);
    double waiting = Run(940);
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success, "status");
    CHECK(waiting <= steady + 1000.0 && status.overruns == overruns,
          "61 ms later for 2.5 s: still held, nothing counted yet");
    double lost = Run(1124);
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success, "status");
    if (lost < steady + 55e6 || status.overruns != overruns + 1)
    {
        printf("61 ms later for 5.5 s: frame 0 %.0f ns from before, %llu overruns, %llu before\n",
               lost - steady, (unsigned long long)status.overruns, (unsigned long long)overruns);
        CHECK(false, "61 ms later for good is frames lost: one overrun, and the clock follows");
    }
    // The output latency 2 ms higher in one delivery of five, as Chrome's
    // whole-millisecond figure comes in apart from its timestamp: each
    // such delivery places frame 0 2 ms earlier, a low the clock does not
    // keep. Then 2 ms higher for good: the clock follows by the
    // third delivery and not before.
    NewRoute();
    Pull(1);
    double before = TakeAll();
    for (int i = 0; i < 40; ++i)
    {
        OutputLatency(i % 5 == 2 ? 0.012 : 0.01);
        Pull(1);
        (void)TakeAll();
    }
    double jittered = Run(20);
    if (jittered < before - 1000.0 || jittered > before + 1000.0)
    {
        printf("jitter: frame 0 %.0f ns from before\n", jittered - before);
        CHECK(false, "the output latency's jitter leaves the clock where it was");
    }
    OutputLatency(0.012);
    double moved[3];
    for (int i = 0; i < 3; ++i)
    {
        Pull(1);
        moved[i] = TakeAll() - before;
    }
    OutputLatency(0.01);
    if (moved[0] < -1000.0 || moved[1] < -1000.0 || moved[2] < -2001000.0 || moved[2] > -1999000.0)
    {
        printf("step: frame 0 %.0f, %.0f, %.0f ns from before\n", moved[0], moved[1], moved[2]);
        CHECK(false, "a lasting 2 ms step reaches the clock by the third delivery");
    }
    Stamp(0);
    CHECK(maudDestroyStream(s_context, s_stream) == maud_success, "destroy the capture");
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy the context");
    if (s_isolated)
    {
        BeginFilter(false);
        return;
    }
    BeginEnded(true);
}

// Granted again after the track ended: the frames the stream had
// gathered toward a block on the old track are dropped, so its first
// block is the new track's alone, at a position past them.
static void Reopened(void* user)
{
    (void)user;
    Drain();
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
              status.suspension == maud_suspendNone,
          "granted again, the capture runs");
    Pull(8);
    (void)TakeAll();
    CHECK(s_blocks == 2, "two blocks of the new track");
    CHECK(s_joined == 0, "no block joins the old track's frames to the new track's");
    if (s_firstPosition != 3 * QUANTUM)
    {
        printf("ended: the first block at frame %llu\n", (unsigned long long)s_firstPosition);
        CHECK(false, "the first block's position is past the dropped frames");
    }
    CHECK(maudDestroyStream(s_context, s_stream) == maud_success, "destroy the capture");
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy the context");
    if (s_isolated)
    {
        BeginEnded(false);
        return;
    }
    printf("capture: %s\n", s_failures == 0 ? "pass" : "fail");
    ExitWith(s_failures == 0 ? 0 : 1);
}

// After the grant, three quanta toward a block of 480 frames; then the
// track ends, and the stream asks for the microphone again.
static void EndedGranted(void* user)
{
    (void)user;
    Drain();
    Pull(3);
    (void)TakeAll();
    CHECK(s_blocks == 0, "three quanta, short of a block");
    EndTrack();
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
              status.suspension == maud_suspendPermission,
          "the track ended, it waits for the permission again");
    emscripten_set_timeout(Reopened, 0, nullptr);
}

// Opens a capture of periodFrames (0 for the default) feeding callback;
// it waits for the permission until getUserMedia's promise settles,
// after this step, then runs `then`.
static void Open(bool isolated, maudStreamCallback callback, uint32_t periodFrames,
                 void (*then)(void*))
{
    s_isolated = isolated;
    s_frames = 0;
    s_disorder = 0;
    s_blocks = 0;
    s_joined = 0;
    s_firstPosition = 0;
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
    stream.periodFrames = periodFrames;
    stream.callback = callback;
    CHECK(maudCreateStream(s_context, &stream, &s_stream) == maud_success &&
              maudStartStream(s_context, s_stream) == maud_success,
          "a capture");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
              status.suspension == maud_suspendPermission,
          "it waits for the permission");
    emscripten_set_timeout(then, 0, nullptr);
}

static void Begin(bool isolated)
{
    Open(isolated, Take, 0, Granted);
}

static void BeginFilter(bool isolated)
{
    Open(isolated, Take, 0, FilterGranted);
}

static void BeginEnded(bool isolated)
{
    Open(isolated, Watch, 480, EndedGranted);
}

int main(void)
{
    ExitWith(1);
    Begin(true);
    return 0;
}
