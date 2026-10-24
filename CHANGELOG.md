# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every file format.

## [0.0.1](https://github.com/Rawframe-Project/maul-audio/compare/v0.0.1...v0.0.1) (2026-10-24)


### ⚠ BREAKING CHANGES

* **bake:** a version-1 .maudbake file (one without reverb tails) no longer loads: maudLoadProbeSet returns maud_errorUnsupported. Bake the scene again with this release and save it.

### Fixed

* **bake:** refuse version-1 bakes ([#7](https://github.com/Rawframe-Project/maul-audio/issues/7)) ([afd5f16](https://github.com/Rawframe-Project/maul-audio/commit/afd5f1679b47e42bff05670cdaf13e65b40ef1d5))


### CI

* release with release-please ([#9](https://github.com/Rawframe-Project/maul-audio/issues/9)) ([64e1543](https://github.com/Rawframe-Project/maul-audio/commit/64e1543b3eb3f0413d02cc2a39f2254ea0017f68))

## 0.0.1

The first release on the organization's standard. Version numbers
restarted at 0.0.1, and the earlier tags and releases were removed.

### Added

- Playback, capture, duplex and loopback streams of 32-bit float
  frames, in fixed periods, on PipeWire, PulseAudio and ALSA (each
  opened at run time), WASAPI, Core Audio on macOS, iOS, AAudio on
  Android 11 and later, and the web through an AudioWorklet, with a
  private backend slot for platforms whose APIs cannot be public.
- Device lists with stable keys, hotplug, the default device per role,
  and streams that follow the default or stay on a named device; every
  change, move and suspension reported as a typed notification, and
  streams kept across a restart of the sound server.
- Rates that never change silently: the device's own, one the device
  must run at, or one the platform converts. Exclusive streams where
  the platform has them, object streams for Windows' and Apple's
  spatial renderers, and a stream clock that maps frames to the host
  clock with the latency, for synchronization.
- The platform's voice processing on capture streams, reported as the
  platform applies it, and the library's own voice activity detector,
  automatic gain control, noise suppressor and echo canceller, which
  run on any frames without a device.
- An offline backend that renders and captures at the caller's clock
  and scripts device changes, for tests and rendering to memory.
- Binaural rendering through HRTF sets loaded from bytes, with the
  head's near field, and a default set made from the SADIE II KU100
  measurements.
- A first- to third-order ambisonic bed with encoding, rotation and
  binaural and speaker decoders, and vector base amplitude panning to
  speaker layouts.
- The direct effect (air absorption, directivity, occlusion and
  transmission in three bands) and a parametric reverb with a second
  slope for coupled spaces.
- The spatializer: occlusion and transmission traced through the
  host's geometry or the library's own scenes, reverberation estimates,
  geometric reflections, paths around corners over probe sets, and
  probe sets baked to the same bytes on every platform and saved to
  files.
- Build options to leave out either part, a shared library option, a
  CMake package and a pkg-config module.
