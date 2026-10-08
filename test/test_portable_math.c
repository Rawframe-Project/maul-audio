// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The portable functions (whitebox): within 4.5e-16 of the C library's
// (relative for exp and log, absolute for sine and cosine) over their
// ranges; exact where they must be (e^0, log 1, sin 0, cos 0); the
// edges (0, infinity, NaN, subnormals, overflow); and a hash of their
// bits at fixed arguments that every platform must reproduce.

#include "portable_math.h"
#include "test_harness.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static uint64_t s_hash = 1469598103934665603u;

static void Hash(double v)
{
    unsigned char bytes[8];
    memcpy(bytes, &v, sizeof(bytes));
    for (int i = 0; i < 8; ++i)
    {
        s_hash = (s_hash ^ bytes[i]) * 1099511628211u;
    }
}

int main(void)
{
    double we = 0.0;
    double wl = 0.0;
    double ws = 0.0;
    for (int i = 0; i <= 200000; ++i)
    {
        double x = -700.0 + 1400.0 * (double)i / 200000.0;
        we = fmax(we, fabs(maudExp(x) / exp(x) - 1.0));
        // The inputs from the portable exp too: the C library's would
        // differ by platform and change the hash.
        double y = maudExp(-60.0 + 120.0 * (double)i / 200000.0);
        wl = fmax(wl, fabs(maudLog(y) - log(y)) / fmax(1.0, fabs(log(y))));
        double a = -50000.0 + 100000.0 * (double)i / 200000.0;
        double s = 0.0;
        double c = 0.0;
        maudSinCos(a, &s, &c);
        ws = fmax(ws, fmax(fabs(s - sin(a)), fabs(c - cos(a))));
        Hash(maudExp(x));
        Hash(maudLog(y));
        Hash(s);
        Hash(c);
    }
    printf("worst: exp %.2e, log %.2e, sine and cosine %.2e; hash %016llx\n", we, wl, ws,
           (unsigned long long)s_hash);
    CHECK(we < 4.5e-16 && wl < 4.5e-16 && ws < 4.5e-16, "within 2 ulp of the C library");
    double s = 1.0;
    double c = 0.0;
    maudSinCos(0.0, &s, &c);
    CHECK(maudExp(0.0) == 1.0 && maudLog(1.0) == 0.0 && s == 0.0 && c == 1.0, "exact points");
    CHECK(maudExp(800.0) == HUGE_VAL && maudExp(-800.0) == 0.0, "overflow and underflow");
    CHECK(isfinite(maudExp(709.0)) && fabs(maudExp(709.0) / exp(709.0) - 1.0) < 1e-12 &&
              maudExp(-708.0) > 0.0 && fabs(maudExp(-708.0) / exp(-708.0) - 1.0) < 1e-12,
          "the range's ends computed, not cut");
    maudSinCos(1e7, &s, &c);
    CHECK(isnan(maudExp((double)NAN)) && maudExp(-HUGE_VAL) == 0.0 && isnan(s) && isnan(c),
          "NaN through, infinity to 0, out of range");
    CHECK(maudLog(0.0) == -HUGE_VAL && isnan(maudLog(-1.0)) && maudLog(HUGE_VAL) == HUGE_VAL,
          "log's edges");
    CHECK(fabs(maudLog(4.9e-324) - log(4.9e-324)) < 1e-12, "a subnormal");
    CHECK(fabs(maudLog10(1000.0) - 3.0) < 1e-15 && fabs(maudPow10(-2.0) - 0.01) < 1e-17,
          "base ten");
    CHECK(s_hash == 0x55a8181078e01116u, "the same bits on every platform");
    return s_failures == 0 ? 0 : 1;
}
