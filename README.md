# Maul Audio

Audio devices and spatial sound in C, for games, tools and voice applications, with no dependencies beyond each platform's own audio API.

[![CI](https://github.com/Rawframe-Project/maul-audio/actions/workflows/ci.yml/badge.svg)](https://github.com/Rawframe-Project/maul-audio/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/github/license/Rawframe-Project/maul-audio)](LICENSE)
[![Release](https://img.shields.io/github/v/release/Rawframe-Project/maul-audio)](https://github.com/Rawframe-Project/maul-audio/releases)

A program that makes sound has to find the output, play to it without dropouts, follow the user when they plug in headphones, and survive the sound server restarting. A game also wants its sources placed around the listener, muffled behind walls, and ringing in the room they are in. Maul Audio does both, in two parts a program can use apart:

- **Device**: playback, capture, duplex and loopback streams on Linux (PipeWire, PulseAudio, ALSA), Windows, macOS, iOS, Android and the web; device lists and hotplug, following the default device, typed device loss, a stream clock for synchronization, the platform's voice processing and the library's own (voice detection, gain control, noise suppression, echo cancellation), and an offline backend for tests and rendering to memory.
- **Spatial**: binaural rendering through HRTF sets, a third-order ambisonic bed, speaker panning and decoding, air absorption, directivity, occlusion and transmission, a parametric reverb, geometric reflections, paths around corners, and probe sets baked offline to the same bytes on every platform.

It owns no mixer, voices or decoders: the program mixes, and the library moves its frames to and from the device or renders the sources it is given. Nothing on the audio thread allocates, locks or waits.

| Work | Time | One core in real time |
|---|---|---|
| A binaural source, moving | 12.2 us | about 820 sources |
| A binaural source, still (with the near field) | 6.5 (9.2) us | about 1,500 (1,100) sources |
| A source's direct effects (air absorption, a wall's transmission), moving | 3.6 us | about 2,800 sources |
| A source into the third-order bed | 1.3 us | about 7,700 sources |
| The bed decoded binaurally | 72 us | once per listener |
| A reverb | 53 us | about 190 reverbs |
| Noise suppression, mono (16 kHz, 48 kHz) | 20, 41 us | |
| Echo cancellation, mono (16 kHz, 48 kHz) | 27, 76 us | |

Per 10 ms of 48 kHz audio on one core of an AMD Ryzen 9 5900X (Linux, Clang 20, Release).

## Quick start

```sh
git clone https://github.com/Rawframe-Project/maul-audio.git
cd maul-audio
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sample_devices 10
./build/sample_binaural data/hrtf/sadie2-ku100-48k.maudhrtf circle.wav
```

`sample_devices` lists the devices and plays a tone for ten seconds; `sample_binaural` renders a source circling the listener to a WAV file. Install with `cmake --install build --prefix <prefix>` and use it from CMake:

```cmake
find_package(maul-audio REQUIRED CONFIG)
target_link_libraries(app PRIVATE maul-audio::maul-audio)
```

or through pkg-config as `maul-audio`. [Getting started](docs/user/getting-started.md) covers the build options and the model the API follows.

## Platforms

Linux, Windows 10 and later, macOS, iOS, Android 11 and later, and browsers through Emscripten, with GCC 14 or Clang 19 and newer (`clang-cl` on Windows). The public headers are C17 and C++17, so any C or C++ program can include them. CI builds and tests every platform; [Platforms](docs/user/platforms.md) lists what each one needs.

## Documentation

- [The documentation site](https://rawframe-project.github.io/maul-audio/), with the API reference generated from the headers.
- [User documentation](docs/README.md): one page per part of the library.
- [How it works](docs/internals/overview.md) and [how to develop it](docs/operations/development.md).
- [Changelog](CHANGELOG.md).

Maul Audio belongs to the Maul family of standalone C libraries, which share one set of conventions.

## Contributing

Issues are welcome. Pull requests are accepted for issues a maintainer has approved; see [CONTRIBUTING.md](https://github.com/Rawframe-Project/.github/blob/main/CONTRIBUTING.md). The project is developed with AI coding agents, directed and reviewed by its maintainers; outside contributions follow the [AI policy](https://github.com/Rawframe-Project/.github/blob/main/AI_POLICY.md).

## License

MIT; see [LICENSE](LICENSE). The default HRTF set in `data/hrtf/` is made from the SADIE II database (University of York) under the Apache License 2.0; `data/hrtf/sadie2-ku100.notice.txt` carries its notice. `tools/sofa_to_maudhrtf.py` and `tools/near_field_table.py` use numpy and h5py (BSD 3-Clause); nothing outside the platform's own libraries is linked into the library.
