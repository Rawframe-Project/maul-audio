// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Checks device names, for the backend tests: valid UTF-8, and the test
// daemon's source, whose description is longer than a name may be.

#ifndef MAUL_AUDIO_TEST_TEST_NAMES_H
#define MAUL_AUDIO_TEST_TEST_NAMES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Whether text is a run of whole UTF-8 sequences: a valid lead byte
// (no C0, C1 or F5 to FF) and its continuation bytes, none cut off.
static inline bool IsUtf8(const char* text, size_t length)
{
    const unsigned char* bytes = (const unsigned char*)text;
    for (size_t at = 0; at < length;)
    {
        unsigned char lead = bytes[at];
        size_t more = lead < 0x80   ? 0
                      : lead < 0xC2 ? SIZE_MAX
                      : lead < 0xE0 ? 1
                      : lead < 0xF0 ? 2
                      : lead < 0xF5 ? 3
                                    : SIZE_MAX;
        if (more == SIZE_MAX || more >= length - at)
        {
            return false;
        }
        for (size_t i = 1; i <= more; ++i)
        {
            if ((bytes[at + i] & 0xC0) != 0x80)
            {
                return false;
            }
        }
        at += more + 1;
    }
    return true;
}

// Whether name is the test source's description (tools/pipewire_daemon.sh:
// "Maud test source ", sixty U+011F and fifty U+97F3, 287 bytes) cut after
// the last whole character within 256 bytes with a NUL: 254 bytes.
static inline bool IsCutSourceName(const char* name, size_t length)
{
    char expected[256];
    size_t at = strlen("Maud test source ");
    memcpy(expected, "Maud test source ", at);
    for (int i = 0; i < 60; ++i, at += 2)
    {
        memcpy(expected + at, "\xC4\x9F", 2);
    }
    for (; at + 3 <= 254; at += 3)
    {
        memcpy(expected + at, "\xE9\x9F\xB3", 3);
    }
    return length == at && memcmp(name, expected, at) == 0;
}

#endif // MAUL_AUDIO_TEST_TEST_NAMES_H
