// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Device text as platforms report it, cut to fit without splitting a
// UTF-8 sequence.

#ifndef MAUL_AUDIO_SRC_UTF8_H
#define MAUL_AUDIO_SRC_UTF8_H

#include <stddef.h>
#include <stdint.h>

// The length of text cut to at most limit bytes without splitting a
// UTF-8 sequence: a device name as a backend reports it, cut to the
// context's text limit.
size_t maudCutUtf8(const char* text, size_t limit);

// Copies text into out, capacity bytes with the terminating NUL (at
// least 1), cut after the last whole UTF-8 sequence that fits; returns
// the bytes copied, the NUL not counted.
size_t maudCopyUtf8(char* out, size_t capacity, const char* text);

// Encodes count UTF-16 units as UTF-8 into out, capacity bytes with the
// terminating NUL (at least 1), cut after the last whole character that
// fits; returns the bytes written, the NUL not counted. A surrogate pair
// is one four-byte character; a surrogate without its pair, and U+0000,
// become U+FFFD. Each unit takes at least a byte, so count units cut
// short of a string never end the text early with half a pair once
// count is at least capacity.
size_t maudUtf16ToUtf8(char* out, size_t capacity, const uint16_t* units, size_t count);

#endif // MAUL_AUDIO_SRC_UTF8_H
