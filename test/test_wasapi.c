// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WASAPI backend's devices against what COM itself reports, its
// notifications, whose callbacks the test calls itself (wine delivers
// none), and its streams. Without an audio endpoint the test is
// skipped, unless MAUD_REQUIRE_WASAPI is set.

// getenv reads the test's switches; the C runtime's warning that it is
// unsafe is about its result's lifetime, which the test does not keep.
#define _CRT_SECURE_NO_WARNINGS

#include "context.h"
#include "device.h"
#include "test_clock.h"
#include "test_harness.h"
#include "wasapi_core.h"

#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <stdatomic.h>
#include <stdio.h>
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

// The tests' sleep with the signature test_clock.h takes.
static void Pause(int milliseconds)
{
    Sleep((DWORD)milliseconds);
}

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

// The thread that runs the test, which no callback may run on.
static DWORD s_control;

typedef struct Blocks
{
    _Atomic(uint32_t) count;
    _Atomic(uint32_t) wrongSize;
    _Atomic(uint32_t) withInput;
    _Atomic(uint32_t) onControl;
    // Whether the first callback's thread was named maud-wasapi.
    _Atomic(int) named;
    uint32_t periodFrames;
} Blocks;

// Whether the calling thread's description is maud-wasapi, through
// GetThreadDescription, which mingw's headers do not declare.
static bool NamedMaudWasapi(void)
{
    typedef HRESULT(WINAPI * Describe)(HANDLE thread, PWSTR * description);
    FARPROC found = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription");
    if (found == nullptr)
    {
        return false;
    }
    Describe describe;
    memcpy((void*)&describe, (const void*)&found, sizeof(describe));
    PWSTR name = nullptr;
    bool named = SUCCEEDED(describe(GetCurrentThread(), &name)) && name != nullptr &&
                 wcscmp(name, L"maud-wasapi") == 0;
    LocalFree(name);
    return named;
}

static void CountBlocks(const maudStreamBlock* block, void* user)
{
    Blocks* blocks = user;
    if (GetCurrentThreadId() == s_control)
    {
        atomic_fetch_add(&blocks->onControl, 1);
    }
    if (block->frameCount != blocks->periodFrames)
    {
        atomic_fetch_add(&blocks->wrongSize, 1);
    }
    if (block->input != nullptr)
    {
        atomic_fetch_add(&blocks->withInput, 1);
    }
    if (atomic_load(&blocks->count) == 0)
    {
        atomic_store(&blocks->named, NamedMaudWasapi() ? 1 : -1);
    }
    atomic_fetch_add(&blocks->count, 1);
}

static bool WaitForBlocks(maudContext* context, const Blocks* blocks, uint32_t count)
{
    for (int tries = 0; tries < 500 && atomic_load(&blocks->count) < count; ++tries)
    {
        maudNotification ignored;
        while (maudNextNotification(context, &ignored) == maud_success)
        {
        }
        Sleep(10);
    }
    return atomic_load(&blocks->count) >= count;
}

// Frames the stream moves per second of wall time over one window.
static double MeasureWindow(const maudContext* context, maudStreamId stream, int milliseconds)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    uint64_t first = 0;
    uint64_t last = 0;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    CHECK(maudGetStreamPosition(context, stream, &first) == maud_success, "position");
    Sleep(milliseconds);
    CHECK(maudGetStreamPosition(context, stream, &last) == maud_success, "position");
    QueryPerformanceCounter(&end);
    double seconds = (double)(end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
    return (double)(last - first) / seconds;
}

// The stream's rate: the best of three windows of a second. A
// loaded machine can stall the platform's clock, which only lowers a
// window's count, so the best window is the one that shows the rate.
static double MeasureRate(const maudContext* context, maudStreamId stream)
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

static maudStreamId OpenStream(maudContext* context, maudStreamDef* def, Blocks* blocks)
{
    def->periodFrames = 256;
    def->callback = CountBlocks;
    def->user = blocks;
    blocks->periodFrames = 256;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, def, &stream) == maud_success, "create");
    return stream;
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
}

static void TestOutputStream(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenStream(context, &def, &blocks);
    maudStreamFormat format;
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success &&
              format.sampleRate == 48000,
          "native at the engine's rate");
    Sleep(100);
    CHECK(atomic_load(&blocks.count) == 0, "nothing before start");
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 20), "blocks arrive");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(atomic_load(&blocks.onControl) == 0, "on a thread of the stream's");
    CHECK(atomic_load(&blocks.named) == 1, "named maud-wasapi");
    CHECK(Near(MeasureRate(context, stream), 48000.0), "at its rate");
    CHECK(StreamClockIsSound(context, stream, true, true, Pause),
          "its clock maps frames to host time");
    CHECK(maudStopStream(context, stream) == maud_success, "stop");
    uint32_t stopped = atomic_load(&blocks.count);
    Sleep(100);
    CHECK(atomic_load(&blocks.count) == stopped, "no callbacks once stopped");
    CHECK(maudStartStream(context, stream) == maud_success, "start again");
    CHECK(WaitForBlocks(context, &blocks, stopped + 20), "it runs again");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy while running");
    def = maudDefaultStreamDef();
    def.ratePolicy = maud_rateRequired;
    def.sampleRate = 44100;
    def.callback = CountBlocks;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported,
          "a required rate the engine does not run at");
    Blocks converted = {0};
    def.ratePolicy = maud_ratePlatformConverted;
    stream = OpenStream(context, &def, &converted);
    CHECK(maudStartStream(context, stream) == maud_success, "start converted");
    CHECK(WaitForBlocks(context, &converted, 10), "it runs");
    CHECK(Near(MeasureRate(context, stream), 44100.0), "at its own rate");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.callback = CountBlocks;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorUnsupported, "no pull mode");
}

static void TestInputStream(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    maudStreamId stream = OpenStream(context, &def, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success, "start capture");
    CHECK(WaitForBlocks(context, &blocks, 20), "captured blocks arrive");
    CHECK(atomic_load(&blocks.withInput) == atomic_load(&blocks.count), "each with input");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(StreamClockIsSound(context, stream, false, true, Pause),
          "its clock maps frames to host time");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// A stream on the default moves with it: WASAPI binds it to one
// endpoint, so it is opened again there and runs on.
static void TestMove(maudContext* context)
{
    maudDeviceId ids[8];
    uint32_t count = 0;
    maudDeviceId original = {0, 0};
    CHECK(maudGetDevices(context, maud_directionOutput, ids, 8, &count) == maud_success, "list");
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &original) ==
              maud_success,
          "the default");
    maudDeviceId other = {0, 0};
    for (uint32_t i = 0; i < count && i < 8 && other.index1 == 0; ++i)
    {
        other = ids[i].index1 != original.index1 ? ids[i] : other;
    }
    if (other.index1 == 0)
    {
        return;
    }
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenStream(context, &def, &blocks);
    CHECK(maudStartStream(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 10), "it plays");
    maudSetDefaultDevice(context, maud_roleGeneral, other);
    maudNotification record;
    bool moved = false;
    while (maudNextNotification(context, &record) == maud_success)
    {
        moved = moved ||
                (record.kind == maud_notifyStreamMoved && record.streamId.index1 == stream.index1 &&
                 record.deviceId.index1 == other.index1);
    }
    CHECK(moved, "it moves with the default");
    const maudWasapi* wasapi = context->native;
    const maudWasapiStream* entry = &wasapi->streams[stream.index1 - 1];
    CHECK(entry->device.index1 == other.index1 && entry->device.generation == other.generation,
          "opened again on the new endpoint");
    uint32_t after = atomic_load(&blocks.count);
    CHECK(WaitForBlocks(context, &blocks, after + 20), "and plays on the new endpoint");
    CHECK(Near(MeasureRate(context, stream), 48000.0), "at its rate");
    maudSetDefaultDevice(context, maud_roleGeneral, original);
    CHECK(WaitForBlocks(context, &blocks, atomic_load(&blocks.count) + 20), "and back");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
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
    s_control = GetCurrentThreadId();
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
    TestOutputStream(context);
    TestInputStream(context);
    TestMove(context);
    TestNotifier(context);
    TestDrainRescans(context);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
