// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A frame's place in a block, n + 1, as a float, as the ramps across a
// block take it. SSE2 has no unsigned conversion, so converting the
// unsigned index took several instructions in each sample's loop; the
// signed conversion is one on every target and gives the same value
// below 2^31. A caller takes it for a block shorter than that, and the
// unsigned one otherwise, each loop specialised by the constant.

#ifndef MAUL_AUDIO_SRC_RAMP_H
#define MAUL_AUDIO_SRC_RAMP_H

#include "maul-audio/base.h"

#include <stdbool.h>
#include <stdint.h>

// Whether frames is short enough for the signed conversion.
MAUD_NODISCARD static inline bool maudRampSigned(uint32_t frames)
{
    return frames <= (uint32_t)INT32_MAX;
}

static inline float maudFramePlace(uint32_t n, bool isSigned)
{
    return isSigned ? (float)(int32_t)(n + 1) : (float)(n + 1);
}

// A ramp's gain at frame n of frames, from a to b, for a ramp whose
// change b - a does not fit a float (two finite gains of opposite sign
// near the float range). Taken in double, every frame's gain lies
// between a and b, and the last is b.
static inline float maudWideRampGain(float a, float b, uint32_t n, uint32_t frames)
{
    double change = (double)b - (double)a;
    return (float)((double)a + change * ((double)n + 1.0) / (double)frames);
}

#endif // MAUL_AUDIO_SRC_RAMP_H
