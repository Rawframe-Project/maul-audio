// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening a stream on a backend that gives exclusive use (whitebox, the
// offline backend's table copied as one that does): an exclusive stream
// at the device's own rate opens; one asking the platform to convert
// its rate is unsupported, exclusive use having no converter.

#include "backend.h"
#include "context.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/stream.h"

static void Ignore(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

int main(void)
{
    maudContextDef cd = maudDefaultContextDef();
    cd.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&cd, &context) == maud_success, "an offline context");
    if (context == nullptr)
    {
        return 1;
    }
    const maudBackend* offline = context->backend;
    maudBackend exclusive = *offline;
    exclusive.exclusive = true;
    context->backend = &exclusive;
    maudStreamDef sd = maudDefaultStreamDef();
    sd.mode = maud_modePull;
    sd.callback = Ignore;
    sd.share = maud_shareExclusive;
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &sd.device) ==
              maud_success,
          "the device");
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &sd, &stream) == maud_success, "exclusive at the native rate");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroyed");
    sd.ratePolicy = maud_ratePlatformConverted;
    sd.sampleRate = 44100;
    CHECK(maudCreateStream(context, &sd, &stream) == maud_errorUnsupported,
          "exclusive with the platform converting: unsupported");
    context->backend = offline;
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
    return s_failures == 0 ? 0 : 1;
}
