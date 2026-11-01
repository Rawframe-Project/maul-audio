// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The watertight test (watertight.h), as the paper gives it, without
// back-face culling. Where a float value of the test overflows (vertices
// near FLT_MAX, or a direction too short for sz), the test runs again in
// double, in which every product of finite floats is finite: an overflow
// would otherwise make an edge function inf - inf, a NaN that passes
// every test. The rerun takes each vertex's sheared x and y from the
// float path where they are finite, so that an edge two triangles share
// has one sign whichever path tests each, and a ray through it hits one
// of them. A distance past FLT_MAX is a miss.

#include "watertight.h"

#include <float.h>
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
    ray->szDouble = 1.0 / (double)direction[kz];
}

// A double as a float that keeps its sign: a value too small for a float
// becomes the smallest float of its sign, not 0.
static float Narrow(double value)
{
    float f = (float)value;
    if (f == 0.0f && value != 0.0)
    {
        return value > 0.0 ? FLT_TRUE_MIN : -FLT_TRUE_MIN;
    }
    return f;
}

// The edge functions, recomputed in double where one is exactly zero
// (a float product rounded or underflowed away a sign).
static void Edges(float ax, float ay, float bx, float by, float cx, float cy, float* u, float* v,
                  float* w)
{
    *u = cx * by - cy * bx;
    *v = ax * cy - ay * cx;
    *w = bx * ay - by * ax;
    if (*u == 0.0f || *v == 0.0f || *w == 0.0f)
    {
        *u = Narrow((double)cx * (double)by - (double)cy * (double)bx);
        *v = Narrow((double)ax * (double)cy - (double)ay * (double)cx);
        *w = Narrow((double)bx * (double)ay - (double)by * (double)ax);
    }
}

// A vertex's sheared x and y for the test in double: the float path's
// where they are finite, and in double where they overflowed, as they
// then do in every triangle that holds the vertex.
static void Sheared(const maudShearedRay* ray, const float* p, double* x, double* y)
{
    int kx = ray->kx;
    int ky = ray->ky;
    int kz = ray->kz;
    float fx = (p[kx] - ray->origin[kx]) - ray->sx * (p[kz] - ray->origin[kz]);
    float fy = (p[ky] - ray->origin[ky]) - ray->sy * (p[kz] - ray->origin[kz]);
    double z = (double)p[kz] - (double)ray->origin[kz];
    *x = isfinite(fx) ? (double)fx : (double)p[kx] - (double)ray->origin[kx] - (double)ray->sx * z;
    *y = isfinite(fy) ? (double)fy : (double)p[ky] - (double)ray->origin[ky] - (double)ray->sy * z;
}

// The test in double, for a triangle whose float values overflowed. A
// value it cannot decide (a ray with no direction) fails the final range
// test, so it is a miss, as is a distance past FLT_MAX.
static bool HitInDouble(const maudShearedRay* ray, const float* a, const float* b, const float* c,
                        float tMin, float tMax, float* t)
{
    double ax;
    double ay;
    double bx;
    double by;
    double cx;
    double cy;
    Sheared(ray, a, &ax, &ay);
    Sheared(ray, b, &bx, &by);
    Sheared(ray, c, &cx, &cy);
    double u = cx * by - cy * bx;
    double v = ax * cy - ay * cx;
    double w = bx * ay - by * ax;
    if ((u < 0.0 || v < 0.0 || w < 0.0) && (u > 0.0 || v > 0.0 || w > 0.0))
    {
        return false;
    }
    double det = u + v + w;
    if (det == 0.0)
    {
        return false;
    }
    int kz = ray->kz;
    double sz = ray->szDouble;
    double za = sz * ((double)a[kz] - (double)ray->origin[kz]);
    double zb = sz * ((double)b[kz] - (double)ray->origin[kz]);
    double zc = sz * ((double)c[kz] - (double)ray->origin[kz]);
    double distance = (u * za + v * zb + w * zc) / det;
    if (!(distance >= (double)tMin && distance <= (double)tMax && distance <= (double)FLT_MAX))
    {
        return false;
    }
    float narrow = (float)distance;
    narrow = narrow < tMin ? tMin : narrow;
    *t = narrow > tMax ? tMax : narrow;
    return true;
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
    if (!isfinite(u) || !isfinite(v) || !isfinite(w))
    {
        return HitInDouble(ray, a, b, c, tMin, tMax, t);
    }
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
    if (!isfinite(det) || !isfinite(tScaled))
    {
        return HitInDouble(ray, a, b, c, tMin, tMax, t);
    }
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
    if (!isfinite(distance))
    {
        return HitInDouble(ray, a, b, c, tMin, tMax, t);
    }
    distance = distance < tMin ? tMin : distance;
    *t = distance > tMax ? tMax : distance;
    return true;
}
