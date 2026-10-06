// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The watertight triangle test alone: a plain hit and miss with their
// distances; and a triangle whose edge function underflows to 0 in
// float while its true sign says the ray passes outside, which the
// double recheck must catch rather than turn into a hit.

#include "test_harness.h"
#include "watertight.h"

#include <math.h>

int main(void)
{
    float origin[3] = {0.0f, 0.0f, -1.0f};
    float up[3] = {0.0f, 0.0f, 1.0f};
    maudShearedRay ray;
    maudShearRay(origin, up, &ray);
    float a[3] = {-1.0f, -1.0f, 2.0f};
    float b[3] = {1.0f, -1.0f, 2.0f};
    float c[3] = {0.0f, 1.0f, 2.0f};
    float t = 0.0f;
    CHECK(maudHitTriangle(&ray, a, b, c, 0.0f, 10.0f, &t) && fabsf(t - 3.0f) < 1e-6f,
          "a hit at its distance");
    CHECK(!maudHitTriangle(&ray, a, b, c, 0.0f, 2.5f, &t), "beyond the range: none");
    CHECK(maudHitTriangle(&ray, a, c, b, 0.0f, 10.0f, &t), "either winding");
    // Two vertices near 1e-24 from the ray: the edge between them has a
    // float edge function of 0 (its products underflow) and a true value
    // of +9.9e-47, against the other two edges' negative values.
    float far[3] = {1.0f, 1.0f, 0.0f};
    float near1[3] = {1e-24f, 1e-23f, 0.0f};
    float near2[3] = {1e-23f, 1e-24f, 0.0f};
    CHECK(!maudHitTriangle(&ray, far, near1, near2, 0.0f, 10.0f, &t),
          "an underflowed edge keeps its sign");
    return s_failures == 0 ? 0 : 1;
}
