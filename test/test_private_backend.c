// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A private backend built from outside the library (test/private_backend,
// through MAUL_AUDIO_PRIVATE_BACKEND): asked for by its kind, it opens
// with its device and plays a stream the caller renders; asked for as
// the native backend, it comes first.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/stream.h"

static int s_blocks;

static void Count(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
    s_blocks++;
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
                  maudGetContextBackend(context) == maud_backendPrivate,
              "the private backend, by kind and first as native");
        if (context == nullptr)
        {
            return 1;
        }
        maudDeviceInfo info = {0};
        maudDeviceId device = {0, 0};
        CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &device) ==
                      maud_success &&
                  maudGetDeviceInfo(context, device, &info) == maud_success &&
                  info.nativeSampleRate == 48000,
              "its device");
        maudStreamDef sd = maudDefaultStreamDef();
        sd.mode = maud_modePull;
        sd.callback = Count;
        maudStreamId stream = {0, 0};
        float frames[2 * 960];
        s_blocks = 0;
        CHECK(maudCreateStream(context, &sd, &stream) == maud_success &&
                  maudStartStream(context, stream) == maud_success &&
                  maudRenderStream(context, stream, frames, 960) == maud_success && s_blocks == 2,
              "a stream the caller renders, two periods of 480");
        CHECK(maudDestroyContext(context) == maud_success, "destroy");
    }
    return s_failures == 0 ? 0 : 1;
}
