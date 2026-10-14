// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every refusal context.h, stream.h, device.h, notification.h and
// focus.h document, each made on its own on the offline backend: a
// context def or stream def with one field out of range, each
// maud_errorInvalid with nothing made; the device queries, the
// notification queue, the host's lifecycle and focus calls without a
// context, without somewhere to write, or with a direction or role out
// of range.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/focus.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <stdlib.h>

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    return malloc(size);
}

static void Silence(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

static maudContextDef Offline(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    return def;
}

static void TestContextDefs(void)
{
    maudContextDef def = Offline();
    maudContext* context = (maudContext*)&context;
    CHECK(maudCreateContext(nullptr, &context) == maud_errorInvalid && context == nullptr,
          "no def");
    CHECK(maudCreateContext(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    static int s_vm;
    maudContextDef bad[11];
    for (int i = 0; i < COUNT(bad); ++i)
    {
        bad[i] = def;
    }
    bad[0].cookie = 0;
    bad[1].allocator = (maudAllocator){Alloc, nullptr, nullptr};
    bad[2].limits.streams = 0;
    bad[3].limits.periodFrames = 0;
    bad[4].limits.devices = 0;
    bad[5].limits.notifications = 1;
    bad[6].limits.deviceTextBytes = 0;
    bad[7].offlineSampleRate = 7999;
    bad[8].offlineSampleRate = 384001;
    bad[9].androidJavaVm = &s_vm;
    bad[10].backend = maud_backendPrivate + 1;
    bool refused = true;
    for (int i = 0; i < COUNT(bad); ++i)
    {
        context = (maudContext*)&context;
        bool one = maudCreateContext(&bad[i], &context) == maud_errorInvalid && context == nullptr;
        if (!one)
        {
            printf("context def %d not refused\n", i);
            if (context != nullptr)
            {
                CHECK(maudDestroyContext(context) == maud_success, "destroyed");
            }
        }
        refused = refused && one;
    }
    CHECK(refused, "a context def field out of range");
    maudContextDef bounds[3] = {def, def, def};
    bounds[0].offlineSampleRate = 8000;
    bounds[1].offlineSampleRate = 384000;
    bounds[2].limits.notifications = 2;
    bool taken = true;
    for (int i = 0; i < COUNT(bounds); ++i)
    {
        bool one = maudCreateContext(&bounds[i], &context) == maud_success;
        taken = taken && one && maudDestroyContext(context) == maud_success;
    }
    CHECK(taken, "a context def's bounds taken");
    CHECK(maudDestroyContext(nullptr) == maud_success, "destroying NULL does nothing");
    CHECK(maudResumeContext(nullptr) == maud_errorInvalid &&
              maudSetContextSuspended(nullptr, true) == maud_errorInvalid,
          "resume and suspend without a context");
}

static void TestStreamDefs(void)
{
    maudContextDef contextDef = Offline();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&contextDef, &context) == maud_success, "a context");
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.callback = Silence;
    maudStreamId stream = {7, 7};
    maudStreamDef bad[10];
    for (int i = 0; i < COUNT(bad); ++i)
    {
        bad[i] = def;
    }
    bad[0].cookie = 0;
    bad[1].callback = nullptr;
    bad[2].direction = maud_directionDuplex + 1;
    bad[3].mode = maud_modePull + 1;
    bad[4].ratePolicy = maud_ratePlatformConverted + 1;
    bad[5].role = maud_roleCommunications + 1;
    bad[6].layout = maud_layoutNone;
    bad[7].direction = maud_directionInput;
    bad[7].voice = 8;
    bad[8].voice = maud_voiceNoiseSuppression;
    bad[9].direction = maud_directionInput;
    bad[9].voice = maud_voiceGainControl | 16;
    bool refused = true;
    for (int i = 0; i < COUNT(bad); ++i)
    {
        stream = (maudStreamId){7, 7};
        bool one =
            maudCreateStream(context, &bad[i], &stream) == maud_errorInvalid && stream.index1 == 0;
        if (!one)
        {
            printf("stream def %d not refused\n", i);
            (void)maudDestroyStream(context, stream);
        }
        refused = refused && one;
    }
    CHECK(refused, "a stream def field out of range, or voice processing on an output");
    def.periodFrames = contextDef.limits.periodFrames + 1;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorCapacity && stream.index1 == 0,
          "a period past the context's limit");
    def.periodFrames = contextDef.limits.periodFrames;
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudDestroyStream(context, stream) == maud_success,
          "a period at the limit taken");
    CHECK(maudDestroyContext(context) == maud_success, "destroyed");
}

static void TestQueries(void)
{
    maudContextDef def = Offline();
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "a context");
    maudDeviceId device = {0, 0};
    uint32_t count = 0;
    maudDeviceId ids[2];
    CHECK(maudGetDevices(nullptr, maud_directionOutput, ids, 2, &count) == maud_errorInvalid &&
              maudGetDevices(context, maud_directionOutput, ids, 2, nullptr) == maud_errorInvalid &&
              maudGetDevices(context, maud_directionOutput, nullptr, 2, &count) ==
                  maud_errorInvalid &&
              maudGetDevices(context, maud_directionDuplex, ids, 2, &count) == maud_errorInvalid,
          "devices without a context, a count, an array for the capacity or a single direction");
    CHECK(maudGetDevices(context, maud_directionOutput, nullptr, 0, &count) == maud_success &&
              count > 0,
          "the count alone");
    CHECK(maudGetDefaultDevice(nullptr, maud_directionOutput, maud_roleGeneral, &device) ==
                  maud_errorInvalid &&
              maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, nullptr) ==
                  maud_errorInvalid &&
              maudGetDefaultDevice(context, maud_directionDuplex, maud_roleGeneral, &device) ==
                  maud_errorInvalid &&
              maudGetDefaultDevice(context, maud_directionOutput, maud_roleCommunications + 1,
                                   &device) == maud_errorInvalid,
          "a default without a context, somewhere to write, a direction or a role");
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &device) ==
              maud_success,
          "the default output");
    maudDeviceInfo info;
    CHECK(maudGetDeviceInfo(nullptr, device, &info) == maud_errorInvalid &&
              maudGetDeviceInfo(context, device, nullptr) == maud_errorInvalid,
          "info without a context or somewhere to write");
    char text[64];
    size_t length = 0;
    maudDeviceId gone = {device.index1, device.generation + 1};
    CHECK(maudGetDeviceName(nullptr, device, text, sizeof(text), &length) == maud_errorInvalid &&
              maudGetDeviceName(context, device, text, sizeof(text), nullptr) ==
                  maud_errorInvalid &&
              maudGetDeviceName(context, device, nullptr, sizeof(text), &length) ==
                  maud_errorInvalid &&
              maudGetDeviceName(context, gone, text, sizeof(text), &length) == maud_errorStale,
          "a name without a context, a length, bytes for the capacity, or of a stale device");
    CHECK(maudGetDeviceKey(nullptr, device, text, sizeof(text), &length) == maud_errorInvalid &&
              maudGetDeviceKey(context, device, text, sizeof(text), nullptr) == maud_errorInvalid &&
              maudGetDeviceKey(context, device, nullptr, sizeof(text), &length) ==
                  maud_errorInvalid &&
              maudGetDeviceKey(context, gone, text, sizeof(text), &length) == maud_errorStale,
          "a key the same");
    CHECK(maudGetDeviceName(context, device, nullptr, 0, &length) == maud_errorCapacity &&
              length > 0,
          "no room for a name: its length, as a capacity error");
    maudNotification record;
    CHECK(maudNextNotification(nullptr, &record) == maud_errorInvalid &&
              maudNextNotification(context, nullptr) == maud_errorInvalid,
          "a notification without a context or somewhere to write");
    CHECK(maudRequestFocus(nullptr, maud_focusLasting, maud_roleGeneral) == maud_errorInvalid &&
              maudRequestFocus(context, maud_focusBriefMixed + 1, maud_roleGeneral) ==
                  maud_errorInvalid &&
              maudRequestFocus(context, maud_focusLasting, maud_roleCommunications + 1) ==
                  maud_errorInvalid,
          "focus without a context, a known request or a role");
    CHECK(maudRequestFocus(context, maud_focusLasting, maud_roleGeneral) == maud_errorUnsupported,
          "focus where the platform has none");
    CHECK(maudDestroyContext(context) == maud_success, "destroyed");
}

int main(void)
{
    TestContextDefs();
    TestStreamDefs();
    TestQueries();
    return s_failures == 0 ? 0 : 1;
}
