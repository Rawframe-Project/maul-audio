# The Maul Audio HRTF format

A `.maudhrtf` file holds one head-related transfer function set at one
sample rate, ready for the binaural renderer: minimum-phase responses
with their onset delays kept apart, on elevation rings whose azimuths
are evenly spaced. `tools/sofa_to_maudhrtf.py` writes it from a SOFA
file. The library's loader reads it and treats it as hostile input.

All numbers are little-endian. Offsets are in bytes from the start of
the file.

## Header

| Offset | Type | Field | Rule |
|---|---|---|---|
| 0 | 8 bytes | magic | `MAUDHRTF` |
| 8 | uint32 | version | 1 |
| 12 | uint32 | sampleRate | 8,000 to 384,000 |
| 16 | uint32 | taps | 8 to 1,024 |
| 20 | uint32 | ringCount | 1 to 181 |
| 24 | uint32 | directionCount | 1 to 65,536; the sum of the rings' azimuth counts |
| 28 | float32 | scale | finite, greater than 0: a tap is its 16-bit value times scale |
| 32 | uint32 | nameBytes | 0 to 256 |
| 36 | uint32 | licenseBytes | 0 to 65,536 |
| 40 | uint32 | crc | CRC-32 (ISO-HDLC, as zlib computes it) of every byte from offset 44 to the end |

## Body

From offset 44, in this order, with nothing between:

1. **Name:** `nameBytes` of UTF-8, the dataset and subject, unterminated.
2. **License:** `licenseBytes` of UTF-8, the dataset's license and its
   attribution, unterminated.
3. **Rings:** `ringCount` records of eight bytes: float32 `elevation`
   in degrees, then uint32 `azimuthCount`, at least 1. Elevations rise
   strictly, from -90 to 90 inclusive. A ring's first azimuth is 0 and
   its step is 360 / `azimuthCount` degrees.
4. **Delays:** for each direction, ring by ring and azimuth by azimuth,
   two uint16: the left ear's onset delay, then the right's, in 1/256
   samples.
5. **Taps:** for each direction in the same order, `taps` int16 for the
   left ear, then `taps` for the right.

The file ends after the taps; a longer or shorter file is invalid.

## Directions

Directions follow SOFA's spherical convention, seen from the listener:
azimuth counterclockwise from straight ahead (90 is the left), and
elevation up from the horizontal plane. The library converts its own
frame at its API.

## Responses

Each response is minimum-phase and starts at its first tap; the delay
restores the time it took to arrive, so the interaural delay is the
difference between the ears' delays. A renderer interpolates responses
and delays separately.
