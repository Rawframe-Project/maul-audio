# File formats

The library reads two kinds of file, and writes one. Both are little-endian, start with an eight-byte magic and a version, and seal what follows with a CRC-32 (ISO-HDLC, as zlib computes it). Both loaders treat the bytes as hostile: every count is checked against a limit, every size is computed from the counts and compared, and the checksum verified, before anything is allocated or any value is read; then every value is checked against its range. Both are fuzzed (`test/fuzz_hrtf.c`, `test/fuzz_bake.c`).

Both are made of tagged fields, so that a later version can add one without breaking an earlier reader's files ([maul-conventions.md](maul-conventions.md#compatibility)). A format's version changes whenever its bytes change, and before 1.0 a reader takes only the current version, refusing another with `maud_errorVersion`.

## Contents

- [Tagged fields](#tagged-fields)
- [HRTF sets (.maudhrtf)](#hrtf-sets-maudhrtf)
- [Bakes (.maudbake)](#bakes-maudbake)

## Tagged fields

After the header comes a sequence of fields to the end of the file ([file_fields.h](../../src/file_fields.h)). Each field is a uint32 tag, a uint32 byte length and the payload. The payload starts at a multiple of 8 bytes from the file's start, and is padded with zero bytes to the next multiple of 8. No payload holds an element wider than 4 bytes, and the loaders copy what they keep, so 8 is enough. A tag is four ASCII characters, the first in the lowest byte, so a dump shows it in order.

A reader takes the fields in any order. It refuses a tag it does not know with `maud_errorVersion`, since only a later version writes one. It refuses a field written twice, a field longer than what is left, and padding that is not zero, with `maud_errorInvalid`. An absent field takes its documented default; a field with no default is required. A writer puts the fields in the order the tables below give, and leaves out a field that holds its default, so its bytes depend only on the content.

## HRTF sets (.maudhrtf)

One head-related transfer function set at one sample rate, laid out for the renderer: minimum-phase responses with their onset delays kept apart, on elevation rings of evenly spaced azimuths. Kept apart, a renderer interpolates responses and delays separately, which keeps interaural time differences right between measured directions; a measured response with its delay inside would smear when two are blended. `tools/sofa_to_maudhrtf.py` writes it from a SOFA file; [hrtf_file.h](../../src/hrtf_file.h) reads it.

### Header

| Offset | Type | Field | Valid values |
|---|---|---|---|
| 0 | 8 bytes | magic | `MAUDHRTF` |
| 8 | uint32 | version | 3; another is `maud_errorVersion` |
| 12 | uint32 | CRC-32 | of every byte from offset 16 to the end |

### Fields

From offset 16:

| Tag | Payload | Absent |
|---|---|---|
| `head` | 24 bytes: the sample rate (uint32, 8,000 to 384,000), taps per response (uint32, 8 to 1,024), ring count (uint32, 1 to 181), direction count (uint32, 1 to 65,536, the sum of the rings' azimuth counts), scale (float32, finite and above 0: a tap is its int16 value times this) and distance (float32, 0.05 to 100: metres from the head's centre at which the set was measured) | `maud_errorInvalid` |
| `name` | UTF-8, the dataset and subject, not terminated, up to 256 bytes | empty |
| `lics` | UTF-8, the dataset's license and attribution, not terminated, up to 65,536 bytes. A set travels with its license, and `maudGetHrtfInfo` hands both texts to the host. | empty |
| `ring` | per ring, a float32 elevation in degrees and a uint32 azimuth count of at least 1. Elevations rise strictly from -90 to 90 inclusive. Each ring's first azimuth is 0 and its step 360 divided by its count. | `maud_errorInvalid` |
| `dlay` | per direction, ring by ring and azimuth by azimuth, two uint16, the left ear's onset delay then the right's, in 1/256 of a sample | `maud_errorInvalid` |
| `taps` | per direction in the same order, the left ear's taps then the right's, int16 each | `maud_errorInvalid` |

Each field's length must be the one the head's counts give: 8 bytes a ring, 4 a direction for the delays, 4 times the taps a direction for the taps.

Directions follow SOFA's convention: azimuth counterclockwise from straight ahead (90 is the left), elevation up from the horizontal plane. The library converts from the listener frame at its API.

## Bakes (.maudbake)

One probe set: its probes, the links between them, and per probe the reverberation estimate and, when the spatializer renders reflections, the energy field. `maudSaveProbeSet` writes it and `maudLoadProbeSet` reads it ([bake_file.h](../../src/bake_file.h)). A set saves to the same bytes on every platform: bakes are content a program ships, so their bytes must not depend on the machine that made them.

### Header

| Offset | Type | Field | Valid values |
|---|---|---|---|
| 0 | 8 bytes | magic | `MAUDBAKE` |
| 8 | uint32 | version | 3; another is `maud_errorVersion` |
| 12 | uint32 | CRC-32 | of every byte from offset 16 to the end |

### Fields

From offset 16:

| Tag | Payload | Absent |
|---|---|---|
| `head` | 12 bytes: the probe count (uint32, 0 to the loading spatializer's `maxProbes`), the link count (uint32, 0 to its `maxProbePairs`) and the range (float32, 0.1 to 1,000: the longest link in metres) | `maud_errorInvalid` |
| `prob` | per probe, x, y and z as float32 in metres, y up, each finite | `maud_errorInvalid` |
| `rows` | probe count + 1 uint32, where each probe's links start in `nbrs`: the first 0, never falling, the last twice the link count | `maud_errorInvalid` |
| `nbrs` | twice the link count uint32; probe i's neighbours lie between row i and row i + 1. Each is below the probe count and not i, they rise strictly within a row, none is farther from probe i than the range, and every link is listed from both ends. Link lengths are not stored: they are the distances between the probes. | `maud_errorInvalid` |
| `revb` | the estimates: per probe, three float32 reverberation times in seconds, low band first, each 0.1 to 20; then per probe three float32 levels in dB, each -96 to 24; then per probe three float32 times of the slower slope, each 0 (none in that band) or 0.1 to 20; then per probe three float32 levels of the slower slope in dB, each -96 to 24 | no estimates and no fields |
| `fhdr` | 8 bytes: the energy fields' order (uint32, 1 to 3) and their bins (uint32, 1 to 401) | no fields; present without `flds` is `maud_errorInvalid` |
| `flds` | the energy fields: per probe, (order + 1)^2 channels in ACN order; per channel three bands; per band, bins float32 energies in 10 ms bins, each finite, and at least 0 in channel 0 | no fields; present without `fhdr` or `revb` is `maud_errorInvalid` |

Each field's length must be the one the head's counts and the fields' layout give. A set of probes and links alone has neither `revb` nor the fields; one baked without reflections has `revb` alone. The writer leaves out what the set does not have.

### Loading

A file loads only if its counts fit the spatializer's limits, and a file with fields only if they match the spatializer's reflections: the same order and the same number of 10 ms bins. A file of probes and links alone loads into any spatializer with room for it. The results:

- `maud_errorInvalid`: the bytes are not a well-formed bake.
- `maud_errorCapacity`: well formed, but its counts do not fit.
- `maud_errorVersion`: another version, or a field only a later version writes.
- `maud_errorUnsupported`: a bake whose fields do not match.
