// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cutting and encoding device text: a cut backs up over continuation
// bytes to the start of the sequence it would split.

#include "utf8.h"

#include <string.h>

size_t maudCutUtf8(const char* text, size_t limit)
{
    size_t length = strlen(text);
    if (length <= limit)
    {
        return length;
    }
    while (limit > 0 && ((unsigned char)text[limit] & 0xC0u) == 0x80u)
    {
        limit--;
    }
    return limit;
}

size_t maudCopyUtf8(char* out, size_t capacity, const char* text)
{
    size_t length = maudCutUtf8(text, capacity - 1);
    memcpy(out, text, length);
    out[length] = '\0';
    return length;
}

// Writes code point as UTF-8 at out + length when it fits before the
// NUL's byte; false when it does not.
static bool Put(char* out, size_t capacity, size_t* length, uint32_t code)
{
    unsigned char bytes[4];
    size_t size = 0;
    if (code < 0x80u)
    {
        bytes[size++] = (unsigned char)code;
    }
    else if (code < 0x800u)
    {
        bytes[size++] = (unsigned char)(0xC0u | (code >> 6));
        bytes[size++] = (unsigned char)(0x80u | (code & 0x3Fu));
    }
    else if (code < 0x10000u)
    {
        bytes[size++] = (unsigned char)(0xE0u | (code >> 12));
        bytes[size++] = (unsigned char)(0x80u | ((code >> 6) & 0x3Fu));
        bytes[size++] = (unsigned char)(0x80u | (code & 0x3Fu));
    }
    else
    {
        bytes[size++] = (unsigned char)(0xF0u | (code >> 18));
        bytes[size++] = (unsigned char)(0x80u | ((code >> 12) & 0x3Fu));
        bytes[size++] = (unsigned char)(0x80u | ((code >> 6) & 0x3Fu));
        bytes[size++] = (unsigned char)(0x80u | (code & 0x3Fu));
    }
    if (size > capacity - 1 - *length)
    {
        return false;
    }
    memcpy(out + *length, bytes, size);
    *length += size;
    return true;
}

static bool IsHigh(uint16_t unit)
{
    return unit >= 0xD800u && unit < 0xDC00u;
}

static bool IsLow(uint16_t unit)
{
    return unit >= 0xDC00u && unit < 0xE000u;
}

size_t maudUtf16ToUtf8(char* out, size_t capacity, const uint16_t* units, size_t count)
{
    size_t length = 0;
    bool fits = true;
    for (size_t i = 0; i < count && fits; ++i)
    {
        uint32_t code = units[i];
        if (IsHigh(units[i]) && i + 1 < count && IsLow(units[i + 1]))
        {
            code = 0x10000u + ((code - 0xD800u) << 10) + (units[i + 1] - 0xDC00u);
            ++i;
        }
        else if (IsHigh(units[i]) || IsLow(units[i]) || code == 0)
        {
            code = 0xFFFDu;
        }
        fits = Put(out, capacity, &length, code);
    }
    out[length] = '\0';
    return length;
}
