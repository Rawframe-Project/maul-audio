# Devices

How a context keeps its device table and its streams in line with what the platform reports, and why a host sees changes only when it drains.

## The device table

A context's devices live in a fixed table of `limits.devices` slots, both directions together ([context_core.h](../../src/context_core.h)). A device id is a 1-based slot and a generation; a device that leaves frees its slot, and the next device to take it gets a new generation, so an old id reads `maud_errorStale`. A device that comes back is a new id: the **key** is what identifies it across leaving and returning, and what a backend matches on.

Backends report devices through [device.h](../../src/device.h) and nothing else: add, remove, set a default, change a form, a format or a spatializer state. Each report posts its notifications and keeps the streams on their devices in the same call, so the order a host reads is always the order things happened. A backend that rescans the whole list (ALSA, WASAPI, Core Audio, iOS, AAudio, the web) hands the scan to `maudSyncDevices`, which matches by direction and key, removes what is gone, adds what is new and reports what changed; a backend that gets one event per change (PipeWire, PulseAudio) reports each one.

Names and keys are cut to `limits.deviceTextBytes` without splitting a UTF-8 sequence ([test_utf8.c](../../test/test_utf8.c)).

## Following

[follow.h](../../src/follow.h) is the only code that moves streams. A stream is either bound to a named device or follows the default of its direction and role:

- When a default changes, every stream following it moves. Core Audio and WASAPI bind a platform stream to one device (`reopensOnMove`), so their streams are reopened on the new device; the other backends retarget the platform stream or let the platform move it. A native-rate stream on a device at another rate takes that rate, except a duplex stream, whose halves keep the rate they opened at and let the platform convert. A rate a backend reads from its platform passes through `maudDeviceRate` ([device.h](../../src/device.h)), which takes it only within the 8,000 to 384,000 a stream runs at, checked before it is converted; anything else reads as 0, a rate the device cannot tell, and leaves a native stream at its own. PipeWire's `clock.rate` and `clock.force-rate` settings are read the same way, an unusable one as none set.
- When a device goes, a stream made on it is suspended with `maud_suspendDeviceLost` for good, since the program named that device; a stream that followed it moves to the new default, or waits with `maud_suspendNoDevice` when there is none.
- When a device appears and its direction had none, it becomes the default for both roles, and waiting streams move to it.

A stream carries one reason at a time. Losing its device, or its direction having none, overrides whatever else held it. When the host, the platform's policy or a permission lets go, the stream falls back to what it still waits for, in that order (the host's suspension, the policy, the permission), or runs.

## The drain

`maudNextNotification` is where the platform's reports become the host's. Every backend that receives reports on a thread of the platform's (WASAPI's notification client, Core Audio's listeners, AAudio's error callback) only raises a flag or queues a record there; the drain takes them in on the host's thread, where the device table may change. PipeWire and PulseAudio go further: their main loops run only inside the drain, without blocking, so a context that is never drained never hears from its server. This keeps the device table single-threaded: it changes only on the thread that uses the context.

The drain also retries. A stream whose platform side failed to open (a device that refuses its format, a service that is restarting) is suspended with `maud_suspendPlatform`, and each drain tries it again once its wait is up ([retry.h](../../src/retry.h)): 250 ms after the first failure, doubling to 4 s, back to 250 ms after a success. A host that drains every frame therefore never repeats the platform's work every frame.

## The notification queue

The queue holds `limits.notifications` records in a ring, allocated with the context. It never grows and never drops silently: when one place is left, an overflow record takes it, and every record that does not fit after that only raises the overflow's count ([notify.h](../../src/notify.h)). A host that reads an overflow queries the devices and streams again; that is always enough, since every record describes state the host can read.

## Service restarts

PipeWire and PulseAudio can restart under a running program (an update, a crash, a user's `systemctl restart`). When the connection drops, the backend keeps its devices for ten seconds marked as away, suspends running streams with `maud_suspendPlatform`, and tries a new connection from the drain. A device the new server lists with the same key takes its old slot back, so its id still works and streams made on it resume; a device not listed within ten seconds is removed.

Core Audio reports a restart of `coreaudiod` through its listeners and the backend reopens its streams. WASAPI does not report a restart of the audio service at all: a stream's thread sees its client fail, the stream is suspended, and the drain reopens it with growing waits. ALSA has no service.

[The device checklist](../operations/device-checklist.md) lists which of these changes the tests drive and which a person checks on real hardware before a release.
