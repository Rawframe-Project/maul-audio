// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the bake reader with the bytes a host hands over. The first
// byte chooses whether the checksum is sealed afresh (so inputs reach
// the checks past it) and the fields' layout the reader accepts (order
// 0 to 3, 6 bins, as the shipped bake has at order 1); the rest is the
// file. A refusal must name the file's fault (its format, its version,
// the limits or another fields' layout), and a file the reader takes must
// write back to the same bytes.
//
// A broken promise prints one line, "test/fuzz_bake.c:LINE: expectation
// failed: " with the promise and the values that broke it, never the
// input's bytes: the line a finding's report keeps (tools/fuzz_target.sh).

#include "bake_file.h"
#include "crc32.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

// Prints the promise a line broke and the values that broke it, flushed
// before the abort, and aborts.
[[noreturn]] [[gnu::format(printf, 2, 3)]] static void Broken(int line, const char* format, ...)
{
    fprintf(stderr, "test/fuzz_bake.c:%d: expectation failed: ", line);
    va_list values;
    va_start(values, format);
    vfprintf(stderr, format, values);
    va_end(values);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
}

#define EXPECT(condition, ...) ((condition) ? (void)0 : Broken(__LINE__, __VA_ARGS__))

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1)
    {
        return 0;
    }
    size_t count = size - 1;
    uint8_t* bytes = malloc(count > 0 ? count : 1);
    EXPECT(bytes != nullptr, "the input's copy is allocated, %zu bytes", count);
    memcpy(bytes, data + 1, count);
    // The header: magic, version, then the checksum of every byte after.
    if ((data[0] & 1) != 0 && count >= 16)
    {
        uint32_t crc = maudCrc32(bytes + 16, count - 16);
        for (int k = 0; k < 4; ++k)
        {
            bytes[12 + k] = (uint8_t)(crc >> (8 * k));
        }
    }
    uint32_t order = (uint32_t)(data[0] >> 1) & 3u;
    const maudBakeLimits limits = {4096, 1u << 16, order, order > 0 ? 6u : 0u};
    const maudAllocator allocator = {nullptr, nullptr, nullptr};
    maudProbeGraph graph;
    maudProbeBake bake;
    maudResult read = maudReadBakeFile(bytes, count, &limits, &allocator, &graph, &bake);
    EXPECT(read == maud_success || read == maud_errorFormat || read == maud_errorVersion ||
               read == maud_errorCapacity || read == maud_errorUnsupported,
           "a refusal names the file's fault, result %d", (int)read);
    if (read == maud_success)
    {
        size_t again = maudBakeFileBytes(&graph, &bake, order, limits.fieldBins);
        EXPECT(again == count, "a bake read writes back to its size, %zu bytes of %zu", again,
               count);
        uint8_t* written = malloc(again);
        EXPECT(written != nullptr, "the written bake is allocated, %zu bytes", again);
        maudWriteBakeFile(&graph, &bake, order, limits.fieldBins, written);
        size_t at = 0;
        while (at < count && written[at] == bytes[at])
        {
            ++at;
        }
        EXPECT(at == count,
               "a bake read writes back to the same bytes, first difference at byte %zu of %zu", at,
               count);
        free(written);
        maudReleaseProbeGraph(&allocator, &graph);
        maudReleaseProbeBake(&allocator, &bake);
    }
    free(bytes);
    return 0;
}
