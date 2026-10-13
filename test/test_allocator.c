// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The allocator (whitebox): without hooks, malloc serves every
// alignment it guarantees, max_align_t's included, and refuses a larger
// one; releasing NULL does nothing; an allocator is valid with both
// hooks or neither. A layout whose size would pass SIZE_MAX, by a count
// of items or by aligning the next part, overflows and stays so.

#include "allocator.h"
#include "test_harness.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    return malloc(size);
}

int main(void)
{
    const maudAllocator plain = {nullptr, nullptr, nullptr};
    void* most = maudAllocate(&plain, 64, alignof(max_align_t));
    CHECK(most != nullptr, "max_align_t's alignment served");
    maudRelease(&plain, most, 64, alignof(max_align_t));
    CHECK(maudAllocate(&plain, 64, 2 * alignof(max_align_t)) == nullptr, "a larger one refused");
    const maudAllocator half = {Alloc, nullptr, nullptr};
    CHECK(maudIsAllocatorValid(&plain) && !maudIsAllocatorValid(&half), "both hooks or neither");
    maudRelease(&half, nullptr, 64, 8);
    maudLayout counted = {0};
    CHECK(maudLayoutAdd(&counted, 2, 16, 16) == 0 && counted.size >= 32 && !counted.overflow,
          "a part at the start");
    CHECK(maudLayoutAdd(&counted, SIZE_MAX / 2, 4, 4) == 0 && counted.overflow,
          "a count of items past SIZE_MAX");
    CHECK(maudLayoutAdd(&counted, 1, 1, 1) == 0 && counted.overflow, "and it stays overflowed");
    maudLayout ending = {0};
    (void)maudLayoutAdd(&ending, SIZE_MAX - 8, 1, 1);
    CHECK(!ending.overflow && maudLayoutAdd(&ending, 1, 1, 16) == 0 && ending.overflow,
          "aligning past SIZE_MAX");
    maudLayout end = {0};
    (void)maudLayoutAdd(&end, SIZE_MAX - 8, 1, 1);
    CHECK(maudLayoutAdd(&end, 16, 1, 1) == 0 && end.overflow, "an end past SIZE_MAX");
    return s_failures == 0 ? 0 : 1;
}
