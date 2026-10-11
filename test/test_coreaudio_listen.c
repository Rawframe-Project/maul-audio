// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context's listeners on the system object, against a fake HAL: the
// device list, both defaults and the audio server's restarts are heard;
// a change raises the changed flag and a restart the restarted flag
// too; after a restart every listener is removed and the system
// object's added again, once however many restarts were reported, and
// nothing is done without one; stopping removes them all.

#include "coreaudio_form.h"
#include "coreaudio_listen.h"
#include "test_harness.h"

#include <string.h>

typedef struct Listened
{
    AudioObjectID object;
    AudioObjectPropertySelector selector;
} Listened;

static Listened s_listened[16];
static uint32_t s_listenedCount;
static uint32_t s_adds;
static uint32_t s_removes;

static OSStatus FakeGetData(AudioObjectID object, const AudioObjectPropertyAddress* address,
                            UInt32 qualifierSize, const void* qualifier, UInt32* size, void* data)
{
    (void)object;
    (void)address;
    (void)qualifierSize;
    (void)qualifier;
    (void)size;
    (void)data;
    return kAudioHardwareUnknownPropertyError;
}

static Boolean FakeHas(AudioObjectID object, const AudioObjectPropertyAddress* address)
{
    (void)object;
    return address->mSelector == kAudioDevicePropertyDataSource;
}

static OSStatus FakeAdd(AudioObjectID object, const AudioObjectPropertyAddress* address,
                        dispatch_queue_t queue, AudioObjectPropertyListenerBlock listener)
{
    (void)queue;
    s_adds++;
    if (listener == nullptr || s_listenedCount == 16)
    {
        return kAudioHardwareUnspecifiedError;
    }
    s_listened[s_listenedCount++] = (Listened){object, address->mSelector};
    return noErr;
}

static OSStatus FakeRemove(AudioObjectID object, const AudioObjectPropertyAddress* address,
                           dispatch_queue_t queue, AudioObjectPropertyListenerBlock listener)
{
    (void)queue;
    (void)listener;
    s_removes++;
    for (uint32_t i = 0; i < s_listenedCount; ++i)
    {
        if (s_listened[i].object == object && s_listened[i].selector == address->mSelector)
        {
            s_listened[i] = s_listened[--s_listenedCount];
            return noErr;
        }
    }
    return kAudioHardwareUnspecifiedError;
}

static const maudCoreAudioHal s_hal = {FakeGetData, FakeHas, FakeAdd, FakeRemove};

static bool Heard(AudioObjectID object, AudioObjectPropertySelector selector)
{
    for (uint32_t i = 0; i < s_listenedCount; ++i)
    {
        if (s_listened[i].object == object && s_listened[i].selector == selector)
        {
            return true;
        }
    }
    return false;
}

static bool HearsSystem(void)
{
    return Heard(kAudioObjectSystemObject, kAudioHardwarePropertyDevices) &&
           Heard(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice) &&
           Heard(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultInputDevice) &&
           Heard(kAudioObjectSystemObject, kAudioHardwarePropertyServiceRestarted);
}

// Calls the change block as the HAL does, with one property.
static void Report(maudCoreAudio* coreaudio, AudioObjectPropertySelector selector)
{
    AudioObjectPropertyAddress address = {selector, kAudioObjectPropertyScopeGlobal,
                                          MAUD_COREAUDIO_ELEMENT_MAIN};
    coreaudio->listener(1, &address);
}

static void TestListen(void)
{
    maudCoreAudioEndpoint endpoints[1] = {{.object = 7}};
    maudDeviceSpec specs[1] = {{.info.direction = maud_directionOutput}};
    maudCoreAudioWatch watched[1];
    maudCoreAudio coreaudio = {
        .hal = &s_hal,
        .queue = dispatch_queue_create("test-listen", DISPATCH_QUEUE_SERIAL),
        .endpoints = endpoints,
        .specs = specs,
        .watched = watched,
        .watchCapacity = 1,
    };
    atomic_init(&coreaudio.changed, false);
    atomic_init(&coreaudio.restarted, false);
    maudCoreAudioListen(&coreaudio);
    maudCoreAudioWatchSources(&coreaudio, 1);
    CHECK(coreaudio.listening && HearsSystem() && s_listenedCount == 5 &&
              Heard(7, kAudioDevicePropertyDataSource),
          "the device list, the defaults, the restarts and a data source heard");

    // A change raises the changed flag alone, and a relisten does
    // nothing.
    Report(&coreaudio, kAudioHardwarePropertyDevices);
    CHECK(atomic_load(&coreaudio.changed) && !atomic_load(&coreaudio.restarted),
          "a change raises the changed flag");
    uint32_t adds = s_adds;
    uint32_t removes = s_removes;
    CHECK(!maudCoreAudioRelisten(&coreaudio) && s_adds == adds && s_removes == removes,
          "nothing done without a restart");

    // A restart, reported twice: one relisten, every listener removed
    // and the system object's added again; the data source waits for
    // the next scan.
    atomic_store(&coreaudio.changed, false);
    Report(&coreaudio, kAudioHardwarePropertyServiceRestarted);
    Report(&coreaudio, kAudioHardwarePropertyServiceRestarted);
    CHECK(atomic_load(&coreaudio.changed) && atomic_load(&coreaudio.restarted),
          "a restart raises both flags");
    CHECK(maudCoreAudioRelisten(&coreaudio), "a restart relistens");
    CHECK(s_removes == removes + 5 && s_adds == adds + 4 && s_listenedCount == 4 && HearsSystem() &&
              coreaudio.watchedCount == 0 && coreaudio.listening,
          "every listener removed, the system object's added again");
    CHECK(!maudCoreAudioRelisten(&coreaudio), "once for the restarts reported");
    maudCoreAudioWatchSources(&coreaudio, 1);
    CHECK(Heard(7, kAudioDevicePropertyDataSource), "the next scan hears the data source");

    // Stopping removes them all.
    maudCoreAudioStopListening(&coreaudio);
    CHECK(s_listenedCount == 0 && coreaudio.listener == nullptr, "all removed");
    dispatch_release(coreaudio.queue);
}

int main(void)
{
    TestListen();
    return s_failures == 0 ? 0 : 1;
}
