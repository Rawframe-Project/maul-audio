# Bake data

## `office.maudbake`

A bake in the format `docs/bake-format.md` specifies, made by the
library itself: the office of `test/test_spatializer_bake.c` (5 x 3 x
4 m, a material per wall), 20 probes on a 1 m grid at 1.5 m, 1024
reverberation rays, reflections of order 1 over 0.05 s. That test bakes
it again and requires the same bytes on every platform, which is the
bake's promise of input-deterministic output; setting
`MAUD_WRITE_GOLDEN` while running the test rewrites it, which only a
deliberate change to the bake may do. It is also the bake fuzz
target's seed, behind a 0x03 byte.
