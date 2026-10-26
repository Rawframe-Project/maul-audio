# Backends

What each native backend does that its code alone does not explain: how it is threaded, what the platform hides, and the traps it works around. The web has a page of its own, [The web](web.md).

## Contents

- [Linux: libraries opened at run time](#linux-libraries-opened-at-run-time)
- [PipeWire](#pipewire)
- [PulseAudio](#pulseaudio)
- [ALSA](#alsa)
- [WASAPI](#wasapi)
- [Core Audio](#core-audio)
- [iOS](#ios)
- [AAudio](#aaudio)
- [The offline backend](#the-offline-backend)
- [Private backends](#private-backends)

## Linux: libraries opened at run time

The three Linux backends open their libraries with `dlopen` per context and call them through tables ([pipewire_api.h](../../src/pipewire_api.h), [pulse_api.h](../../src/pulse_api.h), [alsa_api.h](../../src/alsa_api.h)). The library links nothing of them, so one binary runs on a desktop with PipeWire, a server with PulseAudio, an embedded board with ALSA alone, and a container with none, where creation returns `maud_errorUnsupported`. The build needs the headers only. A symbol a table needs that the installed library lacks makes that backend refuse to open, and the native order moves to the next. `test_loaders` points each loader at a file that is no library and at a stub with none of the functions, and checks that both are refused and closed again.

The native order is PipeWire, then PulseAudio, then ALSA. On a PipeWire system the PulseAudio and ALSA APIs are served by PipeWire too, so asking for them explicitly tests those paths against a PipeWire daemon.

## PipeWire

The context owns a `pw_loop` as PipeWire's main loop and iterates it on the host's thread only: without blocking when the host drains, and up to a deadline for creation's round trips (up to two seconds for the first device list). Registry events become device changes; the `default` metadata names the defaults, and when it goes, as it does when WirePlumber, which owns it, restarts, the context lets go of it and binds the next one, as it does the `settings` metadata; a card's Route params say which port each node plays through, so a jack switch changes a node's form ([pipewire_card.h](../../src/pipewire_card.h)). What the backend parses out of the daemon's objects is in [pipewire_parse.h](../../src/pipewire_parse.h), apart from the connection, so tests feed it pods without a daemon.

Streams are `pw_stream`s processed on libpipewire's real-time data thread, which libpipewire starts with its context. The graph runs at one rate, so `maud_rateRequired` is the graph's rate and every device reports it. PipeWire moves streams itself when a default changes, so a move only retargets. Pull mode is refused: PipeWire owns the thread.

A lost connection keeps the devices for ten seconds (see [Devices](devices.md#service-restarts)); each drain tries a new core.

## PulseAudio

The context owns a plain `pa_mainloop` iterated on the host's thread, as PipeWire's is. Each stream has its own `pa_mainloop`, `pa_context` and `pa_stream`, connected corked by the control thread; while it runs, one library thread iterates that loop and moves whatever size the server asks for through the period adapter. A stream's connection is run by its thread while the stream runs and by the control thread otherwise, never both at once, so the audio thread shares no lock with the context's loop. The server's underflow and overflow callbacks count xruns. Timing updates keep `pa_stream_get_latency` current for the clock. libpulse 14.0 and later give a port's type, which becomes the device's form.

## ALSA

ALSA has no server and no thread. The devices are the default PCM and each hardware endpoint the control interface lists, found without opening any of them ([alsa_scan.h](../../src/alsa_scan.h)), so their formats read 0 until a stream opens one. Opening a hardware PCM to list it would take it from every other program. Hotplug comes from watching `/dev/snd` without blocking ([alsa_watch.h](../../src/alsa_watch.h)): a card's nodes appear before they are readable, so the watch waits for readable, and the drain rescans.

Each stream opens its PCM when it is created. While it runs, one library thread polls the PCM's descriptors and an `eventfd`, moves what the PCM takes or has, and reorders channels where the PCM's channel map differs from the stream's layout ([alsa_chmap.h](../../src/alsa_chmap.h)). An `-EPIPE` is an xrun, recovered with `snd_pcm_recover`; a PCM that fails past recovery (its card went away) suspends the stream. A hardware PCM is exclusive by nature, opened shared or not; the default PCM never is.

## WASAPI

COM objects belong to the apartment that made them, and a host's main thread is often in a single-threaded apartment (drag and drop needs one). Calling an MMDevice or audio client made in one apartment from another without marshalling is outside COM's rules. So every COM call of a context runs on one thread of its own in the multithreaded apartment ([wasapi_apartment.h](../../src/wasapi_apartment.h)), while the calling thread waits; the host may call from any apartment.

Devices are the active endpoints, their formats read from the property store without activating them. The `IMMNotificationClient`'s callbacks run on the system's threads and only raise a flag ([wasapi_notify.h](../../src/wasapi_notify.h)); the drain lists endpoints and defaults again. WASAPI binds an audio client to one endpoint, so every move reopens the stream.

Each stream has a shared-mode, event-driven audio client. While it runs, one library thread in the multithreaded apartment, registered with MMCSS, waits on the client's event and moves the buffer's free space or every captured packet. A loopback stream's blocks pause while the endpoint plays nothing, since Windows delivers no packets then. Exclusive streams take the endpoint in a format it takes as is ([wasapi_exclusive.h](../../src/wasapi_exclusive.h)). The plain-integer parts (speaker masks, performance-counter times) live in [wasapi_format.h](../../src/wasapi_format.h) so that every platform builds and tests them.

The Windows runners in CI have no audio endpoint, so the WASAPI tests run under Wine in development (see [Test environments](../operations/test-environments.md)). Wine does not enforce COM apartments, so it cannot catch a call on the wrong thread.

## Core Audio

Devices are the HAL's devices that are not hidden, one per direction with channels on it, keyed by UID. The HAL reports changes to the device list and the defaults through a listener block on a queue the context owns; the block raises a flag, and the drain lists devices and defaults again ([coreaudio_listen.h](../../src/coreaudio_listen.h)). A device's form comes from its data source, else its transport ([coreaudio_form.h](../../src/coreaudio_form.h)). After `coreaudiod` restarts, the drain adds its listeners again and opens every stream's unit again.

Each stream has an AUHAL output unit on its device, which converts the stream's 32-bit float frames to the device's format; the render callback runs on the HAL's IO thread. An AUHAL unit is bound to one device, so a move reopens it. A duplex stream that asks for voice processing runs both halves on one Voice-Processing I/O unit instead, which takes what it plays out of what it hears. Exclusive streams take hog mode on the device ([coreaudio_hog.h](../../src/coreaudio_hog.h)).

## iOS

iOS routes audio itself and offers no choice of output, so the context has one output and one input, the session's, keyed `default`; streams follow them wherever iOS routes. Streams run on RemoteIO units, or a Voice-Processing I/O unit when they ask for voice processing. The audio session ([ios_session.h](../../src/ios_session.h)) is the context's: its category follows the streams that exist (playback, record, or play-and-record with the speaker as the default output and Bluetooth allowed) and the host's focus requests, and `iosSilencedBySwitch` makes output alone Ambient, which the Ring/Silent switch silences. Interruptions hold the streams with `maud_suspendPolicy` and are focus states; route changes give the default devices their forms; a reset of the media services holds the streams while every unit is made again.

## AAudio

AAudio lists no devices and reports no changes, and Android has no query for the device media goes to: its policy routes a stream opened without one. So the context always has a default output and input, keyed `default`, which streams on the null id follow wherever Android moves them. The output's format comes from an AAudio stream opened once at the start; the input is described from the output, since opening one would show the microphone in use.

Given a `JavaVM` and an Android `Context`, the Java half ([aaudio_java.h](../../src/aaudio_java.h), `java/maul/audio/Devices.java`) lists every user-facing device next to the defaults, raises a flag when they change, asks for the microphone, and takes audio focus. Each JNI call attaches the calling thread when it is not attached and detaches it afterwards, so the host's threads are left as they were; a pending Java exception is cleared and the call fails. The native library finds the Java class by name, which R8 cannot see, so the library ships `java/proguard-rules.pro`, and the Android tests build their application with R8 and that file, so a member reached by name without a keep rule fails a test.

AAudio's error callback runs on a thread of AAudio's that may not close the stream: it raises a flag, and the drain opens the stream again on the new default.

## The offline backend

The offline backend's devices are the host's ([offline.c](../../src/offline.c)); its streams render on the caller's thread (`rendersOnCaller`), in pull mode only, and it has no rate converter. It renders object streams to the caller, standing in for a platform renderer, so object streams are tested everywhere.

## Private backends

A private backend implements [backend.h](../../src/backend.h) from outside the repository and defines `maudGetPrivateBackend` ([Private backends](../user/private-backends.md)). The header is the contract and is not stable: keeping it internal leaves the core free to change how it talks to backends (how a stream's state is published, the drain, the period loop), which a public backend interface would freeze for backends the library cannot see. A console build pins a release and rebuilds its backend with each one it moves to.
