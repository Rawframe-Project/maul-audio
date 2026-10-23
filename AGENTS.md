# Maul Audio

Maul Audio is a C library of audio device I/O and spatial sound for games, tools and voice applications: streams, devices and voice processing on Linux, Windows, macOS, iOS, Android and the web (the Device part), and binaural, ambisonic and speaker rendering driven by geometry (the Spatial part). It is part of the Maul family; the family rules are in [docs/internals/maul-conventions.md](docs/internals/maul-conventions.md), and [docs/internals/overview.md](docs/internals/overview.md) explains how the parts fit.

It never compromises on:

1. **The audio thread.** Nothing the library runs there allocates, locks, waits or logs, and host code that calls back in is refused and counted, never trusted.
2. **Nothing changes silently.** A stream never resamples behind the host's back; every device change, suspension and dropped notification reaches the host as a typed record; a platform that cannot do something answers `maud_errorUnsupported`.
3. **Fixed memory.** Every object allocates when it is made, sized by named limits; going past a limit is `maud_errorCapacity`, never unbounded work.
4. **Hostile files.** HRTF sets and bakes are checked whole, every count against a limit, before anything is allocated, in every build.
5. **Bakes are content.** A bake is the same bytes on every platform, compiler and task split.

## Commands

```sh
cmake -B out/dev -G Ninja -DCMAKE_C_COMPILER=clang-20 -DCMAKE_BUILD_TYPE=Debug -DMAUL_AUDIO_SANITIZE=ON -DMAUL_AUDIO_WERROR=ON
cmake --build out/dev
ctest --test-dir out/dev -L reverb       # one suite, by label: test/test_<label>.c
ctest --test-dir out/dev                  # everything this machine can run
for c in check_source check_modules check_docs check_lengths check_guide check_litmus; do python3 tools/$c.py; done
python3 tools/cost_table.py --check       # README's cost table against bench/baseline.txt
python3 /home/rawframe-project/.github/scripts/docs_check.py --strict --exclude data/hrtf/apache-2.0
python3 /home/rawframe-project/.github/maul/family_sync.py --check maul-audio
```

Use gcc-14 or clang-20; the machine's default gcc is too old for C23. Run full builds and test runs through the workspace's heavy-job wrapper (`/home/rawframe-project/tools/heavy.sh`). Backend suites need their platform; [docs/operations/test-environments.md](docs/operations/test-environments.md) says how each runs.

## Ways to hurt yourself

- **Killing by pattern.** Never `pkill -f`, `pgrep | kill` or `kill` a PID found by matching a name or path: other sessions run sound servers, Wine, emulators, browsers and fuzzers on the same machine with the same names. Kill only a PID you started; `tools/pipewire_daemon.sh stop <dir>` stops only the daemons it started in `<dir>`.
- **Touching a real sound server.** Tests reach a private PipeWire daemon in a directory of their own (`tools/pipewire_daemon.sh start <dir>`, then `XDG_RUNTIME_DIR=<dir>/run`). Never run a test, `pactl` or `pw-cli` against the session's own server, and never restart `pipewire`, `pipewire-pulse` or `coreaudiod` on a shared machine: the device checklist's restarts are for a developer's own hardware.
- **Sharing Wine.** Give Wine a prefix of your own (`WINEPREFIX`), hold one `wineserver -p` for a run and stop it after; another session's wineserver may exit under you.
- **Leaving emulators, simulators and browsers running.** Stop the Android emulator, iOS simulator, headless Chrome or test daemon you started when the run that needed it ends.
- **Endless fuzzing.** A libFuzzer target runs until it finds something. Always pass `-max_total_time`.
- **Rewriting golden data.** `MAUD_WRITE_GOLDEN` rewrites `data/golden/*.wav` and `data/bake/office.maudbake`. Set it only for a deliberate change of results, and say which change in the pull request.
- **Editing generated or vendored files.** `src/near_field_table.c` is written by `tools/near_field_table.py`, `data/hrtf/sadie2-ku100-48k.maudhrtf` by `tools/sofa_to_maudhrtf.py` ([development.md](docs/operations/development.md#generated-sources-and-data) has the commands and sources); `data/hrtf/apache-2.0/` is the license exactly as published.
- **Editing shared family files.** These are synced from the canonical copies in the organization's `.github` repository (`maul/`) and fail CI when they differ: `docs/internals/maul-conventions.md`; `.clang-format`, `.clang-tidy`, `.editorconfig`, `.gitattributes`, `.gitignore`; `cmake/CompilerFlags.cmake`, `cmake/Install.cmake`, `cmake/Library.cmake`, `cmake/ReadVersion.cmake`, `cmake/Testing.cmake`, `cmake/Config.cmake.in`, `cmake/pkg-config.pc.in`, `cmake/HeaderCheck/CMakeLists.txt`; `bench/CMakeLists.txt`; `test/test_harness.h`; and `tools/build_docs.py`, `check_docs.py`, `check_guide.py`, `check_lengths.py`, `check_modules.py`, `check_source.py`, `gen_api.py`. Change the canonical copy first, through the manager.
- **Reaching Java by name without a keep rule.** `src/aaudio_java.c` finds `java/maul/audio` classes and members through JNI. A new one needs a rule in `java/proguard-rules.pro`; the Android tests are built by R8 and fail without it.

## Invariants

- **Two parts.** No Device source calls a Spatial one or the reverse; the `MAUL_AUDIO_DEVICE=OFF` and `MAUL_AUDIO_SPATIAL=OFF` builds link only while that holds. Shared code is the base (`MAUL_AUDIO_BASE_SOURCES` in `CMakeLists.txt`).
- **The audio thread.** Code reachable from a platform's audio callback, a stream's period loop, an effect's `Process` call or the spatializer's rendering side never allocates, locks, waits or calls the C library's I/O. State crosses threads through atomics and the published buffers [realtime.md](docs/internals/realtime.md) describes.
- **Core and backends.** Backends implement `src/backend.h`, report devices only through `src/device.h`, and never move streams: `src/follow.c` does. Platform callbacks only raise flags or queue records; the device table changes on the drain, on the host's thread.
- **Linux libraries are opened at run time** into tables (`*_api.c`); never call libpipewire, libpulse or libasound by name.
- **Web JavaScript** exists once, as `EM_JS` functions in the backend's C files.
- **Determinism.** Simulation results do not depend on the task split: rays in fixed order, batches of 64, sums in batch order, randomness hashed from indices. Code whose output is stored or compared across platforms calls `src/portable_math.h`, never `<math.h>`'s transcendental functions.
- **Library version:** `MAUD_VERSION_*` in `include/maul-audio/base.h`; CMake reads it there and `MAUD_ABI_VERSION` follows. Every def of an object made without a parent carries the version second after its cookie, stamped by an inline default; its creation checks the cookie, then the version.
- **Public headers** are the common subset of C17 and C++17 and declare only the API; every public function has a `///` block with `@param`, `@return` and a thread safety paragraph (`tools/check_docs.py`). No engine concept is named in them (`tools/check_litmus.py`).
- **Modules:** `tools/modules.txt` lists which module may include which. A new include needs a line there.
- **Assertions:** `MAUD_ASSERT` (`src/invariant.h`) checks internal invariants only, on in builds without `NDEBUG`, compiled out of Release, where audio-thread loops must not pay for it. Never assert on an argument, a platform report or a file's bytes: validate and return a status. Library code never calls `assert` or `abort`.
- **Limits:** every bound on memory or work is a field of a def or a named `MAUD_*` constant in a public header.
- **The README's cost table** is written from `bench/baseline.txt` by `tools/cost_table.py`; change the baseline, then run the tool.

## Testing

Follow the [repository standard's Tests section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#tests); in this repository:

- Test the Device part's contract on the offline backend, which scripts device changes; test a backend against what its platform reports, never against the backend's private state.
- A test that cannot reach its platform exits 77. With its `MAUD_REQUIRE_<BACKEND>` variable set it fails instead; CI sets them, so a broken environment never passes as a skip.
- Spatial results are compared against the golden renders in `data/golden/` within -50 dB per block, and bakes against `data/bake/office.maudbake` byte for byte. Keep golden scenes few.
- Every C snippet of `docs/user/` appears unchanged in `test/test_guide.c`; `tools/check_guide.py` enforces it.
- A fuzz finding becomes a test in its module's suite with the minimized input.

## Documentation

Follow the [repository standard's Documentation section](https://github.com/Rawframe-Project/.github/blob/main/docs/repository-standard.md#documentation). A change in what a caller must do or can rely on updates its page of `docs/user/`; a change in how a backend or effect works, or why, updates its `docs/internals/` page; a change to a file format updates `docs/internals/file-formats.md` and the format's version. The API reference is generated from the headers into the documentation site; never commit it.

## Pull requests

Titles are conventional commits. Scopes: `context`, `device`, `stream`, `offline`, `voice`, `pipewire`, `pulse`, `alsa`, `wasapi`, `coreaudio`, `ios`, `aaudio`, `web`, `hrtf`, `binaural`, `ambisonics`, `speakers`, `direct`, `reverb`, `scene`, `spatializer`, `bake`, `build`, `bench`, `family`. Example: `fix(pulse): keep a pinned stream across a server restart`.

A pull request that changes results (a golden render, a bake, a stream's timing) says what changed and who hears it.
