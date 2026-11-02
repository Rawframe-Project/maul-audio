// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Directions at both ends of a float's range, each beside the same
// direction at an ordinary length, which it differs from by a power of
// two: components that are subnormal, the smallest normal, ones whose
// squares are subnormal, ones whose squares overflow, near FLT_MAX, and
// one huge beside one tiny that counts as 0 next to it. A direction's
// length must not count, so each pair must give the same bits.

#ifndef MAUL_AUDIO_TEST_LENGTHS_H
#define MAUL_AUDIO_TEST_LENGTHS_H

#include "maul-audio/base.h"

#include <float.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct LengthPair
{
    maudVector3 ordinary;
    maudVector3 scaled;
} LengthPair;

static const LengthPair s_lengths[] = {
    {{4.0f, -2.0f, 3.0f}, {4.0f * 0x1p-149f, -2.0f * 0x1p-149f, 3.0f * 0x1p-149f}},
    {{4.0f, -2.0f, 3.0f}, {4.0f * 0x1p-126f, -2.0f * 0x1p-126f, 3.0f * 0x1p-126f}},
    {{4.0f, -2.0f, 3.0f}, {4.0f * 0x1p-70f, -2.0f * 0x1p-70f, 3.0f * 0x1p-70f}},
    {{4.0f, -2.0f, 3.0f}, {4.0f * 0x1p64f, -2.0f * 0x1p64f, 3.0f * 0x1p64f}},
    {{4.0f, -2.0f, 3.0f}, {4.0f * 0x1p125f, -2.0f * 0x1p125f, 3.0f * 0x1p125f}},
    {{FLT_MAX * 0x1p-100f, 0.0f, -FLT_MAX * 0x1p-100f}, {FLT_MAX, 0.0f, -FLT_MAX}},
    {{1.0f, 0.0f, -1.0f}, {0x1p100f, 0x1p-100f, -0x1p100f}},
};

enum
{
    LENGTH_PAIRS = sizeof(s_lengths) / sizeof(s_lengths[0])
};

static void PrintLength(const char* what, maudVector3 v, bool same)
{
    printf("%s (%g %g %g): same %d\n", what, (double)v.x, (double)v.y, (double)v.z, same);
}

#endif // MAUL_AUDIO_TEST_LENGTHS_H
