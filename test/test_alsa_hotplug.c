// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// ALSA hotplug without plugging a card: the device table follows
// endpoint lists the test makes up, the watch reports card nodes a test
// directory gains, loses or changes, and the drain rescans on them.

#include "alsa_core.h"
#include "alsa_scan.h"
#include "alsa_watch.h"
#include "context.h"
#include "test_harness.h"

#include "maul-audio/device.h"
#include "maul-audio/notification.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SKIP 77

// Drains the queue; counts the records of kind.
static uint32_t Drain(maudContext* context, maudNotificationKind kind)
{
    uint32_t count = 0;
    maudNotification record;
    while (maudNextNotification(context, &record) == maud_success)
    {
        count += record.kind == kind ? 1u : 0u;
    }
    return count;
}

static uint32_t CountDevices(const maudContext* context, maudDirection direction)
{
    uint32_t count = 0;
    CHECK(maudGetDevices(context, direction, nullptr, 0, &count) == maud_success, "count");
    return count;
}

static bool HasKey(const maudContext* context, maudDirection direction, const char* key)
{
    maudDeviceId ids[64];
    uint32_t count = 0;
    CHECK(maudGetDevices(context, direction, ids, 64, &count) == maud_success, "list");
    for (uint32_t i = 0; i < count && i < 64; ++i)
    {
        char bytes[128];
        size_t length = 0;
        if (maudGetDeviceKey(context, ids[i], bytes, sizeof(bytes), &length) == maud_success &&
            length == strlen(key) && memcmp(bytes, key, length) == 0)
        {
            return true;
        }
    }
    return false;
}

static void TestSync(maudContext* context)
{
    uint32_t outputs = CountDevices(context, maud_directionOutput);
    uint32_t inputs = CountDevices(context, maud_directionInput);
    maudAlsaEndpoint fake[2] = {
        {maud_directionOutput, "hw:CARD=Fake,DEV=0", "Fake card, Fake PCM"},
        {maud_directionInput, "hw:CARD=Fake,DEV=0", "Fake card, Fake PCM"},
    };
    CHECK(maudAlsaSyncDevices(context, fake, 2) == maud_success, "sync to a made-up card");
    CHECK(Drain(context, maud_notifyDeviceRemoved) == outputs + inputs - 2,
          "the real endpoints go");
    CHECK(CountDevices(context, maud_directionOutput) == 2 &&
              CountDevices(context, maud_directionInput) == 2,
          "the default and the made-up endpoint remain");
    CHECK(HasKey(context, maud_directionOutput, "hw:CARD=Fake,DEV=0") &&
              HasKey(context, maud_directionInput, "hw:CARD=Fake,DEV=0"),
          "in both directions");
    CHECK(maudAlsaSyncDevices(context, fake, 2) == maud_success, "the same list again");
    CHECK(Drain(context, maud_notifyDeviceAdded) == 0, "changes nothing");
    CHECK(maudAlsaSyncDevices(context, fake, 1) == maud_success, "its input goes");
    CHECK(!HasKey(context, maud_directionInput, "hw:CARD=Fake,DEV=0") &&
              HasKey(context, maud_directionOutput, "hw:CARD=Fake,DEV=0"),
          "by direction");
    CHECK(maudAlsaSyncDevices(context, nullptr, 0) == maud_success, "no cards");
    CHECK(CountDevices(context, maud_directionOutput) == 1 &&
              HasKey(context, maud_directionOutput, "default"),
          "the default stays");
    maudDeviceId current = {0, 0};
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &current) ==
              maud_success,
          "and is still the default");
    Drain(context, maud_notifyDeviceAdded);
}

static void Touch(const char* directory, const char* name)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/%s", directory, name);
    int file = open(path, O_CREAT | O_WRONLY, 0600);
    CHECK(file >= 0, "create a node");
    close(file);
}

static void TestWatch(const char* directory)
{
    CHECK(maudAlsaOpenWatch("/nonexistent/maud") == -1, "no directory, no watch");
    CHECK(!maudAlsaTakeChanges(-1), "and no changes");
    int watch = maudAlsaOpenWatch(directory);
    CHECK(watch >= 0, "watch the directory");
    CHECK(!maudAlsaTakeChanges(watch), "nothing yet");
    Touch(directory, "seq");
    CHECK(!maudAlsaTakeChanges(watch), "a node that is not a card's");
    Touch(directory, "controlC9");
    CHECK(maudAlsaTakeChanges(watch), "a card's control appears");
    CHECK(!maudAlsaTakeChanges(watch), "taken once");
    char path[128];
    snprintf(path, sizeof(path), "%s/controlC9", directory);
    CHECK(chmod(path, 0644) == 0, "chmod");
    CHECK(maudAlsaTakeChanges(watch), "it becomes readable");
    Touch(directory, "pcmC9D0p");
    snprintf(path, sizeof(path), "%s/pcmC9D0p", directory);
    CHECK(maudAlsaTakeChanges(watch) && unlink(path) == 0, "a PCM node");
    CHECK(maudAlsaTakeChanges(watch), "and its removal");
    maudAlsaCloseWatch(watch);
}

// The drain rescans when the watch reports a card node: the real
// endpoints the made-up list removed come back.
static void TestDrainRescans(maudContext* context, const char* directory)
{
    maudAlsa* alsa = context->native;
    uint32_t real = maudAlsaScan(&alsa->api, alsa->endpoints, context->def.limits.devices);
    maudAlsaCloseWatch(alsa->watch);
    alsa->watch = maudAlsaOpenWatch(directory);
    CHECK(Drain(context, maud_notifyDeviceAdded) == 0, "no rescan without an event");
    Touch(directory, "controlC8");
    CHECK(Drain(context, maud_notifyDeviceAdded) == real, "the drain rescans");
    CHECK(CountDevices(context, maud_directionOutput) +
                  CountDevices(context, maud_directionInput) ==
              real + 2,
          "and lists the machine's endpoints again");
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendAlsa;
    maudContext* context = nullptr;
    if (maudCreateContext(&def, &context) != maud_success)
    {
        return getenv("MAUD_REQUIRE_ALSA") != nullptr ? 1 : SKIP;
    }
    char directory[] = "/tmp/maud-snd-XXXXXX";
    CHECK(mkdtemp(directory) != nullptr, "a test directory");
    TestSync(context);
    TestWatch(directory);
    TestDrainRescans(context, directory);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    for (const char* const* name = (const char* const[]){"seq", "controlC9", "controlC8", nullptr};
         *name != nullptr; ++name)
    {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", directory, *name);
        unlink(path);
    }
    CHECK(rmdir(directory) == 0, "the test directory is removed");
    return s_failures == 0 ? 0 : 1;
}
