# Maul Audio

Audio device I/O and spatial audio for games, tools and voice
applications. Written in C23 with public headers any C17 or C++17
program can include, with no dependencies beyond each platform's own
audio APIs and an MIT license.

It has two parts, each usable without the other:

- **Device:** playback, capture and duplex streams on every platform,
  device enumeration and hotplug, default-device following, typed
  device loss, format and latency reporting, a stream clock, platform
  voice processing, and an offline backend for tests and rendering to
  memory.
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

Not released. The first decisions are made and the skeleton builds;
the device layer on desktop and the offline backend come first.

## Building

Requirements: CMake 3.25 and GCC 14 or Clang 19 or newer; on Windows,
`clang-cl` (the Visual Studio component "C++ Clang tools for Windows").

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

## Design

The rules every Maul library follows are in `docs/conventions.md` and
`docs/adr/`; the records particular to this library are listed in
`docs/adr/maud.md`.

## License

MIT; see `LICENSE`.
