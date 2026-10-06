// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Spatial part's root. A spatializer has two sides. The simulation
// side, called from the host's own threads or tasks, makes sources, sets
// their poses and runs steps that work out what the direct path does to
// each source. The rendering side, on the audio thread, latches the
// newest step and reads each source's result without allocating, locking
// or waiting; a step is published whole, never torn. The host passes the
// results to the direct effect and to the binaural effect or a panner.
//
// Positions are in metres in the host's world, right-handed with +y up;
// an orientation turns the frame of the listener or a source (+x right,
// +y up, -z ahead) into the world's.

#ifndef MAUL_AUDIO_SPATIALIZER_H
#define MAUL_AUDIO_SPATIALIZER_H

#include "maul-audio/base.h"
#include "maul-audio/direct.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // A spatializer.
    typedef struct maudSpatializer maudSpatializer;

    // A source of a spatializer; 0 is no source.
    typedef struct maudSourceId
    {
        uint32_t index1;
        uint32_t generation;
    } maudSourceId;

    // Where something is and which way it faces.
    typedef struct maudPose
    {
        maudVector3 position;
        // Turns the thing's frame into the world's; any nonzero length is
        // normalized.
        maudQuaternion orientation;
    } maudPose;

    // How to create a spatializer. Build it with
    // maudDefaultSpatializerDef.
    typedef struct maudSpatializerDef
    {
        uint32_t cookie;
        // The most sources at once, 1 to 65,536.
        uint32_t sourceCapacity;
        maudAllocator allocator;
    } maudSpatializerDef;

    // How to create a source. Build it with maudDefaultSourceDef.
    typedef struct maudSourceDef
    {
        uint32_t cookie;
        // The source's directivity.
        maudDirectivityPattern directivity;
    } maudSourceDef;

    // What a step found for a source.
    typedef struct maudDirectResult
    {
        // From the listener to the source, in metres.
        float distance;
        // The direction from the listener to the source in the listener's
        // frame, unit length (straight ahead when the two coincide).
        maudVector3 direction;
        // The source's directivity towards the listener, per band.
        float directivity[MAUD_DIRECT_BANDS];
        // How much of the source the path's obstacles hide, 0 to 1.
        float occlusion;
        // What passes through them, per band.
        float transmission[MAUD_DIRECT_BANDS];
    } maudDirectResult;

    /// Returns the default spatializer def: 256 sources.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudSpatializerDef maudDefaultSpatializerDef(void);

    /// Returns the default source def: omnidirectional.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudSourceDef maudDefaultSourceDef(void);

    /// Creates a spatializer, allocating everything it will use.
    ///
    /// @param def             The def, from maudDefaultSpatializerDef.
    /// @param spatializerOut  Receives the spatializer; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie or a capacity out of range;
    ///         `maud_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateSpatializer(const maudSpatializerDef* def,
                                                             maudSpatializer** spatializerOut);

    /// Destroys a spatializer and its sources. NULL is ignored. Neither
    /// side may be in use.
    ///
    /// @param spatializer  The spatializer.
    /// @par Thread safety
    /// Safe from any thread; the spatializer is used by one thread at a
    /// time.
    MAUD_API void maudDestroySpatializer(maudSpatializer* spatializer);

    /// Creates a source, at the world's origin facing -z until its pose
    /// is set. It appears in results from the next step on.
    ///
    /// @param spatializer  The spatializer.
    /// @param def          The def, from maudDefaultSourceDef.
    /// @param sourceOut    Receives the source; 0 on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie or an invalid directivity pattern;
    ///         `maud_errorCapacity` when the spatializer has its capacity
    ///         of sources.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudCreateSource(maudSpatializer* spatializer,
                                                        const maudSourceDef* def,
                                                        maudSourceId* sourceOut);

    /// Destroys a source. Results from steps before this still hold it;
    /// later steps do not.
    ///
    /// @param spatializer  The spatializer.
    /// @param source       The source.
    /// @return `maud_success`, `maud_errorInvalid` for a NULL spatializer
    ///         or a 0 or unknown id, or `maud_errorStale` for a destroyed
    ///         source's id.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudDestroySource(maudSpatializer* spatializer,
                                                         maudSourceId source);

    /// Sets a source's pose for the steps that follow.
    ///
    /// @param spatializer  The spatializer.
    /// @param source       The source.
    /// @param pose         The pose.
    /// @return `maud_success`, `maud_errorInvalid` for a NULL pointer, a 0
    ///         or unknown id, a value that is not finite or a zero
    ///         orientation, or `maud_errorStale` for a destroyed source's
    ///         id.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudSetSourcePose(maudSpatializer* spatializer,
                                                         maudSourceId source, const maudPose* pose);

    /// Runs a direct step for a listener and publishes its results.
    ///
    /// @param spatializer  The spatializer.
    /// @param listener     The listener's pose.
    /// @return `maud_success`, or `maud_errorInvalid` for a NULL pointer, a
    ///         value that is not finite or a zero orientation.
    /// @par Thread safety
    /// Safe from any thread; the simulation side is used by one thread at
    /// a time.
    MAUD_NODISCARD MAUD_API maudResult maudSimulateDirect(maudSpatializer* spatializer,
                                                          const maudPose* listener);

    /// Latches the newest published step for the rendering side; until
    /// the next latch, results come from it.
    ///
    /// @param spatializer  The spatializer.
    /// @return The step's number (the first step is 1), or 0 if none has
    ///         been published or spatializer is NULL.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The rendering side is
    /// used by one thread at a time.
    MAUD_API uint64_t maudLatchResults(maudSpatializer* spatializer);

    /// Reads a source's result from the latched step.
    ///
    /// @param spatializer  The spatializer.
    /// @param source       The source.
    /// @param resultOut    Receives the result.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a 0
    ///         or unknown id, or no latched step; `maud_errorStale` when
    ///         the latched step does not hold the source (made after it,
    ///         or destroyed before it).
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. The rendering side is
    /// used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudGetDirectResult(const maudSpatializer* spatializer,
                                                           maudSourceId source,
                                                           maudDirectResult* resultOut);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_SPATIALIZER_H
