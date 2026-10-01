// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The clock stamp: an output's frame is heard after its callback and an
// input's was captured before it, a new stream has no host time, and a
// reader racing the writer never sees half a stamp.

#include "clock.h"
#include "test_harness.h"

#include <pthread.h>

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
    atomic_bool done;
} Race;

// Stamps with the latency equal to the position, so a torn read shows.
static void* Write(void* user)
{
    Race* race = user;
    for (uint64_t i = 1; i <= 200000; ++i)
    {
        atomic_store_explicit(&race->core.position, i, memory_order_relaxed);
        maudStampOutputClock(&race->core, (int64_t)i);
    }
    atomic_store(&race->done, true);
    return nullptr;
}

static void TestNoTornReads(void)
{
    Race race;
    InitCore(&race.core);
    atomic_init(&race.done, false);
    pthread_t writer;
    if (pthread_create(&writer, nullptr, Write, &race) != 0)
    {
        CHECK(false, "a writer thread");
        return;
    }
    uint64_t torn = 0;
    uint64_t reads = 0;
    while (!atomic_load(&race.done))
    {
        uint64_t position = 0;
        int64_t host = 0;
        int64_t latency = 0;
        maudReadClock(&race.core, &position, &host, &latency);
        torn += (int64_t)position != latency ? 1u : 0u;
        reads++;
    }
    pthread_join(writer, nullptr);
    CHECK(reads > 0 && torn == 0, "every read is one whole stamp");
}

int main(void)
{
    TestDirections();
    TestNoTornReads();
    return s_failures == 0 ? 0 : 1;
}
