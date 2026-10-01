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
