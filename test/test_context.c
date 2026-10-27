// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Contexts: defaults, refusals of invalid defs (and the offline rate's
// bounds taken), the native backend's absence in this build, audio
// focus where the backend has none, memory through the def's allocator,
// and the largest limits refused as capacity.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/focus.h"
#include "maul-audio/notification.h"

#include <stdlib.h>

typedef struct CountingAllocator
{
    int live;
    int calls;
    bool fail;
    // Requests past this many bytes are refused when it is not 0, and the
    // largest request is kept.
    size_t refuseAbove;
    size_t largest;
} CountingAllocator;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    CountingAllocator* counter = context;
    counter->calls++;
    counter->largest = size > counter->largest ? size : counter->largest;
    if (counter->fail || (counter->refuseAbove != 0 && size > counter->refuseAbove))
    {
        return nullptr;
    }
    counter->live++;
    return malloc(size);
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    CountingAllocator* counter = context;
    counter->live--;
    free(memory);
}

static maudContextDef OfflineDef(CountingAllocator* counter)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, counter};
    return def;
}

static void TestDefaults(void)
{
    maudContextDef def = maudDefaultContextDef();
    CHECK(def.limits.streams == 8, "8 streams");
    CHECK(def.limits.periodFrames == 8192, "8192-frame periods");
    CHECK(def.backend == maud_backendNative, "native backend");
    CHECK(def.offlineSampleRate == 48000, "48 kHz offline");
    CHECK(def.allocator.alloc == nullptr && def.allocator.free == nullptr, "C allocator");
    CHECK(def.cookie == MAUD_CONTEXT_DEF_COOKIE, "the cookie");
    CHECK(def.version == MAUD_ABI_VERSION, "the headers' binary interface");
}

static void TestOfflineContextLifetime(void)
{
    CountingAllocator counter = {0};
    maudContextDef def = OfflineDef(&counter);
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "create");
    CHECK(context != nullptr, "context returned");
    CHECK(maudGetContextBackend(context) == maud_backendOffline, "offline backend");
    CHECK(maudGetContextMisuse(context) == 0, "no misuse");
    CHECK(maudResumeContext(context) == maud_success, "resume where no policy holds audio");
    CHECK(maudResumeContext(nullptr) == maud_errorInvalid, "resume NULL");
    CHECK(counter.live == 1, "one block");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    CHECK(counter.live == 0, "block returned");
    CHECK(maudDestroyContext(nullptr) == maud_success, "destroying NULL does nothing");
}

static void TestNativeContextOrUnsupported(void)
{
    // Whether a native backend answers depends on the build and the
    // machine; either outcome must be whole.
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = (maudContext*)&def;
    maudResult result = maudCreateContext(&def, &context);
    CHECK(result == maud_success || result == maud_errorUnsupported, "native or unsupported");
    if (result == maud_success)
    {
        CHECK(maudGetContextBackend(context) != maud_backendNative, "the backend it chose");
        CHECK(maudDestroyContext(context) == maud_success, "destroy");
    }
    else
    {
        CHECK(context == nullptr, "context cleared");
    }
}

static void CheckRefused(const maudContextDef* def, const char* what)
{
    maudContext* context = (maudContext*)def;
    CHECK(maudCreateContext(def, &context) == maud_errorInvalid, what);
    CHECK(context == nullptr, what);
}

static void TestInvalidDefsAreRefused(void)
{
    CountingAllocator counter = {0};
    maudContextDef def = OfflineDef(&counter);
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, nullptr) == maud_errorInvalid, "null out");
    CheckRefused(nullptr, "null def");
    def.cookie = 0;
    CheckRefused(&def, "no cookie");
    def = OfflineDef(&counter);
    def.allocator.free = nullptr;
    CheckRefused(&def, "allocator with one function");
    def = OfflineDef(&counter);
    def.limits.streams = 0;
    CheckRefused(&def, "no streams");
    def = OfflineDef(&counter);
    def.limits.periodFrames = 0;
    CheckRefused(&def, "no period frames");
    def.limits.periodFrames = MAUD_MAX_PERIOD_FRAMES + 1;
    CheckRefused(&def, "period frames past the largest");
    maudContextDef largest = maudDefaultContextDef();
    largest.backend = maud_backendOffline;
    largest.limits.periodFrames = MAUD_MAX_PERIOD_FRAMES;
    CHECK(maudCreateContext(&largest, &context) == maud_success &&
              maudDestroyContext(context) == maud_success,
          "the largest period frames taken");
    def = OfflineDef(&counter);
    def.offlineSampleRate = 7999;
    CheckRefused(&def, "rate below range");
    def.offlineSampleRate = 384001;
    CheckRefused(&def, "rate above range");
    // Either end of the range is taken (made through the C library, so
    // the counter stays at nothing allocated).
    const uint32_t ends[2] = {8000, 384000};
    for (int i = 0; i < 2; ++i)
    {
        maudContextDef edge = maudDefaultContextDef();
        edge.backend = maud_backendOffline;
        edge.offlineSampleRate = ends[i];
        maudContext* made = nullptr;
        CHECK(maudCreateContext(&edge, &made) == maud_success &&
                  maudDestroyContext(made) == maud_success,
              "a rate at either end");
    }
    def = OfflineDef(&counter);
    def.backend = 200;
    CheckRefused(&def, "unknown backend");
    def.backend = maud_backendPrivate + 1;
    CheckRefused(&def, "one past the last backend");
    // The last backends are known: made where they are built,
    // unsupported elsewhere, never refused as invalid.
    const maudBackendKind last[2] = {maud_backendWeb, maud_backendPrivate};
    for (int i = 0; i < 2; ++i)
    {
        maudContextDef known = maudDefaultContextDef();
        known.backend = last[i];
        maudContext* made = nullptr;
        maudResult result = maudCreateContext(&known, &made);
        CHECK(result == maud_success || result == maud_errorUnsupported, "a known backend");
        CHECK(made == nullptr || maudDestroyContext(made) == maud_success, "destroyed");
    }
    def = OfflineDef(&counter);
    def.androidJavaVm = &counter;
    CheckRefused(&def, "a Java VM without an Android Context");
    def = OfflineDef(&counter);
    def.androidContext = &counter;
    CheckRefused(&def, "an Android Context without a Java VM");
    CHECK(counter.calls == 0, "nothing allocated");
    (void)context;
}

// A def of another interface is refused before anything else of it
// is read; one without the cookie is not a def at all.
static void TestOtherVersionIsRefused(void)
{
    CountingAllocator counter = {0};
    const uint32_t others[4] = {MAUD_ABI_VERSION + 1, MAUD_ABI_VERSION - 1,
                                MAUD_ABI_VERSION + (1u << 16), UINT32_MAX};
    for (int i = 0; i < 4; ++i)
    {
        maudContextDef def = OfflineDef(&counter);
        def.version = others[i];
        def.limits.streams = 0;
        maudContext* context = (maudContext*)&def;
        CHECK(maudCreateContext(&def, &context) == maud_errorVersion, "another version");
        CHECK(context == nullptr, "no context");
        def.cookie = 0;
        CheckRefused(&def, "another version without the cookie");
    }
    CHECK(counter.calls == 0, "nothing allocated");
}

static void TestAllocatorFailureIsCapacity(void)
{
    CountingAllocator counter = {.fail = true};
    maudContextDef def = OfflineDef(&counter);
    maudContext* context = (maudContext*)&def;
    CHECK(maudCreateContext(&def, &context) == maud_errorCapacity, "capacity");
    CHECK(context == nullptr, "context cleared");
    CHECK(counter.calls == 1 && counter.live == 0, "nothing kept");
}

// The largest limits ask for more device text than a 32-bit address
// space holds: refused as capacity, either before allocating or by the
// allocator asked for all of it, never with a smaller block.
static void TestLargestLimitsAreCapacity(void)
{
    CountingAllocator counter = {.refuseAbove = (size_t)1 << 28};
    maudContextDef def = OfflineDef(&counter);
    def.limits.streams = UINT16_MAX;
    def.limits.devices = UINT16_MAX;
    def.limits.notifications = UINT16_MAX;
    def.limits.deviceTextBytes = UINT16_MAX;
    maudContext* context = (maudContext*)&def;
    CHECK(maudCreateContext(&def, &context) == maud_errorCapacity, "capacity");
    CHECK(context == nullptr && counter.live == 0, "nothing kept");
    uint64_t text = 2u * (uint64_t)UINT16_MAX * UINT16_MAX;
    CHECK(counter.calls == 0 || counter.largest >= text, "no block smaller than the text");
}

// The offline backend has no audio focus: requests are unsupported,
// the state stays none and no record comes; bad arguments are refused.
static void TestFocusUnsupported(void)
{
    CountingAllocator counter = {0};
    maudContextDef def = OfflineDef(&counter);
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "create");
    maudFocus focus = maud_focusHeld;
    CHECK(maudGetContextFocus(context, &focus) == maud_success && focus == maud_focusNone,
          "no focus at first");
    CHECK(maudRequestFocus(context, maud_focusLasting, maud_roleGeneral) == maud_errorUnsupported,
          "no focus offline");
    CHECK(maudRequestFocus(context, maud_focusRelease, maud_roleGeneral) == maud_errorUnsupported,
          "nothing to release");
    CHECK(maudRequestFocus(nullptr, maud_focusLasting, maud_roleGeneral) == maud_errorInvalid,
          "no context");
    CHECK(maudRequestFocus(context, maud_focusBriefMixed + 1, maud_roleGeneral) ==
              maud_errorInvalid,
          "an unknown request");
    CHECK(maudRequestFocus(context, maud_focusLasting, maud_roleCommunications + 1) ==
              maud_errorInvalid,
          "an unknown role");
    CHECK(maudGetContextFocus(context, nullptr) == maud_errorInvalid &&
              maudGetContextFocus(nullptr, &focus) == maud_errorInvalid,
          "NULL pointers");
    maudNotification record;
    bool focusRecord = false;
    while (maudNextNotification(context, &record) == maud_success)
    {
        focusRecord = focusRecord || record.kind == maud_notifyFocusChanged;
    }
    CHECK(!focusRecord, "no focus record");
    CHECK(maudGetContextMisuse(context) == 0, "no misuse");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

int main(void)
{
    TestDefaults();
    TestOfflineContextLifetime();
    TestNativeContextOrUnsupported();
    TestInvalidDefsAreRefused();
    TestOtherVersionIsRefused();
    TestFocusUnsupported();
    TestAllocatorFailureIsCapacity();
    TestLargestLimitsAreCapacity();
    return s_failures == 0 ? 0 : 1;
}
