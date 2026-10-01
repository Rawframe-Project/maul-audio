// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ALSA backend. Devices are listed wherever libasound loads. Streams
// run on the default PCM, which the test points at a PulseAudio server
// through its own ~/.asoundrc, never at the machine's sound hardware;
// without a server they are skipped, unless MAUD_REQUIRE_ALSA is set.

#include "test_harness.h"

#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <dirent.h>
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

// Makes HOME a fresh directory whose .asoundrc sends the default PCM to
// the pulse plugin.
static bool UseTestHome(void)
{
    static char home[] = "/tmp/maud-alsa-XXXXXX";
    if (mkdtemp(home) == nullptr)
    {
        return false;
    }
    char path[64];
    snprintf(path, sizeof(path), "%s/.asoundrc", home);
    FILE* file = fopen(path, "w");
    if (file == nullptr)
    {
        return false;
    }
    fputs("pcm.!default { type pulse }\nctl.!default { type pulse }\n", file);
    fclose(file);
    return setenv("HOME", home, 1) == 0;
}

typedef struct Blocks
{
    _Atomic(uint32_t) count;
    _Atomic(uint32_t) wrongSize;
    _Atomic(uint32_t) withInput;
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

// Frames the stream moves per second of wall time, over 1.5 seconds.
static double MeasureRate(const maudContext* context, maudStreamId stream)
{
    struct timespec start;
    struct timespec end;
    uint64_t first = 0;
    uint64_t last = 0;
    clock_gettime(CLOCK_MONOTONIC, &start);
    CHECK(maudGetStreamPosition(context, stream, &first) == maud_success, "position");
    Sleep(1500);
    CHECK(maudGetStreamPosition(context, stream, &last) == maud_success, "position");
    clock_gettime(CLOCK_MONOTONIC, &end);
    double seconds =
        (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) * 1e-9;
    return (double)(last - first) / seconds;
}

static bool Near(double rate, double expected, double tolerance)
{
    return rate > expected * (1.0 - tolerance) && rate < expected * (1.0 + tolerance);
}

static bool KeyIs(const maudContext* context, maudDeviceId device, const char* key)
{
    char bytes[128];
    size_t length = 0;
    return maudGetDeviceKey(context, device, bytes, sizeof(bytes), &length) == maud_success &&
           length == strlen(key) && memcmp(bytes, key, length) == 0;
}

static void TestDevices(const maudContext* context)
{
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
        for (uint32_t i = 1; i < count && i < 64; ++i)
        {
            char key[128];
            size_t length = 0;
            CHECK(maudGetDeviceKey(context, ids[i], key, sizeof(key), &length) == maud_success &&
                      length > 8 && memcmp(key, "hw:CARD=", 8) == 0,
                  "every other device is a hardware endpoint");
        }
    }
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
              format.sampleRate == 48000,
          "native at the rate nearest 48 kHz");
    CHECK(ThreadCount() == 0, "no thread before start");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(ThreadCount() == 1, "one thread while it runs");
    CHECK(WaitForBlocks(&blocks, 20), "blocks arrive");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(Near(MeasureRate(context, stream), 48000.0, 0.04), "at its rate");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    CHECK(ThreadCount() == 0, "joined when it stops");
    uint32_t stopped = atomic_load(&blocks.count);
    Sleep(100);
    CHECK(atomic_load(&blocks.count) == stopped, "no callbacks once stopped");
    CHECK(maudStartStream(context, stream) == maud_success, "start again");
    CHECK(WaitForBlocks(&blocks, stopped + 20), "it runs again");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy while running");
    CHECK(ThreadCount() == 0, "and joined");
    Blocks required = {0};
    def = maudDefaultStreamDef();
    def.ratePolicy = maud_rateRequired;
    def.sampleRate = 44100;
    stream = OpenDefault(context, maud_directionOutput, &required, &def);
    CHECK(maudStartStream(context, stream) == maud_success, "start at 44.1 kHz");
    CHECK(WaitForBlocks(&required, 10), "it runs");
    CHECK(Near(MeasureRate(context, stream), 44100.0, 0.04), "at the rate required");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.callback = CountBlocks;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no pull mode");
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
    CHECK(Near(MeasureRate(context, stream), 48000.0, 0.04), "at its rate");
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

int main(void)
{
    if (!UseTestHome())
    {
        return 1;
    }
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
        TestInputStream(context);
    }
    CHECK(server || getenv("MAUD_REQUIRE_ALSA") == nullptr, "a server for the streams");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
