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

A backend suite exits 77 (skipped) when its platform is missing. Where a run must not pass with nothing tested, a `MAUD_REQUIRE_<BACKEND>` variable turns that skip into a failure: `MAUD_REQUIRE_PIPEWIRE`, `MAUD_REQUIRE_PULSE`, `MAUD_REQUIRE_ALSA`, `MAUD_REQUIRE_WASAPI`, `MAUD_REQUIRE_COREAUDIO`, `MAUD_REQUIRE_AAUDIO`, and `MAUD_REQUIRE_CAPTURE` for capture on AAudio. Restart tests take the commands that stop and start the service in `MAUD_TEST_PIPEWIRE_STOP`, `MAUD_TEST_PIPEWIRE_START` and `MAUD_TEST_COREAUDIO_RESTART`; without them they test less and say so.

## Linux: a private sound server

The PipeWire and PulseAudio suites run against a PipeWire daemon of their own, with its PulseAudio server and WirePlumber, one null sink and one null source, and no real device, all under one directory and on a session bus of their own ([tools/pipewire_daemon.sh](../../tools/pipewire_daemon.sh)). It never touches the developer's own sound server.

```sh
tools/pipewire_daemon.sh start "$PWD/out/pw"
export XDG_RUNTIME_DIR=$PWD/out/pw/run MAUD_REQUIRE_PIPEWIRE=1 MAUD_REQUIRE_PULSE=1
export MAUD_TEST_PIPEWIRE_STOP="$PWD/tools/pipewire_daemon.sh stop $PWD/out/pw"
export MAUD_TEST_PIPEWIRE_START="$PWD/tools/pipewire_daemon.sh start $PWD/out/pw"
ctest --test-dir out/dev -R '^(pipewire|pipewire_restart|pulse|alsa)$'
tools/pipewire_daemon.sh stop "$PWD/out/pw"
```

The script records the PIDs it started and stops only those. It needs `pipewire`, `pipewire-pulse`, `wireplumber`, `pipewire-bin`, `pulseaudio-utils` (the PulseAudio tests load sinks and move streams with `pactl`) and `dbus-daemon`. The source's description is deliberately longer than a device name may be, with multi-byte characters at the cut, so name truncation is tested.

ALSA's suites run through PipeWire's ALSA plugin, which absorbs a stall without an xrun and has no cards, so ALSA's recovery and card scan are tested by hand (step 7 of the checklist). `test_alsa_hotplug` drives the card watch with a test directory that gains and loses nodes.

## Windows and Wine

Windows' hosted runners have no audio endpoint, so the WASAPI suites cannot run there; the Windows CI cell builds everything and runs the rest. In development the library is cross-built with Clang for `x86_64-w64-mingw32` and the WASAPI suites run under Wine against the same private PipeWire server, through Wine's PulseAudio driver:

- Wine 11 or later is needed for loopback capture; older Wine has no WASAPI loopback.
- Give Wine a prefix of its own (`WINEPREFIX`) and hold one `wineserver -p` for the whole run: otherwise a wineserver exiting after one test races the next test's start, which shows as `recvmsg: Connection reset by peer`. Stop it when the run ends.
- `MAUD_WASAPI_STA=1` runs `test_wasapi` again with the host's thread in a single-threaded apartment.

Wine does not enforce COM apartments, so a COM call on the wrong thread passes under Wine; step 6 of the checklist covers it on Windows.

## macOS

macOS runners have no audio hardware either. The Core Audio suite plays into BlackHole, a virtual device (`brew install --cask blackhole-2ch`), and makes an aggregate device over it to test hotplug and defaults. `MAUD_TEST_COREAUDIO_RESTART="sudo killall coreaudiod"` restarts the audio server under a running stream. After such a restart on BlackHole, a lone stream runs several times fast until another client starts the device; on real hardware, step 4 of the checklist checks the rate by ear.

## iOS

`cmake/ios-simulator.cmake` builds for the simulator. Headless suites run through `simctl spawn` on the booted device or the one `MAUD_IOS_DEVICE` names; suites that need an application run through [tools/run_ios_app.sh](../../tools/run_ios_app.sh), which grants the microphone, installs and launches the bundle, and waits at most two minutes for its result line. The simulator's microphone and speaker are the host's default devices, which `test/ios/default_device.c` sets to BlackHole, since a runner's own virtual device stalled the capture tests now and then.

## Android

`cmake/android-emulator.cmake` builds for x86_64 against Android 11 (API 30), the oldest the backend takes. Executables run in the emulator through [tools/run_android.sh](../../tools/run_android.sh), which pushes each one and runs it with the environment's `MAUD_` variables; `RUN_ANDROID_LIBS` pushes a sanitizer's runtime beside it. The Java suite is an application built without Gradle by [tools/build_android_app.sh](../../tools/build_android_app.sh): javac, then R8 with `java/proguard-rules.pro` as a release build shrinks, so a member the native library reaches by name without a keep rule fails. The emulator plays without a host audio connection and refuses the shell's recording, so capture is tested up to its refusal. Boot the emulator with `-no-snapshot`, set `ANDROID_HOME`, `ANDROID_NDK_HOME` and `ANDROID_SERIAL`, and stop it when the run ends.

## The web

An Emscripten build runs the library's suites under Node, the backend's JavaScript against stand-ins with a simulated clock ([test_web_transport.c](../../test/test_web_transport.c), [test_web_capture.c](../../test/test_web_capture.c)). The pages in `test/web/` run in headless Chrome through puppeteer ([run_page.js](../../test/web/run_page.js)): it serves the build with or without the cross-origin isolation headers, clicks once the page waits for a gesture, refuses the microphone with `deny`, and with `loopback` picks the page's own tab for display capture. `MAUD_PUPPETEER` names the puppeteer module.

```sh
npm install --prefix out/web-tools puppeteer
MAUD_PUPPETEER=$PWD/out/web-tools/node_modules/puppeteer node test/web/run_page.js out/web page_web.html 1
```

The clock page measures timing in a real browser, so it is sensitive to a loaded machine.
