// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// x86 keeps the environment in MXCSR: flush-to-zero is bit 15,
// denormals-are-zero bit 6, the rounding mode bits 13 and 14, the
// exception masks bits 7 to 12. AArch64 keeps it in FPCR: flush-to-zero
// is bit 24, half-precision flush-to-zero bit 19, default NaN bit 25, the
// rounding mode bits 22 and 23, the trap enables bits 8 to 12 and 15, and
// with FEAT_AFP input flush and alternate handling bits 0 and 1.
// WebAssembly has one environment, the one the library needs. Any other
// architecture is refused at build time rather than computed in an
// unknown environment.

#include "float_env.h"

#include <float.h>

#if FLT_EVAL_METHOD != 0
#error "maul-audio: float arithmetic must round to float (FLT_EVAL_METHOD 0)"
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

// The register is read and written through volatile asm with a memory
// clobber: Clang treats _mm_getcsr as free of side effects and may reuse
// an earlier read, which would undo a restore.
#if defined(_MSC_VER) && !defined(__clang__)
#include <xmmintrin.h>
static uint64_t ReadControl(void)
{
    return _mm_getcsr();
}
static void WriteControl(uint64_t value)
{
    _mm_setcsr((uint32_t)value);
}
#else
static uint64_t ReadControl(void)
{
    uint32_t value = 0;
    __asm__ volatile("stmxcsr %0" : "=m"(value) : : "memory");
    return value;
}
static void WriteControl(uint64_t value)
{
    uint32_t control = (uint32_t)value;
    __asm__ volatile("ldmxcsr %0" : : "m"(control) : "memory");
}
#endif

// Clear flush-to-zero, denormals-are-zero and the rounding mode; mask
// every exception.
static const uint32_t s_flushToZero = 1u << 15;
static const uint32_t s_denormalsAreZero = 1u << 6;
static const uint32_t s_roundingMode = 3u << 13;
static const uint32_t s_exceptionMasks = 0x3fu << 7;

static uint64_t Wanted(uint64_t control)
{
    uint32_t clear = s_flushToZero | s_denormalsAreZero | s_roundingMode;
    return (control & ~(uint64_t)clear) | s_exceptionMasks;
}

#elif defined(__aarch64__) || defined(_M_ARM64)

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
// FPCR as _ReadStatusReg names it: op0 3, op1 3, CRn 4, CRm 4, op2 0.
#define FPCR_REGISTER 0x5A20
static uint64_t ReadControl(void)
{
    return (uint64_t)_ReadStatusReg(FPCR_REGISTER);
}
static void WriteControl(uint64_t value)
{
    _WriteStatusReg(FPCR_REGISTER, (__int64)value);
}
#else
static uint64_t ReadControl(void)
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(value) : : "memory");
    return value;
}
static void WriteControl(uint64_t value)
{
    __asm__ volatile("msr fpcr, %0" : : "r"(value) : "memory");
}
#endif

// Clear flush-to-zero (single and half, and input flush), alternate
// handling, default NaN, the rounding mode and the trap enables.
static const uint64_t s_flushToZero = 1ull << 24;
static const uint64_t s_flushToZeroHalf = 1ull << 19;
static const uint64_t s_defaultNan = 1ull << 25;
static const uint64_t s_roundingMode = 3ull << 22;
static const uint64_t s_trapEnables = (0x1full << 8) + (1ull << 15);
static const uint64_t s_inputFlushAndAlternate = 3ull;

static uint64_t Wanted(uint64_t control)
{
    uint64_t clear = s_flushToZero | s_flushToZeroHalf | s_defaultNan | s_roundingMode |
                     s_trapEnables | s_inputFlushAndAlternate;
    return control & ~clear;
}

#elif defined(__wasm__)

static uint64_t ReadControl(void)
{
    return 0;
}

static void WriteControl(uint64_t value)
{
    (void)value;
}

static uint64_t Wanted(uint64_t control)
{
    return control;
}

#else
#error "maul-audio: unknown floating-point environment for this architecture"
#endif

void maudFloatEnvEnter(maudFloatEnv* env)
{
    uint64_t saved = ReadControl();
    env->saved = saved;
    uint64_t wanted = Wanted(saved);
    if (wanted != saved)
    {
        WriteControl(wanted);
    }
}

void maudFloatEnvLeave(const maudFloatEnv* env)
{
    if (ReadControl() != env->saved)
    {
        WriteControl(env->saved);
    }
}

void maudFloatEnvTask(maudFloatEnvTaskFn* body, uint32_t start, uint32_t end, void* context)
{
    maudFloatEnv env;
    maudFloatEnvEnter(&env);
    body(start, end, context);
    maudFloatEnvLeave(&env);
}
