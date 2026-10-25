# Streams

What happens between a platform's audio callback and the host's, and why each piece is there.

## Opening

`maudCreateStream` checks the def's ranges, then asks the backend to settle a format on the device the stream starts on (`openStream` in [backend.h](../../src/backend.h)): the rate from the policy, the period, the layout. A backend refuses what its platform cannot run (`maud_errorUnsupported`) instead of converting behind the host's back: a stream never resamples unless its policy is `maud_ratePlatformConverted`, and then the platform's converter does it, never the library's. A stream with no device yet (following a default of a direction with none) settles on the backend's assumed rate and is suspended.

[stream_open.c](../../src/stream_open.c) then allocates the stream's one block (the period buffer, the objects of an object stream, a duplex ring) and the backend attaches its platform side. The library allocates nothing for the stream after this: a move, even to a device at another rate, reuses the block, since the period in frames does not change.

## The fixed period

Platforms move buffers of whatever size they like: PipeWire's quantum changes with the graph, WASAPI's packets vary, a browser's worklet takes 128 frames, Core Audio's buffer size is a hint. The host gets exactly `periodFrames` every time: [period.h](../../src/period.h) holds one period, fills it through the callback when an output runs dry, and passes it on when an input completes one. The cost is up to one period of extra latency on platforms whose buffers do not line up, which the stream clock reports.

## The stream clock

At each callback the rendering thread stamps the stream: the frame at its position before this callback's frames is heard (output) or was captured (input) so many nanoseconds from now, as the platform's own timestamps say ([clock.h](../../src/clock.h)). Any thread reads the stamp under a sequence counter. The stamp follows route changes (a Bluetooth headset adds a hundred milliseconds or more), since the platform reports its latency afresh at each callback. Each backend works its latency out from what its platform offers: PipeWire's and PulseAudio's timing info, WASAPI's device position and QPC time, Core Audio's timestamps and the device's latency and safety offset, AAudio's frame timestamps, the browser's output timestamp ([The web](web.md)).

## Duplex

A duplex stream is an output stream and a hidden input stream of the backend, joined by a ring ([duplex.c](../../src/duplex.c)). The input's callback writes into the ring; the output's callback takes a period of input from it and calls the host with both, so the host sees them in step. The ring holds at least eight periods; it is kept near two:

- On **one clock** (`maud_driftNone`: PipeWire's graph, one Core Audio device, the web's AudioContext), nothing drifts and the ring only absorbs the order of the two callbacks.
- On **two clocks** (`maud_driftSlip`: two devices), the input runs slightly faster or slower than the output. When the ring runs short the missing input is silence; past four periods the oldest frames beyond two are dropped. Both are counted as slipped frames. Resampling the input to the output's clock would hide drift without dropping, at the cost of an adaptive resampler on the audio thread; slipping costs an audible glitch every few minutes on two consumer devices, which a voice application tolerates and a host that cares can avoid by choosing devices on one clock.

The input opens at the output's rate, required where the device runs it, converted by the platform where it does not. A duplex stream keeps that rate when its output moves.

## Underruns and overruns

The status counts what the platform reveals ([xrun.h](../../src/xrun.h)): a gap in PipeWire's clock of more than one and a half periods; PulseAudio's underflow and overflow callbacks; ALSA's recovery from `-EPIPE`, or a running output's queue found empty before a write once it has had the stream's frames, which is how ALSA's PulseAudio plugin plays a gap, with no `-EPIPE`, or found still holding frames a period after every frame written could have played out, once an earlier write found frames queued, which is how the plugin reports a gap through `pipewire-pulse` (#60) (a fill into an empty queue sets no such time, since a sink may start late; a sink that stops taking frames mid-stream and resumes with them still queued looks the same to the plugin, and counts too); WASAPI's buffer found empty while playing, or its data discontinuity flag on capture; a jump in Core Audio's and iOS's sample time; AAudio's running total; on the web, a stream's frame 0 moving later for good. Each gap counts once, by the event that shows it: a late cycle, an underflow callback, a recovery, the write that finds the queue empty or overdue. A platform that reveals nothing counts nothing; the counters are a diagnostic, not a guarantee.

## Exclusive streams

An exclusive stream plays the device's own format, so it is the one place the library converts samples: float to and from the integer formats a device takes as is ([sample_convert.h](../../src/sample_convert.h)). Full scale is 1.0, values past it saturate, NaN becomes silence, integers round to the nearest. No rate conversion: an exclusive stream runs at the device's rate or is refused.

## Object streams

An object stream's callback fills the bed and up to `objectCount` mono objects, each with a position, a gain and an activity that persist from one period to the next, so a still object costs no writes. The period adapter keeps one period of frames per object. The backend hands them to the platform renderer: WASAPI's spatial audio objects ([wasapi_objects.h](../../src/wasapi_objects.h)), where the bed is one static object per channel and an active object takes a dynamic one while the platform has one to give; Apple's spatial mixer ([apple_objects.h](../../src/apple_objects.h)), each object on its own input bus. Neither applies distance attenuation: the host's direct path already did.

## Voice processing on the platform

An input stream asks the platform for its processing when the backend opens it (WASAPI's communications category, the Voice-Processing I/O unit on macOS and iOS, AAudio's voice communication preset), then reports what the platform says is active through `maudReportVoice`. Platforms that say nothing leave `voiceReported` false; a backend whose platform has none reports nothing active from the start.
