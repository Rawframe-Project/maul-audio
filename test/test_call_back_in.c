// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Host code the context runs calling back into it, on the offline
// backend: from a stream's callback and from the context's allocator,
// every call that changes the context is refused with maud_errorState
// and counted as misuse, rendering the stream again from its own
// callback included, and the outer call goes on as if nothing happened.
// The calls the callback may make (the stream queries, the misuse count,
// the host clock, rendering another stream) work from it.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/focus.h"
#include "maul-audio/notification.h"
#include "maul-audio/offline.h"
#include "maul-audio/stream.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define PERIOD 64u
// The calls TryAll makes.
#define CALLS 16

static maudContext* s_context;
static maudStreamId s_self;
static maudStreamId s_other;
static maudDeviceId s_device;
// Whether the next callback of s_self, or the next allocator call, calls
// back in; what it found.
static bool s_fromCallback;
static bool s_fromAllocator;
static int s_refused;
static bool s_allowed;

// Makes every call that changes the context, each one expected to be
// refused; returns how many were.
static int TryAll(void)
{
    int refused = 0;
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    maudStreamId stream = {0, 0};
    maudNotification record;
    maudOfflineDeviceDef deviceDef = maudDefaultOfflineDeviceDef();
    maudDeviceId device = {0, 0};
    float frames[2 * PERIOD];
    refused += maudCreateStream(s_context, &def, &stream) == maud_errorState;
    refused += maudDestroyStream(s_context, s_self) == maud_errorState;
    refused += maudStartStream(s_context, s_self) == maud_errorState;
    refused += maudStopStream(s_context, s_self) == maud_errorState;
    refused += maudRenderStream(s_context, s_self, frames, PERIOD) == maud_errorState;
    refused += maudResumeContext(s_context) == maud_errorState;
    refused += maudSetContextSuspended(s_context, true) == maud_errorState;
    refused += maudNextNotification(s_context, &record) == maud_errorState;
    refused += maudRequestFocus(s_context, maud_focusLasting, maud_roleGeneral) == maud_errorState;
    refused += maudAddOfflineDevice(s_context, &deviceDef, &device) == maud_errorState;
    refused += maudRemoveOfflineDevice(s_context, s_device) == maud_errorState;
    refused +=
        maudSetOfflineDefaultDevice(s_context, maud_roleGeneral, s_device) == maud_errorState;
    refused +=
        maudSetOfflineDeviceForm(s_context, s_device, maud_formHeadphones) == maud_errorState;
    refused +=
        maudSetOfflineDeviceFormat(s_context, s_device, maud_layoutMono, 44100) == maud_errorState;
    refused += maudSetOfflineDeviceSpatializer(s_context, s_device, maud_spatializerUnknown, false,
                                               0) == maud_errorState;
    refused += maudDestroyContext(s_context) == maud_errorState;
    return refused;
}

// The calls the callback may make.
static bool TryAllowed(void)
{
    maudStreamFormat format;
    uint64_t frames = 0;
    maudStreamClock clock;
    float other[2 * PERIOD];
    return maudGetStreamFormat(s_context, s_self, &format) == maud_success &&
           maudGetStreamPosition(s_context, s_self, &frames) == maud_success &&
           maudGetStreamClock(s_context, s_self, &clock) == maud_success &&
           maudGetHostNanoseconds() > 0 && maudGetContextMisuse(s_context) > 0 &&
           maudRenderStream(s_context, s_other, other, PERIOD) == maud_success;
}

static void Fill(const maudStreamBlock* block, void* user)
{
    if (user == &s_self && s_fromCallback)
    {
        s_fromCallback = false;
        s_refused = TryAll();
        s_allowed = TryAllowed();
    }
    memset(block->output, 0, (size_t)block->frameCount * 2 * sizeof(float));
}

// Aligned by hand: Windows' C library has no aligned_alloc.
static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    if (s_fromAllocator)
    {
        s_fromAllocator = false;
        s_refused = TryAll();
    }
    unsigned char* raw = malloc(size + alignment + sizeof(void*));
    if (raw == nullptr)
    {
        return nullptr;
    }
    uintptr_t start = (uintptr_t)(raw + sizeof(void*));
    unsigned char* aligned =
        (unsigned char*)((start + alignment - 1) & ~(uintptr_t)(alignment - 1));
    memcpy(aligned - sizeof(void*), &raw, sizeof(void*));
    return aligned;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    if (s_fromAllocator)
    {
        s_fromAllocator = false;
        s_refused = TryAll();
    }
    void* raw;
    memcpy(&raw, (unsigned char*)memory - sizeof(void*), sizeof(void*));
    free(raw);
}

static maudStreamId Open(void* user)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.periodFrames = PERIOD;
    def.callback = Fill;
    def.user = user;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(s_context, &def, &stream) == maud_success &&
              maudStartStream(s_context, stream) == maud_success,
          "a running stream");
    return stream;
}

static void TestFromCallback(void)
{
    uint64_t before = maudGetContextMisuse(s_context);
    float frames[2 * PERIOD];
    s_fromCallback = true;
    s_refused = 0;
    CHECK(maudRenderStream(s_context, s_self, frames, PERIOD) == maud_success,
          "the outer render goes on");
    CHECK(s_refused == CALLS, "every call that changes the context refused from the callback");
    CHECK(maudGetContextMisuse(s_context) == before + CALLS, "each counted once");
    CHECK(s_allowed, "the calls it may make work");
}

static void TestFromAllocator(void)
{
    uint64_t before = maudGetContextMisuse(s_context);
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.periodFrames = PERIOD;
    def.callback = Fill;
    maudStreamId stream = {0, 0};
    s_fromAllocator = true;
    s_refused = 0;
    CHECK(maudCreateStream(s_context, &def, &stream) == maud_success, "the outer creation goes on");
    CHECK(s_refused == CALLS, "every call refused from the allocator making a stream");
    s_fromAllocator = true;
    s_refused = 0;
    CHECK(maudDestroyStream(s_context, stream) == maud_success, "the outer destruction goes on");
    CHECK(s_refused == CALLS, "and from the allocator releasing it");
    CHECK(maudGetContextMisuse(s_context) == before + 2 * CALLS, "each counted once");
    float frames[2 * PERIOD];
    CHECK(maudRenderStream(s_context, s_self, frames, PERIOD) == maud_success,
          "the context renders as before");
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    def.allocator = (maudAllocator){Alloc, Free, nullptr};
    CHECK(maudCreateContext(&def, &s_context) == maud_success, "context");
    CHECK(maudGetDefaultDevice(s_context, maud_directionOutput, maud_roleGeneral, &s_device) ==
              maud_success,
          "the default output");
    s_self = Open(&s_self);
    s_other = Open(&s_other);
    TestFromCallback();
    TestFromAllocator();
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy the context");
    return s_failures == 0 ? 0 : 1;
}
