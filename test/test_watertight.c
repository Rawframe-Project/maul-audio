// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The watertight triangle test alone: a plain hit and miss with their
// distances; and a triangle whose edge function underflows to 0 in
// float while its true sign says the ray passes outside, which the
// double recheck must catch rather than turn into a hit; and triangles
// with vertices near 3e38, whose float edge functions overflow: one
// 3e38 m from the ray must not answer a hit at a NaN distance, and one
// the ray meets must answer its true distance.

#include "test_harness.h"
#include "watertight.h"

#include <math.h>
#include <stdio.h>

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
    // A triangle 1 m across at x = 3e38, and a ray 1.41 m long near the
    // origin: its edge functions are inf - inf in float.
    float from[3] = {0.0f, 1.0f, 0.0f};
    float diagonal[3] = {0.70710678f, 0.0f, 0.70710678f};
    maudShearedRay slanted;
    maudShearRay(from, diagonal, &slanted);
    float far1[3] = {3.0e38f, 3.0f, 0.0f};
    float far2[3] = {3.0e38f, 4.0f, 0.0f};
    float far3[3] = {3.0e38f, 3.0f, 1.0f};
    t = 0.0f;
    bool hit = maudHitTriangle(&slanted, far1, far2, far3, 0.0f, 1.4142135f, &t);
    printf("far: hit %d at %g\n", hit, (double)t);
    CHECK(!hit, "a triangle 3e38 m away: none");
    // A triangle 5 m ahead whose corners lie near 3e38 on every side.
    float huge1[3] = {-3.0e38f, -3.0e38f, 5.0f};
    float huge2[3] = {3.0e38f, -3.0e38f, 5.0f};
    float huge3[3] = {0.0f, 3.0e38f, 5.0f};
    float zero[3] = {0.0f, 0.0f, 0.0f};
    maudShearedRay ahead;
    maudShearRay(zero, up, &ahead);
    t = 0.0f;
    hit = maudHitTriangle(&ahead, huge1, huge2, huge3, 0.0f, INFINITY, &t);
    printf("wide: hit %d at %g\n", hit, (double)t);
    CHECK(hit && t == 5.0f, "an overflowing triangle the ray meets: its distance");
    return s_failures == 0 ? 0 : 1;
}
