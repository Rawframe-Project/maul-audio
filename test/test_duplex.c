// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Duplex streams on the offline backend, where the test feeds the input
// and renders the output itself: one callback with both, the slip
// policy's silence and drops counted, the pair's two slots, and the
// def's checks.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/offline.h"
#include "maul-audio/stream.h"

#include <stdlib.h>
#include <string.h>

#define PERIOD 64u

static long s_live;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_live++;
    return malloc(size);
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    s_live--;
    free(memory);
}

typedef struct Echo
{
    uint32_t calls;
    uint32_t bothSeen;
    // The first input sample of the last call.
    float first;
} Echo;

// Plays the input back, mono to both channels.
static void EchoBack(const maudStreamBlock* block, void* user)
{
    Echo* echo = user;
    echo->calls++;
    echo->bothSeen += block->input != nullptr && block->output != nullptr ? 1u : 0u;
    echo->first = block->input != nullptr ? block->input[0] : -1.0f;
    for (uint32_t i = 0; block->input != nullptr && i < block->frameCount * 2; ++i)
    {
        block->output[i] = block->input[i];
    }
}

static maudContext* OfflineContext(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    def.limits.streams = 2;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "context");
    return context;
}

static maudStreamDef DuplexDef(Echo* echo)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionDuplex;
    def.mode = maud_modePull;
    def.periodFrames = PERIOD;
    def.callback = EchoBack;
    def.user = echo;
    return def;
}

// Feeds frames whose samples are their index in the fed sequence.
static void Feed(maudContext* context, maudStreamId stream, uint32_t from, uint32_t frames)
{
    float samples[2 * 16 * PERIOD];
    for (uint32_t i = 0; i < frames; ++i)
    {
        samples[2 * i] = (float)(from + i);
        samples[2 * i + 1] = (float)(from + i);
    }
    CHECK(maudFeedStream(context, stream, samples, frames) == maud_success, "feed");
}

static uint64_t Slipped(const maudContext* context, maudStreamId stream)
{
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success &&
              status.drift == maud_driftSlip,
          "a duplex stream declares its slip");
    return status.slippedFrames;
}

static void TestBothInOneCallback(void)
{
    maudContext* context = OfflineContext();
    Echo echo = {0};
    maudStreamDef def = DuplexDef(&echo);
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "create");
    maudStreamDef other = maudDefaultStreamDef();
    other.mode = maud_modePull;
    other.callback = EchoBack;
    maudStreamId third = {0, 0};
    CHECK(maudCreateStream(context, &other, &third) == maud_errorCapacity,
          "the pair takes two slots");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    float out[2 * PERIOD];
    CHECK(maudRenderStream(context, stream, out, PERIOD) == maud_success, "render before input");
    CHECK(echo.bothSeen == 1 && echo.first == 0.0f, "silence before the input arrives");
    CHECK(Slipped(context, stream) == 0, "which is not a slip");
    Feed(context, stream, 0, PERIOD);
    CHECK(maudRenderStream(context, stream, out, PERIOD) == maud_success, "render");
    CHECK(echo.calls == 2 && echo.first == 0.0f && out[2 * (PERIOD - 1)] == (float)(PERIOD - 1),
          "the input fed comes back in the same callback");
    CHECK(maudRenderStream(context, stream, out, PERIOD) == maud_success, "render short");
    CHECK(Slipped(context, stream) == PERIOD, "a period missing slips as silence");
    // Ten periods into a ring of eight: the last two do not fit and
    // drop as they come; then playback drops the oldest six of the eight,
    // keeping two.
    Feed(context, stream, PERIOD, 10 * PERIOD);
    CHECK(maudRenderStream(context, stream, out, PERIOD) == maud_success, "render after a flood");
    CHECK(Slipped(context, stream) == PERIOD + 2 * PERIOD + 6 * PERIOD,
          "a full ring drops the newest, playback the oldest beyond two periods");
    CHECK(echo.first == (float)(PERIOD + 6 * PERIOD), "and the two periods after them play");
    uint32_t named = 0;
    for (uint32_t index = 1; index <= 2; ++index)
    {
        for (uint32_t generation = 0; generation < 4; ++generation)
        {
            uint64_t ignored = 0;
            named += maudGetStreamPosition(context, (maudStreamId){index, generation}, &ignored) ==
                             maud_success
                         ? 1u
                         : 0u;
        }
    }
    CHECK(named == 1, "only the duplex stream's id names a stream, not its input half");
    uint64_t position = 0;
    CHECK(maudGetStreamPosition(context, stream, &position) == maud_success &&
              position == 4 * PERIOD,
          "the position is the output's");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    CHECK(maudCreateStream(context, &other, &third) == maud_success, "both slots free again");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
    CHECK(s_live == 0, "every block returned");
}

static void TestDefs(void)
{
    maudContext* context = OfflineContext();
    Echo echo = {0};
    maudStreamDef def = DuplexDef(&echo);
    maudDeviceId output = {0, 0};
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &output) ==
              maud_success,
          "the output");
    def.inputDevice = output;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid,
          "an output named as the input");
    CHECK(maudGetContextMisuse(context) == 1, "counted as misuse");
    def = DuplexDef(&echo);
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "a plain duplex def");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context with it");
    CHECK(s_live == 0, "every block returned");
    maudStreamDef plain = maudDefaultStreamDef();
    CHECK(plain.inputDevice.index1 == 0, "no input device by default");
}

static void TestStopsBothHalves(void)
{
    maudContext* context = OfflineContext();
    Echo echo = {0};
    maudStreamDef def = DuplexDef(&echo);
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "create");
    float frames[2 * PERIOD] = {0};
    CHECK(maudFeedStream(context, stream, frames, PERIOD) == maud_errorState,
          "not fed before it starts");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(maudFeedStream(context, stream, frames, PERIOD) == maud_success, "fed once started");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    CHECK(maudFeedStream(context, stream, frames, PERIOD) == maud_errorState,
          "the input stops with it");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

int main(void)
{
    TestBothInOneCallback();
    TestDefs();
    TestStopsBothHalves();
    return s_failures == 0 ? 0 : 1;
}
