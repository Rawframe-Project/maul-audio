// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's worker thread (whitebox): started, it runs its function
// once and reports itself running; joined, it has finished and is not
// running, and a second join does nothing. Built where a backend uses
// it: WASAPI, PulseAudio and ALSA.

#include "test_harness.h"
#include "worker.h"

#include <stdatomic.h>

static atomic_int s_runs;

static void Run(void* user)
{
    atomic_fetch_add((atomic_int*)user, 1);
}

int main(void)
{
    maudWorker worker = {0};
    bool started = maudStartWorker(&worker, Run, &s_runs, "maud-test");
    CHECK(started && worker.running, "started and running");
    maudJoinWorker(&worker);
    CHECK(atomic_load(&s_runs) == 1 && !worker.running, "joined after one run");
    maudJoinWorker(&worker);
    CHECK(atomic_load(&s_runs) == 1, "a second join does nothing");
    return s_failures == 0 ? 0 : 1;
}
