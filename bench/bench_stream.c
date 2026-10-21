// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The stream path's own cost per 10 ms block at 48 kHz, the host's
// callback doing next to nothing: an offline stereo stream rendered, an
// offline object stream with 32 active objects rendered, an offline
// duplex stream fed a block of input and rendered (its input queued in
// the ring between the halves and taken back out), and the
// conversion of a stereo block to 16- and 32-bit integers and back, as
// the ALSA and WASAPI paths convert for devices that take integers.
// Prints the best of five runs in microseconds per block, each row
// beside its recorded baseline.

// fopen reads the baseline; the C runtime's warning that it is unsafe
// is about the Annex K alternative, which the family does not use.
#define _CRT_SECURE_NO_WARNINGS

#include "baseline.h"
#include "sample_convert.h"

#include "maul-audio/context.h"
#include "maul-audio/objects.h"
#include "maul-audio/stream.h"

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define BLOCK   480
#define BLOCKS  20000
#define OBJECTS 32

static float s_frames[2 * BLOCK];
static float s_input[2 * BLOCK];
static float s_objectFrames[OBJECTS][BLOCK];
static int32_t s_integers[2 * BLOCK];

static double Seconds(void)
{
    struct timespec now;
    timespec_get(&now, TIME_UTC);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

// Writes one sample a block, and keeps the objects active where they are.
static void Touch(const maudStreamBlock* block, void* user)
{
    (void)user;
    block->output[0] = 0.1f;
    for (uint32_t i = 0; i < block->objectCount; ++i)
    {
        block->objects[i].active = true;
        block->objects[i].samples[0] = 0.1f;
    }
}

static void Render(bool objects)
{
    maudContextDef cd = maudDefaultContextDef();
    cd.backend = maud_backendOffline;
    maudContext* context = nullptr;
    if (maudCreateContext(&cd, &context) != maud_success)
    {
        return;
    }
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.periodFrames = BLOCK;
    def.objectCount = objects ? OBJECTS : 0;
    def.callback = Touch;
    maudStreamId stream = {0, 0};
    if (maudCreateStream(context, &def, &stream) != maud_success ||
        maudStartStream(context, stream) != maud_success)
    {
        (void)maudDestroyContext(context);
        return;
    }
    maudStreamObject records[OBJECTS];
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int b = 0; b < BLOCKS; ++b)
        {
            for (uint32_t i = 0; objects && i < OBJECTS; ++i)
            {
                records[i] = (maudStreamObject){.samples = s_objectFrames[i]};
            }
            maudResult result =
                objects ? maudRenderObjects(context, stream, s_frames, records, OBJECTS, BLOCK)
                        : maudRenderStream(context, stream, s_frames, BLOCK);
            if (result != maud_success)
            {
                (void)maudDestroyContext(context);
                return;
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    double micro = best / BLOCKS * 1e6;
    printf("%-36s %7.3f us per 10 ms block\n",
           objects ? "offline object stream, 32 objects" : "offline stereo stream", micro);
    Against(objects ? "stream.objects.us" : "stream.render.us", micro, false);
    (void)maudDestroyContext(context);
}

// A duplex stream's block: the input half fed, the ring between the
// halves crossed, the output half rendered with the input beside it.
static void Duplex(void)
{
    maudContextDef cd = maudDefaultContextDef();
    cd.backend = maud_backendOffline;
    maudContext* context = nullptr;
    if (maudCreateContext(&cd, &context) != maud_success)
    {
        return;
    }
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionDuplex;
    def.mode = maud_modePull;
    def.periodFrames = BLOCK;
    def.callback = Touch;
    maudStreamId stream = {0, 0};
    if (maudCreateStream(context, &def, &stream) != maud_success ||
        maudStartStream(context, stream) != maud_success)
    {
        (void)maudDestroyContext(context);
        return;
    }
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int b = 0; b < BLOCKS; ++b)
        {
            if (maudFeedStream(context, stream, s_input, BLOCK) != maud_success ||
                maudRenderStream(context, stream, s_frames, BLOCK) != maud_success)
            {
                (void)maudDestroyContext(context);
                return;
            }
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    double micro = best / BLOCKS * 1e6;
    printf("%-36s %7.3f us per 10 ms block\n", "offline duplex stream, fed and rendered", micro);
    Against("stream.duplex.us", micro, false);
    (void)maudDestroyContext(context);
}

static void Convert(maudSampleKind kind, const char* name, const char* key)
{
    for (int i = 0; i < 2 * BLOCK; ++i)
    {
        s_frames[i] = (float)(i % 97) / 97.0f - 0.5f;
    }
    double best = 1e9;
    for (int run = 0; run < 5; ++run)
    {
        double start = Seconds();
        for (int b = 0; b < BLOCKS; ++b)
        {
            maudFloatToSamples(s_integers, s_frames, 2 * BLOCK, kind);
            maudSamplesToFloat(s_frames, s_integers, 2 * BLOCK, kind);
        }
        double elapsed = Seconds() - start;
        best = elapsed < best ? elapsed : best;
    }
    double micro = best / BLOCKS * 1e6;
    printf("%-36s %7.3f us per 10 ms block\n", name, micro);
    Against(key, micro, false);
}

int main(void)
{
    Render(false);
    Render(true);
    Duplex();
    Convert(maud_sampleInt16, "stereo to 16 bits and back", "convert.int16.us");
    Convert(maud_sampleInt32, "stereo to 32 bits and back", "convert.int32.us");
    return 0;
}
