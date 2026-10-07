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
thread where the platform provides none.

## Status

Not released. The device layer works on Linux (PipeWire, PulseAudio,
ALSA), Windows (WASAPI), macOS (Core Audio) and the web, with an
offline backend for tests; the mobile backends come later. The spatial
part has HRTF sets, a binaural effect per source, a third-order
ambisonic bed with binaural and speaker decoders, and speaker panning
for the library's channel layouts; the rest of it comes later.

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

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/maud.md`.

## License

MIT; see `LICENSE`.
