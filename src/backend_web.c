// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend. The context owns an AudioContext, kept by the
// JavaScript side in a table under a handle. Streams play through a
// JavaScript AudioWorkletProcessor fed with chunks the main thread
// renders when the worklet reports one played, so the host's callback
// runs on the main thread, four chunks of 512 frames ahead of the
// speaker. The browser's autoplay policy holds a context until a user
// gesture; the drain reads the AudioContext's state and suspends or
// resumes the streams.

#include "backend.h"
#include "context.h"
#include "device.h"
#include "follow.h"
#include "period.h"
#include "thread.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <string.h>

// Frames in each chunk the main thread renders, and chunks queued.
#define CHUNK_FRAMES 512u
#define QUEUE_DEPTH  4u
// The render quantum of Web Audio.
#define QUANTUM_FRAMES 128u

// clang-format off

// Makes an AudioContext and returns its handle, or 0 where the page has
// no Web Audio.
EM_JS(int, maudWebOpen, (void), {
    if (typeof AudioContext === "undefined") {
        return 0;
    }
    const web = globalThis.maudWeb || (globalThis.maudWeb = {contexts: [null], nodes: [null]});
    web.contexts.push({context: new AudioContext(), worklet: null});
    return web.contexts.length - 1;
});

EM_JS(int, maudWebRate, (int handle), {
    return globalThis.maudWeb.contexts[handle].context.sampleRate;
});

// 1 while the browser holds the context, 0 when it runs.
EM_JS(int, maudWebHeld, (int handle), {
    return globalThis.maudWeb.contexts[handle].context.state === "suspended" ? 1 : 0;
});

EM_JS(void, maudWebResume, (int handle), {
    globalThis.maudWeb.contexts[handle].context.resume();
});

EM_JS(void, maudWebClose, (int handle), {
    const entry = globalThis.maudWeb.contexts[handle];
    entry.context.close();
    globalThis.maudWeb.contexts[handle] = null;
});

// Plays posted chunks of interleaved frames and reports each one
// played; told to drop its queue, it asks for a new one. The source is
// plain string literals: EM_JS passes its body through the C
// preprocessor, which would split arrow functions and template
// literals.
EM_JS(void, maudWebAddProcessor, (int handle), {
    const source = [
        "class MaudQueue extends AudioWorkletProcessor {",
        "  constructor(options) {",
        "    super();",
        "    this.channels = options.processorOptions.channels;",
        "    this.depth = options.processorOptions.depth;",
        "    this.chunks = [];",
        "    this.offset = 0;",
        "    const self = this;",
        "    this.port.onmessage = function (event) {",
        "      if (event.data === 'drop') {",
        "        self.chunks = [];",
        "        self.offset = 0;",
        "        for (let i = 0; i < self.depth; ++i) { self.port.postMessage(1); }",
        "      }",
        "      else { self.chunks.push(event.data); }",
        "    };",
        "  }",
        "  process(inputs, outputs) {",
        "    const out = outputs[0];",
        "    const frames = out[0].length;",
        "    for (let i = 0; i < frames && this.chunks.length > 0; ++i) {",
        "      const chunk = this.chunks[0];",
        "      for (let c = 0; c < out.length; ++c) { out[c][i] = chunk[this.offset * this.channels + c]; }",
        "      this.offset += 1;",
        "      if (this.offset * this.channels >= chunk.length) {",
        "        this.chunks.shift();",
        "        this.offset = 0;",
        "        this.port.postMessage(1);",
        "      }",
        "    }",
        "    return true;",
        "  }",
        "}",
        "registerProcessor('maud-queue', MaudQueue);",
    ].join("\n");
    const entry = globalThis.maudWeb.contexts[handle];
    entry.worklet = entry.context.audioWorklet.addModule(
        URL.createObjectURL(new Blob([source], {type: "text/javascript"})));
});

// Makes a stream's node once the processor is registered and queues its
// first chunks; returns the node's handle. Each chunk played asks the
// module for the next.
EM_JS(int, maudWebOpenNode, (int handle, void* context, int slot, int channels, int frames,
                             int depth), {
    const web = globalThis.maudWeb;
    const entry = web.contexts[handle];
    const record = {node: null, closed: false};
    web.nodes.push(record);
    const id = web.nodes.length - 1;
    function send() {
        const pointer = _maudWebRender(context, slot);
        const samples = HEAPF32.slice(pointer >> 2, (pointer >> 2) + frames * channels);
        record.node.port.postMessage(samples, [samples.buffer]);
    }
    entry.worklet.then(function () {
        if (record.closed) {
            return;
        }
        record.node = new AudioWorkletNode(entry.context, "maud-queue", {
            numberOfInputs: 0,
            outputChannelCount: [channels],
            processorOptions: {channels: channels, depth: depth},
        });
        record.node.port.onmessage = function () {
            if (!record.closed) {
                send();
            }
        };
        for (let i = 0; i < depth; ++i) {
            send();
        }
        record.node.connect(entry.context.destination);
    });
    return id;
});

// Drops what a node has queued.
EM_JS(void, maudWebDropNode, (int node), {
    const record = globalThis.maudWeb.nodes[node];
    if (record.node !== null) {
        record.node.port.postMessage("drop");
    }
});

EM_JS(void, maudWebCloseNode, (int node), {
    const record = globalThis.maudWeb.nodes[node];
    record.closed = true;
    if (record.node !== null) {
        record.node.port.onmessage = null;
        record.node.disconnect();
    }
    globalThis.maudWeb.nodes[node] = null;
});

// clang-format on

// A stream's node and the chunk the main thread renders into.
typedef struct maudWebStream
{
    int node;
    float* chunk;
    size_t chunkBytes;
} maudWebStream;

typedef struct maudWeb
{
    int handle;
    maudWebStream* streams;
    size_t bytes;
} maudWeb;

// Renders one chunk of a stream on the main thread and returns it; the
// JavaScript side calls it when the worklet has played a chunk.
EMSCRIPTEN_KEEPALIVE float* maudWebRender(maudContext* context, int slotIndex);

float* maudWebRender(maudContext* context, int slotIndex)
{
    maudWeb* web = context->native;
    maudStreamCore* core = &context->streams.slots[slotIndex].core;
    float* chunk = web->streams[slotIndex].chunk;
    bool running = atomic_load_explicit(&core->state, memory_order_acquire) == maud_streamRunning;
    atomic_store_explicit(&core->renderingThread, maudCurrentThread(), memory_order_release);
    core->period.sampleRate = atomic_load_explicit(&core->blockRate, memory_order_acquire);
    if (running)
    {
        maudPullPeriod(&core->period, chunk, CHUNK_FRAMES);
        atomic_fetch_add_explicit(&core->position, CHUNK_FRAMES, memory_order_release);
    }
    else
    {
        memset(chunk, 0, web->streams[slotIndex].chunkBytes);
    }
    atomic_store_explicit(&core->renderingThread, 0, memory_order_release);
    return chunk;
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t streams = context->def.limits.streams;
    size_t bytes = sizeof(maudWeb) + (size_t)streams * sizeof(maudWebStream);
    maudWeb* web = maudContextAllocate(context, bytes, alignof(maudWeb));
    if (web == nullptr)
    {
        return maud_errorCapacity;
    }
    *web = (maudWeb){.handle = maudWebOpen(), .streams = (maudWebStream*)(web + 1), .bytes = bytes};
    memset(web->streams, 0, (size_t)streams * sizeof(maudWebStream));
    context->native = web;
    if (web->handle == 0)
    {
        maudContextRelease(context, web, bytes, alignof(maudWeb));
        context->native = nullptr;
        return maud_errorUnsupported;
    }
    maudWebAddProcessor(web->handle);
    uint32_t rate = (uint32_t)maudWebRate(web->handle);
    maudDeviceSpec spec = {
        .info =
            {
                .direction = maud_directionOutput,
                .nativeLayout = maud_layoutStereo,
                .nativeSampleRate = rate,
                .minSampleRate = rate,
                .maxSampleRate = rate,
            },
        .name = "Default",
        .nameLength = 7,
        .key = "default",
        .keyLength = 7,
    };
    maudDeviceId device;
    maudResult result = maudAddDevice(context, &spec, &device);
    context->held = maudWebHeld(web->handle) != 0;
    if (result != maud_success)
    {
        maudWebClose(web->handle);
        maudContextRelease(context, web, bytes, alignof(maudWeb));
        context->native = nullptr;
    }
    return result;
}

static void CloseContext(maudContext* context)
{
    maudWeb* web = context->native;
    maudWebClose(web->handle);
    maudContextRelease(context, web, web->bytes, alignof(maudWeb));
    context->native = nullptr;
}

// Follows the browser's hold on the context.
static void Pump(maudContext* context)
{
    maudWeb* web = context->native;
    maudHoldStreams(context, maudWebHeld(web->handle) != 0);
}

static void ResumeContext(maudContext* context)
{
    maudWeb* web = context->native;
    maudWebResume(web->handle);
}

// Web Audio runs every node at the AudioContext's rate: a native stream
// takes it, and a required or converted rate must be it. Capture comes
// later.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)device;
    uint32_t rate = (uint32_t)maudWebRate(((const maudWeb*)context->native)->handle);
    if (def->mode == maud_modePull || def->direction != maud_directionOutput ||
        (def->ratePolicy != maud_rateNative && def->sampleRate != rate))
    {
        return maud_errorUnsupported;
    }
    *formatOut = (maudStreamFormat){
        .sampleRate = rate,
        .periodFrames = def->periodFrames != 0 ? def->periodFrames : QUANTUM_FRAMES,
        .layout = def->layout,
        .ratePolicy = def->ratePolicy,
    };
    return maud_success;
}

static maudWebStream* EntryOf(maudContext* context, const maudStreamSlot* slot)
{
    maudWeb* web = context->native;
    return &web->streams[slot - context->streams.slots];
}

static maudResult AttachStream(maudContext* context, maudStreamSlot* slot)
{
    maudWeb* web = context->native;
    maudWebStream* entry = EntryOf(context, slot);
    uint32_t channels = slot->core.period.channelCount;
    entry->chunkBytes = (size_t)CHUNK_FRAMES * channels * sizeof(float);
    entry->chunk = maudContextAllocate(context, entry->chunkBytes, alignof(float));
    if (entry->chunk == nullptr)
    {
        return maud_errorCapacity;
    }
    entry->node = maudWebOpenNode(web->handle, context, (int)(slot - context->streams.slots),
                                  (int)channels, (int)CHUNK_FRAMES, (int)QUEUE_DEPTH);
    return maud_success;
}

static void DetachStream(maudContext* context, maudStreamSlot* slot)
{
    maudWebStream* entry = EntryOf(context, slot);
    maudWebCloseNode(entry->node);
    maudContextRelease(context, entry->chunk, entry->chunkBytes, alignof(float));
    *entry = (maudWebStream){0};
}

// A stopped stream renders silence; what it had queued is dropped.
static void SetStreamActive(maudContext* context, maudStreamSlot* slot, bool active)
{
    if (!active)
    {
        maudWebDropNode(EntryOf(context, slot)->node);
    }
}

static const maudBackend s_web = {
    .kind = maud_backendWeb,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .attachStream = AttachStream,
    .detachStream = DetachStream,
    .setStreamActive = SetStreamActive,
    .retargetStream = nullptr,
    .resumeContext = ResumeContext,
    .rendersOnCaller = false,
};

const maudBackend* maudGetWebBackend(void)
{
    return &s_web;
}
