// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams on the offline backend: formats, fixed periods across any
// render size, silence before each block, input in completed periods,
// stale ids, refusals, and no allocation while rendering.

#include "test_harness.h"

#include "maul-audio/stream.h"

#include <stdlib.h>
#include <string.h>

#define MAX_BLOCKS 64

typedef struct Recorder
{
    maudContext* context;
    maudStreamId stream;
    uint32_t blocks;
    uint32_t frameCounts[MAX_BLOCKS];
    uint64_t positions[MAX_BLOCKS];
    float firstSamples[MAX_BLOCKS];
    bool writeOutput;
    bool sawStaleOutput;
    // Control calls the callback makes, and what they returned.
    bool tryControl;
    maudResult controlResult;
    maudResult renderResult;
    maudResult resumeResult;
} Recorder;

static int s_liveAllocations = 0;
static int s_allocationCalls = 0;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_allocationCalls++;
    s_liveAllocations++;
    return malloc(size);
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    s_liveAllocations--;
    free(memory);
}

// Output sample i of channel c of the stream: exact in binary32.
static float Tone(uint64_t frame, uint32_t channel)
{
    return (float)(frame % 1000u) + (float)channel * 0.5f;
}

static void Record(const maudStreamBlock* block, void* user)
{
    Recorder* recorder = user;
    uint32_t channels = maudGetLayoutChannelCount(block->layout);
    if (recorder->blocks < MAX_BLOCKS)
    {
        recorder->frameCounts[recorder->blocks] = block->frameCount;
        recorder->positions[recorder->blocks] = block->position;
        recorder->firstSamples[recorder->blocks] = block->input != nullptr ? block->input[0] : 0.0f;
    }
    recorder->blocks++;
    if (block->output != nullptr)
    {
        for (uint32_t i = 0; i < block->frameCount * channels; ++i)
        {
            recorder->sawStaleOutput = recorder->sawStaleOutput || block->output[i] != 0.0f;
        }
        for (uint32_t i = 0; recorder->writeOutput && i < block->frameCount; ++i)
        {
            for (uint32_t c = 0; c < channels; ++c)
            {
                block->output[i * channels + c] = Tone(block->position + i, c);
            }
        }
    }
    if (recorder->tryControl)
    {
        maudStreamId other = {0, 0};
        maudStreamDef def = maudDefaultStreamDef();
        recorder->controlResult = maudCreateStream(recorder->context, &def, &other);
        float frame[2];
        recorder->renderResult = maudRenderStream(recorder->context, recorder->stream, frame, 1);
        recorder->resumeResult = maudResumeContext(recorder->context);
    }
}

static maudContext* OfflineContext(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    def.limits.streams = 2;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "offline context");
    return context;
}

static maudStreamDef PullDef(Recorder* recorder)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.callback = Record;
    def.user = recorder;
    return def;
}

static maudStreamId Open(maudContext* context, const maudStreamDef* def)
{
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, def, &stream) == maud_success, "create stream");
    CHECK(maudStartStream(context, stream) == maud_success, "start stream");
    return stream;
}

static void TestNativeFormatOnTheOfflineDevice(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {0};
    maudStreamDef def = PullDef(&recorder);
    maudStreamId stream = Open(context, &def);
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success, "format");
    CHECK(format.sampleRate == 48000, "the device's rate");
    CHECK(format.periodFrames == 480, "10 ms periods by default");
    CHECK(format.layout == maud_layoutStereo, "stereo");
    CHECK(format.ratePolicy == maud_rateNative, "native policy");
    def.ratePolicy = maud_rateRequired;
    def.sampleRate = 44100;
    def.periodFrames = 256;
    def.layout = maud_layout5Point1;
    stream = Open(context, &def);
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success, "format");
    CHECK(format.sampleRate == 44100 && format.periodFrames == 256, "required rate and period");
    CHECK(format.layout == maud_layout5Point1, "5.1");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    CHECK(s_liveAllocations == 0, "every stream freed with the context");
}

static void TestBlocksAreFixedAcrossRenderSizes(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {.writeOutput = true};
    maudStreamDef def = PullDef(&recorder);
    def.periodFrames = 64;
    maudStreamId stream = Open(context, &def);
    static float rendered[2 * 1000];
    static const uint32_t sizes[] = {1, 63, 64, 65, 200, 7, 600};
    uint32_t offset = 0;
    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); ++k)
    {
        CHECK(maudRenderStream(context, stream, rendered + 2 * offset, sizes[k]) == maud_success,
              "render");
        offset += sizes[k];
    }
    bool exact = true;
    for (uint32_t i = 0; i < offset; ++i)
    {
        exact = exact && rendered[2 * i] == Tone(i, 0) && rendered[2 * i + 1] == Tone(i, 1);
    }
    CHECK(exact, "frames arrive in order whatever the render sizes");
    CHECK(recorder.blocks == (offset + 63) / 64, "one callback per period");
    bool fixed = true;
    for (uint32_t b = 0; b < recorder.blocks && b < MAX_BLOCKS; ++b)
    {
        fixed = fixed && recorder.frameCounts[b] == 64 && recorder.positions[b] == 64u * b;
    }
    CHECK(fixed, "every block is one period at its stream position");
    uint64_t position = 0;
    CHECK(maudGetStreamPosition(context, stream, &position) == maud_success, "position");
    CHECK(position == offset, "position counts rendered frames");
    CHECK(!recorder.sawStaleOutput, "output cleared before each block");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestOutputIsSilentWhenTheCallbackWritesNothing(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {.writeOutput = true};
    maudStreamDef def = PullDef(&recorder);
    def.periodFrames = 16;
    maudStreamId stream = Open(context, &def);
    float frames[2 * 16];
    CHECK(maudRenderStream(context, stream, frames, 16) == maud_success, "first block");
    recorder.writeOutput = false;
    CHECK(maudRenderStream(context, stream, frames, 16) == maud_success, "second block");
    bool silent = true;
    for (size_t i = 0; i < sizeof(frames) / sizeof(frames[0]); ++i)
    {
        silent = silent && frames[i] == 0.0f;
    }
    CHECK(silent, "no stale samples");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestInputArrivesInCompletedPeriods(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {0};
    maudStreamDef def = PullDef(&recorder);
    def.direction = maud_directionInput;
    def.layout = maud_layoutMono;
    def.periodFrames = 10;
    maudStreamId stream = Open(context, &def);
    float frames[25];
    for (uint32_t i = 0; i < 25; ++i)
    {
        frames[i] = (float)i;
    }
    CHECK(maudFeedStream(context, stream, frames, 9) == maud_success, "feed 9");
    CHECK(recorder.blocks == 0, "no block before a period is complete");
    CHECK(maudFeedStream(context, stream, frames + 9, 16) == maud_success, "feed 16");
    CHECK(recorder.blocks == 2, "two completed periods");
    CHECK(recorder.firstSamples[0] == 0.0f && recorder.firstSamples[1] == 10.0f,
          "blocks hold the fed frames in order");
    CHECK(recorder.positions[1] == 10, "second block at frame 10");
    float out[1];
    CHECK(maudRenderStream(context, stream, out, 1) == maud_errorInvalid, "render an input");
    CHECK(maudGetContextMisuse(context) == 1, "counted as misuse");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestUnsupportedAndInvalidDefs(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {0};
    maudStreamId stream = {0, 0};
    maudStreamDef def = PullDef(&recorder);
    def.mode = maud_modeCallback;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "callback mode");
    def = PullDef(&recorder);
    def.ratePolicy = maud_ratePlatformConverted;
    def.sampleRate = 44100;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no converter");
    CHECK(maudGetContextMisuse(context) == 0, "unsupported is not misuse");
    def = PullDef(&recorder);
    def.sampleRate = 44100;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "rate with native");
    def = PullDef(&recorder);
    def.ratePolicy = maud_rateRequired;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "required without rate");
    def = PullDef(&recorder);
    def.callback = nullptr;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "no callback");
    def = PullDef(&recorder);
    def.layout = maud_layoutNone;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "no layout");
    def = PullDef(&recorder);
    def.cookie = 0;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "no cookie");
    CHECK(stream.index1 == 0 && stream.generation == 0, "null id on failure");
    def = PullDef(&recorder);
    def.voice = maud_voiceEchoCancellation;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid,
          "voice processing on an output");
    def = PullDef(&recorder);
    def.direction = maud_directionInput;
    def.voice = 8;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "an unknown voice part");
    CHECK(maudGetContextMisuse(context) == 7, "each invalid def counted");
    def = PullDef(&recorder);
    def.periodFrames = 8193;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorCapacity, "period past limit");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

// The offline backend has no voice processing and says so: an input
// stream reports none, whatever it asked for; an output reports nothing.
static void TestVoiceReport(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {0};
    maudStreamDef def = PullDef(&recorder);
    def.direction = maud_directionInput;
    def.voice = maud_voiceEchoCancellation | maud_voiceNoiseSuppression | maud_voiceGainControl;
    maudStreamId input = {0, 0};
    CHECK(maudCreateStream(context, &def, &input) == maud_success, "an input asking for voice");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, input, &status) == maud_success, "its status");
    CHECK(status.voiceReported && status.voiceActive == maud_voiceNone, "reports none active");
    def = PullDef(&recorder);
    maudStreamId output = {0, 0};
    CHECK(maudCreateStream(context, &def, &output) == maud_success, "an output");
    CHECK(maudGetStreamStatus(context, output, &status) == maud_success, "its status");
    CHECK(!status.voiceReported && status.voiceActive == maud_voiceNone, "reports nothing");
    CHECK(maudDefaultStreamDef().voice == maud_voiceNone, "no voice processing by default");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

// Exclusive use needs a device and a backend that can give it; the
// offline backend cannot, and says so rather than sharing. Shared
// streams report that they are.
static void TestExclusive(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {0};
    maudStreamId stream = {0, 0};
    maudDeviceId device = {0, 0};
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &device) ==
              maud_success,
          "the device");
    CHECK(maudDefaultStreamDef().share == maud_shareShared, "shared by default");
    maudStreamDef def = PullDef(&recorder);
    def.share = maud_shareExclusive;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid,
          "exclusive on the default, which could become any device");
    def.share = maud_shareExclusive + 1;
    def.device = device;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "an unknown share mode");
    CHECK(maudGetContextMisuse(context) == 2, "both counted");
    def.share = maud_shareExclusive;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "exclusive where the backend has none");
    def.direction = maud_directionDuplex;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "nor for a duplex stream");
    CHECK(maudGetContextMisuse(context) == 2, "neither is misuse");
    def = PullDef(&recorder);
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "a shared stream");
    maudStreamStatus status = {.exclusive = true};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success && !status.exclusive,
          "reports it shares");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestStreamLimitAndStaleIds(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {0};
    maudStreamDef def = PullDef(&recorder);
    maudStreamId first = Open(context, &def);
    maudStreamId second = Open(context, &def);
    maudStreamId third = {0, 0};
    CHECK(maudCreateStream(context, &def, &third) == maud_errorCapacity, "past the limit");
    CHECK(maudDestroyStream(context, first) == maud_success, "destroy first");
    CHECK(maudDestroyStream(context, first) == maud_errorStale, "destroyed id is stale");
    CHECK(maudStartStream(context, first) == maud_errorStale, "start stale");
    float frames[2];
    CHECK(maudRenderStream(context, first, frames, 1) == maud_errorStale, "render stale");
    maudStreamId reused = Open(context, &def);
    CHECK(reused.index1 == first.index1 && reused.generation != first.generation,
          "a reused slot gets a new generation");
    CHECK(maudStartStream(context, first) == maud_errorStale, "the old id stays stale");
    CHECK(maudStartStream(context, (maudStreamId){0, 0}) == maud_errorStale, "null id");
    CHECK(maudStartStream(context, (maudStreamId){99, 1}) == maud_errorStale, "out of range");
    CHECK(maudGetContextMisuse(context) == 0, "stale ids are not misuse");
    (void)second;
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestStoppedStreamsDoNotRender(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {0};
    maudStreamDef def = PullDef(&recorder);
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "create");
    float frames[2];
    CHECK(maudRenderStream(context, stream, frames, 1) == maud_errorState, "created stopped");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(maudRenderStream(context, stream, frames, 1) == maud_success, "started renders");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    CHECK(maudRenderStream(context, stream, frames, 1) == maud_errorState, "stopped again");
    CHECK(maudRenderStream(context, stream, nullptr, 1) == maud_errorInvalid, "null frames");
    CHECK(maudRenderStream(context, stream, nullptr, 0) == maud_errorState, "zero frames");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestControlCallsFromTheCallbackAreRefused(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {.tryControl = true};
    maudStreamDef def = PullDef(&recorder);
    maudStreamId stream = Open(context, &def);
    recorder.context = context;
    recorder.stream = stream;
    float frames[2 * 480];
    CHECK(maudRenderStream(context, stream, frames, 480) == maud_success, "render");
    CHECK(recorder.controlResult == maud_errorState, "create refused on the audio thread");
    CHECK(recorder.renderResult == maud_errorState, "re-entrant render refused");
    CHECK(recorder.resumeResult == maud_errorState, "resume refused on the audio thread");
    CHECK(maudGetContextMisuse(context) == 2, "control calls counted as misuse");
    recorder.tryControl = false;
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy after rendering");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestRenderingDoesNotAllocate(void)
{
    maudContext* context = OfflineContext();
    Recorder recorder = {.writeOutput = true};
    maudStreamDef def = PullDef(&recorder);
    maudStreamId stream = Open(context, &def);
    def.direction = maud_directionInput;
    maudStreamId input = Open(context, &def);
    static float frames[2 * 4800];
    int before = s_allocationCalls;
    for (int i = 0; i < 10; ++i)
    {
        CHECK(maudRenderStream(context, stream, frames, 4800) == maud_success, "render");
        CHECK(maudFeedStream(context, input, frames, 4800) == maud_success, "feed");
    }
    CHECK(s_allocationCalls == before, "no allocation while rendering");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    CHECK(s_liveAllocations == 0, "all memory returned");
}

int main(void)
{
    TestNativeFormatOnTheOfflineDevice();
    TestBlocksAreFixedAcrossRenderSizes();
    TestOutputIsSilentWhenTheCallbackWritesNothing();
    TestInputArrivesInCompletedPeriods();
    TestUnsupportedAndInvalidDefs();
    TestStreamLimitAndStaleIds();
    TestVoiceReport();
    TestExclusive();
    TestStoppedStreamsDoNotRender();
    TestControlCallsFromTheCallbackAreRefused();
    TestRenderingDoesNotAllocate();
    return s_failures == 0 ? 0 : 1;
}
