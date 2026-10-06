// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Object streams on the offline backend, whose caller stands in for the
// platform's renderer: the defs refused, objects starting silent at the
// listener and inactive, frames and the latest placement handed out
// across any render size, placement kept between periods, and the
// render calls of the two kinds of stream kept apart.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/objects.h"
#include "maul-audio/stream.h"

#include <string.h>

#define OBJECTS 3u
#define PERIOD  256u

typedef struct Scene
{
    uint32_t blocks;
    bool startedClean;
    bool sawObjects;
    bool pointerRestored;
} Scene;

// Object 0's frame f of block b, distinct for every frame.
static float Ramp(uint32_t block, uint32_t frame)
{
    return (float)block + (float)frame / (float)PERIOD;
}

// Each block: the bed at 0.1, object 0 a ramp distinct in every frame,
// placed only on the first block (to the right, at half gain) and then
// left alone; object 1 stays inactive; object 2 is written only in the
// first block, whose end clears its pointer to see it set again, and is
// active from the second.
static void Place(const maudStreamBlock* block, void* user)
{
    Scene* scene = user;
    scene->sawObjects = block->objects != nullptr && block->objectCount == OBJECTS &&
                        block->objectsAvailable == OBJECTS && block->frameCount == PERIOD;
    maudStreamObject* objects = block->objects;
    if (scene->blocks == 0)
    {
        scene->startedClean = true;
        for (uint32_t i = 0; i < OBJECTS; ++i)
        {
            scene->startedClean = scene->startedClean && !objects[i].active &&
                                  objects[i].gain == 1.0f && objects[i].position[0] == 0.0f &&
                                  objects[i].position[1] == 0.0f &&
                                  objects[i].position[2] == 0.0f && objects[i].samples[0] == 0.0f;
        }
        objects[0].active = true;
        objects[0].gain = 0.5f;
        objects[0].position[0] = 2.0f;
        objects[0].position[2] = -1.0f;
    }
    else
    {
        scene->pointerRestored = objects[2].samples != nullptr;
        objects[2].active = true;
    }
    for (uint32_t f = 0; f < block->frameCount; ++f)
    {
        block->output[2 * f] = 0.1f;
        block->output[2 * f + 1] = 0.1f;
        objects[0].samples[f] = Ramp(scene->blocks, f);
        if (scene->blocks == 0)
        {
            objects[2].samples[f] = -1.0f;
        }
    }
    if (scene->blocks == 0)
    {
        objects[2].samples = nullptr;
    }
    ++scene->blocks;
}

static maudContext* Offline(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    def.limits.streams = 2;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "offline context");
    return context;
}

static maudStreamDef ObjectDef(Scene* scene)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.periodFrames = PERIOD;
    def.objectCount = OBJECTS;
    def.callback = Place;
    def.user = scene;
    return def;
}

static void TestRefusedDefs(void)
{
    maudContext* context = Offline();
    Scene scene = {0};
    maudStreamId stream = {0, 0};
    CHECK(maudDefaultStreamDef().objectCount == 0, "no objects by default");
    maudStreamDef def = ObjectDef(&scene);
    def.objectCount = MAUD_MAX_STREAM_OBJECTS + 1;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "too many objects");
    def = ObjectDef(&scene);
    def.direction = maud_directionInput;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "an input");
    def.direction = maud_directionDuplex;
    CHECK(maudCreateStream(context, &def, &stream) == maud_errorInvalid, "a duplex stream");
    def = ObjectDef(&scene);
    def.share = maud_shareExclusive;
    CHECK(maudGetDefaultDevice(context, maud_directionOutput, maud_roleGeneral, &def.device) ==
                  maud_success &&
              maudCreateStream(context, &def, &stream) == maud_errorInvalid,
          "exclusive use");
    def = ObjectDef(&scene);
    def.objectCount = MAUD_MAX_STREAM_OBJECTS;
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudDestroyStream(context, stream) == maud_success,
          "the most objects");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

// Renders in sizes that cross periods (100, then 300, then 112 frames:
// two periods in all) and checks every frame and the placement.
static void TestRendering(void)
{
    maudContext* context = Offline();
    Scene scene = {0};
    maudStreamDef def = ObjectDef(&scene);
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStartStream(context, stream) == maud_success,
          "an object stream");
    static float bed[2 * 2 * PERIOD];
    static float frames[OBJECTS][2 * PERIOD];
    maudStreamObject objects[OBJECTS];
    static const uint32_t sizes[] = {100, 300, 112};
    uint32_t done = 0;
    for (uint32_t s = 0; s < 3; ++s)
    {
        for (uint32_t i = 0; i < OBJECTS; ++i)
        {
            objects[i] = (maudStreamObject){.samples = frames[i] + done};
        }
        CHECK(maudRenderObjects(context, stream, bed + 2 * done, objects, OBJECTS, sizes[s]) ==
                  maud_success,
              "render");
        CHECK(objects[0].active && objects[0].gain == 0.5f && objects[0].position[0] == 2.0f &&
                  objects[0].position[1] == 0.0f && objects[0].position[2] == -1.0f,
              "object 0 placed once, kept");
        CHECK(!objects[1].active && objects[1].gain == 1.0f, "object 1 untouched");
        done += sizes[s];
    }
    CHECK(scene.blocks == 2 && scene.startedClean && scene.sawObjects,
          "two blocks, the first clean, each with the objects");
    CHECK(scene.pointerRestored, "a cleared frame pointer is set again");
    CHECK(objects[2].active, "object 2 active from the second block");
    bool bedRight = true;
    bool framesRight = true;
    for (uint32_t f = 0; f < 2 * PERIOD; ++f)
    {
        bedRight = bedRight && bed[2 * f] == 0.1f && bed[2 * f + 1] == 0.1f;
        framesRight = framesRight && frames[0][f] == Ramp(f / PERIOD, f % PERIOD) &&
                      frames[1][f] == 0.0f && frames[2][f] == (f < PERIOD ? -1.0f : 0.0f);
    }
    CHECK(bedRight, "the bed");
    CHECK(framesRight, "each object's frames, a period at a time, silent where unwritten");
    uint64_t position = 0;
    CHECK(maudGetStreamPosition(context, stream, &position) == maud_success &&
              position == 2 * PERIOD,
          "the clock");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

static void Plain(const maudStreamBlock* block, void* user)
{
    (void)user;
    CHECK(block->objects == nullptr && block->objectCount == 0 && block->objectsAvailable == 0,
          "an ordinary stream's block has no objects");
}

static void TestRenderCallsKeptApart(void)
{
    maudContext* context = Offline();
    Scene scene = {0};
    maudStreamDef def = ObjectDef(&scene);
    maudStreamId objectStream = {0, 0};
    CHECK(maudCreateStream(context, &def, &objectStream) == maud_success &&
              maudStartStream(context, objectStream) == maud_success,
          "an object stream");
    def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.callback = Plain;
    maudStreamId plain = {0, 0};
    CHECK(maudCreateStream(context, &def, &plain) == maud_success &&
              maudStartStream(context, plain) == maud_success,
          "an ordinary stream");
    float bed[2 * 8];
    float frames[OBJECTS][8];
    maudStreamObject objects[OBJECTS];
    for (uint32_t i = 0; i < OBJECTS; ++i)
    {
        objects[i] = (maudStreamObject){.samples = frames[i]};
    }
    CHECK(maudRenderStream(context, objectStream, bed, 8) == maud_errorInvalid,
          "an object stream renders its objects");
    CHECK(maudRenderObjects(context, plain, bed, objects, 0, 8) == maud_errorInvalid,
          "an ordinary stream has none");
    CHECK(maudRenderObjects(context, objectStream, bed, objects, OBJECTS - 1, 8) ==
              maud_errorInvalid,
          "another count");
    CHECK(maudRenderObjects(context, objectStream, bed, nullptr, OBJECTS, 0) == maud_errorInvalid,
          "no records");
    CHECK(maudRenderObjects(context, objectStream, nullptr, objects, OBJECTS, 8) ==
              maud_errorInvalid,
          "no bed");
    objects[1].samples = nullptr;
    CHECK(maudRenderObjects(context, objectStream, bed, objects, OBJECTS, 8) == maud_errorInvalid,
          "an object without room");
    CHECK(maudRenderObjects(context, objectStream, nullptr, objects, OBJECTS, 0) == maud_success,
          "nothing due");
    CHECK(maudRenderStream(context, plain, bed, 8) == maud_success, "the ordinary one renders");
    CHECK(maudRenderObjects(nullptr, objectStream, bed, objects, OBJECTS, 8) == maud_errorInvalid,
          "no context");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
}

int main(void)
{
    TestRefusedDefs();
    TestRendering();
    TestRenderCallsKeptApart();
    return s_failures == 0 ? 0 : 1;
}
