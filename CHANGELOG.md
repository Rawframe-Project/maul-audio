# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API, the ABI and every data
format.

## [Unreleased]

### Added

- The library skeleton: the build, the family rules and tools, the
  version and result API (`maudGetVersion`, `maudResultName`) and the
  library profile.
- Result codes for the coming device and spatial APIs, and the
  allocator owner objects take (`maudAllocator`).
- Channel layouts (`maudChannelLayout`): mono, stereo, quad, 5.1, 7.1
  and 7.1.4 in the Windows speaker mask order, with each channel's
  speaker and its nominal Recommendation ITU-R BS.2051 position.
- Exact conversion between interleaved frames and one array per
  channel (`maudInterleave`, `maudDeinterleave`), real-time safe.
- A benchmark of the conversions.
- Contexts (`maudCreateContext`, `maudDestroyContext`), with named
  limits, a misuse count and the backend kind; the native backend is
  not in this build yet.
- The offline backend: pull streams rendered on the caller's thread
  at a caller-driven clock (`maudRenderStream`, `maudFeedStream`).
- Streams (`maudCreateStream`, `maudDestroyStream`, `maudStartStream`,
  `maudStopStream`, `maudGetStreamFormat`, `maudGetStreamPosition`):
  one real-time callback receiving fixed periods, output cleared to
  silence before each block, and a rate policy (native, required or
  platform-converted).
- Control calls made on a thread that is rendering one of the
  context's streams are refused with `maud_errorState`, and debug
  builds trap library allocations there.
- Devices (`maudGetDevices`, `maudGetDeviceInfo`, `maudGetDeviceName`,
  `maudGetDeviceKey`, `maudGetDefaultDevice`): ids, native formats,
  rate ranges, display names, persistent keys and the default per role
  (general, communications).
- Streams follow the default device of their direction and role when
  opened on the null device, and move when it changes; a stream on a
  device that disappears is suspended (`maudGetStreamStatus`).
- The notification queue (`maudNextNotification`): device added and
  removed, default changed, stream moved, suspended, resumed and
  format changed, and an overflow record that counts what a full queue
  dropped.
- Scripted offline devices (`maudAddOfflineDevice`,
  `maudRemoveOfflineDevice`, `maudSetOfflineDefaultDevice`), so device
  changes can be tested without hardware.
- The PipeWire backend's devices: a native context on Linux connects to
  PipeWire, opened at run time, lists its sinks and sources with their
  formats, follows its default devices, and reports devices plugged
  in and out.
- Streams on PipeWire: callbacks on libpipewire's real-time thread,
  always in whole periods; native (the graph's rate, which every
  PipeWire device reports), platform-converted and required rates;
  streams on the default move with it, native streams follow the
  graph's rate when it changes, and a stream on a device that is
  unplugged is suspended.
- A restarted PipeWire daemon is reconnected: while it is away every
  device is removed and streams on a default wait; when it is back
  they resume on its devices.
- Backends by name: a context def may ask for PipeWire or PulseAudio
  as well as native, and a context reports the backend it chose;
  native tries PipeWire, then PulseAudio.
- PulseAudio devices (sinks and sources, monitors excluded), the
  server's defaults, hotplug, and reconnection to a restarted server;
  libpulse is loaded at run time.
- Streams on PulseAudio: each running stream has one library thread,
  named maud-pulse, started with the stream and joined when it stops;
  callbacks in whole periods; native rates at the sink's rate; streams
  opened on a device stay on it; streams resume after the server
  restarts.
- ALSA, for systems with neither PipeWire nor PulseAudio: the default
  PCM and each hardware endpoint as devices, found without opening
  any; streams that take the hardware's rate nearest 48 kHz, reorder
  channels to the PCM's map, and run on one library thread, named
  maud-alsa, while they run. alsa-lib's messages are silenced for the
  library's own calls. Native tries ALSA after PulseAudio.
- Devices may report a rate of 0 and maud_layoutNone where the
  platform cannot tell without opening them.
- ALSA hotplug: the context watches /dev/snd and, on the notification
  drain, rescans the cards when one's nodes come, go or become
  readable, posting added and removed devices. A stream whose card
  went away stops polling its PCM.
- WASAPI devices on Windows 10 and later: the active endpoints with
  the engine's format, read without activating them; the console and
  communications roles' defaults; added, removed and changed devices
  and defaults through IMMNotificationClient, taken on the drain. The
  context holds the multithreaded apartment open with
  CoIncrementMTAUsage instead of initializing COM on the host's
  threads.
- Streams on WASAPI: shared mode, event-driven, at the engine's rate
  unless platform-converted; each running stream has one thread, named
  maud-wasapi, in MMCSS's "Pro Audio" class; a stream on a default is
  opened again on its new endpoint when the default moves.
- The web backend, Web Audio: one output at the AudioContext's rate,
  played by an AudioWorklet from frames the library renders on the
  page's main thread, through a SharedArrayBuffer ring on cross-origin
  isolated pages and posted chunks elsewhere. It keeps the browser's
  baseLatency plus 256 frames ahead, and 128 more for each quantum
  that runs short, up to 4096.
- `maudResumeContext` and the suspension `maud_suspendPolicy`: streams
  the browser's autoplay policy holds wait, and resume once a user
  gesture's handler calls it.
- CoreAudio on macOS: the HAL's devices by UID, with names, nominal
  rates and the ranges they run at, both roles' defaults, and changes
  heard on a private dispatch queue; output streams through an AUHAL
  unit at the device's rate or converted, the callback on the HAL's IO
  thread; a stream on the default follows it to its new device;
  devices that come and go are added and removed, and a stream opened
  on one that goes is suspended as lost.
- Capture on CoreAudio: an AUHAL input unit at the device's rate (its
  input side cannot convert the rate, so a converted input at another
  rate is refused).
- Capture on the web: getUserMedia with the browser's voice processing
  off, into a capture worklet, through a SharedArrayBuffer ring on
  isolated pages and posted chunks elsewhere; until the browser grants
  the microphone the stream waits, suspended with the new reason
  `maud_suspendPermission`.
- The stream clock (`maudGetStreamClock`, `maudGetHostNanoseconds`): a
  frame, the host time at which it is heard or was captured, and the
  latency, stamped at each callback from the platform's report: on
  PipeWire, PulseAudio, ALSA, WASAPI (IAudioClock and the capture
  packets' times), CoreAudio (the buffer's host time and the device's
  latencies) and the web (what is buffered ahead plus the context's
  latencies).
- Duplex streams (`maud_directionDuplex`, `maudStreamDef.inputDevice`):
  one callback with the input and the output of a period, built from an
  output and an input of any backend joined by a ring; the input
  follows the output's clock by slipping, declared and counted in the
  stream's status (`maudDriftPolicy`, `slippedFrames`).
- Duplex on one clock where the platform has one, reported as
  `maud_driftNone`: PipeWire schedules both halves under one driver
  (one `node.group`), the web renders both in one AudioContext, and
  CoreAudio runs them on one clock when both are on one device. A
  CoreAudio duplex on two devices needs them at one nominal rate for
  now, since an input unit cannot convert; it slips.
- Platform voice processing, asked for per part
  (`maudStreamDef.voice`: echo cancellation, noise suppression, gain
  control) by input and duplex streams, and reported per part in the
  status (`voiceReported`, `voiceActive`), since platforms may ignore
  a request. The web passes the parts to getUserMedia and reports the
  track's settings; ALSA and the offline backend report none.
- Voice processing on WASAPI: a stream that asks for it is a
  communications stream (`AudioCategory_Communications`), and an input
  that asks for none asks for the raw signal; on Windows 11 the
  effects manager turns the asked-for parts on and the others off where
  the stream may choose, and reports what is on.
- Voice processing on CoreAudio: a duplex stream that asks for it runs
  both halves on one Voice-Processing I/O unit, which takes what it
  plays out of what it hears, on one clock; it reports echo
  cancellation and noise suppression (the unit has no switch for one
  without the other) and gain control as the unit reads it. An input
  alone runs on the HAL unit and reports no processing. The unit
  captures one channel whatever it is asked for; it is asked for one
  and the channel is spread to the stream's, and a capture render's
  unwritten frames are silence.
- A first-party voice activity detector (`maul-audio/voice.h`,
  `maudVoiceDetector`), standing apart from contexts and streams: six
  band levels against noise floors learned over 1.5 s, four
  aggressiveness levels, a hangover; real-time safe, and its decisions
  do not depend on how the frames are cut.
- A first-party automatic gain control (`maudGainControl`), after
  WebRTC AGC2's adaptive digital rules on the voice detector: speech
  brought to a target level, the gain changing at a bounded pace and
  rising only on proven speech, noise kept under a ceiling, and a
  limiter holding every sample under -1 dBFS; it may also lower a loud
  microphone's gain.
- Underrun and overrun counters in the stream status (`underruns`,
  `overruns`), counted as each platform reveals them: ALSA's `-EPIPE`,
  PulseAudio's underflow and overflow, PipeWire's skipped graph cycles,
  WASAPI's empty render buffers and capture discontinuities,
  CoreAudio's sample time jumps, the web's short quanta and full
  capture rings.
- Host lifecycle suspension (`maudSetContextSuspended`): the host's
  background, hidden-tab or sleep signal suspends every stream that
  runs or only waits to, with the new reason `maud_suspendHost`, and
  resumes them after; a lost device keeps its reason, and the web's
  AudioContext suspends with the host.
- Device forms and route changes: `maudDeviceInfo.form` says what a
  device's active port leads to (speakers, headphones, headset,
  handset, microphone, line, digital, or unknown), read from PipeWire's
  and PulseAudio's form factor and port type, WASAPI's endpoint form
  factor and CoreAudio's data source and transport; a change on the
  same device, as headphones in its jack, posts
  `maud_notifyRouteChanged` with the new form. The offline backend
  scripts it with `maudSetOfflineDeviceForm`. ALSA hardware PCMs named for HDMI,
  DisplayPort, IEC958 or S/PDIF are digital. On PipeWire, a node on a card
  takes the form of its card's active route (the Device's Route
  `port.type`), so a jack switch on one node is a route change.
- Fixed: destroying a stream on a platform backend failed with
  `maud_errorState` whenever its callback happened to be running on the
  platform's thread, leaving the stream running; it now succeeds, and
  returns once that callback has. Only the offline backend, whose host
  renders, still refuses while another thread renders the stream.
- Web device selection: the browser's audio inputs, and its outputs
  where `AudioContext.setSinkId` exists, are listed by their ids and
  labels (read again on `devicechange` and once the microphone is
  granted). A capture stream asks for its device; output streams set
  the AudioContext's sink, and since one AudioContext plays to one
  device, an output on another device while one plays is refused with
  `maud_errorUnsupported`.
- Share modes: `maudStreamDef.share` asks for `maud_shareExclusive`,
  the device for one stream alone, on a named device; it never falls
  back to shared and is refused with `maud_errorUnsupported` where the
  backend or device cannot give it (PipeWire, PulseAudio, the web, the
  offline backend, ALSA's default PCM, duplex streams, converted
  rates). ALSA opens its hardware PCMs exclusively.
  `maudStreamStatus.exclusive` reports whether a stream keeps others
  off its device, which an ALSA hardware PCM does even opened shared.
- CoreAudio exclusive streams: an exclusive stream takes its device in
  hog mode while it lives and gives it back when it goes; a device held
  by another process, or by another stream, is refused with
  `maud_errorPlatform`.
- WASAPI exclusive streams: an exclusive stream opens its endpoint in
  exclusive, event-driven mode in the first format the device takes as
  is (32-bit float, 32-bit integers with 32 or 24 valid bits, 16-bit
  integers), converting samples to and from float, with a buffer of its
  period aligned as the endpoint asks; refused with
  `maud_errorUnsupported` where the device or the user's policy allows
  none (as under Wine) and `maud_errorPlatform` when another
  application holds it.
- The HRTF format (`docs/hrtf-format.md`): minimum-phase responses with
  onset delays on 5-degree elevation rings, 16-bit, with the dataset's
  name and license, the distance it was measured at, and a CRC-32
  (version 2); `tools/sofa_to_maudhrtf.py` converts
  SOFA files into it (numpy and h5py, tool-only); the default set,
  SADIE II's KU100 at 48 kHz under the Apache License 2.0, generated
  into `data/hrtf/` with its provenance.
- HRTF loading (`maul-audio/hrtf.h`): `maudLoadHrtf` reads a .maudhrtf
  file's bytes, checking every count, size, ring, text and its CRC-32
  before trusting anything, with limits on directions and taps in its
  def, and resamples the set to the renderer's rate (band-limited, the
  gain kept; a resampled set starts a constant 24 input samples early);
  `maudGetHrtfInfo` reports the set, the distance it was measured at,
  its name and license. A libFuzzer target, `fuzz_hrtf`, behind
  `MAUL_AUDIO_FUZZ`.
- The near-field filter the binaural effect will use (internal): per
  ear, a gain and a first-order high shelf for a source's distance
  against the set's, from a table generated by
  `tools/near_field_table.py` (numpy, tool-only) out of the rigid-sphere
  series, its DC gain exact (Weiss's sphere theorem). Within 0.48 dB RMS
  of the sphere from 0.1 m, and within 1 dB at the test's reference
  points.
- Binaural effects (`maul-audio/binaural.h`): one per source, mono in,
  both ears out, through a loaded HRTF set. Each ear blends the four
  measured responses and delays around the direction, reads a cubic
  fractional delay and runs a direct-form FIR; a change of direction
  crossfades the old and new filters and ramps the delays over 2.67 ms,
  the latest change winning; the host's gain ramps across each call.
  Processing allocates nothing. `maudVector3` (`maul-audio/base.h`)
  carries the listener's frame: +x right, +y up, -z ahead. A benchmark,
  `maul-audio_bench_binaural`, prints the cost per source.
