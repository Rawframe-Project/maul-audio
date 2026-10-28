# Test environments

Every backend is tested against its platform where a machine can host one, and the Device part's core is tested on the offline backend everywhere. This page says what each environment is, how to run it, and what it cannot show; the [device checklist](device-checklist.md) covers what only real hardware shows.

## Contents

- [Switches the tests read](#switches-the-tests-read)
- [Linux: a private sound server](#linux-a-private-sound-server)
- [Windows and Wine](#windows-and-wine)
- [macOS](#macos)
- [iOS](#ios)
- [Android](#android)
- [The web](#the-web)

## Switches the tests read

A backend suite exits 77 (skipped) when its platform is missing. Where a run must not pass with nothing tested, a `MAUD_REQUIRE_<BACKEND>` variable turns that skip into a failure: `MAUD_REQUIRE_PIPEWIRE`, `MAUD_REQUIRE_PULSE`, `MAUD_REQUIRE_ALSA`, `MAUD_REQUIRE_WASAPI`, `MAUD_REQUIRE_COREAUDIO`, `MAUD_REQUIRE_AAUDIO`, and `MAUD_REQUIRE_CAPTURE` for capture on AAudio. The sound-server fixture and the macOS and Android CI cells set them. On Linux the restart tests stop and start the sound-server fixture's daemon through its script, whose path CMake compiles in ([test_daemon.h](../../test/test_daemon.h)), so no test runs a command from its environment. On macOS the Core Audio restart takes its command from `MAUD_TEST_COREAUDIO_RESTART`. Without these, the restart tests test less and say so.

## Linux: a private sound server

The PipeWire, PulseAudio and ALSA suites, the guide and stall suites that open the native backend, and the browser pages run against a PipeWire daemon of their own, with its PulseAudio server and WirePlumber, one null sink and one null source and no real device, all under `out/<preset>/sound-server` and on a session bus of their own ([tools/pipewire_daemon.sh](../../tools/pipewire_daemon.sh)). CTest starts it as the `sound-server` fixture before the first suite that needs it and stops it after the last; the restart suites stop and start it themselves through the same script. Every other test gets an empty runtime directory of its own (`sound-server/none`), so no test ever reaches the session's own sound server or touches the daemon's directory while the daemon starts. The script makes `run/pulse` before it starts pipewire-pulse, which checks for that directory and then makes it, and fails if a PulseAudio client (such as the script's own `pactl` probe) made it in between.

```sh
cmake --preset linux-clang-20-asan && cmake --build --preset linux-clang-20-asan
ctest --preset linux-clang-20-asan -L integration
```

Where the daemon's programs (`pipewire`, `pipewire-pulse`, `wireplumber`, `pw-cli`, `pw-metadata`, `pactl`, `dbus-daemon`) are installed, the fixture runs and the suites must reach it (`MAUD_REQUIRE_PIPEWIRE` and `MAUD_REQUIRE_PULSE`); where they are not, the environment is absent and the suites skip. ALSA's default device reaches the server through ALSA's PulseAudio plugin, so the ALSA suite requires the server only where the plugin's configuration is installed, and always in CI, whose image has the plugin.

The script waits for each daemon to answer, at most 20 seconds a step (a loaded machine takes under two), and fails with the reason and the daemons' logs, at once when a daemon has exited. Its private configuration leaves out PipeWire's realtime module and JACK detection, which a container has no use for. A start that finds the daemon running (CTest runs the fixture's setup again when it repeats tests) reuses it as it is and makes no test nodes, and every start fails unless there is exactly one test sink and one test source: two devices of one name would leave the default naming the other one. It records each process it starts with its start time and signals a recorded PID only while both still match: a record can outlive its process, and by then the PID may be another program's. The null source's description is deliberately longer than a device name may be, with multi-byte characters at the cut, so name truncation is tested.

The timing checks (each stream's rate, a duplex stream's slips) measure in windows of a second or two, by the stream clock's own host times rather than the wall clock. They judge only clean windows: those in which no node of the private graph counted an xrun (`pipewire_daemon.sh xruns`, from `pw-top`), the stream counted none, and the test thread's 10 ms sleeps never woke more than 10 ms late. A rate check also sets aside, as settling, a window whose clock latency moved by more than 0.5 % of its span: the clock's host times carry its latency, and for a second or two after a stall the stream's queue drains or fills again. A machine short of CPU really plays fewer frames, and its server may fill a starved client's gaps with silence it never counts, so such a window measures the machine. A measure that finds fewer than 3 clean windows in 30 windows, or in 45 seconds of them, judges nothing: it prints `not enough clean windows: k of n had xruns or a late wakeup ...` and its windows. A suite with no failure then ends with a line starting `inconclusive:`, which CTest reports as skipped and CI fails as a test that did not run. The line says which case it is: measures that saw stalls were starved by the machine, and measures that saw none found only windows whose latency kept moving, which a rate bug can also cause. A real failure in the same suite still fails it. `pipewire_starved` runs an output stream's rate check with every measure starved (`MAUD_TEST_STARVE_RATE`) and passes only if the suite ends inconclusive as starved. At normal load every window is clean. A suite inconclusive with stalls in CI means the runner is short of capacity; the bounds are not to be loosened for it ([test_clock.h](../../test/test_clock.h), [test_daemon.h](../../test/test_daemon.h)).

ALSA's suites run through ALSA's PulseAudio plugin, served by `pipewire-pulse`, which plays a stall's gap with no `-EPIPE` and has no cards: the suites see the underrun the stream counts from its empty queue, or from one still reported holding frames once they could all have played, and ALSA's `-EPIPE` recovery and card scan are tested by hand (step 7 of the checklist). `test_alsa_hotplug` drives the card watch with a test directory that gains and loses nodes.

## Windows and Wine

Windows' hosted runners have no audio endpoint, so the WASAPI suites cannot run there; the Windows CI cell builds everything and runs the rest. In development the library is cross-built with Clang for `x86_64-w64-mingw32` and the WASAPI suites run under Wine against the same private PipeWire server, through Wine's PulseAudio driver:

- Wine 11 or later is needed for loopback capture; older Wine has no WASAPI loopback.
- Give Wine a prefix of its own (`WINEPREFIX`) and hold one `wineserver -p` for the whole run: otherwise a wineserver exiting after one test races the next test's start, which shows as `recvmsg: Connection reset by peer`. Stop it when the run ends.
- `MAUD_WASAPI_STA=1` runs `test_wasapi` again with the host's thread in a single-threaded apartment.

Wine does not enforce COM apartments, so a COM call on the wrong thread passes under Wine; step 6 of the checklist covers it on Windows.

## macOS

macOS runners have no audio hardware either. The Core Audio suite plays into BlackHole, a virtual device that `tools/ci_blackhole.sh` installs in CI, and makes an aggregate device over it to test hotplug and defaults. It needs the runners' null device too, as the other output the default moves to; without both it is skipped. It changes the system's default output, and opens inputs other than BlackHole's (on a developer's Mac, its real microphones), only where `MAUD_REQUIRE_COREAUDIO` is set, as in CI: it makes BlackHole the default, moves the default to the null device and back, and restores the default the machine had when it ends. A run that does not finish (a crash, a timeout, an interrupt) leaves BlackHole the default; System Settings > Sound > Output restores it. Without the variable, the suite leaves the default alone, plays on BlackHole by its key, opens no other input, and leaves out the default's checks, its moves, the service restart and the duplexes on two devices, printing a line that names them. Any capture, BlackHole's included, makes macOS ask once for microphone access for the program that runs the suite. In CI, `MAUD_TEST_COREAUDIO_RESTART="sudo killall coreaudiod"` lets the suite restart the audio server under a running stream; never set it on a machine someone uses. After such a restart on BlackHole, a lone stream runs several times fast until another client starts the device; on real hardware, step 4 of the checklist checks the rate by ear.

## iOS

`cmake/ios-simulator.cmake` builds for the simulator. Headless suites run through `simctl spawn` on the booted device or the one `MAUD_IOS_DEVICE` names; suites that need an application run through [tools/run_ios_app.sh](../../tools/run_ios_app.sh), which installs the bundle, grants the microphone and launches it within five minutes, and then waits at most two minutes for its result line. The two bounds are apart, so a simulator slow to take the bundle fails as that and never eats into the application's time. The simulator's microphone and speaker are the host's default devices. A runner's own virtual device can stall capture, so `test/ios/default_device.c` sets both defaults to BlackHole before the suites run.

## Android

`cmake/android-emulator.cmake` builds for x86_64 against Android 11 (API 30), the oldest the backend takes. Executables run in the emulator through [tools/run_android.sh](../../tools/run_android.sh), which pushes each one and runs it with the environment's `MAUD_` variables; `RUN_ANDROID_LIBS` pushes a sanitizer's runtime beside it. The Java suite is an application built without Gradle by [tools/build_android_app.sh](../../tools/build_android_app.sh): javac, then R8 with `java/proguard-rules.pro` as a release build shrinks, so a member the native library reaches by name without a keep rule fails. The emulator plays without a host audio connection and refuses the shell's recording, so capture is tested up to its refusal. Boot the emulator with `-no-snapshot`, set `ANDROID_HOME`, `ANDROID_NDK_HOME` and `ANDROID_SERIAL`, and stop it when the run ends.

## The web

An Emscripten build runs the library's suites under Node, the backend's JavaScript against stand-ins with a simulated clock ([test_web_transport.c](../../test/test_web_transport.c), [test_web_capture.c](../../test/test_web_capture.c)). The pages in `test/web/` are CTest suites too (`page_*`), run in headless Chrome through puppeteer ([run_page.js](../../test/web/run_page.js)): it serves the build with or without the cross-origin isolation headers, clicks once the page waits for a gesture, refuses the microphone with `deny`, and with `loopback` picks the page's own tab for display capture. `test/web/package-lock.json` pins puppeteer; `MAUD_PUPPETEER` names its module, and without it the pages skip.

On a Linux host the pages need the [private sound server](#linux-a-private-sound-server): Chrome plays into its null sink, a device that is always present, so a page never meets a missing or changing output. Without the daemon's programs the pages skip. A device error Chrome reports (`[AudioContext] Fallback to the default device ...`, `The AudioContext encountered an error from the audio device ...`) fails the page, with Chrome's message as the reason. Each page runs alone (CTest's `RUN_SERIAL`): it measures timing and xruns, which other suites running beside it would disturb. In CI the web job runs in the organization's image for this.

```sh
mkdir -p out/browser && cp test/web/package*.json out/browser/ && npm ci --prefix out/browser
cmake --preset web-emscripten && cmake --build --preset web-emscripten
MAUD_PUPPETEER=$PWD/out/browser/node_modules/puppeteer ctest --preset web-emscripten
```

Three pages are quarantined: the clock and loopback pages, which measure timing and capture in a real browser (#3, #4), and `page_web_plain`, whose xrun counts follow how busy the main thread is through the web clock's lost-frame rule (#5). They carry the `quarantine` label and run at night; each leaves quarantine after 14 nightly runs in a row pass against the null sink.
