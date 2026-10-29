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
    CHECK(maudCreateOfflineDevice(context, &def, &id) == maud_success, "add a device");
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
              maudStream_Start(context, stream) == maud_success,
          "a loopback stream opens and starts");
    float frames[25];
    for (uint32_t i = 0; i < 25; ++i)
    {
        frames[i] = (float)i;
    }
    CHECK(maudStream_Feed(context, stream, frames, 25) == maud_success, "fed what was played");
    CHECK(recorder.blocks == 2 && !recorder.sawOutput, "two input blocks, no output");
    CHECK(recorder.firstSamples[0] == 0.0f && recorder.firstSamples[1] == 10.0f &&
              recorder.positions[1] == 10,
          "the blocks hold the fed frames in order");
    float out[1];
    CHECK(maudStream_Render(context, stream, out, 1) == maud_errorInvalid, "not rendered");
    CHECK(maudContext_GetMisuse(context) == 1, "rendering one is misuse");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestDevices(void)
{
    maudContext* context = Offline();
    maudDeviceId initial = {0, 0};
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &initial) ==
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
    CHECK(maudStream_GetStatus(context, stream, &status) == maud_success &&
              SameDevice(status.device, second),
          "it runs on that output");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy it");
    def.device = (maudDeviceId){0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStream_GetStatus(context, stream, &status) == maud_success &&
              SameDevice(status.device, initial),
          "the null id takes the default output");
    CHECK(maudDevice_SetOfflineDefault(context, second, maud_roleGeneral) == maud_success &&
              maudStream_GetStatus(context, stream, &status) == maud_success &&
              SameDevice(status.device, second),
          "and follows it");
    CHECK(maudDevice_SetOfflineDefault(context, microphone, maud_roleGeneral) == maud_success &&
              maudStream_GetStatus(context, stream, &status) == maud_success &&
              SameDevice(status.device, second),
          "not the default input");
    CHECK(maudContext_GetMisuse(context) == 1, "the input device was misuse");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

// Loopback streams lose their output as output streams do: a pinned
// one is suspended as lost and fed no more, a following one moves to the
// new default output, or waits for one when no output is left.
static void TestLost(void)
{
    maudContext* context = Offline();
    maudDeviceId initial = {0, 0};
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &initial) ==
              maud_success,
          "the default output");
    maudDeviceId second = AddDevice(context, maud_directionOutput);
    CHECK(maudDevice_SetOfflineDefault(context, second, maud_roleGeneral) == maud_success,
          "the second output is the default");
    Recorder pinnedHeard = {0};
    Recorder followerHeard = {0};
    maudStreamDef def = LoopbackDef(&pinnedHeard);
    def.device = second;
    maudStreamId pinned = {0, 0};
    maudStreamId follower = {0, 0};
    CHECK(maudCreateStream(context, &def, &pinned) == maud_success &&
              maudStream_Start(context, pinned) == maud_success,
          "a loopback stream pinned to the second output");
    def = LoopbackDef(&followerHeard);
    CHECK(maudCreateStream(context, &def, &follower) == maud_success &&
              maudStream_Start(context, follower) == maud_success,
          "one following the default");
    CHECK(maudDestroyOfflineDevice(context, second) == maud_success, "unplug the second output");
    maudStreamStatus status;
    CHECK(maudStream_GetStatus(context, pinned, &status) == maud_success && status.started &&
              status.suspension == maud_suspendDeviceLost && status.device.index1 == 0,
          "the pinned one is lost");
    CHECK(maudStream_GetStatus(context, follower, &status) == maud_success &&
              status.suspension == maud_suspendNone && SameDevice(status.device, initial),
          "the follower moved to the default output left");
    float played[8 * 480] = {0};
    CHECK(maudStream_Feed(context, pinned, played, 480) == maud_errorState,
          "the lost one is fed no more");
    CHECK(maudStream_Feed(context, follower, played, 480) == maud_success &&
              followerHeard.blocks > 0 && pinnedHeard.blocks == 0,
          "the follower is");
    CHECK(maudDestroyOfflineDevice(context, initial) == maud_success &&
              maudStream_GetStatus(context, follower, &status) == maud_success &&
              status.suspension == maud_suspendNoDevice,
          "with no output left, it waits for one");
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
    CHECK(maudContext_GetMisuse(context) == 0, "unsupported is not misuse");
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
    CHECK(maudContext_GetMisuse(context) == 5, "each invalid def is misuse");
    uint32_t count = 0;
    maudDeviceId id = {0, 0};
    CHECK(maudContext_GetDevices(context, maud_directionLoopback, nullptr, 0, &count) ==
                  maud_errorInvalid &&
              maudContext_GetDefaultDevice(context, maud_directionLoopback, maud_roleGeneral,
                                           &id) == maud_errorInvalid,
          "no device is a loopback one");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

int main(void)
{
    TestFedAsInput();
    TestDevices();
    TestLost();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
