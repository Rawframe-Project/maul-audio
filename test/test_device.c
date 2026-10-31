// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Devices on the offline backend: enumeration, names and keys, defaults
// per role, streams following a default and moving, device loss,
// suspension and resumption, format changes (a stream's, and a
// device's own), the notification queue's overflow record, and the
// scripting calls refused without a context and inside a callback.

#include "test_harness.h"

#include "maul-audio/notification.h"
#include "maul-audio/offline.h"

#include <string.h>

typedef struct Blocks
{
    uint32_t count;
    uint32_t lastRate;
} Blocks;

static void Count(const maudStreamBlock* block, void* user)
{
    Blocks* blocks = user;
    blocks->count++;
    blocks->lastRate = block->sampleRate;
}

static maudContext* Offline(uint16_t notifications)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    def.limits.devices = 4;
    def.limits.notifications = notifications;
    def.limits.deviceTextBytes = 32;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "offline context");
    return context;
}

static bool Same(maudDeviceId a, maudDeviceId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static bool SameStream(maudStreamId a, maudStreamId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static maudDeviceId AddDevice(maudContext* context, maudDirection direction, uint32_t rate,
                              const char* name)
{
    maudOfflineDeviceDef def = maudDefaultOfflineDeviceDef();
    def.direction = direction;
    def.sampleRate = rate;
    def.name = name;
    def.nameLength = strlen(name);
    def.key = name;
    def.keyLength = strlen(name);
    maudDeviceId id = {0, 0};
    CHECK(maudCreateOfflineDevice(context, &def, &id) == maud_success, "add device");
    return id;
}

static maudStreamId OpenOutput(maudContext* context, maudDeviceId device, maudDeviceRole role,
                               Blocks* blocks)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.device = device;
    def.role = role;
    def.callback = Count;
    def.user = blocks;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "create stream");
    CHECK(maudStream_Start(context, stream) == maud_success, "start stream");
    return stream;
}

// Takes the next notification and checks its kind.
static maudNotification Expect(maudContext* context, maudNotificationKind kind, const char* what)
{
    maudNotification record = {0};
    CHECK(maudContext_NextNotification(context, &record) == maud_success, what);
    CHECK(record.kind == kind, what);
    return record;
}

static void ExpectDrained(maudContext* context, const char* what)
{
    maudNotification record;
    CHECK(maudContext_NextNotification(context, &record) == maud_empty, what);
}

static void TestStartingDevices(void)
{
    maudContext* context = Offline(16);
    ExpectDrained(context, "a new context reports nothing");
    maudDeviceId ids[4];
    uint32_t count = 0;
    CHECK(maudContext_GetDevices(context, maud_directionOutput, ids, 4, &count) == maud_success,
          "list");
    CHECK(count == 1, "one output");
    maudDeviceInfo info;
    CHECK(maudDevice_GetInfo(context, ids[0], &info) == maud_success, "info");
    CHECK(info.direction == maud_directionOutput && info.nativeSampleRate == 48000, "48 kHz out");
    CHECK(info.nativeLayout == maud_layoutStereo, "stereo");
    CHECK(info.defaultGeneral && info.defaultCommunications, "default for both roles");
    CHECK(info.minSampleRate == 8000 && info.maxSampleRate == 384000, "rate range");
    char name[32];
    size_t length = 0;
    CHECK(maudDevice_GetName(context, ids[0], name, sizeof(name), &length) == maud_success, "name");
    CHECK(length == 14 && memcmp(name, "Offline output", 14) == 0, "output name");
    CHECK(maudDevice_GetKey(context, ids[0], name, sizeof(name), &length) == maud_success, "key");
    CHECK(length == 14 && memcmp(name, "offline-output", 14) == 0, "output key");
    CHECK(maudContext_GetDevices(context, maud_directionInput, nullptr, 0, &count) == maud_success,
          "count inputs");
    CHECK(count == 1, "one input");
    maudDeviceId input;
    CHECK(maudContext_GetDefaultDevice(context, maud_directionInput, maud_roleCommunications,
                                       &input) == maud_success,
          "default input");
    CHECK(maudDevice_GetName(context, input, name, 12, &length) == maud_errorCapacity, "too small");
    CHECK(length == 13, "needed length");
    CHECK(maudDevice_GetName(context, input, name, 13, &length) == maud_success, "exact fit");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestDefaultChangeMovesFollowingStreams(void)
{
    maudContext* context = Offline(32);
    maudDeviceId first;
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &first) ==
              maud_success,
          "first output");
    Blocks following = {0};
    Blocks pinned = {0};
    maudStreamId follower = OpenOutput(context, (maudDeviceId){0, 0}, maud_roleGeneral, &following);
    maudStreamId fixed = OpenOutput(context, first, maud_roleGeneral, &pinned);
    maudDeviceId second = AddDevice(context, maud_directionOutput, 44100, "Second");
    maudNotification record = Expect(context, maud_notifyDeviceAdded, "added");
    CHECK(Same(record.device, second) && record.direction == maud_directionOutput, "added id");
    ExpectDrained(context, "a second device changes no default");
    CHECK(maudOfflineDevice_SetDefault(context, second, maud_roleGeneral) == maud_success, "set");
    record = Expect(context, maud_notifyDefaultChanged, "default changed");
    CHECK(Same(record.device, second) && record.role == maud_roleGeneral, "new default");
    record = Expect(context, maud_notifyStreamMoved, "follower moved");
    CHECK(SameStream(record.stream, follower) && Same(record.device, second), "moved where");
    record = Expect(context, maud_notifyStreamFormatChanged, "native rate changed");
    CHECK(record.sampleRate == 44100, "to the new device's rate");
    ExpectDrained(context, "the pinned stream stays");
    maudStreamStatus status;
    CHECK(maudStream_GetStatus(context, fixed, &status) == maud_success, "pinned status");
    CHECK(Same(status.device, first) && status.suspension == maud_suspendNone, "still on first");
    float frames[2 * 480];
    CHECK(maudStream_Render(context, follower, frames, 480) == maud_success, "render follower");
    CHECK(following.lastRate == 44100, "blocks carry the new rate");
    maudStreamFormat format;
    CHECK(maudStream_GetFormat(context, follower, &format) == maud_success, "format");
    CHECK(format.sampleRate == 44100, "format follows");
    CHECK(maudOfflineDevice_SetDefault(context, second, maud_roleGeneral) == maud_success, "again");
    ExpectDrained(context, "setting the same default reports nothing");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestRolesAreFollowedSeparately(void)
{
    maudContext* context = Offline(32);
    Blocks blocks = {0};
    maudStreamId voice =
        OpenOutput(context, (maudDeviceId){0, 0}, maud_roleCommunications, &blocks);
    maudDeviceId headset = AddDevice(context, maud_directionOutput, 48000, "Headset");
    Expect(context, maud_notifyDeviceAdded, "added");
    CHECK(maudOfflineDevice_SetDefault(context, headset, maud_roleGeneral) == maud_success, "set");
    Expect(context, maud_notifyDefaultChanged, "general default");
    ExpectDrained(context, "the voice stream follows communications");
    CHECK(maudOfflineDevice_SetDefault(context, headset, maud_roleCommunications) == maud_success,
          "set communications");
    Expect(context, maud_notifyDefaultChanged, "communications default");
    maudNotification record = Expect(context, maud_notifyStreamMoved, "voice moved");
    CHECK(SameStream(record.stream, voice), "the voice stream");
    ExpectDrained(context, "same rate, no format change");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestLossSuspendsPinnedStreamsAndMovesFollowers(void)
{
    maudContext* context = Offline(32);
    maudDeviceId speakers = AddDevice(context, maud_directionOutput, 48000, "Speakers");
    maudDeviceId first;
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &first) ==
              maud_success,
          "first");
    Blocks blocks = {0};
    maudStreamId follower = OpenOutput(context, (maudDeviceId){0, 0}, maud_roleGeneral, &blocks);
    maudStreamId pinned = OpenOutput(context, first, maud_roleGeneral, &blocks);
    Expect(context, maud_notifyDeviceAdded, "added");
    CHECK(maudDestroyOfflineDevice(context, first) == maud_success, "unplug the default");
    maudNotification record = Expect(context, maud_notifyDeviceRemoved, "removed");
    CHECK(Same(record.device, first), "removed id");
    record = Expect(context, maud_notifyStreamSuspended, "pinned suspended");
    CHECK(SameStream(record.stream, pinned) && record.reason == maud_suspendDeviceLost, "lost");
    record = Expect(context, maud_notifyDefaultChanged, "general default passes on");
    CHECK(Same(record.device, speakers) && record.role == maud_roleGeneral, "to speakers");
    record = Expect(context, maud_notifyStreamMoved, "follower moved");
    CHECK(SameStream(record.stream, follower), "the follower");
    record = Expect(context, maud_notifyDefaultChanged, "communications default passes on");
    CHECK(record.role == maud_roleCommunications, "communications");
    ExpectDrained(context, "nothing else");
    float frames[2 * 16];
    CHECK(maudStream_Render(context, pinned, frames, 16) == maud_errorState, "lost renders not");
    CHECK(maudStream_Render(context, follower, frames, 16) == maud_success, "follower renders");
    maudStreamStatus status;
    CHECK(maudStream_GetStatus(context, pinned, &status) == maud_success, "status");
    CHECK(status.started && status.suspension == maud_suspendDeviceLost, "started but lost");
    CHECK(status.device.index1 == 0, "on no device");
    maudDeviceInfo info;
    CHECK(maudDevice_GetInfo(context, first, &info) == maud_errorStale, "removed id is stale");
    CHECK(maudDestroyOfflineDevice(context, first) == maud_errorStale, "remove twice");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestFollowersWaitForADevice(void)
{
    maudContext* context = Offline(32);
    maudDeviceId only;
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &only) ==
              maud_success,
          "only output");
    Blocks blocks = {0};
    maudStreamId follower = OpenOutput(context, (maudDeviceId){0, 0}, maud_roleGeneral, &blocks);
    CHECK(maudDestroyOfflineDevice(context, only) == maud_success, "unplug the last output");
    Expect(context, maud_notifyDeviceRemoved, "removed");
    maudNotification record = Expect(context, maud_notifyDefaultChanged, "no general default");
    CHECK(record.device.index1 == 0, "null default");
    record = Expect(context, maud_notifyStreamSuspended, "follower waits");
    CHECK(record.reason == maud_suspendNoDevice, "no device");
    Expect(context, maud_notifyDefaultChanged, "no communications default");
    ExpectDrained(context, "nothing else");
    maudDeviceId none;
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &none) ==
              maud_empty,
          "no default");
    float frames[2 * 16];
    CHECK(maudStream_Render(context, follower, frames, 16) == maud_errorState, "waiting");
    Blocks late = {0};
    maudStreamId opened = OpenOutput(context, (maudDeviceId){0, 0}, maud_roleGeneral, &late);
    maudStreamStatus status;
    CHECK(maudStream_GetStatus(context, opened, &status) == maud_success, "status");
    CHECK(status.suspension == maud_suspendNoDevice, "opened waiting");
    maudDeviceId back = AddDevice(context, maud_directionOutput, 48000, "Back");
    Expect(context, maud_notifyDeviceAdded, "added");
    record = Expect(context, maud_notifyDefaultChanged, "it becomes the default");
    CHECK(Same(record.device, back), "the new device");
    record = Expect(context, maud_notifyStreamMoved, "follower moved");
    CHECK(SameStream(record.stream, follower), "first follower");
    record = Expect(context, maud_notifyStreamResumed, "follower resumed");
    CHECK(SameStream(record.stream, follower), "first follower resumed");
    Expect(context, maud_notifyStreamMoved, "late stream moved");
    Expect(context, maud_notifyStreamResumed, "late stream resumed");
    Expect(context, maud_notifyDefaultChanged, "communications default");
    ExpectDrained(context, "nothing else");
    CHECK(maudStream_Render(context, follower, frames, 16) == maud_success, "renders again");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestOverflowRecordCountsDropped(void)
{
    maudContext* context = Offline(4);
    maudDeviceId devices[2];
    devices[0] = AddDevice(context, maud_directionInput, 48000, "A");
    devices[1] = AddDevice(context, maud_directionInput, 48000, "B");
    CHECK(maudDestroyOfflineDevice(context, devices[0]) == maud_success, "remove A");
    CHECK(maudDestroyOfflineDevice(context, devices[1]) == maud_success, "remove B");
    AddDevice(context, maud_directionInput, 48000, "C");
    Expect(context, maud_notifyDeviceAdded, "first record");
    Expect(context, maud_notifyDeviceAdded, "second record");
    Expect(context, maud_notifyDeviceRemoved, "third record");
    maudNotification record = Expect(context, maud_notifyOverflow, "overflow takes the last");
    CHECK(record.notificationDropped == 2, "the dropped records are counted");
    ExpectDrained(context, "drained");
    AddDevice(context, maud_directionInput, 48000, "D");
    Expect(context, maud_notifyDeviceAdded, "records flow again");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestRefusals(void)
{
    maudContext* context = Offline(16);
    maudOfflineDeviceDef def = maudDefaultOfflineDeviceDef();
    maudDeviceId id = {7, 7};
    def.cookie = 0;
    CHECK(maudCreateOfflineDevice(context, &def, &id) == maud_errorInvalid, "no cookie");
    CHECK(id.index1 == 0, "null id on failure");
    def = maudDefaultOfflineDeviceDef();
    def.sampleRate = 400000;
    CHECK(maudCreateOfflineDevice(context, &def, &id) == maud_errorInvalid, "rate out of range");
    // On a context of its own, the room here being counted.
    maudContext* spare = Offline(16);
    def.sampleRate = 384000;
    def.direction = maud_directionInput;
    maudDeviceInfo highest = {0};
    CHECK(maudCreateOfflineDevice(spare, &def, &id) == maud_success &&
              maudDevice_GetInfo(spare, id, &highest) == maud_success &&
              highest.nativeSampleRate == 384000 && highest.spatializer == maud_spatializerUnknown,
          "the highest rate taken; an input's spatializer unknown");
    CHECK(maudDestroyContext(spare) == maud_success, "destroy the spare");
    def = maudDefaultOfflineDeviceDef();
    def.nameLength = 3;
    CHECK(maudCreateOfflineDevice(context, &def, &id) == maud_errorInvalid, "length without name");
    CHECK(maudContext_GetMisuse(context) == 3, "each counted");
    def = maudDefaultOfflineDeviceDef();
    def.name = "A name longer than thirty-two bytes";
    def.nameLength = strlen(def.name);
    CHECK(maudCreateOfflineDevice(context, &def, &id) == maud_errorCapacity, "name too long");
    AddDevice(context, maud_directionOutput, 48000, "Third");
    AddDevice(context, maud_directionOutput, 48000, "Fourth");
    def = maudDefaultOfflineDeviceDef();
    CHECK(maudCreateOfflineDevice(context, &def, &id) == maud_errorCapacity, "device limit");
    CHECK(maudOfflineDevice_SetDefault(context, id, 2) == maud_errorInvalid, "unknown role");
    CHECK(maudOfflineDevice_SetDefault(context, (maudDeviceId){1, 9}, maud_roleGeneral) ==
              maud_errorStale,
          "stale default");
    maudDeviceId input;
    CHECK(maudContext_GetDefaultDevice(context, maud_directionInput, maud_roleGeneral, &input) ==
              maud_success,
          "input");
    Blocks blocks = {0};
    maudStreamDef stream = maudDefaultStreamDef();
    stream.mode = maud_modePull;
    stream.callback = Count;
    stream.user = &blocks;
    stream.device = input;
    maudStreamId streamId;
    uint64_t misuse = maudContext_GetMisuse(context);
    CHECK(maudCreateStream(context, &stream, &streamId) == maud_errorInvalid, "wrong direction");
    CHECK(maudContext_GetMisuse(context) == misuse + 1, "counted");
    stream.device = (maudDeviceId){1, 9};
    CHECK(maudCreateStream(context, &stream, &streamId) == maud_errorStale, "stale device");
    maudDeviceId ids[1];
    uint32_t count = 0;
    CHECK(maudContext_GetDevices(context, maud_directionOutput, ids, 1, &count) == maud_success,
          "list");
    CHECK(count == 3, "the true total past the capacity");
    CHECK(maudContext_GetDevices(context, 2, ids, 1, &count) == maud_errorInvalid,
          "unknown direction");
    CHECK(maudContext_GetDevices(context, maud_directionOutput, nullptr, 1, &count) ==
              maud_errorInvalid,
          "null array with room");
    CHECK(maudContext_NextNotification(context, nullptr) == maud_errorInvalid, "null record");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

// A device's form is what the platform says its port leads to: added
// with one, it reports it; a change, as headphones in its jack, is one
// route record, and setting the same form again is none. Streams on it
// keep running.
static void TestRouteChanges(void)
{
    maudContext* context = Offline(16);
    maudOfflineDeviceDef def = maudDefaultOfflineDeviceDef();
    CHECK(def.form == maud_formUnknown, "unknown by default");
    def.form = maud_formSpeakers;
    def.key = "laptop";
    def.keyLength = 6;
    maudDeviceId device = {0, 0};
    CHECK(maudCreateOfflineDevice(context, &def, &device) == maud_success, "add");
    maudDeviceInfo info = {0};
    CHECK(maudDevice_GetInfo(context, device, &info) == maud_success &&
              info.form == maud_formSpeakers && info.spatializer == maud_spatializerNone,
          "it reports its form, and no spatializer on an output");
    Blocks blocks = {0};
    maudStreamId stream = OpenOutput(context, device, maud_roleGeneral, &blocks);
    while (maudContext_NextNotification(context, &(maudNotification){0}) == maud_success)
    {
    }
    CHECK(maudOfflineDevice_SetForm(context, device, maud_formHeadphones) == maud_success,
          "plug headphones");
    maudNotification record = Expect(context, maud_notifyRouteChanged, "a route record");
    CHECK(Same(record.device, device) && record.direction == maud_directionOutput &&
              record.form == maud_formHeadphones,
          "naming the device and its new form");
    CHECK(maudDevice_GetInfo(context, device, &info) == maud_success &&
              info.form == maud_formHeadphones,
          "which its info reports");
    CHECK(maudOfflineDevice_SetForm(context, device, maud_formHeadphones) == maud_success,
          "the same again");
    ExpectDrained(context, "is no change");
    maudStreamStatus status = {0};
    CHECK(maudStream_GetStatus(context, stream, &status) == maud_success &&
              Same(status.device, device) && status.suspension == maud_suspendNone,
          "the stream stays on it");
    CHECK(maudOfflineDevice_SetForm(context, device, maud_formDigital + 1) == maud_errorInvalid,
          "an unknown form");
    CHECK(maudOfflineDevice_SetForm(context, device, maud_formDigital) == maud_success,
          "the last form");
    def.form = maud_formDigital + 1;
    CHECK(maudCreateOfflineDevice(context, &def, &device) == maud_errorInvalid,
          "an unknown form in a def");
    CHECK(maudOfflineDevice_SetForm(context, (maudDeviceId){3, 7}, maud_formLine) ==
              maud_errorStale,
          "a stale device");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

// What a platform spatializer does on an output is part of its info: a
// change is one record naming the device, the same state again is none;
// inputs take none, and objects need a spatializer that is on.
static void TestSpatializerChanges(void)
{
    maudContext* context = Offline(16);
    maudDeviceId output = {0, 0};
    maudDeviceId input = {0, 0};
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &output) ==
                  maud_success &&
              maudContext_GetDefaultDevice(context, maud_directionInput, maud_roleGeneral,
                                           &input) == maud_success,
          "the starting devices");
    maudDeviceInfo info = {0};
    CHECK(maudDevice_GetInfo(context, output, &info) == maud_success &&
              info.spatializer == maud_spatializerNone && !info.headTracking &&
              info.spatialObjectCount == 0,
          "an offline output has none");
    CHECK(maudDevice_GetInfo(context, input, &info) == maud_success &&
              info.spatializer == maud_spatializerUnknown,
          "an input says nothing");
    while (maudContext_NextNotification(context, &(maudNotification){0}) == maud_success)
    {
    }
    CHECK(maudOfflineDevice_SetSpatializer(context, output, maud_spatializerOn, true, 128) ==
              maud_success,
          "a spatial format turned on");
    maudNotification record = Expect(context, maud_notifySpatializerChanged, "a record");
    CHECK(Same(record.device, output) && record.direction == maud_directionOutput,
          "naming the device");
    CHECK(maudDevice_GetInfo(context, output, &info) == maud_success &&
              info.spatializer == maud_spatializerOn && info.headTracking &&
              info.spatialObjectCount == 128,
          "which its info reports");
    CHECK(maudOfflineDevice_SetSpatializer(context, output, maud_spatializerOn, true, 128) ==
              maud_success,
          "the same again");
    ExpectDrained(context, "is no change");
    CHECK(maudOfflineDevice_SetSpatializer(context, output, maud_spatializerOn, false, 128) ==
              maud_success,
          "the head tracker gone");
    Expect(context, maud_notifySpatializerChanged, "is a change");
    CHECK(maudOfflineDevice_SetSpatializer(context, output, maud_spatializerOn, false, 20) ==
              maud_success,
          "fewer objects");
    Expect(context, maud_notifySpatializerChanged, "is a change");
    CHECK(maudOfflineDevice_SetSpatializer(context, output, maud_spatializerOff, false, 4) ==
              maud_errorInvalid,
          "objects while off");
    CHECK(maudOfflineDevice_SetSpatializer(context, output, maud_spatializerOn + 1, false, 0) ==
              maud_errorInvalid,
          "an unknown state");
    CHECK(maudOfflineDevice_SetSpatializer(context, input, maud_spatializerOff, false, 0) ==
              maud_errorInvalid,
          "an input");
    CHECK(maudOfflineDevice_SetSpatializer(context, (maudDeviceId){3, 7}, maud_spatializerOff,
                                           false, 0) == maud_errorStale,
          "a stale device");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

// A device's native format is part of its info: a change is one record
// naming the device and its rate, and a stream on it that follows its
// rate takes the new one; the same format again is no change.
static void TestFormatChanges(void)
{
    maudContext* context = Offline(16);
    maudDeviceId output = {0, 0};
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &output) ==
              maud_success,
          "the starting output");
    Blocks blocks = {0};
    maudStreamId stream = OpenOutput(context, output, maud_roleGeneral, &blocks);
    while (maudContext_NextNotification(context, &(maudNotification){0}) == maud_success)
    {
    }
    CHECK(maudOfflineDevice_SetFormat(context, output, maud_layout5Point1, 96000) == maud_success,
          "another format picked");
    maudNotification record = Expect(context, maud_notifyDeviceFormatChanged, "a record");
    CHECK(Same(record.device, output) && record.direction == maud_directionOutput &&
              record.sampleRate == 96000,
          "naming the device and its rate");
    maudDeviceInfo info = {0};
    CHECK(maudDevice_GetInfo(context, output, &info) == maud_success &&
              info.nativeLayout == maud_layout5Point1 && info.nativeSampleRate == 96000,
          "which its info reports");
    record = Expect(context, maud_notifyStreamFormatChanged, "the stream follows");
    CHECK(SameStream(record.stream, stream) && record.sampleRate == 96000, "to the new rate");
    ExpectDrained(context, "nothing more");
    CHECK(maudOfflineDevice_SetFormat(context, output, maud_layout5Point1, 96000) == maud_success,
          "the same again");
    ExpectDrained(context, "is no change");
    CHECK(maudOfflineDevice_SetFormat(context, output, maud_layoutStereo, 96000) == maud_success,
          "the layout alone");
    Expect(context, maud_notifyDeviceFormatChanged, "is a change");
    ExpectDrained(context, "which the stream's rate does not follow");
    CHECK(maudOfflineDevice_SetFormat(context, output, maud_layoutNone, 48000) ==
                  maud_errorInvalid &&
              maudOfflineDevice_SetFormat(context, output, maud_layoutStereo, 7999) ==
                  maud_errorInvalid &&
              maudOfflineDevice_SetFormat(context, output, maud_layoutStereo, 384001) ==
                  maud_errorInvalid,
          "no layout, rates out of range");
    CHECK(maudOfflineDevice_SetFormat(context, output, maud_layoutStereo, 8000) == maud_success &&
              maudOfflineDevice_SetFormat(context, output, maud_layoutStereo, 384000) ==
                  maud_success,
          "the rates' ends");
    CHECK(maudOfflineDevice_SetFormat(context, (maudDeviceId){3, 7}, maud_layoutStereo, 48000) ==
              maud_errorStale,
          "a stale device");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

// What the scripting calls return inside a render callback.
typedef struct Inside
{
    maudContext* context;
    maudDeviceId device;
    maudResult results[6];
    uint32_t blocks;
} Inside;

static void ScriptInside(const maudStreamBlock* block, void* user)
{
    (void)block;
    Inside* inside = user;
    if (inside->blocks++ > 0)
    {
        return;
    }
    maudContext* context = inside->context;
    maudOfflineDeviceDef def = maudDefaultOfflineDeviceDef();
    maudDeviceId added;
    inside->results[0] = maudCreateOfflineDevice(context, &def, &added);
    inside->results[1] = maudDestroyOfflineDevice(context, inside->device);
    inside->results[2] = maudOfflineDevice_SetDefault(context, inside->device, maud_roleGeneral);
    inside->results[3] = maudOfflineDevice_SetForm(context, inside->device, maud_formHeadphones);
    inside->results[4] =
        maudOfflineDevice_SetFormat(context, inside->device, maud_layoutStereo, 44100);
    inside->results[5] =
        maudOfflineDevice_SetSpatializer(context, inside->device, maud_spatializerOff, false, 0);
}

// Every scripting call refuses a NULL context, and a thread rendering
// one of the context's streams, counting that as misuse; the device is
// untouched.
static void TestScriptGuards(void)
{
    maudOfflineDeviceDef def = maudDefaultOfflineDeviceDef();
    maudDeviceId id = {7, 7};
    CHECK(maudCreateOfflineDevice(nullptr, &def, &id) == maud_errorInvalid && id.index1 == 0,
          "add without a context");
    CHECK(maudDestroyOfflineDevice(nullptr, (maudDeviceId){1, 1}) == maud_errorInvalid &&
              maudOfflineDevice_SetDefault(nullptr, (maudDeviceId){1, 1}, maud_roleGeneral) ==
                  maud_errorInvalid &&
              maudOfflineDevice_SetForm(nullptr, (maudDeviceId){1, 1}, maud_formHeadphones) ==
                  maud_errorInvalid &&
              maudOfflineDevice_SetFormat(nullptr, (maudDeviceId){1, 1}, maud_layoutStereo,
                                          44100) == maud_errorInvalid &&
              maudOfflineDevice_SetSpatializer(nullptr, (maudDeviceId){1, 1}, maud_spatializerOff,
                                               false, 0) == maud_errorInvalid,
          "the rest without a context");
    maudContext* context = Offline(16);
    Inside inside = {.context = context};
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral,
                                       &inside.device) == maud_success,
          "the default output");
    maudStreamDef stream = maudDefaultStreamDef();
    stream.mode = maud_modePull;
    stream.callback = ScriptInside;
    stream.user = &inside;
    maudStreamId streamId;
    CHECK(maudCreateStream(context, &stream, &streamId) == maud_success &&
              maudStream_Start(context, streamId) == maud_success,
          "a stream");
    uint64_t misuse = maudContext_GetMisuse(context);
    float frames[2 * 64];
    CHECK(maudStream_Render(context, streamId, frames, 64) == maud_success && inside.blocks == 1,
          "rendered");
    bool refused = true;
    for (int i = 0; i < 6; ++i)
    {
        refused = refused && inside.results[i] == maud_errorBusy;
    }
    CHECK(refused, "every call refused inside the callback");
    CHECK(maudContext_GetMisuse(context) == misuse + 6, "each counted");
    maudDeviceInfo info = {0};
    CHECK(maudDevice_GetInfo(context, inside.device, &info) == maud_success &&
              info.form != maud_formHeadphones && info.nativeSampleRate != 44100,
          "the device untouched");
    ExpectDrained(context, "nothing posted");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

int main(void)
{
    TestStartingDevices();
    TestDefaultChangeMovesFollowingStreams();
    TestRolesAreFollowedSeparately();
    TestLossSuspendsPinnedStreamsAndMovesFollowers();
    TestFollowersWaitForADevice();
    TestOverflowRecordCountsDropped();
    TestRefusals();
    TestRouteChanges();
    TestSpatializerChanges();
    TestFormatChanges();
    TestScriptGuards();
    return s_failures == 0 ? 0 : 1;
}
