// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Whether two caller buffers share a byte. Compared as addresses, since a
// caller's buffers are different objects and C does not order pointers
// into different objects.

#ifndef MAUL_AUDIO_SRC_OVERLAP_H
#define MAUL_AUDIO_SRC_OVERLAP_H

#include "maul-audio/base.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Whether the aBytes bytes from a and the bBytes bytes from b share one.
MAUD_NODISCARD static inline bool maudRangesOverlap(const void* a, size_t aBytes, const void* b,
                                                    size_t bBytes)
{
    uintptr_t x = (uintptr_t)a;
    uintptr_t y = (uintptr_t)b;
    return aBytes > 0 && bBytes > 0 && x < y + bBytes && y < x + aBytes;
}

// Whether the bytes bytes from a and the bytes bytes from b share one.
MAUD_NODISCARD static inline bool maudSpansOverlap(const void* a, const void* b, size_t bytes)
{
    return maudRangesOverlap(a, bytes, b, bytes);
}

#endif // MAUL_AUDIO_SRC_OVERLAP_H
