// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ALSA backend. Devices are listed wherever libasound loads. Streams
// run on the default PCM, which the test's own ~/.asoundrc makes a plug
// over a PulseAudio server whose "hardware" runs at 44.1 kHz only,
// never the machine's sound hardware; without a server they are
// skipped, unless MAUD_REQUIRE_ALSA is set.

#include "test_clock.h"
#include "test_harness.h"

#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <dirent.h>
#include <ftw.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define SKIP 77

static void Sleep(int milliseconds)
{
    struct timespec pause = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&pause, nullptr);
}

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
    if (blocks->stallAt != 0 && atomic_load(&blocks->count) == blocks->stallAt)
    {
        Sleep(200);
    }
    atomic_fetch_add(&blocks->count, 1);
}

static bool WaitForBlocks(const Blocks* blocks, uint32_t count)
{
    for (int tries = 0; tries < 500 && atomic_load(&blocks->count) < count; ++tries)
    {
        Sleep(10);
    }
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
    int count = ThreadCount();
    for (int tries = 0; tries < 100 && count != 0; ++tries)
    {
        Sleep(10);
        count = ThreadCount();
    }
    return count;
}

// Frames the stream moves per second of wall time over one window.
static double MeasureWindow(const maudContext* context, maudStreamId stream, int milliseconds)
{
    struct timespec start;
    struct timespec end;
    uint64_t first = 0;
    uint64_t last = 0;
    clock_gettime(CLOCK_MONOTONIC, &start);
    CHECK(maudGetStreamPosition(context, stream, &first) == maud_success, "position");
    Sleep(milliseconds);
    CHECK(maudGetStreamPosition(context, stream, &last) == maud_success, "position");
    clock_gettime(CLOCK_MONOTONIC, &end);
    double seconds =
        (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) * 1e-9;
    return (double)(last - first) / seconds;
}

// The stream's rate: the best of five windows of a second. A
// loaded machine can stall the platform's clock, which only lowers a
// window's count, so the best window is the one that shows the rate.
static double MeasureRate(const maudContext* context, maudStreamId stream)
{
    double best = 0.0;
    for (int window = 0; window < 5; ++window)
    {
        double rate = MeasureWindow(context, stream, 1000);
        best = rate > best ? rate : best;
    }
    return best;
}

// Whether a measured rate is the expected one: at most 2% above it,
// and up to 6% below, since a stalled clock only lowers a count. The
// ranges of 44.1 and 48 kHz do not meet. The measurement is printed
// when it is not.
static bool Near(double rate, double expected)
{
    bool within = rate > expected * 0.94 && rate < expected * 1.02;
    if (!within)
    {
        fprintf(stderr, "measured %.0f frames/s, expected %.0f\n", rate, expected);
    }
    return within;
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
    CHECK(Near(MeasureRate(context, stream), 44100.0), "at its rate");
    CHECK(StreamClockIsSound(context, stream, true, true, Sleep),
          "its clock maps frames to host time");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    CHECK(ThreadsAfterJoin() == 0, "joined when it stops");
    Sleep(200);
    CHECK(Corked(), "and what it queued is dropped");
    uint32_t stopped = atomic_load(&blocks.count);
    Sleep(100);
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
    CHECK(Near(MeasureRate(context, stream), 44100.0), "at the rate required");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.callback = CountBlocks;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no pull mode");
}

// A callback late past the device buffer: the stream runs on. PipeWire's
// ALSA plugin, which the test's default PCM is, absorbs the stall
// without an xrun (no -EPIPE, no underrun counted), so the recovery
// path runs only on hardware (docs/device-checklist.md).
static void TestXrun(maudContext* context)
{
    Blocks blocks = {.stallAt = 30};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenDefault(context, maud_directionOutput, &blocks, &def);
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(&blocks, 31), "past the stall");
    CHECK(WaitForBlocks(&blocks, 80), "it runs on");
    CHECK(Near(MeasureRate(context, stream), 44100.0), "at its rate");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
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
    CHECK(Near(MeasureRate(context, stream), 44100.0), "at its rate");
    CHECK(StreamClockIsSound(context, stream, false, true, Sleep),
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
        return getenv("MAUD_REQUIRE_ALSA") != nullptr ? 1 : SKIP;
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
        TestXrun(context);
        TestExclusiveRefused(context);
        TestInputStream(context);
    }
    CHECK(server || getenv("MAUD_REQUIRE_ALSA") == nullptr, "a server for the streams");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
