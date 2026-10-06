#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Generates the near-field table of Maul Audio's binaural effect: for
# each incidence angle (the angle at the head's centre between an ear
# and the source) and each inverse normalized distance u = a / r (a the
# head radius, u = 0 a source at infinity), the parameters of a gain and
# a first-order high shelf that approximate a rigid sphere's response to
# a point source at that distance, divided by its response to one at
# infinity (the distance variation function without the 1/r pressure
# change).
#
# The sphere's response is the series of Duda and Martens (J. Acoust.
# Soc. Am. 104, 1998), as Eq. 1 of Spagnol, Tavazzi and Avanzini
# (Applied Acoustics 115, 2017); a source at infinity takes its
# plane-wave limit. The gain at DC is exact, from Weiss's sphere theorem
# (the potential-flow limit), and the shelf fits the rest. Frequencies are normalized, mu = 2 pi f a / c, so
# one table serves every head radius. The shelf is analog,
# H(s) = (V s + w) / (s + w), with V its high-frequency gain and w its
# corner, fitted by least squares in dB over mu from 0.16 to 24 (100 Hz
# to 15 kHz for a = 8.75 cm); the gain is the fit's mean offset.
#
# The output depends only on this script and numpy's arithmetic.
#
# usage: near_field_table.py output.c [--check] [--reference PATH]
#   --check      prints how far interpolation between grid points strays
#                from fits made at the midpoints
#   --reference  also writes exact values for the library's test

import argparse
import sys

import numpy as np

ANGLES = np.arange(0.0, 180.0 + 0.5, 5.0)
FIT_MU = np.geomspace(0.032, 24.0, 112)


def u_grid(count):
    """Inverse distances from 0 (infinity) to 1 / 1.15, denser near the
    head, where the response changes fastest."""
    t = np.linspace(0.0, 1.0, count)
    return (1.0 / 1.15) * t**0.5


def hankels(z, count):
    """Spherical Hankel functions h_0 .. h_count of the first kind at z,
    by upward recurrence (stable for them)."""
    h = np.zeros(count + 1, complex)
    h[0] = -1j * np.exp(1j * z) / z
    if count >= 1:
        h[1] = -np.exp(1j * z) * (z + 1j) / z**2
    for m in range(1, count):
        h[m + 1] = (2 * m + 1) / z * h[m] - h[m - 1]
    return h


def legendre(count, x):
    p = np.zeros(count + 1)
    p[0] = 1.0
    if count >= 1:
        p[1] = x
    for m in range(1, count):
        p[m + 1] = ((2 * m + 1) * x * p[m] - m * p[m - 1]) / (m + 1)
    return p


def sphere(mu, alpha, u):
    """The sphere's response at normalized frequency mu, incidence angle
    alpha in degrees, inverse normalized distance u (0: a plane wave),
    relative to the free field at the centre."""
    x = np.cos(np.radians(alpha))
    if u == 0.0:
        terms = int(mu + 30)
        hm = hankels(mu, terms + 1)
        p = legendre(terms, x)
        total = 0j
        for m in range(terms):
            derivative = hm[m - 1] - (m + 1) / mu * hm[m] if m > 0 else -hm[1]
            total += (-1j) ** (m - 1) * (2 * m + 1) * p[m] / derivative
        return total / mu**2
    rho = 1.0 / u
    terms = int(mu * rho + 30)
    with np.errstate(over="ignore", invalid="ignore"):
        hx = hankels(mu * rho, terms)
        hm = hankels(mu, terms + 1)
    p = legendre(terms, x)
    total = 0j
    for m in range(terms):
        derivative = hm[m - 1] - (m + 1) / mu * hm[m] if m > 0 else -hm[1]
        size = (2 * m + 1) * hx[m] / derivative
        # Past mu the terms shrink about as rho^-m; once they no longer
        # count, or the functions leave the floating-point range, stop.
        # (Judged without the Legendre factor, which vanishes for every
        # odd m at 90 degrees.)
        if not np.isfinite(size):
            break
        total += size * p[m]
        if m > mu + 10 and abs(size) < 1e-13 * abs(total):
            break
    return -rho / mu * np.exp(-1j * mu * rho) * total


def target(alpha, u, mus=FIT_MU):
    """The distance variation in dB at normalized frequencies mus,
    without 1/r."""
    near = np.array([abs(sphere(mu, alpha, u)) for mu in mus])
    far = np.array([abs(sphere(mu, alpha, 0.0)) for mu in mus])
    # The response is relative to the free field at the centre, which
    # already removes 1/r at the centre: what is left is the head's effect.
    return 20.0 * np.log10(near / far)


def shelf_db(v_db, log_w):
    v = 10.0 ** (v_db / 20.0)
    w = np.exp(log_w)
    s = 1j * FIT_MU
    return 20.0 * np.log10(np.abs((v * s + w) / (s + w)))


V_BOUNDS = (-30.0, 30.0)
W_BOUNDS = (np.log(0.05), np.log(60.0))
# Pull towards the neighbour's solution, in dB squared per unit: it
# settles directions the curve barely constrains, so neighbouring
# points stay on one branch and interpolate.
PULL = 0.002


def dc_db(alpha, u):
    """The variation at DC, in closed form: at zero frequency the field is
    potential flow, and a rigid sphere in a point source's potential is
    solved by Weiss's sphere theorem (an image source of strength 1 / rho
    at 1 / rho from the centre, and a line sink from the centre to it).
    Relative to the incident potential at the centre, 1 / rho."""
    if u == 0.0:
        return 0.0
    rho = 1.0 / u
    # The ear at unit distance, alpha from the source's axis (z).
    ex, ez = np.sin(np.radians(alpha)), np.cos(np.radians(alpha))
    incident = 1.0 / np.hypot(ex, ez - rho)
    image = u / np.hypot(ex, ez - u)
    # Integral over xi from 0 to u of 1 / |ear - xi z|.
    if ex > 1e-12:
        sink = np.arcsinh((u - ez) / ex) - np.arcsinh(-ez / ex)
    else:
        # On the axis: in front of the line (ez = 1 > u) or behind (ez = -1).
        sink = np.log(ez / (ez - u)) if ez > 0.0 else np.log((u - ez) / -ez)
    return float(20.0 * np.log10((incident + image - sink) * rho))


def fit(curve, gain, start=None):
    """Least-squares shelf gain (dB) and log corner for a curve whose DC
    gain is given, within bounds. From a neighbour's solution (start) it
    refines locally with a pull towards it; without one it searches a
    grid first."""

    def cost(v_db, log_w):
        r = curve - gain - shelf_db(v_db, log_w)
        c = np.mean(r**2)
        if start is not None:
            c += PULL * ((v_db - start[0]) ** 2 + (10.0 * (log_w - start[1])) ** 2)
        return c

    if start is None:
        best = (np.inf, 0.0, 0.0)
        for v_db in np.linspace(V_BOUNDS[0], V_BOUNDS[1], 61):
            for log_w in np.linspace(W_BOUNDS[0], W_BOUNDS[1], 48):
                c = cost(v_db, log_w)
                if c < best[0]:
                    best = (c, v_db, log_w)
        c, v_db, log_w = best
    else:
        v_db, log_w = start
        c = cost(v_db, log_w)
    step_v, step_w = 0.5, 0.1
    while step_v > 1e-4:
        improved = False
        for dv, dw in ((step_v, 0), (-step_v, 0), (0, step_w), (0, -step_w)):
            nv = min(max(v_db + dv, V_BOUNDS[0]), V_BOUNDS[1])
            nw = min(max(log_w + dw, W_BOUNDS[0]), W_BOUNDS[1])
            trial = cost(nv, nw)
            if trial < c:
                c, v_db, log_w, improved = trial, nv, nw, True
        if not improved:
            step_v, step_w = step_v / 2, step_w / 2
    sd = float(np.sqrt(np.mean((curve - gain - shelf_db(v_db, log_w)) ** 2)))
    return gain, v_db, log_w, sd


def table(us):
    """Fits by continuation: along each angle's distances from far to
    near, each from the previous one; an angle's farthest point from the
    previous angle's."""
    rows = np.zeros((len(ANGLES), len(us), 3))
    worst = (0.0, None)
    for i, alpha in enumerate(ANGLES):
        for j, u in enumerate(us):
            if u == 0.0:
                # At infinity the variation is nothing: no gain, a flat
                # shelf. The corner continues the nearer point's, set below.
                continue
            if j > 1:
                start = rows[i, j - 1, 1:]
            elif i > 0:
                start = rows[i - 1, j, 1:]
            else:
                start = None
            gain, v_db, log_w, sd = fit(target(alpha, u), dc_db(alpha, u), start)
            rows[i, j] = (gain, v_db, log_w)
            if sd > worst[0]:
                worst = (sd, (float(alpha), float(1.0 / u)))
        rows[i, 0] = (0.0, 0.0, rows[i, 1, 2])
    return rows, worst


def model_db(params):
    gain, v_db, log_w = params
    return gain + shelf_db(v_db, log_w)


def check(us, rows):
    """The largest RMS difference in dB, over FIT_MU, between the model
    interpolated at midpoints of the distance grid and the exact curve,
    against the error of a fit made there."""
    worst = (0.0, None)
    for i, alpha in enumerate(ANGLES[::4]):
        row = rows[i * 4]
        for j in range(len(us) - 1):
            u = 0.5 * (us[j] + us[j + 1])
            exact = target(alpha, u)
            interpolated = model_db(0.5 * (row[j] + row[j + 1]))
            own = model_db(fit(exact, dc_db(alpha, u), 0.5 * (row[j, 1:] + row[j + 1, 1:]))[:3])
            excess = np.sqrt(np.mean((interpolated - exact) ** 2)) - np.sqrt(
                np.mean((own - exact) ** 2)
            )
            if excess > worst[0]:
                worst = (excess, (float(alpha), float(1.0 / u)))
    # Midpoints between angles, at every fourth distance.
    for i in range(len(ANGLES) - 1):
        alpha = 0.5 * (ANGLES[i] + ANGLES[i + 1])
        for j in range(1, len(us), 4):
            exact = target(alpha, us[j])
            middle = 0.5 * (rows[i, j] + rows[i + 1, j])
            interpolated = model_db(middle)
            own = model_db(fit(exact, dc_db(alpha, us[j]), middle[1:])[:3])
            excess = np.sqrt(np.mean((interpolated - exact) ** 2)) - np.sqrt(
                np.mean((own - exact) ** 2)
            )
            if excess > worst[0]:
                worst = (excess, (float(alpha), float(1.0 / us[j])))
    return worst


def write(path, us, rows):
    with open(path, "w", encoding="utf-8") as out:
        out.write("// SPDX-License-Identifier: MIT\n")
        out.write("// Copyright (c) 2026 Sirac Ozmen\n//\n")
        out.write("// Generated by tools/near_field_table.py; do not edit.\n\n")
        out.write('#include "near_field_table.h"\n\n')
        out.write("// clang-format off\n")
        out.write("const float maudNearFieldInverseDistances[MAUD_NEAR_FIELD_DISTANCES] = {\n")
        out.write("".join(f"    {u:#.9g}f,\n" for u in us))
        out.write("};\n\n")
        out.write(
            "const float maudNearFieldTable[MAUD_NEAR_FIELD_ANGLES][MAUD_NEAR_FIELD_DISTANCES][3] = {\n"
        )
        for i in range(len(ANGLES)):
            out.write("    {\n")
            for j in range(len(us)):
                g, v, w = rows[i, j]
                out.write(f"        {{{g:#.6g}f, {v:#.6g}f, {w:#.6g}f}},\n")
            out.write("    },\n")
        out.write("};\n")
        out.write("// clang-format on\n")


# Reference points for the library's test: (incidence angle, distance in
# metres), on and off the grid, near and far side; a set measured at
# 1.2 m, a head of 8.75 cm.
REFERENCE_POINTS = ((0.0, 0.15), (30.0, 0.2), (62.5, 0.9), (85.0, 0.12), (97.5, 0.3),
                    (135.0, 0.11), (180.0, 0.5))
REFERENCE_HZ = (100.0, 300.0, 1000.0, 3000.0, 6000.0, 10000.0, 12000.0)
REFERENCE_RADIUS = 0.0875
REFERENCE_SET = 1.2
SPEED_OF_SOUND = 343.0


def write_reference(path):
    mus = np.array(REFERENCE_HZ) * 2.0 * np.pi * REFERENCE_RADIUS / SPEED_OF_SOUND
    with open(path, "w", encoding="utf-8") as out:
        out.write("// SPDX-License-Identifier: MIT\n")
        out.write("// Copyright (c) 2026 Sirac Ozmen\n//\n")
        out.write("// Generated by tools/near_field_table.py; do not edit. The rigid\n")
        out.write("// sphere's variation in dB, exactly, for a set measured at 1.2 m and a\n")
        out.write("// head of 8.75 cm: per point, the incidence angle, the distance in\n")
        out.write("// metres, then the variation at each frequency.\n\n")
        out.write("// clang-format off\n")
        out.write("#define REFERENCE_POINTS %d\n#define REFERENCE_FREQUENCIES %d\n\n"
                  % (len(REFERENCE_POINTS), len(REFERENCE_HZ)))
        out.write("static const double s_referenceHz[REFERENCE_FREQUENCIES] = {\n")
        out.write("    " + ", ".join(f"{f:#.1f}" for f in REFERENCE_HZ) + ",\n};\n\n")
        out.write("static const double s_reference[REFERENCE_POINTS][2 + REFERENCE_FREQUENCIES] = {\n")
        u_set = REFERENCE_RADIUS / REFERENCE_SET
        for alpha, metres in REFERENCE_POINTS:
            u = REFERENCE_RADIUS / metres
            values = target(alpha, u, mus) - target(alpha, u_set, mus)
            out.write(f"    {{{alpha:#.1f}, {metres:#.3f}, " + ", ".join(f"{v:#.4f}" for v in values) + "},\n")
        out.write("};\n")
        out.write("// clang-format on\n")


def main():
    parser = argparse.ArgumentParser(description="the near-field table")
    parser.add_argument("output")
    parser.add_argument("--distances", type=int, default=24)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--reference")
    arguments = parser.parse_args()
    if arguments.reference is not None:
        write_reference(arguments.reference)
    us = u_grid(arguments.distances)
    rows, worst = table(us)
    write(arguments.output, us, rows)
    print(f"{len(ANGLES)} angles x {len(us)} distances; worst fit {worst[0]:.2f} dB RMS at {worst[1]}")
    if arguments.check:
        excess, where = check(us, rows)
        print(f"interpolation adds at most {excess:.3f} dB RMS (at {where})")


if __name__ == "__main__":
    main()
