# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every file format.

## [0.1.0](https://github.com/Rawframe-Project/maul-audio/compare/v0.0.4...v0.1.0) (2026-10-27)


### ⚠ BREAKING CHANGES

* **bake:** maudLoadProbeSet reads only version 3 .maudbake files and refuses earlier ones, and a later version or an unknown field, with maud_errorVersion instead of maud_errorUnsupported. Bake the scene again.
* **hrtf:** maudLoadHrtf reads only version 3 .maudhrtf files and refuses earlier ones, and a later version or an unknown field, with maud_errorVersion. Convert a SOFA source again with tools/sofa_to_maudhrtf.py.

### Added

* **bake:** make bakes tagged fields ([#88](https://github.com/Rawframe-Project/maul-audio/issues/88)) ([0ed6aa7](https://github.com/Rawframe-Project/maul-audio/commit/0ed6aa7edc4ea46ea3574933c1d0e7910f72b25a))
* **hrtf:** make HRTF sets tagged fields ([#87](https://github.com/Rawframe-Project/maul-audio/issues/87)) ([9f725cc](https://github.com/Rawframe-Project/maul-audio/commit/9f725cc4c30cd54d65586700522577f1b9822511))


### Fixed

* **build:** escape backslashes and quotes in javac's argument file ([#84](https://github.com/Rawframe-Project/maul-audio/issues/84)) ([ba251a6](https://github.com/Rawframe-Project/maul-audio/commit/ba251a6a64ddc05a367145fdba2702ae93f3685a))
* close the CodeQL and pinned-dependency alerts ([#79](https://github.com/Rawframe-Project/maul-audio/issues/79)) ([e96bc6e](https://github.com/Rawframe-Project/maul-audio/commit/e96bc6e97d67e5bc2cd84f1b27492b6a6e0e3143))
* **hrtf:** size the resampled filters through the checked layout ([#99](https://github.com/Rawframe-Project/maul-audio/issues/99)) ([074946f](https://github.com/Rawframe-Project/maul-audio/commit/074946f31a09041a94720645fbd1b121b1462a61)), closes [#98](https://github.com/Rawframe-Project/maul-audio/issues/98)
* **pipewire:** read a capture chunk the way PipeWire's own clients do ([#97](https://github.com/Rawframe-Project/maul-audio/issues/97)) ([c2e8f46](https://github.com/Rawframe-Project/maul-audio/commit/c2e8f46031170ee4a29e6c9f4f011e466eebd763)), closes [#96](https://github.com/Rawframe-Project/maul-audio/issues/96)
* **pipewire:** rebind the default metadata when it is replaced ([#83](https://github.com/Rawframe-Project/maul-audio/issues/83)) ([c5709e5](https://github.com/Rawframe-Project/maul-audio/commit/c5709e5fffb39e579578bce12bd09d2a8494e502)), closes [#80](https://github.com/Rawframe-Project/maul-audio/issues/80)
* **stream:** drop a reconnected input's gathered frames on every backend ([#92](https://github.com/Rawframe-Project/maul-audio/issues/92)) ([7f39fa3](https://github.com/Rawframe-Project/maul-audio/commit/7f39fa3be5e48a7ac9f343be7dc6e3873410b12f)), closes [#76](https://github.com/Rawframe-Project/maul-audio/issues/76)
* **stream:** keep a moved input's first block free of the old sound ([#75](https://github.com/Rawframe-Project/maul-audio/issues/75)) ([b84ad28](https://github.com/Rawframe-Project/maul-audio/commit/b84ad2833373e42ce38632833973575b1a89c40c)), closes [#50](https://github.com/Rawframe-Project/maul-audio/issues/50)
* **voice:** parenthesize the silence floor's macro ([#66](https://github.com/Rawframe-Project/maul-audio/issues/66)) ([d46e4e4](https://github.com/Rawframe-Project/maul-audio/commit/d46e4e403f44613adb673daa6892b36eb9e65bda))

## [0.0.4](https://github.com/Rawframe-Project/maul-audio/releases/tag/v0.0.4) (2026-10-25)

### Changed

- On 32-bit x86 the library is built with SSE2 math
  (`-msse2 -mfpmath=sse`), so it needs a processor with SSE2, and every
  build now leaves trapping math at the compiler's default, from the
  family's compiler flags. On 64-bit builds the bake still matches byte
  for byte and every test passes unchanged
  ([#57](https://github.com/Rawframe-Project/maul-audio/pull/57)).

### Fixed

- An ALSA output through ALSA's PulseAudio plugin, the default PCM
  wherever ALSA programs are routed to a PulseAudio server (a PipeWire
  system without `pipewire-alsa` included), counts the gaps it plays.
  The plugin plays silence for a starved stream without `-EPIPE`, so
  such gaps used to leave `underruns` unchanged. A running output now
  counts one underrun when it finds its PCM's queue empty before a
  write, once the PCM has had the stream's frames, and when a write
  comes more than a period after every frame written so far could have
  played out, once the write before it found frames queued; the second
  catches a stall that `pipewire-pulse` reports with frames still
  queued. Each gap counts once. The first fill after a start or a
  recovery, silence written while the stream does not run, and an empty
  queue reported before the frames could have played count nothing, and
  neither does a sink that starts late. A sink that stops taking frames
  mid-stream and resumes later also counts an underrun, since the server
  reports it as it reports a starved stream. Hardware PCMs are
  unchanged: their gaps still come as `-EPIPE`
  ([#35](https://github.com/Rawframe-Project/maul-audio/pull/35),
  [#62](https://github.com/Rawframe-Project/maul-audio/pull/62)).
- On 32-bit x86 the library's floating-point setting also holds the x87
  control word, so a host that leaves x87 at single or double precision
  no longer changes what the library computes there, and `sqrt` compiles
  to one rounded instruction rather than a C library call that may set
  `errno`. The bake and the golden renders are unchanged
  ([#23](https://github.com/Rawframe-Project/maul-audio/pull/23)).

## [0.0.3](https://github.com/Rawframe-Project/maul-audio/releases/tag/v0.0.3) (2026-10-24)

### Changed

- The host's allocator and its task hooks' enqueue and finish
  functions now run in the host's own floating-point setting, as the
  rest of the family does; before, they ran in the library's. Bakes,
  paths and estimates are unchanged, and the ray queries still compute
  in the library's setting, whether a task body or the calling thread
  runs them.
  ([#17](https://github.com/Rawframe-Project/maul-audio/pull/17))

### Fixed

- A web stream on a busy page no longer reports overruns or underruns
  that did not happen, and its clock no longer resets for them. A
  delivery placed late once is not a loss; a real loss is counted once,
  up to five seconds later, and until then the stream's clock runs
  behind by the lost frames.
  ([#18](https://github.com/Rawframe-Project/maul-audio/pull/18))

## [0.0.2](https://github.com/Rawframe-Project/maul-audio/releases/tag/v0.0.2) (2026-10-24)

### Fixed

- Bakes, probe sets, paths, reverberation estimates and scene ray
  queries now give the same bits whatever floating-point mode the
  host's threads run in. The simulation calls and the task bodies they
  hand to the host switch to round to nearest with subnormals kept and
  every exception masked, and restore the thread's mode on return.
  Before, a host running flush-to-zero, denormals-are-zero or another
  rounding mode got different bakes, and threads in different modes
  gave different bits for different task splits. The rendering side
  never touches the mode. Results in the default mode do not change.
  ([#14](https://github.com/Rawframe-Project/maul-audio/pull/14))

## [0.0.1](https://github.com/Rawframe-Project/maul-audio/releases/tag/v0.0.1) (2026-10-24)

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

### Removed

- Reading version-1 `.maudbake` files, made before reverb tails:
  `maudLoadProbeSet` returns `maud_errorUnsupported`. Bake the scene
  again with this release and save it.
