// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Core Audio's device forms and data source listeners, against a fake
// HAL with the devices CI's runners lack: a form comes from the data
// source of the direction's scope, else from an HDMI or DisplayPort
// transport; a listener is added once per scanned endpoint with a data
// source, up to the capacity, and removed when the endpoint leaves the
// scan or the context stops listening.

#include "coreaudio_form.h"
#include "test_harness.h"

#include <string.h>

#define FOUR(a, b, c, d)                                                                           \
    (((UInt32)(a) << 24) | ((UInt32)(b) << 16) | ((UInt32)(c) << 8) | (UInt32)(d))

// A fake device: its data source in each scope (0 for none) and its
// transport (0 for none).
typedef struct FakeDevice
{
    AudioObjectID object;
    UInt32 outputSource;
    UInt32 inputSource;
    UInt32 transport;
    // Whether reading the data source fails though the device has one.
    bool sourceFails;
    // Whether adding a listener to it fails.
    bool addFails;
} FakeDevice;

typedef struct Listened
{
    AudioObjectID object;
    AudioObjectPropertyScope scope;
} Listened;

static FakeDevice s_devices[4];
static uint32_t s_deviceCount;
static Listened s_listened[8];
static uint32_t s_listenedCount;
static uint32_t s_adds;
static uint32_t s_removes;
static uint32_t s_strayRemoves;

static const FakeDevice* Find(AudioObjectID object)
{
    for (uint32_t i = 0; i < s_deviceCount; ++i)
    {
        if (s_devices[i].object == object)
        {
            return &s_devices[i];
        }
    }
    return nullptr;
}

static UInt32 SourceIn(const FakeDevice* device, AudioObjectPropertyScope scope)
{
    return scope == kAudioObjectPropertyScopeOutput  ? device->outputSource
           : scope == kAudioObjectPropertyScopeInput ? device->inputSource
                                                     : 0;
}

static OSStatus FakeGetData(AudioObjectID object, const AudioObjectPropertyAddress* address,
                            UInt32 qualifierSize, const void* qualifier, UInt32* size, void* data)
{
    (void)qualifierSize;
    (void)qualifier;
    const FakeDevice* device = Find(object);
    UInt32 value = 0;
    if (device != nullptr && address->mSelector == kAudioDevicePropertyDataSource &&
        !device->sourceFails)
    {
        value = SourceIn(device, address->mScope);
    }
    else if (device != nullptr && address->mSelector == kAudioDevicePropertyTransportType &&
             address->mScope == kAudioObjectPropertyScopeGlobal)
    {
        value = device->transport;
    }
    if (value == 0 || *size != sizeof(value))
    {
        return kAudioHardwareUnknownPropertyError;
    }
    memcpy(data, &value, sizeof(value));
    return noErr;
}

static Boolean FakeHas(AudioObjectID object, const AudioObjectPropertyAddress* address)
{
    const FakeDevice* device = Find(object);
    return device != nullptr && address->mSelector == kAudioDevicePropertyDataSource &&
           SourceIn(device, address->mScope) != 0;
}

static OSStatus FakeAdd(AudioObjectID object, const AudioObjectPropertyAddress* address,
                        dispatch_queue_t queue, AudioObjectPropertyListenerBlock listener)
{
    (void)queue;
    const FakeDevice* device = Find(object);
    s_adds++;
    if (device == nullptr || device->addFails || listener == nullptr || s_listenedCount == 8)
    {
        return kAudioHardwareUnspecifiedError;
    }
    s_listened[s_listenedCount++] = (Listened){object, address->mScope};
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
        if (s_listened[i].object == object && s_listened[i].scope == address->mScope)
        {
            s_listened[i] = s_listened[--s_listenedCount];
            return noErr;
        }
    }
    s_strayRemoves++;
    return kAudioHardwareUnspecifiedError;
}

static const maudCoreAudioHal s_hal = {FakeGetData, FakeHas, FakeAdd, FakeRemove};

static bool Listening(AudioObjectID object, AudioObjectPropertyScope scope)
{
    for (uint32_t i = 0; i < s_listenedCount; ++i)
    {
        if (s_listened[i].object == object && s_listened[i].scope == scope)
        {
            return true;
        }
    }
    return false;
}

static void SetDevices(const FakeDevice* devices, uint32_t count)
{
    memcpy(s_devices, devices, count * sizeof(FakeDevice));
    s_deviceCount = count;
}

static maudDeviceForm FormOf(UInt32 source, maudDirection direction)
{
    FakeDevice device = {.object = 10};
    if (direction == maud_directionOutput)
    {
        device.outputSource = source;
    }
    else
    {
        device.inputSource = source;
    }
    SetDevices(&device, 1);
    return maudCoreAudioFormOf(&s_hal, 10, direction);
}

static void TestForms(void)
{
    CHECK(FormOf(FOUR('i', 's', 'p', 'k'), maud_directionOutput) == maud_formSpeakers,
          "internal speakers");
    CHECK(FormOf(FOUR('e', 's', 'p', 'k'), maud_directionOutput) == maud_formSpeakers,
          "external speakers");
    CHECK(FormOf(FOUR('h', 'd', 'p', 'n'), maud_directionOutput) == maud_formHeadphones,
          "headphones");
    CHECK(FormOf(FOUR('i', 'm', 'i', 'c'), maud_directionInput) == maud_formMicrophone,
          "internal microphone");
    CHECK(FormOf(FOUR('e', 'm', 'i', 'c'), maud_directionInput) == maud_formMicrophone,
          "external microphone");
    CHECK(FormOf(FOUR('l', 'i', 'n', 'e'), maud_directionInput) == maud_formLine, "line in");
    CHECK(FormOf(FOUR('s', 'p', 'd', 'f'), maud_directionOutput) == maud_formDigital, "S/PDIF");
    CHECK(FormOf(FOUR('x', 'x', 'x', 'x'), maud_directionOutput) == maud_formUnknown,
          "a source of no known form");
    CHECK(FormOf(0, maud_directionOutput) == maud_formUnknown, "no source, no transport");

    // A source is read in the direction's scope only.
    FakeDevice inputOnly = {.object = 11, .inputSource = FOUR('h', 'd', 'p', 'n')};
    SetDevices(&inputOnly, 1);
    CHECK(maudCoreAudioFormOf(&s_hal, 11, maud_directionOutput) == maud_formUnknown,
          "the output scope's source");
    FakeDevice outputOnly = {.object = 12, .outputSource = FOUR('i', 'm', 'i', 'c')};
    SetDevices(&outputOnly, 1);
    CHECK(maudCoreAudioFormOf(&s_hal, 12, maud_directionInput) == maud_formUnknown,
          "the input scope's source");

    // Without a known source, the transport.
    FakeDevice transports[4] = {
        {.object = 20, .transport = kAudioDeviceTransportTypeHDMI},
        {.object = 21, .transport = kAudioDeviceTransportTypeDisplayPort},
        {.object = 22, .transport = kAudioDeviceTransportTypeUSB},
        {.object = 23,
         .outputSource = FOUR('h', 'd', 'p', 'n'),
         .sourceFails = true,
         .transport = kAudioDeviceTransportTypeHDMI},
    };
    SetDevices(transports, 4);
    CHECK(maudCoreAudioFormOf(&s_hal, 20, maud_directionOutput) == maud_formDigital, "HDMI");
    CHECK(maudCoreAudioFormOf(&s_hal, 21, maud_directionOutput) == maud_formDigital, "DisplayPort");
    CHECK(maudCoreAudioFormOf(&s_hal, 22, maud_directionOutput) == maud_formUnknown, "USB");
    CHECK(maudCoreAudioFormOf(&s_hal, 23, maud_directionOutput) == maud_formDigital,
          "an unreadable source falls back to the transport");
    // A known source wins over the transport.
    FakeDevice both = {.object = 24,
                       .outputSource = FOUR('h', 'd', 'p', 'n'),
                       .transport = kAudioDeviceTransportTypeUSB};
    SetDevices(&both, 1);
    CHECK(maudCoreAudioFormOf(&s_hal, 24, maud_directionOutput) == maud_formHeadphones,
          "the source before the transport");
}

// A context's watch state over arrays one slot longer than the scan,
// whose last slot holds a stale copy a loop past its count would read.
typedef struct Context
{
    maudCoreAudio coreaudio;
    maudCoreAudioEndpoint endpoints[5];
    maudDeviceSpec specs[5];
    maudCoreAudioWatch watched[5];
} Context;

static void Scan(Context* context, const AudioObjectID* objects, const maudDirection* directions,
                 uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        context->endpoints[i].object = objects[i];
        context->specs[i].info.direction = directions[i];
    }
    maudCoreAudioWatchSources(&context->coreaudio, count);
}

static void Begin(Context* context, uint32_t capacity, AudioObjectPropertyListenerBlock listener)
{
    memset(context, 0, sizeof(*context));
    context->coreaudio = (maudCoreAudio){
        .hal = &s_hal,
        .listener = listener,
        .endpoints = context->endpoints,
        .specs = context->specs,
        .watched = context->watched,
        .watchCapacity = capacity,
    };
    s_listenedCount = 0;
    s_adds = 0;
    s_removes = 0;
    s_strayRemoves = 0;
}

static void TestWatch(void)
{
    AudioObjectPropertyListenerBlock listener =
        ^(UInt32 count, const AudioObjectPropertyAddress* addresses) {
          (void)count;
          (void)addresses;
        };
    const FakeDevice devices[4] = {
        {.object = 1, .outputSource = FOUR('i', 's', 'p', 'k')},
        {.object = 2,
         .outputSource = FOUR('h', 'd', 'p', 'n'),
         .inputSource = FOUR('l', 'i', 'n', 'e')},
        {.object = 3},
        {.object = 4, .outputSource = FOUR('e', 's', 'p', 'k'), .addFails = true},
    };
    SetDevices(devices, 4);
    const maudDirection out = maud_directionOutput;
    const maudDirection in = maud_directionInput;
    Context context;

    // Each scanned endpoint with a source, in its own scope.
    Begin(&context, 4, listener);
    const AudioObjectID first[4] = {1, 2, 2, 3};
    const maudDirection firstDirections[4] = {out, out, in, out};
    // A stale endpoint past the scan, which has a source.
    context.endpoints[4].object = 4;
    context.specs[4].info.direction = out;
    Scan(&context, first, firstDirections, 4);
    CHECK(s_listenedCount == 3 && context.coreaudio.watchedCount == 3, "three sources heard");
    CHECK(Listening(1, kAudioObjectPropertyScopeOutput) &&
              Listening(2, kAudioObjectPropertyScopeOutput) &&
              Listening(2, kAudioObjectPropertyScopeInput),
          "each in its scope");
    CHECK(s_adds == 3, "nothing asked of a device without a source or past the scan");

    // The same scan again adds nothing.
    Scan(&context, first, firstDirections, 4);
    CHECK(s_adds == 3 && s_listenedCount == 3 && context.coreaudio.watchedCount == 3,
          "no listener twice");

    // A scan without the first device stops hearing it, and only it.
    const AudioObjectID second[2] = {2, 2};
    const maudDirection secondDirections[2] = {out, in};
    context.endpoints[2].object = 1;
    context.specs[2].info.direction = out;
    Scan(&context, second, secondDirections, 2);
    CHECK(s_removes == 1 && s_strayRemoves == 0, "one listener removed");
    CHECK(!Listening(1, kAudioObjectPropertyScopeOutput) && s_listenedCount == 2 &&
              context.coreaudio.watchedCount == 2,
          "the device that left");

    // A scan of the input alone keeps the input and drops the output.
    const AudioObjectID third[1] = {2};
    const maudDirection thirdDirections[1] = {in};
    context.endpoints[1].object = 2;
    context.specs[1].info.direction = out;
    Scan(&context, third, thirdDirections, 1);
    CHECK(Listening(2, kAudioObjectPropertyScopeInput) &&
              !Listening(2, kAudioObjectPropertyScopeOutput) && s_listenedCount == 1 &&
              context.coreaudio.watchedCount == 1 && s_strayRemoves == 0,
          "by direction, not by device alone");

    // A watched entry past the count, stale from a removal, does not
    // count as watched.
    const AudioObjectID fourth[2] = {2, 1};
    const maudDirection fourthDirections[2] = {in, out};
    context.watched[1] = (maudCoreAudioWatch){1, out};
    Scan(&context, fourth, fourthDirections, 2);
    CHECK(Listening(1, kAudioObjectPropertyScopeOutput) && context.coreaudio.watchedCount == 2,
          "a stale entry is not a listener");

    // A failed add is not recorded.
    const AudioObjectID fifth[3] = {2, 1, 4};
    const maudDirection fifthDirections[3] = {in, out, out};
    Scan(&context, fifth, fifthDirections, 3);
    CHECK(context.coreaudio.watchedCount == 2 && !Listening(4, kAudioObjectPropertyScopeOutput),
          "a failed add");

    // Stopping removes every listener added.
    context.watched[2] = (maudCoreAudioWatch){4, out};
    maudCoreAudioUnwatchSources(&context.coreaudio);
    CHECK(s_listenedCount == 0 && context.coreaudio.watchedCount == 0 && s_strayRemoves == 0,
          "all removed");

    // Up to the capacity.
    Begin(&context, 2, listener);
    Scan(&context, first, firstDirections, 4);
    CHECK(s_listenedCount == 2 && context.coreaudio.watchedCount == 2, "the capacity holds");

    // A context that does not listen adds nothing.
    Begin(&context, 4, nullptr);
    Scan(&context, first, firstDirections, 4);
    CHECK(s_adds == 0 && context.coreaudio.watchedCount == 0, "not listening");
}

int main(void)
{
    TestForms();
    TestWatch();
    return s_failures == 0 ? 0 : 1;
}
