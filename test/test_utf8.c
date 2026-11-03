// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cutting device names to the context's text limit, and copying and
// encoding them into fixed buffers, without splitting a UTF-8 sequence;
// encoding keys whole or not at all.

#include "test_harness.h"
#include "utf8.h"

#include <string.h>

int main(void)
{
    CHECK(maudCutUtf8("speaker", 64) == 7, "a short name is whole");
    CHECK(maudCutUtf8("speaker", 7) == 7, "a name of the limit's length is whole");
    CHECK(maudCutUtf8("speaker", 3) == 3, "ASCII cuts at the limit");
    // "hoparlör": the cut at 7 would split the two bytes of 'ö'.
    CHECK(maudCutUtf8("hoparl\xC3\xB6r", 7) == 6, "a two-byte sequence is not split");
    CHECK(maudCutUtf8("hoparl\xC3\xB6r", 8) == 8, "a whole sequence is kept");
    // A four-byte sequence cut anywhere inside goes whole.
    CHECK(maudCutUtf8("a\xF0\x9F\x94\x8A", 3) == 1, "a four-byte sequence is not split");
    CHECK(maudCutUtf8("\xC3\xB6", 1) == 0, "nothing fits");
    char out[8];
    CHECK(maudCopyUtf8(out, sizeof(out), "hoparl\xC3\xB6r") == 6 && strcmp(out, "hoparl") == 0,
          "a copy keeps the whole sequences that fit with the NUL");
    CHECK(maudCopyUtf8(out, sizeof(out), "kulak") == 5 && strcmp(out, "kulak") == 0,
          "a short text is copied whole");
    CHECK(maudCopyUtf8(out, 1, "kulak") == 0 && out[0] == '\0', "room for the NUL alone");
    // "a" and U+1F50A, a surrogate pair, encode to five bytes.
    const uint16_t speaker[] = {'a', 0xD83D, 0xDD0A};
    CHECK(maudUtf16ToUtf8(out, sizeof(out), speaker, 3) == 5 &&
              memcmp(out, "a\xF0\x9F\x94\x8A", 6) == 0,
          "a surrogate pair is one four-byte character");
    CHECK(maudUtf16ToUtf8(out, 5, speaker, 3) == 1 && strcmp(out, "a") == 0,
          "a character that does not fit is left out whole");
    // U+00F6 two bytes, U+20AC three.
    const uint16_t mixed[] = {0x00F6, 0x20AC};
    CHECK(maudUtf16ToUtf8(out, sizeof(out), mixed, 2) == 5 &&
              memcmp(out, "\xC3\xB6\xE2\x82\xAC", 6) == 0,
          "two- and three-byte characters");
    const uint16_t lone[] = {0xDC00, 0xD800, 0};
    CHECK(maudUtf16ToUtf8(out, sizeof(out), lone, 2) == 6 &&
              memcmp(out, "\xEF\xBF\xBD\xEF\xBF\xBD", 7) == 0,
          "a surrogate without its pair is U+FFFD");
    // A USB product name with a lone surrogate, which has no UTF8String
    // on iOS and is read as UTF-16 there.
    const uint16_t usb[] = {'M', 'i', 'c', 0xD83D, '!'};
    CHECK(maudUtf16ToUtf8(out, sizeof(out), usb, 5) == 7 && memcmp(out, "Mic\xEF\xBF\xBD!", 8) == 0,
          "a lone surrogate inside a name keeps the rest of it");
    CHECK(maudUtf16ToUtf8(out, sizeof(out), &lone[2], 1) == 3 &&
              memcmp(out, "\xEF\xBF\xBD", 4) == 0,
          "and so is U+0000, which would end the text");
    CHECK(maudUtf16ToUtf8(out, sizeof(out), speaker, 0) == 0 && out[0] == '\0', "no units");
    // A key is encoded whole and unchanged, or refused.
    CHECK(maudUtf16ToKey(out, sizeof(out), speaker, 3) && memcmp(out, "a\xF0\x9F\x94\x8A", 6) == 0,
          "a key that fits is encoded whole");
    CHECK(maudUtf16ToKey(out, 6, speaker, 3), "a key of the buffer's length with the NUL fits");
    CHECK(!maudUtf16ToKey(out, 5, speaker, 3), "a key one byte too long is refused, never cut");
    CHECK(!maudUtf16ToKey(out, sizeof(out), usb, 5), "a key with a lone surrogate is refused");
    CHECK(!maudUtf16ToKey(out, sizeof(out), &lone[2], 1), "and so is one with U+0000");
    CHECK(maudUtf16ToKey(out, 1, speaker, 0) && out[0] == '\0', "an empty key fits");
    return s_failures == 0 ? 0 : 1;
}
