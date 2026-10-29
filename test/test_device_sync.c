// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Syncing the device table with a backend's list (whitebox): when one
// device's native rate changes and the others' do not, a stream at the
// native rate on it takes the new rate and says so, and so it does when
// every device's rate changes at once; a platform's rate outside the
// rates a stream runs at reads as none, and a device that cannot tell
// its rate leaves the stream's. Two devices of opposite directions under
// one key are each found by their direction, an unknown key by none; a
// removed device's slot taken again leaves its old id stale.

#include "context.h"
#include "device.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/notification.h"
#include "maul-audio/stream.h"

#include <math.h>

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
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
    }
    CHECK(maudSyncDevices(context, specs, count, nullptr) == maud_success, "synced");
    bool told = false;
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
        told = told || (record.kind == maud_notifyStreamFormatChanged &&
                        record.stream.index1 == stream.index1 && record.sampleRate == 44100);
    }
    maudStreamFormat format = {0};
    CHECK(maudStream_GetFormat(context, stream, &format) == maud_success &&
              format.sampleRate == 44100 && told,
          "the stream takes the device's new rate and says so");
    // Every device's rate changed at once: the stream follows as well.
    for (uint32_t i = 0; i < count; ++i)
    {
        specs[i].info.nativeSampleRate = 32000;
        specs[i].info.minSampleRate = 32000;
    }
    CHECK(maudSyncDevices(context, specs, count, nullptr) == maud_success &&
              maudStream_GetFormat(context, stream, &format) == maud_success &&
              format.sampleRate == 32000,
          "every rate changed, the stream's too");
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
    }
    // A platform's rate is taken within the rates a stream runs at;
    // anything else reads as one it cannot tell.
    CHECK(maudDeviceRate(8000.0) == 8000 && maudDeviceRate(384000.0) == 384000 &&
              maudDeviceRate(44099.6) == 44100,
          "rates within the range, rounded");
    CHECK(maudDeviceRate(7999.0) == 0 && maudDeviceRate(384001.0) == 0 &&
              maudDeviceRate(0.0) == 0 && maudDeviceRate(-48000.0) == 0 &&
              maudDeviceRate(4294967296.0 * 2.0) == 0 && maudDeviceRate((double)NAN) == 0 &&
              maudDeviceRate((double)INFINITY) == 0,
          "rates outside it, past uint32_t or not a number, as none");
    // A device that cannot tell its rate leaves a native stream at its
    // own, and says nothing of it.
    for (uint32_t i = 0; i < count; ++i)
    {
        specs[i].info.nativeSampleRate = 0;
        specs[i].info.minSampleRate = 0;
        specs[i].info.maxSampleRate = 0;
    }
    bool changed = false;
    CHECK(maudSyncDevices(context, specs, count, nullptr) == maud_success, "synced again");
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
        changed = changed || record.kind == maud_notifyStreamFormatChanged;
    }
    CHECK(maudStream_GetFormat(context, stream, &format) == maud_success &&
              format.sampleRate == 32000 && !changed,
          "a rate the device cannot tell leaves the stream's");
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
    CHECK(maudDevice_GetInfo(context, ids[0], &info) == maud_errorStale &&
              maudDevice_GetInfo(context, again, &info) == maud_success,
          "the old id stale, the new one not");
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy the stream");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
    return s_failures == 0 ? 0 : 1;
}
