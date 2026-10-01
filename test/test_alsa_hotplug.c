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
#include "device.h"
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

// A PCM's name says digital for HDMI and S/PDIF, nothing for others.
static void TestPcmForms(void)
{
    CHECK(maudAlsaFormOfPcm("HDMI 0") == maud_formDigital &&
              maudAlsaFormOfPcm("ALC1220 IEC958") == maud_formDigital &&
              maudAlsaFormOfPcm("USB S/PDIF out") == maud_formDigital &&
              maudAlsaFormOfPcm("DisplayPort 1") == maud_formDigital,
          "HDMI, IEC958, S/PDIF and DisplayPort are digital");
    CHECK(maudAlsaFormOfPcm("ALC1220 Analog") == maud_formUnknown &&
              maudAlsaFormOfPcm(nullptr) == maud_formUnknown,
          "an analog PCM, or none, is unknown");
}

static void TestSync(maudContext* context)
{
    uint32_t outputs = CountDevices(context, maud_directionOutput);
    uint32_t inputs = CountDevices(context, maud_directionInput);
    maudDeviceSpec fake[2] = {
        {.info = {.direction = maud_directionOutput},
         .name = "Fake card, Fake PCM",
         .nameLength = 19,
         .key = "hw:CARD=Fake,DEV=0",
         .keyLength = 18},
        {.info = {.direction = maud_directionInput},
         .name = "Fake card, Fake PCM",
         .nameLength = 19,
         .key = "hw:CARD=Fake,DEV=0",
         .keyLength = 18},
    };
    CHECK(maudSyncDevices(context, fake, 2, "default") == maud_success, "sync to a made-up card");
    CHECK(Drain(context, maud_notifyDeviceRemoved) == outputs + inputs - 2,
          "the real endpoints go");
    CHECK(CountDevices(context, maud_directionOutput) == 2 &&
              CountDevices(context, maud_directionInput) == 2,
          "the default and the made-up endpoint remain");
    CHECK(HasKey(context, maud_directionOutput, "hw:CARD=Fake,DEV=0") &&
              HasKey(context, maud_directionInput, "hw:CARD=Fake,DEV=0"),
          "in both directions");
    CHECK(maudSyncDevices(context, fake, 2, "default") == maud_success, "the same list again");
    CHECK(Drain(context, maud_notifyDeviceAdded) == 0, "changes nothing");
    fake[0].info.nativeSampleRate = fake[0].info.minSampleRate = fake[0].info.maxSampleRate = 44100;
    fake[0].info.nativeLayout = maud_layoutStereo;
    CHECK(maudSyncDevices(context, fake, 2, "default") == maud_success, "a format changes");
    maudDeviceId ids[4];
    uint32_t outputs2 = 0;
    CHECK(maudGetDevices(context, maud_directionOutput, ids, 4, &outputs2) == maud_success, "list");
    maudDeviceInfo info = {0};
    CHECK(outputs2 == 2 && maudGetDeviceInfo(context, ids[1], &info) == maud_success &&
              info.nativeSampleRate == 44100 && info.maxSampleRate == 44100 &&
              info.nativeLayout == maud_layoutStereo,
          "the device takes it in place");
    CHECK(Drain(context, maud_notifyDeviceAdded) == 0, "without being added again");
    fake[0].info.form = maud_formDigital;
    CHECK(maudSyncDevices(context, fake, 2, "default") == maud_success, "its form changes");
    maudNotification record = {0};
    CHECK(maudNextNotification(context, &record) == maud_success &&
              record.kind == maud_notifyRouteChanged && record.form == maud_formDigital &&
              record.deviceId.index1 == ids[1].index1,
          "a route change, naming the device and its form");
    CHECK(maudGetDeviceInfo(context, ids[1], &info) == maud_success &&
              info.form == maud_formDigital,
          "which its info reports");
    CHECK(maudSyncDevices(context, fake, 2, "default") == maud_success &&
              Drain(context, maud_notifyRouteChanged) == 0,
          "and the same form again is none");
    CHECK(maudSyncDevices(context, fake, 1, "default") == maud_success, "its input goes");
    CHECK(!HasKey(context, maud_directionInput, "hw:CARD=Fake,DEV=0") &&
              HasKey(context, maud_directionOutput, "hw:CARD=Fake,DEV=0"),
          "by direction");
    CHECK(maudSyncDevices(context, nullptr, 0, "default") == maud_success, "no cards");
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
    // More events than one read takes: all of them are taken at once.
    for (int i = 0; i < 200; ++i)
    {
        char name[64];
        snprintf(name, sizeof(name), "pcmC9D%dp-with-a-long-name-to-fill-the-buffer", i);
        Touch(directory, name);
        snprintf(path, sizeof(path), "%s/%s", directory, name);
        unlink(path);
    }
    CHECK(maudAlsaTakeChanges(watch), "a burst of events");
    CHECK(!maudAlsaTakeChanges(watch), "taken in full");
    maudAlsaCloseWatch(watch);
}

// The drain rescans when the watch reports a card node: the real
// endpoints the made-up list removed come back.
static void TestDrainRescans(maudContext* context, const char* directory)
{
    maudAlsa* alsa = context->native;
    struct stat snd;
    CHECK(alsa->watch >= 0 || stat("/dev/snd", &snd) != 0, "a machine with /dev/snd is watched");
    uint32_t real = maudAlsaScan(&alsa->api, alsa->endpoints, alsa->specs,
                                 context->def.limits.devices, context->def.limits.deviceTextBytes);
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
    TestPcmForms();
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
