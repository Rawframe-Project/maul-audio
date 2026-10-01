// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Scanning the sound cards. Each card's control lists its PCM devices
// and, per direction, whether the device has it and its name; no PCM is
// opened, so no codec wakes.

#include "alsa_scan.h"

#include "context.h"
#include "device.h"

#include <stdio.h>
#include <string.h>

// Lists one card device's endpoint in one direction, if the card has it.
static bool ScanEndpoint(const maudAlsaApi* api, snd_ctl_t* ctl, const char* cardId,
                         const char* cardName, int device, maudDirection direction,
                         maudAlsaEndpoint* endpoint)
{
    alignas(max_align_t) unsigned char infoBytes[MAUD_ALSA_STRUCT_BYTES];
    snd_pcm_info_t* info = (snd_pcm_info_t*)infoBytes;
    memset(infoBytes, 0, sizeof(infoBytes));
    api->pcmInfoSetDevice(info, (unsigned int)device);
    api->pcmInfoSetSubdevice(info, 0);
    api->pcmInfoSetStream(info, direction == maud_directionOutput ? SND_PCM_STREAM_PLAYBACK
                                                                  : SND_PCM_STREAM_CAPTURE);
    if (api->ctlPcmInfo(ctl, info) < 0)
    {
        return false;
    }
    endpoint->direction = direction;
    int keyLength =
        snprintf(endpoint->key, sizeof(endpoint->key), "hw:CARD=%s,DEV=%d", cardId, device);
    snprintf(endpoint->name, sizeof(endpoint->name), "%s, %s", cardName, api->pcmInfoGetName(info));
    return keyLength > 0 && (size_t)keyLength < sizeof(endpoint->key);
}

// Lists every endpoint of one card from count on; returns the new count.
static uint32_t ScanCard(const maudAlsaApi* api, int card, maudAlsaEndpoint* endpoints,
                         uint32_t count, uint32_t capacity)
{
    char ctlName[32];
    snprintf(ctlName, sizeof(ctlName), "hw:%d", card);
    snd_ctl_t* ctl = nullptr;
    if (api->ctlOpen(&ctl, ctlName, SND_CTL_NONBLOCK) < 0)
    {
        return count;
    }
    alignas(max_align_t) unsigned char cardBytes[MAUD_ALSA_STRUCT_BYTES];
    snd_ctl_card_info_t* cardInfo = (snd_ctl_card_info_t*)cardBytes;
    memset(cardBytes, 0, sizeof(cardBytes));
    if (api->ctlCardInfo(ctl, cardInfo) == 0)
    {
        const char* id = api->ctlCardInfoGetId(cardInfo);
        const char* name = api->ctlCardInfoGetName(cardInfo);
        int device = -1;
        while (api->ctlPcmNextDevice(ctl, &device) == 0 && device >= 0)
        {
            for (int direction = 0; direction < 2 && count < capacity; ++direction)
            {
                count += ScanEndpoint(api, ctl, id, name, device, (maudDirection)direction,
                                      &endpoints[count])
                             ? 1u
                             : 0u;
            }
        }
    }
    api->ctlClose(ctl);
    return count;
}

uint32_t maudAlsaScan(const maudAlsaApi* api, maudAlsaEndpoint* endpoints, uint32_t capacity)
{
    uint32_t count = 0;
    int card = -1;
    while (count < capacity && api->cardNext(&card) == 0 && card >= 0)
    {
        count = ScanCard(api, card, endpoints, count, capacity);
    }
    return count;
}

static bool SameText(const maudDeviceText* text, const char* bytes)
{
    return text->length == strlen(bytes) && memcmp(text->bytes, bytes, text->length) == 0;
}

// The listed endpoint a device is, or NULL.
static const maudAlsaEndpoint* Listed(const maudDeviceSlot* slot, const maudAlsaEndpoint* endpoints,
                                      uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (endpoints[i].direction == slot->info.direction &&
            SameText(&slot->key, endpoints[i].key))
        {
            return &endpoints[i];
        }
    }
    return nullptr;
}

// Whether a live device is the endpoint.
static bool Present(const maudContext* context, const maudAlsaEndpoint* endpoint)
{
    for (uint32_t i = 0; i < context->devices.capacity; ++i)
    {
        const maudDeviceSlot* slot = &context->devices.slots[i];
        if (slot->live && slot->info.direction == endpoint->direction &&
            SameText(&slot->key, endpoint->key))
        {
            return true;
        }
    }
    return false;
}

maudResult maudAlsaSyncDevices(maudContext* context, const maudAlsaEndpoint* endpoints,
                               uint32_t count)
{
    for (uint32_t i = 0; i < context->devices.capacity; ++i)
    {
        maudDeviceSlot* slot = &context->devices.slots[i];
        if (slot->live && !SameText(&slot->key, "default") &&
            Listed(slot, endpoints, count) == nullptr)
        {
            maudRemoveDevice(context, slot);
        }
    }
    size_t limit = context->def.limits.deviceTextBytes;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (Present(context, &endpoints[i]))
        {
            continue;
        }
        size_t nameLength = strlen(endpoints[i].name);
        maudDeviceSpec spec = {
            .info = {.direction = endpoints[i].direction},
            .name = endpoints[i].name,
            .nameLength = nameLength < limit ? nameLength : limit,
            .key = endpoints[i].key,
            .keyLength = strlen(endpoints[i].key),
        };
        maudDeviceId id;
        maudResult result = maudAddDevice(context, &spec, &id);
        if (result != maud_success)
        {
            return result;
        }
    }
    return maud_success;
}
