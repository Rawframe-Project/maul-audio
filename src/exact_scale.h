// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A power of two to scale a vector or quaternion by before squaring its
// components, so that no square underflows or overflows: a float's
// square leaves its range below about 1e-23 and above about 1.8e19. The
// scale is exact, so the direction is kept to the bit, and it is 1 for
// any largest component from 2^-32 up to 2^32, where nothing changes.

#ifndef MAUL_AUDIO_SRC_EXACT_SCALE_H
#define MAUL_AUDIO_SRC_EXACT_SCALE_H

// The scale for a largest component's size m: below 2^-32 or from 2^32
// up, the one that takes m to between 2^-61 and 2^56.
static inline float maudExactScale(float m)
{
    return m < 0x1p-32f ? 0x1p88f : m >= 0x1p32f ? 0x1p-88f : 1.0f;
}

#endif // MAUL_AUDIO_SRC_EXACT_SCALE_H
