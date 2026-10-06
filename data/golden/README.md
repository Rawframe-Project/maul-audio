# Golden renders

The references `test/test_golden.c` holds the library's rendering to,
made by the library itself through the offline backend at 48 kHz in 10
ms blocks, on the shipped HRTF set, and stored as 16-bit stereo WAV so
that anyone can listen to them:

- `hrtf-sweep.wav`: white noise through the binaural effect, the source
  2 m away turning once about the listener in the second while rising
  from 45 degrees below to 45 above.
- `ambisonic-rotation.wav`: noise encoded at third order ahead, left and
  up, the bed turned a full turn about the vertical in the half second
  and decoded for both ears.
- `occlusion-walk.wav`: a source walking from x = -3 to 3 m along z =
  -4 m, from sight to behind a wall 4 m wide at z = -2 m; each block
  the spatializer's volumetric occlusion and transmission feed the
  direct effect, then the binaural effect.
- `reverb-tail.wav`: 10 ms of noise into the reverb (T60 1.2, 0.9 and
  0.5 s per band, 20 ms delay), its first-order bed decoded for both
  ears.

A render passes when, per 10 ms block and channel, its error's RMS is
at most the reference block's RMS times 10^(-50/20), or 3e-5 when that
is less; renders differ between platforms by far less (the C library's
math, an ulp in a coefficient). Setting `MAUD_WRITE_GOLDEN` while
running the test rewrites them, which only a deliberate change to
rendering may do.
