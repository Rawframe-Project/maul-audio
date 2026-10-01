// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Cutting device names to the context's text limit without splitting a
// UTF-8 sequence.

#include "device.h"
#include "test_harness.h"

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
    return s_failures == 0 ? 0 : 1;
}
