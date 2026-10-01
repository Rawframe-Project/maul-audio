// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Changes to the device table, as a backend reports them: each posts
// its notifications and keeps the streams on their devices.

#ifndef MAUL_AUDIO_SRC_DEVICE_H
#define MAUL_AUDIO_SRC_DEVICE_H

#include "context_core.h"

// A device as a backend describes it. The default flags of info are
// ignored.
typedef struct maudDeviceSpec
{
    maudDeviceInfo info;
    const char* name;
    size_t nameLength;
    const char* key;
    size_t keyLength;
} maudDeviceSpec;

// Adds a device. If its direction had none, it becomes the default for
// every role. maud_errorCapacity past the device or text limit.
maudResult maudAddDevice(maudContext* context, const maudDeviceSpec* spec,
                         maudDeviceId* deviceIdOut);

// Removes a live device. Every default it was passes to the first
// remaining device of its direction, or to the null id.
void maudRemoveDevice(maudContext* context, maudDeviceSlot* slot);

// Makes a live device the default of its direction for role.
void maudSetDefaultDevice(maudContext* context, maudDeviceRole role, maudDeviceId device);

#endif // MAUL_AUDIO_SRC_DEVICE_H
