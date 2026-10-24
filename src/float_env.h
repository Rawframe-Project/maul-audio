// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The floating-point environment the Spatial simulation computes in:
// round to nearest, denormals kept, no traps. A host may run its threads
// with flush-to-zero, denormals-are-zero or another rounding mode; each
// public call that promises the same bits everywhere (a bake, probe
// placement, paths, estimates) enters this environment and leaves the
// host's as it found it, and so does each task body it hands the host's
// task hooks, so threads in different modes give the same bits. The
// realtime side never touches the mode (realtime.md).
//
// Compilers treat floating-point arithmetic as free of side effects (the
// build uses -fno-trapping-math), so they may move it across the writes
// to the control register. A call that enters therefore does no
// arithmetic of its own between entering and leaving: it calls a worker
// marked MAUD_NOINLINE, whose body cannot move out of the call.

#ifndef MAUL_AUDIO_SRC_FLOAT_ENV_H
#define MAUL_AUDIO_SRC_FLOAT_ENV_H

#include <stdint.h>

#if defined(_MSC_VER) && !defined(__clang__)
#define MAUD_NOINLINE __declspec(noinline)
#else
#define MAUD_NOINLINE __attribute__((noinline))
#endif

// The host's control register, saved on entry.
typedef struct maudFloatEnv
{
    uint64_t saved;
} maudFloatEnv;

// Saves the thread's floating-point control register into env and sets
// round to nearest, denormals kept and every exception masked, writing
// the register only when that changes it.
void maudFloatEnvEnter(maudFloatEnv* env);

// Restores the control register maudFloatEnvEnter saved, writing it only
// when it differs.
void maudFloatEnvLeave(const maudFloatEnv* env);

// A task body as the task hooks run it (maudTaskFn).
typedef void maudFloatEnvTaskFn(uint32_t start, uint32_t end, void* context);

// Runs body over [start, end) in the environment, restoring the thread's
// own before it returns: the task body handed to the host's hooks, which
// may run it on a thread in any mode. body is a MAUD_NOINLINE worker.
void maudFloatEnvTask(maudFloatEnvTaskFn* body, uint32_t start, uint32_t end, void* context);

#endif // MAUL_AUDIO_SRC_FLOAT_ENV_H
