#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Sirac Ozmen
#
# Converts a SOFA file (SimpleFreeFieldHRIR) into a .maudhrtf file, the
# format docs/hrtf-format.md specifies. Each measured response's onset is
# found where its low-passed version (below 1.2 to 2 kHz, where the
# interaural delay is a delay rather than pinna detail) first reaches a
# tenth of its peak; the response is made minimum-phase and cut to the
# format's length;
# the measured directions are then resampled onto elevation rings 5
# degrees apart, each with round(72 cos(elevation)) evenly spaced
# azimuths, by inverse-angle weighting of the three nearest measured
# directions (the nearest alone when it lies within 0.1 degree).
#
# The output depends only on the input, this script and numpy's
# arithmetic: no time stamps, a fixed order everywhere.
#
# Needs numpy and h5py (both BSD licensed; tool-only, see THIRD_PARTY.md).
#
# usage: sofa_to_maudhrtf.py input.sofa output.maudhrtf
#            [--name TEXT] [--license-file PATH]
#   --name          the dataset and subject; by default from the file's
#                   DatabaseName and ListenerShortName attributes
#   --license-file  the license and attribution text; by default the
#                   file's License attribute, which must then be present

import argparse
import struct
import sys
import zlib

try:
    import h5py
    import numpy as np
except ImportError:
    sys.exit("sofa_to_maudhrtf.py needs numpy and h5py: pip install numpy h5py")

MAGIC = b"MAUDHRTF"
VERSION = 1
RING_STEP = 5.0
AZIMUTHS_AT_EQUATOR = 72
TAPS_AT_48K = 128
FADE_TAPS = 16
FFT_SIZE = 8192
ONSET_FRACTION = 0.1
ONSET_PASS_HZ = 1200.0
ONSET_STOP_HZ = 2000.0
NEAREST_ONLY_DEGREES = 0.1
NEIGHBOURS = 3
DELAY_UNITS = 256


def attribute(sofa, name):
    value = sofa.attrs.get(name)
    if value is None:
        return None
    return value.decode("utf-8") if isinstance(value, bytes) else str(value)


def unit_vectors(azimuth, elevation):
    """Directions in degrees to unit vectors (x ahead, y left, z up)."""
    a = np.radians(azimuth)
    e = np.radians(elevation)
    return np.stack([np.cos(e) * np.cos(a), np.cos(e) * np.sin(a), np.sin(e)], axis=1)


def read_sofa(path):
    with h5py.File(path, "r") as sofa:
        if attribute(sofa, "SOFAConventions") != "SimpleFreeFieldHRIR":
            sys.exit(f"{path}: not a SimpleFreeFieldHRIR file")
        ir = np.asarray(sofa["Data.IR"], dtype=np.float64)
        rate = float(np.asarray(sofa["Data.SamplingRate"]).reshape(-1)[0])
        position = np.asarray(sofa["SourcePosition"], dtype=np.float64)
        kind = attribute(sofa["SourcePosition"], "Type") or "spherical"
        if kind == "cartesian":
            x, y, z = position[:, 0], position[:, 1], position[:, 2]
            position = np.stack(
                [np.degrees(np.arctan2(y, x)), np.degrees(np.arctan2(z, np.hypot(x, y)))], axis=1
            )
        if ir.ndim != 3 or ir.shape[1] != 2 or ir.shape[0] != position.shape[0]:
            sys.exit(f"{path}: Data.IR is not M x 2 x N for M source positions")
        name = " ".join(
            part
            for part in (attribute(sofa, "DatabaseName"), attribute(sofa, "ListenerShortName"))
            if part
        )
        return ir, rate, position[:, 0], position[:, 1], name, attribute(sofa, "License")


def low_pass(response, rate):
    """The response through a zero-phase low-pass: flat to
    ONSET_PASS_HZ, a half-cosine down to zero at ONSET_STOP_HZ."""
    spectrum = np.fft.rfft(response, FFT_SIZE)
    frequency = np.fft.rfftfreq(FFT_SIZE, 1.0 / rate)
    ramp = np.clip((frequency - ONSET_PASS_HZ) / (ONSET_STOP_HZ - ONSET_PASS_HZ), 0.0, 1.0)
    return np.fft.irfft(spectrum * 0.5 * (1.0 + np.cos(np.pi * ramp)), FFT_SIZE)[: len(response)]


def onset(response, rate):
    """The fractional sample where the low-passed response first reaches
    ONSET_FRACTION of its peak, interpolated linearly."""
    magnitude = np.abs(low_pass(response, rate))
    threshold = ONSET_FRACTION * magnitude.max()
    index = int(np.argmax(magnitude >= threshold))
    if index == 0:
        return 0.0
    before, after = magnitude[index - 1], magnitude[index]
    return index - 1 + (threshold - before) / (after - before)


def minimum_phase(response, taps):
    """The minimum-phase response with the same magnitude (real
    cepstrum), cut to taps with a half-cosine fade over its last
    FADE_TAPS."""
    spectrum = np.fft.rfft(response, FFT_SIZE)
    cepstrum = np.fft.irfft(np.log(np.maximum(np.abs(spectrum), 1e-12)), FFT_SIZE)
    fold = np.zeros(FFT_SIZE)
    fold[0] = 1.0
    fold[1 : FFT_SIZE // 2] = 2.0
    fold[FFT_SIZE // 2] = 1.0
    result = np.fft.irfft(np.exp(np.fft.rfft(cepstrum * fold, FFT_SIZE)), FFT_SIZE)[:taps]
    fade = 0.5 * (1.0 + np.cos(np.pi * np.arange(1, FADE_TAPS + 1) / FADE_TAPS))
    result[taps - FADE_TAPS :] *= fade
    return result


def rings():
    elevations = np.arange(-90.0, 90.0 + RING_STEP / 2, RING_STEP)
    counts = [max(1, int(round(AZIMUTHS_AT_EQUATOR * np.cos(np.radians(e))))) for e in elevations]
    return elevations, counts


def grid(elevations, counts):
    azimuth = np.concatenate([np.arange(n) * (360.0 / n) for n in counts])
    elevation = np.concatenate([np.full(n, e) for e, n in zip(elevations, counts)])
    return azimuth, elevation


def resample(measured, values, wanted):
    """Values at the wanted directions from the measured ones: the
    nearest alone within NEAREST_ONLY_DEGREES, otherwise the NEIGHBOURS
    nearest weighted by inverse angle."""
    result = np.empty((wanted.shape[0],) + values.shape[1:])
    for i, direction in enumerate(wanted):
        cosine = np.clip(measured @ direction, -1.0, 1.0)
        order = np.argsort(-cosine, kind="stable")[:NEIGHBOURS]
        angles = np.degrees(np.arccos(cosine[order]))
        if angles[0] <= NEAREST_ONLY_DEGREES:
            result[i] = values[order[0]]
            continue
        weights = 1.0 / angles
        weights /= weights.sum()
        result[i] = np.tensordot(weights, values[order], axes=1)
    return result


def convert(ir, rate, azimuth, elevation):
    taps = max(8, int(round(TAPS_AT_48K * rate / 48000.0)))
    count = ir.shape[0]
    onsets = np.array([[onset(ir[m, ear], rate) for ear in range(2)] for m in range(count)])
    responses = np.array([[minimum_phase(ir[m, ear], taps) for ear in range(2)] for m in range(count)])
    ring_elevations, ring_counts = rings()
    wanted = unit_vectors(*grid(ring_elevations, ring_counts))
    measured = unit_vectors(azimuth, elevation)
    delays = resample(measured, onsets, wanted)
    delays -= delays.min()
    taps_out = resample(measured, responses, wanted)
    return taps, ring_elevations, ring_counts, delays, taps_out


def encode(rate, taps, ring_elevations, ring_counts, delays, responses, name, license_text):
    peak = float(np.abs(responses).max())
    scale = np.float32(peak / 32767.0 if peak > 0 else 1.0)
    samples = np.clip(np.round(responses / float(scale)), -32768, 32767).astype("<i2")
    units = np.clip(np.round(delays * DELAY_UNITS), 0, 65535).astype("<u2")
    name_bytes = name.encode("utf-8")[:256]
    license_bytes = license_text.encode("utf-8")
    if len(license_bytes) > 65536:
        sys.exit("the license text is longer than 65,536 bytes")
    body = bytearray(name_bytes)
    body += license_bytes
    for elevation, count in zip(ring_elevations, ring_counts):
        body += struct.pack("<fI", float(elevation), count)
    body += units.tobytes()
    body += samples.tobytes()
    header = MAGIC + struct.pack(
        "<IIIIIfII",
        VERSION,
        int(round(rate)),
        taps,
        len(ring_counts),
        int(sum(ring_counts)),
        float(scale),
        len(name_bytes),
        len(license_bytes),
    )
    return header + struct.pack("<I", zlib.crc32(body)) + bytes(body)


def main():
    parser = argparse.ArgumentParser(description="SOFA to .maudhrtf")
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--name")
    parser.add_argument("--license-file")
    arguments = parser.parse_args()
    ir, rate, azimuth, elevation, name, license_text = read_sofa(arguments.input)
    if arguments.name is not None:
        name = arguments.name
    if arguments.license_file is not None:
        with open(arguments.license_file, encoding="utf-8") as text:
            license_text = text.read()
    if not license_text:
        sys.exit("no License attribute in the file: give --license-file")
    taps, ring_elevations, ring_counts, delays, responses = convert(ir, rate, azimuth, elevation)
    data = encode(rate, taps, ring_elevations, ring_counts, delays, responses, name, license_text)
    with open(arguments.output, "wb") as out:
        out.write(data)
    print(
        f"{arguments.output}: {sum(ring_counts)} directions on {len(ring_counts)} rings, "
        f"{taps} taps at {int(round(rate))} Hz, {len(data)} bytes"
    )


if __name__ == "__main__":
    main()
