// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PipeWire daemon going away and coming back: its devices keep
// their ids, streams on a default or a device wait for the platform and
// resume on the new daemon, one created meanwhile waits too, and one
// stopped meanwhile starts on it. A device the new daemon does not list
// goes once it has listed the rest; away longer than the backend keeps
// them, every device goes. Streams the host suspended stay suspended for
// it across a restart.
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

static maudStreamId Open(maudContext* context, maudDeviceId device, Blocks* blocks)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.device = device;
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

static bool SameDevice(maudDeviceId a, maudDeviceId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// The output device whose key is key, or the null id.
static maudDeviceId FindByKey(const maudContext* context, const char* key)
{
    maudDeviceId ids[16];
    uint32_t count = 0;
    if (maudGetDevices(context, maud_directionOutput, ids, 16, &count) != maud_success)
    {
        return (maudDeviceId){0, 0};
    }
    for (uint32_t i = 0; i < count && i < 16; ++i)
    {
        char bytes[64];
        size_t length = 0;
        if (maudGetDeviceKey(context, ids[i], bytes, sizeof(bytes), &length) == maud_success &&
            length == strlen(key) && memcmp(bytes, key, length) == 0)
        {
            return ids[i];
        }
    }
    return (maudDeviceId){0, 0};
}

// What the notifications said, for a stream on the default (0), one on
// the test sink (1) and one created while the daemon was away (2).
typedef struct Seen
{
    maudStreamId streams[3];
    maudDeviceId sink;
    maudDeviceId gone;
    maudSuspendReason reason[3];
    bool resumed[3];
    bool sinkRemoved;
    bool goneRemoved;
} Seen;

// Drains the notifications there are into seen.
static void Note(maudContext* context, Seen* seen)
{
    maudNotification record;
    while (maudNextNotification(context, &record) == maud_success)
    {
        for (int i = 0; i < 3; ++i)
        {
            bool mine = seen->streams[i].index1 != 0 && SameStream(record.stream, seen->streams[i]);
            if (mine && record.kind == maud_notifyStreamSuspended)
            {
                seen->reason[i] = record.reason;
            }
            seen->resumed[i] =
                seen->resumed[i] || (mine && record.kind == maud_notifyStreamResumed);
        }
        bool removed = record.kind == maud_notifyDeviceRemoved;
        seen->sinkRemoved = seen->sinkRemoved || (removed && SameDevice(record.device, seen->sink));
        seen->goneRemoved = seen->goneRemoved || (removed && SameDevice(record.device, seen->gone));
    }
}

// Notes for up to tries polls of 10 ms, until done.
#define NOTE_UNTIL(context, seen, tries, done)                                                     \
    for (int poll = 0; poll < (tries) && !(done); ++poll)                                          \
    {                                                                                              \
        Note(context, seen);                                                                       \
        Sleep(10);                                                                                 \
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
    CHECK(system("pactl load-module module-null-sink sink_name=maud-test-gone >/dev/null") == 0,
          "a sink the restart will not bring back");
    Seen seen = {0};
    NOTE_UNTIL(context, &seen, 300, (seen.gone = FindByKey(context, "maud-test-gone")).index1 != 0);
    seen.sink = FindByKey(context, "maud-test-sink");
    CHECK(seen.gone.index1 != 0 && seen.sink.index1 != 0, "both sinks listed");
    Blocks before = {0};
    Blocks pinned = {0};
    seen.streams[0] = Open(context, (maudDeviceId){0, 0}, &before);
    seen.streams[1] = Open(context, seen.sink, &pinned);
    CHECK(WaitForBlocks(context, &before, 10) && WaitForBlocks(context, &pinned, 10),
          "playing before");
    Blocks idle = {0};
    maudStreamId stopped = Open(context, (maudDeviceId){0, 0}, &idle);
    CHECK(WaitForBlocks(context, &idle, 5) && maudStopStream(context, stopped) == maud_success,
          "a third plays, then stops");
    Note(context, &seen);
    uint32_t outputs = OutputCount(context);
    CHECK(system(stop) == 0, "the daemon stops");
    int64_t away = maudGetHostNanoseconds();
    NOTE_UNTIL(context, &seen, 500, seen.reason[0] != 0 && seen.reason[1] != 0);
    CHECK(seen.reason[0] == maud_suspendPlatform && seen.reason[1] == maud_suspendPlatform,
          "both wait for the platform while it is away");
    CHECK(!seen.sinkRemoved && !seen.goneRemoved && OutputCount(context) == outputs,
          "its devices are kept");
    Blocks during = {0};
    seen.streams[2] = Open(context, (maudDeviceId){0, 0}, &during);
    NOTE_UNTIL(context, &seen, 100, seen.reason[2] != 0);
    CHECK(seen.reason[2] == maud_suspendPlatform, "one created while away waits too");
    CHECK(system(start) == 0, "the daemon starts again");
    NOTE_UNTIL(context, &seen, 500,
               seen.resumed[0] && seen.resumed[1] && seen.resumed[2] && seen.goneRemoved);
    int64_t listed = maudGetHostNanoseconds() - away;
    CHECK(seen.resumed[0] && seen.resumed[1] && seen.resumed[2], "all three resume");
    CHECK(seen.goneRemoved && listed < 9000000000ll,
          "the sink it does not bring back goes once the rest are listed");
    CHECK(!seen.sinkRemoved && SameDevice(FindByKey(context, "maud-test-sink"), seen.sink),
          "the sink keeps its id");
    uint32_t resumed = atomic_load(&before.count);
    uint32_t resumedPinned = atomic_load(&pinned.count);
    CHECK(WaitForBlocks(context, &before, resumed + 20) &&
              WaitForBlocks(context, &pinned, resumedPinned + 20),
          "both play on the new daemon");
    CHECK(WaitForBlocks(context, &during, 20), "the late stream plays too");
    uint32_t idled = atomic_load(&idle.count);
    CHECK(maudStartStream(context, stopped) == maud_success &&
              WaitForBlocks(context, &idle, idled + 20),
          "the stopped one starts on the new daemon");
    // A second sink, loaded now, takes a node slot below the test sink's
    // (adopted into a new slot by the restart above), so across another
    // restart the returning test sink meets it first and must pass it by
    // name.
    CHECK(system("pactl load-module module-null-sink sink_name=maud-test-gone >/dev/null") == 0,
          "a second sink the restart will not bring back");
    seen.gone = (maudDeviceId){0, 0};
    seen.goneRemoved = false;
    NOTE_UNTIL(context, &seen, 300, (seen.gone = FindByKey(context, "maud-test-gone")).index1 != 0);
    CHECK(seen.gone.index1 != 0 && !SameDevice(seen.gone, seen.sink), "the second sink listed");
    for (int i = 0; i < 3; ++i)
    {
        seen.reason[i] = 0;
        seen.resumed[i] = false;
    }
    CHECK(system(stop) == 0, "the daemon stops again");
    NOTE_UNTIL(context, &seen, 500, seen.reason[1] != 0);
    CHECK(seen.reason[1] == maud_suspendPlatform, "the pinned stream waits again");
    CHECK(system(start) == 0, "the daemon starts once more");
    NOTE_UNTIL(context, &seen, 500, seen.resumed[1] && seen.goneRemoved);
    CHECK(seen.resumed[1] && seen.goneRemoved, "the pinned stream resumes, the second sink goes");
    CHECK(!seen.sinkRemoved && SameDevice(FindByKey(context, "maud-test-sink"), seen.sink),
          "the sink keeps its id past the other name");
    // Away for longer than the devices are kept.
    Note(context, &seen);
    seen.resumed[0] = false;
    CHECK(system(stop) == 0, "the daemon stops for longer");
    NOTE_UNTIL(context, &seen, 1300,
               seen.sinkRemoved && seen.reason[1] == maud_suspendDeviceLost &&
                   seen.reason[0] == maud_suspendNoDevice);
    CHECK(seen.sinkRemoved && OutputCount(context) == 0, "every device goes");
    CHECK(seen.reason[1] == maud_suspendDeviceLost, "the pinned stream with its device");
    CHECK(seen.reason[0] == maud_suspendNoDevice, "the one on the default waits for a device");
    CHECK(system(start) == 0, "the daemon starts again");
    NOTE_UNTIL(context, &seen, 500, seen.resumed[0]);
    resumed = atomic_load(&before.count);
    CHECK(seen.resumed[0] && WaitForBlocks(context, &before, resumed + 20),
          "the one on the default plays on the new sink");
    CHECK(OutputCount(context) >= 1, "the devices are back");
    // The host suspends the context: both streams are suspended for the
    // host and silent, stay so across a restart, and play on resume.
    Seen host = {.sink = FindByKey(context, "maud-test-sink")};
    Blocks blocks[2] = {0};
    host.streams[0] = Open(context, (maudDeviceId){0, 0}, &blocks[0]);
    host.streams[1] = Open(context, host.sink, &blocks[1]);
    CHECK(WaitForBlocks(context, &blocks[0], 20) && WaitForBlocks(context, &blocks[1], 20),
          "two more play");
    Note(context, &host);
    CHECK(maudSetContextSuspended(context, true) == maud_success, "the host suspends");
    NOTE_UNTIL(context, &host, 100, host.reason[0] != 0 && host.reason[1] != 0);
    CHECK(host.reason[0] == maud_suspendHost && host.reason[1] == maud_suspendHost,
          "both suspended for the host");
    Sleep(100);
    uint32_t counts[2] = {atomic_load(&blocks[0].count), atomic_load(&blocks[1].count)};
    Sleep(300);
    CHECK(atomic_load(&blocks[0].count) == counts[0] && atomic_load(&blocks[1].count) == counts[1],
          "no callbacks while suspended");
    CHECK(system(stop) == 0, "the daemon stops");
    NOTE_UNTIL(context, &host, 30, false);
    CHECK(system(start) == 0, "the daemon starts again");
    NOTE_UNTIL(context, &host, 500, SameDevice(FindByKey(context, "maud-test-sink"), host.sink));
    Note(context, &host);
    CHECK(SameDevice(FindByKey(context, "maud-test-sink"), host.sink) &&
              host.reason[0] == maud_suspendHost && host.reason[1] == maud_suspendHost &&
              !host.resumed[0] && !host.resumed[1],
          "across a restart they stay suspended for the host");
    CHECK(maudSetContextSuspended(context, false) == maud_success, "the host resumes");
    NOTE_UNTIL(context, &host, 500, host.resumed[0] && host.resumed[1]);
    counts[0] = atomic_load(&blocks[0].count);
    counts[1] = atomic_load(&blocks[1].count);
    CHECK(host.resumed[0] && host.resumed[1] &&
              WaitForBlocks(context, &blocks[0], counts[0] + 20) &&
              WaitForBlocks(context, &blocks[1], counts[1] + 20),
          "both resume and play");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
