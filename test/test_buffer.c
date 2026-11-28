// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Interleaving and deinterleaving: exact copies in the documented order,
// with nothing written past an array, and refusals of invalid
// arguments, interleaved frames over a channel's samples among them.

#include "test_harness.h"

#include "maul-audio/buffer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES       37
#define MAX_CHANNELS 12

// A sample value that names its channel and frame, and is exact in binary32.
static float Sample(uint32_t channel, uint32_t frame)
{
    return (float)(channel * 1000u + frame) + 0.25f;
}

// Each array on the heap at its exact size, so that a write past one
// is caught under the address sanitizer.
static void CheckRoundTrip(uint32_t channelCount)
{
    float* planar[MAX_CHANNELS];
    float* back[MAX_CHANNELS];
    float* interleaved = malloc(sizeof(float) * channelCount * FRAMES);
    for (uint32_t c = 0; c < channelCount; ++c)
    {
        planar[c] = malloc(sizeof(float) * FRAMES);
        back[c] = calloc(FRAMES, sizeof(float));
        for (uint32_t i = 0; i < FRAMES; ++i)
        {
            planar[c][i] = Sample(c, i);
        }
    }
    CHECK(maudInterleave((const float* const*)planar, channelCount, FRAMES, interleaved) ==
              maud_success,
          "interleave");
    bool ordered = true;
    for (uint32_t i = 0; i < FRAMES; ++i)
    {
        for (uint32_t c = 0; c < channelCount; ++c)
        {
            ordered = ordered && interleaved[i * channelCount + c] == Sample(c, i);
        }
    }
    CHECK(ordered, "frame i, channel c at i * channelCount + c");
    CHECK(maudDeinterleave(interleaved, channelCount, FRAMES, back) == maud_success,
          "deinterleave");
    bool exact = true;
    for (uint32_t c = 0; c < channelCount; ++c)
    {
        exact = exact && memcmp(back[c], planar[c], sizeof(float) * FRAMES) == 0;
        free(planar[c]);
        free(back[c]);
    }
    CHECK(exact, "round trip is exact");
    free(interleaved);
}

static void TestRoundTripsEveryChannelCount(void)
{
    for (uint32_t channels = 1; channels <= MAX_CHANNELS; ++channels)
    {
        CheckRoundTrip(channels);
    }
}

static void TestZeroFramesNeedNoArrays(void)
{
    CHECK(maudInterleave(nullptr, 2, 0, nullptr) == maud_success, "interleave nothing");
    CHECK(maudDeinterleave(nullptr, 6, 0, nullptr) == maud_success, "deinterleave nothing");
}

static void TestInvalidArgumentsAreRefused(void)
{
    float left[4] = {0};
    float right[4] = {0};
    float frames[8] = {0};
    const float* in[2] = {left, right};
    float* out[2] = {left, right};
    const float* missing[2] = {left, nullptr};
    float* missingOut[2] = {nullptr, right};
    CHECK(maudInterleave(in, 0, 4, frames) == maud_errorInvalid, "no channels");
    CHECK(maudDeinterleave(frames, 0, 4, out) == maud_errorInvalid, "no channels out");
    CHECK(maudInterleave(nullptr, 2, 4, frames) == maud_errorInvalid, "null planar");
    CHECK(maudInterleave(in, 2, 4, nullptr) == maud_errorInvalid, "null frames out");
    CHECK(maudDeinterleave(nullptr, 2, 4, out) == maud_errorInvalid, "null frames");
    CHECK(maudDeinterleave(frames, 2, 4, nullptr) == maud_errorInvalid, "null planar out");
    CHECK(maudInterleave(missing, 2, 4, frames) == maud_errorInvalid, "null channel");
    CHECK(maudDeinterleave(frames, 2, 4, missingOut) == maud_errorInvalid, "null channel out");
}

// Interleaved frames that share a byte with a channel's samples are
// refused both ways, the output left as it was: a planar buffer laid out
// channel after channel and interleaved into itself, frames whose last
// sample is a channel's first, and a channel whose last sample is the
// frames' first. Frames that end where a channel starts are apart.
static void TestOverlapIsRefused(void)
{
    float buffer[16];
    for (int i = 0; i < 16; ++i)
    {
        buffer[i] = (float)i;
    }
    const float* inPlace[2] = {buffer, buffer + 4};
    CHECK(maudInterleave(inPlace, 2, 4, buffer) == maud_errorInvalid, "interleaved in place");
    float* outPlace[2] = {buffer, buffer + 4};
    CHECK(maudDeinterleave(buffer, 2, 4, outPlace) == maud_errorInvalid, "deinterleaved in place");
    // Frames over [1, 9), a channel from 8.
    const float* firstSample[2] = {buffer + 8, buffer + 12};
    CHECK(maudInterleave(firstSample, 2, 4, buffer + 1) == maud_errorInvalid,
          "the frames' last sample on a channel's first");
    float* firstOut[2] = {buffer + 8, buffer + 12};
    CHECK(maudDeinterleave(buffer + 1, 2, 4, firstOut) == maud_errorInvalid,
          "a channel's first sample on the frames' last");
    // A channel over [0, 4), frames from 3.
    const float* lastSample[2] = {buffer, buffer + 12};
    CHECK(maudInterleave(lastSample, 2, 4, buffer + 3) == maud_errorInvalid,
          "a channel's last sample on the frames' first");
    float* lastOut[2] = {buffer, buffer + 12};
    CHECK(maudDeinterleave(buffer + 3, 2, 4, lastOut) == maud_errorInvalid,
          "the frames' first sample on a channel's last");
    bool kept = true;
    for (int i = 0; i < 16; ++i)
    {
        kept = kept && buffer[i] == (float)i;
    }
    CHECK(kept, "nothing written by a refused call");
    const float* after[2] = {buffer + 8, buffer + 12};
    CHECK(maudInterleave(after, 2, 4, buffer) == maud_success, "frames right before a channel");
    float* afterOut[2] = {buffer + 8, buffer + 12};
    CHECK(maudDeinterleave(buffer, 2, 4, afterOut) == maud_success,
          "a channel right after the frames");
}

static void TestTotalsPastMemoryAreRefused(void)
{
    // Only a 32-bit size_t can overflow with 32-bit counts; elsewhere the
    // product fits and the refusal cannot be provoked without real memory.
    if (SIZE_MAX > UINT32_MAX)
    {
        return;
    }
    float sample = 0.0f;
    const float* in[1] = {&sample};
    float* out[1] = {&sample};
    CHECK(maudInterleave(in, 1, UINT32_MAX, &sample) == maud_errorInvalid, "bytes overflow");
    CHECK(maudDeinterleave(&sample, 1, UINT32_MAX, out) == maud_errorInvalid, "bytes overflow");
}

int main(void)
{
    TestRoundTripsEveryChannelCount();
    TestZeroFramesNeedNoArrays();
    TestInvalidArgumentsAreRefused();
    TestOverlapIsRefused();
    TestTotalsPastMemoryAreRefused();
    return s_failures == 0 ? 0 : 1;
}
