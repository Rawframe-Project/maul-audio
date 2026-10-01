// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web backend in a browser, as test/web/run_page.js runs it. The
// page holds audio until a gesture: the test makes a stream, finds it
// held by the policy, logs that it waits, and the driver's click calls
// maudResumeContext. The stream then plays; its rate, stopping and
// refusals are checked, and the result is logged for the driver, which
// runs the page with and without cross-origin isolation.

#include "../test_harness.h"
#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#include <emscripten/eventloop.h>
#include <stdio.h>

typedef enum Step
{
    stepWaitForGesture,
    stepWaitForResume,
    stepWaitForBlocks,
    stepMeasure,
    stepStopped,
    stepRestarted,
    stepDone,
} Step;

static maudContext* s_context;
static maudStreamId s_stream;
static Step s_step;
static uint32_t s_blocks;
static uint32_t s_wrongSize;
static double s_since;
static uint64_t s_position;
static int s_window;
static double s_best;

static void CountBlocks(const maudStreamBlock* block, void* user)
{
    (void)user;
    s_wrongSize += block->frameCount != 256 ? 1u : 0u;
    s_blocks++;
}

// Makes the page's clicks call TestGesture.
EM_JS(void, ListenForGesture, (void),
      { document.addEventListener("click", function() { _TestGesture(); }); });

EMSCRIPTEN_KEEPALIVE void TestGesture(void);

void TestGesture(void)
{
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
    CHECK(maudDestroyStream(s_context, s_stream) == maud_success, "destroy while running");
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy");
    printf("MAUD_TEST_RESULT %s\n", s_failures == 0 ? "pass" : "fail");
    s_step = stepDone;
}

// Three windows of two seconds, long enough that the fallback's chunks
// of 512 frames and the 10 ms steps blur little; the best is the rate
// (a stalled clock only lowers one), at most 2% above and 6% below.
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
    if (++s_window < 3)
    {
        return;
    }
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(s_context, s_stream, &format) == maud_success, "format");
    if (!(s_best > format.sampleRate * 0.94 && s_best < format.sampleRate * 1.02))
    {
        printf("measured %.0f frames/s, expected %u\n", s_best, format.sampleRate);
        CHECK(false, "at the context's rate");
    }
    CHECK(s_wrongSize == 0, "in whole periods");
    CHECK(maudStopStream(s_context, s_stream) == maud_success, "stop");
    s_blocks = 0;
    s_step = stepStopped;
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
        if (resumed)
        {
            maudStreamStatus status;
            CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
                      status.suspension == maud_suspendNone,
                  "the stream runs after the gesture");
            s_step = stepWaitForBlocks;
        }
        break;
    case stepWaitForBlocks:
        if (s_blocks >= 20)
        {
            s_since = now;
            s_position = Position();
            s_step = stepMeasure;
        }
        break;
    case stepMeasure:
        Measure(now);
        break;
    case stepStopped:
        if (now - s_since > 300.0)
        {
            CHECK(s_blocks == 0, "no callbacks once stopped");
            CHECK(maudStartStream(s_context, s_stream) == maud_success, "start again");
            s_step = stepRestarted;
        }
        break;
    case stepRestarted:
        if (s_blocks >= 20)
        {
            Finish();
        }
        break;
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
    def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.direction = maud_directionInput;
    CHECK(maudCreateStream(s_context, &def, &stream) == maud_errorUnsupported, "no capture yet");
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
    maudStreamStatus status;
    CHECK(maudGetStreamStatus(s_context, s_stream, &status) == maud_success &&
              status.suspension == maud_suspendPolicy,
          "held by the autoplay policy");
    ListenForGesture();
    printf("MAUD_TEST_WAITING_FOR_GESTURE\n");
    s_step = stepWaitForResume;
    emscripten_set_interval(Step_, 10.0, nullptr);
    return 0;
}
