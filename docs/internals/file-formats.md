# File formats

The library reads two kinds of file, and writes one. Both are little-endian, start with an eight-byte magic, a version and a fixed header, end with nothing after their last section, and seal their body with a CRC-32 (ISO-HDLC, as zlib computes it) of every byte after the header. Both loaders treat the bytes as hostile: every count is checked against a limit, the file's exact length is computed from the counts and compared, and the checksum verified, before anything is allocated or any value is read; then every value is checked against its range. Both are fuzzed (`test/fuzz_hrtf.c`, `test/fuzz_bake.c`).

A format's version changes whenever its bytes change. Before 1.0 nothing promises that a later release reads an earlier file.

## Contents

- [HRTF sets (.maudhrtf)](#hrtf-sets-maudhrtf)
- [Bakes (.maudbake)](#bakes-maudbake)

## HRTF sets (.maudhrtf)

One head-related transfer function set at one sample rate, laid out for the renderer: minimum-phase responses with their onset delays kept apart, on elevation rings of evenly spaced azimuths. Kept apart, a renderer interpolates responses and delays separately, which keeps interaural time differences right between measured directions; a measured response with its delay inside would smear when two are blended. `tools/sofa_to_maudhrtf.py` writes it from a SOFA file; [hrtf_file.h](../../src/hrtf_file.h) reads it in place.

### Header

| Offset | Type | Field | Valid values |
|---|---|---|---|
| 0 | 8 bytes | magic | `MAUDHRTF` |
| 8 | uint32 | version | 2; another is `maud_errorUnsupported` |
| 12 | uint32 | sample rate | 8,000 to 384,000 |
| 16 | uint32 | taps per response | 8 to 1,024 |
| 20 | uint32 | ring count | 1 to 181 |
| 24 | uint32 | direction count | 1 to 65,536, the sum of the rings' azimuth counts |
| 28 | float32 | scale | finite and above 0: a tap is its int16 value times this |
| 32 | float32 | distance | 0.05 to 100: metres from the head's centre at which the set was measured |
| 36 | uint32 | name bytes | 0 to 256 |
| 40 | uint32 | license bytes | 0 to 65,536 |
| 44 | uint32 | CRC-32 | of every byte from offset 48 to the end |

### Body

From offset 48, with nothing between the sections:

1. **Name**: UTF-8, the dataset and subject, not terminated.
2. **License**: UTF-8, the dataset's license and attribution, not terminated. A set travels with its license, and `maudGetHrtfInfo` hands both texts to the host.
3. **Rings**: per ring, a float32 elevation in degrees and a uint32 azimuth count of at least 1. Elevations rise strictly from -90 to 90 inclusive. Each ring's first azimuth is 0 and its step 360 divided by its count.
4. **Delays**: per direction, ring by ring and azimuth by azimuth, two uint16, the left ear's onset delay then the right's, in 1/256 of a sample.
5. **Taps**: per direction in the same order, the left ear's taps then the right's, int16 each.

Directions follow SOFA's convention: azimuth counterclockwise from straight ahead (90 is the left), elevation up from the horizontal plane. The library converts from the listener frame at its API.

## Bakes (.maudbake)

One probe set: its probes, the links between them, and per probe the reverberation estimate and, when the spatializer renders reflections, the energy field. `maudSaveProbeSet` writes it and `maudLoadProbeSet` reads it ([bake_file.h](../../src/bake_file.h)). A set saves to the same bytes on every platform: bakes are content a program ships, so their bytes must not depend on the machine that made them.

### Header

| Offset | Type | Field | Valid values |
|---|---|---|---|
| 0 | 8 bytes | magic | `MAUDBAKE` |
| 8 | uint32 | version | 2; another is `maud_errorUnsupported` |
| 12 | uint32 | probe count | 0 to the loading spatializer's `maxProbes` |
| 16 | uint32 | link count | 0 to the loading spatializer's `maxProbePairs` |
| 20 | float32 | range | 0.1 to 1,000: the longest link in metres |
| 24 | uint32 | layers | 0 (probes and links), 1 (and estimates) or 3 (and fields) |
| 28 | uint32 | field order | 0 without fields, else 1 to 3 |
| 32 | uint32 | field bins | 0 without fields, else 1 to 401 |
| 36 | uint32 | CRC-32 | of every byte from offset 40 to the end |

### Body

From offset 40, with nothing between the sections:

1. **Probes**: per probe, x, y and z as float32 in metres, y up, each finite.
2. **Rows**: probe count + 1 uint32, where each probe's links start in the next section: the first 0, never falling, the last twice the link count.
3. **Neighbours**: twice the link count uint32; probe i's neighbours lie between row i and row i + 1. Each is below the probe count and not i, they rise strictly within a row, none is farther from probe i than the range, and every link is listed from both ends. Link lengths are not stored: they are the distances between the probes.
4. **Times** (layer 1): per probe, three float32 reverberation times in seconds, low band first, each 0.1 to 20.
5. **Levels** (layer 1): per probe, three float32 levels in dB, each -96 to 24.
6. **Tail times** (layer 1): per probe, three float32 times of the slower slope, each 0 (none in that band) or 0.1 to 20.
7. **Tail levels** (layer 1): per probe, three float32 levels of the slower slope in dB, each -96 to 24.
8. **Fields** (layer 2): per probe, (field order + 1)^2 channels in ACN order; per channel three bands; per band, field-bins float32 energies in 10 ms bins, each finite, and at least 0 in channel 0.

### Loading

A file loads only if its counts fit the spatializer's limits, and a file with fields only if they match the spatializer's reflections: the same order and the same number of 10 ms bins. A file of probes and links alone loads into any spatializer with room for it. The results:

- `maud_errorInvalid`: the bytes are not a well-formed bake.
- `maud_errorCapacity`: well formed, but its counts do not fit.
- `maud_errorUnsupported`: another version, or fields that do not match.
