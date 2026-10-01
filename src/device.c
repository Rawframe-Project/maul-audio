// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The device table: what a backend reports, the defaults per direction
// and role, and the queries hosts make. Names and keys live in the
// context's text storage, two fixed-size places per slot.

#include "device.h"

#include "context.h"
#include "follow.h"
#include "notify.h"

#include <string.h>

static maudDeviceId IdOf(const maudContext* context, const maudDeviceSlot* slot)
{
    return (maudDeviceId){(uint32_t)(slot - context->devices.slots) + 1, slot->generation};
}

static bool SameDevice(maudDeviceId a, maudDeviceId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

static void SetDefault(maudContext* context, maudDirection direction, maudDeviceRole role,
                       maudDeviceId device)
{
    context->devices.defaults[direction][role] = device;
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyDefaultChanged,
                                      .direction = direction,
                                      .role = role,
                                      .deviceId = device,
                                  });
    maudFollowDefault(context, direction, role);
}

maudResult maudAddDevice(maudContext* context, const maudDeviceSpec* spec,
                         maudDeviceId* deviceIdOut)
{
    uint32_t textBytes = context->def.limits.deviceTextBytes;
    maudDeviceSlot* slot = nullptr;
    for (uint32_t i = 0; i < context->devices.capacity && slot == nullptr; ++i)
    {
        slot = context->devices.slots[i].live ? nullptr : &context->devices.slots[i];
    }
    if (slot == nullptr || spec->nameLength > textBytes || spec->keyLength > textBytes)
    {
        return maud_errorCapacity;
    }
    slot->info = spec->info;
    slot->info.defaultGeneral = false;
    slot->info.defaultCommunications = false;
    if (spec->nameLength != 0)
    {
        memcpy(slot->name.bytes, spec->name, spec->nameLength);
    }
    slot->name.length = (uint32_t)spec->nameLength;
    if (spec->keyLength != 0)
    {
        memcpy(slot->key.bytes, spec->key, spec->keyLength);
    }
    slot->key.length = (uint32_t)spec->keyLength;
    slot->live = true;
    maudDeviceId id = IdOf(context, slot);
    maudDirection direction = spec->info.direction;
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyDeviceAdded,
                                      .direction = direction,
                                      .deviceId = id,
                                  });
    for (uint32_t role = maud_roleGeneral; role <= maud_roleCommunications; ++role)
    {
        if (context->devices.defaults[direction][role].index1 == 0)
        {
            SetDefault(context, direction, (maudDeviceRole)role, id);
        }
    }
    *deviceIdOut = id;
    return maud_success;
}

static maudDeviceId FirstDevice(const maudContext* context, maudDirection direction)
{
    for (uint32_t i = 0; i < context->devices.capacity; ++i)
    {
        const maudDeviceSlot* slot = &context->devices.slots[i];
        if (slot->live && slot->info.direction == direction)
        {
            return IdOf(context, slot);
        }
    }
    return (maudDeviceId){0, 0};
}

void maudRemoveDevice(maudContext* context, maudDeviceSlot* slot)
{
    maudDeviceId id = IdOf(context, slot);
    maudDirection direction = slot->info.direction;
    slot->live = false;
    // A generation of 0 never names a device, so it is skipped on wrap.
    slot->generation = slot->generation == UINT32_MAX ? 1 : slot->generation + 1;
    maudPostNotification(context, &(maudNotification){
                                      .kind = maud_notifyDeviceRemoved,
                                      .direction = direction,
                                      .deviceId = id,
                                  });
    maudLoseDevice(context, id);
    for (uint32_t role = maud_roleGeneral; role <= maud_roleCommunications; ++role)
    {
        if (SameDevice(context->devices.defaults[direction][role], id))
        {
            SetDefault(context, direction, (maudDeviceRole)role, FirstDevice(context, direction));
        }
    }
}

void maudSetDefaultDevice(maudContext* context, maudDeviceRole role, maudDeviceId device)
{
    const maudDeviceSlot* slot = maudFindDevice(context, device);
    maudDirection direction = slot->info.direction;
    if (!SameDevice(context->devices.defaults[direction][role], device))
    {
        SetDefault(context, direction, role, device);
    }
}

maudResult maudGetDevices(const maudContext* context, maudDirection direction, maudDeviceId* idsOut,
                          uint32_t capacity, uint32_t* countOut)
{
    if (context == nullptr || countOut == nullptr || (idsOut == nullptr && capacity != 0) ||
        direction > maud_directionInput)
    {
        return maud_errorInvalid;
    }
    uint32_t count = 0;
    for (uint32_t i = 0; i < context->devices.capacity; ++i)
    {
        const maudDeviceSlot* slot = &context->devices.slots[i];
        if (slot->live && slot->info.direction == direction)
        {
            if (count < capacity)
            {
                idsOut[count] = IdOf(context, slot);
            }
            count++;
        }
    }
    *countOut = count;
    return maud_success;
}

maudResult maudGetDeviceInfo(const maudContext* context, maudDeviceId device,
                             maudDeviceInfo* infoOut)
{
    if (context == nullptr || infoOut == nullptr)
    {
        return maud_errorInvalid;
    }
    const maudDeviceSlot* slot = maudFindDevice(context, device);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    const maudDeviceId* defaults = context->devices.defaults[slot->info.direction];
    *infoOut = slot->info;
    infoOut->defaultGeneral = SameDevice(defaults[maud_roleGeneral], device);
    infoOut->defaultCommunications = SameDevice(defaults[maud_roleCommunications], device);
    return maud_success;
}

static maudResult CopyText(const maudContext* context, maudDeviceId device, bool key,
                           char* bytesOut, size_t capacity, size_t* lengthOut)
{
    if (context == nullptr || lengthOut == nullptr || (bytesOut == nullptr && capacity != 0))
    {
        return maud_errorInvalid;
    }
    const maudDeviceSlot* slot = maudFindDevice(context, device);
    if (slot == nullptr)
    {
        return maud_errorStale;
    }
    const maudDeviceText* text = key ? &slot->key : &slot->name;
    *lengthOut = text->length;
    if (text->length > capacity)
    {
        return maud_errorCapacity;
    }
    if (text->length != 0)
    {
        memcpy(bytesOut, text->bytes, text->length);
    }
    return maud_success;
}

maudResult maudGetDeviceName(const maudContext* context, maudDeviceId device, char* bytesOut,
                             size_t capacity, size_t* lengthOut)
{
    return CopyText(context, device, false, bytesOut, capacity, lengthOut);
}

maudResult maudGetDeviceKey(const maudContext* context, maudDeviceId device, char* bytesOut,
                            size_t capacity, size_t* lengthOut)
{
    return CopyText(context, device, true, bytesOut, capacity, lengthOut);
}

maudResult maudGetDefaultDevice(const maudContext* context, maudDirection direction,
                                maudDeviceRole role, maudDeviceId* deviceIdOut)
{
    if (context == nullptr || deviceIdOut == nullptr || direction > maud_directionInput ||
        role > maud_roleCommunications)
    {
        return maud_errorInvalid;
    }
    *deviceIdOut = context->devices.defaults[direction][role];
    return deviceIdOut->index1 != 0 ? maud_success : maud_empty;
}
