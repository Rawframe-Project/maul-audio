# Maul Audio guide

Maul Audio moves audio between a host and the platform's devices, and
renders sound in space. This guide walks through the Device part: a
context and its devices, a stream, the notifications that keep a host
current, the offline backend tests run on, and the voice processors.
`docs/api.md` lists every function; each header says what its
functions take and return.

## The ground rules

- **Results.** Every call that can fail returns a `maudResult`, marked
  so that a compiler warns when it is dropped; `maudResultName` names
  one. Nothing is printed or logged.
- **Defs.** Each object is made from a def that `maudDefault...Def`
  returns, carrying a cookie that catches a def built by hand. Change
  the fields you need and pass it on.
- **Handles.** A context and the standalone processors are pointers.
  Devices and streams are ids with a generation: an id whose object is
  gone gives `maud_errorStale`, never another object.
- **Threads.** A context is used by one thread at a time. A stream's
  callback runs on an audio thread and must not allocate, lock or
  wait; the library holds itself to the same rule there.
- **Memory.** A context allocates once, at its creation, through the
  def's allocator, sized by the def's limits.

## Opening a context

```c
maudContextDef def = maudDefaultContextDef();   // the platform's backend
maudContext* context = NULL;
maudResult made = maudCreateContext(&def, &context);
if (made != maud_success)
{
    // maud_errorUnsupported: no backend runs here, such as a Linux
    // machine without an audio service.
}
```

The native backend is the platform's: PipeWire, then PulseAudio, then
ALSA on Linux; WASAPI on Windows; Core Audio on macOS and iOS; AAudio on
Android; Web Audio in a browser. `maudGetContextBackend` says which
one opened. `maudDestroyContext` stops and destroys its streams.

## Devices

```c
maudDeviceId ids[16];
uint32_t count = 0;
if (maudGetDevices(context, maud_directionOutput, ids, 16, &count) == maud_success)
{
    for (uint32_t i = 0; i < count && i < 16; ++i)
    {
        char name[256];
        size_t length = 0;
        maudDeviceInfo info;
        if (maudGetDeviceName(context, ids[i], name, sizeof(name), &length) == maud_success &&
            maudGetDeviceInfo(context, ids[i], &info) == maud_success)
        {
            // name holds length bytes, not terminated; info has the native
            // rate and layout, the form (speakers, headphones, ...), and
            // whether it is a default.
        }
    }
}
```

A count larger than the room given says how many there are.
`maudGetDeviceKey` gives a key that survives restarts where the
platform allows, for a host to remember a choice;
`maudGetDefaultDevice` gives the default of a direction for a role
(general or communications).

## Playing a stream

A stream calls the host back once per period with frames to fill,
interleaved, in the stream's layout:

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
    result = maudStartStream(context, stream);
}
maudStreamFormat format;
if (result == maud_success && maudGetStreamFormat(context, stream, &format) == maud_success)
{
    // format.sampleRate and format.periodFrames: what the callback sees.
}
```

A stream on the null device follows the default: when the default
moves, so does the stream. One opened on a named device stays there and
is suspended if the device goes. A stream never resamples silently: it
runs at the device's rate unless the def asks for another
(`maud_rateRequired` refuses a rate the device does not run;
`maud_ratePlatformConverted` lets the platform convert, where it can).
`maudGetStreamClock` maps a frame to the host's clock, with the
latency, for A/V sync.

## Keeping current

Device changes arrive as notifications, which the host drains on its
own thread, once a frame for a game:

```c
maudNotification note;
while (maudNextNotification(context, &note) == maud_success)
{
    switch (note.kind)
    {
    case maud_notifyDeviceAdded:
    case maud_notifyDeviceRemoved:
    case maud_notifyDefaultChanged:
        // refresh a device menu
        break;
    case maud_notifyStreamSuspended:
        // note.reason: the device was lost, there is none, or the platform
        // holds audio (a browser before a gesture, an iOS interruption)
        break;
    default:
        break;
    }
}
```

On backends whose platform reports through a loop the library owns
(PipeWire), the drain also takes in what the platform reported and
reconnects to a service that restarted, so a host that drains regularly
keeps the device table current.

## The offline backend

The offline backend plays to no hardware: the host renders a stream on
its own thread, at a clock it drives. A context starts with one output
and one input, and the host adds, removes and sets default devices to
script changes. Tests and offline rendering use it:

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
    result = maudAddOfflineDevice(context, &deviceDef, &speakers);
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
    result = maudStartStream(context, stream);
}
float frames[2 * 480];
if (result == maud_success)
{
    result = maudRenderStream(context, stream, frames, 480);   // 10 ms
}
```

`maudFeedStream` feeds an input stream the same way.

## Voice processing

A capture stream can ask the platform for its voice processing
(`def.voice`), and the status reports which parts are actually on,
since platforms ignore requests. Where there is none, the library's own
processors run on the host's frames, each standalone, in any chunk
size, without a context:

```c
maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();   // 48 kHz, mono
maudNoiseSuppressor* suppressor = NULL;
maudResult result = maudCreateNoiseSuppressor(&def, &suppressor);
maudNoiseState state;
if (result == maud_success)
{
    result = maudSuppressNoise(suppressor, frames, frameCount, &state);   // in place
}
// state.speechProbability; the output lags the input by 10 ms.
maudDestroyNoiseSuppressor(suppressor);
```

The echo canceller takes the capture and what was played, and runs in
place of the noise suppressor:

```c
maudEchoCancellerDef def = maudDefaultEchoCancellerDef();   // 48 kHz, a 0.2 s path
maudEchoCanceller* canceller = NULL;
maudResult result = maudCreateEchoCanceller(&def, &canceller);
if (result == maud_success)
{
    result = maudCancelEcho(canceller, capture, played, frameCount, NULL);   // in place
}
maudDestroyEchoCanceller(canceller);
```

The voice detector (`maudDetectVoice`) and the gain control
(`maudApplyGainControl`) follow the same shape.

## Building and using it

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build --prefix /usr/local
```

A host finds the package with `find_package(maul-audio)` and links
`maul-audio::maul-audio`; `-DMAUL_AUDIO_SPATIAL=OFF` builds the Device
part alone. `samples/devices.c` puts the calls above together.
