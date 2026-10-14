# Fuzz corpora

Inputs for the fuzz targets (`test/fuzz_<area>.c`): `fuzz_hrtf/` and
`fuzz_bake/`, each the first byte the target reads as its choice and
then the file. They are what a long campaign under ASan and UBSan found,
minimized with libFuzzer's `-set_cover_merge=1`: small HRTF sets of one
to three rings and one to sixteen taps, and bakes of two probes at
reflection orders 0 to 3, made by the library and changed by the
fuzzer, none larger than 8 KB. CI's fuzz job starts from them, with the
shipped files in `data/` as further seeds.

To grow them, run a target on a copy of its directory for as long as
you like, then merge the result back:

    fuzz_bake -set_cover_merge=1 test/corpus/fuzz_bake grown/
