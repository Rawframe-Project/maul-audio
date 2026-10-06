// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The near-field filter of one ear: how a rigid sphere of the head's
// radius changes the sound of a source at one distance against a source
// at the distance the HRTF set was measured at, as a gain and a
// first-order high shelf. Only the head's effect: the 1/r change of
// pressure is the host's distance attenuation.

#ifndef MAUL_AUDIO_SRC_NEAR_FIELD_H
#define MAUL_AUDIO_SRC_NEAR_FIELD_H

// y[n] = b0 x[n] + b1 x[n - 1] - a1 y[n - 1]; |a1| < 1.
typedef struct maudNearFieldFilter
{
    float b0;
    float b1;
    float a1;
} maudNearFieldFilter;

// The filter for an incidence angle in degrees (the angle at the head's
// centre between the ear and the source, 0 to 180), the source's and
// the set's distances as the head radius over the distance (0 for
// infinity; past 1 / 1.15 counts as 1 / 1.15), a head radius in metres
// and a sample rate. Equal distances give exactly the identity.
maudNearFieldFilter maudNearField(float angleDegrees, float inverseDistance,
                                  float setInverseDistance, float headRadius, float sampleRate);

#endif // MAUL_AUDIO_SRC_NEAR_FIELD_H
