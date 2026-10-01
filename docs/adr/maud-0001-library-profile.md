# maud-0001. Library profile

Status: Accepted

## Context

Every Maul library states in one record what its domain adds to the
family rulebook (family record 0005). Audio is presentation: what a
listener hears need not be bit-identical across machines. Baked
acoustic data is different, because programs cook it as content with
an identity, so its bytes must not depend on where it was baked.
Audio also runs on a thread with a deadline, which the rest of the
family does not have.

## Decision

- **Determinism:**
  - Rendered output (device blocks through the offline backend,
    spatializer output) is reproducible on one platform and build: the
    same inputs give the same samples, so golden tests compare exactly
    there, and across platforms within tolerances each test states.
  - Baked data is bit-exact on every supported platform, compiler,
    architecture and worker count: identical inputs and library
    version give identical bytes.
  - Simulation results (occlusion, reflections, paths) do not depend
    on worker count or scheduling: rays are generated and batched in a
    fixed order, and every reduction has a fixed order.
  - The family's floating-point rules apply: no fast math and no
    implicit contraction; fused multiply-add only where the SIMD
    header spells it.
- **Threads:** the library starts one thread, and only in one case: a
  stream in callback mode on a backend whose platform calls no audio
  thread of its own (WASAPI, ALSA, PulseAudio, and PipeWire when the
  host does not drive it). That thread runs the stream's period loop
  at the platform's real-time priority and calls only the host's
  real-time callback. It starts when the stream starts and is joined
  when the stream stops or is destroyed, so no such thread outlives
  its context. A stream in pull mode starts none: the host's thread
  waits on it and processes each period. Everything else (device
  changes, simulation, baking) runs on the host's threads or on tasks
  its task hooks run (family record 0017).
- **The real-time callback:** the host's stream callback is the one
  application function the library calls on an audio thread, and it is
  declared real-time. Library code on an audio thread, the platform's
  or its own, never allocates, frees, locks, blocks in a system call,
  waits on another thread, or logs. It reads only what was published
  to it and publishes only into bounded queues; a full queue is a
  counted, typed failure. Debug and test builds trap allocation and
  locking on the audio thread.
- **Memory:** the owner objects are the context (the Device part's
  root) and the spatializer (the Spatial part's root), each created
  with the caller's allocator. Streams, sources, listeners, scenes and
  probe sets allocate what they need when they are created, sized by
  named limits; nothing is allocated on the audio thread. Objects the
  audio thread may still read are retired through a fence and freed by
  the thread that owns them. Loaded data (HRTF sets, baked data, scene
  files) is never trusted for a size before it is checked.
- **Platform dependencies:** the C library and libm; per backend, the
  platform's audio APIs: WASAPI and the Windows multimedia class
  scheduler (`ole32`, `avrt`); PipeWire, PulseAudio and ALSA on Linux,
  each loaded at run time so a program runs where one is missing;
  CoreAudio and AudioToolbox on macOS; AVFAudio and AudioToolbox on
  iOS; AAudio on Android (API 30 and later); Web Audio with an
  AudioWorklet on the web.
- **Commit areas:** `aaudio`, `alsa`, `ambisonics`, `api`, `bake`,
  `bench`, `build`, `capture`, `ci`, `coreaudio`, `device`, `docs`,
  `hrtf`, `occlusion`, `offline`, `panning`, `pipewire`, `pulse`,
  `reverb`, `samples`, `spatial`, `tests`, `tools`, `voice`, `wasapi`,
  `web`.

## Consequences

A host gets the same bake bytes on every machine and the same samples
from the same inputs on one build, and can keep its own thread in
charge of the real-time loop where the platform allows. The library
pays with a fixed ray order in every simulation and with the
instrumentation that proves the audio thread stays within its rules.
