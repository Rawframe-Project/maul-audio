// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WASAPI backend's state.

#ifndef MAUL_AUDIO_SRC_WASAPI_CORE_H
#define MAUL_AUDIO_SRC_WASAPI_CORE_H

#include "context_core.h"
#include "device.h"
#include "wasapi_notify.h"

// Bytes of an endpoint's ID and friendly name, in UTF-8.
#define MAUD_WASAPI_KEY_BYTES  128
#define MAUD_WASAPI_NAME_BYTES 256

typedef struct maudWasapiEndpoint
{
    char key[MAUD_WASAPI_KEY_BYTES];
    char name[MAUD_WASAPI_NAME_BYTES];
} maudWasapiEndpoint;

typedef struct maudWasapi
{
    maudContext* context;
    IMMDeviceEnumerator* enumerator;
    maudWasapiNotifier notifier;
    bool registered;
    // Keeps the multithreaded apartment alive while the context lives.
    CO_MTA_USAGE_COOKIE apartment;
    bool apartmentHeld;
    // Room for a scan of as many endpoints as the context has devices.
    maudWasapiEndpoint* endpoints;
    maudDeviceSpec* specs;
    size_t bytes;
} maudWasapi;

#endif // MAUL_AUDIO_SRC_WASAPI_CORE_H
