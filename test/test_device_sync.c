// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Syncing the device table with a backend's list (whitebox): when one
// device's native rate changes and the others' do not, a stream at the
// native rate on it takes the new rate and says so. Two devices of
// opposite directions under one key are each found by their direction,
// an unknown key by none; a removed device's slot taken again leaves
// its old id stale.

#include "context.h"
#include "device.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
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
                        record.stream.index1 == stream.index1 && record.sampleRate == 44100);
    }
    maudStreamFormat format = {0};
    CHECK(maudGetStreamFormat(context, stream, &format) == maud_success &&
              format.sampleRate == 44100 && told,
          "the stream takes the device's new rate and says so");
    // Two devices, an output and an input, under one key (as Core Audio
    // keys a device's two directions by its UID): each direction finds
    // its own; a key no device has finds none, every slot looked at.
    maudDeviceSpec shared[2] = {specs[0], specs[0]};
    shared[1].info.direction = specs[0].info.direction == maud_directionOutput
                                   ? maud_directionInput
                                   : maud_directionOutput;
    for (int k = 0; k < 2; ++k)
    {
        shared[k].key = "shared";
        shared[k].keyLength = 6;
    }
    maudDeviceId ids[2] = {{0, 0}, {0, 0}};
    CHECK(maudAddDevice(context, &shared[0], &ids[0]) == maud_success &&
              maudAddDevice(context, &shared[1], &ids[1]) == maud_success,
          "two devices under one key");
    bool own = true;
    for (int k = 0; k < 2; ++k)
    {
        maudDeviceId found = maudFindDeviceByKey(context, shared[k].info.direction, "shared", 6);
        own = own && found.index1 == ids[k].index1 && found.generation == ids[k].generation;
    }
    CHECK(own, "each direction finds its own");
    CHECK(maudFindDeviceByKey(context, maud_directionOutput, "missing", 7).index1 == 0,
          "an unknown key finds none");
    // A removed device's slot taken again: its old id is stale.
    maudRemoveDevice(context, maudFindDevice(context, ids[0]));
    maudDeviceId again = {0, 0};
    CHECK(maudAddDevice(context, &shared[0], &again) == maud_success &&
              again.index1 == ids[0].index1,
          "the slot taken again");
    maudDeviceInfo info;
    CHECK(maudGetDeviceInfo(context, ids[0], &info) == maud_errorStale &&
              maudGetDeviceInfo(context, again, &info) == maud_success,
          "the old id stale, the new one not");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy the stream");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
    return s_failures == 0 ? 0 : 1;
}
