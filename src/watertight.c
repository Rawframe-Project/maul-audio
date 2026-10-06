// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The watertight test (watertight.h), as the paper gives it, without
// back-face culling.

#include "watertight.h"

#include <math.h>

void maudShearRay(const float* origin, const float* direction, maudShearedRay* ray)
{
    for (int i = 0; i < 3; ++i)
    {
        ray->origin[i] = origin[i];
    }
    int kz = 0;
    for (int i = 1; i < 3; ++i)
    {
        kz = fabsf(direction[i]) > fabsf(direction[kz]) ? i : kz;
    }
    int kx = (kz + 1) % 3;
    int ky = (kx + 1) % 3;
    // Keep the winding: swap when the ray runs along -kz.
    if (direction[kz] < 0.0f)
    {
        int swap = kx;
        kx = ky;
        ky = swap;
    }
    ray->kx = kx;
    ray->ky = ky;
    ray->kz = kz;
    ray->sx = direction[kx] / direction[kz];
    ray->sy = direction[ky] / direction[kz];
    ray->sz = 1.0f / direction[kz];
}

// The edge functions, recomputed in double where one is exactly zero.
static void Edges(float ax, float ay, float bx, float by, float cx, float cy, float* u, float* v,
                  float* w)
{
    *u = cx * by - cy * bx;
    *v = ax * cy - ay * cx;
    *w = bx * ay - by * ax;
    if (*u == 0.0f || *v == 0.0f || *w == 0.0f)
    {
        *u = (float)((double)cx * (double)by - (double)cy * (double)bx);
        *v = (float)((double)ax * (double)cy - (double)ay * (double)cx);
        *w = (float)((double)bx * (double)ay - (double)by * (double)ax);
    }
}

bool maudHitTriangle(const maudShearedRay* ray, const float* a, const float* b, const float* c,
                     float tMin, float tMax, float* t)
{
    float pa[3];
    float pb[3];
    float pc[3];
    for (int i = 0; i < 3; ++i)
    {
        pa[i] = a[i] - ray->origin[i];
        pb[i] = b[i] - ray->origin[i];
        pc[i] = c[i] - ray->origin[i];
    }
    int kx = ray->kx;
    int ky = ray->ky;
    int kz = ray->kz;
    float ax = pa[kx] - ray->sx * pa[kz];
    float ay = pa[ky] - ray->sy * pa[kz];
    float bx = pb[kx] - ray->sx * pb[kz];
    float by = pb[ky] - ray->sy * pb[kz];
    float cx = pc[kx] - ray->sx * pc[kz];
    float cy = pc[ky] - ray->sy * pc[kz];
    float u;
    float v;
    float w;
    Edges(ax, ay, bx, by, cx, cy, &u, &v, &w);
    if ((u < 0.0f || v < 0.0f || w < 0.0f) && (u > 0.0f || v > 0.0f || w > 0.0f))
    {
        return false;
    }
    float det = u + v + w;
    if (det == 0.0f)
    {
        return false;
    }
    float tScaled = u * (ray->sz * pa[kz]) + v * (ray->sz * pb[kz]) + w * (ray->sz * pc[kz]);
    // The distance is tScaled / det; compare without dividing first,
    // with the sign of det taken out.
    if (det < 0.0f)
    {
        tScaled = -tScaled;
        det = -det;
    }
    if (tScaled < tMin * det || tScaled > tMax * det)
    {
        return false;
    }
    // Rounding in the division may step just outside the range.
    float distance = tScaled / det;
    distance = distance < tMin ? tMin : distance;
    *t = distance > tMax ? tMax : distance;
    return true;
}
