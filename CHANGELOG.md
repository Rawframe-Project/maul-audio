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
