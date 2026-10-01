// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Device part's root: a context holds the connection to a backend
// and the streams opened through it.

#ifndef MAUL_AUDIO_CONTEXT_H
#define MAUL_AUDIO_CONTEXT_H

#include "maul-audio/base.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // A context. Create it with maudCreateContext.
    typedef struct maudContext maudContext;

    // Which backend a context talks to.
    typedef uint8_t maudBackendKind;

    enum
    {
        // The platform's own audio system.
        maud_backendNative = 0,
        // Rendering to caller buffers at a caller-driven clock: no device,
        // no thread, the same samples from the same inputs. Always built.
        maud_backendOffline = 1,
    };

    // The named limits of a context. A request past one is refused.
    typedef struct maudLimits
    {
        // Streams that exist at once.
        uint16_t streams;
        // The largest period a stream may ask for, in frames.
        uint32_t periodFrames;
    } maudLimits;

    // How a context is made. Build it with maudDefaultContextDef.
    typedef struct maudContextDef
    {
        uint32_t cookie;
        maudAllocator allocator;
        maudLimits limits;
        maudBackendKind backend;
        // The rate the offline backend's device runs at, in frames per
        // second, from 8,000 to 384,000.
        uint32_t offlineSampleRate;
    } maudContextDef;

    /// Returns the default context def: 8 streams, periods of at most
    /// 8,192 frames, the C library's allocator, the native backend and an
    /// offline rate of 48,000.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudContextDef maudDefaultContextDef(void);

    /// Creates a context.
    ///
    /// @param def         The def, from maudDefaultContextDef.
    /// @param contextOut  Receives the context; set to NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL argument, a def
    ///         without its cookie, an allocator with one function, a limit
    ///         of 0 or an offline rate out of range; `maud_errorUnsupported`
    ///         when this build has no native backend for the platform;
    ///         `maud_errorCapacity` when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateContext(const maudContextDef* def,
                                                         maudContext** contextOut);

    /// Destroys a context and every stream it still holds.
    ///
    /// @param context  The context. NULL does nothing.
    /// @return `maud_success`, or `maud_errorState` when called on a thread
    ///         that is rendering one of its streams; the context is then
    ///         left as it was and the call counts as misuse.
    /// @par Thread safety
    /// Safe from any thread; the context is used by one thread at a time.
    /// No stream of the context may be rendering on another thread.
    MAUD_NODISCARD MAUD_API maudResult maudDestroyContext(maudContext* context);

    /// Returns the backend a context talks to.
    ///
    /// @param context  The context.
    /// @return The backend kind.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudBackendKind maudGetContextBackend(const maudContext* context);

    /// Returns how many calls the context refused as misuse: invalid
    /// arguments against it, and control calls made on a thread that was
    /// rendering one of its streams. Stale ids are not misuse.
    ///
    /// @param context  The context.
    /// @return The count since the context was created.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API uint64_t maudGetContextMisuse(const maudContext* context);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_CONTEXT_H
