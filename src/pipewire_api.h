// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// libpipewire, opened at run time. The headers' inline calls go
// through PipeWire's method tables; the exported functions the backend
// uses go through this table, so the library links nothing of
// PipeWire's and runs where it is missing.

#ifndef MAUL_AUDIO_SRC_PIPEWIRE_API_H
#define MAUL_AUDIO_SRC_PIPEWIRE_API_H

#include <pipewire/pipewire.h>
#include <stdbool.h>

typedef struct maudPipewireApi
{
    void* library;
    void (*init)(int* argc, char** argv[]);
    void (*deinit)(void);
    struct pw_loop* (*loopNew)(const struct spa_dict* props);
    void (*loopDestroy)(struct pw_loop* loop);
    struct pw_context* (*contextNew)(struct pw_loop* mainLoop, struct pw_properties* props,
                                     size_t userDataSize);
    void (*contextDestroy)(struct pw_context* context);
    struct pw_core* (*contextConnect)(struct pw_context* context, struct pw_properties* props,
                                      size_t userDataSize);
    int (*coreDisconnect)(struct pw_core* core);
    void (*proxyDestroy)(struct pw_proxy* proxy);
} maudPipewireApi;

// Opens libpipewire and fills the table. False, with the table zeroed,
// when the library or a function is missing.
bool maudLoadPipewire(maudPipewireApi* api);

// Closes what maudLoadPipewire opened.
void maudUnloadPipewire(maudPipewireApi* api);

#endif // MAUL_AUDIO_SRC_PIPEWIRE_API_H
