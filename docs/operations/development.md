# Development

How to build and test the library while working on it, regenerate its data, and measure it.

## Contents

- [Toolchain](#toolchain)
- [Building and testing](#building-and-testing)
- [Shared family files](#shared-family-files)
- [Generated sources and data](#generated-sources-and-data)
- [Benchmarks](#benchmarks)
- [Fuzzing](#fuzzing)

## Toolchain

GCC 14 or Clang 19 and newer (the family refuses older compilers, since the library is C23), CMake 3.25, Ninja, and the development headers of PipeWire, PulseAudio and ALSA on Linux. The checks also need `clang-format` at the version CI pins (22.1.5, from pip), `clang-tidy-20` and Python 3. The organization's CI image (`ghcr.io/rawframe-project/ci-cpp`) has all of them, and runs locally with Docker.

On the shared development machine the default `gcc` is 13: pass `-DCMAKE_C_COMPILER=gcc-14` or `clang-20`. Run large builds and full test runs through the workspace's heavy-job wrapper (`/home/rawframe-project/tools/heavy.sh`).

## Building and testing

```sh
cmake -B out/dev -G Ninja -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Debug -DMAUL_AUDIO_SANITIZE=ON -DMAUL_AUDIO_WERROR=ON
cmake --build out/dev
ctest --test-dir out/dev -L reverb        # one suite, by its label
```

Each suite is `test/test_<name>.c`, registered as `<name>` with the label `<name>`. Debug builds turn the library's internal assertions on (`MAUD_ASSERT`, [invariant.h](../../src/invariant.h)); `MAUL_AUDIO_SANITIZE` adds AddressSanitizer and UBSan, `MAUL_AUDIO_TSAN` ThreadSanitizer.

The backend suites (`pipewire`, `pulse`, `alsa`, `wasapi`, `coreaudio`, `aaudio`, the web pages) need a platform; [Test environments](test-environments.md) says how each one runs. Without its platform a backend suite exits 77, which CTest reports as skipped.

## Shared family files

The family's shared files (the conventions page, formatting and analysis settings, CMake modules, the test harness and the checks in `tools/`) are written by the organization's sync tool and must not be edited here:

```sh
python3 /home/rawframe-project/.github/maul/family_sync.py --check maul-audio
python3 /home/rawframe-project/.github/maul/family_sync.py --write maul-audio
```

A change to one is made in the canonical copy first, through the manager. `AGENTS.md` lists the files.

## Generated sources and data

| Path | Generator | When to run it |
|---|---|---|
| `src/near_field_table.c` | `python3 tools/near_field_table.py src/near_field_table.c` (numpy), with `--reference test/near_field_reference.h` for the test's exact values | Never, unless the near-field model changes. The output depends only on the script and numpy's arithmetic. |
| `data/hrtf/sadie2-ku100-48k.maudhrtf` | `tools/sofa_to_maudhrtf.py` (numpy, h5py), see below | When the HRTF format or its conversion changes. |
| `data/golden/*.wav` | `MAUD_WRITE_GOLDEN=1 ctest -R '^golden$'` | When rendering changes on purpose. |
| `data/bake/office.maudbake` | `MAUD_WRITE_GOLDEN=1 ctest -R '^spatializer_bake$'` | When baking changes on purpose. |
| `README.md`'s cost table | `python3 tools/cost_table.py` from `bench/baseline.txt` | When the baseline is recorded again. |

A pull request that rewrites a golden file says which change in results caused it.

### The default HRTF set

`data/hrtf/sadie2-ku100-48k.maudhrtf` is generated, never edited, from the SADIE II database's subject D1 (a Neumann KU100 dummy head, University of York, Apache License 2.0):

- **Source:** `D1_HRIR_SOFA.zip` from <https://zenodo.org/records/12092466> (SADIE II v2-2), SHA-256 `366321fa78f211bacc0ec6bea96701625b196a5db54ec16748b0eab0b9705f75`; its member `D1_HRIR_SOFA/D1_48K_24bit_256tap_FIR_SOFA.sofa`, SHA-256 `9af7cb19531e52fb7ae8ec92621e6ab62b1d5fe584b3742be36699a0ddb0ccd4`.
- **Command,** with numpy 2.5.3 and h5py 3.16.0:

  ```sh
  tools/sofa_to_maudhrtf.py D1_48K_24bit_256tap_FIR_SOFA.sofa \
      data/hrtf/sadie2-ku100-48k.maudhrtf \
      --name "SADIE II D1 (KU100), 48 kHz" \
      --license-file data/hrtf/sadie2-ku100.notice.txt
  ```

- **Result:** 1,652 directions on 37 rings, 128 taps at 48 kHz, measured at 1.2 m, 853,896 bytes, SHA-256 `6867385a5f475ff735151daeba145d12e5c7524c9a52cb1be37f65c587deac33`.

`data/hrtf/sadie2-ku100.notice.txt` carries the copyright, the license notice, the reference the authors ask for and the changes made; the same text is inside the file. `data/hrtf/apache-2.0/` holds the license text exactly as published, with its SHA-256.

### Golden renders and bakes

The golden renders are four scenes rendered through the offline backend at 48 kHz in 10 ms blocks on the shipped HRTF set, stored as 16-bit stereo WAV so anyone can listen to them: noise through the binaural effect circling and rising past the listener (`hrtf-sweep.wav`), a third-order bed turned a full turn (`ambisonic-rotation.wav`), a source walking behind a wall with the spatializer's occlusion and transmission feeding the direct effect (`occlusion-walk.wav`), and a burst of noise through the reverb (`reverb-tail.wav`). A render passes when each 10 ms block's error is at least 50 dB below the reference block's level.

`data/bake/office.maudbake` is the office scene of `test/test_spatializer_bake.c` (5 x 3 x 4 m, a material per wall, 20 probes on a 1 m grid, 1,024 rays, reflections of order 1 over 0.05 s), baked by the library. The test bakes it again and requires the same bytes on every platform.

## Benchmarks

`bench/` holds plain C benchmarks with fixed inputs, built with the library (`MAUL_AUDIO_BUILD_BENCH`): `maul-audio_bench_binaural`, `_bench_scene`, `_bench_speakers`, `_bench_control`, `_bench_voice`, `_bench_stream` and the family's `_bench`. Each prints the best of several runs beside its row in `bench/baseline.txt`, which records the figures and a budget per row on one machine. Timings are never a CI gate: wall time is not repeatable on shared runners.

Measure in a Release build on a quiet machine, alternating the old and new code, and quote both in the pull request. When a change means to move a figure, record the row again in `bench/baseline.txt` and run `tools/cost_table.py` so the README's table follows; `tools/cost_table.py --check` fails when the two differ.

On the web, `bench/size_web.c` links one feature at a time, so the web cell reports what each costs a page.

## Fuzzing

`test/fuzz_hrtf.c` and `test/fuzz_bake.c` fuzz the two loaders with libFuzzer (`-DMAUL_AUDIO_FUZZ=ON`, Clang). A target's first byte chooses what the rest is (CMakeLists.txt says how), so the shipped HRTF set behind a `0x01` and the shipped bakes behind a `0x03` seed them, with the minimized corpora in `test/corpus/<target>/`. Always pass `-max_total_time`:

```sh
cmake -B out/fuzz -G Ninja -DCMAKE_C_COMPILER=clang-20 -DMAUL_AUDIO_FUZZ=ON -DMAUL_AUDIO_DEVICE=OFF -DMAUL_AUDIO_BUILD_TESTS=OFF
cmake --build out/fuzz
./out/fuzz/fuzz_bake -max_total_time=60 test/corpus/fuzz_bake
```

To grow a corpus, run a target on a copy of its directory, then merge back what reaches new code: `fuzz_bake -set_cover_merge=1 test/corpus/fuzz_bake grown/`. A crash becomes a test in its module's suite with the minimized input.
