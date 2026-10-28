// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ALSA backend. Devices are listed wherever libasound loads. Streams
// run on the default PCM, which the test's own ~/.asoundrc makes a plug
// over a PulseAudio server whose "hardware" runs at 44.1 kHz only,
// never the machine's sound hardware; without a server they are
// skipped, unless MAUD_REQUIRE_ALSA is set. An allocator refusing each
// block of a context's creation, and of a stream's, is a capacity error.

#include "test_clock.h"
#include "test_daemon.h"
#include "test_harness.h"
#include "test_names.h"
#include "test_wait.h"

#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <dirent.h>
#include <ftw.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// The test's HOME, removed when it ends.
static char s_home[] = "/tmp/maud-alsa-XXXXXX";

// Makes HOME a fresh directory whose .asoundrc makes the default PCM a
// plug over the pulse plugin, fixed at 44.1 kHz.
static bool UseTestHome(void)
{
    if (mkdtemp(s_home) == nullptr)
    {
        return false;
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/.asoundrc", s_home);
    FILE* file = fopen(path, "w");
    if (file == nullptr)
    {
        return false;
    }
    fputs("pcm.!default { type plug; slave { pcm \"pulse\"; rate 44100 } }\n"
          "ctl.!default { type pulse }\n",
          file);
    fclose(file);
    return setenv("HOME", s_home, 1) == 0;
}

static int RemoveEntry(const char* path, const struct stat* status, int flag, struct FTW* walk)
{
    (void)status;
    (void)flag;
    (void)walk;
    return remove(path);
}

// Removes the test's HOME and what libpulse wrote into it.
static void RemoveTestHome(void)
{
    nftw(s_home, RemoveEntry, 8, FTW_DEPTH | FTW_PHYS);
}

typedef struct Blocks
{
    _Atomic(uint32_t) count;
    _Atomic(uint32_t) wrongSize;
    _Atomic(uint32_t) withInput;
    // Stalls the callback once, past the device buffer, at this block.
    uint32_t stallAt;
    uint32_t periodFrames;
    // The thread the callback runs on, once known.
    _Atomic(bool) known;
    pthread_t thread;
} Blocks;

static void CountBlocks(const maudStreamBlock* block, void* user)
{
    Blocks* blocks = user;
    if (block->frameCount != blocks->periodFrames)
    {
        atomic_fetch_add(&blocks->wrongSize, 1);
    }
    if (block->input != nullptr)
    {
        atomic_fetch_add(&blocks->withInput, 1);
    }
    if (!atomic_load_explicit(&blocks->known, memory_order_relaxed))
    {
        blocks->thread = pthread_self();
        atomic_store_explicit(&blocks->known, true, memory_order_release);
    }
    if (blocks->stallAt != 0 && atomic_load(&blocks->count) == blocks->stallAt)
    {
        // hygiene: stalls the callback past the buffer so the PCM runs dry
        Pause(200);
    }
    atomic_fetch_add(&blocks->count, 1);
}

static bool WaitForBlocks(const Blocks* blocks, uint32_t count)
{
    POLL_UNTIL(5000, "the stream's blocks", (void)0, atomic_load(&blocks->count) >= count);
    return atomic_load(&blocks->count) >= count;
}

// The library's ALSA stream threads, by their name in /proc.
static int ThreadCount(void)
{
    int count = 0;
    DIR* tasks = opendir("/proc/self/task");
    if (tasks == nullptr)
    {
        return -1;
    }
    for (struct dirent* entry = readdir(tasks); entry != nullptr; entry = readdir(tasks))
    {
        char path[300];
        char name[32] = {0};
        snprintf(path, sizeof(path), "/proc/self/task/%s/comm", entry->d_name);
        FILE* comm = entry->d_name[0] != '.' ? fopen(path, "r") : nullptr;
        if (comm != nullptr)
        {
            count +=
                fgets(name, sizeof(name), comm) != nullptr && strncmp(name, "maud-alsa", 9) == 0;
            fclose(comm);
        }
    }
    closedir(tasks);
    return count;
}

// The stream threads still listed after a join, waiting up to a second
// for them to leave: pthread_join returns once a thread has cleared its
// id, before the kernel drops it from /proc/self/task. A thread never
// joined stays listed.
static int ThreadsAfterJoin(void)
{
    int count = 0;
    POLL_UNTIL(1000, "the joined stream threads to leave /proc", count = ThreadCount(), count == 0);
    return count;
}

// The stream's rate by its own clock (test_clock.h), in windows of 2 s
// judged by the graph's and the stream's xruns and the machine's
// promptness (test_daemon.h), the context drained meanwhile. A window's
// rate is off by where its two clock stamps fall: through the PulseAudio
// plugin, clean windows of a second spread from -1.9 to +2.0 % on a
// loaded machine, as wide as the 2 % a rate is judged by, and windows of
// 2 s spread a third as much on the same machine.
static ClockRate MeasureRate(maudContext* context, maudStreamId stream)
{
    GraphWatch watch = {.context = context, .stream = stream, .milliseconds = 2000};
    return StreamClockRate(context, stream, GraphWindow, &watch, 0.0);
}

static bool KeyIs(const maudContext* context, maudDeviceId device, const char* key)
{
    char bytes[128];
    size_t length = 0;
    return maudGetDeviceKey(context, device, bytes, sizeof(bytes), &length) == maud_success &&
           length == strlen(key) && memcmp(bytes, key, length) == 0;
}

// Whether the machine has a sound card's control device.
static bool HasCard(void)
{
    DIR* devices = opendir("/dev/snd");
    bool found = false;
    for (struct dirent* entry = devices != nullptr ? readdir(devices) : nullptr;
         entry != nullptr && !found; entry = readdir(devices))
    {
        found = strncmp(entry->d_name, "controlC", 8) == 0;
    }
    if (devices != nullptr)
    {
        closedir(devices);
    }
    return found;
}

// Whether the one sink input on the server is corked.
static bool Corked(void)
{
    return system("pactl list sink-inputs | grep -q 'Corked: yes'") == 0;
}

static void TestDevices(const maudContext* context)
{
    uint32_t endpoints = 0;
    CHECK(maudGetContextBackend(context) == maud_backendAlsa, "ALSA");
    for (int direction = 0; direction < 2; ++direction)
    {
        maudDeviceId ids[64];
        uint32_t count = 0;
        CHECK(maudGetDevices(context, (maudDirection)direction, ids, 64, &count) == maud_success,
              "list");
        CHECK(count >= 1 && KeyIs(context, ids[0], "default"), "the default PCM comes first");
        maudDeviceId current = {0, 0};
        CHECK(maudGetDefaultDevice(context, (maudDirection)direction, maud_roleGeneral, &current) ==
                      maud_success &&
                  current.index1 == ids[0].index1,
              "and is the default");
        maudDeviceInfo info = {0};
        CHECK(maudGetDeviceInfo(context, ids[0], &info) == maud_success, "info");
        CHECK(info.nativeSampleRate == 0 && info.nativeLayout == maud_layoutNone,
              "its rate is unknown until it opens");
        endpoints += count - 1;
        for (uint32_t i = 0; i < count && i < 64; ++i)
        {
            char name[512];
            size_t named = 0;
            CHECK(maudGetDeviceName(context, ids[i], name, sizeof(name), &named) == maud_success &&
                      named > 0 && named <= 256 && IsUtf8(name, named),
                  "every device has a name in UTF-8 within the limit");
        }
        for (uint32_t i = 1; i < count && i < 64; ++i)
        {
            char key[128];
            size_t length = 0;
            CHECK(maudGetDeviceKey(context, ids[i], key, sizeof(key), &length) == maud_success &&
                      length > 8 && memcmp(key, "hw:CARD=", 8) == 0,
                  "every other device is a hardware endpoint");
        }
    }
    CHECK(endpoints > 0 || !HasCard(), "a machine with a sound card lists its endpoints");
}

static maudStreamId OpenDefault(maudContext* context, maudDirection direction, Blocks* blocks,
                                maudStreamDef* def)
{
    def->direction = direction;
    def->periodFrames = 256;
    def->callback = CountBlocks;
    def->user = blocks;
    blocks->periodFrames = 256;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, def, &stream) == maud_success, "create");
    return stream;
}

// Drains notifications until one of kind for stream arrives, with reason
// when it is a suspension, stopping at it. Whether one did.
static bool DrainToNotice(maudContext* context, maudStreamId stream, maudNotificationKind kind,
                          maudSuspendReason reason)
{
    maudNotification record;
    while (maudNextNotification(context, &record) == maud_success)
    {
        if (record.kind == kind && record.stream.index1 == stream.index1 &&
            record.stream.generation == stream.generation &&
            (kind != maud_notifyStreamSuspended || record.reason == reason))
        {
            return true;
        }
    }
    return false;
}

// Drains notifications for up to a second until one of kind for stream
// arrives, with reason when it is a suspension.
static bool NoticeFor(maudContext* context, maudStreamId stream, maudNotificationKind kind,
                      maudSuspendReason reason)
{
    bool arrived = false;
    POLL_UNTIL(1000, "a notification about the stream",
               arrived = DrainToNotice(context, stream, kind, reason), arrived);
    return arrived;
}

// The host suspends the context: the stream is suspended for the host,
// gives no callbacks, and plays again once the host resumes.
static void TestHostSuspended(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenDefault(context, maud_directionOutput, &blocks, &def);
    CHECK(maudStartStream(context, stream) == maud_success && WaitForBlocks(&blocks, 20),
          "it plays");
    CHECK(maudSetContextSuspended(context, true) == maud_success &&
              NoticeFor(context, stream, maud_notifyStreamSuspended, maud_suspendHost),
          "suspended for the host");
    // hygiene: lets callbacks in flight at the suspension end before counting
    Pause(100);
    uint32_t count = atomic_load(&blocks.count);
    // hygiene: gives a callback while suspended 300 ms to show
    Pause(300);
    CHECK(atomic_load(&blocks.count) == count, "no callbacks while suspended");
    CHECK(maudSetContextSuspended(context, false) == maud_success &&
              NoticeFor(context, stream, maud_notifyStreamResumed, maud_suspendNone),
          "resumed");
    count = atomic_load(&blocks.count);
    CHECK(WaitForBlocks(&blocks, count + 20), "and plays");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// The server behind the default PCM ends its stream, as a PipeWire policy
// or an administrator can, by destroying the newest sink input's node:
// the PCM is recovered, which reconnects it, and plays on.
static void TestServerEnded(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenDefault(context, maud_directionOutput, &blocks, &def);
    CHECK(maudStartStream(context, stream) == maud_success && WaitForBlocks(&blocks, 20),
          "it plays");
    CHECK(system("pw-cli destroy \"$(pactl list sink-inputs | "
                 "awk -F'\\\"' '/object.id/{id=$2} END{print id}')\" >/dev/null 2>&1") == 0,
          "the server ends it");
    // hygiene: lets the server's end reach the stream before its count is taken
    Pause(200);
    uint32_t ended = atomic_load(&blocks.count);
    CHECK(WaitForBlocks(&blocks, ended + 20), "it plays on, recovered");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

static void TestOutputStream(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenDefault(context, maud_directionOutput, &blocks, &def);
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success &&
              format.sampleRate == 44100,
          "native at the hardware's rate nearest 48 kHz");
    CHECK(ThreadCount() == 0, "no thread before start");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(ThreadCount() == 1, "one thread while it runs");
    CHECK(WaitForBlocks(&blocks, 20), "blocks arrive");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(RateHolds(MeasureRate(context, stream), 44100.0), "at its rate");
    CHECK(StreamClockIsSound(context, stream, true, true, Pause),
          "its clock maps frames to host time");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    CHECK(ThreadsAfterJoin() == 0, "joined when it stops");
    POLL_UNTIL(2000, "the queue's drop", (void)0, Corked());
    CHECK(Corked(), "and what it queued is dropped");
    uint32_t stopped = atomic_load(&blocks.count);
    // hygiene: gives a callback after the stop 100 ms to show
    Pause(100);
    CHECK(atomic_load(&blocks.count) == stopped, "no callbacks once stopped");
    CHECK(maudStartStream(context, stream) == maud_success, "start again");
    CHECK(WaitForBlocks(&blocks, stopped + 20), "it runs again");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy while running");
    CHECK(ThreadsAfterJoin() == 0, "and joined");
    def = maudDefaultStreamDef();
    def.ratePolicy = maud_rateRequired;
    def.sampleRate = 48000;
    def.callback = CountBlocks;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "a required rate the hardware lacks");
    Blocks required = {0};
    def.sampleRate = 44100;
    stream = OpenDefault(context, maud_directionOutput, &required, &def);
    CHECK(maudStartStream(context, stream) == maud_success, "start at 44.1 kHz");
    CHECK(WaitForBlocks(&required, 10), "it runs");
    CHECK(RateHolds(MeasureRate(context, stream), 44100.0), "at the rate required");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.callback = CountBlocks;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no pull mode");
}

// A callback late past the device buffer: the stream runs on, and counts
// the gap. ALSA's PulseAudio plugin, which the test's default PCM is, plays
// silence through the stall with no -EPIPE; the stream counts it from the
// queue it finds empty. The -EPIPE recovery path runs only on hardware
// (docs/operations/device-checklist.md).
static void TestXrun(maudContext* context)
{
    Blocks blocks = {.stallAt = 30};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenDefault(context, maud_directionOutput, &blocks, &def);
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(&blocks, 31), "past the stall");
    CHECK(WaitForBlocks(&blocks, 80), "it runs on");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success && status.underruns > 0,
          "the stall counted as an underrun");
    CHECK(RateHolds(MeasureRate(context, stream), 44100.0), "at its rate");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

static void Interrupt(int signal)
{
    (void)signal;
}

// Signals to the stream's thread, each cutting its poll short (EINTR),
// as a host's profiler or timers may: the stream runs on.
static void TestSignals(maudContext* context)
{
    struct sigaction action = {.sa_handler = Interrupt};
    struct sigaction before;
    sigemptyset(&action.sa_mask);
    CHECK(sigaction(SIGUSR1, &action, &before) == 0, "a handler");
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenDefault(context, maud_directionOutput, &blocks, &def);
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(&blocks, 5), "blocks arrive");
    if (atomic_load_explicit(&blocks.known, memory_order_acquire))
    {
        for (int i = 0; i < 20; ++i)
        {
            pthread_kill(blocks.thread, SIGUSR1);
            // hygiene: spaces the signals over the stream's writes
            Pause(5);
        }
    }
    uint32_t signalled = atomic_load(&blocks.count);
    CHECK(WaitForBlocks(&blocks, signalled + 40), "it runs on after the signals");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    sigaction(SIGUSR1, &before, nullptr);
}

static void TestInputStream(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenDefault(context, maud_directionInput, &blocks, &def);
    CHECK(maudStartStream(context, stream) == maud_success, "start capture");
    CHECK(WaitForBlocks(&blocks, 20), "captured blocks arrive");
    CHECK(atomic_load(&blocks.withInput) == atomic_load(&blocks.count), "each with input");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(RateHolds(MeasureRate(context, stream), 44100.0), "at its rate");
    CHECK(StreamClockIsSound(context, stream, false, true, Pause),
          "its clock maps frames to host time");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success && status.voiceReported &&
              status.voiceActive == maud_voiceNone,
          "ALSA reports no voice processing");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// A PCM that fails to open is refused, and alsa-lib's complaint about it
// goes nowhere.
static void TestSilentFailure(maudContext* context)
{
    setenv("PULSE_SERVER", "unix:/nonexistent/maud-no-server", 1);
    fflush(stderr);
    int saved = dup(2);
    FILE* capture = tmpfile();
    CHECK(capture != nullptr && saved >= 0 && dup2(fileno(capture), 2) >= 0, "capture stderr");
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.user = &blocks;
    maudStreamId stream = {0, 0};
    maudResult result = maudCreateStream(context, &def, &stream);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    unsetenv("PULSE_SERVER");
    CHECK(result == maud_errorPlatform, "a PCM that cannot open is refused");
    struct stat written;
    CHECK(fstat(fileno(capture), &written) == 0 && written.st_size == 0, "and nothing is printed");
    fclose(capture);
}

// The default PCM has no exclusive mode, which a sound server shares: a stream asking for it on
// the default output device is refused, not shared.
static void TestExclusiveRefused(maudContext* context)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.share = maud_shareExclusive;
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &def.device) ==
              maud_success,
          "the default output");
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "exclusive use is refused");
    def.share = maud_shareShared;
    maudStreamStatus status = {.exclusive = true};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudGetStreamStatus(context, stream, &status) == maud_success && !status.exclusive,
          "and opened shared, it reports that it shares");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// An allocator that refuses its call numbered failAt, counting from 1,
// and counts the blocks still held.
typedef struct Refusing
{
    int calls;
    int failAt;
    int live;
} Refusing;

static void* RefusingAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    Refusing* refusing = context;
    if (++refusing->calls == refusing->failAt)
    {
        return nullptr;
    }
    refusing->live++;
    return malloc(size);
}

static void RefusingFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    Refusing* refusing = context;
    refusing->live--;
    free(memory);
}

// A refused block of a context's creation, or of a stream's, is a
// capacity error that holds nothing; the context stays usable.
static void TestRefusingAllocator(bool server)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendAlsa;
    Refusing refusing = {0};
    def.allocator = (maudAllocator){RefusingAlloc, RefusingFree, &refusing};
    maudContext* context = nullptr;
    for (int failAt = 1; failAt < 64 && context == nullptr; ++failAt)
    {
        refusing = (Refusing){.failAt = failAt};
        maudResult result = maudCreateContext(&def, &context);
        CHECK(result == maud_success ||
                  (result == maud_errorCapacity && context == nullptr && refusing.live == 0),
              "a refused block is a capacity error, and nothing is held");
    }
    CHECK(context != nullptr, "created once the allocator gives every block");
    if (context == nullptr)
    {
        return;
    }
    int held = refusing.live;
    Blocks blocks = {0};
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.callback = CountBlocks;
    streamDef.user = &blocks;
    maudStreamId stream = {0, 0};
    bool opened = !server;
    for (int ahead = 1; ahead < 16 && !opened; ++ahead)
    {
        refusing.failAt = refusing.calls + ahead;
        maudResult result = maudCreateStream(context, &streamDef, &stream);
        opened = result == maud_success;
        CHECK(opened ||
                  (result == maud_errorCapacity && stream.index1 == 0 && refusing.live == held),
              "a refused stream block is a capacity error, and nothing is held");
    }
    CHECK(opened, "a stream once the allocator gives every block");
    if (server && opened)
    {
        CHECK(maudDestroyStream(context, stream) == maud_success, "destroy the stream");
    }
    CHECK(maudDestroyContext(context) == maud_success && refusing.live == 0, "every block back");
}

int main(void)
{
    if (!UseTestHome())
    {
        return 1;
    }
    atexit(RemoveTestHome);
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendAlsa;
    maudContext* context = nullptr;
    if (maudCreateContext(&def, &context) != maud_success)
    {
        return getenv("MAUD_REQUIRE_ALSA") != nullptr ? 1 : PlatformAbsent("no libasound");
    }
    TestDevices(context);
    TestSilentFailure(context);
    Blocks probe = {0};
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.callback = CountBlocks;
    streamDef.user = &probe;
    maudStreamId stream = {0, 0};
    bool server = maudCreateStream(context, &streamDef, &stream) == maud_success;
    if (server)
    {
        CHECK(maudDestroyStream(context, stream) == maud_success, "destroy the probe");
        TestOutputStream(context);
        TestServerEnded(context);
        TestHostSuspended(context);
        TestXrun(context);
        TestSignals(context);
        TestExclusiveRefused(context);
        TestInputStream(context);
    }
    CHECK(server || getenv("MAUD_REQUIRE_ALSA") == nullptr, "a server for the streams");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    TestRefusingAllocator(server);
    return s_failures == 0 ? 0 : 1;
}
