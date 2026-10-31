// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A private backend built from outside the library (test/private_backend,
// through MAUL_AUDIO_PRIVATE_BACKEND): asked for by its kind, it opens
// with its device and plays a stream the caller renders; asked for as
// the native backend, it comes first. A stream it holds memory for is
// stale before that memory goes back to the allocator, so a free that
// reads it back finds it gone.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/stream.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int s_blocks;
static maudContext* s_context;
// A stream every free reads back while armed, after freeing its block,
// and whether any read found it still there.
static maudStreamId s_readBack;
static bool s_readArmed;
static int s_reads;
static bool s_foundLive;

// Aligned by hand: Windows' C library has no aligned_alloc.
static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
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
    void* raw;
    memcpy(&raw, (unsigned char*)memory - sizeof(void*), sizeof(void*));
    free(raw);
    // As a free that reports what it released might.
    if (s_readArmed)
    {
        maudStreamStatus status;
        s_reads++;
        s_foundLive =
            s_foundLive || maudStream_GetStatus(s_context, s_readBack, &status) != maud_errorStale;
    }
}

static void Count(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
    s_blocks++;
}

// The backend gives its block back first, then the library its
// samples: every free during the destruction must find the stream stale.
static void TestReadBack(void)
{
    maudContextDef cd = maudDefaultContextDef();
    cd.backend = maud_backendPrivate;
    cd.allocator = (maudAllocator){Alloc, Free, nullptr};
    CHECK(maudCreateContext(&cd, &s_context) == maud_success, "a context on the private backend");
    if (s_context == nullptr)
    {
        return;
    }
    maudStreamDef sd = maudDefaultStreamDef();
    sd.mode = maud_modePull;
    sd.callback = Count;
    CHECK(maudCreateStream(s_context, &sd, &s_readBack) == maud_success, "a stream to destroy");
    s_readArmed = true;
    s_reads = 0;
    s_foundLive = false;
    CHECK(maudDestroyStream(s_context, s_readBack) == maud_success, "destroyed");
    s_readArmed = false;
    CHECK(s_reads == 2 && !s_foundLive,
          "both frees, the backend's block and the samples, find the stream stale");
    CHECK(maudDestroyContext(s_context) == maud_success, "destroy the context");
    s_context = nullptr;
}

int main(void)
{
    const maudBackendKind kinds[2] = {maud_backendPrivate, maud_backendNative};
    for (int i = 0; i < 2; ++i)
    {
        maudContextDef cd = maudDefaultContextDef();
        cd.backend = kinds[i];
        maudContext* context = nullptr;
        CHECK(maudCreateContext(&cd, &context) == maud_success && context != nullptr &&
                  maudContext_GetBackend(context) == maud_backendPrivate,
              "the private backend, by kind and first as native");
        if (context == nullptr)
        {
            return 1;
        }
        maudDeviceInfo info = {0};
        maudDeviceId device = {0, 0};
        CHECK(maudContext_GetDefaultDevice(context, maud_directionOutput, maud_roleGeneral,
                                           &device) == maud_success &&
                  maudDevice_GetInfo(context, device, &info) == maud_success &&
                  info.nativeSampleRate == 48000,
              "its device");
        maudStreamDef sd = maudDefaultStreamDef();
        sd.mode = maud_modePull;
        sd.callback = Count;
        maudStreamId stream = {0, 0};
        float frames[2 * 960];
        s_blocks = 0;
        CHECK(maudCreateStream(context, &sd, &stream) == maud_success &&
                  maudStream_Start(context, stream) == maud_success &&
                  maudStream_Render(context, stream, frames, 960) == maud_success && s_blocks == 2,
              "a stream the caller renders, two periods of 480");
        CHECK(maudDestroyContext(context) == maud_success, "destroy");
    }
    TestReadBack();
    return s_failures == 0 ? 0 : 1;
}
