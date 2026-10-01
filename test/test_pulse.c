// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PulseAudio backend against a running server: its sinks and
// sources as devices, the server's defaults, a sink added, made the
// default and removed with pactl, and the server restarted when
// MAUD_TEST_PIPEWIRE_STOP and MAUD_TEST_PIPEWIRE_START name commands.
// Without a server the test is skipped, unless MAUD_REQUIRE_PULSE is
// set.

#include "test_clock.h"
#include "test_harness.h"

#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SKIP 77

static void Sleep(int milliseconds)
{
    struct timespec pause = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&pause, nullptr);
}

static bool SameDevice(maudDeviceId a, maudDeviceId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// The device of direction whose key is key, or the null id.
static maudDeviceId FindByKey(const maudContext* context, maudDirection direction, const char* key)
{
    maudDeviceId ids[32];
    uint32_t count = 0;
    CHECK(maudGetDevices(context, direction, ids, 32, &count) == maud_success, "list");
    for (uint32_t i = 0; i < count && i < 32; ++i)
    {
        char bytes[256];
        size_t length = 0;
        if (maudGetDeviceKey(context, ids[i], bytes, sizeof(bytes), &length) == maud_success &&
            length == strlen(key) && memcmp(bytes, key, length) == 0)
        {
            return ids[i];
        }
    }
    return (maudDeviceId){0, 0};
}

// Whether any input device's key ends in ".monitor".
static bool HasMonitor(const maudContext* context)
{
    maudDeviceId ids[32];
    uint32_t count = 0;
    CHECK(maudGetDevices(context, maud_directionInput, ids, 32, &count) == maud_success, "inputs");
    for (uint32_t i = 0; i < count && i < 32; ++i)
    {
        char bytes[256];
        size_t length = 0;
        if (maudGetDeviceKey(context, ids[i], bytes, sizeof(bytes), &length) == maud_success &&
            length >= 8 && memcmp(bytes + length - 8, ".monitor", 8) == 0)
        {
            return true;
        }
    }
    return false;
}

// Drains notifications for up to five seconds until one of kind
// arrives; with a device given, only one about that device.
static bool WaitFor(maudContext* context, maudNotificationKind kind, maudDeviceId device,
                    maudNotification* recordOut)
{
    for (int tries = 0; tries < 500; ++tries)
    {
        while (maudNextNotification(context, recordOut) == maud_success)
        {
            if (recordOut->kind == kind &&
                (device.index1 == 0 || SameDevice(recordOut->deviceId, device)))
            {
                return true;
            }
        }
        Sleep(10);
    }
    return false;
}

// Drains notifications for up to five seconds until both roles' output
// default is device.
static bool WaitForDefaults(maudContext* context, maudDeviceId device)
{
    for (int tries = 0; tries < 500; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        maudDeviceId general = {0, 0};
        maudDeviceId communications = {0, 0};
        maudResult first =
            maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &general);
        maudResult second = maudGetDefaultDevice(context, maud_directionOutput,
                                                 maud_roleCommunications, &communications);
        if (first == maud_success && second == maud_success && SameDevice(general, device) &&
            SameDevice(communications, device))
        {
            return true;
        }
        Sleep(10);
    }
    return false;
}

// The thread that runs the test, which no callback may run on.
static pthread_t s_control;

typedef struct Blocks
{
    _Atomic(uint32_t) count;
    _Atomic(uint32_t) wrongSize;
    _Atomic(uint32_t) withInput;
    _Atomic(uint32_t) onControl;
    uint32_t periodFrames;
    // The block at which the callback stalls for 300 ms, or 0.
    uint32_t stallAt;
} Blocks;

static void CountBlocks(const maudStreamBlock* block, void* user)
{
    Blocks* blocks = user;
    if (pthread_equal(pthread_self(), s_control))
    {
        atomic_fetch_add(&blocks->onControl, 1);
    }
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
        Sleep(300);
    }
    atomic_fetch_add(&blocks->count, 1);
}

static bool WaitForBlocks(maudContext* context, const Blocks* blocks, uint32_t count)
{
    for (int tries = 0; tries < 500 && atomic_load(&blocks->count) < count; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        Sleep(10);
    }
    return atomic_load(&blocks->count) >= count;
}

// The library's PulseAudio stream threads, by their name in /proc.
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
                fgets(name, sizeof(name), comm) != nullptr && strncmp(name, "maud-pulse", 10) == 0;
            fclose(comm);
        }
    }
    closedir(tasks);
    return count;
}

// Frames the stream moves per second of wall time over one window.
static double MeasureWindow(maudContext* context, maudStreamId stream, int milliseconds)
{
    struct timespec start;
    struct timespec end;
    uint64_t first = 0;
    uint64_t last = 0;
    clock_gettime(CLOCK_MONOTONIC, &start);
    CHECK(maudGetStreamPosition(context, stream, &first) == maud_success, "position");
    for (int i = 0; i < milliseconds / 10; ++i)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        Sleep(10);
    }
    CHECK(maudGetStreamPosition(context, stream, &last) == maud_success, "position");
    clock_gettime(CLOCK_MONOTONIC, &end);
    double seconds =
        (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) * 1e-9;
    return (double)(last - first) / seconds;
}

// The stream's rate: the best of three windows of a second. A
// loaded machine can stall the platform's clock, which only lowers a
// window's count, so the best window is the one that shows the rate.
static double MeasureRate(maudContext* context, maudStreamId stream)
{
    double best = 0.0;
    for (int window = 0; window < 3; ++window)
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

static maudStreamId OpenStream(maudContext* context, maudDirection direction, maudDeviceId device,
                               Blocks* blocks)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = direction;
    def.device = device;
    def.periodFrames = 256;
    def.callback = CountBlocks;
    def.user = blocks;
    blocks->periodFrames = 256;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "create");
    return stream;
}

static bool Run(const char* command)
{
    return system(command) == 0;
}

// Whether the one sink input is corked: 1 yes, 0 no, -1 none.
static int Corked(void)
{
    FILE* listing = popen("pactl list sink-inputs | grep 'Corked:'", "r");
    char line[64] = {0};
    bool read = listing != nullptr && fgets(line, sizeof(line), listing) != nullptr;
    if (listing != nullptr)
    {
        pclose(listing);
    }
    return !read ? -1 : strstr(line, "yes") != nullptr ? 1 : 0;
}

// Drains notifications until the device with key appears, up to five
// seconds.
static maudDeviceId WaitForKey(maudContext* context, maudDirection direction, const char* key)
{
    maudDeviceId device = {0, 0};
    for (int tries = 0; tries < 500 && device.index1 == 0; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        device = FindByKey(context, direction, key);
        Sleep(10);
    }
    return device;
}

static void TestDevices(const maudContext* context)
{
    CHECK(maudGetContextBackend(context) == maud_backendPulse, "PulseAudio");
    maudDeviceId sink = FindByKey(context, maud_directionOutput, "maud-test-sink");
    CHECK(sink.index1 != 0, "the test sink is a device");
    char name[64];
    size_t length = 0;
    CHECK(maudGetDeviceName(context, sink, name, sizeof(name), &length) == maud_success, "name");
    CHECK(length == 14 && memcmp(name, "Maud test sink", 14) == 0, "its description");
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, sink, &info) == maud_success, "info");
    CHECK(info.nativeSampleRate == 48000 && info.nativeLayout == maud_layoutStereo, "48k stereo");
    maudDeviceId source = FindByKey(context, maud_directionInput, "maud-test-source");
    CHECK(source.index1 != 0, "the test source is a device");
    CHECK(!HasMonitor(context), "no monitor source is a device");
    maudDeviceId current = {0, 0};
    for (uint32_t role = 0; role < 2; ++role)
    {
        CHECK(maudGetDefaultDevice(context, maud_directionOutput, (maudDeviceRole)role, &current) ==
                      maud_success &&
                  SameDevice(current, sink),
              "the server's default sink");
        CHECK(maudGetDefaultDevice(context, maud_directionInput, (maudDeviceRole)role, &current) ==
                      maud_success &&
                  SameDevice(current, source),
              "the server's default source");
    }
}

static void TestHotplug(maudContext* context)
{
    CHECK(Run("pactl load-module module-null-sink sink_name=maud-pulse-hotplug "
              "sink_properties='device.description=Hotplug device.form_factor=headphone' "
              "> /dev/null"),
          "load a sink");
    maudNotification record;
    maudDeviceId any = {0, 0};
    CHECK(WaitFor(context, maud_notifyDeviceAdded, any, &record), "it is added");
    maudDeviceId plugged = FindByKey(context, maud_directionOutput, "maud-pulse-hotplug");
    CHECK(SameDevice(record.deviceId, plugged), "by its name");
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, plugged, &info) == maud_success &&
              info.form == maud_formHeadphones,
          "its form factor read as headphones");
    CHECK(Run("pactl set-default-sink maud-pulse-hotplug"), "make it the default");
    CHECK(WaitFor(context, maud_notifyDefaultChanged, plugged, &record), "the default follows");
    CHECK(WaitForDefaults(context, plugged), "for both roles");
    CHECK(Run("pactl set-default-sink maud-test-sink"), "restore the default");
    CHECK(WaitForDefaults(context, FindByKey(context, maud_directionOutput, "maud-test-sink")),
          "and back");
    CHECK(Run("pactl unload-module module-null-sink"), "unload it");
    CHECK(WaitFor(context, maud_notifyDeviceRemoved, plugged, &record), "it is removed");
    CHECK(FindByKey(context, maud_directionOutput, "maud-pulse-hotplug").index1 == 0, "gone");
}

// A callback that stalls past the server's buffer leaves the sink dry:
// an underrun, counted.
static void TestUnderrun(maudContext* context)
{
    Blocks blocks = {.stallAt = 30};
    maudDeviceId none = {0, 0};
    maudStreamId stream = OpenStream(context, maud_directionOutput, none, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 80), "past the stall");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success && status.underruns >= 1,
          "the stall counted as an underrun");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

static void TestOutputStream(maudContext* context)
{
    Blocks blocks = {0};
    maudDeviceId none = {0, 0};
    maudStreamId stream = OpenStream(context, maud_directionOutput, none, &blocks);
    CHECK(ThreadCount() == 0, "no stream thread before start");
    CHECK(Corked() == 1, "connected corked");
    Sleep(100);
    CHECK(atomic_load(&blocks.count) == 0, "nothing before start");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(ThreadCount() == 1, "one thread while it runs");
    Sleep(200);
    CHECK(Corked() == 0, "uncorked while it runs");
    CHECK(Run("pactl list sink-inputs | grep -q 'node.latency = \"256/48000\"'"),
          "a period of latency asked for");
    CHECK(WaitForBlocks(context, &blocks, 20), "blocks arrive");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(Near(MeasureRate(context, stream), 48000.0), "at the sink's rate");
    CHECK(StreamClockIsSound(context, stream, true, true, Sleep),
          "its clock maps frames to host time");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    CHECK(ThreadCount() == 0, "joined when it stops");
    Sleep(200);
    CHECK(Corked() == 1, "corked once stopped");
    uint32_t stopped = atomic_load(&blocks.count);
    Sleep(100);
    CHECK(atomic_load(&blocks.count) == stopped, "no callbacks once stopped");
    CHECK(maudStartStream(context, stream) == maud_success, "start again");
    CHECK(WaitForBlocks(context, &blocks, stopped + 20), "it runs again");
    CHECK(atomic_load(&blocks.onControl) == 0, "no callback on the control thread");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy while running");
    CHECK(ThreadCount() == 0, "and joined");
    maudStreamDef def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.user = &blocks;
    def.ratePolicy = maud_rateRequired;
    def.sampleRate = 44100;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "a required rate the sink does not run at");
    def.sampleRate = 48000;
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "and one it does");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.mode = maud_modePull;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no pull mode");
}

static void TestInputStream(maudContext* context)
{
    Blocks blocks = {0};
    maudDeviceId none = {0, 0};
    maudStreamId stream = OpenStream(context, maud_directionInput, none, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success, "start capture");
    CHECK(WaitForBlocks(context, &blocks, 20), "captured blocks arrive");
    CHECK(atomic_load(&blocks.withInput) == atomic_load(&blocks.count), "each with input");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(StreamClockIsSound(context, stream, false, true, Sleep),
          "its clock maps frames to host time");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// A duplex stream whose input device runs at 44.1 kHz, its output at
// 48 kHz: the input half cannot be required at the output's rate, so
// the server converts it, and both arrive in each callback.
static void TestDuplexConverted(maudContext* context)
{
    CHECK(Run("pactl load-module module-null-sink media.class=Audio/Source/Virtual "
              "sink_name=maud-pulse-slow rate=44100 channels=2 > /dev/null"),
          "load a 44.1 kHz source");
    maudDeviceId slow = WaitForKey(context, maud_directionInput, "maud-pulse-slow");
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionDuplex;
    def.inputDevice = slow;
    def.periodFrames = 256;
    def.callback = CountBlocks;
    def.user = &blocks;
    blocks.periodFrames = 256;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "a duplex across rates");
    maudStreamFormat format = {0};
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success &&
              format.sampleRate == 48000,
          "at the output's rate");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 40), "duplex blocks arrive");
    CHECK(atomic_load(&blocks.withInput) == atomic_load(&blocks.count), "each with input");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    CHECK(Run("pactl unload-module module-null-sink"), "unload it");
}

// A stream opened on a device stays on it and is lost with it.
static void TestPinnedStream(maudContext* context)
{
    CHECK(Run("pactl load-module module-null-sink sink_name=maud-pulse-pinned rate=44100 "
              "sink_properties=device.description=Pinned > /dev/null"),
          "load a 44.1 kHz sink");
    maudDeviceId pinned = WaitForKey(context, maud_directionOutput, "maud-pulse-pinned");
    Blocks blocks = {0};
    maudStreamId stream = OpenStream(context, maud_directionOutput, pinned, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 20), "it plays");
    char command[160];
    snprintf(command, sizeof(command),
             "pactl list sink-inputs | grep -q \"Sink: $(pactl list short sinks | "
             "awk '$2==\"maud-pulse-pinned\"{print $1}')\"");
    CHECK(Run(command), "on the device it was opened on");
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success, "format");
    CHECK(format.sampleRate == 44100, "a native stream at its sink's rate");
    CHECK(!Run("pactl move-sink-input $(pactl list short sink-inputs | awk '{print $1}') "
               "maud-test-sink 2> /dev/null"),
          "the server may not move it");
    CHECK(Run("pactl unload-module module-null-sink"), "unplug it");
    maudNotification record;
    bool lost = false;
    for (int tries = 0; tries < 500 && !lost; ++tries)
    {
        while (!lost && maudNextNotification(context, &record) == maud_success)
        {
            lost = record.kind == maud_notifyStreamSuspended &&
                   record.reason == maud_suspendDeviceLost;
        }
        Sleep(10);
    }
    CHECK(lost, "the stream is lost with it");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

static void TestRestart(maudContext* context)
{
    const char* stop = getenv("MAUD_TEST_PIPEWIRE_STOP");
    const char* start = getenv("MAUD_TEST_PIPEWIRE_START");
    if (stop == nullptr || start == nullptr)
    {
        return;
    }
    maudDeviceId sink = FindByKey(context, maud_directionOutput, "maud-test-sink");
    Blocks blocks = {0};
    maudDeviceId none = {0, 0};
    maudStreamId stream = OpenStream(context, maud_directionOutput, none, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 20), "it plays");
    CHECK(Run(stop), "the server stops");
    maudNotification record;
    CHECK(WaitFor(context, maud_notifyDeviceRemoved, sink, &record), "its devices go");
    CHECK(Run(start), "the server starts again");
    maudDeviceId current = {0, 0};
    for (int tries = 0; tries < 500 && current.index1 == 0; ++tries)
    {
        while (maudNextNotification(context, &record) == maud_success)
        {
        }
        maudResult result =
            maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &current);
        CHECK(result == maud_success || result == maud_empty, "default or none yet");
        Sleep(10);
    }
    sink = FindByKey(context, maud_directionOutput, "maud-test-sink");
    CHECK(sink.index1 != 0 && SameDevice(current, sink), "the sink and its default come back");
    uint32_t resumed = atomic_load(&blocks.count);
    CHECK(WaitForBlocks(context, &blocks, resumed + 20), "the stream plays on the new server");
    CHECK(atomic_load(&blocks.onControl) == 0, "rebuilt without a callback on this thread");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// With no PipeWire daemon to answer, native falls back to PulseAudio.
static void TestNativeFallback(void)
{
    setenv("PIPEWIRE_REMOTE", "maud-no-such-daemon", 1);
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "native without PipeWire");
    CHECK(maudGetContextBackend(context) == maud_backendPulse, "is PulseAudio");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    unsetenv("PIPEWIRE_REMOTE");
}

// PulseAudio has no exclusive mode: a stream asking for it on
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
}

int main(void)
{
    s_control = pthread_self();
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendPulse;
    maudContext* context = nullptr;
    if (maudCreateContext(&def, &context) != maud_success)
    {
        return getenv("MAUD_REQUIRE_PULSE") != nullptr ? 1 : SKIP;
    }
    TestDevices(context);
    TestHotplug(context);
    TestOutputStream(context);
    TestUnderrun(context);
    TestExclusiveRefused(context);
    TestInputStream(context);
    TestDuplexConverted(context);
    TestPinnedStream(context);
    TestRestart(context);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    TestNativeFallback();
    return s_failures == 0 ? 0 : 1;
}
