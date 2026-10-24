# Documentation

## Using the library

- [Getting started](user/getting-started.md): requirements, building, installing, build options, the model the API follows.
- [Devices and streams](user/devices-and-streams.md): contexts, devices, playing and capturing, following the default, notifications, suspension, timing, duplex, loopback, exclusive and object streams.
- [Offline rendering](user/offline-rendering.md): rendering without hardware, and scripting device changes for tests.
- [Voice processing](user/voice-processing.md): the platform's processing and the library's detector, gain control, noise suppressor and echo canceller.
- [Spatial audio](user/spatial-audio.md): HRTF sets, the binaural effect, the ambisonic bed, speakers, the direct effect and the reverb.
- [Environments](user/environments.md): the spatializer, scenes, reverberation, reflections, probe sets, paths and bakes.
- [Platforms](user/platforms.md): what each platform needs, and what to check when there is no sound.
- [Private backends](user/private-backends.md): building a backend kept outside the library.
- [API reference](https://rawframe-project.github.io/maul-audio/api.html), generated from the headers.

## How it works

- [Overview](internals/overview.md): the library profile and how the parts fit.
- [Glossary](internals/glossary.md).
- [Real-time rules](internals/realtime.md), [Devices](internals/devices.md), [Streams](internals/streams.md), [Backends](internals/backends.md), [The web](internals/web.md), [Voice processing](internals/voice-processing.md).
- [Spatial rendering](internals/spatial-rendering.md), [Propagation](internals/propagation.md), [File formats](internals/file-formats.md).
- [Maul conventions](internals/maul-conventions.md): the rules every Maul library follows.

## Maintaining it

- [Development](operations/development.md): toolchain, building and testing, generated data, benchmarks, fuzzing.
- [Checks](operations/checks.md): the local tiers, the CI jobs and their budgets.
- [Test environments](operations/test-environments.md): the sound servers, Wine, simulators, emulators and browsers the tests run against.
- [Device checklist](operations/device-checklist.md): what is checked by hand on real hardware before a release.
