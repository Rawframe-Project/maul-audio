// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PipeWire backend against a running daemon: its devices, a device
// plugged in and out, and a default changed, with a second PipeWire
// client of the test's own doing the plugging. Without a daemon the
// test is skipped, unless MAUD_REQUIRE_PIPEWIRE is set.

#include "test_clock.h"
#include "test_harness.h"

#include "maul-audio/notification.h"

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <stdatomic.h>
#include <stdio.h>
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
    struct pw_metadata* settings;
    struct pw_proxy* node;
    // The links' input nodes and the plugged sink's global id, written
    // on the helper's loop thread.
    uint32_t linkInputs[256];
    uint32_t linkCount;
    uint32_t pluggedId;
} Helper;

static void OnHelperGlobal(void* data, uint32_t id, uint32_t permissions, const char* type,
                           uint32_t version, const struct spa_dict* props)
{
    (void)permissions;
    (void)version;
    Helper* helper = data;
    const char* input = props != nullptr ? spa_dict_lookup(props, PW_KEY_LINK_INPUT_NODE) : nullptr;
    if (strcmp(type, PW_TYPE_INTERFACE_Link) == 0 && input != nullptr && helper->linkCount < 256)
    {
        helper->linkInputs[helper->linkCount++] = (uint32_t)atoi(input);
    }
    const char* nodeName = props != nullptr ? spa_dict_lookup(props, PW_KEY_NODE_NAME) : nullptr;
    if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0 && nodeName != nullptr &&
        strcmp(nodeName, "maud-test-hotplug") == 0)
    {
        helper->pluggedId = id;
    }
    const char* name = props != nullptr ? spa_dict_lookup(props, PW_KEY_METADATA_NAME) : nullptr;
    if (helper->metadata == nullptr && strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 &&
        name != nullptr && strcmp(name, "default") == 0)
    {
        helper->metadata = pw_registry_bind(helper->registry, id, PW_TYPE_INTERFACE_Metadata,
                                            PW_VERSION_METADATA, 0);
    }
    if (helper->settings == nullptr && strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 &&
        name != nullptr && strcmp(name, "settings") == 0)
    {
        helper->settings = pw_registry_bind(helper->registry, id, PW_TYPE_INTERFACE_Metadata,
                                            PW_VERSION_METADATA, 0);
    }
}

static const struct pw_registry_events s_helperRegistry = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = OnHelperGlobal,
};

static void Sleep(int milliseconds)
{
    struct timespec pause = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L};
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
        ready = helper->metadata != nullptr && helper->settings != nullptr;
        pw_thread_loop_unlock(helper->loop);
    }
    return ready;
}

// Plugs in a null sink named maud-test-hotplug at 44.1 kHz, which says
// it is headphones; it lives as long as the helper's node proxy.
static void PlugSink(Helper* helper)
{
    pw_thread_loop_lock(helper->loop);
    struct pw_properties* props =
        pw_properties_new("factory.name", "support.null-audio-sink", "node.name",
                          "maud-test-hotplug", "node.description", "Maud hotplug sink",
                          "media.class", "Audio/Sink", "audio.rate", "44100", "audio.channels", "2",
                          "audio.position", "[FL FR]", "device.form-factor", "headphone", nullptr);
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

// Forces the graph's rate, or with "0" lets it go.
static void ForceGraphRate(Helper* helper, const char* rate)
{
    pw_thread_loop_lock(helper->loop);
    pw_metadata_set_property(helper->settings, PW_ID_CORE, "clock.force-rate", nullptr, rate);
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
    if (helper->settings != nullptr)
    {
        pw_proxy_destroy((struct pw_proxy*)helper->settings);
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
    CHECK(maudGetContextBackend(context) == maud_backendPipewire, "PipeWire chosen");
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
    CHECK(maudGetDeviceInfo(context, plugged, &info) == maud_success, "plugged info");
    CHECK(info.nativeSampleRate == 48000 && info.minSampleRate == 48000 &&
              info.maxSampleRate == 48000,
          "a 44.1 kHz node runs at the graph's rate");
    CHECK(info.form == maud_formHeadphones, "its form factor read as headphones");
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
    // The block at which the callback stalls for 300 ms, or 0; set while
    // it stalls.
    uint32_t stallAt;
    atomic_bool stalling;
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
    if (blocks->stallAt != 0 && atomic_load(&blocks->count) == blocks->stallAt)
    {
        atomic_store(&blocks->stalling, true);
        Sleep(300);
        atomic_store(&blocks->stalling, false);
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

// Whether a link into the plugged sink appeared within three seconds.
static bool WaitForLinkIntoPlugged(maudContext* context, Helper* helper)
{
    for (int tries = 0; tries < 300; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        pw_thread_loop_lock(helper->loop);
        bool linked = false;
        for (uint32_t i = 0; i < helper->linkCount && helper->pluggedId != 0; ++i)
        {
            linked = linked || helper->linkInputs[i] == helper->pluggedId;
        }
        pw_thread_loop_unlock(helper->loop);
        if (linked)
        {
            return true;
        }
        Sleep(10);
    }
    return false;
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
    double rate = MeasureRate(context, stream);
    CHECK(Near(rate, 48000.0), "the clock advances at the stream's rate");
    CHECK(StreamClockIsSound(context, stream, true, false, Sleep),
          "its clock maps frames to host time");
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
    def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.user = &blocks;
    def.ratePolicy = maud_rateRequired;
    def.sampleRate = 44100;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "a rate the device does not run at");
    def.sampleRate = 48000;
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "the device's own rate");
    maudStreamClock clock = {.hostNanoseconds = 1};
    CHECK(maudGetStreamClock(context, stream, &clock) == maud_success &&
              clock.hostNanoseconds == 0 && clock.position == 0,
          "a new stream has no stamp, even in a slot used before");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

static void TestInputStream(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamId stream = OpenStream(context, maud_directionInput, (maudDeviceId){0, 0}, &blocks);
    CHECK(WaitForBlocks(context, &blocks, 10), "capture callbacks");
    CHECK(atomic_load(&blocks.withInput) == atomic_load(&blocks.count), "blocks hold input");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "every capture block is one period");
    CHECK(StreamClockIsSound(context, stream, false, true, Sleep),
          "its clock maps frames to host time");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy capture");
}

// The drivers pw-top lists the library's nodes under, from its last
// report: the count of nodes found and of distinct drivers among them.
// The library's nodes are the followers that are not the test devices;
// pw-top names them after the program.
static void DriversOfOurNodes(uint32_t* nodesOut, uint32_t* driversOut)
{
    *nodesOut = 0;
    *driversOut = 0;
    if (system("pw-top -b -n 3 > pw-top.txt 2> /dev/null") != 0)
    {
        return;
    }
    FILE* file = fopen("pw-top.txt", "r");
    if (file == nullptr)
    {
        return;
    }
    char line[512];
    long driver = -1;
    long seen[2] = {-1, -1};
    uint32_t nodes = 0;
    while (fgets(line, sizeof line, file) != nullptr)
    {
        if (strncmp(line, "S   ID", 6) == 0)
        {
            nodes = 0;
            seen[0] = seen[1] = -1;
            continue;
        }
        long id = strtol(line + 1, nullptr, 10);
        if (strstr(line, " + ") == nullptr)
        {
            driver = id;
        }
        else if (strstr(line, "+ maud-test-") == nullptr && nodes < 2)
        {
            seen[nodes++] = driver;
        }
    }
    fclose(file);
    remove("pw-top.txt");
    *nodesOut = nodes;
    *driversOut = nodes == 0 ? 0 : (nodes == 2 && seen[0] != seen[1] ? 2u : 1u);
}

// A callback that stalls makes the graph skip the stream's cycles: an
// underrun for an output, an overrun for an input, each counted.
static void TestXruns(maudContext* context)
{
    for (int input = 0; input < 2; ++input)
    {
        Blocks blocks = {.stallAt = 30};
        maudDirection direction = input ? maud_directionInput : maud_directionOutput;
        maudStreamId stream = OpenStream(context, direction, (maudDeviceId){0, 0}, &blocks);
        CHECK(WaitForBlocks(context, &blocks, 80), "past the stall");
        maudStreamStatus status = {0};
        CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
        CHECK(input ? status.overruns >= 1 && status.underruns == 0
                    : status.underruns >= 1 && status.overruns == 0,
              "the stall counted by the stream's direction");
        CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    }
    // A calm stream, stopped and started ten times, counts next to
    // nothing: neither its cycles nor the gaps of its stops are xruns.
    // Five are allowed, for a loaded machine; a gap counted would add ten.
    Blocks calm = {0};
    maudStreamId stream = OpenStream(context, maud_directionOutput, (maudDeviceId){0, 0}, &calm);
    for (int restart = 0; restart < 10; ++restart)
    {
        CHECK(WaitForBlocks(context, &calm, atomic_load(&calm.count) + 40), "it runs");
        CHECK(maudStopStream(context, stream) == maud_success, "stop");
        Sleep(100);
        CHECK(maudStartStream(context, stream) == maud_success, "start again");
    }
    CHECK(WaitForBlocks(context, &calm, atomic_load(&calm.count) + 40), "it runs on");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
    if (status.underruns > 5)
    {
        fprintf(stderr, "a calm stream counted %llu underruns\n",
                (unsigned long long)status.underruns);
    }
    CHECK(status.underruns <= 5, "a calm stream counts next to none");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy calm");
}

// Destroying a stream whose callback is running on the data thread
// succeeds, and returns only once that callback has: none comes after.
static void TestDestroyWhileRendering(maudContext* context)
{
    for (int duplex = 0; duplex < 2; ++duplex)
    {
        Blocks blocks = {.stallAt = 10};
        maudDirection direction = duplex ? maud_directionDuplex : maud_directionOutput;
        maudStreamId stream = OpenStream(context, direction, (maudDeviceId){0, 0}, &blocks);
        for (int tries = 0; tries < 300 && !atomic_load(&blocks.stalling); ++tries)
        {
            maudNotification ignored;
            while (maudNextNotification(context, &ignored) == maud_success)
            {
            }
            Sleep(1);
        }
        CHECK(atomic_load(&blocks.stalling), "its callback is running");
        CHECK(maudDestroyStream(context, stream) == maud_success, "destroyed meanwhile");
        uint32_t count = atomic_load(&blocks.count);
        CHECK(!atomic_load(&blocks.stalling) && count == blocks.stallAt + 1,
              "after the callback returned");
        Sleep(100);
        CHECK(atomic_load(&blocks.count) == count, "and no callback came after");
    }
}

// A duplex stream on the default sink and source, which are two drivers
// here: both buffers in each callback at the graph's rate, its halves
// grouped under one driver, so the status declares one clock.
static void TestDuplexStream(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamId stream = OpenStream(context, maud_directionDuplex, (maudDeviceId){0, 0}, &blocks);
    CHECK(WaitForBlocks(context, &blocks, 40), "duplex callbacks");
    CHECK(atomic_load(&blocks.withInput) == atomic_load(&blocks.count), "each with input");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "every duplex block is one period");
    CHECK(Near(MeasureRate(context, stream), 48000.0), "at the graph's rate");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
    CHECK(status.drift == maud_driftNone, "no drift on one graph");
    uint32_t nodes = 0;
    uint32_t drivers = 0;
    DriversOfOurNodes(&nodes, &drivers);
    CHECK(nodes == 2 && drivers == 1, "both halves follow one driver");
    uint64_t before = status.slippedFrames;
    Sleep(2000);
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
    if (status.slippedFrames - before > 512)
    {
        fprintf(stderr, "slipped %llu frames in 2 s\n",
                (unsigned long long)(status.slippedFrames - before));
    }
    CHECK(status.slippedFrames - before <= 512, "and little slips on one graph");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy duplex");
    // A stall in the duplex callback skips both halves' cycles: the
    // output's underruns and the input's overruns.
    Blocks stalled = {.stallAt = 30};
    stream = OpenStream(context, maud_directionDuplex, (maudDeviceId){0, 0}, &stalled);
    CHECK(WaitForBlocks(context, &stalled, 80), "past the duplex stall");
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success && status.underruns >= 1 &&
              status.overruns >= 1,
          "both halves count the stall");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy the stalled duplex");
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
    CHECK(WaitForLinkIntoPlugged(context, helper), "the pinned stream is linked to its device");
    SetDefaultKey(helper, "default.configured.audio.sink", "maud-test-hotplug");
    maudNotification record;
    CHECK(WaitForStream(context, maud_notifyStreamMoved, follower, &record), "the follower moves");
    CHECK(Near(MeasureRate(context, follower), 48000.0), "at the graph's rate still");
    ForceGraphRate(helper, "44100");
    CHECK(WaitForStream(context, maud_notifyStreamFormatChanged, follower, &record),
          "the graph's new rate");
    CHECK(record.sampleRate == 44100, "44.1 kHz");
    uint32_t before = atomic_load(&following.count);
    CHECK(WaitForBlocks(context, &following, before + 20), "the follower runs on");
    CHECK(atomic_load(&following.lastRate) == 44100, "its blocks carry the new rate");
    CHECK(Near(MeasureRate(context, follower), 44100.0), "and it runs at it");
    ForceGraphRate(helper, "0");
    CHECK(WaitForStream(context, maud_notifyStreamFormatChanged, follower, &record), "and back");
    CHECK(record.sampleRate == 48000, "48 kHz");
    SetDefaultKey(helper, "default.configured.audio.sink", "maud-test-sink");
    UnplugSink(helper);
    CHECK(WaitForStream(context, maud_notifyStreamSuspended, pinned, &record), "pinned lost");
    CHECK(record.reason == maud_suspendDeviceLost, "its device is gone");
    CHECK(maudDestroyStream(context, pinned) == maud_success, "destroy pinned");
    CHECK(maudDestroyStream(context, follower) == maud_success, "destroy follower");
    ResetDefaults(context, helper);
}

// PipeWire has no exclusive mode for a client: a stream asking for it on
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
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    maudResult result = maudCreateContext(&def, &context);
    // Without a PipeWire daemon the native context picks the next
    // backend, or none: the test then has nothing to test.
    bool pipewire =
        result == maud_success && maudGetContextBackend(context) == maud_backendPipewire;
    if (!pipewire && getenv("MAUD_REQUIRE_PIPEWIRE") == nullptr)
    {
        if (result == maud_success)
        {
            CHECK(maudDestroyContext(context) == maud_success, "destroy the other backend");
        }
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
    TestDuplexStream(context);
    TestXruns(context);
    TestExclusiveRefused(context);
    TestDestroyWhileRendering(context);
    TestStreamsMoveAndAreLost(context, &helper);
    StopHelper(&helper);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
