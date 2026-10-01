// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PipeWire backend against a running daemon: its devices, a device
// plugged in and out, and a default changed, with a second PipeWire
// client of the test's own doing the plugging. Without a daemon the
// test is skipped, unless MAUD_REQUIRE_PIPEWIRE is set.

#include "test_harness.h"

#include "maul-audio/notification.h"

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SKIP 77

// The test's own client: a thread loop, a core, the default metadata.
typedef struct Helper
{
    struct pw_thread_loop* loop;
    struct pw_context* context;
    struct pw_core* core;
    struct pw_registry* registry;
    struct spa_hook registryListener;
    struct pw_metadata* metadata;
    struct pw_proxy* node;
} Helper;

static void OnHelperGlobal(void* data, uint32_t id, uint32_t permissions, const char* type,
                           uint32_t version, const struct spa_dict* props)
{
    (void)permissions;
    (void)version;
    Helper* helper = data;
    const char* name = props != nullptr ? spa_dict_lookup(props, PW_KEY_METADATA_NAME) : nullptr;
    if (helper->metadata == nullptr && strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 &&
        name != nullptr && strcmp(name, "default") == 0)
    {
        helper->metadata = pw_registry_bind(helper->registry, id, PW_TYPE_INTERFACE_Metadata,
                                            PW_VERSION_METADATA, 0);
    }
}

static const struct pw_registry_events s_helperRegistry = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = OnHelperGlobal,
};

static void Sleep(int milliseconds)
{
    struct timespec pause = {0, (long)milliseconds * 1000000L};
    nanosleep(&pause, nullptr);
}

static bool StartHelper(Helper* helper)
{
    *helper = (Helper){0};
    pw_init(nullptr, nullptr);
    helper->loop = pw_thread_loop_new("maud-test", nullptr);
    helper->context = pw_context_new(pw_thread_loop_get_loop(helper->loop), nullptr, 0);
    pw_thread_loop_start(helper->loop);
    pw_thread_loop_lock(helper->loop);
    helper->core = pw_context_connect(helper->context, nullptr, 0);
    if (helper->core != nullptr)
    {
        helper->registry = pw_core_get_registry(helper->core, PW_VERSION_REGISTRY, 0);
        pw_registry_add_listener(helper->registry, &helper->registryListener, &s_helperRegistry,
                                 helper);
    }
    pw_thread_loop_unlock(helper->loop);
    // The loop thread binds the metadata when its global arrives; read it
    // under the loop's lock.
    bool ready = false;
    for (int tries = 0; tries < 300 && !ready && helper->core != nullptr; ++tries)
    {
        Sleep(10);
        pw_thread_loop_lock(helper->loop);
        ready = helper->metadata != nullptr;
        pw_thread_loop_unlock(helper->loop);
    }
    return ready;
}

// Plugs in a null sink named maud-test-hotplug at 44.1 kHz; it lives as
// long as the helper's node proxy.
static void PlugSink(Helper* helper)
{
    pw_thread_loop_lock(helper->loop);
    struct pw_properties* props = pw_properties_new(
        "factory.name", "support.null-audio-sink", "node.name", "maud-test-hotplug",
        "node.description", "Maud hotplug sink", "media.class", "Audio/Sink", "audio.rate", "44100",
        "audio.channels", "2", "audio.position", "[FL FR]", nullptr);
    helper->node = pw_core_create_object(helper->core, "adapter", PW_TYPE_INTERFACE_Node,
                                         PW_VERSION_NODE, &props->dict, 0);
    pw_properties_free(props);
    pw_thread_loop_unlock(helper->loop);
}

static void UnplugSink(Helper* helper)
{
    pw_thread_loop_lock(helper->loop);
    pw_proxy_destroy(helper->node);
    helper->node = nullptr;
    pw_thread_loop_unlock(helper->loop);
}

// Writes a default metadata key naming a node: the configured default,
// which the session manager turns into the effective one, or the
// effective default itself.
static void SetDefaultKey(Helper* helper, const char* key, const char* nodeName)
{
    char value[128];
    snprintf(value, sizeof(value), "{ \"name\": \"%s\" }", nodeName);
    pw_thread_loop_lock(helper->loop);
    pw_metadata_set_property(helper->metadata, PW_ID_CORE, key, "Spa:String:JSON", value);
    pw_thread_loop_unlock(helper->loop);
}

static void StopHelper(Helper* helper)
{
    pw_thread_loop_lock(helper->loop);
    if (helper->node != nullptr)
    {
        pw_proxy_destroy(helper->node);
    }
    if (helper->metadata != nullptr)
    {
        pw_proxy_destroy((struct pw_proxy*)helper->metadata);
    }
    if (helper->registry != nullptr)
    {
        spa_hook_remove(&helper->registryListener);
        pw_proxy_destroy((struct pw_proxy*)helper->registry);
    }
    if (helper->core != nullptr)
    {
        pw_core_disconnect(helper->core);
    }
    pw_thread_loop_unlock(helper->loop);
    pw_thread_loop_stop(helper->loop);
    pw_context_destroy(helper->context);
    pw_thread_loop_destroy(helper->loop);
    pw_deinit();
}

static bool KeyIs(const maudContext* context, maudDeviceId device, const char* key)
{
    char bytes[64];
    size_t length = 0;
    return maudGetDeviceKey(context, device, bytes, sizeof(bytes), &length) == maud_success &&
           length == strlen(key) && memcmp(bytes, key, length) == 0;
}

// Drains notifications for up to three seconds until one of kind
// arrives whose device has key, or whose device is that one.
static bool WaitFor(maudContext* context, maudNotificationKind kind, const char* key,
                    maudDeviceId* deviceOut)
{
    for (int tries = 0; tries < 300; ++tries)
    {
        maudNotification record;
        while (maudNextNotification(context, &record) == maud_success)
        {
            bool matches = key == nullptr ? record.deviceId.index1 == deviceOut->index1 &&
                                                record.deviceId.generation == deviceOut->generation
                                          : KeyIs(context, record.deviceId, key);
            if (record.kind == kind && matches)
            {
                *deviceOut = record.deviceId;
                return true;
            }
        }
        Sleep(10);
    }
    return false;
}

// Drains notifications for up to three seconds until device is the
// default output for both roles.
static bool WaitForDefault(maudContext* context, maudDeviceId device)
{
    for (int tries = 0; tries < 300; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        maudDeviceId general = {0, 0};
        maudDeviceId communications = {0, 0};
        if (maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &general) ==
                maud_success &&
            maudGetDefaultDevice(context, maud_directionOutput, maud_roleCommunications,
                                 &communications) == maud_success &&
            general.index1 == device.index1 && general.generation == device.generation &&
            communications.index1 == device.index1)
        {
            return true;
        }
        Sleep(10);
    }
    return false;
}

static bool FindByKey(const maudContext* context, maudDirection direction, const char* key,
                      maudDeviceId* deviceOut)
{
    maudDeviceId ids[32];
    uint32_t count = 0;
    if (maudGetDevices(context, direction, ids, 32, &count) != maud_success)
    {
        return false;
    }
    for (uint32_t i = 0; i < count && i < 32; ++i)
    {
        if (KeyIs(context, ids[i], key))
        {
            *deviceOut = ids[i];
            return true;
        }
    }
    return false;
}

static void TestTheDaemonsDevices(maudContext* context)
{
    CHECK(maudGetContextBackend(context) == maud_backendNative, "native backend");
    maudDeviceId sink = {0, 0};
    CHECK(FindByKey(context, maud_directionOutput, "maud-test-sink", &sink), "the test sink");
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, sink, &info) == maud_success, "sink info");
    CHECK(info.nativeSampleRate == 48000 && info.nativeLayout == maud_layoutStereo, "48 kHz");
    CHECK(info.defaultGeneral && info.defaultCommunications, "the default sink");
    char name[64];
    size_t length = 0;
    CHECK(maudGetDeviceName(context, sink, name, sizeof(name), &length) == maud_success, "name");
    CHECK(length == 14 && memcmp(name, "Maud test sink", 14) == 0, "its description");
    maudDeviceId source = {0, 0};
    CHECK(FindByKey(context, maud_directionInput, "maud-test-source", &source), "test source");
    maudDeviceId fallback = {0, 0};
    CHECK(maudGetDefaultDevice(context, maud_directionInput, maud_roleGeneral, &fallback) ==
              maud_success,
          "default source");
    CHECK(fallback.index1 == source.index1, "the test source is the default");
}

// Points both default keys at the test sink, whatever an earlier run
// left in the daemon, and waits until the context agrees.
static void ResetDefaults(maudContext* context, Helper* helper)
{
    SetDefaultKey(helper, "default.configured.audio.sink", "maud-test-sink");
    SetDefaultKey(helper, "default.audio.sink", "maud-test-sink");
    maudDeviceId sink = {0, 0};
    CHECK(FindByKey(context, maud_directionOutput, "maud-test-sink", &sink), "the test sink");
    CHECK(WaitForDefault(context, sink), "the test sink is the default");
}

static void TestHotplugAndDefaults(maudContext* context, Helper* helper)
{
    ResetDefaults(context, helper);
    maudDeviceId plugged = {0, 0};
    PlugSink(helper);
    CHECK(WaitFor(context, maud_notifyDeviceAdded, "maud-test-hotplug", &plugged), "plugged in");
    maudDeviceInfo info = {0};
    for (int tries = 0; tries < 100 && info.nativeSampleRate != 44100; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        CHECK(maudGetDeviceInfo(context, plugged, &info) == maud_success, "plugged info");
        Sleep(10);
    }
    CHECK(info.nativeSampleRate == 44100, "its rate");
    // The effective default, as the session manager writes it. A direct
    // write stays until the session manager's own choice changes.
    SetDefaultKey(helper, "default.audio.sink", "maud-test-hotplug");
    maudDeviceId changed = plugged;
    CHECK(WaitFor(context, maud_notifyDefaultChanged, nullptr, &changed), "effective default");
    maudDeviceId sink = {0, 0};
    CHECK(FindByKey(context, maud_directionOutput, "maud-test-sink", &sink), "the test sink");
    SetDefaultKey(helper, "default.audio.sink", "maud-test-sink");
    CHECK(WaitForDefault(context, sink), "effective default back");
    // The configured default, as a user's choice writes it; the session
    // manager makes it the effective one.
    SetDefaultKey(helper, "default.configured.audio.sink", "maud-test-hotplug");
    CHECK(WaitForDefault(context, plugged), "configured to the plugged sink");
    CHECK(maudGetDeviceInfo(context, plugged, &info) == maud_success && info.defaultGeneral,
          "it reports itself the default");
    SetDefaultKey(helper, "default.configured.audio.sink", "maud-test-sink");
    CHECK(WaitForDefault(context, sink), "configured back to the test sink");
    UnplugSink(helper);
    maudDeviceId removed = plugged;
    CHECK(WaitFor(context, maud_notifyDeviceRemoved, nullptr, &removed), "unplugged");
    CHECK(maudGetDeviceInfo(context, plugged, &info) == maud_errorStale, "its id is stale");
    ResetDefaults(context, helper);
}

// What a stream's callback saw, written on libpipewire's data thread.
typedef struct Blocks
{
    _Atomic(uint32_t) count;
    _Atomic(uint32_t) wrongSize;
    _Atomic(uint32_t) lastRate;
    _Atomic(uint32_t) withInput;
    _Atomic(int32_t) controlResult;
    uint32_t periodFrames;
    bool tryControl;
    maudContext* context;
    maudStreamId stream;
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
    atomic_store(&blocks->lastRate, block->sampleRate);
    if (blocks->tryControl && atomic_load(&blocks->count) == 2)
    {
        atomic_store(&blocks->controlResult, maudStopStream(blocks->context, blocks->stream));
    }
    atomic_fetch_add(&blocks->count, 1);
}

// Drains notifications, which runs PipeWire's main loop, for up to
// three seconds until the callback has run count times.
static bool WaitForBlocks(maudContext* context, Blocks* blocks, uint32_t count)
{
    for (int tries = 0; tries < 300 && atomic_load(&blocks->count) < count; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        Sleep(10);
    }
    return atomic_load(&blocks->count) >= count;
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
    blocks->context = context;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "create stream");
    blocks->stream = stream;
    CHECK(maudStartStream(context, stream) == maud_success, "start stream");
    return stream;
}

static void TestOutputStream(maudContext* context)
{
    Blocks blocks = {.tryControl = true};
    maudStreamId stream = OpenStream(context, maud_directionOutput, (maudDeviceId){0, 0}, &blocks);
    maudStreamFormat format = {0};
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success, "format");
    CHECK(format.sampleRate == 48000 && format.periodFrames == 256, "the sink's rate");
    CHECK(WaitForBlocks(context, &blocks, 20), "callbacks on the data thread");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "every block is one period");
    CHECK(atomic_load(&blocks.lastRate) == 48000, "blocks carry the rate");
    CHECK(atomic_load(&blocks.controlResult) == maud_errorState, "control refused there");
    CHECK(maudGetContextMisuse(context) >= 1, "and counted");
    uint64_t position = 0;
    CHECK(maudGetStreamPosition(context, stream, &position) == maud_success && position > 0,
          "the clock advances");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    WaitForBlocks(context, &blocks, UINT32_MAX / 2);
    uint32_t stopped = atomic_load(&blocks.count);
    Sleep(100);
    CHECK(atomic_load(&blocks.count) == stopped, "no callbacks once stopped");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy stream");
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.callback = CountBlocks;
    def.user = &blocks;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no pull mode");
}

static void TestInputStream(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamId stream = OpenStream(context, maud_directionInput, (maudDeviceId){0, 0}, &blocks);
    CHECK(WaitForBlocks(context, &blocks, 10), "capture callbacks");
    CHECK(atomic_load(&blocks.withInput) == atomic_load(&blocks.count), "blocks hold input");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "every capture block is one period");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy capture");
}

// Drains notifications for up to three seconds until one of kind for
// stream arrives.
static bool WaitForStream(maudContext* context, maudNotificationKind kind, maudStreamId stream,
                          maudNotification* recordOut)
{
    for (int tries = 0; tries < 300; ++tries)
    {
        while (maudNextNotification(context, recordOut) == maud_success)
        {
            if (recordOut->kind == kind && recordOut->streamId.index1 == stream.index1 &&
                recordOut->streamId.generation == stream.generation)
            {
                return true;
            }
        }
        Sleep(10);
    }
    return false;
}

static void TestStreamsMoveAndAreLost(maudContext* context, Helper* helper)
{
    ResetDefaults(context, helper);
    Blocks following = {0};
    maudStreamId follower =
        OpenStream(context, maud_directionOutput, (maudDeviceId){0, 0}, &following);
    PlugSink(helper);
    maudDeviceId plugged = {0, 0};
    CHECK(WaitFor(context, maud_notifyDeviceAdded, "maud-test-hotplug", &plugged), "plugged in");
    Blocks pinnedBlocks = {0};
    maudStreamId pinned = OpenStream(context, maud_directionOutput, plugged, &pinnedBlocks);
    SetDefaultKey(helper, "default.configured.audio.sink", "maud-test-hotplug");
    maudNotification record;
    CHECK(WaitForStream(context, maud_notifyStreamFormatChanged, follower, &record),
          "the follower takes the new device's rate");
    CHECK(record.sampleRate == 44100, "44.1 kHz");
    uint32_t before = atomic_load(&following.count);
    CHECK(WaitForBlocks(context, &following, before + 20), "the follower runs on");
    CHECK(atomic_load(&following.lastRate) == 44100, "its blocks carry the new rate");
    SetDefaultKey(helper, "default.configured.audio.sink", "maud-test-sink");
    CHECK(WaitForStream(context, maud_notifyStreamFormatChanged, follower, &record), "and back");
    UnplugSink(helper);
    CHECK(WaitForStream(context, maud_notifyStreamSuspended, pinned, &record), "pinned lost");
    CHECK(record.reason == maud_suspendDeviceLost, "its device is gone");
    CHECK(maudDestroyStream(context, pinned) == maud_success, "destroy pinned");
    CHECK(maudDestroyStream(context, follower) == maud_success, "destroy follower");
    ResetDefaults(context, helper);
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    maudResult result = maudCreateContext(&def, &context);
    if (result == maud_errorUnsupported && getenv("MAUD_REQUIRE_PIPEWIRE") == nullptr)
    {
        return SKIP;
    }
    CHECK(result == maud_success, "a native context on the daemon");
    if (result != maud_success)
    {
        return 1;
    }
    TestTheDaemonsDevices(context);
    Helper helper;
    CHECK(StartHelper(&helper), "the test's own client");
    TestHotplugAndDefaults(context, &helper);
    TestOutputStream(context);
    TestInputStream(context);
    TestStreamsMoveAndAreLost(context, &helper);
    StopHelper(&helper);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
