// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WASAPI backend's devices against what COM itself reports, its
// notifications, whose callbacks the test calls itself (wine delivers
// none), and its streams. Without an audio endpoint the test is
// skipped, unless MAUD_REQUIRE_WASAPI is set. With MAUD_WASAPI_STA set,
// the test's thread enters a single-threaded apartment first, as a
// host's main thread does for drag and drop: the context, its devices
// and its streams then work from there too.

// getenv reads the test's switches; the C runtime's warning that it is
// unsafe is about its result's lifetime, which the test does not keep.
#define _CRT_SECURE_NO_WARNINGS

#include "context.h"
#include "device.h"
#include "test_clock.h"
#include "test_harness.h"
#include "test_names.h"
#include "test_wait.h"
#include "wasapi_core.h"
#include "wasapi_stream.h"

#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/objects.h"
#include "maul-audio/stream.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
        count += record.kind == kind ? 1u : 0u;
    }
    return count;
}

static uint32_t CountDevices(const maudContext* context, maudDirection direction)
{
    uint32_t count = 0;
    CHECK(maudContext_GetDevices(context, direction, nullptr, 0, &count) == maud_success, "count");
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

// The endpoint form factor of the device whose endpoint ID is key, as
// the test reads it itself; UnknownFormFactor when it cannot.
static UINT FormFactorOf(IMMDeviceEnumerator* enumerator, const char* key)
{
    static const PROPERTYKEY formFactor = {
        {0x1DA5D803, 0xD492, 0x4EDD, {0x8C, 0x23, 0xE0, 0xC0, 0xFF, 0xEE, 0x7F, 0x0E}}, 0};
    wchar_t id[128];
    IMMDevice* device = nullptr;
    IPropertyStore* store = nullptr;
    UINT form = UnknownFormFactor;
    if (MultiByteToWideChar(CP_UTF8, 0, key, -1, id, 128) > 0 &&
        SUCCEEDED(IMMDeviceEnumerator_GetDevice(enumerator, id, &device)) &&
        SUCCEEDED(IMMDevice_OpenPropertyStore(device, STGM_READ, &store)))
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(IPropertyStore_GetValue(store, &formFactor, &value)) && value.vt == VT_UI4)
        {
            form = value.ulVal;
        }
        PropVariantClear(&value);
        IPropertyStore_Release(store);
    }
    if (device != nullptr)
    {
        IMMDevice_Release(device);
    }
    return form;
}

// Whether a device's form is what its endpoint's form factor says.
static bool FormMatches(maudDeviceForm form, UINT formFactor)
{
    switch (formFactor)
    {
    case Speakers:
        return form == maud_formSpeakers;
    case LineLevel:
        return form == maud_formLine;
    case Headphones:
        return form == maud_formHeadphones;
    case Microphone:
        return form == maud_formMicrophone;
    case Headset:
        return form == maud_formHeadset;
    case Handset:
        return form == maud_formHandset;
    case UnknownDigitalPassthrough:
    case SPDIF:
    case DigitalAudioDisplayDevice:
        return form == maud_formDigital;
    default:
        return form == maud_formUnknown;
    }
}

static bool DefaultIs(const maudContext* context, maudDirection direction, maudDeviceRole role,
                      const char* key)
{
    maudDeviceId current = {0, 0};
    char bytes[128];
    size_t length = 0;
    return maudContext_GetDefaultDevice(context, direction, role, &current) == maud_success &&
           maudDevice_GetKey(context, current, bytes, sizeof(bytes), &length) == maud_success &&
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
    // The block at which the callback stalls for 300 ms, or 0.
    uint32_t stallAt;
    // Whether an output plays a 1 kHz tone at 0.5, and the loudest input
    // sample so far, in thousandths.
    bool tone;
    float phase;
    _Atomic(uint32_t) peak;
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
        uint32_t channels = maudGetLayoutChannelCount(block->layout);
        uint32_t peak = atomic_load(&blocks->peak);
        for (uint32_t i = 0; i < block->frameCount * channels; ++i)
        {
            float level = fabsf(block->input[i]) * 1000.0f;
            peak = level > (float)peak ? (uint32_t)level : peak;
        }
        atomic_store(&blocks->peak, peak);
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
    if (atomic_load(&blocks->count) == 0)
    {
        atomic_store(&blocks->named, NamedMaudWasapi() ? 1 : -1);
    }
    if (blocks->stallAt != 0 && atomic_load(&blocks->count) == blocks->stallAt)
    {
        // hygiene: stalls the callback past the buffer so the endpoint underruns
        Pause(300);
    }
    atomic_fetch_add(&blocks->count, 1);
}

static void DrainAll(maudContext* context)
{
    maudNotification ignored;
    while (maudContext_NextNotification(context, &ignored) == maud_success)
    {
    }
}

static bool WaitForBlocks(maudContext* context, const Blocks* blocks, uint32_t count)
{
    POLL_UNTIL(5000, "the stream's blocks", DrainAll(context),
               atomic_load(&blocks->count) >= count);
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
    CHECK(maudStream_GetPosition(context, stream, &first) == maud_success, "position");
    // hygiene: a window of the stream's frames against the wall clock
    Pause(milliseconds);
    CHECK(maudStream_GetPosition(context, stream, &last) == maud_success, "position");
    QueryPerformanceCounter(&end);
    double seconds = (double)(end.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
    return (double)(last - first) / seconds;
}

// The stream's rate: the best of five windows of a second. A
// loaded machine can stall the platform's clock, which only lowers a
// window's count, so the best window is the one that shows the rate.
static double MeasureRate(const maudContext* context, maudStreamId stream)
{
    double best = 0.0;
    for (int window = 0; window < 5; ++window)
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

typedef struct Where
{
    DWORD thread;
    APTTYPE type;
    HRESULT read;
} Where;

static void Locate(void* user)
{
    Where* where = user;
    APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
    where->thread = GetCurrentThreadId();
    where->read = CoGetApartmentType(&where->type, &qualifier);
}

// The backend's COM calls run on a thread of the context's in the
// multithreaded apartment, not on the caller's, whatever its apartment.
static void TestApartment(maudContext* context)
{
    maudWasapi* wasapi = context->native;
    Where where = {0, APTTYPE_STA, E_FAIL};
    maudCallInWasapiApartment(&wasapi->apartment, Locate, &where);
    CHECK(where.thread != 0 && where.thread != GetCurrentThreadId(), "a thread of its own");
    CHECK(SUCCEEDED(where.read) && where.type == APTTYPE_MTA, "in the multithreaded apartment");
    Where again = {0, APTTYPE_STA, E_FAIL};
    maudCallInWasapiApartment(&wasapi->apartment, Locate, &again);
    CHECK(again.thread == where.thread, "the same thread each call");
}

static void TestDevices(maudContext* context)
{
    CHECK(maudContext_GetBackend(context) == maud_backendWasapi, "WASAPI");
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
        CHECK(maudContext_GetDevices(context, flows[f].direction, ids, 32, &count) ==
                      maud_success &&
                  count >= 1,
              "endpoints are listed");
        for (uint32_t i = 0; i < count && i < 32; ++i)
        {
            maudDeviceInfo info = {0};
            CHECK(maudDevice_GetInfo(context, ids[i], &info) == maud_success &&
                      info.nativeSampleRate >= 8000 && info.nativeLayout != maud_layoutNone &&
                      info.minSampleRate == info.nativeSampleRate &&
                      info.maxSampleRate == info.nativeSampleRate,
                  "each with the engine's format, read without activating it");
            char endpoint[128];
            size_t length = 0;
            CHECK(maudDevice_GetKey(context, ids[i], endpoint, sizeof(endpoint) - 1, &length) ==
                      maud_success,
                  "its key");
            endpoint[length < sizeof(endpoint) - 1 ? length : sizeof(endpoint) - 1] = '\0';
            UINT formFactor = FormFactorOf(enumerator, endpoint);
            printf("endpoint %s: form factor %u, form %u, spatializer %u (%u objects)\n", endpoint,
                   formFactor, info.form, (unsigned)info.spatializer, info.spatialObjects);
            CHECK(FormMatches(info.form, formFactor), "and the form its form factor names");
            char name[512];
            size_t named = 0;
            CHECK(maudDevice_GetName(context, ids[i], name, sizeof(name), &named) == maud_success &&
                      named > 0 && named < 256 && IsUtf8(name, named) &&
                      (named != length || memcmp(name, endpoint, length) != 0),
                  "and a name in UTF-8, its friendly name, not its key");
            // Under Wine, the test daemon's source, whose friendly name is
            // longer than a name may be.
            if (named > 17 && memcmp(name, "Maud test source ", 17) == 0)
            {
                CHECK(IsCutSourceName(name, named), "a long friendly name cut, not dropped");
            }
            // An output says what spatial sound does there, with objects
            // exactly when a spatial format is on; Windows tracks no head.
            bool output = flows[f].direction == maud_directionOutput;
            CHECK(output ? info.spatializer != maud_spatializerUnknown &&
                               (info.spatialObjects > 0) == (info.spatializer == maud_spatializerOn)
                         : info.spatializer == maud_spatializerUnknown,
                  "and its spatializer's state");
            CHECK(!info.headTracking, "no head tracking");
        }
    }
    IMMDeviceEnumerator_Release(enumerator);
}

// A mono or stereo shared stream goes to the ears unvirtualized, so its
// mark is honored; with more channels the user's spatial format may
// take it, and the fate is unknown.
static void TestMark(maudContext* context)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.callback = CountBlocks;
    def.contentSpatialized = true;
    def.layout = maud_layoutStereo;
    maudStreamId stream = {0, 0};
    maudStreamStatus status = {0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStream_GetStatus(context, stream, &status) == maud_success &&
              status.spatialMark == maud_markHonored &&
              maudDestroyStream(context, stream) == maud_success,
          "a marked stereo stream's mark is honored");
    def.layout = maud_layout5Point1;
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStream_GetStatus(context, stream, &status) == maud_success &&
              status.spatialMark == maud_markUnknown &&
              maudDestroyStream(context, stream) == maud_success,
          "a marked 5.1 stream's fate is unknown");
}

static void TestOutputStream(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenStream(context, &def, &blocks);
    maudStreamFormat format;
    CHECK(maudStream_GetFormat(context, stream, &format) == maud_success &&
              format.sampleRate == 48000,
          "native at the engine's rate");
    // hygiene: gives a callback before the start 100 ms to show
    Pause(100);
    CHECK(atomic_load(&blocks.count) == 0, "nothing before start");
    CHECK(maudStream_Start(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 20), "blocks arrive");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(atomic_load(&blocks.onControl) == 0, "on a thread of the stream's");
    CHECK(atomic_load(&blocks.named) == 1, "named maud-wasapi");
    CHECK(Near(MeasureRate(context, stream), 48000.0), "at its rate");
    CHECK(StreamClockIsSound(context, stream, true, true, Pause),
          "its clock maps frames to host time");
    CHECK(maudStream_Stop(context, stream) == maud_success, "stop");
    uint32_t stopped = atomic_load(&blocks.count);
    // hygiene: gives a callback after the stop 100 ms to show
    Pause(100);
    CHECK(atomic_load(&blocks.count) == stopped, "no callbacks once stopped");
    CHECK(maudStream_Start(context, stream) == maud_success, "start again");
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
    CHECK(maudStream_Start(context, stream) == maud_success, "start converted");
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
    CHECK(maudStream_Start(context, stream) == maud_success, "start capture");
    CHECK(WaitForBlocks(context, &blocks, 20), "captured blocks arrive");
    CHECK(atomic_load(&blocks.withInput) == atomic_load(&blocks.count), "each with input");
    CHECK(atomic_load(&blocks.wrongSize) == 0, "in whole periods");
    CHECK(StreamClockIsSound(context, stream, false, true, Pause),
          "its clock maps frames to host time");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// A loopback stream on the default endpoint captures the tone an output
// stream plays there. Wine before 9.12 has no loopback, so this runs
// only with MAUD_WASAPI_LOOPBACK set, as the gate does under a Wine that
// has it.
static void TestLoopback(maudContext* context)
{
    const char* loopSwitch = getenv("MAUD_WASAPI_LOOPBACK");
    if (loopSwitch == nullptr || loopSwitch[0] == '\0')
    {
        return;
    }
    Blocks played = {.tone = true};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId tone = OpenStream(context, &def, &played);
    Blocks heard = {0};
    def = maudDefaultStreamDef();
    def.direction = maud_directionLoopback;
    maudStreamId loop = OpenStream(context, &def, &heard);
    CHECK(maudStream_Start(context, tone) == maud_success &&
              maudStream_Start(context, loop) == maud_success,
          "a tone and a loopback on the default endpoint");
    POLL_UNTIL(3000, "the loopback to hear the tone", (void)0, atomic_load(&heard.peak) >= 400);
    CHECK(atomic_load(&heard.peak) >= 400 && atomic_load(&heard.peak) <= 600,
          "the loopback captures the tone");
    CHECK(atomic_load(&heard.withInput) == atomic_load(&heard.count) &&
              atomic_load(&heard.wrongSize) == 0,
          "as input, in whole periods");
    CHECK(maudDestroyStream(context, tone) == maud_success, "the tone ends");
    // hygiene: lets the ended tone leave the loopback before the quiet window
    Pause(200);
    uint32_t before = atomic_load(&heard.count);
    atomic_store(&heard.peak, 0);
    // hygiene: a second of silence for the loopback to capture
    Pause(1000);
    uint32_t during = atomic_load(&heard.count) - before;
    printf("loopback in silence: %u blocks in 1 s, peak %u\n", during, atomic_load(&heard.peak));
    CHECK(during >= 50 && atomic_load(&heard.peak) < 10,
          "silent periods while the endpoint delivers them");
    CHECK(maudDestroyStream(context, loop) == maud_success, "destroy it");
    def = maudDefaultStreamDef();
    def.direction = maud_directionLoopback;
    def.callback = CountBlocks;
    def.loopbackExcludesSelf = true;
    CHECK(maudCreateStream(context, &def, &loop) == maud_errorUnsupported,
          "leaving the host out is not there yet");
}

// Wine's ntdll exports wine_get_version; Windows' does not.
static bool UnderWine(void);

// A callback that stalls leaves the engine's buffer empty: an underrun,
// counted when the stream next finds it so. Wine holds its buffer
// instead of letting it drain, so there the count is not checked.
static void TestUnderrun(maudContext* context)
{
    Blocks blocks = {.stallAt = 30};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenStream(context, &def, &blocks);
    CHECK(maudStream_Start(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 80), "past the stall");
    maudStreamStatus status = {0};
    CHECK(maudStream_GetStatus(context, stream, &status) == maud_success, "status");
    if (UnderWine())
    {
        printf("underruns after a stall under Wine: %llu\n", (unsigned long long)status.underruns);
    }
    else
    {
        CHECK(status.underruns >= 1, "the stall counted as an underrun");
    }
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// Wine's ntdll exports wine_get_version; Windows' does not.
static bool UnderWine(void)
{
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll != nullptr && GetProcAddress(ntdll, "wine_get_version") != nullptr;
}

// A capture asking for every part of voice processing runs as a
// communications stream. Windows 11 reports its effects; Wine has no
// effects manager, so nothing is reported there.
static void TestVoiceInput(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    def.voice = maud_voiceEchoCancellation | maud_voiceNoiseSuppression | maud_voiceGainControl;
    maudStreamId stream = OpenStream(context, &def, &blocks);
    CHECK(maudStream_Start(context, stream) == maud_success, "start a voice capture");
    CHECK(WaitForBlocks(context, &blocks, 20), "its blocks arrive");
    maudStreamStatus status = {0};
    CHECK(maudStream_GetStatus(context, stream, &status) == maud_success, "its status");
    if (UnderWine())
    {
        CHECK(!status.voiceReported && status.voiceActive == maud_voiceNone,
              "Wine reports no effects");
    }
    else
    {
        printf("voice processing reported %d, active %u\n", status.voiceReported ? 1 : 0,
               (unsigned)status.voiceActive);
    }
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// A stream on the default moves with it: WASAPI binds it to one
// endpoint, so it is opened again there and runs on.
static void TestMove(maudContext* context)
{
    maudDeviceId ids[8];
    uint32_t count = 0;
    maudDeviceId original = {0, 0};
    CHECK(maudContext_GetDevices(context, maud_directionOutput, ids, 8, &count) == maud_success,
          "list");
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral,
                                       &original) == maud_success,
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
    CHECK(maudStream_Start(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 10), "it plays");
    maudSetDefaultDevice(context, maud_roleGeneral, other);
    maudNotification record;
    bool moved = false;
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
        moved = moved ||
                (record.kind == maud_notifyStreamMoved && record.stream.index1 == stream.index1 &&
                 record.device.index1 == other.index1);
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

typedef struct Detach
{
    maudContext* context;
    maudStreamSlot* slot;
} Detach;

// Drops a stream's client, as a failed reopening leaves it.
static void DropClient(void* user)
{
    Detach* detach = user;
    maudWasapiDetachStream(detach->context, detach->slot);
}

// The drain opens a running stream again when its thread ended on a
// failure (an endpoint gone, as here made up) and when it has no client.
static void TestResume(maudContext* context)
{
    maudWasapi* wasapi = context->native;
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenStream(context, &def, &blocks);
    CHECK(maudStream_Start(context, stream) == maud_success, "start");
    CHECK(WaitForBlocks(context, &blocks, 10), "it plays");
    maudWasapiStream* entry = &wasapi->streams[stream.index1 - 1];
    atomic_store(&entry->failed, true);
    CHECK(WaitForBlocks(context, &blocks, atomic_load(&blocks.count) + 20) &&
              !atomic_load(&entry->failed),
          "a stream whose thread failed is opened again");
    Detach detach = {context, &context->streams.slots[stream.index1 - 1]};
    maudCallInWasapiApartment(&wasapi->apartment, DropClient, &detach);
    CHECK(entry->client == nullptr, "a stream left without its client");
    CHECK(WaitForBlocks(context, &blocks, atomic_load(&blocks.count) + 20) &&
              entry->client != nullptr,
          "is opened again too");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// Drains notifications until one of kind for stream arrives, with reason
// when it is a suspension, stopping at it. Whether one did.
static bool DrainToNotice(maudContext* context, maudStreamId stream, maudNotificationKind kind,
                          maudSuspendReason reason)
{
    maudNotification record;
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
        if (record.kind == kind && record.stream.index1 == stream.index1 &&
            record.stream.generation == stream.generation &&
            (kind != maud_notifyStreamSuspended || record.reason == reason))
        {
            return true;
        }
    }
    return false;
}

// Drains notifications for up to a second until one of kind for stream
// arrives, with reason when it is a suspension.
static bool NoticeFor(maudContext* context, maudStreamId stream, maudNotificationKind kind,
                      maudSuspendReason reason)
{
    bool arrived = false;
    POLL_UNTIL(1000, "a notification about the stream",
               arrived = DrainToNotice(context, stream, kind, reason), arrived);
    return arrived;
}

// The host suspends the context: the stream is suspended for the host,
// gives no callbacks, and plays again once the host resumes.
static void TestHostSuspended(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    maudStreamId stream = OpenStream(context, &def, &blocks);
    CHECK(maudStream_Start(context, stream) == maud_success && WaitForBlocks(context, &blocks, 10),
          "it plays");
    CHECK(maudContext_SetSuspended(context, true) == maud_success &&
              NoticeFor(context, stream, maud_notifyStreamSuspended, maud_suspendHost),
          "suspended for the host");
    // hygiene: lets callbacks in flight at the suspension end before counting
    Pause(100);
    uint32_t count = atomic_load(&blocks.count);
    // hygiene: gives a callback while suspended 300 ms to show
    Pause(300);
    CHECK(atomic_load(&blocks.count) == count, "no callbacks while suspended");
    CHECK(maudContext_SetSuspended(context, false) == maud_success &&
              NoticeFor(context, stream, maud_notifyStreamResumed, maud_suspendNone),
          "resumed");
    count = atomic_load(&blocks.count);
    CHECK(WaitForBlocks(context, &blocks, count + 20), "and plays");
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
    // Any property: the spatial format the user chose has no documented
    // key, and the drain posts only what changed.
    IMMNotificationClient_OnPropertyValueChanged(client, L"x", s_otherKey);
    CHECK(maudTakeWasapiChanges(&wasapi->notifier), "another property");
    IMMNotificationClient_OnPropertyValueChanged(client, L"x", s_formatKey);
    CHECK(maudTakeWasapiChanges(&wasapi->notifier), "the device format");
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

typedef struct Placed
{
    atomic_uint count;
    atomic_uint mostOffered;
} Placed;

// Plays a tone in the bed and in an active object to the right.
static void PlaceObject(const maudStreamBlock* block, void* user)
{
    Placed* placed = user;
    maudStreamObject* object = &block->objects[0];
    object->active = true;
    object->position.x = 2.0f;
    for (uint32_t i = 0; i < block->frameCount; ++i)
    {
        float value = (i & 32u) != 0 ? 0.1f : -0.1f;
        object->samples[i] = value;
        block->output[2 * i] = value;
        block->output[2 * i + 1] = value;
    }
    if (block->objectsAvailable > atomic_load(&placed->mostOffered))
    {
        atomic_store(&placed->mostOffered, block->objectsAvailable);
    }
    atomic_fetch_add(&placed->count, 1);
}

// An object stream runs on the endpoint's spatial render stream: the bed
// plays, and the objects offered are at most what the endpoint says it
// takes (none under Wine, or on Windows while no spatial format is on).
// A bed of a layout the spatial stream has no objects for is refused.
static void TestObjects(maudContext* context)
{
    maudDeviceId output = {0, 0};
    maudDeviceInfo info = {0};
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &output) ==
                  maud_success &&
              maudDevice_GetInfo(context, output, &info) == maud_success,
          "the default output");
    Placed placed = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.objectCount = 2;
    def.callback = PlaceObject;
    def.user = &placed;
    maudStreamId stream = {0, 0};
    maudResult result = maudCreateStream(context, &def, &stream);
    printf("object stream: %s, the endpoint takes %u objects\n", maudResultName(result),
           info.spatialObjects);
    CHECK(result == maud_success && maudStream_Start(context, stream) == maud_success,
          "an object stream runs");
    POLL_UNTIL(3000, "the object stream's blocks", (void)0, atomic_load(&placed.count) >= 20);
    CHECK(atomic_load(&placed.count) >= 20, "its callback runs");
    CHECK(atomic_load(&placed.mostOffered) <= info.spatialObjects,
          "offered no more objects than the endpoint takes");
    uint64_t position = 0;
    CHECK(maudStream_GetPosition(context, stream, &position) == maud_success && position > 0,
          "its clock advances");
    CHECK(maudStream_Stop(context, stream) == maud_success &&
              maudStream_Start(context, stream) == maud_success,
          "stops and starts again");
    uint32_t before = atomic_load(&placed.count);
    POLL_UNTIL(3000, "the object stream's blocks again", (void)0,
               atomic_load(&placed.count) >= before + 10);
    CHECK(atomic_load(&placed.count) >= before + 10, "and runs again");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
}

// Wine refuses exclusive use of every endpoint (its PulseAudio driver as
// unsupported, its ALSA one failing to open the device); Windows may give it, in
// which case the stream says so and plays, or refuse it as the device or
// the user's policy says. It never falls back to shared.
static void TestExclusive(maudContext* context)
{
    Blocks blocks = {0};
    maudStreamDef def = maudDefaultStreamDef();
    def.share = maud_shareExclusive;
    CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral,
                                       &def.device) == maud_success,
          "the default output");
    maudStreamId stream = {0, 0};
    def.callback = CountBlocks;
    def.user = &blocks;
    maudResult result = maudCreateStream(context, &def, &stream);
    printf("exclusive output: %s\n", maudResultName(result));
    if (UnderWine())
    {
        CHECK(result == maud_errorUnsupported || result == maud_errorPlatform,
              "Wine refuses exclusive use");
        return;
    }
    CHECK(result == maud_success || result == maud_errorUnsupported || result == maud_errorPlatform,
          "exclusive use is given or refused");
    if (result == maud_success)
    {
        maudStreamStatus status = {0};
        CHECK(maudStream_GetStatus(context, stream, &status) == maud_success && status.exclusive,
              "and the stream says so");
        CHECK(maudStream_Start(context, stream) == maud_success &&
                  WaitForBlocks(context, &blocks, 20),
              "it plays");
        CHECK(maudDestroyStream(context, stream) == maud_success, "destroy");
    }
}

int main(void)
{
    s_control = GetCurrentThreadId();
    const char* staSwitch = getenv("MAUD_WASAPI_STA");
    bool sta = staSwitch != nullptr && staSwitch[0] != '\0';
    if (sta)
    {
        APTTYPE type = APTTYPE_MTA;
        APTTYPEQUALIFIER qualifier = APTTYPEQUALIFIER_NONE;
        CHECK(SUCCEEDED(OleInitialize(nullptr)) &&
                  SUCCEEDED(CoGetApartmentType(&type, &qualifier)) &&
                  (type == APTTYPE_STA || type == APTTYPE_MAINSTA),
              "the test's thread in a single-threaded apartment");
    }
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
        if (sta)
        {
            OleUninitialize();
        }
        return getenv("MAUD_REQUIRE_WASAPI") != nullptr ? 1 : PlatformAbsent("no audio endpoint");
    }
    TestApartment(context);
    TestDevices(context);
    TestMark(context);
    TestOutputStream(context);
    TestInputStream(context);
    TestLoopback(context);
    TestVoiceInput(context);
    TestUnderrun(context);
    TestExclusive(context);
    TestObjects(context);
    TestMove(context);
    TestResume(context);
    TestHostSuspended(context);
    TestNotifier(context);
    TestDrainRescans(context);
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    if (sta)
    {
        OleUninitialize();
    }
    return s_failures == 0 ? 0 : 1;
}
