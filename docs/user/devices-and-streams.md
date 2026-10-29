# Devices and streams

A context connects to one backend and holds the streams opened through it. A stream moves interleaved 32-bit float frames to or from a device, one fixed period at a time, through a callback the program gives it. Device changes arrive as notifications the program drains on its own thread.

## Contents

- [Contexts and backends](#contexts-and-backends)
- [Devices](#devices)
- [Playing a stream](#playing-a-stream)
- [Rates, periods and layouts](#rates-periods-and-layouts)
- [Following the default](#following-the-default)
- [Keeping current](#keeping-current)
- [Why a stream is suspended](#why-a-stream-is-suspended)
- [Timing and synchronization](#timing-and-synchronization)
- [Capture, duplex and loopback](#capture-duplex-and-loopback)
- [Exclusive streams](#exclusive-streams)
- [The host's lifecycle and audio focus](#the-hosts-lifecycle-and-audio-focus)
- [Object streams](#object-streams)

## Contexts and backends

`maud_backendNative`, the default, opens the platform's backend: PipeWire, then PulseAudio, then ALSA on Linux; WASAPI on Windows; Core Audio on macOS and on iOS; AAudio on Android; Web Audio in a browser. A build with a private backend tries it first. `maudContext_GetBackend` says which one opened. When none answers (a Linux machine with no sound server and no ALSA), creation returns `maud_errorUnsupported`.

`maud_backendOffline` plays to no hardware; see [Offline rendering](offline-rendering.md).

The context def's limits (streams, the longest period, devices, notifications, the bytes of a device's name) size everything the context will hold; it allocates them once, when it is made. Each limit has a ceiling in `context.h` (`MAUD_MAX_STREAMS`, `MAUD_MAX_PERIOD_FRAMES`, `MAUD_MAX_DEVICES`, `MAUD_MAX_NOTIFICATIONS`, `MAUD_MAX_DEVICE_TEXT_BYTES`), and a def past one is refused with `maud_errorLimit`. A request past a limit is refused with `maud_errorCapacity`. `maudDestroyContext` stops and destroys every stream it still holds.

## Devices

```c
maudDeviceId ids[16];
uint32_t count = 0;
if (maudContext_GetDevices(context, maud_directionOutput, ids, 16, &count) == maud_success)
{
    for (uint32_t i = 0; i < count && i < 16; ++i)
    {
        char name[256];
        size_t length = 0;
        maudDeviceInfo info;
        if (maudDevice_GetName(context, ids[i], name, sizeof(name), &length) == maud_success &&
            maudDevice_GetInfo(context, ids[i], &info) == maud_success)
        {
            // name holds length bytes, not terminated; info has the native
            // rate and layout, the form (speakers, headphones, ...), and
            // whether it is a default.
        }
    }
}
```

A count larger than the room given says how many devices there are. Names and keys are UTF-8 with a length and no terminator.

- `maudContext_GetDefaultDevice` gives the default device of a direction for a role: `maud_roleGeneral` for games and media, `maud_roleCommunications` for calls.
- `maudDevice_GetKey` gives a key that stays the same when the device is unplugged and plugged in again, and across restarts where the platform allows. Store the key to remember a user's choice; an id does not survive the device leaving.
- `maudDeviceInfo` reports the native layout and rate (zero where the platform cannot tell without opening the device, as on ALSA), the rates the device takes without conversion, its form (speakers, headphones, a headset, a digital connector), and what the platform's own spatializer does there.

## Playing a stream

The callback gets one period at a time, already cleared to silence, and fills it:

```c
static void Play(const maudStreamBlock* block, void* user)
{
    Synth* synth = user;   // the host's own state
    uint32_t channels = maudGetLayoutChannelCount(block->layout);
    for (uint32_t i = 0; i < block->frameCount; ++i)
    {
        float value = NextSample(synth);
        for (uint32_t c = 0; c < channels; ++c)
        {
            block->output[i * channels + c] = value;
        }
    }
}
```

```c
maudStreamDef def = maudDefaultStreamDef();   // output, stereo, native rate, the default
def.callback = Play;
def.user = &synth;
maudStreamId stream = {0, 0};
maudResult result = maudCreateStream(context, &def, &stream);
if (result == maud_success)
{
    result = maudStream_Start(context, stream);
}
maudStreamFormat format;
if (result == maud_success && maudStream_GetFormat(context, stream, &format) == maud_success)
{
    // format.sampleRate and format.periodFrames: what the callback sees.
}
```

The callback runs on an audio thread with a deadline. It must not allocate, lock, wait or call anything that might. Of the context it may only read the stream's format, position and clock, read the host clock and the misuse count, and render or feed another offline stream; any other call that changes the context is refused with `maud_errorBusy` and counted as misuse. `maudCreateStream` allocates everything the stream will use, so starting it allocates nothing.

`maudDestroyStream` returns after the callback has run for the last time, so the program may free what `user` points to as soon as it returns.

## Rates, periods and layouts

A stream never resamples silently. Its def's rate policy decides:

- `maud_rateNative`, the default: the device's own rate. Read it from the format.
- `maud_rateRequired`: the def's rate, which the device must run at without conversion, or creation is refused. On PipeWire every device runs at the graph's rate.
- `maud_ratePlatformConverted`: the def's rate, converted by the platform's own converter where it has one.

The callback always gets exactly the stream's period, whatever sizes the platform moves; `periodFrames` 0 asks for the backend's default. Channels are in the order WAV files use (the Windows speaker mask order) on every platform; `maudGetLayoutSpeaker` and `maudGetLayoutSpeakerPosition` say which speaker each channel feeds and where it stands.

## Following the default

A stream made on the null device follows the default of its direction and role: when the default moves, the stream moves with it (`maud_notifyStreamMoved`) and, under `maud_rateNative`, takes the new device's rate (`maud_notifyStreamFormatChanged`). A stream made on a named device stays there; when the device goes, the stream is suspended with `maud_suspendDeviceLost` until it is destroyed.

## Keeping current

Changes arrive in the context's notification queue. Drain it on the program's own thread, once a frame in a game:

```c
maudNotification note;
while (maudContext_NextNotification(context, &note) == maud_success)
{
    switch (note.kind)
    {
    case maud_notifyDeviceAdded:
    case maud_notifyDeviceRemoved:
    case maud_notifyDefaultChanged:
        // refresh a device menu
        break;
    case maud_notifyStreamSuspended:
        // note.reason: the device was lost, there is none, the platform
        // holds audio (a browser before a gesture, an iOS interruption),
        // or it could not open the stream there (retried until it can)
        break;
    default:
        break;
    }
}
```

The drain does more than read the queue. On PipeWire and PulseAudio it takes in what the sound server reported since the last call and reconnects to a server that restarted; on every backend it tries again to open streams the platform failed to open. A program that never drains keeps stale device lists and leaves such streams waiting.

When the queue is full, the records that do not fit are counted and one `maud_notifyOverflow` says how many were lost: query the devices and the streams' statuses again. The queue's size is the context def's `limits.notifications`.

Across a restart of the audio service, devices keep their ids while they come back within a few seconds, and running streams are suspended with `maud_suspendPlatform` until they play on the new service. A device the new service does not bring back is removed.

## Why a stream is suspended

`maudStream_GetStatus` gives a stream's state: started or not, the device it is on, and why it cannot run.

| Reason | What happened | What ends it |
|---|---|---|
| `maud_suspendDeviceLost` | The device the stream was made on went away, or a browser refused to play on it. | Nothing: make a new stream. |
| `maud_suspendNoDevice` | The stream follows the default and its direction has no device. | A device appearing. |
| `maud_suspendPolicy` | The platform holds audio until the user acts: a browser before a gesture, an iOS interruption. | `maudContext_Unlock` from a gesture's handler, or the interruption ending. |
| `maud_suspendPermission` | The platform has not granted the device: a browser before the user allows the microphone. | The user granting it. |
| `maud_suspendHost` | The program suspended the context. | `maudContext_SetSuspended(context, false)`. |
| `maud_suspendPlatform` | The platform failed to open the stream on its device: the device refused the format, or the service is restarting. | The library tries again after a wait that grows to a few seconds, from the drain. |

Each change of state posts `maud_notifyStreamSuspended` or `maud_notifyStreamResumed`. A suspended stream that was started renders nothing until it resumes; the status also counts underruns and overruns the platform revealed.

## Timing and synchronization

`maudStream_GetPosition` counts the frames a stream has moved. `maudStream_GetClock` maps one of them to the host clock (`maudGetHostNanoseconds`) with the latency between the callback and the device, as the platform last reported it at a callback; a frame's time follows at the stream's rate. Use it to line audio up with video or input. On the offline backend the host time is 0, since the caller's clock is the only one.

## Capture, duplex and loopback

- **Input** streams deliver frames in `block->input`. An input or duplex stream may ask for the platform's voice processing (`def.voice`); the status says which parts the platform reports active, since platforms ignore requests. See [Voice processing](voice-processing.md).
- **Duplex** streams capture and play in one callback, on a device of each direction (`def.device` and `def.inputDevice`). The input arrives in step with the output. On one clock nothing drifts; on two, the input waits in a ring held near two periods, and frames dropped or filled with silence are counted in the status (`slippedFrames`). A duplex stream keeps the one rate it opened at: when its output follows the default to a device at another rate, the platform converts it and its policy reads `maud_ratePlatformConverted`.
- **Loopback** streams capture what an output device plays, as an input stream's blocks: the reference an echo canceller needs when the far end plays in another application. PipeWire, PulseAudio, WASAPI and the web have them; ALSA, Core Audio, iOS and Android refuse them with `maud_errorUnsupported`. On the web the user picks what to share in the browser's picker, so the stream is made in a gesture's handler. On Windows its blocks pause while the device plays nothing.

## Exclusive streams

`maud_shareExclusive` asks for a device for one stream alone: WASAPI's and AAudio's exclusive modes, Core Audio's hog mode, an ALSA hardware device. It needs a named device, never falls back to shared, plays at the device's own rate, and is refused with `maud_errorUnsupported` where the backend or device cannot give it (PipeWire, PulseAudio, iOS, the web, the offline backend, ALSA's default device, duplex streams) and with `maud_errorPlatform` while another application holds the device.

## The host's lifecycle and audio focus

The library never guesses that the program went to the background. Call `maudContext_SetSuspended(context, true)` when it does (a hidden tab, a backgrounded application, the system going to sleep): every stream is suspended with `maud_suspendHost` and the platform side stops. Call it with `false` when the program is back.

Browsers hold audio until the user acts, and iOS can hold it after an interruption: call `maudContext_Unlock` from a user gesture's handler, such as a click's.

On platforms with audio focus (Android with the Java half, iOS), `maudContext_RequestFocus` asks for the program's turn to play, and `maud_notifyFocusChanged` reports the state (held, lost, paused, ducked). The library pauses and ducks nothing itself: the program does what the focus asks.

## Object streams

An output stream with `objectCount` above 0 places mono objects around the listener next to its ordinary output, the bed, for the platform's renderer to spatialize: Windows' spatial sound objects, Apple's spatial mixer. A device's `spatializer` and `spatialObjectCount` say what the platform takes there. The program still applies what the path does to each source (distance, occlusion, air) to the object's frames; only the binaural or panning step moves to the platform. Where the backend has no object renderer, such a stream is refused with `maud_errorUnsupported`. A stream takes at most `MAUD_MAX_STREAM_OBJECTS` objects; more is refused with `maud_errorLimit`.

Streams run in callback mode on every native backend; pull mode, where the program's own thread renders, is the offline backend's.
