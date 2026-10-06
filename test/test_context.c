// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Contexts: defaults, refusals of invalid defs, the native backend's
// absence in this build, audio focus where the backend has none, and
// memory through the def's allocator.

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
} CountingAllocator;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    CountingAllocator* counter = context;
    counter->calls++;
    if (counter->fail)
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
    def = OfflineDef(&counter);
    def.offlineSampleRate = 7999;
    CheckRefused(&def, "rate below range");
    def.offlineSampleRate = 384001;
    CheckRefused(&def, "rate above range");
    def = OfflineDef(&counter);
    def.backend = 200;
    CheckRefused(&def, "unknown backend");
    def = OfflineDef(&counter);
    def.androidJavaVm = &counter;
    CheckRefused(&def, "a Java VM without an Android Context");
    def = OfflineDef(&counter);
    def.androidContext = &counter;
    CheckRefused(&def, "an Android Context without a Java VM");
    CHECK(counter.calls == 0, "nothing allocated");
    (void)context;
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
    TestFocusUnsupported();
    TestAllocatorFailureIsCapacity();
    return s_failures == 0 ? 0 : 1;
}
