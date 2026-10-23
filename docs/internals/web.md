# The web

A browser gives a page no audio thread it can run C on in time. Emscripten's code runs on the main thread (or workers), and the AudioWorklet that does run on the audio thread is JavaScript in another global scope. The web backend ([backend_web.c](../../src/backend_web.c)) therefore splits each stream: the library renders on the main thread, ahead of time, and a small worklet plays what it was given. Its JavaScript lives once, as `EM_JS` functions in the C sources.

## Playback

The context owns one `AudioContext`, kept by the JavaScript side in a table under a handle. Each stream has an `AudioWorkletProcessor` that plays 128-frame quanta from a queue and reports every quantum it plays. The main thread answers each report by rendering, in steps of 128 frames through the period adapter, up to a **fill target**: the browser's `baseLatency` plus two quanta, growing by one quantum for each quantum the worklet plays short, up to 4,096 frames.

The frames cross to the worklet one of two ways:

- On a **cross-origin isolated** page (served with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`), through a `SharedArrayBuffer` ring the worklet reads directly.
- Elsewhere, as chunks **posted** to the worklet's port, their buffers transferred.

While the main thread keeps up, the two cost the same; headless Chrome reports about 150 ms of output latency either way, most of it its own `outputLatency`. Posted chunks also wait in the port's message queue, so on a loaded machine they arrive late more often, the worklet plays short, and the fill target grows until it reaches its ceiling, where a ring would have held its target. The library supports both because isolation breaks pages that embed cross-origin content without CORP headers, which many pages do.

The callback runs on the main thread, from the browser's event loop. A long task on the page (layout, a large script) delays rendering; the fill target is what absorbs it.

## Capture

`getUserMedia` asks for the microphone, with the browser's voice processing only if the stream asks for it (it is on by default in browsers, which a music application does not want). The track plays into a capture worklet, mixed to the stream's channels, which writes each quantum into a ring on an isolated page or posts it elsewhere ([web_capture.h](../../src/web_capture.h)). The main thread copies what waits through the period adapter, so a capture callback runs on the main thread too.

Until the user allows the microphone, a capture stream waits with `maud_suspendPermission`. When the browser ends a track (its device went, or permission was withdrawn), the capture opens again, on the default if its device went. A capture on the default is also opened again when the system's default microphone moves, because an open track stays on its device.

A loopback stream captures what the user picks in `getDisplayMedia`'s picker, with its audio where the browser gives it. The picker needs a user gesture, so such a stream is made in a gesture's handler. When the user presses the browser's "Stop sharing", the stream waits with `maud_suspendPermission`; only a new stream made in a gesture can ask again.

## Devices

The JavaScript side keeps the latest `enumerateDevices` list and marks it new; the drain copies it and syncs the device table ([web_devices.h](../../src/web_devices.h)). Inputs are listed always; outputs only where `AudioContext.setSinkId` can choose one. The browser's `default` and `communications` entries are the Default devices; entries without an id (before the microphone is granted) name nothing. When the system's default moves, the Default device stays the default and reports `maud_notifyRouteChanged`. A sink the browser refuses suspends the streams on it with `maud_suspendDeviceLost`.

The autoplay policy holds a new `AudioContext` until a user gesture. The drain reads the context's state and suspends or resumes the streams with `maud_suspendPolicy`; `maudResumeContext` from a gesture's handler asks the browser to run it.

## The stream clock

A web stream's clock comes from the `AudioContext`'s output timestamp: the context time a frame is heard, and the `performance.now()` that goes with it. Two things make the raw timestamp unusable as is:

- **Early stamps.** In a context's first quarter second, and when a report is starved, the timestamp runs early by up to hundreds of milliseconds. Stamps count only after the context has run 0.25 s and while they are under 100 ms old.
- **Drift through a sound server.** Through PulseAudio (as Chrome plays on Linux), the timestamp drifts late for seconds and then snaps back, while the frames reach the device evenly, and returns to its true low only every tens of seconds.

The filter ([web_clock.h](../../src/web_clock.h)) therefore works out, from each stamp, the time the stream's frame 0 was heard or captured, and holds the earliest such time over the last 30 seconds, in six spans of five seconds. A frame's time is frame 0's plus its place. A frame-0 time more than 50 ms later than the held one (twice the largest drift measured through PulseAudio) is not drift: the browser lost frames before the stream or played silence it never asked for, so the filter counts an xrun and restarts from there. A shorter window was tried first and let the held time rise with the drift between its lows.

Each stream's stamps are taken against one reading of the host clock for the whole report, so a page delayed between two readings does not shift the clock by the delay.

## What the tests cover

The Node tests ([test_web_transport.c](../../test/test_web_transport.c), [test_web_capture.c](../../test/test_web_capture.c)) run the backend's JavaScript against stand-ins for the worklet, the media devices and a simulated `performance.now()`, so timing logic runs deterministically. The pages in `test/web/` run in headless Chrome through puppeteer, on an isolated and a plain page: playback, capture with the microphone allowed and refused, default changes, sinks, a stopped track, loopback, and the clock. What only a real browser and real devices show is on the [device checklist](../operations/device-checklist.md).
