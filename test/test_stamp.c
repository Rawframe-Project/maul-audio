// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clock stamp: an output's frame is heard after its callback and an
// input's was captured before it, a new stream has no host time, and a
// reader racing the writer never sees half a stamp.

#include "clock.h"
#include "test_harness.h"

#include <pthread.h>
#include <sched.h>

static void InitCore(maudStreamCore* core)
{
    atomic_init(&core->position, 0);
    atomic_init(&core->clockSequence, 0);
    atomic_init(&core->clockPosition, 0);
    atomic_init(&core->clockHost, 0);
    atomic_init(&core->clockLatency, 0);
}

static void TestDirections(void)
{
    maudStreamCore core;
    InitCore(&core);
    uint64_t position = 1;
    int64_t host = 1;
    int64_t latency = 1;
    maudReadClock(&core, &position, &host, &latency);
    CHECK(position == 0 && host == 0 && latency == 0, "no stamp yet");
    const int64_t half = 500000000;
    atomic_store(&core.position, 100);
    int64_t before = maudNowNanoseconds();
    maudStampOutputClock(&core, half);
    int64_t after = maudNowNanoseconds();
    maudReadClock(&core, &position, &host, &latency);
    CHECK(position == 100 && latency == half, "the output stamp's frame and latency");
    CHECK(host >= before + half && host <= after + half, "heard after the callback");
    before = maudNowNanoseconds();
    maudStampInputClock(&core, half);
    after = maudNowNanoseconds();
    maudReadClock(&core, &position, &host, &latency);
    CHECK(host >= before - half && host <= after - half, "captured before the callback");
    maudResetClock(&core);
    maudReadClock(&core, &position, &host, &latency);
    CHECK(host == 0 && latency == 0, "reset");
    CHECK(maudGetHostNanoseconds() >= after, "the public clock is the same one");
}

typedef struct Race
{
    maudStreamCore core;
    // The reader has started; it has finished.
    atomic_bool started;
    atomic_bool done;
} Race;

// The reads the race makes, the stamps they must see change, and how
// long it may take: far above a loaded runner's need.
#define RACE_READS   200000u
#define RACE_CHANGES 100u
#define RACE_SECONDS 30

// Stamps with the latency equal to the position, so a torn read shows,
// from when the reader starts until it finishes.
static void* Write(void* user)
{
    Race* race = user;
    int64_t deadline = maudNowNanoseconds() + RACE_SECONDS * 1000000000LL;
    while (!atomic_load(&race->started) && maudNowNanoseconds() < deadline)
    {
        sched_yield();
    }
    for (uint64_t i = 1; !atomic_load(&race->done) && maudNowNanoseconds() < deadline; ++i)
    {
        atomic_store_explicit(&race->core.position, i, memory_order_relaxed);
        maudStampOutputClock(&race->core, (int64_t)i);
    }
    return nullptr;
}

// The reader keeps reading until it has made RACE_READS reads and seen
// the stamp change RACE_CHANGES times, so that the two always overlap:
// on a loaded machine the writer could otherwise finish before the
// reader ran at all.
static void TestNoTornReads(void)
{
    Race race;
    InitCore(&race.core);
    atomic_init(&race.started, false);
    atomic_init(&race.done, false);
    pthread_t writer;
    if (pthread_create(&writer, nullptr, Write, &race) != 0)
    {
        CHECK(false, "a writer thread");
        return;
    }
    atomic_store(&race.started, true);
    int64_t deadline = maudNowNanoseconds() + RACE_SECONDS * 1000000000LL;
    uint64_t torn = 0;
    uint64_t reads = 0;
    uint64_t changes = 0;
    uint64_t last = 0;
    while ((reads < RACE_READS || changes < RACE_CHANGES) && maudNowNanoseconds() < deadline)
    {
        uint64_t position = 0;
        int64_t host = 0;
        int64_t latency = 0;
        maudReadClock(&race.core, &position, &host, &latency);
        torn += (int64_t)position != latency ? 1u : 0u;
        changes += position != last ? 1u : 0u;
        last = position;
        reads++;
    }
    atomic_store(&race.done, true);
    pthread_join(writer, nullptr);
    CHECK(torn == 0, "every read is one whole stamp");
    CHECK(reads >= RACE_READS && changes >= RACE_CHANGES,
          "the reads overlapped the writer's stamps within 30 s");
}

int main(void)
{
    TestDirections();
    TestNoTornReads();
    return s_failures == 0 ? 0 : 1;
}
