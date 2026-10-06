// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A spatializer's two sides on two threads: a simulation thread runs
// steps whose sources sit at distances encoding the step number, while a
// rendering thread latches and reads. Every latched step is whole (each
// source's distance is the step's number plus its index, from that one
// step) and newer steps are never followed by older ones.

#include "test_harness.h"

#include "maul-audio/spatializer.h"

#include <pthread.h>
#include <stdatomic.h>

enum
{
    SOURCES = 64,
    STEPS = 20000
};

static maudSpatializer* s_spatializer;
static maudSourceId s_sources[SOURCES];
static atomic_bool s_done;

static void* Simulate(void* unused)
{
    (void)unused;
    maudPose listener = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    for (int step = 1; step <= STEPS; ++step)
    {
        for (int i = 0; i < SOURCES; ++i)
        {
            maudPose pose = {{0.0f, 0.0f, -(float)(step + i)}, {0.0f, 0.0f, 0.0f, 1.0f}};
            if (maudSetSourcePose(s_spatializer, s_sources[i], &pose) != maud_success)
            {
                return nullptr;
            }
        }
        if (maudSimulateDirect(s_spatializer, &listener) != maud_success)
        {
            return nullptr;
        }
    }
    atomic_store(&s_done, true);
    return nullptr;
}

int main(void)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.sourceCapacity = SOURCES;
    CHECK(maudCreateSpatializer(&def, &s_spatializer) == maud_success, "a spatializer");
    maudSourceDef source = maudDefaultSourceDef();
    for (int i = 0; i < SOURCES; ++i)
    {
        CHECK(maudCreateSource(s_spatializer, &source, &s_sources[i]) == maud_success, "a source");
    }
    pthread_t thread;
    CHECK(pthread_create(&thread, nullptr, Simulate, nullptr) == 0, "a thread");
    uint64_t last = 0;
    long latches = 0;
    bool whole = true;
    bool ordered = true;
    while (!atomic_load(&s_done) || last < STEPS)
    {
        uint64_t step = maudLatchResults(s_spatializer);
        ordered = ordered && step >= last;
        last = step;
        if (step == 0)
        {
            continue;
        }
        latches += 1;
        for (int i = 0; i < SOURCES; ++i)
        {
            maudDirectResult r;
            whole = whole && maudGetDirectResult(s_spatializer, s_sources[i], &r) == maud_success &&
                    r.distance == (float)(step + (uint64_t)i);
        }
    }
    pthread_join(thread, nullptr);
    printf("%ld latches over %d steps\n", latches, STEPS);
    CHECK(whole, "every latched step is whole");
    CHECK(ordered, "steps never go back");
    CHECK(last == STEPS, "the last step is seen");
    maudDestroySpatializer(s_spatializer);
    return s_failures == 0 ? 0 : 1;
}
