// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The audio-thread trap: in debug builds, a library allocation on a
// thread that is rendering one of the context's streams stops the
// program. The trap instruction raises SIGILL on x86 and SIGTRAP on
// ARM; the handler turns either into success, and returning normally
// means the trap did not fire.

#include "context.h"
#include "test_harness.h"
#include "thread.h"

#include "maul-audio/stream.h"

#include <signal.h>
#include <stdlib.h>

static void Silence(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

static void Trapped(int signal)
{
    (void)signal;
    _Exit(0);
}

int main(void)
{
    maudContextDef contextDef = maudDefaultContextDef();
    contextDef.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&contextDef, &context) == maud_success, "context");
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.callback = Silence;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success, "stream");
    if (s_failures != 0)
    {
        return 1;
    }
    signal(SIGILL, Trapped);
    signal(SIGTRAP, Trapped);
    maudStreamSlot* slot = maudFindStream(context, stream);
    atomic_store(&slot->core.renderingThread, maudCurrentThread());
    void* memory = maudContextAllocate(context, 16, 16);
    // Not trapped: release the claim first, so the release cannot trap
    // in the allocation's place.
    atomic_store(&slot->core.renderingThread, 0);
    maudContextRelease(context, memory, 16, 16);
    return 1;
}
