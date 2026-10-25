// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A stalled host never glitches the output: a native output stream's
// callback latches the spatializer's results every block while the host
// steps the spatializer every 5 ms, and the host's any-hit query sleeps
// 50 ms inside one step, as a game thread stalled in its physics would.
// No block that starts during the stall waits for the host, and each
// step's results say which step made them (the wall is up on odd
// steps), so a torn read shows: these hold whatever the platform does.
// Over a clean window the stream also keeps running, keeps the last
// whole step and counts no underrun. A window is clean when the
// platform kept up on its own: the fixture's graph counted no xrun, the
// stream none before the stall, and this thread's sleeps woke on time.
// On a loaded machine the platform delivers late or not at all, which
// says nothing about the library, so those timing checks are excused,
// and every run says which it was, and whether any block started during
// the stall for the behavior checks to watch. Without a device that
// opens, or with a stream the platform suspends for want of one, the test
// is skipped, unless MAUD_REQUIRE_PIPEWIRE or MAUD_REQUIRE_COREAUDIO is
// set, and then the native backend must be the one it names.

#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
// getenv reads the test's switches; the C runtime's warning that it is
// unsafe is about its result's lifetime, which the test does not keep.
#define _CRT_SECURE_NO_WARNINGS

#include "test_harness.h"
// The sound-server fixture's daemon runs on a Linux host only; elsewhere
// there is no graph to read, and iOS has no system() for its helper.
#if defined(__linux__) && !defined(__ANDROID__)
#define STALL_FIXTURE
#include "test_daemon.h"
#endif

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
#endif
#include "test_wait.h"

// The xruns every node of the fixture's graph has counted, summed;
// false where there is no fixture or the count cannot be read. It takes
// about a second.
#if defined(STALL_FIXTURE)
static bool GraphXruns(unsigned long long* count)
{
    return GraphXrunsEnd(GraphXrunsBegin(), count);
}
#else
static bool GraphXruns(unsigned long long* count)
{
    (void)count;
    return false;
}

// No sound-server fixture here: no graph to read.
static bool HaveDaemon(void)
{
    return false;
}

// How late a sleep may wake before the window counts as starved.
#define LATE_WAKEUP 10000000LL
#endif

// Sleeps, and returns how many nanoseconds past the time asked it woke.
static int64_t PauseLate(int milliseconds)
{
    int64_t before = maudGetHostNanoseconds();
    // hygiene: the host's own steps and stall, which the test times
    Pause(milliseconds);
    return maudGetHostNanoseconds() - before - (int64_t)milliseconds * 1000000;
}

// The window's verdict, printed on every run and, in a GitHub Actions
// job, added to its summary, so that runs show whether CI judges the
// timing checks at all. The iOS and Android jobs, whose tests run where
// the summary file is not, grep the printed line from CTest's log
// (.github/workflows/ci.yml, "Stall verdict"): keep its prefix.
static void Report(const char* verdict)
{
    printf("stall window: %s\n", verdict);
    const char* summary = getenv("GITHUB_STEP_SUMMARY");
    FILE* file = summary != nullptr ? fopen(summary, "a") : nullptr;
    if (file != nullptr)
    {
        fprintf(file, "- `stall`: timing checks %s\n", verdict);
        fclose(file);
    }
}

static void Drain(maudContext* context)
{
    maudNotification ignored;
    while (maudNextNotification(context, &ignored) == maud_success)
    {
    }
}

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
    // The blocks that started during the stall, and the longest of them
    // in nanoseconds, timed to its end.
    atomic_uint startedInStall;
    atomic_llong longestInStall;
    // Read inside the stall, by the host.
    unsigned blocksBefore;
    unsigned blocksAfter;
    unsigned latchedInStall;
    maudContext* context;
    maudStreamId stream;
    uint64_t underrunsBefore;
    // The latest this thread woke from a sleep over the judged window.
    int64_t latestWakeup;
} Shared;

static Shared s_shared;

static void Render(const maudStreamBlock* block, void* user)
{
    Shared* shared = user;
    int64_t start = maudGetHostNanoseconds();
    bool inStall = atomic_load(&shared->stalling);
    if (inStall)
    {
        atomic_fetch_add(&shared->startedInStall, 1);
    }
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
    if (inStall)
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
    // hygiene: the stall itself, a host thread stuck in its physics
    int64_t late = PauseLate(STALL_MS);
    shared->latestWakeup = late > shared->latestWakeup ? late : shared->latestWakeup;
    atomic_store(&shared->stalling, false);
    shared->blocksAfter = atomic_load(&shared->blocks);
    shared->latchedInStall = atomic_load(&shared->latched);
}

int main(void)
{
    maudBackendKind requiredKind = maud_backendNative;
    if (getenv("MAUD_REQUIRE_PIPEWIRE") != nullptr)
    {
        requiredKind = maud_backendPipewire;
    }
    else if (getenv("MAUD_REQUIRE_COREAUDIO") != nullptr)
    {
        requiredKind = maud_backendCoreAudio;
    }
    bool required = requiredKind != maud_backendNative;
    maudContextDef contextDef = maudDefaultContextDef();
    maudContext* context = nullptr;
    if (maudCreateContext(&contextDef, &context) != maud_success)
    {
        CHECK(!required, "a native context");
        return required ? 1 : PlatformAbsent("no native backend opens here");
    }
    maudBackendKind chosen = maudGetContextBackend(context);
    if (required && chosen != requiredKind)
    {
        printf("the native backend is kind %d, and this run requires kind %d\n", (int)chosen,
               (int)requiredKind);
    }
    CHECK(!required || chosen == requiredKind, "the native backend is the one this run requires");
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
        return required ? 1 : PlatformAbsent("no native output stream opens here");
    }
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, shared->stream, &format) == maud_success, "format");
    // The host drains its notifications as a game does once a frame,
    // which on some platforms is what connects the stream; then it steps
    // every 5 ms, the stalled step stalling the drain too.
    POLL_UNTIL(3000, "ten blocks from the stream", Drain(context),
               atomic_load(&shared->blocks) >= 10);
    // A platform with no output device opens the stream suspended (the
    // Windows runners): nothing to stall under there.
    maudStreamStatus first = {0};
    CHECK(maudGetStreamStatus(context, shared->stream, &first) == maud_success, "status");
    if (atomic_load(&shared->blocks) < 10 && !required && first.suspension != maud_suspendNone)
    {
        printf("the stream is suspended (reason %d)\n", (int)first.suspension);
        CHECK(maudDestroyContext(context) == maud_success, "destroyed");
        maudDestroySpatializer(shared->spatializer);
        return s_failures == 0 ? PlatformAbsent("no output device for the stream") : 1;
    }
    if (atomic_load(&shared->blocks) < 10)
    {
        printf("%u blocks; started %d, suspension %d\n", atomic_load(&shared->blocks),
               (int)first.started, (int)first.suspension);
    }
    CHECK(atomic_load(&shared->blocks) >= 10, "the stream runs");
    unsigned long long graphBefore = 0;
    unsigned long long graphAfter = 0;
    bool graphRead = GraphXruns(&graphBefore);
    maudPose listener = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudStreamStatus status = {0};
    for (unsigned step = 1; step <= STEPS; ++step)
    {
        atomic_store(&shared->step, step);
        CHECK(maudSimulateDirect(shared->spatializer, &listener) == maud_success, "a step");
        Drain(context);
        // hygiene: the host steps every 5 ms, as a game's thread does
        int64_t late = PauseLate(5);
        if (step >= STALLED_STEP && step <= STALLED_STEP + 10)
        {
            shared->latestWakeup = late > shared->latestWakeup ? late : shared->latestWakeup;
        }
        // The underruns over the stall, read about 50 ms after it, once
        // the platform has had time to report a late one: a window as
        // narrow as that, since some platforms (the iOS simulator) report
        // an underrun now and then with no stall at all.
        if (step == STALLED_STEP + 10)
        {
            CHECK(maudGetStreamStatus(context, shared->stream, &status) == maud_success, "status");
        }
    }
    graphRead = GraphXruns(&graphAfter) && graphRead;
    // On the fixture the graph's count must be read and unchanged; on a
    // platform without one, the stream's count and the sleeps judge.
    bool clean = (!HaveDaemon() || (graphRead && graphAfter == graphBefore)) &&
                 shared->underrunsBefore == 0 && shared->latestWakeup <= LATE_WAKEUP;
    double periodMs = 1000.0 * format.periodFrames / format.sampleRate;
    unsigned during = shared->blocksAfter - shared->blocksBefore;
    long long longest = atomic_load(&shared->longestInStall);
    printf("stall: %u blocks of %.1f ms, the longest %.3f ms; latched step %u; underruns %llu "
           "before, %llu after\n",
           during, periodMs, (double)longest / 1e6, shared->latchedInStall,
           (unsigned long long)shared->underrunsBefore, (unsigned long long)status.underruns);
    CHECK(longest < 5000000, "no block waited for the stalled host");
    CHECK(atomic_load(&shared->torn) == 0, "every step read whole");
    // With no block started during the stall, the behavior checks above
    // watched nothing; the verdict says so.
    const char* unobserved = atomic_load(&shared->startedInStall) == 0
                                 ? "; behavior unobserved: no block started during the stall"
                                 : "";
    char verdict[320];
    if (clean)
    {
        snprintf(verdict, sizeof(verdict), "judged%s", unobserved);
    }
    else
    {
        snprintf(verdict, sizeof(verdict),
                 "excused: graph xruns %llu to %llu%s, %llu underruns before the stall, a "
                 "sleep %.1f ms late%s",
                 graphBefore, graphAfter, HaveDaemon() && !graphRead ? " (not read)" : "",
                 (unsigned long long)shared->underrunsBefore, (double)shared->latestWakeup / 1e6,
                 unobserved);
    }
    Report(verdict);
    if (clean)
    {
        // Blocks keep coming: at least 60% of what 50 ms holds, the rest
        // for a platform that pulls in bursts.
        CHECK(during >= (unsigned)(0.6 * STALL_MS / periodMs), "the stream ran through the stall");
        CHECK(shared->latchedInStall == STALLED_STEP - 1, "the last whole step stayed latched");
        CHECK(status.underruns == 0, "no underrun over the stall");
    }
    CHECK(maudStopStream(context, shared->stream) == maud_success, "stopped");
    CHECK(maudDestroyContext(context) == maud_success, "destroyed");
    maudDestroySpatializer(shared->spatializer);
    return s_failures == 0 ? 0 : 1;
}
