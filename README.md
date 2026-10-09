# Maul Audio

Audio device I/O and spatial audio for games, tools and voice
applications. Written in C23 with public headers any C17 or C++17
program can include, with no dependencies beyond each platform's own
audio APIs and an MIT license.

It has two parts, each usable without the other:

- **Device:** playback, capture and duplex streams on every platform,
  device enumeration and hotplug, default-device following, typed
  device loss, format and latency reporting, a stream clock, platform
  voice processing and the library's own (voice activity, gain
  control, noise suppression, echo cancellation), and an offline
  backend for tests and rendering to memory.
- **Spatial:** HRTF binaural rendering, ambisonics, speaker panning,
  air absorption, directivity, occlusion and transmission, reverb,
  reflections and propagation around geometry, and deterministic
  offline baking.

It owns no mixer, voices, decoders or game concepts: a host mixes,
and the library moves its blocks to and from the device or renders
the sources it is given. Nothing on the audio thread allocates, locks
or waits, and the library starts no thread except a stream's audio
thread where the platform provides none and, on Windows, one thread
per context in COM's multithreaded apartment, on which every WASAPI
call runs, so that the host's threads may be in any apartment.

## Status

Version 0.1.2. The device layer works on Linux
(PipeWire, PulseAudio, ALSA), Windows (WASAPI), macOS (Core Audio),
iOS, Android (AAudio) and the web, with an offline backend for tests,
and a private backend (a console's) can join at configure time.
Everything listed above is built and tested in CI; WASAPI's device
tests, which need an endpoint the Windows runners lack, run under Wine
before each commit, and `docs/device-checklist.md` lists what is
checked by hand on real hardware. Before 1.0.0 any minor release may
change the API, the ABI and the data formats.

## Cost

On one core of an AMD Ryzen 9 5900X (Linux, clang 20, Release; the
figures in `bench/baseline.txt`), per 10 ms of 48 kHz audio:

| Work | Time | One core in real time |
|---|---|---|
| A binaural source, moving | 17.3 us | about 580 sources |
| A binaural source, still (with the near field) | 10.7 (13.2) us | about 940 (750) sources |
| A source's direct effects (air absorption, a wall's transmission), moving | 8.3 us | about 1,200 sources |
| A source into the third-order bed | 2.4 us | about 4,100 sources |
| The bed decoded binaurally | 123 us | once per listener |
| A reverb | 53 us | about 190 reverbs |
| Noise suppression, mono (16 kHz, 48 kHz) | 45, 93 us | |
| Echo cancellation, mono (16 kHz, 48 kHz) | 93, 287 us | |

On the web, each feature linked alone costs a page, gzipped: a
playback stream about 16 KB of WebAssembly and JavaScript, the
binaural effect 28 KB, the reverb 30 KB, the voice processors 32 KB;
the default HRTF set is a separate file of 854 KB, 636 KB gzipped,
which the host fetches and hands in. The CI's web cell reports these.

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows").

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

Each part has an option, both on by default: `-DMAUL_AUDIO_DEVICE=OFF`
builds the Spatial part alone, `-DMAUL_AUDIO_SPATIAL=OFF` the Device
part alone. Neither part's code uses the other's, so a host links only
what it uses.

Two samples show the parts in use: `sample_devices` lists the devices,
plays a tone on the default output and prints every notification;
`sample_binaural` renders a source circling the listener to a WAV file
with the shipped HRTF set, without a device.

Device churn a CI runner cannot simulate (a real headset plugged in,
the audio service restarted on Windows or macOS) is checked by hand
before a release, following `docs/device-checklist.md`.

## Guide

`docs/guide.md` walks through both parts: a context and its devices,
a stream, notifications, the offline backend and the voice processors;
then an HRTF set, the binaural effect, the ambisonic bed, direct
effects, the reverb and the spatializer. Its snippets are built and run
by `test/test_guide.c`.

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/maud.md`.

## License

MIT; see `LICENSE`.
