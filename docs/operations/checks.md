# Checks

What runs before a push, on a pull request, on `main` and every night, and the budgets each must keep.

## Local tiers

`tools/check.sh` runs the Linux checks in three tiers, each including the one before. Every build is a CI cell's CMake preset (`CMakePresets.json`), in `out/<preset>`, so a local run and CI run the same commands. The organization's CI image has every tool the tiers need; run the `pr` and `full` tiers through the workspace's heavy-job wrapper on the shared machine.

| Tier | What it runs | Budget |
|---|---|---|
| `changed` | The source, module, length, documentation, snippet, API and test hygiene rules, the cost table, the organization's documentation check, the family sync check, `clang-format` on the changed files, and in `linux-clang-20-asan` the suites the changed files reach, an integration suite only when a change reaches it by name, against the sound-server fixture. | Under 2 minutes |
| `pr` | Every rule, `clang-format` on every file, `clang-tidy` over the Linux build's sources, the `linux-gcc-14` and `linux-clang-20-asan` presets with the sound-server suites, and 10 seconds of fuzzing per target. | Under 10 minutes |
| `full` | Also the TSan, shared, Device-only, Spatial-only, private-backend and Clang Release presets, and a minute of fuzzing per target. | Under 20 minutes |

The test hygiene check (`tools/check_test_hygiene.py`, from the family) runs its own fixtures, then scans the tests for a sleep, a wall-clock read, a retry or a skip that does not say why it stays (`// hygiene: <reason>`, or `#` in CMake), and after each preset's configure checks that every test in the build carries a label. The platform suites wait and skip only through `test/test_wait.h`, whose sleep and skip are the marked ones. CI's `checks` job runs the same, on the `linux-gcc-14` configure.

The changed tier maps each changed path to the suites of its area (`labels_for` in the script); a change to the base, the build or the test harness runs every unit suite. An integration suite runs only when a change reaches it by name: its own test file, `test/test_daemon.h` or `tools/pipewire_daemon.sh`. It runs then even beside a change that reaches every unit suite. Each suite's CTest label is its name.

## Labels

- `integration`: a suite that drives a real platform (a sound server, the platform's audio service, a simulator, an emulator, a browser). It may read the clock and use localhost, and runs in the changed tier only when a change reaches it by name.
- `quarantine`: a check that failed intermittently. It runs only in the nightly run, with an issue, and leaves quarantine after 14 nightly runs in a row pass. Today: the browser clock and loopback pages, the main page on a plain page (`page_web_plain`), and `pipewire_move`, the PipeWire loopback's move to a silent sink (`test_pipewire move`, #50), whose run of `pipewire` leaves that one check out.
- `package`: the installed package consumed by `samples/minimal` through CMake and pkg-config (`MAUL_AUDIO_TEST_PACKAGE`, [PackageTest.cmake](../../cmake/PackageTest.cmake)).

On Linux the `sound-server` fixture starts a private PipeWire daemon for the suites that need one and stops it after them ([Test environments](test-environments.md#linux-a-private-sound-server)). Every other test runs with an empty runtime directory of the build's, so no test reaches the session's own sound server.

## The pull request check

`.github/workflows/ci.yml`. Every job runs on every pull request unless the `changes` job finds only documentation, which skips the builds. The `required` job is the only one the ruleset requires; it passes when every other job passed or was skipped.

| Job | What it checks |
|---|---|
| `docs`, `zizmor` | The organization's documentation check, run from the pinned commit of `.github`, so the pin fixes the rules; the workflows' security audit. |
| `checks` | `clang-format`, the family's rules, the engine-free API (`check_litmus.py`), the README's cost table, the documentation site, `clang-tidy` over the Linux build's sources. |
| `build` | One shared-workflow cell per preset: `linux-gcc-14` (both parts, every Linux backend, the package test), `linux-clang-20`, `linux-clang-20-asan` (each test up to three times on a pull request, so a flaky one fails before it merges), `linux-clang-20-tsan`, `linux-shared` (the package test), `linux-device-only`, `linux-spatial-only`, `linux-private-backend`, `macos-clang` (into BlackHole, the package test), `windows-clang-cl` (the package test; the WASAPI suites need an endpoint the runners lack, so they run under Wine in development). |
| `msvc-headers` | The public headers alone under MSVC's `cl`, as C17 and C++17 with `/W4 /WX`. |
| `fuzz` | Each loader under libFuzzer, one cell per target, for 20 seconds from its seeds and its cached corpus. |
| `web-emscripten` | The suites under Node, the browser pages in headless Chrome, and what each feature costs a page. |
| `android-emulator` | The AAudio suites and the Java half in the Android 11 emulator. |
| `ios-simulator` | The iOS build with its tests; on `main` and at night the tests run in a booted simulator. |

Every cell that runs CTest fails when a test is skipped or not run, and uploads its logs when it fails, with the sound-server daemons' logs. A step that feeds a file list to a tool fails on an empty list.

## Main and the night

On `main`, fuzzing runs 90 seconds per target and each target's corpus, minimized with libFuzzer's merge, is saved in an Actions cache of its own for the next run; the iOS suites run in the simulator. At night, or by hand with `gh workflow run ci.yml --ref main -f nightly=true`, the extended run adds the `extended` cells (`linux-clang-20-msan` without the integration suites and `context`, which load the system's sound libraries that MemorySanitizer cannot see into, and with 20 minutes a suite, since the bake suite takes several under it; `linux-gcc-14` on arm64; `linux-quarantine`, the quarantined Linux suites against the sound-server fixture; `macos-clang-asan`; `macos-clang-tsan`), `ios-simulator-asan`, the quarantined pages, and ten minutes of fuzzing per target. The extended run may take up to two hours and is not a required check. A failure on `main` or at night is reported to the manager and fixed before other work.

`.github/workflows/codeql.yml` builds both parts with GCC 14 in the CI image for CodeQL on every pull request and on `main`; `pages.yml` publishes the documentation site from `main`; `release.yml` makes the releases ([Release](release.md)); `scorecard.yml` grades the repository's security practices (OpenSSF Scorecard) on `main` and every week. Each scheduled run has the repository's own slot from the organization's schedule (`.github` `docs/ci-schedule.md`), so the organization's runs do not start together.
