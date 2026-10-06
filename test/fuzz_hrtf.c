// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the HRTF loader with the bytes a host hands over. The first
// byte chooses whether the checksum is sealed afresh (so inputs reach the
// checks past it) and the rate the set is loaded for; the rest is the
// file. A load must succeed exactly when the reader calls the file
// well-formed, and a loaded set must report what the file says.

#include "hrtf_file.h"

#include "maul-audio/hrtf.h"

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
    static const uint32_t rates[4] = {0, 44100, 48000, 96000};
    uint8_t choice = data[0];
    size_t count = size - 1;
    uint8_t* bytes = malloc(count > 0 ? count : 1);
    Expect(bytes != nullptr);
    memcpy(bytes, data + 1, count);
    if ((choice & 1u) != 0 && count >= 48)
    {
        uint32_t crc = maudCrc32(bytes + 48, count - 48);
        for (int i = 0; i < 4; ++i)
        {
            bytes[44 + i] = (uint8_t)(crc >> (8 * i));
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
    Expect(result == maud_success ? wellFormed : hrtf == nullptr);
    Expect(result != maud_success || wellFormed);
    if (hrtf != nullptr)
    {
        maudHrtfInfo info;
        Expect(maudGetHrtfInfo(hrtf, &info) == maud_success);
        Expect(info.directionCount == file.directionCount && info.ringCount == file.ringCount);
        Expect(info.nameLength == file.nameLength && info.licenseLength == file.licenseLength);
        Expect(info.distance == file.distance);
        maudDestroyHrtf(hrtf);
    }
    free(bytes);
    return 0;
}
