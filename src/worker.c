// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Stream threads on POSIX systems.

#include "worker.h"

#include <sched.h>

// Asks for the lowest real-time priority, which ordinary users are
// usually refused; the thread runs at normal priority then.
static void* RunWorker(void* user)
{
    maudWorker* worker = user;
    struct sched_param param = {.sched_priority = sched_get_priority_min(SCHED_FIFO)};
    (void)pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    worker->run(worker->user);
    return nullptr;
}

bool maudStartWorker(maudWorker* worker, void (*run)(void* user), void* user, const char* name)
{
    worker->run = run;
    worker->user = user;
    worker->running = pthread_create(&worker->thread, nullptr, RunWorker, worker) == 0;
    if (worker->running)
    {
        (void)pthread_setname_np(worker->thread, name);
    }
    return worker->running;
}

void maudJoinWorker(maudWorker* worker)
{
    if (worker->running)
    {
        pthread_join(worker->thread, nullptr);
        worker->running = false;
    }
}
