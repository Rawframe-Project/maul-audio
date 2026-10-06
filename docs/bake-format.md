# The Maul Audio bake format

A `.maudbake` file holds one baked probe set: its probes, the links
between them, and per probe the reverberation times and levels and,
when the bake was made with reflections, the energy field.
`maudSaveProbeSet` writes it and `maudLoadProbeSet` reads it; the same
set saves to the same bytes on every platform. The loader treats it as
hostile input.

All numbers are little-endian. Offsets are in bytes from the start of
the file.

## Header

| Offset | Type | Field | Rule |
|---|---|---|---|
| 0 | 8 bytes | magic | `MAUDBAKE` |
| 8 | uint32 | version | 1 |
| 12 | uint32 | probeCount | 0 to the loading spatializer's maxProbes |
| 16 | uint32 | linkCount | 0 to the loading spatializer's maxProbePairs |
| 20 | float32 | range | 0.1 to 1,000: the longest link, in metres |
| 24 | uint32 | layers | 0 (probes and links), 1 (and reverberation) or 3 (and fields) |
| 28 | uint32 | fieldOrder | 0 without fields, else 1 to 3 |
| 32 | uint32 | fieldBins | 0 without fields, else 1 to 401 |
| 36 | uint32 | crc | CRC-32 (ISO-HDLC, as zlib computes it) of every byte from offset 40 to the end |

## Body

From offset 40, in this order, with nothing between:

1. **Probes:** `probeCount` records of three float32, x, y and z in
   metres, y up, each finite.
2. **Rows:** `probeCount` + 1 uint32, where each probe's links start:
   the first 0, never falling, the last 2 x `linkCount`.
3. **Neighbours:** 2 x `linkCount` uint32, probe i's from row i to row
   i + 1: each below `probeCount`, not i, rising strictly within a row,
   no farther from probe i than `range`, and every link listed from
   both ends.
4. **Times** (layer 1): for each probe, three float32 reverberation times in
   seconds, low band first, each 0.1 to 20.
5. **Levels** (layer 1): for each probe, three float32 levels in dB, each -96 to
   24.
6. **Fields** (layer 2): for each probe, (`fieldOrder` + 1)^2 channels
   in ACN order, for each channel three bands, for each band
   `fieldBins` float32 energies in 10 ms bins, each finite and at least
   0 for channel 0.

The file ends after the last section; a longer or shorter file is
invalid. Link lengths are not stored: they are the distances between
the probes.

## Loading

A spatializer loads a file only if its counts fit its limits, and a
file with reverberation only if its fields match the spatializer's
reflections: none without reflections, and with them the bed's order
and the response's 10 ms bins. A file of probes and links loads into
any spatializer with room for it. A file that is
well formed but does not fit is refused with `maud_errorCapacity`
(counts) or `maud_errorUnsupported` (fields, or another version); one
that is not well formed with `maud_errorInvalid`.
