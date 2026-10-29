# Voice processing

The four processors in `maul-audio/voice.h` are for programs that capture speech where the platform offers no processing, or offers processing a program cannot rely on. Each is a standalone object with no context, works on any frame count with results that do not depend on how the frames are cut, and allocates only when it is made. They share small parts ([voice_dsp.h](../../src/voice_dsp.h): biquads, levels) and the detector's analysis ([vad_core.h](../../src/vad_core.h)).

The detector, gain control and suppressor follow the designs WebRTC's processing uses; the echo canceller was measured against Speex's on the AEC Challenge's synthetic clips. The source files name the papers each algorithm comes from.

## Frame independence

Every processor works in fixed frames internally (10 ms, or the echo canceller's blocks) and keeps what does not complete a frame for the next call. A host may therefore call it with whatever its stream delivers, and the output is the same as with any other cut. The tests feed the same input in different cuts and compare.

## The voice detector

[vad_core.c](../../src/vad_core.c) follows the classic band-energy design: six band levels from band-pass filters after a high-pass, each against its own noise floor, the minimum of its smoothed level over 1.5 s. A frame is speech when the weighted mean of the bands' margins over their floors passes a global threshold, or when one band above 250 Hz passes a local one (rumble lives below 250 Hz, and voice is never only there). The decision turns on after an onset and stays on for the hangover. `aggressiveness` raises the thresholds. The detector listens to the channels' mean.

## The gain control

[gain_control.c](../../src/gain_control.c) is an adaptive digital controller. At each 10 ms frame the detector's analysis says how confident it is of speech; the speech level is a weighted average over confident frames, reliable after twelve in a row. That level sets a target gain, capped so the noise floor stays under its ceiling, and the gain moves toward it by a bounded step per second, rising only after twelve confident frames in a row, so it never pumps up the noise between words. The gain ramps linearly across the next frame, and a peak limiter keeps every sample under -1 dBFS. It adds no delay: the gain follows what the frames before showed.

## The noise suppressor

[noise_suppressor.c](../../src/noise_suppressor.c) runs a high-pass biquad first, then frames of 20 ms under a square-root Hann window, 10 ms apart. The noise spectrum follows Gerkmann and Hendriks' unbiased MMSE estimate under a speech presence probability, which tracks changing noise within tens of milliseconds where a minimum-statistics search takes a second. Each frequency's gain is Cohen's optimally modified log-spectral amplitude estimate under that probability, floored at `floorDb`. The channels' mean power sets one gain per frequency for all channels, so a stereo image keeps its place. The output lags the input by one hop, 10 ms.

## The echo canceller

The echo canceller has a linear stage and a suppressor ([echo_canceller.c](../../src/echo_canceller.c)):

- **Before**: the capture through a DC notch, both signals through a pre-emphasis, whose coefficients are set in hertz so they mean the same at any rate.
- **The linear stage** ([echo_filter.h](../../src/echo_filter.h)): a multidelay block frequency-domain adaptive filter (Soo and Pang) over `tailTime` of echo path. Its step size per frequency follows the leakage of the echo into the error (Valin), proportionate across partitions. Two filters run: a background one adapts, a foreground one produces the output, and a statistical test hands the background's coefficients over when it does better or takes them back when it diverges. Double talk (both ends speaking) is what diverges a single adaptive filter; this keeps the output from following it.
- **The suppressor** ([echo_suppressor.h](../../src/echo_suppressor.h)): one gain per frequency against the noise and the residual echo the filter leaves, the residual estimated from the echo estimate's power and the leakage of its band, speech presence from smoothed per-band SNRs, and a log-spectral amplitude gain floored between a noise floor and an echo floor.

Blocks are the smallest power of two of frames lasting 8 ms or more: 128 at 16 kHz, 512 at 48 kHz. Shorter blocks at 48 kHz cost the suppressor more frequency resolution than the filter gained in tracking. The output lags the input by two blocks.

The canceller is not given the delay between what was played and what the microphone hears: the host aligns the two from the streams' clocks, and the filter's length absorbs what is left. It replaces the noise suppressor; running both suppresses twice.

## Real-time cost

At 48 kHz on one desktop core, noise suppression costs about 41 microseconds per 10 ms of mono audio and echo cancellation about 76 (see the README's cost table, from `bench/baseline.txt`). The transforms are the library's real FFT ([real_fft.h](../../src/real_fft.h)), whose tables live in memory the processor allocated, so a transform allocates nothing.
