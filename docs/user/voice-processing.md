# Voice processing

A voice application needs the microphone's signal cleaned: what the loudspeaker played taken back out, the room's noise lowered, the level evened, and silence told from speech. Maul Audio offers two ways: the platform's own processing on a capture stream, and four processors of its own that run on any frames, on any platform.

## The platform's processing

An input or duplex stream asks for it in its def's `voice` flags: `maud_voiceEchoCancellation`, `maud_voiceNoiseSuppression` and `maud_voiceGainControl`, in any combination. Platforms take such a request as a hint. Read what actually runs from the stream's status: `voiceReported` says whether the platform has said, and `voiceActive` which parts it reported on. `maud_voiceNone`, the default, asks the platform to leave the signal alone where it can, which a music or measurement application wants.

## The library's processors

Each processor stands alone: no context, no stream, its own def and allocator. It takes interleaved float frames in any count, so it runs in a stream's callback or anywhere else, and its output does not depend on how the frames are cut. Processing is real-time safe. Each is used by one thread at a time.

| Processor | What it does | Delay |
|---|---|---|
| Voice detector (`maudVoiceDetector_Detect`) | Decides, every 10 ms, whether the frames hold speech, with a probability, the level and the noise floor. A hangover keeps it active through short pauses. | None |
| Gain control (`maudGainControl_Apply`) | Brings speech to a target level, changing the gain slowly and only on evidence of speech, never raising the noise above a ceiling, and limits peaks to -1 dBFS. | None |
| Noise suppressor (`maudNoiseSuppressor_Suppress`) | Removes rumble below speech, then lowers each frequency by how likely it holds only noise, down to a floor. | 10 ms |
| Echo canceller (`maudEchoCanceller_Cancel`) | Takes what was played out of the capture, and the noise with it. | Two blocks of 8 ms or more |

A noise suppressor:

```c
maudNoiseSuppressorDef def = maudDefaultNoiseSuppressorDef();   // 48 kHz, mono
maudNoiseSuppressor* suppressor = NULL;
maudResult result = maudCreateNoiseSuppressor(&def, &suppressor);
maudNoiseState state;
if (result == maud_success)
{
    result = maudNoiseSuppressor_Suppress(suppressor, frames, frameCount, &state);   // in place
}
// state.speechProbability; the output lags the input by 10 ms.
maudDestroyNoiseSuppressor(suppressor);
```

The echo canceller takes the capture and what was played, both mono at one rate:

```c
maudEchoCancellerDef def = maudDefaultEchoCancellerDef();   // 48 kHz, a 0.2 s path
maudEchoCanceller* canceller = NULL;
maudResult result = maudCreateEchoCanceller(&def, &canceller);
if (result == maud_success)
{
    result = maudEchoCanceller_Cancel(canceller, capture, played, frameCount, NULL);   // in place
}
maudDestroyEchoCanceller(canceller);
```

## Putting them together

- Run the echo canceller **instead of** the noise suppressor, not before it: it suppresses noise itself, and two suppressors in a row take too much.
- Give the echo canceller the played signal **aligned** with the capture. The streams' clocks (`maudStream_GetClock`) give both latencies; a duplex stream delivers its input in step with its output, which makes the alignment the difference of the two latencies. Its def's `tailSeconds` is the longest echo path it learns, 0.2 s by default and up to 1 s.
- When the far end plays in another application, capture it with a **loopback** stream on the output device and use that as the played signal. PipeWire, PulseAudio, WASAPI and the web have loopback streams.
- Put the gain control last, after the echo is out, so it does not raise the echo with the speech.
- The echo canceller's state reports `leakage`: high while it learns or after the echo path changes (the user moved, the volume changed), low once it has adapted.

The noise suppressor takes rates that are a multiple of 100 from 8,000 to 384,000, the others any rate in that range. The detector listens to the mean of the channels, and the gain control and suppressor apply one gain to all of them.
