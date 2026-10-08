// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The allocator (whitebox): without hooks, malloc serves every
// alignment it guarantees, max_align_t's included, and refuses a larger
// one; an allocator is valid with both hooks or neither.

#include "allocator.h"
#include "test_harness.h"

#include <stddef.h>
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
    return s_failures == 0 ? 0 : 1;
}
