// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the bake reader with the bytes a host hands over. The first
// byte chooses whether the checksum is sealed afresh (so inputs reach
// the checks past it) and the fields' layout the reader accepts (order
// 0 to 3, 6 bins, as the shipped bake has at order 1); the rest is the
// file. A file the reader takes must write back to the same bytes.

#include "bake_file.h"
#include "crc32.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
    {
        return 0;
    }
    size_t count = size - 1;
    uint8_t* bytes = malloc(count > 0 ? count : 1);
    Expect(bytes != nullptr);
    memcpy(bytes, data + 1, count);
    if ((data[0] & 1) != 0 && count >= MAUD_BAKE_HEADER)
    {
        uint32_t crc = maudCrc32(bytes + MAUD_BAKE_HEADER, count - MAUD_BAKE_HEADER);
        for (int k = 0; k < 4; ++k)
        {
            bytes[36 + k] = (uint8_t)(crc >> (8 * k));
        }
    }
    uint32_t order = (uint32_t)(data[0] >> 1) & 3u;
    const maudBakeLimits limits = {4096, 1u << 16, order, order > 0 ? 6u : 0u};
    const maudAllocator allocator = {nullptr, nullptr, nullptr};
    maudProbeGraph graph;
    maudProbeBake bake;
    if (maudReadBakeFile(bytes, count, &limits, &allocator, &graph, &bake) == maud_success)
    {
        size_t again = maudBakeFileBytes(&graph, &bake, order, limits.fieldBins);
        Expect(again == count);
        uint8_t* written = malloc(again);
        Expect(written != nullptr);
        maudWriteBakeFile(&graph, &bake, order, limits.fieldBins, written);
        Expect(memcmp(written, bytes, count) == 0);
        free(written);
        maudReleaseProbeGraph(&allocator, &graph);
        maudReleaseProbeBake(&allocator, &bake);
    }
    free(bytes);
    return 0;
}
