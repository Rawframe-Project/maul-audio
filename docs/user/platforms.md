# Platforms

The Device part runs on Linux, Windows, macOS, iOS, Android and the web; the Spatial part runs anywhere a C compiler does. This page lists what a program must do on each platform, and what to look at when there is no sound.

| Platform | Backend | Loopback | Exclusive | Objects | Audio focus |
|---|---|---|---|---|---|
| Linux | PipeWire, then PulseAudio, then ALSA | PipeWire, PulseAudio | ALSA hardware devices | no | no |
| Windows 10 and later | WASAPI | yes | yes | spatial sound objects | no |
| macOS | Core Audio | no | hog mode | Apple's spatial mixer | no |
| iOS | RemoteIO with an audio session | no | no | Apple's spatial mixer | yes |
| Android 11 (API 30) and later | AAudio | no | AAudio's exclusive mode | no | with the Java half |
| Browsers | Web Audio with an AudioWorklet | the user's pick | no | no | no |

## Linux

The library opens libpipewire, libpulse and libasound at run time, so a program built with all three runs on a machine that has any of them, and creation fails with `maud_errorUnsupported` only where none answers. A native context on PipeWire waits up to two seconds on the calling thread for the device list when it is made.

ALSA lists the cards' hardware endpoints without opening them, so their rates read 0 until a stream opens one. A hardware endpoint opened even in shared mode keeps other programs off the device; the status's `exclusive` says so.

## Windows

The library needs nothing from the program's threads: every WASAPI call runs on a thread of the context's own in COM's multithreaded apartment, so the program's main thread may be in a single-threaded apartment, as one with drag and drop is. Streams on a device that plays nothing get no loopback blocks while it is silent. Build with `clang-cl`; MSVC's `cl` compiles the headers but not the library.

## macOS and iOS

On iOS the context manages the audio session: the category follows the streams there are (playback, recording or both, the speaker the default output with Bluetooth allowed), and other applications' audio mixes with the program's until it asks for focus. `iosSilencedBySwitch` in the context def lets the Ring/Silent switch silence output that is not recording, as a game's sound effects should be. An interruption (a call, an alarm) suspends streams with `maud_suspendPolicy`; one that ends without the hint to resume waits for `maudResumeContext` or a focus request.

An iOS application that captures needs `NSMicrophoneUsageDescription` in its `Info.plist`, or the system ends it at the first capture.

## Android

AAudio alone plays the default output and captures the default input. With the Java half, the context lists every device, follows their changes, asks for the microphone, and takes audio focus:

1. Compile `java/maul/audio` with the application's own Java sources.
2. Pass the application's `JavaVM*` and an `android.content.Context` in the context def's `androidJavaVm` and `androidContext`. When the Context is an Activity, the context asks for the `RECORD_AUDIO` permission once.
3. If the application shrinks its code with R8 or ProGuard, as a release build does, add `java/proguard-rules.pro` to its rules. The native library finds the classes by name, which a shrinker cannot see; without the rules, creating a context with those handles fails with `maud_errorPlatform`.

An application targeting Android 15 may ask for audio focus only while it is the top application or runs a foreground service.

## The web

Build with Emscripten. Each stream plays through an AudioWorklet; the library renders ahead on the page's main thread, from the browser's event loop. Three things are the page's to do:

- **A gesture.** Browsers hold audio until the user acts. Call `maudResumeContext` from a click's or a key's handler; until then streams are suspended with `maud_suspendPolicy`. A loopback stream is made in such a handler too, since it opens the browser's picker of what to share.
- **Isolation, where it can.** A page served with `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp` is cross-origin isolated, and the library then hands frames to the worklet through shared memory. Elsewhere it posts them, which on a loaded machine needs more latency to avoid dropouts. `maudGetStreamClock` reports the latency a stream actually has.
- **Files.** The library opens no files: the page fetches the HRTF set (854 KB, 636 KB gzipped) and hands its bytes to `maudLoadHrtf`.

The microphone needs the page served from `https` or `localhost`. Until the user allows it, capture streams wait with `maud_suspendPermission`.

## When there is no sound

- **Creation fails with `maud_errorUnsupported`.** No backend answered: on Linux no sound server is running and libasound is missing, or the build left the platform's backend out.
- **A stream is started but silent.** Read `maudGetStreamStatus`: its `suspension` says why (see [Devices and streams](devices-and-streams.md#why-a-stream-is-suspended)). On the web the usual reason is a missing gesture.
- **Clicks and gaps.** The status counts underruns and overruns the platform revealed. A callback that allocates, locks, logs or waits misses its deadline; so does one that does more work than a period allows.
- **Calls fail with `maud_errorState`.** The call came from the stream's callback, or from the allocator or a hook the object was running; `maudGetContextMisuse` and `maudGetSpatializerMisuse` count such calls.
- **`maud_errorVersion`.** The program was built against headers of another version than the library it runs with. Rebuild it against the library's headers.
- **The device list is stale.** Drain the notification queue regularly: on PipeWire and PulseAudio the drain is what takes in the server's reports.
