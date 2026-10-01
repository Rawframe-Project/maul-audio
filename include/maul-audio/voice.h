// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// First-party voice processing: a voice activity detector. It stands
// apart from contexts and streams; a host runs it on captured frames,
// in its callback or anywhere else.

#ifndef MAUL_AUDIO_VOICE_H
#define MAUL_AUDIO_VOICE_H

#include "maul-audio/base.h"
#include "maul-audio/layout.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A voice activity detector. Build its def with
    // maudDefaultVoiceDetectorDef.
    typedef struct maudVoiceDetectorDef
    {
        uint32_t cookie;
        // The frames' rate, from 8,000 to 384,000.
        uint32_t sampleRate;
        // The frames' layout; the detector listens to the channels' mean.
        maudChannelLayout layout;
        // How much evidence speech needs, from 0 to 3: a higher value
        // admits less noise and misses more quiet speech.
        uint8_t aggressiveness;
        // How long the detector stays active after speech ends.
        uint32_t hangoverMilliseconds;
        // Where its memory comes from; all zero for the library's default.
        maudAllocator allocator;
    } maudVoiceDetectorDef;

    // What a detector has concluded from the frames so far.
    typedef struct maudVoiceState
    {
        // Whether voice is active at the last whole 10 ms frame.
        bool active;
        // How likely that frame is speech, from 0 to 1.
        float probability;
        // That frame's level and the noise floor under it, in dBFS (a
        // full-scale sine is -3).
        float levelDbfs;
        float noiseDbfs;
        // The 10 ms frames analyzed so far.
        uint64_t frames;
    } maudVoiceState;

    typedef struct maudVoiceDetector maudVoiceDetector;

    /// Returns the default voice detector def: 48,000, mono,
    /// aggressiveness 1, a 200 ms hangover, the default allocator.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudVoiceDetectorDef maudDefaultVoiceDetectorDef(void);

    /// Creates a voice activity detector.
    ///
    /// @param def          The def, from maudDefaultVoiceDetectorDef.
    /// @param detectorOut  Receives the detector; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a
    ///         def out of range; `maud_errorCapacity` when the allocator
    ///         fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateVoiceDetector(const maudVoiceDetectorDef* def,
                                                               maudVoiceDetector** detectorOut);

    /// Destroys a voice activity detector. NULL is ignored.
    ///
    /// @param detector  The detector.
    /// @par Thread safety
    /// Safe from any thread; the detector is used by one thread at a time.
    MAUD_API void maudDestroyVoiceDetector(maudVoiceDetector* detector);

    /// Analyzes interleaved frames, in any count: what does not complete
    /// a 10 ms frame waits for the next call, so the results do not
    /// depend on how the frames are cut.
    ///
    /// @param detector    The detector.
    /// @param frames      frameCount frames in the def's layout; may be NULL
    ///                    when frameCount is 0.
    /// @param frameCount  How many.
    /// @param stateOut    Receives the state after them; may be NULL.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL detector, or
    ///         NULL frames with a frameCount.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The detector is used
    /// by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudDetectVoice(maudVoiceDetector* detector,
                                                       const float* frames, uint32_t frameCount,
                                                       maudVoiceState* stateOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_VOICE_H
