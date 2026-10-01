// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The host's lifecycle on the offline backend: suspending a context
// suspends its streams with maud_suspendHost and resuming lets them run,
// a lost device keeps its reason, a stream made meanwhile waits too, and
// the callback may not call it.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/notification.h"
#include "maul-audio/offline.h"
#include "maul-audio/stream.h"

#include <string.h>

#define PERIOD 64u

static maudContext* s_context;
static maudResult s_fromCallback = maud_success;
static bool s_tryFromCallback;

static void Fill(const maudStreamBlock* block, void* user)
{
    (void)user;
    if (s_tryFromCallback)
    {
        s_fromCallback = maudSetContextSuspended(s_context, true);
        s_tryFromCallback = false;
    }
    memset(block->output, 0, (size_t)block->frameCount * 2 * sizeof(float));
}

static maudStreamId Open(maudDeviceId device, bool start)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.periodFrames = PERIOD;
    def.device = device;
    def.callback = Fill;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(s_context, &def, &stream) == maud_success, "create");
    if (start)
    {
        CHECK(maudStartStream(s_context, stream) == maud_success, "start");
    }
    return stream;
}

static maudSuspendReason ReasonOf(maudStreamId stream)
{
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(s_context, stream, &status) == maud_success, "status");
    return status.suspension;
}

// Counts the notifications of a kind and reason waiting, draining all.
static uint32_t Drain(maudNotificationKind kind, maudSuspendReason reason)
{
    uint32_t count = 0;
    maudNotification record;
    while (maudNextNotification(s_context, &record) == maud_success)
    {
        count += record.kind == kind && record.reason == reason ? 1u : 0u;
    }
    return count;
}

static bool Renders(maudStreamId stream)
{
    float frames[2 * PERIOD];
    return maudRenderStream(s_context, stream, frames, PERIOD) == maud_success;
}

static void TestSuspendAndResume(void)
{
    maudStreamId running = Open((maudDeviceId){0, 0}, true);
    maudStreamId idle = Open((maudDeviceId){0, 0}, false);
    (void)Drain(maud_notifyStreamSuspended, maud_suspendNone);
    CHECK(Renders(running), "it renders before");
    CHECK(maudSetContextSuspended(s_context, true) == maud_success, "suspend");
    CHECK(ReasonOf(running) == maud_suspendHost && ReasonOf(idle) == maud_suspendHost,
          "both streams wait for the host");
    CHECK(Drain(maud_notifyStreamSuspended, maud_suspendHost) == 2, "each told");
    CHECK(!Renders(running), "nothing renders while suspended");
    CHECK(maudSetContextSuspended(s_context, true) == maud_success &&
              Drain(maud_notifyStreamSuspended, maud_suspendHost) == 0,
          "suspending again does nothing");
    maudStreamId late = Open((maudDeviceId){0, 0}, true);
    CHECK(ReasonOf(late) == maud_suspendHost && !Renders(late), "a stream made meanwhile waits");
    CHECK(maudSetContextSuspended(s_context, false) == maud_success, "resume");
    CHECK(ReasonOf(running) == maud_suspendNone && Renders(running) && Renders(late),
          "they run again");
    CHECK(Drain(maud_notifyStreamResumed, maud_suspendNone) == 3, "each told");
    CHECK(maudDestroyStream(s_context, running) == maud_success &&
              maudDestroyStream(s_context, idle) == maud_success &&
              maudDestroyStream(s_context, late) == maud_success,
          "destroy");
}

// A stream following the default loses it when its direction has no
// device, and keeps that reason through a host suspension; a device that
// arrives while the host is suspended leaves it waiting for the host,
// and it runs once the host is back.
static void TestLostDevice(void)
{
    maudDeviceId original = {0, 0};
    CHECK(maudGetDefaultDevice(s_context, maud_directionOutput, maud_roleGeneral, &original) ==
              maud_success,
          "the default output");
    maudStreamId stream = Open((maudDeviceId){0, 0}, true);
    CHECK(maudRemoveOfflineDevice(s_context, original) == maud_success, "unplug it");
    CHECK(ReasonOf(stream) == maud_suspendNoDevice, "the stream has no device");
    CHECK(maudSetContextSuspended(s_context, true) == maud_success, "suspend");
    CHECK(ReasonOf(stream) == maud_suspendNoDevice, "a missing device keeps its reason");
    maudOfflineDeviceDef deviceDef = maudDefaultOfflineDeviceDef();
    deviceDef.key = "another";
    deviceDef.keyLength = 7;
    maudDeviceId device = {0, 0};
    CHECK(maudAddOfflineDevice(s_context, &deviceDef, &device) == maud_success, "plug another");
    CHECK(maudSetOfflineDefaultDevice(s_context, maud_roleGeneral, device) == maud_success,
          "make it the default");
    CHECK(ReasonOf(stream) == maud_suspendHost, "back while suspended, it waits for the host");
    CHECK(maudSetContextSuspended(s_context, false) == maud_success, "resume");
    CHECK(ReasonOf(stream) == maud_suspendNone && Renders(stream), "and then runs");
    CHECK(maudDestroyStream(s_context, stream) == maud_success, "destroy");
}

static void TestFromCallback(void)
{
    maudStreamId stream = Open((maudDeviceId){0, 0}, true);
    uint64_t before = maudGetContextMisuse(s_context);
    s_tryFromCallback = true;
    CHECK(Renders(stream), "render");
    CHECK(s_fromCallback == maud_errorState, "the callback may not suspend the context");
    CHECK(maudGetContextMisuse(s_context) == before + 1, "counted as misuse");
    CHECK(maudSetContextSuspended(nullptr, true) == maud_errorInvalid, "no context");
    CHECK(maudDestroyStream(s_context, stream) == maud_success, "destroy");
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    CHECK(maudCreateContext(&def, &s_context) == maud_success, "context");
    TestSuspendAndResume();
    TestLostDevice();
    TestFromCallback();
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy the context");
    return s_failures == 0 ? 0 : 1;
}
