// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Loopback streams on the offline backend: fed what their device played,
// they deliver it as input blocks; they run on an output device, follow
// the role's default output, and refuse an input device; the defs a
// loopback stream cannot take are refused, and a direction past it.

#include "test_harness.h"

#include "maul-audio/offline.h"
#include "maul-audio/stream.h"

#define MAX_BLOCKS 16

typedef struct Recorder
{
    uint32_t blocks;
    float firstSamples[MAX_BLOCKS];
    uint64_t positions[MAX_BLOCKS];
    bool sawOutput;
} Recorder;

static void Record(const maudStreamBlock* block, void* user)
{
    Recorder* recorder = user;
    if (recorder->blocks < MAX_BLOCKS)
    {
        recorder->firstSamples[recorder->blocks] =
            block->input != nullptr ? block->input[0] : -1.0f;
        recorder->positions[recorder->blocks] = block->position;
    }
    recorder->sawOutput = recorder->sawOutput || block->output != nullptr;
    recorder->blocks++;
}

static maudContext* Offline(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "offline context");
    return context;
}

static maudStreamDef LoopbackDef(Recorder* recorder)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionLoopback;
    def.mode = maud_modePull;
    def.layout = maud_layoutMono;
    def.periodFrames = 10;
    def.callback = Record;
    def.user = recorder;
    return def;
}

static maudDeviceId AddDevice(maudContext* context, maudDirection direction)
{
    maudOfflineDeviceDef def = maudDefaultOfflineDeviceDef();
    def.direction = direction;
    maudDeviceId id = {0, 0};
    CHECK(maudAddOfflineDevice(context, &def, &id) == maud_success, "add a device");
    return id;
}

static bool SameDevice(maudDeviceId a, maudDeviceId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static void TestFedAsInput(void)
{
    maudContext* context = Offline();
    Recorder recorder = {0};
    maudStreamDef def = LoopbackDef(&recorder);
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStartStream(context, stream) == maud_success,
          "a loopback stream opens and starts");
    float frames[25];
    for (uint32_t i = 0; i < 25; ++i)
    {
        frames[i] = (float)i;
    }
    CHECK(maudFeedStream(context, stream, frames, 25) == maud_success, "fed what was played");
    CHECK(recorder.blocks == 2 && !recorder.sawOutput, "two input blocks, no output");
    CHECK(recorder.firstSamples[0] == 0.0f && recorder.firstSamples[1] == 10.0f &&
              recorder.positions[1] == 10,
          "the blocks hold the fed frames in order");
    float out[1];
    CHECK(maudRenderStream(context, stream, out, 1) == maud_errorInvalid, "not rendered");
    CHECK(maudGetContextMisuse(context) == 1, "rendering one is misuse");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestDevices(void)
{
    maudContext* context = Offline();
    maudDeviceId initial = {0, 0};
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &initial) ==
              maud_success,
          "the default output");
    maudDeviceId second = AddDevice(context, maud_directionOutput);
    maudDeviceId microphone = AddDevice(context, maud_directionInput);
    Recorder recorder = {0};
    maudStreamDef def = LoopbackDef(&recorder);
    maudStreamId stream = {0, 0};
    def.device = microphone;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "not on an input");
    def.device = second;
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "on an output");
    maudStreamStatus status;
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success &&
              SameDevice(status.device, second),
          "it runs on that output");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy it");
    def.device = (maudDeviceId){0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudGetStreamStatus(context, stream, &status) == maud_success &&
              SameDevice(status.device, initial),
          "the null id takes the default output");
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleGeneral, second) == maud_success &&
              maudGetStreamStatus(context, stream, &status) == maud_success &&
              SameDevice(status.device, second),
          "and follows it");
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleGeneral, microphone) == maud_success &&
              maudGetStreamStatus(context, stream, &status) == maud_success &&
              SameDevice(status.device, second),
          "not the default input");
    CHECK(maudGetContextMisuse(context) == 1, "the input device was misuse");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestRefusals(void)
{
    maudContext* context = Offline();
    maudDeviceId output = AddDevice(context, maud_directionOutput);
    Recorder recorder = {0};
    maudStreamId stream = {0, 0};
    maudStreamDef def = LoopbackDef(&recorder);
    def.loopbackExcludesSelf = true;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "the offline backend cannot leave its host out");
    def = LoopbackDef(&recorder);
    def.share = maud_shareExclusive;
    def.device = output;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "exclusive");
    CHECK(maudGetContextMisuse(context) == 0, "unsupported is not misuse");
    def = LoopbackDef(&recorder);
    def.voice = maud_voiceEchoCancellation;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "voice processing");
    def = LoopbackDef(&recorder);
    def.contentSpatialized = true;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "a spatialized mark");
    def = LoopbackDef(&recorder);
    def.objectCount = 1;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "objects");
    def = LoopbackDef(&recorder);
    def.direction = maud_directionInput;
    def.loopbackExcludesSelf = true;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid,
          "leaving the host out is for loopback streams");
    def = LoopbackDef(&recorder);
    def.direction = (maudDirection)(maud_directionLoopback + 1);
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "a direction past it");
    CHECK(maudGetContextMisuse(context) == 5, "each invalid def is misuse");
    uint32_t count = 0;
    maudDeviceId id = {0, 0};
    CHECK(maudGetDevices(context, maud_directionLoopback, nullptr, 0, &count) ==
                  maud_errorInvalid &&
              maudGetDefaultDevice(context, maud_directionLoopback, maud_roleGeneral, &id) ==
                  maud_errorInvalid,
          "no device is a loopback one");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

int main(void)
{
    TestFedAsInput();
    TestDevices();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
