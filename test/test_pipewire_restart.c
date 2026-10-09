// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PipeWire daemon going away and coming back: devices are removed,
// streams that follow a default wait and then resume on the new daemon.
// The daemon is stopped and started by the commands in
// MAUD_TEST_PIPEWIRE_STOP and MAUD_TEST_PIPEWIRE_START; without them,
// or without a daemon, the test is skipped.

#include "test_harness.h"

#include "maul-audio/notification.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SKIP 77

typedef struct Blocks
{
    _Atomic(uint32_t) count;
} Blocks;

static void CountBlocks(const maudStreamBlock* block, void* user)
{
    (void)block;
    Blocks* blocks = user;
    atomic_fetch_add(&blocks->count, 1);
}

static void Sleep(int milliseconds)
{
    struct timespec pause = {0, (long)milliseconds * 1000000L};
    nanosleep(&pause, nullptr);
}

static maudStreamId Follow(maudContext* context, Blocks* blocks)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.periodFrames = 256;
    def.callback = CountBlocks;
    def.user = blocks;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "create");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    return stream;
}

static bool SameStream(maudStreamId a, maudStreamId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// Drains notifications for up to five seconds until a record of kind
// for stream arrives.
static bool WaitForStream(maudContext* context, maudNotificationKind kind, maudStreamId stream,
                          maudNotification* recordOut)
{
    for (int tries = 0; tries < 500; ++tries)
    {
        while (maudNextNotification(context, recordOut) == maud_success)
        {
            if (recordOut->kind == kind && SameStream(recordOut->stream, stream))
            {
                return true;
            }
        }
        Sleep(10);
    }
    return false;
}

static bool WaitForBlocks(maudContext* context, Blocks* blocks, uint32_t count)
{
    for (int tries = 0; tries < 500 && atomic_load(&blocks->count) < count; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        Sleep(10);
    }
    return atomic_load(&blocks->count) >= count;
}

static uint32_t OutputCount(const maudContext* context)
{
    uint32_t count = 0;
    CHECK(maudGetDevices(context, maud_directionOutput, nullptr, 0, &count) == maud_success,
          "count outputs");
    return count;
}

int main(void)
{
    const char* stop = getenv("MAUD_TEST_PIPEWIRE_STOP");
    const char* start = getenv("MAUD_TEST_PIPEWIRE_START");
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    if (stop == nullptr || start == nullptr || maudCreateContext(&def, &context) != maud_success)
    {
        return SKIP;
    }
    Blocks before = {0};
    maudStreamId stream = Follow(context, &before);
    CHECK(WaitForBlocks(context, &before, 10), "playing before");
    CHECK(system(stop) == 0, "the daemon stops");
    maudNotification record;
    CHECK(WaitForStream(context, maud_notifyStreamSuspended, stream, &record), "it waits");
    CHECK(record.reason == maud_suspendNoDevice, "for a device");
    CHECK(OutputCount(context) == 0, "every device is gone");
    Blocks during = {0};
    maudStreamId late = Follow(context, &during);
    maudStreamStatus status;
    CHECK(maudGetStreamStatus(context, late, &status) == maud_success, "status");
    CHECK(status.suspension == maud_suspendNoDevice, "created while away, it waits");
    CHECK(system(start) == 0, "the daemon starts again");
    CHECK(WaitForStream(context, maud_notifyStreamResumed, stream, &record), "it resumes");
    uint32_t resumed = atomic_load(&before.count);
    CHECK(WaitForBlocks(context, &before, resumed + 20), "and plays on the new daemon");
    CHECK(WaitForBlocks(context, &during, 20), "the late stream plays too");
    CHECK(OutputCount(context) >= 1, "the devices are back");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
