// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Devices on the offline backend: enumeration, names and keys, defaults
// per role, streams following a default and moving, device loss,
// suspension and resumption, format changes, and the notification
// queue's overflow record.

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
    CHECK(maudAddOfflineDevice(context, &def, &id) == maud_success, "add device");
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
    CHECK(maudStartStream(context, stream) == maud_success, "start stream");
    return stream;
}

// Takes the next notification and checks its kind.
static maudNotification Expect(maudContext* context, maudNotificationKind kind, const char* what)
{
    maudNotification record = {0};
    CHECK(maudNextNotification(context, &record) == maud_success, what);
    CHECK(record.kind == kind, what);
    return record;
}

static void ExpectDrained(maudContext* context, const char* what)
{
    maudNotification record;
    CHECK(maudNextNotification(context, &record) == maud_empty, what);
}

static void TestStartingDevices(void)
{
    maudContext* context = Offline(16);
    ExpectDrained(context, "a new context reports nothing");
    maudDeviceId ids[4];
    uint32_t count = 0;
    CHECK(maudGetDevices(context, maud_directionOutput, ids, 4, &count) == maud_success, "list");
    CHECK(count == 1, "one output");
    maudDeviceInfo info;
    CHECK(maudGetDeviceInfo(context, ids[0], &info) == maud_success, "info");
    CHECK(info.direction == maud_directionOutput && info.nativeSampleRate == 48000, "48 kHz out");
    CHECK(info.nativeLayout == maud_layoutStereo, "stereo");
    CHECK(info.defaultGeneral && info.defaultCommunications, "default for both roles");
    CHECK(info.minSampleRate == 8000 && info.maxSampleRate == 384000, "rate range");
    char name[32];
    size_t length = 0;
    CHECK(maudGetDeviceName(context, ids[0], name, sizeof(name), &length) == maud_success, "name");
    CHECK(length == 14 && memcmp(name, "Offline output", 14) == 0, "output name");
    CHECK(maudGetDeviceKey(context, ids[0], name, sizeof(name), &length) == maud_success, "key");
    CHECK(length == 14 && memcmp(name, "offline-output", 14) == 0, "output key");
    CHECK(maudGetDevices(context, maud_directionInput, nullptr, 0, &count) == maud_success,
          "count inputs");
    CHECK(count == 1, "one input");
    maudDeviceId input;
    CHECK(maudGetDefaultDevice(context, maud_directionInput, maud_roleCommunications, &input) ==
              maud_success,
          "default input");
    CHECK(maudGetDeviceName(context, input, name, 12, &length) == maud_errorCapacity, "too small");
    CHECK(length == 13, "needed length");
    CHECK(maudGetDeviceName(context, input, name, 13, &length) == maud_success, "exact fit");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestDefaultChangeMovesFollowingStreams(void)
{
    maudContext* context = Offline(32);
    maudDeviceId first;
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &first) ==
              maud_success,
          "first output");
    Blocks following = {0};
    Blocks pinned = {0};
    maudStreamId follower = OpenOutput(context, (maudDeviceId){0, 0}, maud_roleGeneral, &following);
    maudStreamId fixed = OpenOutput(context, first, maud_roleGeneral, &pinned);
    maudDeviceId second = AddDevice(context, maud_directionOutput, 44100, "Second");
    maudNotification record = Expect(context, maud_notifyDeviceAdded, "added");
    CHECK(Same(record.deviceId, second) && record.direction == maud_directionOutput, "added id");
    ExpectDrained(context, "a second device changes no default");
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleGeneral, second) == maud_success, "set");
    record = Expect(context, maud_notifyDefaultChanged, "default changed");
    CHECK(Same(record.deviceId, second) && record.role == maud_roleGeneral, "new default");
    record = Expect(context, maud_notifyStreamMoved, "follower moved");
    CHECK(SameStream(record.streamId, follower) && Same(record.deviceId, second), "moved where");
    record = Expect(context, maud_notifyStreamFormatChanged, "native rate changed");
    CHECK(record.sampleRate == 44100, "to the new device's rate");
    ExpectDrained(context, "the pinned stream stays");
    maudStreamStatus status;
    CHECK(maudGetStreamStatus(context, fixed, &status) == maud_success, "pinned status");
    CHECK(Same(status.device, first) && status.suspension == maud_suspendNone, "still on first");
    float frames[2 * 480];
    CHECK(maudRenderStream(context, follower, frames, 480) == maud_success, "render follower");
    CHECK(following.lastRate == 44100, "blocks carry the new rate");
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, follower, &format) == maud_success, "format");
    CHECK(format.sampleRate == 44100, "format follows");
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleGeneral, second) == maud_success, "again");
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
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleGeneral, headset) == maud_success, "set");
    Expect(context, maud_notifyDefaultChanged, "general default");
    ExpectDrained(context, "the voice stream follows communications");
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleCommunications, headset) == maud_success,
          "set communications");
    Expect(context, maud_notifyDefaultChanged, "communications default");
    maudNotification record = Expect(context, maud_notifyStreamMoved, "voice moved");
    CHECK(SameStream(record.streamId, voice), "the voice stream");
    ExpectDrained(context, "same rate, no format change");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestLossSuspendsPinnedStreamsAndMovesFollowers(void)
{
    maudContext* context = Offline(32);
    maudDeviceId speakers = AddDevice(context, maud_directionOutput, 48000, "Speakers");
    maudDeviceId first;
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &first) ==
              maud_success,
          "first");
    Blocks blocks = {0};
    maudStreamId follower = OpenOutput(context, (maudDeviceId){0, 0}, maud_roleGeneral, &blocks);
    maudStreamId pinned = OpenOutput(context, first, maud_roleGeneral, &blocks);
    Expect(context, maud_notifyDeviceAdded, "added");
    CHECK(maudRemoveOfflineDevice(context, first) == maud_success, "unplug the default");
    maudNotification record = Expect(context, maud_notifyDeviceRemoved, "removed");
    CHECK(Same(record.deviceId, first), "removed id");
    record = Expect(context, maud_notifyStreamSuspended, "pinned suspended");
    CHECK(SameStream(record.streamId, pinned) && record.reason == maud_suspendDeviceLost, "lost");
    record = Expect(context, maud_notifyDefaultChanged, "general default passes on");
    CHECK(Same(record.deviceId, speakers) && record.role == maud_roleGeneral, "to speakers");
    record = Expect(context, maud_notifyStreamMoved, "follower moved");
    CHECK(SameStream(record.streamId, follower), "the follower");
    record = Expect(context, maud_notifyDefaultChanged, "communications default passes on");
    CHECK(record.role == maud_roleCommunications, "communications");
    ExpectDrained(context, "nothing else");
    float frames[2 * 16];
    CHECK(maudRenderStream(context, pinned, frames, 16) == maud_errorState, "lost renders not");
    CHECK(maudRenderStream(context, follower, frames, 16) == maud_success, "follower renders");
    maudStreamStatus status;
    CHECK(maudGetStreamStatus(context, pinned, &status) == maud_success, "status");
    CHECK(status.started && status.suspension == maud_suspendDeviceLost, "started but lost");
    CHECK(status.device.index1 == 0, "on no device");
    maudDeviceInfo info;
    CHECK(maudGetDeviceInfo(context, first, &info) == maud_errorStale, "removed id is stale");
    CHECK(maudRemoveOfflineDevice(context, first) == maud_errorStale, "remove twice");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestFollowersWaitForADevice(void)
{
    maudContext* context = Offline(32);
    maudDeviceId only;
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &only) ==
              maud_success,
          "only output");
    Blocks blocks = {0};
    maudStreamId follower = OpenOutput(context, (maudDeviceId){0, 0}, maud_roleGeneral, &blocks);
    CHECK(maudRemoveOfflineDevice(context, only) == maud_success, "unplug the last output");
    Expect(context, maud_notifyDeviceRemoved, "removed");
    maudNotification record = Expect(context, maud_notifyDefaultChanged, "no general default");
    CHECK(record.deviceId.index1 == 0, "null default");
    record = Expect(context, maud_notifyStreamSuspended, "follower waits");
    CHECK(record.reason == maud_suspendNoDevice, "no device");
    Expect(context, maud_notifyDefaultChanged, "no communications default");
    ExpectDrained(context, "nothing else");
    maudDeviceId none;
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &none) ==
              maud_empty,
          "no default");
    float frames[2 * 16];
    CHECK(maudRenderStream(context, follower, frames, 16) == maud_errorState, "waiting");
    Blocks late = {0};
    maudStreamId opened = OpenOutput(context, (maudDeviceId){0, 0}, maud_roleGeneral, &late);
    maudStreamStatus status;
    CHECK(maudGetStreamStatus(context, opened, &status) == maud_success, "status");
    CHECK(status.suspension == maud_suspendNoDevice, "opened waiting");
    maudDeviceId back = AddDevice(context, maud_directionOutput, 48000, "Back");
    Expect(context, maud_notifyDeviceAdded, "added");
    record = Expect(context, maud_notifyDefaultChanged, "it becomes the default");
    CHECK(Same(record.deviceId, back), "the new device");
    record = Expect(context, maud_notifyStreamMoved, "follower moved");
    CHECK(SameStream(record.streamId, follower), "first follower");
    record = Expect(context, maud_notifyStreamResumed, "follower resumed");
    CHECK(SameStream(record.streamId, follower), "first follower resumed");
    Expect(context, maud_notifyStreamMoved, "late stream moved");
    Expect(context, maud_notifyStreamResumed, "late stream resumed");
    Expect(context, maud_notifyDefaultChanged, "communications default");
    ExpectDrained(context, "nothing else");
    CHECK(maudRenderStream(context, follower, frames, 16) == maud_success, "renders again");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void TestOverflowRecordCountsDropped(void)
{
    maudContext* context = Offline(4);
    maudDeviceId devices[2];
    devices[0] = AddDevice(context, maud_directionInput, 48000, "A");
    devices[1] = AddDevice(context, maud_directionInput, 48000, "B");
    CHECK(maudRemoveOfflineDevice(context, devices[0]) == maud_success, "remove A");
    CHECK(maudRemoveOfflineDevice(context, devices[1]) == maud_success, "remove B");
    AddDevice(context, maud_directionInput, 48000, "C");
    Expect(context, maud_notifyDeviceAdded, "first record");
    Expect(context, maud_notifyDeviceAdded, "second record");
    Expect(context, maud_notifyDeviceRemoved, "third record");
    maudNotification record = Expect(context, maud_notifyOverflow, "overflow takes the last");
    CHECK(record.droppedCount == 2, "the dropped records are counted");
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
    CHECK(maudAddOfflineDevice(context, &def, &id) == maud_errorInvalid, "no cookie");
    CHECK(id.index1 == 0, "null id on failure");
    def = maudDefaultOfflineDeviceDef();
    def.sampleRate = 400000;
    CHECK(maudAddOfflineDevice(context, &def, &id) == maud_errorInvalid, "rate out of range");
    def = maudDefaultOfflineDeviceDef();
    def.nameLength = 3;
    CHECK(maudAddOfflineDevice(context, &def, &id) == maud_errorInvalid, "length without name");
    CHECK(maudGetContextMisuse(context) == 3, "each counted");
    def = maudDefaultOfflineDeviceDef();
    def.name = "A name longer than thirty-two bytes";
    def.nameLength = strlen(def.name);
    CHECK(maudAddOfflineDevice(context, &def, &id) == maud_errorCapacity, "name too long");
    AddDevice(context, maud_directionOutput, 48000, "Third");
    AddDevice(context, maud_directionOutput, 48000, "Fourth");
    def = maudDefaultOfflineDeviceDef();
    CHECK(maudAddOfflineDevice(context, &def, &id) == maud_errorCapacity, "device limit");
    CHECK(maudSetOfflineDefaultDevice(context, 2, id) == maud_errorInvalid, "unknown role");
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleGeneral, (maudDeviceId){1, 9}) ==
              maud_errorStale,
          "stale default");
    maudDeviceId input;
    CHECK(maudGetDefaultDevice(context, maud_directionInput, maud_roleGeneral, &input) ==
              maud_success,
          "input");
    Blocks blocks = {0};
    maudStreamDef stream = maudDefaultStreamDef();
    stream.mode = maud_modePull;
    stream.callback = Count;
    stream.user = &blocks;
    stream.device = input;
    maudStreamId streamId;
    uint64_t misuse = maudGetContextMisuse(context);
    CHECK(maudCreateStream(context, &stream, &streamId) == maud_errorInvalid, "wrong direction");
    CHECK(maudGetContextMisuse(context) == misuse + 1, "counted");
    stream.device = (maudDeviceId){1, 9};
    CHECK(maudCreateStream(context, &stream, &streamId) == maud_errorStale, "stale device");
    maudDeviceId ids[1];
    uint32_t count = 0;
    CHECK(maudGetDevices(context, maud_directionOutput, ids, 1, &count) == maud_success, "list");
    CHECK(count == 3, "the true total past the capacity");
    CHECK(maudGetDevices(context, 2, ids, 1, &count) == maud_errorInvalid, "unknown direction");
    CHECK(maudGetDevices(context, maud_directionOutput, nullptr, 1, &count) == maud_errorInvalid,
          "null array with room");
    CHECK(maudNextNotification(context, nullptr) == maud_errorInvalid, "null record");
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
    CHECK(maudAddOfflineDevice(context, &def, &device) == maud_success, "add");
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, device, &info) == maud_success &&
              info.form == maud_formSpeakers,
          "it reports its form");
    Blocks blocks = {0};
    maudStreamId stream = OpenOutput(context, device, maud_roleGeneral, &blocks);
    while (maudNextNotification(context, &(maudNotification){0}) == maud_success)
    {
    }
    CHECK(maudSetOfflineDeviceForm(context, device, maud_formHeadphones) == maud_success,
          "plug headphones");
    maudNotification record = Expect(context, maud_notifyRouteChanged, "a route record");
    CHECK(Same(record.deviceId, device) && record.direction == maud_directionOutput &&
              record.form == maud_formHeadphones,
          "naming the device and its new form");
    CHECK(maudGetDeviceInfo(context, device, &info) == maud_success &&
              info.form == maud_formHeadphones,
          "which its info reports");
    CHECK(maudSetOfflineDeviceForm(context, device, maud_formHeadphones) == maud_success,
          "the same again");
    ExpectDrained(context, "is no change");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success &&
              Same(status.device, device) && status.suspension == maud_suspendNone,
          "the stream stays on it");
    CHECK(maudSetOfflineDeviceForm(context, device, maud_formDigital + 1) == maud_errorInvalid,
          "an unknown form");
    def.form = maud_formDigital + 1;
    CHECK(maudAddOfflineDevice(context, &def, &device) == maud_errorInvalid,
          "an unknown form in a def");
    CHECK(maudSetOfflineDeviceForm(context, (maudDeviceId){3, 7}, maud_formLine) == maud_errorStale,
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
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &output) ==
                  maud_success &&
              maudGetDefaultDevice(context, maud_directionInput, maud_roleGeneral, &input) ==
                  maud_success,
          "the starting devices");
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, output, &info) == maud_success &&
              info.spatializer == maud_spatializerNone && !info.headTracking &&
              info.spatialObjects == 0,
          "an offline output has none");
    CHECK(maudGetDeviceInfo(context, input, &info) == maud_success &&
              info.spatializer == maud_spatializerUnknown,
          "an input says nothing");
    while (maudNextNotification(context, &(maudNotification){0}) == maud_success)
    {
    }
    CHECK(maudSetOfflineDeviceSpatializer(context, output, maud_spatializerOn, true, 128) ==
              maud_success,
          "a spatial format turned on");
    maudNotification record = Expect(context, maud_notifySpatializerChanged, "a record");
    CHECK(Same(record.deviceId, output) && record.direction == maud_directionOutput,
          "naming the device");
    CHECK(maudGetDeviceInfo(context, output, &info) == maud_success &&
              info.spatializer == maud_spatializerOn && info.headTracking &&
              info.spatialObjects == 128,
          "which its info reports");
    CHECK(maudSetOfflineDeviceSpatializer(context, output, maud_spatializerOn, true, 128) ==
              maud_success,
          "the same again");
    ExpectDrained(context, "is no change");
    CHECK(maudSetOfflineDeviceSpatializer(context, output, maud_spatializerOn, false, 128) ==
              maud_success,
          "the head tracker gone");
    Expect(context, maud_notifySpatializerChanged, "is a change");
    CHECK(maudSetOfflineDeviceSpatializer(context, output, maud_spatializerOn, false, 20) ==
              maud_success,
          "fewer objects");
    Expect(context, maud_notifySpatializerChanged, "is a change");
    CHECK(maudSetOfflineDeviceSpatializer(context, output, maud_spatializerOff, false, 4) ==
              maud_errorInvalid,
          "objects while off");
    CHECK(maudSetOfflineDeviceSpatializer(context, output, maud_spatializerOn + 1, false, 0) ==
              maud_errorInvalid,
          "an unknown state");
    CHECK(maudSetOfflineDeviceSpatializer(context, input, maud_spatializerOff, false, 0) ==
              maud_errorInvalid,
          "an input");
    CHECK(maudSetOfflineDeviceSpatializer(context, (maudDeviceId){3, 7}, maud_spatializerOff, false,
                                          0) == maud_errorStale,
          "a stale device");
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
    return s_failures == 0 ? 0 : 1;
}
