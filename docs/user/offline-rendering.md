# Offline rendering

The offline backend plays to no hardware. The program renders each stream on its own thread at a clock it drives, and scripts the devices: it adds and removes them, moves defaults and changes their formats, and the context reacts as it would to the platform. It is how a program tests its audio code without a sound card, renders to a file faster than real time, or runs on a server.

The same inputs give the same samples on one platform and build. The offline backend is in every build.

## Rendering

An offline context starts with one stereo output and one stereo input at the def's `offlineSampleRate` (48,000 by default), each the default of its direction. Streams on it run in pull mode: `maudStream_Render` calls the callback once per period, on the calling thread, until it has filled the frames asked for.

```c
maudContextDef def = maudDefaultContextDef();
def.backend = maud_backendOffline;
maudContext* context = NULL;
maudResult result = maudCreateContext(&def, &context);
maudDeviceId speakers = {0, 0};
maudOfflineDeviceDef deviceDef = maudDefaultOfflineDeviceDef();   // stereo, 48 kHz
deviceDef.name = "Speakers";
deviceDef.nameLength = 8;
if (result == maud_success)
{
    result = maudCreateOfflineDevice(context, &deviceDef, &speakers);
}
maudStreamDef streamDef = maudDefaultStreamDef();
streamDef.mode = maud_modePull;   // the host's thread renders
streamDef.callback = Play;
streamDef.user = &synth;
maudStreamId stream = {0, 0};
if (result == maud_success)
{
    result = maudCreateStream(context, &streamDef, &stream);
}
if (result == maud_success)
{
    result = maudStream_Start(context, stream);
}
float frames[2 * 480];
if (result == maud_success)
{
    result = maudStream_Render(context, stream, frames, 480);   // 10 ms
}
```

The frame count of a call need not be a multiple of the period: the stream keeps what is left of a period for the next call. Its position and clock advance by exactly the frames rendered.

## Capture and loopback

`maudStream_Feed` hands frames to an input stream, and a loopback stream takes what its device played the same way:

```c
maudStreamDef loopbackDef = maudDefaultStreamDef();
loopbackDef.direction = maud_directionLoopback;   // what the speakers play
loopbackDef.device = speakers;
loopbackDef.mode = maud_modePull;
loopbackDef.callback = Reference;
loopbackDef.user = &reference;
maudStreamId loopback = {0, 0};
if (result == maud_success)
{
    result = maudCreateStream(context, &loopbackDef, &loopback);
}
if (result == maud_success)
{
    result = maudStream_Start(context, loopback);
}
if (result == maud_success)
{
    result = maudStream_Feed(context, loopback, frames, 480);   // what they played
}
```

An object stream renders with `maudStream_RenderObjects`, the caller standing in for the platform's renderer: it gets the bed and each object's frames with the position and gain the callback gave it.

## Scripting devices

| Call | What the platform would have done |
|---|---|
| `maudCreateOfflineDevice` | A device was plugged in. It becomes the default if its direction had none, and streams waiting for one move to it. |
| `maudDestroyOfflineDevice` | A device was unplugged. Streams following the default move; streams made on it are suspended. |
| `maudDevice_SetOfflineDefault` | The user picked another default. |
| `maudDevice_SetOfflineForm` | Headphones were plugged into the device's jack. |
| `maudDevice_SetOfflineFormat` | The user picked another format in the system's settings. |
| `maudDevice_SetOfflineSpatializer` | The user turned a platform spatializer on. |

Each posts the notifications a real change would, in the same order, so a program's handling of device churn can be tested on every platform. An offline device runs at any rate from 8,000 to 384,000 without conversion; there is no platform converter, so `maud_ratePlatformConverted` is refused.

## Threads

Rendering and feeding are real-time safe: they allocate, lock and wait for nothing. A stream renders on one thread at a time, and a stream's own callback may render or feed another offline stream but not itself. Control calls (creating, starting, scripting devices) go on the thread that uses the context, as on every backend.
