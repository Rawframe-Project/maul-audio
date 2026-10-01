// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams: one direction of audio between a host and a device, in
// interleaved 32-bit float frames, handed to the host's real-time
// callback one fixed period at a time.

#ifndef MAUL_AUDIO_STREAM_H
#define MAUL_AUDIO_STREAM_H

#include "maul-audio/context.h"
#include "maul-audio/layout.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Names a stream of a context: a 1-based slot, 0 for the null id, and
    // a generation that tells a live stream from earlier occupants of its
    // slot.
    typedef struct maudStreamId
    {
        uint32_t index1;
        uint32_t generation;
    } maudStreamId;

    // Which way samples move.
    typedef uint8_t maudStreamDirection;

    enum
    {
        // From the host to the device: playback.
        maud_directionOutput = 0,
        // From the device to the host: capture.
        maud_directionInput = 1,
    };

    // Which thread runs the stream's period loop.
    typedef uint8_t maudStreamMode;

    enum
    {
        // The platform's audio thread, or one the library starts where the
        // platform has none.
        maud_modeCallback = 0,
        // The host's own thread, through the library; the only mode of the
        // offline backend.
        maud_modePull = 1,
    };

    // How a stream's rate is chosen.
    typedef uint8_t maudRatePolicy;

    enum
    {
        // The device's own rate. The def's sampleRate must be 0.
        maud_rateNative = 0,
        // The def's sampleRate, which the device must run at without
        // conversion, or the stream is refused.
        maud_rateRequired = 1,
        // The def's sampleRate, converted by the platform's own converter.
        // Refused where the platform has none.
        maud_ratePlatformConverted = 2,
    };

    // One period of a stream, as its callback sees it.
    typedef struct maudStreamBlock
    {
        // Output streams: frameCount interleaved frames to fill, cleared to
        // silence before the call. NULL for input streams.
        float* output;
        // Input streams: frameCount interleaved frames captured. NULL for
        // output streams.
        const float* input;
        uint32_t frameCount;
        uint32_t sampleRate;
        maudChannelLayout layout;
        // The stream frame index of the block's first frame.
        uint64_t position;
    } maudStreamBlock;

    // The host's real-time callback. It must not allocate, lock, wait or
    // make any control call on the stream's context; control calls made
    // from it are refused with maud_errorState.
    typedef void (*maudStreamCallback)(const maudStreamBlock* block, void* user);

    // How a stream is made. Build it with maudDefaultStreamDef.
    typedef struct maudStreamDef
    {
        uint32_t cookie;
        maudStreamDirection direction;
        maudStreamMode mode;
        maudRatePolicy ratePolicy;
        maudChannelLayout layout;
        // Frames per second for maud_rateRequired and
        // maud_ratePlatformConverted, from 8,000 to 384,000; 0 for
        // maud_rateNative.
        uint32_t sampleRate;
        // Frames per callback; 0 asks for the backend's default, 10 ms on
        // the offline backend.
        uint32_t periodFrames;
        maudStreamCallback callback;
        void* user;
    } maudStreamDef;

    // What a stream runs at.
    typedef struct maudStreamFormat
    {
        uint32_t sampleRate;
        uint32_t periodFrames;
        maudChannelLayout layout;
        // The policy in effect.
        maudRatePolicy ratePolicy;
    } maudStreamFormat;

    /// Returns the default stream def: an output stream in callback mode,
    /// stereo, at the device's native rate, with the backend's default
    /// period and no callback.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudStreamDef maudDefaultStreamDef(void);

    /// Creates a stream, stopped, and allocates everything it will use.
    ///
    /// @param context      The context.
    /// @param def          The def, from maudDefaultStreamDef, with a
    ///                     callback.
    /// @param streamIdOut  Receives the stream's id; the null id on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL argument, a def
    ///         without its cookie or callback, or a value out of range;
    ///         `maud_errorUnsupported` for what the backend cannot do (the
    ///         offline backend has only pull mode and no converter);
    ///         `maud_errorCapacity` past the stream or period limit or when
    ///         the allocator fails; `maud_errorState` on a thread rendering
    ///         one of the context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudCreateStream(maudContext* context,
                                                        const maudStreamDef* def,
                                                        maudStreamId* streamIdOut);

    /// Destroys a stream. Its id becomes stale.
    ///
    /// @param context  The context.
    /// @param stream   The stream.
    /// @return `maud_success`; `maud_errorStale` for an id that names no
    ///         stream; `maud_errorInvalid` for a NULL context;
    ///         `maud_errorState` on a thread rendering one of the context's
    ///         streams, or while the stream renders.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudDestroyStream(maudContext* context, maudStreamId stream);

    /// Starts a stream. Starting a started stream does nothing.
    ///
    /// @param context  The context.
    /// @param stream   The stream.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL context; `maud_errorState` on a thread rendering one of
    ///         the context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudStartStream(maudContext* context, maudStreamId stream);

    /// Stops a stream. Stopping a stopped stream does nothing.
    ///
    /// @param context  The context.
    /// @param stream   The stream.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL context; `maud_errorState` on a thread rendering one of
    ///         the context's streams.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudStopStream(maudContext* context, maudStreamId stream);

    /// Reports what a stream runs at.
    ///
    /// @param context    The context.
    /// @param stream     The stream.
    /// @param formatOut  Receives the format.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudGetStreamFormat(const maudContext* context,
                                                           maudStreamId stream,
                                                           maudStreamFormat* formatOut);

    /// Reports how many frames a stream has moved to or from its device.
    ///
    /// @param context    The context.
    /// @param stream     The stream.
    /// @param framesOut  Receives the frame count.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudGetStreamPosition(const maudContext* context,
                                                             maudStreamId stream,
                                                             uint64_t* framesOut);

    /// Renders the next frames of an offline output stream into a caller
    /// buffer, calling the stream's callback once per period as needed, on
    /// the calling thread. The stream's clock advances by frameCount.
    ///
    /// @param context     The context.
    /// @param stream      A started output stream of an offline context.
    /// @param framesOut   Room for frameCount interleaved frames. May be NULL
    ///                    when frameCount is 0.
    /// @param frameCount  The number of frames.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer where frames are due, an input stream or a total
    ///         that does not fit in memory; `maud_errorUnsupported` on a
    ///         context that is not offline; `maud_errorState` for a stopped
    ///         stream or one already rendering.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. A stream renders on one
    /// thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudRenderStream(maudContext* context, maudStreamId stream,
                                                        float* framesOut, uint32_t frameCount);

    /// Feeds the next frames to an offline input stream from a caller
    /// buffer, calling the stream's callback once per completed period, on
    /// the calling thread. The stream's clock advances by frameCount.
    ///
    /// @param context     The context.
    /// @param stream      A started input stream of an offline context.
    /// @param frames      frameCount interleaved frames. May be NULL when
    ///                    frameCount is 0.
    /// @param frameCount  The number of frames.
    /// @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a
    ///         NULL pointer where frames are due, an output stream or a total
    ///         that does not fit in memory; `maud_errorUnsupported` on a
    ///         context that is not offline; `maud_errorState` for a stopped
    ///         stream or one already rendering.
    /// @par Thread safety
    /// Real-time safe: no allocation, lock or wait. A stream renders on one
    /// thread at a time.
    MAUD_NODISCARD MAUD_API maudResult maudFeedStream(maudContext* context, maudStreamId stream,
                                                      const float* frames, uint32_t frameCount);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_STREAM_H
