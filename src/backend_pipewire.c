// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The PipeWire backend's connection and devices. The context owns a
// pw_loop as PipeWire's main loop and iterates it on the host's thread:
// without blocking when the host drains notifications, and up to a
// deadline for the round trips of creation. Registry events become
// device table changes; the default metadata names the defaults.

#include "backend.h"
#include "context.h"
#include "device.h"
#include "pipewire_api.h"

#include <errno.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/raw.h>
#include <spa/param/format.h>
#include <spa/param/param.h>
#include <spa/pod/iter.h>
#include <spa/utils/json.h>
#include <spa/utils/string.h>
#include <string.h>
#include <time.h>

// How long creation waits for PipeWire's answers.
#define ROUNDTRIP_DEADLINE_NS 2000000000ll
// How many loop iterations one pump takes at most.
#define PUMP_ITERATIONS 64
// Bytes of a default device's node name.
#define DEFAULT_NAME_BYTES 256

typedef struct maudPipewire maudPipewire;

// One sink or source node and the proxy that reports its formats.
typedef struct PipewireNode
{
    maudPipewire* owner;
    struct pw_proxy* proxy;
    struct spa_hook listener;
    maudDeviceId device;
    uint32_t globalId;
    bool used;
} PipewireNode;

// The connection: the loop, the core and the registry.
typedef struct PipewireConnection
{
    struct pw_loop* loop;
    struct pw_context* context;
    struct pw_core* core;
    struct spa_hook coreListener;
    struct pw_registry* registry;
    struct spa_hook registryListener;
    int pendingSync;
    bool synced;
    bool lost;
} PipewireConnection;

// The default metadata and the node names it gives per direction.
typedef struct PipewireDefaults
{
    struct pw_proxy* metadata;
    struct spa_hook listener;
    char names[2][DEFAULT_NAME_BYTES];
} PipewireDefaults;

struct maudPipewire
{
    maudPipewireApi api;
    maudContext* context;
    PipewireConnection connection;
    PipewireDefaults defaults;
    PipewireNode* nodes;
    uint32_t nodeCapacity;
    size_t bytes;
};

static int64_t Now(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000000000ll + now.tv_nsec;
}

// The layout with this many channels, or maud_layoutNone.
static maudChannelLayout LayoutForChannels(uint32_t channels)
{
    switch (channels)
    {
    case 1:
        return maud_layoutMono;
    case 2:
        return maud_layoutStereo;
    case 4:
        return maud_layoutQuad;
    case 6:
        return maud_layout5Point1;
    case 8:
        return maud_layout7Point1;
    case 12:
        return maud_layout7Point1Point4;
    default:
        return maud_layoutNone;
    }
}

static uint32_t PropertyNumber(const struct spa_dict* props, const char* key)
{
    const char* text = spa_dict_lookup(props, key);
    uint32_t value = 0;
    return text != nullptr && spa_atou32(text, &value, 10) ? value : 0;
}

// The length of text cut to at most limit bytes without splitting a
// UTF-8 sequence.
static size_t CutUtf8(const char* text, size_t limit)
{
    size_t length = strlen(text);
    if (length <= limit)
    {
        return length;
    }
    while (limit > 0 && ((unsigned char)text[limit] & 0xC0u) == 0x80u)
    {
        limit--;
    }
    return limit;
}

static PipewireNode* FindNode(maudPipewire* pipewire, uint32_t globalId)
{
    for (uint32_t i = 0; i < pipewire->nodeCapacity; ++i)
    {
        if (pipewire->nodes[i].used && pipewire->nodes[i].globalId == globalId)
        {
            return &pipewire->nodes[i];
        }
    }
    return nullptr;
}

// Points both roles' defaults at the devices the metadata names, where
// those exist.
static void ResolveDefaults(maudPipewire* pipewire)
{
    for (uint32_t i = 0; i < pipewire->nodeCapacity; ++i)
    {
        const PipewireNode* node = &pipewire->nodes[i];
        const maudDeviceSlot* slot =
            node->used ? maudFindDevice(pipewire->context, node->device) : nullptr;
        if (slot == nullptr)
        {
            continue;
        }
        const char* wanted = pipewire->defaults.names[slot->info.direction];
        if (strlen(wanted) == slot->key.length &&
            memcmp(wanted, slot->key.bytes, slot->key.length) == 0)
        {
            maudSetDefaultDevice(pipewire->context, maud_roleGeneral, node->device);
            maudSetDefaultDevice(pipewire->context, maud_roleCommunications, node->device);
        }
    }
}

// Reads a rate or a channel count from a format param property: a
// plain value, or a choice whose default is the first value and whose
// other values bound it.
static void ReadChoice(const struct spa_pod* value, uint32_t* defaultOut, uint32_t* minOut,
                       uint32_t* maxOut)
{
    uint32_t count = 0;
    uint32_t choice = 0;
    const struct spa_pod* values = spa_pod_get_values(value, &count, &choice);
    if (values->type != SPA_TYPE_Int || count == 0)
    {
        return;
    }
    const int32_t* numbers = SPA_POD_BODY_CONST(values);
    *defaultOut = (uint32_t)numbers[0];
    *minOut = *defaultOut;
    *maxOut = *defaultOut;
    for (uint32_t i = 1; i < count; ++i)
    {
        uint32_t number = (uint32_t)numbers[i];
        *minOut = number < *minOut ? number : *minOut;
        *maxOut = number > *maxOut ? number : *maxOut;
    }
}

static void OnNodeParam(void* data, int seq, uint32_t id, uint32_t index, uint32_t next,
                        const struct spa_pod* param)
{
    (void)seq;
    (void)index;
    (void)next;
    PipewireNode* node = data;
    maudDeviceSlot* slot = maudFindDevice(node->owner->context, node->device);
    if (id != SPA_PARAM_EnumFormat || param == nullptr || slot == nullptr)
    {
        return;
    }
    const struct spa_pod_prop* rate = spa_pod_find_prop(param, nullptr, SPA_FORMAT_AUDIO_rate);
    if (rate != nullptr)
    {
        ReadChoice(&rate->value, &slot->info.nativeSampleRate, &slot->info.minSampleRate,
                   &slot->info.maxSampleRate);
    }
    const struct spa_pod_prop* channels =
        spa_pod_find_prop(param, nullptr, SPA_FORMAT_AUDIO_channels);
    if (channels != nullptr)
    {
        uint32_t count = 0;
        uint32_t least = 0;
        uint32_t most = 0;
        ReadChoice(&channels->value, &count, &least, &most);
        slot->info.nativeLayout = LayoutForChannels(count);
    }
}

static const struct pw_node_events s_nodeEvents = {
    .version = PW_VERSION_NODE_EVENTS,
    .param = OnNodeParam,
};

// Adds a sink or source node as a device and subscribes to its formats.
static void AddNode(maudPipewire* pipewire, uint32_t globalId, maudDirection direction,
                    const struct spa_dict* props)
{
    PipewireNode* node = nullptr;
    for (uint32_t i = 0; i < pipewire->nodeCapacity && node == nullptr; ++i)
    {
        node = pipewire->nodes[i].used ? nullptr : &pipewire->nodes[i];
    }
    const char* key = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    const char* name = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
    if (node == nullptr || key == nullptr)
    {
        return;
    }
    name = name != nullptr ? name : key;
    uint32_t rate = PropertyNumber(props, PW_KEY_AUDIO_RATE);
    maudDeviceSpec spec = {
        .info = {.direction = direction,
                 .nativeLayout = LayoutForChannels(PropertyNumber(props, PW_KEY_AUDIO_CHANNELS)),
                 .nativeSampleRate = rate,
                 .minSampleRate = rate,
                 .maxSampleRate = rate},
        .name = name,
        .nameLength = CutUtf8(name, pipewire->context->def.limits.deviceTextBytes),
        .key = key,
        .keyLength = strlen(key),
    };
    maudDeviceId device;
    if (maudAddDevice(pipewire->context, &spec, &device) != maud_success)
    {
        return;
    }
    *node = (PipewireNode){.owner = pipewire, .device = device, .globalId = globalId, .used = true};
    node->proxy = pw_registry_bind(pipewire->connection.registry, globalId, PW_TYPE_INTERFACE_Node,
                                   PW_VERSION_NODE, 0);
    if (node->proxy != nullptr)
    {
        struct pw_node* proxy = (struct pw_node*)node->proxy;
        pw_node_add_listener(proxy, &node->listener, &s_nodeEvents, node);
        uint32_t ids[] = {SPA_PARAM_EnumFormat};
        pw_node_subscribe_params(proxy, ids, 1);
    }
    ResolveDefaults(pipewire);
}

static void RemoveNode(maudPipewire* pipewire, PipewireNode* node)
{
    if (node->proxy != nullptr)
    {
        spa_hook_remove(&node->listener);
        pipewire->api.proxyDestroy(node->proxy);
    }
    maudDeviceSlot* slot = maudFindDevice(pipewire->context, node->device);
    if (slot != nullptr)
    {
        maudRemoveDevice(pipewire->context, slot);
    }
    *node = (PipewireNode){0};
}

// Reads the node name out of a default metadata value,
// {"name": "..."}, into name; empty when there is none.
static void ParseDefaultName(const char* value, char* name)
{
    name[0] = '\0';
    struct spa_json root;
    struct spa_json object;
    if (value == nullptr)
    {
        return;
    }
    spa_json_init(&root, value, strlen(value));
    if (spa_json_enter_object(&root, &object) <= 0)
    {
        return;
    }
    char key[16];
    while (spa_json_get_string(&object, key, sizeof(key)) > 0)
    {
        if (spa_streq(key, "name"))
        {
            if (spa_json_get_string(&object, name, DEFAULT_NAME_BYTES) <= 0)
            {
                name[0] = '\0';
            }
            return;
        }
        const char* skipped;
        if (spa_json_next(&object, &skipped) <= 0)
        {
            return;
        }
    }
}

static int OnMetadataProperty(void* data, uint32_t subject, const char* key, const char* type,
                              const char* value)
{
    (void)type;
    maudPipewire* pipewire = data;
    if (subject != PW_ID_CORE || key == nullptr)
    {
        return 0;
    }
    if (spa_streq(key, "default.audio.sink"))
    {
        ParseDefaultName(value, pipewire->defaults.names[maud_directionOutput]);
    }
    else if (spa_streq(key, "default.audio.source"))
    {
        ParseDefaultName(value, pipewire->defaults.names[maud_directionInput]);
    }
    else
    {
        return 0;
    }
    ResolveDefaults(pipewire);
    return 0;
}

static const struct pw_metadata_events s_metadataEvents = {
    .version = PW_VERSION_METADATA_EVENTS,
    .property = OnMetadataProperty,
};

static void BindDefaults(maudPipewire* pipewire, uint32_t globalId)
{
    PipewireDefaults* defaults = &pipewire->defaults;
    defaults->metadata = pw_registry_bind(pipewire->connection.registry, globalId,
                                          PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0);
    if (defaults->metadata != nullptr)
    {
        pw_metadata_add_listener((struct pw_metadata*)defaults->metadata, &defaults->listener,
                                 &s_metadataEvents, pipewire);
    }
}

static void OnGlobal(void* data, uint32_t id, uint32_t permissions, const char* type,
                     uint32_t version, const struct spa_dict* props)
{
    (void)permissions;
    (void)version;
    maudPipewire* pipewire = data;
    if (props == nullptr)
    {
        return;
    }
    if (spa_streq(type, PW_TYPE_INTERFACE_Node))
    {
        const char* mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        if (spa_streq(mediaClass, "Audio/Sink"))
        {
            AddNode(pipewire, id, maud_directionOutput, props);
        }
        else if (mediaClass != nullptr && spa_strstartswith(mediaClass, "Audio/Source"))
        {
            AddNode(pipewire, id, maud_directionInput, props);
        }
    }
    else if (spa_streq(type, PW_TYPE_INTERFACE_Metadata) &&
             pipewire->defaults.metadata == nullptr &&
             spa_streq(spa_dict_lookup(props, PW_KEY_METADATA_NAME), "default"))
    {
        BindDefaults(pipewire, id);
    }
}

static void OnGlobalRemove(void* data, uint32_t id)
{
    maudPipewire* pipewire = data;
    PipewireNode* node = FindNode(pipewire, id);
    if (node != nullptr)
    {
        RemoveNode(pipewire, node);
    }
}

static const struct pw_registry_events s_registryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = OnGlobal,
    .global_remove = OnGlobalRemove,
};

// The daemon went away: every device with it.
static void LoseConnection(maudPipewire* pipewire)
{
    pipewire->connection.lost = true;
    for (uint32_t i = 0; i < pipewire->nodeCapacity; ++i)
    {
        if (pipewire->nodes[i].used)
        {
            RemoveNode(pipewire, &pipewire->nodes[i]);
        }
    }
}

static void OnCoreDone(void* data, uint32_t id, int seq)
{
    maudPipewire* pipewire = data;
    if (id == PW_ID_CORE && seq == pipewire->connection.pendingSync)
    {
        pipewire->connection.synced = true;
    }
}

static void OnCoreError(void* data, uint32_t id, int seq, int res, const char* message)
{
    (void)seq;
    (void)message;
    maudPipewire* pipewire = data;
    if (id == PW_ID_CORE && res == -EPIPE)
    {
        LoseConnection(pipewire);
    }
}

static const struct pw_core_events s_coreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .done = OnCoreDone,
    .error = OnCoreError,
};

// Waits on the calling thread until PipeWire has answered everything
// asked so far, or the deadline passes.
static bool Roundtrip(maudPipewire* pipewire, int64_t deadline)
{
    PipewireConnection* connection = &pipewire->connection;
    connection->synced = false;
    connection->pendingSync = pw_core_sync(connection->core, PW_ID_CORE, 0);
    pw_loop_enter(connection->loop);
    while (!connection->synced && !connection->lost)
    {
        int64_t remaining = deadline - Now();
        if (remaining <= 0)
        {
            break;
        }
        pw_loop_iterate(connection->loop, (int)(remaining / 1000000 + 1));
    }
    pw_loop_leave(connection->loop);
    return connection->synced && !connection->lost;
}

// Releases the connection's parts in the reverse order of creation.
static void Disconnect(maudPipewire* pipewire)
{
    PipewireConnection* connection = &pipewire->connection;
    for (uint32_t i = 0; i < pipewire->nodeCapacity; ++i)
    {
        PipewireNode* node = &pipewire->nodes[i];
        if (node->used && node->proxy != nullptr)
        {
            spa_hook_remove(&node->listener);
            pipewire->api.proxyDestroy(node->proxy);
        }
    }
    if (pipewire->defaults.metadata != nullptr)
    {
        spa_hook_remove(&pipewire->defaults.listener);
        pipewire->api.proxyDestroy(pipewire->defaults.metadata);
    }
    if (connection->registry != nullptr)
    {
        spa_hook_remove(&connection->registryListener);
        pipewire->api.proxyDestroy((struct pw_proxy*)connection->registry);
    }
    if (connection->core != nullptr)
    {
        spa_hook_remove(&connection->coreListener);
        pipewire->api.coreDisconnect(connection->core);
    }
    if (connection->context != nullptr)
    {
        pipewire->api.contextDestroy(connection->context);
    }
    if (connection->loop != nullptr)
    {
        pipewire->api.loopDestroy(connection->loop);
    }
}

static void Release(maudContext* context, maudPipewire* pipewire)
{
    bool initialized = pipewire->api.library != nullptr;
    Disconnect(pipewire);
    if (initialized)
    {
        pipewire->api.deinit();
    }
    maudUnloadPipewire(&pipewire->api);
    maudContextRelease(context, pipewire, pipewire->bytes, alignof(maudPipewire));
    context->native = nullptr;
}

// Connects to the daemon and asks for the registry. False when the
// daemon does not answer.
static bool Connect(maudPipewire* pipewire)
{
    PipewireConnection* connection = &pipewire->connection;
    pipewire->api.init(nullptr, nullptr);
    connection->loop = pipewire->api.loopNew(nullptr);
    connection->context = connection->loop != nullptr
                              ? pipewire->api.contextNew(connection->loop, nullptr, 0)
                              : nullptr;
    connection->core = connection->context != nullptr
                           ? pipewire->api.contextConnect(connection->context, nullptr, 0)
                           : nullptr;
    if (connection->core == nullptr)
    {
        return false;
    }
    pw_core_add_listener(connection->core, &connection->coreListener, &s_coreEvents, pipewire);
    connection->registry = pw_core_get_registry(connection->core, PW_VERSION_REGISTRY, 0);
    if (connection->registry == nullptr)
    {
        return false;
    }
    pw_registry_add_listener(connection->registry, &connection->registryListener, &s_registryEvents,
                             pipewire);
    return true;
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t capacity = context->def.limits.devices;
    size_t bytes = sizeof(maudPipewire) + (size_t)capacity * sizeof(PipewireNode);
    maudPipewire* pipewire = maudContextAllocate(context, bytes, alignof(maudPipewire));
    if (pipewire == nullptr)
    {
        return maud_errorCapacity;
    }
    *pipewire = (maudPipewire){
        .context = context,
        .nodes = (PipewireNode*)(pipewire + 1),
        .nodeCapacity = capacity,
        .bytes = bytes,
    };
    memset(pipewire->nodes, 0, (size_t)capacity * sizeof(PipewireNode));
    context->native = pipewire;
    if (!maudLoadPipewire(&pipewire->api))
    {
        Release(context, pipewire);
        return maud_errorUnsupported;
    }
    // The first round trip lists the globals; the second answers the
    // binds the first one made: node formats and the default metadata.
    int64_t deadline = Now() + ROUNDTRIP_DEADLINE_NS;
    if (!Connect(pipewire) || !Roundtrip(pipewire, deadline) || !Roundtrip(pipewire, deadline))
    {
        Release(context, pipewire);
        return maud_errorUnsupported;
    }
    return maud_success;
}

static void CloseContext(maudContext* context)
{
    Release(context, context->native);
}

static void Pump(maudContext* context)
{
    PipewireConnection* connection = &((maudPipewire*)context->native)->connection;
    if (connection->lost)
    {
        return;
    }
    pw_loop_enter(connection->loop);
    for (int i = 0; i < PUMP_ITERATIONS && !connection->lost; ++i)
    {
        if (pw_loop_iterate(connection->loop, 0) <= 0)
        {
            break;
        }
    }
    pw_loop_leave(connection->loop);
}

static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    (void)def;
    (void)device;
    (void)formatOut;
    return maud_errorUnsupported;
}

static const maudBackend s_pipewire = {
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .rendersOnCaller = false,
};

const maudBackend* maudGetPipewireBackend(void)
{
    return &s_pipewire;
}
