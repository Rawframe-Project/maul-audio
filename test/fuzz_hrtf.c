// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the HRTF loader with the bytes a host hands over. The first
// byte chooses whether the checksum is sealed afresh (so inputs reach the
// checks past it) and the rate the set is loaded for; the rest is the
// file. A load must succeed exactly when the reader calls the file
// well-formed, a refusal must name the file's fault (its format, its
// version or the def's limits), and a loaded set must report what the
// file says.
//
// A broken promise prints one line, "test/fuzz_hrtf.c:LINE: expectation
// failed: " with the promise and the values that broke it, never the
// input's bytes: the line a finding's report keeps (tools/fuzz_target.sh).

#include "hrtf_file.h"

#include "maul-audio/hrtf.h"

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
    fprintf(stderr, "test/fuzz_hrtf.c:%d: expectation failed: ", line);
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
    static const uint32_t rates[4] = {0, 44100, 48000, 96000};
    uint8_t choice = data[0];
    size_t count = size - 1;
    uint8_t* bytes = malloc(count > 0 ? count : 1);
    EXPECT(bytes != nullptr, "the input's copy is allocated, %zu bytes", count);
    memcpy(bytes, data + 1, count);
    // The header: magic, version, then the checksum of every byte after.
    if ((choice & 1u) != 0 && count >= 16)
    {
        uint32_t crc = maudCrc32(bytes + 16, count - 16);
        for (int i = 0; i < 4; ++i)
        {
            bytes[12 + i] = (uint8_t)(crc >> (8 * i));
        }
    }
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = bytes;
    def.byteCount = count;
    def.sampleRate = rates[(choice >> 1) & 3u];
    maudHrtf* hrtf = nullptr;
    maudResult result = maudLoadHrtf(&def, &hrtf);
    maudHrtfFile file;
    bool wellFormed = maudReadHrtfFile(bytes, count, &file) == maud_success;
    EXPECT(result == maud_success ? wellFormed : hrtf == nullptr,
           "a load gives a set exactly when it succeeds, result %d, well-formed %d, set %d",
           (int)result, wellFormed, hrtf != nullptr);
    EXPECT(result != maud_success || wellFormed,
           "a load succeeds only for a well-formed file, result %d, well-formed %d", (int)result,
           wellFormed);
    EXPECT(result == maud_success || result == maud_errorFormat || result == maud_errorVersion ||
               result == maud_errorCapacity,
           "a refusal names the file's fault, result %d", (int)result);
    if (hrtf != nullptr)
    {
        maudHrtfInfo info;
        maudResult got = maudHrtf_GetInfo(hrtf, &info);
        EXPECT(got == maud_success, "a loaded set reports its info, result %d", (int)got);
        EXPECT(info.directionCount == file.directionCount && info.ringCount == file.ringCount,
               "a loaded set reports the file's directions and rings, %u of %u, %u of %u",
               info.directionCount, file.directionCount, info.ringCount, file.ringCount);
        EXPECT(info.nameLength == file.nameLength && info.licenseLength == file.licenseLength,
               "a loaded set reports the file's name and license lengths, %zu of %zu, %zu of %zu",
               info.nameLength, (size_t)file.nameLength, info.licenseLength,
               (size_t)file.licenseLength);
        EXPECT(info.distance == file.distance, "a loaded set reports the file's distance, %a of %a",
               (double)info.distance, (double)file.distance);
        maudDestroyHrtf(hrtf);
    }
    free(bytes);
    return 0;
}
