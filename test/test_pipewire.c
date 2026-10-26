// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PipeWire backend against a running daemon: its devices, a device
// plugged in and out, and a default changed, with a second PipeWire
// client of the test's own doing the plugging, a context of one device
// and one whose allocator refuses. Without a daemon the
// test is skipped, unless MAUD_REQUIRE_PIPEWIRE is set.

#include "test_clock.h"
#include "test_daemon.h"
#include "test_harness.h"
#include "test_names.h"
#include "test_wait.h"

#include "maul-audio/notification.h"

#include <math.h>
#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    struct pw_proxy* surround;
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

static void Drain(maudContext* context)
{
    maudNotification ignored;
    while (maudNextNotification(context, &ignored) == maud_success)
    {
    }
}

// Whether the helper's loop thread has bound the metadata, read under
// the loop's lock.
static bool HelperReady(Helper* helper)
{
    pw_thread_loop_lock(helper->loop);
    bool ready = helper->metadata != nullptr && helper->settings != nullptr;
    pw_thread_loop_unlock(helper->loop);
    return ready;
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
    // The loop thread binds the metadata when its global arrives.
    if (helper->core == nullptr)
    {
        return false;
    }
    POLL_UNTIL(3000, "the helper's metadata", (void)0, HelperReady(helper));
    return HelperReady(helper);
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

// Plugs in a null sink named maud-test-surround whose properties give
// its six channels' positions but not their count, which only its
// formats say.
static void PlugSurround(Helper* helper)
{
    pw_thread_loop_lock(helper->loop);
    struct pw_properties* props = pw_properties_new(
        "factory.name", "support.null-audio-sink", "node.name", "maud-test-surround", "media.class",
        "Audio/Sink", "audio.position", "[FL FR FC LFE RL RR]", nullptr);
    helper->surround = pw_core_create_object(helper->core, "adapter", PW_TYPE_INTERFACE_Node,
                                             PW_VERSION_NODE, &props->dict, 0);
    pw_properties_free(props);
    pw_thread_loop_unlock(helper->loop);
}

static void UnplugSurround(Helper* helper)
{
    pw_thread_loop_lock(helper->loop);
    pw_proxy_destroy(helper->surround);
    helper->surround = nullptr;
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

// The longest a wait for the daemon or the session manager takes, in
// milliseconds (SESSION_MANAGER_MS, test_daemon.h). A wait ends as soon as
// the answer comes.
#define NOTICE_MS SESSION_MANAGER_MS

// Drains notifications until one of kind arrives whose device has key,
// or whose device is that one. Whether one did.
static bool DrainTo(maudContext* context, maudNotificationKind kind, const char* key,
                    maudDeviceId* deviceOut)
{
    maudNotification record;
    while (maudNextNotification(context, &record) == maud_success)
    {
        bool matches = key == nullptr ? record.device.index1 == deviceOut->index1 &&
                                            record.device.generation == deviceOut->generation
                                      : KeyIs(context, record.device, key);
        if (record.kind == kind && matches)
        {
            *deviceOut = record.device;
            return true;
        }
    }
    return false;
}

// Drains notifications for up to NOTICE_MS until one of kind arrives
// whose device has key, or whose device is that one.
static bool WaitFor(maudContext* context, maudNotificationKind kind, const char* key,
                    maudDeviceId* deviceOut)
{
    bool arrived = false;
    POLL_UNTIL(NOTICE_MS, key != nullptr ? key : "a notification about the device",
               arrived = DrainTo(context, kind, key, deviceOut), arrived);
    return arrived;
}

// Set once a default the daemon names has not reached the context in a
// full wait: every later wait for a default fails at once, since the
// first one printed why and waiting again tells nothing more.
static bool s_defaultsLost;

// Prints what the context and the daemon say the output devices and the
// defaults are, when a default never arrives.
static void PrintDefaults(const maudContext* context)
{
    maudDeviceId ids[32];
    uint32_t count = 0;
    maudDeviceId general = {0, 0};
    (void)maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &general);
    if (maudGetDevices(context, maud_directionOutput, ids, 32, &count) == maud_success)
    {
        for (uint32_t i = 0; i < count && i < 32; ++i)
        {
            char key[64];
            size_t length = 0;
            if (maudGetDeviceKey(context, ids[i], key, sizeof(key), &length) != maud_success)
            {
                length = 0;
            }
            printf("the context's output %u: %.*s%s\n", ids[i].index1, (int)length, key,
                   ids[i].index1 == general.index1 ? " (its default)" : "");
        }
    }
    fflush(stdout);
    int status =
        system("echo \"the daemon's test nodes:\"; pw-cli ls Node | grep 'node.name = .maud';"
               " echo \"the daemon's default metadata:\"; pw-metadata -n default 0");
    if (status != 0)
    {
        printf("the daemon's view could not be read (status %d)\n", status);
    }
}

// Whether device is the default output for both roles.
static bool IsDefault(const maudContext* context, maudDeviceId device)
{
    maudDeviceId general = {0, 0};
    maudDeviceId communications = {0, 0};
    return maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &general) ==
               maud_success &&
           maudGetDefaultDevice(context, maud_directionOutput, maud_roleCommunications,
                                &communications) == maud_success &&
           general.index1 == device.index1 && general.generation == device.generation &&
           communications.index1 == device.index1;
}

// Drains notifications for up to milliseconds until device is the
// default output for both roles.
static bool WaitForDefaultWithin(maudContext* context, maudDeviceId device, int milliseconds)
{
    if (s_defaultsLost)
    {
        return false;
    }
    POLL_UNTIL(milliseconds, "the default output", Drain(context), IsDefault(context, device));
    return IsDefault(context, device);
}

static bool WaitForDefault(maudContext* context, maudDeviceId device)
{
    if (s_defaultsLost || WaitForDefaultWithin(context, device, NOTICE_MS))
    {
        return !s_defaultsLost;
    }
    printf("the default never reached the context in %d s\n", NOTICE_MS / 1000);
    PrintDefaults(context);
    s_defaultsLost = true;
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
    char longName[512];
    CHECK(maudGetDeviceName(context, source, longName, sizeof(longName), &length) == maud_success &&
              IsCutSourceName(longName, length),
          "a description past the limit is cut after a whole character");
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

// Writes the effective default output, again each second for thirty
// seconds, until the context follows it.
static bool SetEffectiveDefault(maudContext* context, Helper* helper, const char* nodeName,
                                maudDeviceId device)
{
    // hygiene: writes the default again, which the session manager may override
    for (int write = 0; write < 30; ++write)
    {
        SetDefaultKey(helper, "default.audio.sink", nodeName);
        if (WaitForDefaultWithin(context, device, 1000))
        {
            return true;
        }
    }
    return false;
}

// A node whose properties lack its channel count: its layout comes from
// its formats, which arrive after it is listed and are reported.
static void TestLayoutFromFormats(maudContext* context, Helper* helper)
{
    maudDeviceId plugged = {0, 0};
    PlugSurround(helper);
    CHECK(WaitFor(context, maud_notifyDeviceAdded, "maud-test-surround", &plugged), "plugged in");
    maudDeviceId changed = plugged;
    CHECK(WaitFor(context, maud_notifyDeviceFormatChanged, nullptr, &changed),
          "its format reported");
    maudDeviceInfo info = {0};
    CHECK(maudGetDeviceInfo(context, plugged, &info) == maud_success &&
              info.nativeLayout == maud_layout5Point1,
          "5.1 from its formats");
    UnplugSurround(helper);
    maudDeviceId removed = plugged;
    CHECK(WaitFor(context, maud_notifyDeviceRemoved, nullptr, &removed), "unplugged");
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
    // write stays until the session manager's own choice changes, which
    // it may still be making for the sink just plugged in: the write is
    // made again until it holds.
    CHECK(SetEffectiveDefault(context, helper, "maud-test-hotplug", plugged), "effective default");
    maudDeviceId sink = {0, 0};
    CHECK(FindByKey(context, maud_directionOutput, "maud-test-sink", &sink), "the test sink");
    CHECK(SetEffectiveDefault(context, helper, "maud-test-sink", sink), "effective default back");
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

// The input blocks a trace keeps: 256 periods of 256 frames at 48 kHz,
// 1.4 s.
#define TRACE_BLOCKS 256
// The frames at a block's end a trace measures apart: a 1 kHz tone at
// 48 kHz is never silent over 8 of them.
#define TAIL_FRAMES 8

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
    // Whether an output plays a 1 kHz tone at 0.5, and the loudest input
    // sample since peak was last cleared, in thousandths.
    bool tone;
    float phase;
    _Atomic(uint32_t) peak;
    // While trace is set, each input block's host time, its loudest
    // sample and the loudest over its last TAIL_FRAMES frames, in
    // thousandths, the first TRACE_BLOCKS of them.
    atomic_bool trace;
    _Atomic(uint32_t) traced;
    int64_t traceNs[TRACE_BLOCKS];
    uint32_t tracePeak[TRACE_BLOCKS];
    uint32_t traceTail[TRACE_BLOCKS];
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
        uint32_t channels = maudGetLayoutChannelCount(block->layout);
        uint32_t loudest = 0;
        uint32_t tail = 0;
        uint32_t tailFrom = block->frameCount > TAIL_FRAMES ? block->frameCount - TAIL_FRAMES : 0;
        for (uint32_t i = 0; i < block->frameCount * channels; ++i)
        {
            float level = fabsf(block->input[i]) * 1000.0f;
            loudest = level > (float)loudest ? (uint32_t)level : loudest;
            if (i / channels >= tailFrom)
            {
                tail = level > (float)tail ? (uint32_t)level : tail;
            }
        }
        uint32_t peak = atomic_load(&blocks->peak);
        atomic_store(&blocks->peak, loudest > peak ? loudest : peak);
        uint32_t traced = atomic_load(&blocks->traced);
        if (atomic_load(&blocks->trace) && traced < TRACE_BLOCKS)
        {
            blocks->traceNs[traced] = maudGetHostNanoseconds();
            blocks->tracePeak[traced] = loudest;
            blocks->traceTail[traced] = tail;
            atomic_store(&blocks->traced, traced + 1);
        }
    }
    if (blocks->tone && block->output != nullptr)
    {
        uint32_t channels = maudGetLayoutChannelCount(block->layout);
        for (uint32_t i = 0; i < block->frameCount; ++i)
        {
            float sample = 0.5f * sinf(blocks->phase);
            blocks->phase =
                fmodf(blocks->phase + 6.2831853f * 1000.0f / block->sampleRate, 6.2831853f);
            for (uint32_t c = 0; c < channels; ++c)
            {
                block->output[i * channels + c] = sample;
            }
        }
    }
    atomic_store(&blocks->lastRate, block->sampleRate);
    if (blocks->tryControl && atomic_load(&blocks->count) == 2)
    {
        atomic_store(&blocks->controlResult, maudStopStream(blocks->context, blocks->stream));
    }
    if (blocks->stallAt != 0 && atomic_load(&blocks->count) == blocks->stallAt)
    {
        atomic_store(&blocks->stalling, true);
        // hygiene: stalls the callback 300 ms so the test can act during it
        Pause(300);
        atomic_store(&blocks->stalling, false);
    }
    atomic_fetch_add(&blocks->count, 1);
}

// Drains notifications, which runs PipeWire's main loop, for up to
// three seconds until the callback has run count times.
static bool WaitForBlocks(maudContext* context, Blocks* blocks, uint32_t count)
{
    POLL_UNTIL(3000, "the stream's blocks", Drain(context), atomic_load(&blocks->count) >= count);
    return atomic_load(&blocks->count) >= count;
}

// Whether a link into the plugged sink appeared within three seconds.
// Whether a link into the plugged sink is listed, read under the
// helper's loop lock.
static bool LinkedIntoPlugged(Helper* helper)
{
    pw_thread_loop_lock(helper->loop);
    bool linked = false;
    for (uint32_t i = 0; i < helper->linkCount && helper->pluggedId != 0; ++i)
    {
        linked = linked || helper->linkInputs[i] == helper->pluggedId;
    }
    pw_thread_loop_unlock(helper->loop);
    return linked;
}

static bool WaitForLinkIntoPlugged(maudContext* context, Helper* helper)
{
    POLL_UNTIL(3000, "a link into the plugged sink", Drain(context), LinkedIntoPlugged(helper));
    return LinkedIntoPlugged(helper);
}

// The stream's rate by its own clock (test_clock.h), in windows of a
// second judged by the graph's and the stream's xruns and the machine's
// promptness (test_daemon.h), the context drained meanwhile.
static ClockRate MeasureRate(maudContext* context, maudStreamId stream)
{
    GraphWatch watch = {.context = context, .stream = stream, .milliseconds = 1000};
    return StreamClockRate(context, stream, GraphWindow, &watch);
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

// Ends the newest output stream at the graph, as a policy or an
// administrator can, by destroying its node (found through the daemon's
// PulseAudio server, which lists every output stream).
static bool EndNewestOutputStream(void)
{
    return system("pw-cli destroy \"$(pactl list sink-inputs | "
                  "awk -F'\\\"' '/object.id/{id=$2} END{print id}')\" >/dev/null 2>&1") == 0;
}

// The graph ends a stream: a playing one is built again by the drains
// and plays on, and so is a stopped one, which then starts.
// Drains notifications until one of kind about a stream arrives, for a
// suspension one for reason, stopping at it. Whether one did.
static bool DrainToStream(maudContext* context, maudNotificationKind kind, maudSuspendReason reason)
{
    maudNotification record;
    while (maudNextNotification(context, &record) == maud_success)
    {
        if (record.kind == kind && (kind != maud_notifyStreamSuspended || record.reason == reason))
        {
            return true;
        }
    }
    return false;
}

static void TestGraphEnded(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamId stream = OpenStream(context, maud_directionOutput, (maudDeviceId){0, 0}, &blocks);
    CHECK(WaitForBlocks(context, &blocks, 20), "it plays");
    CHECK(EndNewestOutputStream(), "the graph ends it");
    // hygiene: lets the graph's end reach the stream before its count is taken
    Pause(200);
    uint32_t ended = atomic_load(&blocks.count);
    CHECK(WaitForBlocks(context, &blocks, ended + 20), "it plays on, built again");
    Blocks idle = {0};
    maudStreamId stopped = OpenStream(context, maud_directionOutput, (maudDeviceId){0, 0}, &idle);
    CHECK(WaitForBlocks(context, &idle, 5) && maudStopStream(context, stopped) == maud_success,
          "a second plays and stops");
    CHECK(EndNewestOutputStream(), "the graph ends the stopped one");
    // hygiene: lets the graph's end reach the stopped stream before its count is taken
    Pause(200);
    uint32_t before = atomic_load(&idle.count);
    CHECK(maudStartStream(context, stopped) == maud_success &&
              WaitForBlocks(context, &idle, before + 20),
          "it starts, built again");
    CHECK(maudDestroyStream(context, stopped) == maud_success, "destroy it");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// Sends a signal to the test daemon's PipeWire.
static bool SignalDaemon(const char* signal)
{
    char more[64];
    snprintf(more, sizeof(more), "pipewire %s", signal);
    return DriveDaemon("signal", more);
}

// A rebuild that fails: the graph ends a playing stream, then stops
// answering. The stream is suspended for the platform once its rebuild
// is past the deadline, and resumes, playing, once the graph answers.
static void TestRebuildFails(maudContext* context)
{
    if (!HaveDaemon())
    {
        return;
    }
    Blocks blocks = {0};
    maudStreamId stream = OpenStream(context, maud_directionOutput, (maudDeviceId){0, 0}, &blocks);
    CHECK(WaitForBlocks(context, &blocks, 20), "it plays");
    CHECK(EndNewestOutputStream(), "the graph ends it");
    // Undrained, so the end is queued for the drain that rebuilds it,
    // which the stopped daemon leaves waiting.
    // hygiene: lets the end queue for the drain that rebuilds the stream
    Pause(300);
    CHECK(SignalDaemon("STOP"), "the daemon stops answering");
    bool suspended = false;
    POLL_UNTIL(4000, "the stream's suspension for the platform",
               suspended = DrainToStream(context, maud_notifyStreamSuspended, maud_suspendPlatform),
               suspended);
    CHECK(SignalDaemon("CONT"), "the daemon answers again");
    CHECK(suspended, "suspended for the platform while the rebuild fails");
    bool resumed = false;
    POLL_UNTIL(NOTICE_MS, "the stream's resumption",
               resumed = DrainToStream(context, maud_notifyStreamResumed, maud_suspendNone),
               resumed);
    CHECK(resumed, "resumed once it works");
    uint32_t since = atomic_load(&blocks.count);
    CHECK(WaitForBlocks(context, &blocks, since + 20), "and plays");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// PipeWire cannot say whether it spatializes a stream: a stream marked
// contentSpatialized reports the mark's fate as unknown, an unmarked one
// as no mark.
static void TestMark(maudContext* context)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.contentSpatialized = true;
    maudStreamId stream = {0, 0};
    maudStreamStatus status = {0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudGetStreamStatus(context, stream, &status) == maud_success &&
              status.spatialMark == maud_markUnknown,
          "a marked stream's fate is unknown");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy it");
    def.contentSpatialized = false;
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudGetStreamStatus(context, stream, &status) == maud_success &&
              status.spatialMark == maud_markNone &&
              maudDestroyStream(context, stream) == maud_success,
          "an unmarked one has no mark");
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
    CHECK(RateHolds(MeasureRate(context, stream), 48000.0),
          "the clock advances at the stream's rate");
    CHECK(StreamClockIsSound(context, stream, true, false, Pause),
          "its clock maps frames to host time");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    WaitForBlocks(context, &blocks, UINT32_MAX / 2);
    uint32_t stopped = atomic_load(&blocks.count);
    // hygiene: gives a callback after the stop 100 ms to show
    Pause(100);
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
    // PipeWire has no object renderer: the host's own binaural mix goes
    // to an ordinary stream.
    def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.user = &blocks;
    def.objectCount = 4;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no object streams");
}

static void TestInputStream(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamId stream = OpenStream(context, maud_directionInput, (maudDeviceId){0, 0}, &blocks);
    CHECK(WaitForBlocks(context, &blocks, 10), "capture callbacks");
    CHECK(atomic_load(&blocks.withInput) == atomic_load(&blocks.count), "blocks hold input");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "every capture block is one period");
    CHECK(StreamClockIsSound(context, stream, false, true, Pause),
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
    // The gap of a stop is not an xrun: across each of ten restarts the
    // count stays where it was. A loaded machine may slip a real xrun
    // into the few blocks after a restart now and then (at a load of 230,
    // at more than three of ten); a counted gap would show at every one.
    Blocks calm = {0};
    maudStreamId stream = OpenStream(context, maud_directionOutput, (maudDeviceId){0, 0}, &calm);
    uint32_t jumped = 0;
    maudStreamStatus status = {0};
    for (int restart = 0; restart < 10; ++restart)
    {
        CHECK(WaitForBlocks(context, &calm, atomic_load(&calm.count) + 20), "it runs");
        CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
        uint64_t before = status.underruns;
        CHECK(maudStopStream(context, stream) == maud_success, "stop");
        // hygiene: a stop's gap of 100 ms, which the underrun count must not take
        Pause(100);
        CHECK(maudStartStream(context, stream) == maud_success, "start again");
        CHECK(WaitForBlocks(context, &calm, atomic_load(&calm.count) + 5), "it runs again");
        CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
        jumped += status.underruns != before ? 1u : 0u;
    }
    if (jumped > 6)
    {
        fprintf(stderr, "the count jumped across %u of 10 restarts\n", jumped);
    }
    CHECK(jumped <= 6, "a stop's gap is not counted");
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
        POLL_UNTIL(300, "the callback's stall", Drain(context), atomic_load(&blocks.stalling));
        CHECK(atomic_load(&blocks.stalling), "its callback is running");
        CHECK(maudDestroyStream(context, stream) == maud_success, "destroyed meanwhile");
        uint32_t count = atomic_load(&blocks.count);
        CHECK(!atomic_load(&blocks.stalling) && count == blocks.stallAt + 1,
              "after the callback returned");
        // hygiene: gives a callback after the destroy 100 ms to show
        Pause(100);
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
    CHECK(RateHolds(MeasureRate(context, stream), 48000.0), "at the graph's rate");
    maudStreamStatus status = {0};
    CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
    CHECK(status.drift == maud_driftNone, "no drift on one graph");
    uint32_t nodes = 0;
    uint32_t drivers = 0;
    DriversOfOurNodes(&nodes, &drivers);
    CHECK(nodes == 2 && drivers == 1, "both halves follow one driver");
    // The median slips of the first three 2 s windows in which no node of
    // the graph counted an xrun, the stream none and the test's thread woke
    // on time, out of 30 at most. A loaded machine stalls the graph, which
    // then slips the halves and counts the stall: those windows measure the
    // machine. Slips in a clean window are the library's, which the
    // referee does not see, so a fixed count of clean windows is judged,
    // as many as for a rate; fewer fail as starved.
    GraphWatch watch = {.context = context, .stream = stream, .milliseconds = 2000};
    (void)GraphWindow(&watch);
    uint64_t slips[RATE_CLEAN_WINDOWS] = {0};
    int clean = 0;
    int windows = 0;
    while (windows < RATE_MAX_WINDOWS && clean < RATE_CLEAN_WINDOWS)
    {
        uint64_t before = status.slippedFrames;
        bool kept = GraphWindow(&watch);
        CHECK(maudGetStreamStatus(context, stream, &status) == maud_success, "status");
        windows += 1;
        if (kept)
        {
            slips[clean++] = status.slippedFrames - before;
        }
    }
    for (int i = 1; i < clean; ++i)
    {
        for (int j = i; j > 0 && slips[j] < slips[j - 1]; --j)
        {
            uint64_t swap = slips[j];
            slips[j] = slips[j - 1];
            slips[j - 1] = swap;
        }
    }
    uint64_t median = slips[RATE_CLEAN_WINDOWS / 2];
    if (clean < RATE_CLEAN_WINDOWS)
    {
        fprintf(stderr, "starved: %d of %d windows had xruns or a late wakeup\n", windows - clean,
                windows);
    }
    else if (median > 512)
    {
        fprintf(stderr, "slipped %llu, %llu and %llu frames in three clean 2 s windows\n",
                (unsigned long long)slips[0], (unsigned long long)slips[1],
                (unsigned long long)slips[2]);
    }
    CHECK(clean == RATE_CLEAN_WINDOWS && median <= 512, "and little slips on one graph");
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

// Drains notifications until one of kind for stream arrives, stopping
// at it. Whether one did.
static bool DrainToRecord(maudContext* context, maudNotificationKind kind, maudStreamId stream,
                          maudNotification* recordOut)
{
    while (maudNextNotification(context, recordOut) == maud_success)
    {
        if (recordOut->kind == kind && recordOut->stream.index1 == stream.index1 &&
            recordOut->stream.generation == stream.generation)
        {
            return true;
        }
    }
    return false;
}

// Drains notifications for up to three seconds until one of kind for
// stream arrives.
static bool WaitForStream(maudContext* context, maudNotificationKind kind, maudStreamId stream,
                          maudNotification* recordOut)
{
    bool arrived = false;
    POLL_UNTIL(3000, "a notification about the stream",
               arrived = DrainToRecord(context, kind, stream, recordOut), arrived);
    return arrived;
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
    CHECK(RateHolds(MeasureRate(context, follower), 48000.0), "at the graph's rate still");
    ForceGraphRate(helper, "44100");
    CHECK(WaitForStream(context, maud_notifyStreamFormatChanged, follower, &record),
          "the graph's new rate");
    CHECK(record.sampleRate == 44100, "44.1 kHz");
    uint32_t before = atomic_load(&following.count);
    CHECK(WaitForBlocks(context, &following, before + 20), "the follower runs on");
    CHECK(atomic_load(&following.lastRate) == 44100, "its blocks carry the new rate");
    CHECK(RateHolds(MeasureRate(context, follower), 44100.0), "and it runs at it");
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

// Drains notifications, which runs PipeWire's main loop, until the
// loudest input since the peak was cleared reaches thousandths, for up
// to two seconds; or for half a second, whether it stays below it.
static bool Hears(maudContext* context, const Blocks* blocks, uint32_t thousandths)
{
    POLL_UNTIL(2000, "the input to reach its level", Drain(context),
               atomic_load(&blocks->peak) >= thousandths);
    return atomic_load(&blocks->peak) >= thousandths;
}

static bool StaysBelow(maudContext* context, const Blocks* blocks, uint32_t thousandths)
{
    STEP_FOR(500, Drain(context));
    return atomic_load(&blocks->peak) < thousandths;
}

// The traced blocks that join a sink's tone and another's silence: loud,
// and silent over their last TAIL_FRAMES frames, which no block of the
// tone alone is. A block gathered across a loopback's move would be.
static uint32_t JoinedBlocks(Blocks* blocks)
{
    atomic_store(&blocks->trace, false);
    uint32_t traced = atomic_load(&blocks->traced);
    uint32_t joined = 0;
    for (uint32_t i = 0; i < traced; ++i)
    {
        joined += blocks->tracePeak[i] >= 10 && blocks->traceTail[i] < 10 ? 1 : 0;
    }
    return joined;
}

// Prints a traced loopback's blocks since the default was set: how many
// came, and each that carried sound, in milliseconds since the set and
// since the stream reported its move; then where the server has its
// streams.
static void PrintTrace(const Blocks* blocks, int64_t setAt, int64_t movedAt)
{
    uint32_t traced = atomic_load(&blocks->traced);
    printf("move: reported %.1f ms after the set; %u blocks traced\n",
           (double)(movedAt - setAt) / 1e6, traced);
    for (uint32_t i = 0; i < traced; ++i)
    {
        if (blocks->tracePeak[i] >= 10)
        {
            printf("  block %u at %.1f ms (%.1f after the move): peak %u, last frames %u\n", i,
                   (double)(blocks->traceNs[i] - setAt) / 1e6,
                   (double)(blocks->traceNs[i] - movedAt) / 1e6, blocks->tracePeak[i],
                   blocks->traceTail[i]);
        }
    }
    // The fixture's own server, never the session's.
    (void)!system("echo 'the server now:'; pactl list sink-inputs short;"
                  "pactl list source-outputs short; pw-link -l");
}

// Whether this is the quarantined run, pipewire_move ("test_pipewire
// move"): the loopback's move alone, with its check of the silent sink.
static bool s_moveOnly;

// Whether this is the quarantined run, pipewire_defaults ("test_pipewire
// defaults"): the tests that change the default, whose context stopped
// following it once on a runner (#81).
static bool s_defaultsOnly;

// Forces the fixture's graph to a quantum, or lets it choose again (0).
static bool ForceQuantum(uint32_t frames)
{
    char command[128];
    snprintf(command, sizeof(command),
             "pw-metadata -n settings 0 clock.force-quantum %u >/dev/null", frames);
    return system(command) == 0;
}

// A loopback stream captures what a sink plays, from its monitor:
// silence while nothing plays there, the tone a stream plays once one
// does. One on the default follows the default sink away to a silent
// sink and back, and hears only the silent sink there: no block joins
// the frames it gathered on the old sink to the new one's. The graph
// runs a quantum of 1000 frames, no whole number of the stream's
// periods, so the stream has frames gathered toward a block whenever it
// moves (#50).
static void TestLoopback(maudContext* context, Helper* helper)
{
    ResetDefaults(context, helper);
    CHECK(ForceQuantum(1000), "the graph at a quantum of 1000 frames");
    maudDeviceId home = {0, 0};
    CHECK(FindByKey(context, maud_directionOutput, "maud-test-sink", &home), "the test sink");
    Blocks heard = {0};
    maudStreamId loop = OpenStream(context, maud_directionLoopback, home, &heard);
    CHECK(WaitForBlocks(context, &heard, 10), "a loopback on the test sink runs");
    CHECK(atomic_load(&heard.withInput) == atomic_load(&heard.count) &&
              atomic_load(&heard.wrongSize) == 0,
          "each block with input, one period");
    CHECK(StaysBelow(context, &heard, 10), "silence while nothing plays");
    Blocks played = {.tone = true};
    maudStreamId tone = OpenStream(context, maud_directionOutput, home, &played);
    CHECK(Hears(context, &heard, 400) && atomic_load(&heard.peak) <= 600, "the tone it plays");
    CHECK(maudDestroyStream(context, loop) == maud_success, "destroy it");
    Blocks following = {0};
    loop = OpenStream(context, maud_directionLoopback, (maudDeviceId){0, 0}, &following);
    CHECK(Hears(context, &following, 400), "one on the default hears the tone");
    PlugSink(helper);
    maudDeviceId plugged = {0, 0};
    CHECK(WaitFor(context, maud_notifyDeviceAdded, "maud-test-hotplug", &plugged), "plugged in");
    atomic_store(&following.trace, true);
    int64_t setAt = maudGetHostNanoseconds();
    SetDefaultKey(helper, "default.configured.audio.sink", "maud-test-hotplug");
    maudNotification record;
    CHECK(WaitForStream(context, maud_notifyStreamMoved, loop, &record) &&
              KeyIs(context, record.device, "maud-test-hotplug"),
          "it moves with the default");
    int64_t movedAt = maudGetHostNanoseconds();
    StaysBelow(context, &following, UINT32_MAX);
    atomic_store(&following.peak, 0);
    // Heard the old sink now and then on a loaded runner (#50): judged
    // only in the quarantined run, pipewire_move, until 14 nights in a
    // row pass after the fix.
    bool silent = !s_moveOnly || StaysBelow(context, &following, 10);
    CHECK(silent, "and hears the silent sink");
    uint32_t joined = JoinedBlocks(&following);
    CHECK(joined == 0, "no block joins the old sink's tone to the new sink's silence");
    if (!silent || joined != 0)
    {
        PrintTrace(&following, setAt, movedAt);
    }
    SetDefaultKey(helper, "default.configured.audio.sink", "maud-test-sink");
    CHECK(WaitForStream(context, maud_notifyStreamMoved, loop, &record) &&
              KeyIs(context, record.device, "maud-test-sink"),
          "back with the default");
    atomic_store(&following.peak, 0);
    CHECK(Hears(context, &following, 400), "and hears the tone again");
    CHECK(maudDestroyStream(context, loop) == maud_success &&
              maudDestroyStream(context, tone) == maud_success,
          "destroy both");
    Blocks pinnedHeard = {0};
    maudStreamId pinned = OpenStream(context, maud_directionLoopback, plugged, &pinnedHeard);
    CHECK(WaitForBlocks(context, &pinnedHeard, 10), "one pinned to the plugged sink runs");
    UnplugSink(helper);
    CHECK(WaitForStream(context, maud_notifyStreamSuspended, pinned, &record) &&
              record.reason == maud_suspendDeviceLost,
          "it is lost with its sink");
    CHECK(maudDestroyStream(context, pinned) == maud_success, "destroy it");
    ResetDefaults(context, helper);
    CHECK(ForceQuantum(0), "the graph's quantum its own again");
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionLoopback;
    def.callback = CountBlocks;
    def.loopbackExcludesSelf = true;
    CHECK(maudCreateStream(context, &def, &loop) == maud_errorUnsupported,
          "PipeWire cannot leave the host out");
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

// An allocator that refuses its call numbered failAt, counting from 1,
// and counts the blocks still held.
typedef struct Refusing
{
    int calls;
    int failAt;
    int live;
} Refusing;

static void* RefusingAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    Refusing* refusing = context;
    if (++refusing->calls == refusing->failAt)
    {
        return nullptr;
    }
    refusing->live++;
    return malloc(size);
}

static void RefusingFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    Refusing* refusing = context;
    refusing->live--;
    free(memory);
}

// A context of one device keeps one of the daemon's nodes and drops the
// rest; a context whose allocator refuses any of its creation's blocks
// is refused for capacity and holds nothing.
static void TestLimits(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendPipewire;
    def.limits.devices = 1;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "a context of one device");
    maudDeviceId ids[4];
    uint32_t outputs = 0;
    uint32_t inputs = 0;
    CHECK(maudGetDevices(context, maud_directionOutput, ids, 4, &outputs) == maud_success &&
              maudGetDevices(context, maud_directionInput, ids, 4, &inputs) == maud_success &&
              outputs + inputs == 1,
          "one device listed");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    def = maudDefaultContextDef();
    def.backend = maud_backendPipewire;
    bool created = false;
    for (int failAt = 1; failAt < 64 && !created; ++failAt)
    {
        Refusing refusing = {.failAt = failAt};
        def.allocator = (maudAllocator){RefusingAlloc, RefusingFree, &refusing};
        context = nullptr;
        maudResult result = maudCreateContext(&def, &context);
        created = result == maud_success;
        if (created)
        {
            CHECK(maudDestroyContext(context) == maud_success && refusing.live == 0,
                  "every block back");
        }
        else
        {
            CHECK(result == maud_errorCapacity && context == nullptr && refusing.live == 0,
                  "a refused block is a capacity error, and nothing is held");
        }
    }
    CHECK(created, "created once the allocator gives every block");
}

int main(int argc, char** argv)
{
    s_moveOnly = argc > 1 && strcmp(argv[1], "move") == 0;
    s_defaultsOnly = argc > 1 && strcmp(argv[1], "defaults") == 0;
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
        return PlatformAbsent("no PipeWire daemon");
    }
    CHECK(result == maud_success, "a native context on the daemon");
    if (result != maud_success)
    {
        return 1;
    }
    if (s_moveOnly || s_defaultsOnly)
    {
        Helper helper;
        bool helped = StartHelper(&helper);
        CHECK(helped, "the test's own client, with the session manager's metadata");
        if (helped && s_defaultsOnly)
        {
            TestHotplugAndDefaults(context, &helper);
            TestStreamsMoveAndAreLost(context, &helper);
        }
        if (helped)
        {
            TestLoopback(context, &helper);
        }
        StopHelper(&helper);
        CHECK(maudDestroyContext(context) == maud_success, "destroy");
        return s_failures == 0 ? 0 : 1;
    }
    TestTheDaemonsDevices(context);
    Helper helper;
    // Without a session manager the metadata never comes: the tests that
    // write it fail here rather than crash.
    bool helped = StartHelper(&helper);
    CHECK(helped, "the test's own client, with the session manager's metadata");
    // The tests that change the default run in pipewire_defaults until
    // #81 is fixed.
    if (helped)
    {
        TestLayoutFromFormats(context, &helper);
    }
    TestMark(context);
    TestOutputStream(context);
    TestInputStream(context);
    TestDuplexStream(context);
    TestXruns(context);
    TestExclusiveRefused(context);
    TestDestroyWhileRendering(context);
    TestGraphEnded(context);
    TestRebuildFails(context);
    StopHelper(&helper);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    TestLimits();
    return s_failures == 0 ? 0 : 1;
}
