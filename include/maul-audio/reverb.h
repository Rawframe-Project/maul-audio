// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The parametric reverb: a feedback delay network whose decay follows
// reverberation times given per band (up to 800 Hz, 800 Hz to 8 kHz,
// above 8 kHz). It takes the host's mono reverb send and adds a diffuse
// tail into a first-order ambisonic bed, which the binaural and speaker
// decoders render. One per listener; it allocates nothing once made.

#ifndef MAUL_AUDIO_REVERB_H
#define MAUL_AUDIO_REVERB_H

#include "maul-audio/base.h"
#include "maul-audio/direct.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A reverb.
    typedef struct maudReverb maudReverb;

    // How to create a reverb. Build it with maudDefaultReverbDef.
    typedef struct maudReverbDef
    {
        uint32_t cookie;
        // The rate it runs at, 44,100 to 384,000 Hz.
        float sampleRate;
        maudAllocator allocator;
    } maudReverbDef;

    // What the reverb does during one call.
    typedef struct maudReverbParams
    {
        // The time to decay by 60 dB in each band, 0.1 to 20 s, met at
        // the bands' geometric centres (126 Hz, 2.5 kHz, 12.6 kHz) and
        // between them smoothly.
        float reverbTime[MAUD_DIRECT_BANDS];
    } maudReverbParams;

    /// Returns the default reverb def: 48 kHz.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudReverbDef maudDefaultReverbDef(void);

    /// Creates a reverb, silent.
    ///
    /// @param def        The def, from maudDefaultReverbDef.
    /// @param reverbOut  Receives the reverb; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie or a rate out of range;
    ///         `maud_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateReverb(const maudReverbDef* def,
                                                        maudReverb** reverbOut);

    /// Destroys a reverb. NULL is ignored.
    ///
    /// @param reverb  The reverb.
    /// @par Thread safety
    /// Safe from any thread; the reverb is used by one thread at a time.
    MAUD_API void maudDestroyReverb(maudReverb* reverb);

    /// Runs frames of the send through the reverb, adding its tail into a
    /// first-order bed (four channels, ACN order, SN3D). When the times
    /// change, the decay moves to them across the call; refitting the
    /// filters then takes some tens of microseconds on the calling
    /// thread, bounded and without allocation.
    ///
    /// @param reverb  The reverb.
    /// @param params  This call's params.
    /// @param in      frames samples of the send.
    /// @param bed     Four channels of frames samples, added to.
    /// @param frames  The frames; 0 does nothing.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or
    ///         a time out of range or not finite; nothing is written then.
    /// @par Thread safety
    /// Safe from any thread; the reverb is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudProcessReverb(maudReverb* reverb,
                                                         const maudReverbParams* params,
                                                         const float* in, float* const* bed,
                                                         uint32_t frames);

    /// Silences a reverb's tail.
    ///
    /// @param reverb  The reverb.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer.
    /// @par Thread safety
    /// Safe from any thread; the reverb is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudResetReverb(maudReverb* reverb);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_REVERB_H
