// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WASAPI backend's devices against what COM itself reports, and its
// notifications, whose callbacks the test calls itself: wine delivers
// none. Without an audio endpoint the test is skipped, unless
// MAUD_REQUIRE_WASAPI is set.

#include "context.h"
#include "device.h"
#include "test_harness.h"
#include "wasapi_core.h"

#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <stdlib.h>
#include <string.h>

#define SKIP 77

static const GUID s_clsidEnumerator = {
    0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID s_iidEnumerator = {
    0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const PROPERTYKEY s_otherKey = {
    {0x12345678, 0x1234, 0x1234, {0x12, 0x34, 0x12, 0x34, 0x12, 0x34, 0x12, 0x34}}, 1};
static const PROPERTYKEY s_formatKey = {
    {0xF19F064D, 0x082C, 0x4E27, {0xBC, 0x73, 0x68, 0x82, 0xA1, 0xBB, 0x8E, 0x4C}}, 0};

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

// The system's default endpoint ID for a flow and role, in UTF-8.
static bool SystemDefault(IMMDeviceEnumerator* enumerator, EDataFlow flow, ERole role, char* out)
{
    IMMDevice* device = nullptr;
    LPWSTR id = nullptr;
    bool ok =
        SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, flow, role, &device)) &&
        SUCCEEDED(IMMDevice_GetId(device, &id)) &&
        WideCharToMultiByte(CP_UTF8, 0, id, -1, out, 128, nullptr, nullptr) > 0;
    CoTaskMemFree(id);
    if (device != nullptr)
    {
        IMMDevice_Release(device);
    }
    return ok;
}

static bool DefaultIs(const maudContext* context, maudDirection direction, maudDeviceRole role,
                      const char* key)
{
    maudDeviceId current = {0, 0};
    char bytes[128];
    size_t length = 0;
    return maudGetDefaultDevice(context, direction, role, &current) == maud_success &&
           maudGetDeviceKey(context, current, bytes, sizeof(bytes), &length) == maud_success &&
           length == strlen(key) && memcmp(bytes, key, length) == 0;
}

static void Silence(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

static void TestDevices(maudContext* context)
{
    CHECK(maudGetContextBackend(context) == maud_backendWasapi, "WASAPI");
    IMMDeviceEnumerator* enumerator = nullptr;
    CHECK(SUCCEEDED(CoCreateInstance(&s_clsidEnumerator, nullptr, CLSCTX_ALL, &s_iidEnumerator,
                                     (void**)&enumerator)),
          "the test's own enumerator, on the context's apartment");
    struct
    {
        EDataFlow flow;
        maudDirection direction;
    } flows[] = {{eRender, maud_directionOutput}, {eCapture, maud_directionInput}};
    for (int f = 0; f < 2; ++f)
    {
        char key[128];
        if (SystemDefault(enumerator, flows[f].flow, eConsole, key))
        {
            CHECK(DefaultIs(context, flows[f].direction, maud_roleGeneral, key),
                  "the general default is the console role's");
        }
        if (SystemDefault(enumerator, flows[f].flow, eCommunications, key))
        {
            CHECK(DefaultIs(context, flows[f].direction, maud_roleCommunications, key),
                  "the communications default is the communications role's");
        }
        maudDeviceId ids[32];
        uint32_t count = 0;
        CHECK(maudGetDevices(context, flows[f].direction, ids, 32, &count) == maud_success &&
                  count >= 1,
              "endpoints are listed");
        for (uint32_t i = 0; i < count && i < 32; ++i)
        {
            maudDeviceInfo info = {0};
            CHECK(maudGetDeviceInfo(context, ids[i], &info) == maud_success &&
                      info.nativeSampleRate >= 8000 && info.nativeLayout != maud_layoutNone &&
                      info.minSampleRate == info.nativeSampleRate &&
                      info.maxSampleRate == info.nativeSampleRate,
                  "each with the engine's format, read without activating it");
        }
    }
    IMMDeviceEnumerator_Release(enumerator);
    maudStreamDef def = maudDefaultStreamDef();
    def.callback = Silence;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no streams yet");
}

static void TestNotifier(maudContext* context)
{
    maudWasapi* wasapi = context->native;
    IMMNotificationClient* client = &wasapi->notifier.client;
    CHECK(!maudTakeWasapiChanges(&wasapi->notifier), "nothing yet");
    IMMNotificationClient_OnDeviceAdded(client, L"x");
    CHECK(maudTakeWasapiChanges(&wasapi->notifier), "an added device raises the flag");
    CHECK(!maudTakeWasapiChanges(&wasapi->notifier), "taken once");
    IMMNotificationClient_OnDeviceRemoved(client, L"x");
    CHECK(maudTakeWasapiChanges(&wasapi->notifier), "a removed one");
    IMMNotificationClient_OnDeviceStateChanged(client, L"x", DEVICE_STATE_UNPLUGGED);
    CHECK(maudTakeWasapiChanges(&wasapi->notifier), "a state change");
    IMMNotificationClient_OnDefaultDeviceChanged(client, eRender, eConsole, L"x");
    CHECK(maudTakeWasapiChanges(&wasapi->notifier), "a default change");
    IMMNotificationClient_OnPropertyValueChanged(client, L"x", s_otherKey);
    CHECK(!maudTakeWasapiChanges(&wasapi->notifier), "not another property");
    IMMNotificationClient_OnPropertyValueChanged(client, L"x", s_formatKey);
    CHECK(maudTakeWasapiChanges(&wasapi->notifier), "but the device format");
    void* object = nullptr;
    static const GUID iidNotification = {
        0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
    CHECK(IMMNotificationClient_QueryInterface(client, &iidNotification, &object) == S_OK &&
              object == client,
          "it is an IMMNotificationClient");
    CHECK(IMMNotificationClient_QueryInterface(client, &s_otherKey.fmtid, &object) ==
                  E_NOINTERFACE &&
              object == nullptr,
          "and nothing else");
}

// The drain rescans when the flag is up: devices a made-up empty scan
// removed come back.
static void TestDrainRescans(maudContext* context)
{
    maudWasapi* wasapi = context->native;
    uint32_t real =
        CountDevices(context, maud_directionOutput) + CountDevices(context, maud_directionInput);
    CHECK(maudSyncDevices(context, nullptr, 0, nullptr) == maud_success, "an empty scan");
    CHECK(Drain(context, maud_notifyDeviceRemoved) == real, "removes every device");
    CHECK(Drain(context, maud_notifyDeviceAdded) == 0, "no rescan without the flag");
    IMMNotificationClient_OnDeviceAdded(&wasapi->notifier.client, L"x");
    CHECK(Drain(context, maud_notifyDeviceAdded) == real, "the drain rescans");
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendWasapi;
    maudContext* context = nullptr;
    maudResult created = maudCreateContext(&def, &context);
    uint32_t outputs = created == maud_success ? CountDevices(context, maud_directionOutput) : 0;
    if (outputs == 0)
    {
        if (context != nullptr)
        {
            CHECK(maudDestroyContext(context) == maud_success, "destroy");
        }
        return getenv("MAUD_REQUIRE_WASAPI") != nullptr ? 1 : SKIP;
    }
    TestDevices(context);
    TestNotifier(context);
    TestDrainRescans(context);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
