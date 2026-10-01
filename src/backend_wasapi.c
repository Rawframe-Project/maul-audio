// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WASAPI backend's devices. The context holds the multithreaded
// apartment open, so the host's threads call COM without initializing
// it. Devices are the active endpoints, their formats read from the
// property store without activating them; a notification raises a flag
// and the drain lists endpoints and defaults again.

#include "backend.h"
#include "context.h"
#include "device.h"
#include "layout.h"
#include "wasapi_core.h"

#include <string.h>

static const GUID s_clsidEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID s_iidEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const PROPERTYKEY s_friendlyName = {
    {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};
static const PROPERTYKEY s_deviceFormat = {
    {0xF19F064D, 0x082C, 0x4E27, {0xBC, 0x73, 0x68, 0x82, 0xA1, 0xBB, 0x8E, 0x4C}}, 0};

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

// Converts wide to UTF-8 in out; false when it does not fit.
static bool ToUtf8(const wchar_t* wide, char* out, int capacity)
{
    return WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, capacity, nullptr, nullptr) > 0;
}

// The endpoint ID of a device in UTF-8; false when it has none or it
// does not fit.
static bool IdOf(IMMDevice* device, char* out)
{
    LPWSTR id = nullptr;
    bool ok = SUCCEEDED(IMMDevice_GetId(device, &id)) && ToUtf8(id, out, MAUD_WASAPI_KEY_BYTES);
    CoTaskMemFree(id);
    return ok;
}

// Reads the audio engine's device format into info, if the store has
// one: its rate, and the layout of its channel count. Each layout has a
// count of its own, so the speaker mask would name no other.
static void ReadFormat(IPropertyStore* store, maudDeviceInfo* info)
{
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(IPropertyStore_GetValue(store, &s_deviceFormat, &value)) && value.vt == VT_BLOB &&
        value.blob.cbSize >= sizeof(WAVEFORMATEX))
    {
        const WAVEFORMATEX* format = (const WAVEFORMATEX*)value.blob.pBlobData;
        info->nativeSampleRate = format->nSamplesPerSec;
        info->minSampleRate = format->nSamplesPerSec;
        info->maxSampleRate = format->nSamplesPerSec;
        info->nativeLayout = maudLayoutWithChannels(format->nChannels);
    }
    PropVariantClear(&value);
}

// Describes one endpoint into endpoint and spec; false when it cannot.
static bool Describe(const maudContext* context, IMMDevice* device, maudDirection direction,
                     maudWasapiEndpoint* endpoint, maudDeviceSpec* spec)
{
    IPropertyStore* store = nullptr;
    if (!IdOf(device, endpoint->key) ||
        FAILED(IMMDevice_OpenPropertyStore(device, STGM_READ, &store)))
    {
        return false;
    }
    *spec = (maudDeviceSpec){.info = {.direction = direction}};
    PROPVARIANT name;
    PropVariantInit(&name);
    bool named = SUCCEEDED(IPropertyStore_GetValue(store, &s_friendlyName, &name)) &&
                 name.vt == VT_LPWSTR &&
                 ToUtf8(name.pwszVal, endpoint->name, MAUD_WASAPI_NAME_BYTES);
    PropVariantClear(&name);
    if (!named)
    {
        memcpy(endpoint->name, endpoint->key, sizeof(endpoint->key));
    }
    ReadFormat(store, &spec->info);
    IPropertyStore_Release(store);
    spec->name = endpoint->name;
    spec->nameLength = CutUtf8(endpoint->name, context->def.limits.deviceTextBytes);
    spec->key = endpoint->key;
    spec->keyLength = strlen(endpoint->key);
    return true;
}

// Lists the active endpoints of one flow from count on; returns the new
// count.
static uint32_t ScanFlow(maudContext* context, EDataFlow flow, uint32_t count)
{
    maudWasapi* wasapi = context->native;
    IMMDeviceCollection* collection = nullptr;
    if (FAILED(IMMDeviceEnumerator_EnumAudioEndpoints(wasapi->enumerator, flow, DEVICE_STATE_ACTIVE,
                                                      &collection)))
    {
        return count;
    }
    UINT total = 0;
    IMMDeviceCollection_GetCount(collection, &total);
    maudDirection direction = flow == eRender ? maud_directionOutput : maud_directionInput;
    for (UINT i = 0; i < total && count < context->def.limits.devices; ++i)
    {
        IMMDevice* device = nullptr;
        if (SUCCEEDED(IMMDeviceCollection_Item(collection, i, &device)))
        {
            count += Describe(context, device, direction, &wasapi->endpoints[count],
                              &wasapi->specs[count])
                         ? 1u
                         : 0u;
            IMMDevice_Release(device);
        }
    }
    IMMDeviceCollection_Release(collection);
    return count;
}

// Points one role's default of one flow at the system's.
static void ReadDefault(maudContext* context, EDataFlow flow, ERole role, maudDeviceRole ours)
{
    maudWasapi* wasapi = context->native;
    IMMDevice* device = nullptr;
    char key[MAUD_WASAPI_KEY_BYTES];
    if (FAILED(
            IMMDeviceEnumerator_GetDefaultAudioEndpoint(wasapi->enumerator, flow, role, &device)))
    {
        return;
    }
    bool found = IdOf(device, key);
    IMMDevice_Release(device);
    maudDeviceId id = maudFindDeviceByKey(
        context, flow == eRender ? maud_directionOutput : maud_directionInput, key, strlen(key));
    if (found && id.index1 != 0)
    {
        maudSetDefaultDevice(context, ours, id);
    }
}

// Lists the endpoints again, syncs the table, and reads the defaults.
static maudResult Rescan(maudContext* context)
{
    maudWasapi* wasapi = context->native;
    uint32_t count = ScanFlow(context, eCapture, ScanFlow(context, eRender, 0));
    maudResult result = maudSyncDevices(context, wasapi->specs, count, nullptr);
    for (int flow = 0; flow < 2; ++flow)
    {
        ReadDefault(context, flow == 0 ? eRender : eCapture, eConsole, maud_roleGeneral);
        ReadDefault(context, flow == 0 ? eRender : eCapture, eCommunications,
                    maud_roleCommunications);
    }
    return result;
}

static void Release(maudContext* context, maudWasapi* wasapi)
{
    if (wasapi->registered)
    {
        IMMDeviceEnumerator_UnregisterEndpointNotificationCallback(wasapi->enumerator,
                                                                   &wasapi->notifier.client);
    }
    if (wasapi->enumerator != nullptr)
    {
        IMMDeviceEnumerator_Release(wasapi->enumerator);
    }
    if (wasapi->apartmentHeld)
    {
        CoDecrementMTAUsage(wasapi->apartment);
    }
    maudContextRelease(context, wasapi, wasapi->bytes, alignof(maudWasapi));
    context->native = nullptr;
}

static maudResult OpenContext(maudContext* context)
{
    uint32_t devices = context->def.limits.devices;
    size_t bytes = sizeof(maudWasapi) +
                   (size_t)devices * (sizeof(maudWasapiEndpoint) + sizeof(maudDeviceSpec));
    maudWasapi* wasapi = maudContextAllocate(context, bytes, alignof(maudWasapi));
    if (wasapi == nullptr)
    {
        return maud_errorCapacity;
    }
    *wasapi = (maudWasapi){.context = context, .bytes = bytes};
    wasapi->endpoints = (maudWasapiEndpoint*)(wasapi + 1);
    wasapi->specs = (maudDeviceSpec*)(wasapi->endpoints + devices);
    maudInitWasapiNotifier(&wasapi->notifier);
    context->native = wasapi;
    wasapi->apartmentHeld = SUCCEEDED(CoIncrementMTAUsage(&wasapi->apartment));
    if (!wasapi->apartmentHeld ||
        FAILED(CoCreateInstance(&s_clsidEnumerator, nullptr, CLSCTX_ALL, &s_iidEnumerator,
                                (void**)&wasapi->enumerator)))
    {
        Release(context, wasapi);
        return maud_errorUnsupported;
    }
    wasapi->registered = SUCCEEDED(IMMDeviceEnumerator_RegisterEndpointNotificationCallback(
        wasapi->enumerator, &wasapi->notifier.client));
    maudResult result = Rescan(context);
    if (result != maud_success)
    {
        Release(context, wasapi);
    }
    return result;
}

static void CloseContext(maudContext* context)
{
    Release(context, context->native);
}

static void Pump(maudContext* context)
{
    maudWasapi* wasapi = context->native;
    if (maudTakeWasapiChanges(&wasapi->notifier))
    {
        maudResult result = Rescan(context);
        (void)result;
    }
}

// Streams come in the next step; until then WASAPI refuses them.
static maudResult OpenStream(const maudContext* context, const maudStreamDef* def,
                             const maudDeviceInfo* device, maudStreamFormat* formatOut)
{
    (void)context;
    (void)def;
    (void)device;
    (void)formatOut;
    return maud_errorUnsupported;
}

static const maudBackend s_wasapi = {
    .kind = maud_backendWasapi,
    .openContext = OpenContext,
    .closeContext = CloseContext,
    .pump = Pump,
    .openStream = OpenStream,
    .attachStream = nullptr,
    .detachStream = nullptr,
    .setStreamActive = nullptr,
    .retargetStream = nullptr,
    .rendersOnCaller = false,
};

const maudBackend* maudGetWasapiBackend(void)
{
    return &s_wasapi;
}
