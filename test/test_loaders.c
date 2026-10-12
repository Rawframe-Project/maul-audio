// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Linux sound libraries' loaders (whitebox): a file by a library's
// name that is no library is refused, and a library without the
// functions is refused and closed again; each leaves nothing behind.
// The test runs with LD_LIBRARY_PATH on a directory of such files. The
// stubs, one under each library's soname with none of its functions,
// are opened by path first, so that the loader's open finds the stub
// by its soname (glibc's rule) instead of the system's library.

#include "test_harness.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if MAUD_TEST_ALSA
#include "alsa_api.h"
#endif
#if MAUD_TEST_PIPEWIRE
#include "pipewire_api.h"
#endif
#if MAUD_TEST_PULSE
#include "pulse_api.h"
#endif

// The tables hold pointers alone, so no padding: every byte is cleared.
static bool Cleared(const void* api, size_t size)
{
    const unsigned char* bytes = api;
    for (size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != 0)
        {
            return false;
        }
    }
    return true;
}

#if MAUD_TEST_ALSA
static bool AlsaRefused(void)
{
    maudAlsaApi api;
    memset(&api, 0x5a, sizeof(api));
    return !maudLoadAlsa(&api) && Cleared(&api, sizeof(api));
}
#endif

#if MAUD_TEST_PIPEWIRE
static bool PipewireRefused(void)
{
    maudPipewireApi api;
    memset(&api, 0x5a, sizeof(api));
    return !maudLoadPipewire(&api) && Cleared(&api, sizeof(api));
}
#endif

#if MAUD_TEST_PULSE
static bool PulseRefused(void)
{
    maudPulseApi api;
    memset(&api, 0x5a, sizeof(api));
    return !maudLoadPulse(&api) && Cleared(&api, sizeof(api));
}
#endif

static void Check(bool (*refused)(void), const char* stub, const char* what)
{
    printf("%s\n", what);
    CHECK(refused(), "a file that is no library refused");
    void* held = dlopen(stub, RTLD_NOW | RTLD_LOCAL);
    CHECK(held != nullptr, "the stub opened");
    CHECK(refused(), "a library without the functions refused");
    if (held != nullptr)
    {
        dlclose(held);
    }
    void* left = dlopen(stub, RTLD_NOW | RTLD_NOLOAD);
    CHECK(left == nullptr, "the stub closed again");
    if (left != nullptr)
    {
        dlclose(left);
    }
}

int main(void)
{
#if MAUD_TEST_ALSA
    Check(AlsaRefused, MAUD_STUB_ALSA, "ALSA");
#endif
#if MAUD_TEST_PIPEWIRE
    Check(PipewireRefused, MAUD_STUB_PIPEWIRE, "PipeWire");
#endif
#if MAUD_TEST_PULSE
    Check(PulseRefused, MAUD_STUB_PULSE, "PulseAudio");
#endif
    return s_failures == 0 ? 0 : 1;
}
