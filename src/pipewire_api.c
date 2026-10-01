// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Opening libpipewire. The library is reference counted by the dynamic
// loader, so each context opens and closes it on its own.

#include "pipewire_api.h"

#include <dlfcn.h>
#include <string.h>

// Resolves one function into a table slot, which has the function's
// pointer type; the loader returns an object pointer.
static bool Resolve(void* library, const char* name, void* slot, size_t slotSize)
{
    void* function = dlsym(library, name);
    if (function == nullptr)
    {
        return false;
    }
    memcpy(slot, (const void*)&function, slotSize);
    return true;
}

#define RESOLVE(field, name) Resolve(api->library, name, (void*)&api->field, sizeof(api->field))

bool maudLoadPipewire(maudPipewireApi* api)
{
    *api = (maudPipewireApi){0};
    api->library = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
    if (api->library == nullptr)
    {
        return false;
    }
    bool loaded =
        RESOLVE(init, "pw_init") && RESOLVE(deinit, "pw_deinit") &&
        RESOLVE(loopNew, "pw_loop_new") && RESOLVE(loopDestroy, "pw_loop_destroy") &&
        RESOLVE(contextNew, "pw_context_new") && RESOLVE(contextDestroy, "pw_context_destroy") &&
        RESOLVE(contextConnect, "pw_context_connect") &&
        RESOLVE(coreDisconnect, "pw_core_disconnect") && RESOLVE(proxyDestroy, "pw_proxy_destroy");
    if (!loaded)
    {
        maudUnloadPipewire(api);
        return false;
    }
    return true;
}

void maudUnloadPipewire(maudPipewireApi* api)
{
    if (api->library != nullptr)
    {
        dlclose(api->library);
    }
    *api = (maudPipewireApi){0};
}
