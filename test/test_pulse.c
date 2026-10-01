// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PulseAudio backend against a running server: its sinks and
// sources as devices, the server's defaults, a sink added, made the
// default and removed with pactl, and the server restarted when
// MAUD_TEST_PIPEWIRE_STOP and MAUD_TEST_PIPEWIRE_START name commands.
// Without a server the test is skipped, unless MAUD_REQUIRE_PULSE is
// set.

#include "test_harness.h"

#include "maul-audio/device.h"
#include "maul-audio/notification.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SKIP 77

static void Sleep(int milliseconds)
{
    struct timespec pause = {0, (long)milliseconds * 1000000L};
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

static bool Run(const char* command)
{
    return system(command) == 0;
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
              "sink_properties=device.description=Hotplug > /dev/null"),
          "load a sink");
    maudNotification record;
    maudDeviceId any = {0, 0};
    CHECK(WaitFor(context, maud_notifyDeviceAdded, any, &record), "it is added");
    maudDeviceId plugged = FindByKey(context, maud_directionOutput, "maud-pulse-hotplug");
    CHECK(SameDevice(record.deviceId, plugged), "by its name");
    CHECK(Run("pactl set-default-sink maud-pulse-hotplug"), "make it the default");
    CHECK(WaitFor(context, maud_notifyDefaultChanged, plugged, &record), "the default follows");
    CHECK(Run("pactl set-default-sink maud-test-sink"), "restore the default");
    CHECK(Run("pactl unload-module module-null-sink"), "unload it");
    CHECK(WaitFor(context, maud_notifyDeviceRemoved, plugged, &record), "it is removed");
    CHECK(FindByKey(context, maud_directionOutput, "maud-pulse-hotplug").index1 == 0, "gone");
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
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendPulse;
    maudContext* context = nullptr;
    if (maudCreateContext(&def, &context) != maud_success)
    {
        return getenv("MAUD_REQUIRE_PULSE") != nullptr ? 1 : SKIP;
    }
    TestDevices(context);
    TestHotplug(context);
    TestRestart(context);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
