// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The cost of the control calls a host makes every frame, each of which
// first checks it is not made from host code the object runs: 256
// sources' poses set on a spatializer, and an empty notification queue
// drained on an offline context.
// Prints the best of seven runs, each row beside its recorded baseline.

// fopen reads the baseline; the C runtime's warning that it is unsafe
// is about the Annex K alternative, which the family does not use.
#define _CRT_SECURE_NO_WARNINGS

#include "baseline.h"

#include "maul-audio/context.h"
#include "maul-audio/notification.h"
#include "maul-audio/spatializer.h"

#include <stdio.h>
#include <time.h>

#define SOURCES 256
#define FRAMES  20000
#define DRAINS  2000000

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static void Poses(void)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    maudSpatializer* s = nullptr;
    if (maudCreateSpatializer(&def, &s) != maud_success)
    {
        return;
    }
    maudSourceDef sd = maudDefaultSourceDef();
    maudSourceId ids[SOURCES];
    for (int i = 0; i < SOURCES; ++i)
    {
        if (maudCreateSource(s, &sd, &ids[i]) != maud_success)
        {
            return;
        }
    }
    double best = 1e9;
    for (int run = 0; run < 7; ++run)
    {
        double start = Seconds();
        for (int frame = 0; frame < FRAMES; ++frame)
        {
            for (int i = 0; i < SOURCES; ++i)
            {
                maudPose pose = {{(float)i, 1.5f, (float)frame * 1e-3f}, {0.0f, 0.0f, 0.0f, 1.0f}};
                if (maudSource_SetPose(s, ids[i], &pose) != maud_success)
                {
                    return;
                }
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    printf("poses: %.3f us a frame for %d sources\n", best / FRAMES * 1e6, SOURCES);
    Against("control.poses.us", best / FRAMES * 1e6, false);
    maudDestroySpatializer(s);
}

static void Drain(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    maudContext* context = nullptr;
    if (maudCreateContext(&def, &context) != maud_success)
    {
        return;
    }
    maudNotification notification;
    double best = 1e9;
    for (int run = 0; run < 7; ++run)
    {
        double start = Seconds();
        for (int i = 0; i < DRAINS; ++i)
        {
            (void)maudContext_NextNotification(context, &notification);
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    printf("drain: %.2f ns an empty notification queue\n", best / DRAINS * 1e9);
    Against("control.drain.ns", best / DRAINS * 1e9, false);
    (void)maudDestroyContext(context);
}

int main(void)
{
    Poses();
    Drain();
    return 0;
}
