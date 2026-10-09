// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A stalled host never glitches the output: a native output stream's
// callback latches the spatializer's results every block while the host
// steps the spatializer every 5 ms, and the host's any-hit query sleeps
// 50 ms inside one step, as a game thread stalled in its physics would.
// Over the stall the callback stays short, keeps the last whole step,
// and the stream keeps running without an underrun. Each step's results
// say which step made them (the wall is up on odd steps), so a torn
// read shows. Without a device that opens, or with a stream the
// platform suspends for want of one, the test is skipped, unless
// MAUD_REQUIRE_PIPEWIRE or MAUD_REQUIRE_COREAUDIO is set.

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
// getenv reads the test's switches; the C runtime's warning that it is
// unsafe is about its result's lifetime, which the test does not keep.
#define _CRT_SECURE_NO_WARNINGS

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/layout.h"
#include "maul-audio/notification.h"
#include "maul-audio/spatializer.h"
#include "maul-audio/stream.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void Pause(int milliseconds)
{
    Sleep((DWORD)milliseconds);
}
#else
#include <time.h>
static void Pause(int milliseconds)
{
    struct timespec span = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&span, nullptr);
}
#endif

static void Drain(maudContext* context)
{
    maudNotification ignored;
    while (maudNextNotification(context, &ignored) == maud_success)
    {
    }
}

// The exit code CTest reads as skipped.
#define SKIP         77
#define STEPS        200
#define STALLED_STEP 100
#define STALL_MS     50

typedef struct Shared
{
    maudSpatializer* spatializer;
    maudSourceId source;
    // The host's side: the step being simulated, and whether the stall
    // is under way.
    atomic_uint step;
    atomic_bool stalling;
    // The callback's side.
    atomic_uint blocks;
    atomic_uint latched;
    atomic_uint torn;
    // The longest block during the stall, in nanoseconds.
    atomic_llong longestInStall;
    // Read inside the stall, by the host.
    unsigned blocksBefore;
    unsigned blocksAfter;
    unsigned latchedInStall;
    maudContext* context;
    maudStreamId stream;
    uint64_t underrunsBefore;
} Shared;

static Shared s_shared;

static void Render(const maudStreamBlock* block, void* user)
{
    Shared* shared = user;
    int64_t start = maudGetHostNanoseconds();
    uint64_t step = maudLatchResults(shared->spatializer);
    float gain = 0.0f;
    maudDirectResult result;
    if (step != 0 &&
        maudGetDirectResult(shared->spatializer, shared->source, &result) == maud_success)
    {
        // An odd step put the wall up, an even one took it down.
        if (result.occlusion != ((step & 1u) != 0 ? 1.0f : 0.0f))
        {
            atomic_fetch_add(&shared->torn, 1);
        }
        gain = 0.1f * (1.0f - result.occlusion);
    }
    uint32_t samples = block->frameCount * maudGetLayoutChannelCount(block->layout);
    for (uint32_t i = 0; i < samples; ++i)
    {
        block->output[i] = gain;
    }
    atomic_store(&shared->latched, (unsigned)step);
    atomic_fetch_add(&shared->blocks, 1);
    if (atomic_load(&shared->stalling))
    {
        long long took = maudGetHostNanoseconds() - start;
        long long longest = atomic_load(&shared->longestInStall);
        while (took > longest &&
               !atomic_compare_exchange_weak(&shared->longestInStall, &longest, took))
        {
        }
    }
}

// The host's geometry: a wall on odd steps. Inside the stalled step it
// stalls, recording what the stream did meanwhile.
static void AnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    (void)rays;
    Shared* shared = context;
    unsigned step = atomic_load(&shared->step);
    for (uint32_t i = 0; i < count; ++i)
    {
        occluded[i] = (step & 1u) != 0;
    }
    if (step != STALLED_STEP)
    {
        return;
    }
    maudStreamStatus status;
    if (maudGetStreamStatus(shared->context, shared->stream, &status) == maud_success)
    {
        shared->underrunsBefore = status.underruns;
    }
    shared->blocksBefore = atomic_load(&shared->blocks);
    atomic_store(&shared->stalling, true);
    Pause(STALL_MS);
    atomic_store(&shared->stalling, false);
    shared->blocksAfter = atomic_load(&shared->blocks);
    shared->latchedInStall = atomic_load(&shared->latched);
}

int main(void)
{
    bool required =
        getenv("MAUD_REQUIRE_PIPEWIRE") != nullptr || getenv("MAUD_REQUIRE_COREAUDIO") != nullptr;
    maudContextDef contextDef = maudDefaultContextDef();
    maudContext* context = nullptr;
    if (maudCreateContext(&contextDef, &context) != maud_success)
    {
        CHECK(!required, "a native context");
        return required ? 1 : SKIP;
    }
    Shared* shared = &s_shared;
    maudSpatializerDef spatialDef = maudDefaultSpatializerDef();
    spatialDef.anyHit = AnyHit;
    spatialDef.rayContext = shared;
    maudSourceDef sourceDef = maudDefaultSourceDef();
    sourceDef.occlusion = maud_occlusionRay;
    maudPose source = {{0.0f, 0.0f, -3.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudCreateSpatializer(&spatialDef, &shared->spatializer) == maud_success &&
              maudCreateSource(shared->spatializer, &sourceDef, &shared->source) == maud_success &&
              maudSetSourcePose(shared->spatializer, shared->source, &source) == maud_success,
          "a spatializer with a source");
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.callback = Render;
    streamDef.user = shared;
    shared->context = context;
    maudResult opened = maudCreateStream(context, &streamDef, &shared->stream);
    if (s_failures != 0 || opened != maud_success ||
        maudStartStream(context, shared->stream) != maud_success)
    {
        CHECK(!required, "a native output stream");
        maudDestroySpatializer(shared->spatializer);
        CHECK(maudDestroyContext(context) == maud_success, "destroyed");
        return required ? 1 : SKIP;
    }
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, shared->stream, &format) == maud_success, "format");
    // The host drains its notifications as a game does once a frame,
    // which on some platforms is what connects the stream; then it steps
    // every 5 ms, the stalled step stalling the drain too.
    for (int tries = 0; tries < 300 && atomic_load(&shared->blocks) < 10; ++tries)
    {
        Drain(context);
        Pause(10);
    }
    // A platform with no output device opens the stream suspended (the
    // Windows runners): nothing to stall under there.
    maudStreamStatus first = {0};
    CHECK(maudGetStreamStatus(context, shared->stream, &first) == maud_success, "status");
    if (atomic_load(&shared->blocks) < 10 && !required && first.suspension != maud_suspendNone)
    {
        printf("skipped: the stream is suspended (reason %d)\n", (int)first.suspension);
        CHECK(maudDestroyContext(context) == maud_success, "destroyed");
        maudDestroySpatializer(shared->spatializer);
        return s_failures == 0 ? SKIP : 1;
    }
    if (atomic_load(&shared->blocks) < 10)
    {
        printf("%u blocks; started %d, suspension %d\n", atomic_load(&shared->blocks),
               (int)first.started, (int)first.suspension);
    }
    CHECK(atomic_load(&shared->blocks) >= 10, "the stream runs");
    maudPose listener = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudStreamStatus status = {0};
    for (unsigned step = 1; step <= STEPS; ++step)
    {
        atomic_store(&shared->step, step);
        CHECK(maudSimulateDirect(shared->spatializer, &listener) == maud_success, "a step");
        Drain(context);
        Pause(5);
        // The underruns over the stall, read about 50 ms after it, once
        // the platform has had time to report a late one: a window as
        // narrow as that, since some platforms (the iOS simulator) report
        // an underrun now and then with no stall at all.
        if (step == STALLED_STEP + 10)
        {
            CHECK(maudGetStreamStatus(context, shared->stream, &status) == maud_success, "status");
        }
    }
    double periodMs = 1000.0 * format.periodFrames / format.sampleRate;
    unsigned during = shared->blocksAfter - shared->blocksBefore;
    long long longest = atomic_load(&shared->longestInStall);
    printf("stall: %u blocks of %.1f ms, the longest %.3f ms; latched step %u; underruns %llu "
           "before, %llu after\n",
           during, periodMs, (double)longest / 1e6, shared->latchedInStall,
           (unsigned long long)shared->underrunsBefore, (unsigned long long)status.underruns);
    // Blocks keep coming: at least 60% of what 50 ms holds, the rest for
    // a platform that pulls in bursts.
    CHECK(during >= (unsigned)(0.6 * STALL_MS / periodMs), "the stream ran through the stall");
    CHECK(longest < 5000000, "no block waited for the stalled host");
    CHECK(shared->latchedInStall == STALLED_STEP - 1, "the last whole step stayed latched");
    CHECK(atomic_load(&shared->torn) == 0, "every step read whole");
    // A platform that underran before the stall, as a test daemon on a
    // loaded machine does, says nothing about the stall by its count;
    // the blocks and their length above still do.
    if (shared->underrunsBefore == 0)
    {
        CHECK(status.underruns == 0, "no underrun over the stall");
    }
    else
    {
        printf("the platform underran %llu times before the stall: its count is not read\n",
               (unsigned long long)shared->underrunsBefore);
    }
    CHECK(maudStopStream(context, shared->stream) == maud_success, "stopped");
    CHECK(maudDestroyContext(context) == maud_success, "destroyed");
    maudDestroySpatializer(shared->spatializer);
    return s_failures == 0 ? 0 : 1;
}
