# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every file format.

## [0.3.0](https://github.com/Rawframe-Project/maul-audio/compare/v0.2.0...v0.3.0) (2026-10-29)


### ⚠ BREAKING CHANGES

* 63 public functions and 6 types are renamed by the family's naming rule, maudDevice_SetOfflineDefault, maudProbeSet_GetInfo and maudRotateAmbisonic take their parameters in a new order, maudAcousticScene and its def, calls and cookie are maudScene's, maudInstanceTransform is maudSceneInstanceTransform, maudResultName is maudGetResultName, maudVoiceDetectorDef.hangoverMilliseconds (uint32_t, milliseconds) is hangover (float, seconds), seven count fields, four switch fields (now enableThing) and maudEchoCancellerDef.tailSeconds are renamed. maudStreamCallback was a pointer type and maudStreamFunction is a function type, so a variable or field that holds the callback is declared maudStreamFunction*. The tables in this pull request list each.
* **stream:** maudStreamObject.position is a maudVector3 instead of float[3]; write position.x, .y and .z instead of position[0], [1] and [2].
* **spatializer:** MAUD_DIRECT_BANDS is renamed MAUD_BANDS.
* maudDirectEffectDef.sampleRate, maudReverbDef.sampleRate and maudSpatializerDef.reflectionRate are uint32_t instead of float.
* **context:** maudLimits.streams, devices, notifications and deviceTextBytes are uint32_t instead of uint16_t, and maudCreateContext refuses one past its new MAUD_MAX_* constant with maud_errorLimit.

### Fixed

* **web:** link programs so a failed allocation is a status, not an abort ([#163](https://github.com/Rawframe-Project/maul-audio/issues/163)) ([9efd54c](https://github.com/Rawframe-Project/maul-audio/commit/9efd54c205bd258b3f9673fba8a8615e7ccfab2d)), closes [#158](https://github.com/Rawframe-Project/maul-audio/issues/158)


### Changed

* **context:** cap maudLimits by named constants, not field widths ([#153](https://github.com/Rawframe-Project/maul-audio/issues/153)) ([f670810](https://github.com/Rawframe-Project/maul-audio/commit/f6708101d39bf1decbeb504de864c455c2b2375d))
* name the API by the family's naming rule ([#165](https://github.com/Rawframe-Project/maul-audio/issues/165)) ([2be7d67](https://github.com/Rawframe-Project/maul-audio/commit/2be7d67e9393ef3f04e3348c6b5e2720671361ff))
* **spatializer:** name the band count MAUD_BANDS ([#154](https://github.com/Rawframe-Project/maul-audio/issues/154)) ([43979f3](https://github.com/Rawframe-Project/maul-audio/commit/43979f32867f9236fc6cea71772279d8e739b591)), closes [#145](https://github.com/Rawframe-Project/maul-audio/issues/145)
* **stream:** give a stream object's position as a maudVector3 ([#152](https://github.com/Rawframe-Project/maul-audio/issues/152)) ([82297e3](https://github.com/Rawframe-Project/maul-audio/commit/82297e34abd07c679bf628c0674e7f583920c8b9))
* take every sample rate as a uint32_t ([#151](https://github.com/Rawframe-Project/maul-audio/issues/151)) ([35e8df8](https://github.com/Rawframe-Project/maul-audio/commit/35e8df81c6759e62e8e833cdbe16f6d9a6a79324))

## [0.2.0](https://github.com/Rawframe-Project/maul-audio/compare/v0.1.0...v0.2.0) (2026-10-28)

### Changed

- **Breaking:** malformed `.maudhrtf` and `.maudbake` bytes are refused with the new `maud_errorFormat` (-8) instead of `maud_errorInvalid`, by `maudLoadHrtf` and `maudLoadProbeSet`. A count past a published `MAUD_MAX_*` constant, or below a `MAUD_MIN_*` one, is refused with the new `maud_errorLimit` (-9) instead of `maud_errorInvalid`. This covers the context's `limits.periodFrames` and `limits.notifications`, a stream's `objectCount`, an ambisonic `order`, the spatializer's, acoustic scene's and binaural renderer's capacities, and a probe box's columns, which was `maud_errorCapacity`. Most of these bounds were private and are now public constants beside their API, such as `MAUD_MAX_SOURCES`, `MAUD_MAX_TRIANGLES` and `MAUD_MAX_PROBE_COLUMNS` (16,777,216 columns; the documentation used to say 4,194,304). `maudProcessBinaural` and `maudDecodeBinaural` refuse more frames than the def's `maxFrames` with `maud_errorCapacity` instead of `maud_errorInvalid`. A program that tests for `maud_errorInvalid` after these calls tests for the new results ([#121](https://github.com/Rawframe-Project/maul-audio/pull/121)).
- **Breaking:** a call refused because the object is running host code (a stream's callback, an allocator, a spatializer's ray or task hook), or because another thread is rendering the stream, returns the new `maud_errorBusy` (-10) instead of `maud_errorState`. The same call succeeds once that work ends. `maud_errorState` now means only a state another call changes, such as a stopped stream ([#124](https://github.com/Rawframe-Project/maul-audio/pull/124)).
- **Breaking:** `maudSimulateReverb`, `maudRenderReflections`, `maudCreateProbeSet` (generating probes) and `maudBakeProbeSet` return `maud_errorUnsupported` (-4) instead of `maud_errorState` when the spatializer was created without the estimates, reflections, closest-hit query or rays the call needs. No later call can add those, so retrying is pointless ([#126](https://github.com/Rawframe-Project/maul-audio/pull/126)).
- **Breaking:** `maudCreateContext` refuses a `limits.periodFrames` past `MAUD_MAX_PERIOD_FRAMES` (1,048,576 frames, over 20 seconds at 48 kHz) with `maud_errorLimit`. A duplex stream created under a larger limit used to hang ([#112](https://github.com/Rawframe-Project/maul-audio/pull/112)).

### Fixed

- `maudCreateContext` refuses, with `maud_errorCapacity`, limits whose memory block the address space cannot hold. Where `size_t` has 32 bits, as on the web, the largest limits used to create a context over too small a block, and destroying it then crashed ([#108](https://github.com/Rawframe-Project/maul-audio/pull/108)).
- A device rate the platform reports outside 8,000 to 384,000 frames per second, or not a number, reads as unknown (0) instead of reaching the device list. A native stream on such a device keeps its own rate. This covers WASAPI's device format, Core Audio's nominal rate and rate ranges, and PipeWire's `clock.rate` and `clock.force-rate`. Core Audio's rate used to be converted without a range check ([#120](https://github.com/Rawframe-Project/maul-audio/pull/120)).
- Core Audio: a device's channel count reads only within the configuration the system returned, and counts none for a list claiming more buffers than its bytes hold ([#114](https://github.com/Rawframe-Project/maul-audio/pull/114)).
- Web: a capture stream's block times no longer read early. In Chrome the output latency the capture clock subtracts jitters by about 2 ms, and the clock kept every low of that jitter, so captured frames were placed up to a few milliseconds early, more the longer the stream ran. A capture's frame times now follow the median of its last five deliveries, and a lasting change reaches them by the third. This changes the times of web capture blocks: a program that lines up captured sound with what it plays, such as an echo canceller or a latency measure, sees them a few milliseconds later and steadier. Output streams are unchanged ([#134](https://github.com/Rawframe-Project/maul-audio/pull/134)).

## [0.1.0](https://github.com/Rawframe-Project/maul-audio/compare/v0.0.4...v0.1.0) (2026-10-27)

### Changed

- **Breaking:** HRTF sets (`.maudhrtf`) are version 3, made of tagged fields that a later version can extend. `maudLoadHrtf` reads only version 3 and refuses an earlier file, a later version or an unknown field with `maud_errorVersion` (it was `maud_errorUnsupported`). Convert a SOFA source again with `tools/sofa_to_maudhrtf.py`. The shipped SADIE II set holds the same values, so every render is unchanged ([#87](https://github.com/Rawframe-Project/maul-audio/pull/87)).
- **Breaking:** bakes (`.maudbake`) are version 3 tagged fields too. `maudLoadProbeSet` reads only version 3 and refuses an earlier file, a later version or an unknown field with `maud_errorVersion` (it was `maud_errorUnsupported`); bake the scene again. A bake's layers now follow from the fields it carries, and `maudSaveProbeSet` answers `maud_errorCapacity` for a set too large for the format rather than write a file no reader takes. Baking's results are unchanged ([#88](https://github.com/Rawframe-Project/maul-audio/pull/88)).

### Fixed

- After an input or loopback stream moves or reconnects, its first block on the new device no longer starts with up to one period of the old device's sound, on every backend that reconnects (PipeWire, PulseAudio, Core Audio, WASAPI, AAudio, iOS and the web). That block's `position` skips the frames dropped ([#75](https://github.com/Rawframe-Project/maul-audio/pull/75), [#92](https://github.com/Rawframe-Project/maul-audio/pull/92)).
- PipeWire: when the session manager restarts and replaces its default metadata, the context binds the new one, so streams on the default keep following it and `maudGetDefaultDevice` stays current ([#83](https://github.com/Rawframe-Project/maul-audio/pull/83)).
- PipeWire: an input or loopback stream reads each captured buffer's offset and size within the buffer, as PipeWire's own clients do ([#97](https://github.com/Rawframe-Project/maul-audio/pull/97)).
- HRTF sets lay out their resampled responses through the checked layout, like every other part ([#99](https://github.com/Rawframe-Project/maul-audio/pull/99)).

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
