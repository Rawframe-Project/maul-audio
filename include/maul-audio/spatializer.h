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

    // How a source's occlusion is found.
    typedef uint8_t maudOcclusionMethod;
    enum
    {
        // Not at all: the path is clear.
        maud_occlusionNone = 0,
        // By one ray from the listener to the source: 0 or 1.
        maud_occlusionRay = 1,
        // By points in a sphere around the source: of the points the
        // source sees, the share the listener does not.
        maud_occlusionVolumetric = 2,
    };

    // A ray the spatializer asks about: from origin along a unit
    // direction, between minDistance and maxDistance.
    typedef struct maudRay
    {
        maudVector3 origin;
        maudVector3 direction;
        float minDistance;
        float maxDistance;
    } maudRay;

    // Answers whether each of count rays hits anything within its
    // distances, writing 1 or 0 to occluded[i]. A ray's answer must
    // depend only on the ray and the host's geometry. Called from inside
    // a step, on its thread or on the host's tasks, never on the audio
    // thread, with at most 64 rays at a time.
    typedef void maudAnyHitFn(const maudRay* rays, uint32_t count, uint8_t* occluded,
                              void* context);

    // The task hooks: enqueueTask has the host run task over [0,
    // itemCount) in ranges of at least minRange items, each exactly once,
    // on any threads in any order, and returns a handle that finishTask
    // waits on. Results do not depend on the split.
    typedef void maudTaskFn(uint32_t start, uint32_t end, void* taskContext);
    typedef void* maudEnqueueTaskFn(maudTaskFn* task, uint32_t itemCount, uint32_t minRange,
                                    void* taskContext, void* userContext);
    typedef void maudFinishTaskFn(void* userTask, void* userContext);

    // How to create a spatializer. Build it with
    // maudDefaultSpatializerDef.
    typedef struct maudSpatializerDef
    {
        uint32_t cookie;
        // The most sources at once, 1 to 65,536.
        uint32_t sourceCapacity;
        // The most occlusion points a volumetric source may have, 1 to
        // 1,024.
        uint32_t maxOcclusionSamples;
        // The host's any-hit query; NULL leaves every path clear.
        maudAnyHitFn* anyHit;
        void* rayContext;
        // The task hooks; both NULL runs queries on the step's thread.
        maudEnqueueTaskFn* enqueueTask;
        maudFinishTaskFn* finishTask;
        void* userTaskContext;
        maudAllocator allocator;
    } maudSpatializerDef;

    // How to create a source. Build it with maudDefaultSourceDef.
    typedef struct maudSourceDef
    {
        uint32_t cookie;
        // The source's directivity.
        maudDirectivityPattern directivity;
        // How its occlusion is found.
        maudOcclusionMethod occlusion;
        // For volumetric occlusion: the sphere's radius in metres, above
        // 0, and its points, 1 to the spatializer's maxOcclusionSamples.
        float occlusionRadius;
        uint32_t occlusionSamples;
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

    /// Returns the default spatializer def: 256 sources, up to 64
    /// occlusion points each, no ray query, no task hooks.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudSpatializerDef maudDefaultSpatializerDef(void);

    /// Returns the default source def: omnidirectional, occlusion by one
    /// ray, and for volumetric occlusion a sphere of 1 m with 32 points.
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
    ///         def without its cookie, a capacity out of range or one task
    ///         hook without the other; `maud_errorCapacity` when the
    ///         allocator fails.
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
    ///         def without its cookie, an invalid directivity pattern, an
    ///         unknown occlusion method, or a volumetric radius or point
    ///         count out of range;
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

    /// Runs a direct step for a listener and publishes its results. Its
    /// occlusion rays go to the any-hit query in a fixed order and in
    /// batches of at most 64, through the task hooks if set; no result
    /// depends on how the tasks split the work.
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
