// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The fixed-period adapter (whitebox): an input gathers frames into
// blocks of its period, and a restart, as a backend does before it
// connects a new platform stream, drops the frames gathered toward the
// next block, so that no block joins two platform streams' frames.

#include "period.h"
#include "test_harness.h"

#include "maul-audio/stream.h"

#define FRAMES 4

typedef struct Seen
{
    uint32_t count;
    uint64_t position[4];
    float samples[4][FRAMES];
} Seen;

static void Keep(const maudStreamBlock* block, void* user)
{
    Seen* seen = user;
    if (seen->count < 4)
    {
        seen->position[seen->count] = block->position;
        for (uint32_t i = 0; i < FRAMES; ++i)
        {
            seen->samples[seen->count][i] = block->input[i];
        }
    }
    seen->count += 1;
}

static void InitInput(maudPeriod* period, float* samples, Seen* seen)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.direction = maud_directionInput;
    def.callback = Keep;
    def.user = seen;
    maudStreamFormat format = {
        .sampleRate = 48000, .layout = maud_layoutMono, .periodFrames = FRAMES};
    maudInitPeriod(period, &def, &format, samples);
}

static bool AllAre(const float* samples, float value)
{
    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        if (samples[i] != value)
        {
            return false;
        }
    }
    return true;
}

// Six frames of the old stream make one block and leave two toward the
// next; after the restart the new stream's four frames are a block of
// their own, placed after all six.
static void TestRestartDropsTheGatheredFrames(void)
{
    float samples[FRAMES];
    Seen seen = {0};
    maudPeriod period;
    InitInput(&period, samples, &seen);
    const float old[6] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    maudPushPeriod(&period, old, 6);
    CHECK(seen.count == 1 && seen.position[0] == 0 && AllAre(seen.samples[0], 1.0f),
          "one block of the old stream");
    maudRestartInputPeriod(&period);
    const float fresh[FRAMES] = {0};
    maudPushPeriod(&period, fresh, FRAMES);
    CHECK(seen.count == 2 && AllAre(seen.samples[1], 0.0f),
          "the next block holds the new stream's frames alone");
    CHECK(seen.position[1] == 6, "placed after every frame the old stream delivered");
}

// A restart on a block's boundary changes nothing.
static void TestRestartOnABoundary(void)
{
    float samples[FRAMES];
    Seen seen = {0};
    maudPeriod period;
    InitInput(&period, samples, &seen);
    const float frames[FRAMES] = {0.5f, 0.5f, 0.5f, 0.5f};
    maudPushPeriod(&period, frames, FRAMES);
    maudRestartInputPeriod(&period);
    maudPushPeriod(&period, frames, FRAMES);
    CHECK(seen.count == 2 && seen.position[0] == 0 && seen.position[1] == FRAMES &&
              AllAre(seen.samples[1], 0.5f),
          "blocks follow on as before");
}

int main(void)
{
    TestRestartDropsTheGatheredFrames();
    TestRestartOnABoundary();
    return s_failures == 0 ? 0 : 1;
}
