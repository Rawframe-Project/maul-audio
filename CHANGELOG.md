# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every file format.

## [0.6.0](https://github.com/Rawframe-Project/maul-audio/compare/v0.5.0...v0.6.0) (2026-10-30)


### ⚠ BREAKING CHANGES

* **stream:** maudStream_Render and maudStream_RenderObjects write silence over their frames on any failure but a call back in and a NULL context or stale stream, and maudStream_RenderObjects clears each record but its samples pointer, with silence in its frames, on any failure but a call back in.
* **context:** a maud_errorBusy refusal of maudContext_NextNotification leaves its record as it was instead of zeroing it.
* **device:** maudContext_GetDevices writes the null id over all of idsOut's capacity, and maudDevice_GetName and maudDevice_GetKey write zeros over all of bytesOut's capacity, before they answer; maud_errorCapacity leaves zeros where it left the bytes as they were.
* **spatializer:** maudProbeSet_GetInfo writes count zero points over pointsOut on any failure, where it used to leave them as they were.

### Fixed

* **context:** keep a notification record on a call back in ([#215](https://github.com/Rawframe-Project/maul-audio/issues/215)) ([e33d6f1](https://github.com/Rawframe-Project/maul-audio/commit/e33d6f1da7a817a366afa52b280aeec8709d3fb9))
* **device:** clear the device list and its names before answering ([#207](https://github.com/Rawframe-Project/maul-audio/issues/207)) ([55f9c56](https://github.com/Rawframe-Project/maul-audio/commit/55f9c5677be2b55cf0a6f96f50a611c3e3fd903f)), closes [#206](https://github.com/Rawframe-Project/maul-audio/issues/206)
* **spatializer:** clear a probe set's points when reading them fails ([#204](https://github.com/Rawframe-Project/maul-audio/issues/204)) ([af727d9](https://github.com/Rawframe-Project/maul-audio/commit/af727d9e64bb432b27a63ab3165ee79cff5ee0b7)), closes [#199](https://github.com/Rawframe-Project/maul-audio/issues/199)
* **stream:** silence the frames of a refused render ([#216](https://github.com/Rawframe-Project/maul-audio/issues/216)) ([a8179cd](https://github.com/Rawframe-Project/maul-audio/commit/a8179cd0419ac07a70497b4cb2272c025be8781a)), closes [#208](https://github.com/Rawframe-Project/maul-audio/issues/208)

## [0.5.0](https://github.com/Rawframe-Project/maul-audio/compare/v0.4.0...v0.5.0) (2026-10-30)

0.5.0 ends the review of the API before 1.0.0 ([#150](https://github.com/Rawframe-Project/maul-audio/issues/150)): the last calls and defs that did not follow the Maul family's rules are renamed or corrected. No backend changes what it does (the offline backend's setters are only renamed), so the device checklist on real hardware, which runs before a release that changes a backend, is not run for it. Successful calls, the golden renders and the office bake are unchanged.

### Changed

- **Breaking:** the two calls that choose the spatializer's probe sets are named after the spatializer: `maudProbeSet_UseForPathing` is `maudSpatializer_SetPathingProbeSet` and `maudProbeSet_UseBakedReverb` is `maudSpatializer_SetReverbProbeSet`. Their parameters, results and errors are the same, and the null id still takes a set out of use ([#183](https://github.com/Rawframe-Project/maul-audio/pull/183)).
- **Breaking:** the four calls that script an offline device are named after it: `maudDevice_SetOfflineDefault`, `maudDevice_SetOfflineForm`, `maudDevice_SetOfflineFormat` and `maudDevice_SetOfflineSpatializer` are `maudOfflineDevice_SetDefault`, `maudOfflineDevice_SetForm`, `maudOfflineDevice_SetFormat` and `maudOfflineDevice_SetSpatializer`, with the same parameters and results ([#191](https://github.com/Rawframe-Project/maul-audio/pull/191)).
- **Breaking:** `maudBinauralDef` and `maudBinauralDecoderDef` have no `version` field, and their creations no longer return `maud_errorVersion`. Both take an HRTF set, and `maudLoadHrtf` already refuses headers of another release. A program that builds them with `maudDefaultBinauralDef` and `maudDefaultBinauralDecoderDef` changes nothing; one that set `version` itself removes that line. The two defaults are library functions now, and `MAUD_BINAURAL_DEF_COOKIE` and `MAUD_BINAURAL_DECODER_DEF_COOKIE` are gone ([#190](https://github.com/Rawframe-Project/maul-audio/pull/190)).

### Fixed

- **Breaking:** `maudSource_GetDirectResult` and `maudSpatializer_GetReverbResult` return `maud_errorState` instead of `maud_errorInvalid` before the spatializer has latched its first step. An audio thread that starts before the first step was told on its first blocks that it had passed a bad argument; it now reads that the results are not there yet. The results still read zero. A host that recognized those early blocks by `maud_errorInvalid` checks for `maud_errorState` ([#189](https://github.com/Rawframe-Project/maul-audio/pull/189)).

## [0.4.0](https://github.com/Rawframe-Project/maul-audio/compare/v0.3.0...v0.4.0) (2026-10-29)

0.4.0 makes a refused call leave silence or zeros behind, never data from an earlier call. It changes no backend. Successful calls, the golden renders and the office bake are unchanged. The device checklist on real hardware is waived by the owner for releases through 2026-10-29, so 0.4.0 ships on its other gates, and the checklist runs in [#161](https://github.com/Rawframe-Project/maul-audio/issues/161).

### Changed

- **Breaking:** the binaural effect, the direct effect and the binaural and speaker decoders write silence over their output on any failure, where they used to leave it as it was (`maudBinaural_Process`, `maudDirectEffect_Process`, `maudBinauralDecoder_Decode`, `maudSpeakerDecoder_Decode`). A host that played the buffer after an error it did not check heard the previous block again; it now hears silence. A refused call writes no more than a successful one could: at most the def's `maxFrames` per ear for the binaural effect and decoder (`MAUD_MAX_BINAURAL_FRAMES` when the effect or decoder is NULL), nothing through a NULL buffer or channel pointer, and nothing from a NULL speaker decoder, which gives no channel count. A direct effect run in place silences the block it was given. A buffer a call adds into (the reverb's, encoder's and reflections' bed, a panner's channels) keeps what it held. A host that checks every status hears no difference ([#170](https://github.com/Rawframe-Project/maul-audio/pull/170)).
- **Breaking:** `maudProbeSet_Save` writes `capacity` zero bytes over the caller's buffer on any failure, where it used to leave the buffer as it was. A buffer that held an earlier save kept it after a refusal, so a host that stored the buffer without checking the status wrote a stale bake that loads without complaint. When the capacity is short, `sizeOut` still gives the size the file needs ([#174](https://github.com/Rawframe-Project/maul-audio/pull/174)).

## [0.3.0](https://github.com/Rawframe-Project/maul-audio/compare/v0.2.0...v0.3.0) (2026-10-29)

0.3.0 settles the names of the API before 1.0.0. It changes how web programs link ([#163](https://github.com/Rawframe-Project/maul-audio/pull/163)), and the Core Audio and WASAPI object renderers read the new position type ([#152](https://github.com/Rawframe-Project/maul-audio/pull/152)); the other backends change only by name. The device checklist on real hardware is waived by the owner for releases through 2026-10-29, so 0.3.0 ships on its other gates, and the checklist runs against it in [#161](https://github.com/Rawframe-Project/maul-audio/issues/161). The golden renders and the office bake are unchanged.

### Changed

- **Breaking:** the API follows the Maul family's naming rule. A call on one object is named `maudObject_Verb`, after the kind of its first id or the root it takes (`maudStream_Start`, `maudContext_GetDevices`, `maudDevice_GetInfo`). Creation, destruction, defaults and calls on no object keep `maudVerbNoun` (`maudCreateStream`, `maudDefaultStreamDef`). Callback and hook types end in `Function`, the acoustic scene is `maudScene` throughout, a field that counts things ends in `Count`, times are in seconds with no unit in the name, and a def field that switches a part on is `enableThing` ([#165](https://github.com/Rawframe-Project/maul-audio/pull/165)). A program renames each call, type and field by the tables below, and also:
  - moves the arguments of the three calls marked **reordered**. `maudProbeSet_GetInfo` and `maudRotateAmbisonic` each move two `uint32_t` arguments, so a call left in the old order still compiles: check each one;
  - declares a variable or field that holds a stream callback as `maudStreamFunction*`. `maudStreamCallback` was a pointer type; `maudStreamFunction` is a function type;
  - gives `maudVoiceDetectorDef.hangover` in seconds as a `float` (0.2 for the old 200 milliseconds), and `maudEchoCancellerDef.tailTime` in seconds as before.
- **Breaking:** `maudStreamObject.position` is a `maudVector3` instead of `float[3]`, like every other position in the API. Write `position.x`, `.y` and `.z` instead of `position[0]`, `[1]` and `[2]`. Its frame, its default and its layout are unchanged ([#152](https://github.com/Rawframe-Project/maul-audio/pull/152)).
- **Breaking:** `maudDirectEffectDef.sampleRate`, `maudReverbDef.sampleRate` and `maudSpatializerDef.reflectionRate` are `uint32_t` instead of `float`, like every other sample rate, defaulting to 48000. Their ranges are unchanged ([#151](https://github.com/Rawframe-Project/maul-audio/pull/151)).
- **Breaking:** `MAUD_DIRECT_BANDS` is `MAUD_BANDS`. It sizes every per-band array of the Spatial part, not only the direct path's. The count stays 3 ([#154](https://github.com/Rawframe-Project/maul-audio/pull/154)).
- **Breaking:** `maudLimits.streams`, `devices`, `notifications` and `deviceTextBytes` are `uint32_t` instead of `uint16_t`. The new constants `MAUD_MAX_STREAMS`, `MAUD_MAX_DEVICES`, `MAUD_MAX_NOTIFICATIONS` and `MAUD_MAX_DEVICE_TEXT_BYTES` cap them at 65,535, the most the old fields held, and `maudCreateContext` refuses a limit past its constant with `maud_errorLimit`. Every value accepted before is accepted as before ([#153](https://github.com/Rawframe-Project/maul-audio/pull/153)).
- The voice detector still counts its hangover in 10 ms steps, and a hangover in the upper half of a step now rounds up to the next one instead of down: 207 ms used to give 200 ms and now gives 210 ms. The default, 0.2 seconds, is unchanged. A voice application that set its own hangover may hear speech held one step longer ([#165](https://github.com/Rawframe-Project/maul-audio/pull/165)).

#### Renamed functions

| Old | New | |
|---|---|---|
| `maudResumeContext` | `maudContext_Unlock` |  |
| `maudSetContextSuspended` | `maudContext_SetSuspended` |  |
| `maudGetContextBackend` | `maudContext_GetBackend` |  |
| `maudGetContextMisuse` | `maudContext_GetMisuse` |  |
| `maudGetDevices` | `maudContext_GetDevices` |  |
| `maudGetDefaultDevice` | `maudContext_GetDefaultDevice` |  |
| `maudGetDeviceInfo` | `maudDevice_GetInfo` |  |
| `maudGetDeviceName` | `maudDevice_GetName` |  |
| `maudGetDeviceKey` | `maudDevice_GetKey` |  |
| `maudRequestFocus` | `maudContext_RequestFocus` |  |
| `maudGetContextFocus` | `maudContext_GetFocus` |  |
| `maudNextNotification` | `maudContext_NextNotification` |  |
| `maudRenderObjects` | `maudStream_RenderObjects` |  |
| `maudSetOfflineDefaultDevice` | `maudDevice_SetOfflineDefault` | **reordered**: `(context, role, device)` to `(context, device, role)` |
| `maudSetOfflineDeviceForm` | `maudDevice_SetOfflineForm` |  |
| `maudSetOfflineDeviceFormat` | `maudDevice_SetOfflineFormat` |  |
| `maudSetOfflineDeviceSpatializer` | `maudDevice_SetOfflineSpatializer` |  |
| `maudStartStream` | `maudStream_Start` |  |
| `maudStopStream` | `maudStream_Stop` |  |
| `maudGetStreamFormat` | `maudStream_GetFormat` |  |
| `maudGetStreamStatus` | `maudStream_GetStatus` |  |
| `maudGetStreamPosition` | `maudStream_GetPosition` |  |
| `maudGetStreamClock` | `maudStream_GetClock` |  |
| `maudRenderStream` | `maudStream_Render` |  |
| `maudFeedStream` | `maudStream_Feed` |  |
| `maudDecodeBinaural` | `maudBinauralDecoder_Decode` |  |
| `maudResetBinauralDecoder` | `maudBinauralDecoder_Reset` |  |
| `maudGetSpeakerDecoderMatrix` | `maudSpeakerDecoder_GetMatrix` |  |
| `maudDecodeToSpeakers` | `maudSpeakerDecoder_Decode` |  |
| `maudProcessBinaural` | `maudBinaural_Process` |  |
| `maudResetBinaural` | `maudBinaural_Reset` |  |
| `maudProcessDirect` | `maudDirectEffect_Process` |  |
| `maudResetDirectEffect` | `maudDirectEffect_Reset` |  |
| `maudGetHrtfInfo` | `maudHrtf_GetInfo` |  |
| `maudProcessReverb` | `maudReverb_Process` |  |
| `maudResetReverb` | `maudReverb_Reset` |  |
| `maudGetAcousticSceneMisuse` | `maudAcousticScene_GetMisuse` |  |
| `maudCommitAcousticScene` | `maudAcousticScene_Commit` |  |
| `maudMoveSceneInstance` | `maudSceneInstance_Move` |  |
| `maudGetSpeakerGains` | `maudSpeakerPanner_GetGains` |  |
| `maudPanToSpeakers` | `maudSpeakerPanner_Pan` |  |
| `maudGetSpatializerMisuse` | `maudSpatializer_GetMisuse` |  |
| `maudSetSourcePose` | `maudSource_SetPose` |  |
| `maudGetDirectResult` | `maudSource_GetDirectResult` |  |
| `maudSimulateDirect` | `maudSpatializer_SimulateDirect` |  |
| `maudSimulateReverb` | `maudSpatializer_SimulateReverb` |  |
| `maudSetMaterials` | `maudSpatializer_SetMaterials` |  |
| `maudLatchResults` | `maudSpatializer_LatchResults` |  |
| `maudRenderReflections` | `maudSpatializer_RenderReflections` |  |
| `maudGetReverbResult` | `maudSpatializer_GetReverbResult` |  |
| `maudGetProbeSet` | `maudProbeSet_GetInfo` | **reordered**: `(spatializer, set, infoOut, first, count, points)` to `(spatializer, set, first, infoOut, pointsOut, count)` |
| `maudSetPathing` | `maudProbeSet_UseForPathing` |  |
| `maudUseBakedReverb` | `maudProbeSet_UseBakedReverb` |  |
| `maudBakeProbeSet` | `maudProbeSet_Bake` |  |
| `maudSaveProbeSet` | `maudProbeSet_Save` |  |
| `maudDetectVoice` | `maudVoiceDetector_Detect` |  |
| `maudApplyGainControl` | `maudGainControl_Apply` |  |
| `maudSuppressNoise` | `maudNoiseSuppressor_Suppress` |  |
| `maudCancelEcho` | `maudEchoCanceller_Cancel` |  |
| `maudAddOfflineDevice` | `maudCreateOfflineDevice` |  |
| `maudRemoveOfflineDevice` | `maudDestroyOfflineDevice` |  |
| `maudSceneAnyHit` | `maudFindAnyHit` |  |
| `maudSceneClosestHit` | `maudFindClosestHit` |  |
| `maudResultName` | `maudGetResultName` |  |
| `maudRotateAmbisonic` | `maudRotateAmbisonic` | **reordered**: `(order, from, to, bed, frames)` to `(bed, frames, order, from, to)` |

#### Renamed types

| Old | New |
|---|---|
| `maudStreamCallback` | `maudStreamFunction` |
| `maudAnyHitFn` | `maudAnyHitFunction` |
| `maudClosestHitFn` | `maudClosestHitFunction` |
| `maudTaskFn` | `maudTaskFunction` |
| `maudEnqueueTaskFn` | `maudEnqueueTaskFunction` |
| `maudFinishTaskFn` | `maudFinishTaskFunction` |

#### Renamed scene names

| Old | New |
|---|---|
| `maudAcousticScene` | `maudScene` |
| `maudAcousticSceneDef` | `maudSceneDef` |
| `MAUD_ACOUSTIC_SCENE_DEF_COOKIE` | `MAUD_SCENE_DEF_COOKIE` |
| `maudDefaultAcousticSceneDef` | `maudDefaultSceneDef` |
| `maudCreateAcousticScene` | `maudCreateScene` |
| `maudDestroyAcousticScene` | `maudDestroyScene` |
| `maudAcousticScene_Commit` | `maudScene_Commit` |
| `maudAcousticScene_GetMisuse` | `maudScene_GetMisuse` |
| `maudInstanceTransform` | `maudSceneInstanceTransform` |

#### Renamed fields

| Old | New |
|---|---|
| `maudVoiceDetectorDef.hangoverMilliseconds` (`uint32_t`, milliseconds) | `hangover` (`float`, seconds) |
| `maudEchoCancellerDef.tailSeconds` | `tailTime` |
| `maudHrtfInfo.taps` | `tapCount` |
| `maudProbeSetInfo.probes` | `probeCount` |
| `maudProbeSetInfo.links` | `linkCount` |
| `maudReverbResult.estimates` | `estimateCount` |
| `maudDirectResult.surfaces` | `surfaceCount` |
| `maudDeviceInfo.spatialObjects` | `spatialObjectCount` |
| `maudStreamBlock.objectsAvailable` | `availableObjectCount` |
| `maudBinauralDef.nearField` | `enableNearField` |
| `maudReverbDef.tail` | `enableTail` |
| `maudSourceDef.transmission` | `enableTransmission` |
| `maudSourceDef.pathing` | `enablePathing` |

### Fixed

- Web: a context or effect too large for the page's memory returns `maud_errorCapacity` instead of aborting the whole module with `Aborted(OOM)`. A program that links the library through CMake, from the build tree or the installed package, now links with `-sABORTING_MALLOC=0`; any other build links with it or with `-sALLOW_MEMORY_GROWTH=1`, as the web section of the platforms guide says. A program that relied on the abort now gets NULL from its own failed allocations ([#163](https://github.com/Rawframe-Project/maul-audio/pull/163)).

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
