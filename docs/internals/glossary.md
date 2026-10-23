# Glossary

The words the code, the headers and these pages use, in the sense they use them.

## Devices and streams

- **Backend.** The code that talks to one platform API: PipeWire, PulseAudio, ALSA, WASAPI, Core Audio, iOS, AAudio, the web, the offline backend, or a private one. A context has exactly one.
- **Native backend.** The platform's best backend that answers, asked for with `maud_backendNative`; a context reports the one it chose.
- **Offline backend.** The backend with no hardware: the host renders and feeds streams on its own thread and scripts the devices.
- **Context.** The Device part's root object: one connection to a backend, its device table, its streams and its notification queue.
- **Device.** An endpoint of one direction (output or input) as the backend lists it. Its **key** survives unplugging and restarts; its **id** does not.
- **Default.** The device a platform currently routes a direction to, per **role**: general (games, media) or communications (calls).
- **Stream.** One flow of frames to or from a device, with the host's callback. **Output**, **input**, **duplex** (both in one callback), **loopback** (what an output plays, captured).
- **Frame.** One sample per channel, interleaved. A **period** is the fixed number of frames a callback gets; a **block** is one callback's frames as `maudStreamBlock` describes them.
- **Callback mode.** The platform's audio thread, or the library's, runs the stream. **Pull mode**: the host's thread renders through the library; only the offline backend has it.
- **Following.** A stream made on the null device follows the default of its direction and role: a default change **moves** it.
- **Suspension.** A started stream that cannot run, with a reason (`maudSuspendReason`). It renders nothing until it **resumes**.
- **Drain.** `maudNextNotification`: reading the queue, and on some backends taking in the platform's reports and retrying failed reopens first.
- **Notification.** A typed record of a change in the queue. **Overflow**: records the full queue dropped, counted in one record.
- **Underrun** and **overrun** (together **xruns**). An output played without the stream's frames; an input lost frames before the stream got them.
- **Slip.** A duplex stream on two clocks dropping input frames or filling silence to keep its ring near two periods.
- **Exclusive.** A stream that keeps every other stream and application off its device.
- **Object stream.** An output stream that hands mono objects with positions to the platform's renderer next to its ordinary output, the **bed**.
- **Host clock.** `maudGetHostNanoseconds`: the monotonic clock streams are stamped with. A **stream clock** maps one of a stream's frames to it, with the **latency**.
- **Focus.** The platform's notion of whose turn it is to play (Android, iOS).

## The Spatial part

- **Listener frame.** +x right, +y up, -z ahead, in metres. **World**: the host's coordinates, right-handed with +y up.
- **HRTF set.** Head-related transfer functions: per measured direction, one response per ear and its onset **delay**, on elevation **rings** of evenly spaced azimuths.
- **Binaural.** Rendered for the two ears through an HRTF set. The **near field**: how the head changes a source closer than the set's measuring distance.
- **Band.** One of three frequency bands every Spatial effect works in: up to 800 Hz, 800 Hz to 8 kHz, above 8 kHz.
- **Bed.** An ambisonic sound field of **order** 1 to 3, (order + 1)^2 planar channels in ACN order with SN3D weights (AmbiX). **Encoding** adds a source into it; **rotating** turns it; a **decoder** renders it for the ears or a speaker layout.
- **Panner.** Vector base amplitude panning of a source straight into a speaker layout's channels.
- **Direct effect.** What the straight path does to a source: air absorption, occlusion, transmission, directivity.
- **Occlusion.** How much of a source obstacles hide, 0 to 1. **Volumetric** occlusion samples points in a sphere around the source.
- **Transmission.** What passes through the surfaces a path crosses, per band.
- **Directivity.** How a source's level depends on the angle it faces away from the listener.
- **Reverb.** The late, diffuse part of a room's sound, rendered by a feedback delay network. Its **tail** is a second, slower slope.
- **Reflections.** The early part, from a response the spatializer builds from traced energy and convolves with the host's send.
- **Spatializer.** The Spatial part's root object. Its **simulation side** runs **steps** on the host's threads and publishes them; its **rendering side** **latches** the newest step on the audio thread.
- **Source.** A spatializer's emitter of sound: a pose, a directivity and an occlusion method.
- **Hooks.** Host functions a spatializer calls: the **any-hit** and **closest-hit** ray queries, and the **task hooks** that spread work over the host's task system.
- **Scene.** The library's own geometry and ray tracer: static meshes, and **instances** of instancing meshes that take effect at a **commit**.
- **Material.** A surface's absorption and transmission per band and its scattering.
- **Estimate.** Reverberation times and levels fitted from rays traced from the listener.
- **Energy field.** Traced energy per band in 10 ms bins, on spherical harmonics of its direction of arrival; the source of the reflections' response.
- **Probe set.** Points in walkable space and the **links** between those one ray connects. **Pathing** searches it for a way around an obstacle; a **bake** stores an estimate at every probe.

## The family

- **Def.** The struct an object is made from, with a **cookie** that marks it as built by its default. A **root def** (an object made without a parent) also carries the **ABI stamp**, the version of the headers the program was built with.
- **Misuse.** A call an object refuses because a correct program never makes it, counted on the object.
