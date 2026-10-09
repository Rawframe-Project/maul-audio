// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Uses the installed library as a consumer would, in C17: it checks that
// the linked library is the version it was compiled against, renders a
// stream on the offline backend, and steps a spatializer for one source.

#include "maul-audio/base.h"
#include "maul-audio/context.h"
#include "maul-audio/spatializer.h"
#include "maul-audio/stream.h"

#include <math.h>
#include <stdio.h>

static unsigned s_blocks;

static void Tone(const maudStreamBlock* block, void* user)
{
    (void)user;
    for (uint32_t i = 0; i < block->frameCount * 2; ++i)
    {
        block->output[i] = 0.1f;
    }
    s_blocks++;
}

int main(void)
{
    maudVersion version = maudGetVersion();
    if (version.major != MAUD_VERSION_MAJOR || version.minor != MAUD_VERSION_MINOR)
    {
        printf("FAIL: linked %u.%u, compiled against %d.%d\n", (unsigned)version.major,
               (unsigned)version.minor, MAUD_VERSION_MAJOR, MAUD_VERSION_MINOR);
        return 1;
    }
    // The Device part: 20 ms of a stream on the offline backend.
    maudContextDef contextDef = maudDefaultContextDef();
    contextDef.backend = maud_backendOffline;
    maudContext* context = NULL;
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.mode = maud_modePull;
    streamDef.callback = Tone;
    maudStreamId stream = {0, 0};
    static float frames[2 * 960];
    int ok = maudCreateContext(&contextDef, &context) == maud_success &&
             maudCreateStream(context, &streamDef, &stream) == maud_success &&
             maudStartStream(context, stream) == maud_success &&
             maudRenderStream(context, stream, frames, 960) == maud_success && s_blocks == 2 &&
             frames[0] == 0.1f;
    if (context != NULL && maudDestroyContext(context) != maud_success)
    {
        ok = 0;
    }
    // The Spatial part: a source 3 m ahead, one direct step.
    maudSpatializerDef spatialDef = maudDefaultSpatializerDef();
    maudSourceDef sourceDef = maudDefaultSourceDef();
    maudSpatializer* spatializer = NULL;
    maudSourceId source = {0, 0};
    const maudPose at = {{0.0f, 0.0f, -3.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    const maudPose listener = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudDirectResult result = {0};
    ok = ok && maudCreateSpatializer(&spatialDef, &spatializer) == maud_success &&
         maudCreateSource(spatializer, &sourceDef, &source) == maud_success &&
         maudSetSourcePose(spatializer, source, &at) == maud_success &&
         maudSimulateDirect(spatializer, &listener) == maud_success &&
         maudLatchResults(spatializer) == 1 &&
         maudGetDirectResult(spatializer, source, &result) == maud_success &&
         fabsf(result.distance - 3.0f) < 1e-4f;
    maudDestroySpatializer(spatializer);
    printf("%s: maul-audio %u.%u.%u, %u blocks, a source %.2f m away\n", ok ? "ok" : "FAIL",
           (unsigned)version.major, (unsigned)version.minor, (unsigned)version.patch, s_blocks,
           (double)result.distance);
    return ok ? 0 : 1;
}
