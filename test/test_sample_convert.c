// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Sample conversion for exclusive streams: full scale, saturation,
// rounding, NaN, and round trips through 16 and 32-bit integers.

#include "sample_convert.h"
#include "test_harness.h"

#include <math.h>

static void TestInt16(void)
{
    const float in[] = {0.0f, 1.0f, -1.0f, 2.0f, -2.0f, 0.5f, -0.5f, 1.0f / 65536.0f, NAN};
    int16_t out[9];
    maudFloatToSamples(out, in, 9, maud_sampleInt16);
    CHECK(out[0] == 0 && out[1] == 32767 && out[2] == -32768, "silence and full scale");
    CHECK(out[3] == 32767 && out[4] == -32768, "past full scale saturates");
    CHECK(out[5] == 16384 && out[6] == -16384, "halves");
    CHECK(out[7] == 1, "half a step rounds away from zero");
    CHECK(out[8] == 0, "NaN is silence");
    CHECK(maudSampleBytes(maud_sampleInt16) == 2, "two bytes");
    int16_t every[4] = {-32768, -1, 1, 32767};
    float back[4];
    int16_t again[4];
    maudSamplesToFloat(back, every, 4, maud_sampleInt16);
    CHECK(back[0] == -1.0f && back[1] == -1.0f / 32768.0f, "full scale to -1");
    maudFloatToSamples(again, back, 4, maud_sampleInt16);
    CHECK(again[0] == -32768 && again[1] == -1 && again[2] == 1 && again[3] == 32767,
          "a round trip keeps each sample");
}

static void TestInt32(void)
{
    const float in[] = {0.0f, 1.0f, -1.0f, 0.25f, -3.0f, NAN};
    int32_t out[6];
    maudFloatToSamples(out, in, 6, maud_sampleInt32);
    CHECK(out[0] == 0 && out[1] == 2147483647 && out[2] == INT32_MIN, "full scale");
    CHECK(out[3] == 536870912, "a quarter");
    CHECK(out[4] == INT32_MIN && out[5] == 0, "saturation and NaN");
    CHECK(maudSampleBytes(maud_sampleInt32) == 4, "four bytes");
    // 24 valid bits, left-justified: steps of 256.
    int32_t steps[3] = {256, -256, 0x7FFFFF00};
    float back[3];
    int32_t again[3];
    maudSamplesToFloat(back, steps, 3, maud_sampleInt32);
    maudFloatToSamples(again, back, 3, maud_sampleInt32);
    CHECK(again[0] == 256 && again[1] == -256 && again[2] == 0x7FFFFF00,
          "24-bit samples survive a round trip");
    CHECK(back[0] > 0.0f && back[0] < 1e-6f, "a 24-bit step is small and positive");
}

static void TestFloat(void)
{
    const float in[3] = {0.5f, -2.0f, 1.0f};
    float out[3] = {0};
    maudFloatToSamples(out, in, 3, maud_sampleFloat32);
    CHECK(out[0] == 0.5f && out[1] == -2.0f && out[2] == 1.0f, "float passes untouched");
    float back[3] = {0};
    maudSamplesToFloat(back, out, 3, maud_sampleFloat32);
    CHECK(back[1] == -2.0f, "both ways");
    CHECK(maudSampleBytes(maud_sampleFloat32) == 4, "four bytes");
}

int main(void)
{
    TestInt16();
    TestInt32();
    TestFloat();
    return s_failures == 0 ? 0 : 1;
}
