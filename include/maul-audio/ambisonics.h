// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The ambisonic bed: sources encoded into a sound field of order 1 to 3
// and the field rotated, in planar channels the host owns. Channels
// follow AmbiX: ACN order with SN3D normalization, the field's axes x
// ahead, y left and z up; directions and rotations are given in the
// listener's frame (maudVector3) and converted. Every function here
// allocates nothing and does work in proportion to its frames.

#ifndef MAUL_AUDIO_AMBISONICS_H
#define MAUL_AUDIO_AMBISONICS_H

#include "maul-audio/base.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The highest order, and the channels it takes: (order + 1) squared.
#define MAUD_MAX_AMBISONIC_ORDER    3
#define MAUD_MAX_AMBISONIC_CHANNELS 16

    // A rotation as a unit quaternion in the listener's frame: a sound
    // from direction d comes out from the rotated d. Any nonzero length is
    // normalized.
    typedef struct maudQuaternion
    {
        float x;
        float y;
        float z;
        float w;
    } maudQuaternion;

    // A source as the bed hears it: where it is (only the direction
    // counts; a zero vector is straight ahead) and its gain.
    typedef struct maudAmbisonicSource
    {
        maudVector3 direction;
        float gain;
    } maudAmbisonicSource;

    /// Returns the channels a bed of an order takes.
    ///
    /// @param order  1 to MAUD_MAX_AMBISONIC_ORDER.
    /// @return (order + 1) squared, or 0 for an order out of range.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API uint32_t maudGetAmbisonicChannelCount(uint32_t order);

    /// Computes the gains that encode a direction: one per channel of the
    /// order, in ACN order with SN3D normalization.
    ///
    /// @param order      1 to MAUD_MAX_AMBISONIC_ORDER.
    /// @param direction  The direction, in the listener's frame.
    /// @param gainsOut   Receives the order's channel count of gains.
    /// @return `maud_success`, or `maud_errorInvalid` for an order out of
    ///         range, a NULL pointer or a direction that is not finite.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudGetAmbisonicGains(uint32_t order, maudVector3 direction,
                                                             float* gainsOut);

    /// Adds a mono source into a bed, its direction and gain moving
    /// linearly from `from` to `to` across the call: frame n is encoded
    /// with the gains (n + 1) / frames of the way.
    ///
    /// @param order   The bed's order, 1 to MAUD_MAX_AMBISONIC_ORDER.
    /// @param from    The source at the previous call's end.
    /// @param to      The source at this call's end.
    /// @param in      frames samples.
    /// @param bed     The order's channel count of channels, frames each,
    ///                added to.
    /// @param frames  The frames.
    /// @return `maud_success`, or `maud_errorInvalid` for an order out of
    ///         range, a NULL pointer or a value that is not finite;
    ///         nothing is written then.
    /// @par Thread safety
    /// Safe from any thread; the bed is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudEncodeAmbisonic(uint32_t order,
                                                           const maudAmbisonicSource* from,
                                                           const maudAmbisonicSource* to,
                                                           const float* in, float* const* bed,
                                                           uint32_t frames);

    /// Rotates a bed in place, the rotation moving linearly from `from`
    /// to `to` across the call (a crossfade of the two rotated fields; a
    /// large change within one call blends rather than turns).
    ///
    /// @param order   The bed's order, 1 to MAUD_MAX_AMBISONIC_ORDER.
    /// @param from    The rotation at the previous call's end.
    /// @param to      The rotation at this call's end.
    /// @param bed     The order's channel count of channels, frames each.
    /// @param frames  The frames.
    /// @return `maud_success`, or `maud_errorInvalid` for an order out of
    ///         range, a NULL pointer, or a quaternion of zero length or
    ///         not finite; nothing is written then.
    /// @par Thread safety
    /// Safe from any thread; the bed is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudRotateAmbisonic(uint32_t order,
                                                           const maudQuaternion* from,
                                                           const maudQuaternion* to,
                                                           float* const* bed, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_AMBISONICS_H
