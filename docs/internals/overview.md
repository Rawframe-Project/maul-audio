# Overview

Maul Audio is two parts that share only a base: the Device part (contexts, devices, streams and the voice processors) and the Spatial part (HRTF sets, effects, the ambisonic bed, the spatializer and scenes). Neither part's code calls the other's; `MAUL_AUDIO_DEVICE=OFF` and `MAUL_AUDIO_SPATIAL=OFF` builds link only if that stays true. The base is results, allocators, buffers, layouts and the FFTs ([CMakeLists.txt](../../CMakeLists.txt) lists each part's sources). This page is the library's profile within the Maul family and the shape the other pages assume.

## Profile

- **Determinism.** Audio is presentation, so rendered output is reproducible on one platform and build: the same inputs give the same samples, and golden tests compare across platforms within a tolerance. Baked data is content with an identity, so a bake is bit-exact on every platform, compiler, architecture and task split. Simulation results (occlusion, transmission, estimates, paths) do not depend on how the host's tasks split the work: rays are generated and batched in a fixed order and every reduction runs in a fixed order. Code whose results must match everywhere calls the library's own transcendental functions ([portable_math.h](../../src/portable_math.h)), never the C library's.
- **Threads.** The library starts a thread in two cases only, both declared here:
  - a stream in callback mode on a platform that runs no audio thread of its own (WASAPI, ALSA, PulseAudio) gets one ([worker.h](../../src/worker.h)), started when the stream starts and joined when it stops or is destroyed, at the platform's real-time priority, running only the stream's period loop and the host's callback;
  - a WASAPI context has one thread in COM's multithreaded apartment, on which every COM object of the backend is created, called and released ([wasapi_apartment.h](../../src/wasapi_apartment.h)); it runs no application code and is joined when the context is destroyed.

  Core Audio, AAudio, the AudioWorklet and PipeWire run their own audio threads. libpipewire starts its data thread with its context; PipeWire's main loop runs on the host's thread inside the library's calls. Everything else (device changes, simulation, baking) runs on the host's threads or on its tasks.
- **Real-time.** The host's stream callback is the one application function the library calls on an audio thread, and it is declared real-time. [Real-time rules](realtime.md) says what that binds the library to.
- **Memory.** The owner objects are the context, the spatializer, an acoustic scene and each standalone effect or processor, each with the caller's allocator. A context allocates its tables when it is made, sized by `maudLimits`; a stream allocates everything it will use when it is made. Nothing allocates on the audio thread.
- **Stale ids.** A correct program never uses a device, stream, source or probe set id after the object is gone, but ids reach the host through notifications that may be read late, so a stale id is not misuse: it returns `maud_errorStale` and counts nothing. Invalid arguments and calls back in from host code the object runs count one misuse.
- **Untrusted input.** HRTF files and bakes are hostile bytes: every count is checked against a limit and the checksum verified before anything is allocated, and both loaders are fuzzed. Device names and keys the platform reports are cut to the context's text limit without splitting a UTF-8 sequence.
- **Dependencies.** The C library and libm; per backend, the platform's audio API: `ole32` and `avrt` on Windows; libpipewire, libpulse and libasound on Linux, each opened at run time with `dlopen`; CoreAudio and AudioToolbox on macOS; AVFAudio and AudioToolbox on iOS; AAudio on Android (API 30 and later) with the library's Java class for what AAudio lacks; Web Audio with an AudioWorklet in a browser.
- **Public API.** No engine concept is named in it (worlds, entities, assets, mixers, buses, emitters): any C or C++ program uses the library, a game, an editor or a voice application. `tools/check_litmus.py` refuses such a name in the public headers.

## The Device part

The context ([context_core.h](../../src/context_core.h)) composes its def, its backend, its device table, its stream table and its notification queue. A backend implements [backend.h](../../src/backend.h): it opens and closes its connection, takes in the platform's reports on the host's drain, settles each stream's format or refuses it, and attaches a platform stream to a stream slot. The context and stream modules validate everything the caller passes before a backend sees it; a backend only reports devices ([device.h](../../src/device.h)) and settles what a device can do.

Keeping streams on their devices is the core's job, not a backend's ([follow.h](../../src/follow.h)): when a backend reports that a default changed, the core moves every stream following it; when a device goes, it suspends the streams made on it. [Devices](devices.md) covers the device table, the drain and suspension; [Streams](streams.md) covers what happens between a platform's callback and the host's.

## The Spatial part

The effects are standalone objects with no shared state: an HRTF set, a binaural effect per source, the bed's encoder and rotation (stateless), decoders, panners, a direct effect per source, a reverb per listener. The spatializer drives them from geometry, with a simulation side on the host's threads that publishes whole steps and a rendering side on the audio thread that latches them. [Spatial rendering](spatial-rendering.md) explains the effects, [Propagation](propagation.md) the spatializer, scenes and bakes, and [File formats](file-formats.md) the two file formats.

## What lives where

- [Glossary](glossary.md).
- [Real-time rules](realtime.md): what runs on the audio thread, and how host code that calls back in is refused.
- [Devices](devices.md) and [Streams](streams.md): the Device part's core.
- [Backends](backends.md) and [The web](web.md): what each platform does that the code alone does not explain.
- [Voice processing](voice-processing.md): the four processors.
- [Spatial rendering](spatial-rendering.md), [Propagation](propagation.md), [File formats](file-formats.md): the Spatial part.
- [Maul conventions](maul-conventions.md): the family's rules on naming, errors, memory, threads and the ABI.

[tools/modules.txt](../../tools/modules.txt) states which module may use which, and `tools/check_modules.py` enforces it.
