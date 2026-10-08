// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Syncing the device table with a backend's list (whitebox): when one
// device's native rate changes and the others' do not, a stream at the
// native rate on it takes the new rate and says so.

#include "context.h"
#include "device.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/notification.h"
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
    maudStreamDef sd = maudDefaultStreamDef();
    sd.mode = maud_modePull;
    sd.callback = Ignore;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &sd, &stream) == maud_success, "a stream at the native rate");
    // The table as it stands, the output's rate changed to 44.1 kHz.
    maudDeviceSpec specs[8];
    uint32_t count = 0;
    for (uint32_t i = 0; i < context->devices.capacity && count < 8; ++i)
    {
        const maudDeviceSlot* slot = &context->devices.slots[i];
        if (slot->live)
        {
            specs[count] = (maudDeviceSpec){slot->info, slot->name.bytes, slot->name.length,
                                            slot->key.bytes, slot->key.length};
            if (slot->info.direction == maud_directionOutput)
            {
                specs[count].info.nativeSampleRate = 44100;
                specs[count].info.minSampleRate = 44100;
            }
            count++;
        }
    }
    CHECK(count >= 2, "an output and an input");
    maudNotification record;
    while (maudNextNotification(context, &record) == maud_success)
    {
    }
    CHECK(maudSyncDevices(context, specs, count, nullptr) == maud_success, "synced");
    bool told = false;
    while (maudNextNotification(context, &record) == maud_success)
    {
        told = told || (record.kind == maud_notifyStreamFormatChanged &&
                        record.streamId.index1 == stream.index1 && record.sampleRate == 44100);
    }
    maudStreamFormat format = {0};
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success &&
              format.sampleRate == 44100 && told,
          "the stream takes the device's new rate and says so");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy the stream");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
    return s_failures == 0 ? 0 : 1;
}
