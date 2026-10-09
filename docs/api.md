# Maul Audio API reference

Generated from the public headers by `tools/gen_api.py`. The headers
are the source of truth; this file mirrors them.

## `base.h`

The base of the Maul Audio API: the library version, the export and attribute macros, the result codes every fallible function returns, and the allocator owner objects take.

```c
maudVersion maudGetVersion(void);
```
Returns the version of the library that was linked, which may differ from the MAUD_VERSION macros a program was compiled with.  @return The library version. @par Thread safety Safe from any thread.

```c
const char* maudResultName(maudResult result);
```
Returns the name of a result code, for diagnostics.  @param result  Any value; an unknown one is named as such. @return A static, NUL-terminated string such as "maud_errorCapacity". @par Thread safety Safe from any thread.

## `ambisonics.h`

The ambisonic bed: sources encoded into a sound field of order 1 to 3, the field rotated, in planar channels the host owns, and decoded for both ears. Channels follow AmbiX: ACN order with SN3D normalization, the field's axes x ahead, y left and z up; directions and rotations are given in the listener's frame (maudVector3) and converted. Encoding, rotating and decoding allocate nothing and do work in proportion to their frames.

```c
uint32_t maudGetAmbisonicChannelCount(uint32_t order);
```
Returns the channels a bed of an order takes.  @param order  1 to MAUD_MAX_AMBISONIC_ORDER. @return (order + 1) squared, or 0 for an order out of range. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetAmbisonicGains(uint32_t order, maudVector3 direction, float* gainsOut);
```
Computes the gains that encode a direction: one per channel of the order, in ACN order with SN3D normalization.  @param order      1 to MAUD_MAX_AMBISONIC_ORDER. @param direction  The direction, in the listener's frame. @param gainsOut   Receives the order's channel count of gains. @return `maud_success`, or `maud_errorInvalid` for an order out of range, a NULL pointer or a direction that is not finite. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudEncodeAmbisonic(uint32_t order, const maudPanSource* from, const maudPanSource* to, const float* in, float* const* bed, uint32_t frames);
```
Adds a mono source into a bed, its direction and gain moving linearly from `from` to `to` across the call: frame n is encoded with the gains (n + 1) / frames of the way.  @param order   The bed's order, 1 to MAUD_MAX_AMBISONIC_ORDER. @param from    The source at the previous call's end. @param to      The source at this call's end. @param in      frames samples. @param bed     The order's channel count of channels, frames each, added to. @param frames  The frames. @return `maud_success`, or `maud_errorInvalid` for an order out of range, a NULL pointer or a value that is not finite; nothing is written then. @par Thread safety Safe from any thread; the bed is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudRotateAmbisonic(uint32_t order, const maudQuaternion* from, const maudQuaternion* to, float* const* bed, uint32_t frames);
```
Rotates a bed in place, the rotation moving linearly from `from` to `to` across the call (a crossfade of the two rotated fields; a large change within one call blends rather than turns). A rotation turns the field in the listener's frame: a sound from direction d comes out from the rotated d.  @param order   The bed's order, 1 to MAUD_MAX_AMBISONIC_ORDER. @param from    The rotation at the previous call's end. @param to      The rotation at this call's end. @param bed     The order's channel count of channels, frames each. @param frames  The frames. @return `maud_success`, or `maud_errorInvalid` for an order out of range, a NULL pointer, or a quaternion of zero length or not finite; nothing is written then. @par Thread safety Safe from any thread; the bed is used by one thread at a time.

```c
maudBinauralDecoderDef maudDefaultBinauralDecoderDef(void);
```
Returns the default binaural decoder def: no set, order 3, at most 1,024 frames per call.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateBinauralDecoder(const maudBinauralDecoderDef* def, maudBinauralDecoder** decoderOut);
```
Creates a binaural decoder: fits filters to the set by magnitude least squares (least squares below 1.5 kHz, magnitudes above), each 2 ms long and delaying the field by 0.67 ms. The fit is the heavy part, done here and never while decoding; its working memory comes from the def's allocator and goes back before this returns.  @param def         The def, from maudDefaultBinauralDecoderDef, with a set. @param decoderOut  Receives the decoder; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def without its cookie, without a set or out of range, or a set whose directions cannot carry the order; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroyBinauralDecoder(maudBinauralDecoder* decoder);
```
Destroys a binaural decoder. NULL is ignored.  @param decoder  The decoder. @par Thread safety Safe from any thread; the decoder is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudDecodeBinaural(maudBinauralDecoder* decoder, const float* const* bed, float* const out[2], uint32_t frames);
```
Decodes frames of a bed for both ears, written to out[0] (left) and out[1] (right).  @param decoder  The decoder. @param bed      The decoder's order's channel count of channels, frames each. @param out      Two channels of frames samples each. @param frames   0 to the def's maxFrames; 0 does nothing. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or too many frames; nothing is written then. @par Thread safety Safe from any thread; the decoder is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudResetBinauralDecoder(maudBinauralDecoder* decoder);
```
Forgets the bed a decoder has heard.  @param decoder  The decoder. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread; the decoder is used by one thread at a time.

```c
maudSpeakerDecoderDef maudDefaultSpeakerDecoderDef(void);
```
Returns the default speaker decoder def: stereo, order 3.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateSpeakerDecoder(const maudSpeakerDecoderDef* def, maudSpeakerDecoder** decoderOut);
```
Creates a speaker decoder by all-round ambisonic decoding: the bed decoded to 240 near-uniform virtual speakers with max-rE weights, each panned to the layout as a speaker panner pans, folded into one matrix. A source encoded into the bed reaches the speakers with unit energy on average over the sphere, as one panned to them does; the low-frequency channel gets nothing.  @param def         The def, from maudDefaultSpeakerDecoderDef. @param decoderOut  Receives the decoder; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def without its cookie, an order out of range or a layout the library does not have; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroySpeakerDecoder(maudSpeakerDecoder* decoder);
```
Destroys a speaker decoder. NULL is ignored.  @param decoder  The decoder. @par Thread safety Safe from any thread; the decoder is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetSpeakerDecoderMatrix(const maudSpeakerDecoder* decoder, float* matrixOut);
```
Copies the decoder's matrix: the layout's channel count of rows, each the order's channel count of weights, row-major. Speaker s plays the sum over c of matrix[s][c] times bed channel c.  @param decoder    The decoder. @param matrixOut  Receives the matrix. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudDecodeToSpeakers(const maudSpeakerDecoder* decoder, const float* const* bed, float* const* out, uint32_t frames);
```
Decodes frames of a bed to the layout's channels, written over out. The decoder holds no state between calls.  @param decoder  The decoder. @param bed      The decoder's order's channel count of channels, frames each. @param out      The layout's channel count of channels, frames each. @param frames   The frames; 0 does nothing. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer; nothing is written then. @par Thread safety Safe from any thread; the output is used by one thread at a time.

## `binaural.h`

Binaural effects: one per source, rendering its mono signal for both ears through a loaded HRTF set (maul-audio/hrtf.h). Processing runs on whatever thread the host renders on: it allocates nothing, takes no lock and does work in proportion to its frames.

```c
maudBinauralDef maudDefaultBinauralDef(void);
```
Returns the default binaural def: no set, at most 1,024 frames per call, the near field on for a head of 8.75 cm.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateBinaural(const maudBinauralDef* def, maudBinaural** effectOut);
```
Creates a binaural effect, its memory taken once from the def's allocator.  @param def      The def, from maudDefaultBinauralDef, with a set. @param effectOut  Receives the effect; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a def without its cookie, without a set or out of range; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroyBinaural(maudBinaural* effect);
```
Destroys a binaural effect. NULL is ignored.  @param effect  The effect. @par Thread safety Safe from any thread; the effect is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudProcessBinaural(maudBinaural* effect, const maudBinauralParams* params, const float* in, float* const out[2], uint32_t frames);
```
Renders frames of a source's mono signal for both ears, written to out[0] (left) and out[1] (right). When the position changes, the old and new responses are crossfaded while each ear's delay and near-field filter move, over 2.67 ms; a change during a fade starts when it ends, the latest one winning. The first call after creation or a reset starts at its parameters.  @param effect  The effect. @param params  The source's parameters. @param in      frames samples. @param out     Two channels of frames samples each, apart from in. @param frames  0 to the def's maxFrames; 0 does nothing. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer, too many frames or a parameter that is not finite; nothing is written then. @par Thread safety Safe from any thread; the effect is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudResetBinaural(maudBinaural* effect);
```
Forgets the signal an effect has heard and its parameters, as for a source that starts again.  @param effect  The effect. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread; the effect is used by one thread at a time.

## `buffer.h`

Conversion between the two buffer layouts of the API: interleaved frames, which streams carry, and one array per channel, which the spatializer works on. Both hold 32-bit float samples.

```c
MAUD_NODISCARD MAUD_API maudResult maudInterleave(const float* const* planar, uint32_t channelCount, uint32_t frameCount, float* interleavedOut);
```
Interleaves one array per channel into frames: sample `i` of channel `c` goes to `interleavedOut[i * channelCount + c]`. Samples are copied exactly. The arrays must not overlap.  @param planar          One array of `frameCount` samples per channel. May be NULL when `frameCount` is 0. @param channelCount    The number of channels, at least 1. @param frameCount      The number of samples in each channel. @param interleavedOut  Room for `channelCount * frameCount` samples. May be NULL when `frameCount` is 0. @return `maud_success`, or `maud_errorInvalid` for a channel count of 0, a NULL array where samples are due, or a total that does not fit in memory. @par Thread safety Real-time safe: no allocation, lock or wait.

```c
MAUD_NODISCARD MAUD_API maudResult maudDeinterleave(const float* interleaved, uint32_t channelCount, uint32_t frameCount, float* const* planarOut);
```
Splits frames into one array per channel: `interleaved[i * channelCount + c]` goes to sample `i` of channel `c`. Samples are copied exactly. The arrays must not overlap.  @param interleaved   `channelCount * frameCount` samples. May be NULL when `frameCount` is 0. @param channelCount  The number of channels, at least 1. @param frameCount    The number of frames. @param planarOut     One array with room for `frameCount` samples per channel. May be NULL when `frameCount` is 0. @return `maud_success`, or `maud_errorInvalid` for a channel count of 0, a NULL array where samples are due, or a total that does not fit in memory. @par Thread safety Real-time safe: no allocation, lock or wait.

## `context.h`

The Device part's root: a context holds the connection to a backend and the streams opened through it.

```c
maudContextDef maudDefaultContextDef(void);
```
Returns the default context def: 8 streams, periods of at most 8,192 frames, 32 devices, 256 notifications, 256 bytes of device name and key, the C library's allocator, the native backend and an offline rate of 48,000.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateContext(const maudContextDef* def, maudContext** contextOut);
```
Creates a context. An offline context starts with one output and one input device at the offline rate, stereo, each the default of its direction for every role. A native context on Linux connects to PipeWire, waiting up to two seconds on the calling thread for its device list. Either starts with an empty notification queue.  @param def         The def, from maudDefaultContextDef. @param contextOut  Receives the context; set to NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL argument, a def without its cookie, an allocator with one function, a limit of 0 or an offline rate out of range; `maud_errorUnsupported` when this build or platform lacks the backend asked for, or no audio service of it answers; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudDestroyContext(maudContext* context);
```
Destroys a context and every stream it still holds.  @param context  The context. NULL does nothing. @return `maud_success`, or `maud_errorState` when called on a thread that is rendering one of its streams; the context is then left as it was and the call counts as misuse. @par Thread safety Safe from any thread; the context is used by one thread at a time. No stream of the context may be rendering on another thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudResumeContext(maudContext* context);
```
Asks the platform to let the context's audio run. Browsers hold audio until the user acts: call it from a user gesture's handler, such as a click's. On iOS an interruption that ended without the hint to resume holds audio until this is called. Streams held meanwhile are suspended with maud_suspendPolicy and resume, with a notification, once the platform lets the context run. Where no policy holds audio it does nothing.  @param context  The context. @return `maud_success`; `maud_errorInvalid` for a NULL context; `maud_errorState` when called on a thread that is rendering one of the context's streams, which counts as misuse. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSetContextSuspended(maudContext* context, bool suspended);
```
Suspends a context for the host's lifecycle, or resumes it: the application went to the background, its tab was hidden or the system sleeps, and later it is back. The library never infers this itself. Suspending suspends every stream that runs or only waits to run with maud_suspendHost, with a notification each, and stops its platform side; on the web it also suspends the AudioContext. Resuming lets each run again, or wait for what it waited for. A stream that lost its device keeps that reason. Calling it again the same way does nothing.  @param context    The context. @param suspended  Whether the host is suspended. @return `maud_success`; `maud_errorInvalid` for a NULL context; `maud_errorState` when called on a thread that is rendering one of the context's streams, which counts as misuse. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
maudBackendKind maudGetContextBackend(const maudContext* context);
```
Returns the backend a context talks to: the one its def named, or for maud_backendNative the one it chose.  @param context  The context. @return The backend kind, never maud_backendNative. @par Thread safety Safe from any thread.

```c
uint64_t maudGetContextMisuse(const maudContext* context);
```
Returns how many calls the context refused as misuse: invalid arguments against it, and control calls made on a thread that was rendering one of its streams. Stale ids are not misuse.  @param context  The context. @return The count since the context was created. @par Thread safety Safe from any thread.

## `device.h`

Devices: the endpoints a context's backend offers, the default for each role, and what each device runs at natively.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetDevices(const maudContext* context, maudDirection direction, maudDeviceId* idsOut, uint32_t capacity, uint32_t* countOut);
```
Lists the devices of one direction, in a stable order.  @param context     The context. @param direction   Output or input devices. @param idsOut      Receives up to capacity ids. May be NULL when capacity is 0. @param capacity    The room in idsOut. @param countOut    Receives the number of devices, which may exceed capacity. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer where one is required or an unknown direction. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetDeviceInfo(const maudContext* context, maudDeviceId device, maudDeviceInfo* infoOut);
```
Reports what a device is and runs at.  @param context  The context. @param device   The device. @param infoOut  Receives the information. @return `maud_success`; `maud_errorStale` for an id that names no device; `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetDeviceName(const maudContext* context, maudDeviceId device, char* bytesOut, size_t capacity, size_t* lengthOut);
```
Copies a device's display name, UTF-8, without a terminating NUL.  @param context    The context. @param device     The device. @param bytesOut   Receives the name. May be NULL when capacity is 0. @param capacity   The room in bytesOut. @param lengthOut  Receives the name's length in bytes. @return `maud_success`; `maud_errorCapacity` when the name does not fit, with nothing copied and lengthOut set; `maud_errorStale`; `maud_errorInvalid` for a NULL pointer where one is required. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetDeviceKey(const maudContext* context, maudDeviceId device, char* bytesOut, size_t capacity, size_t* lengthOut);
```
Copies a device's persistent key, UTF-8, without a terminating NUL: the same for the same device after it is unplugged and plugged again, and across restarts where the platform allows, so a host may store it.  @param context    The context. @param device     The device. @param bytesOut   Receives the key. May be NULL when capacity is 0. @param capacity   The room in bytesOut. @param lengthOut  Receives the key's length in bytes. @return `maud_success`; `maud_errorCapacity` when the key does not fit, with nothing copied and lengthOut set; `maud_errorStale`; `maud_errorInvalid` for a NULL pointer where one is required. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetDefaultDevice(const maudContext* context, maudDirection direction, maudDeviceRole role, maudDeviceId* deviceOut);
```
Reports the default device of a direction for a role.  @param context      The context. @param direction    Output or input. @param role         The role. @param deviceOut    Receives the device; the null id when there is none. @return `maud_success`; `maud_empty` when the direction has no device; `maud_errorInvalid` for a NULL pointer or an unknown direction or role. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `direct.h`

The direct effect: what the direct path does to a source before it is spatialized, in three bands (up to 800 Hz, 800 Hz to 8 kHz, above 8 kHz). Air absorption over distance, a directivity pattern, occlusion and transmission through what blocks the path; distance attenuation is the host's own curve, given as a gain. One effect per source, mono in and mono out, ahead of the binaural effect or a speaker panner.

```c
maudDirectEffectDef maudDefaultDirectEffectDef(void);
```
Returns the default direct effect def: 48 kHz, air absorption of ISO 9613 part 1 at 20 degrees C, 50 % humidity and 101.325 kPa.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
maudDirectParams maudDefaultDirectParams(void);
```
Returns the default direct params: gain 1, no distance, a clear path, full transmission and directivity.  @return The params. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetAirAbsorption(float temperatureCelsius, float humidityPercent, float* absorptionOut);
```
Computes air absorption per band from ISO 9613 part 1 at 101.325 kPa: the standard's attenuation averaged in dB over each band, as an amplitude exponent per metre.  @param temperatureCelsius  -20 to 50. @param humidityPercent     Relative humidity, 10 to 100. @param absorptionOut       Receives three exponents. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or a value out of range. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetDirectivity(const maudDirectivityPattern* pattern, maudVector3 toListener, float* directivityOut);
```
Computes a pattern's directivity towards the listener.  @param pattern         The pattern. @param toListener      The direction from the source to the listener in the source's own frame, whose forward axis is -z; a zero vector counts as straight ahead. @param directivityOut  Receives three gains. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer, a weight outside 0 to 1, a power below 0 or a value that is not finite. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateDirectEffect(const maudDirectEffectDef* def, maudDirectEffect** effectOut);
```
Creates a direct effect.  @param def        The def, from maudDefaultDirectEffectDef. @param effectOut  Receives the effect; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def without its cookie, a rate out of range or air absorption below 0 or not finite; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroyDirectEffect(maudDirectEffect* effect);
```
Destroys a direct effect. NULL is ignored.  @param effect  The effect. @par Thread safety Safe from any thread; the effect is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudProcessDirect(maudDirectEffect* effect, const maudDirectParams* params, const float* in, float* out, uint32_t frames);
```
Processes frames, in place allowed. The gain and the band filters move from the previous call's params to these across the call; the first call after creation or a reset starts at these. Band shapes are refitted only when a band moves by 0.05 dB or more.  @param effect  The effect. @param params  This call's params. @param in      frames samples. @param out     frames samples. @param frames  The frames; 0 does nothing. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer, a value that is not finite, a negative gain or distance, or an occlusion, transmission or directivity outside 0 to 1; nothing is written then. @par Thread safety Safe from any thread; the effect is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudResetDirectEffect(maudDirectEffect* effect);
```
Forgets the signal and the params an effect has seen.  @param effect  The effect. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread; the effect is used by one thread at a time.

## `focus.h`

Audio focus: asking the platform for the application's turn to play, and the state of that turn as the platform reports it. The library acts on none of it; the host pauses, ducks or stops its streams.

```c
MAUD_NODISCARD MAUD_API maudResult maudRequestFocus(maudContext* context, maudFocusRequest request, maudDeviceRole role);
```
Asks the platform for audio focus, or gives it back. Ask right before playing; on Android an application targeting Android 15 may ask only while it is the top application or runs a foreground service. The state that follows comes as maud_notifyFocusChanged records and from maudGetContextFocus; a request the platform delays, as during a call, is held once the platform grants it. Releasing reports maud_focusNone. The library never pauses or ducks streams for focus: the host does.  @param context  The context. @param request  What to ask for, or maud_focusRelease. @param role     What the audio is for: maud_roleGeneral for media and games, maud_roleCommunications for a call. @return `maud_success` when granted, delayed or released; `maud_errorPlatform` when the platform refuses; `maud_errorUnsupported` where the backend has no audio focus (the desktop platforms, the web, the offline backend, and Android without the Java half); `maud_errorInvalid` for a NULL context, an unknown request or role; `maud_errorState` when called on a thread that is rendering one of the context's streams, which counts as misuse. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetContextFocus(const maudContext* context, maudFocus* focusOut);
```
Reads where the context stands with audio focus, as of the last drain of its notifications.  @param context   The context. @param focusOut  Receives the state. @return `maud_success`; `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `hrtf.h`

Head-related transfer function sets, loaded from the bytes of a .maudhrtf file (docs/hrtf-format.md) for the binaural renderer. The library reads no files: the host hands over the bytes, which the loader treats as hostile.

```c
maudHrtfDef maudDefaultHrtfDef(void);
```
Returns the default HRTF def: no bytes, the file's own rate, at most 65,536 directions and 1,024 taps.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudLoadHrtf(const maudHrtfDef* def, maudHrtf** hrtfOut);
```
Loads an HRTF set from a .maudhrtf file's bytes, checking every count, size and the checksum before it allocates or trusts anything.  @param def      The def, from maudDefaultHrtfDef. @param hrtfOut  Receives the set; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def without its cookie or out of range, or bytes that are not a well-formed file (a wrong magic, size, ring layout, text or checksum); `maud_errorUnsupported` for a format version this library does not read; `maud_errorCapacity` when the set passes the def's limits or the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroyHrtf(maudHrtf* hrtf);
```
Destroys an HRTF set. NULL is ignored.  @param hrtf  The set. @par Thread safety Safe from any thread; the set is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetHrtfInfo(const maudHrtf* hrtf, maudHrtfInfo* infoOut);
```
Reports what an HRTF set holds.  @param hrtf     The set. @param infoOut  Receives the information. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread; the set is used by one thread at a time.

## `layout.h`

Channel layouts: which speaker each channel of an interleaved frame feeds, and where that speaker stands.

```c
uint32_t maudGetLayoutChannelCount(maudChannelLayout layout);
```
Returns the number of channels in a layout.  @param layout  A layout. @return The channel count, or 0 for `maud_layoutNone` and values that name no layout. @par Thread safety Safe from any thread.

```c
maudSpeaker maudGetLayoutSpeaker(maudChannelLayout layout, uint32_t channel);
```
Returns the speaker one channel of a layout feeds.  @param layout   A layout. @param channel  The channel's index in an interleaved frame. @return The speaker, or `maud_speakerNone` when the layout names no layout or the channel is out of its range. @par Thread safety Safe from any thread.

```c
maudSpeakerPosition maudGetLayoutSpeakerPosition(maudChannelLayout layout, uint32_t channel);
```
Returns the nominal position of the speaker one channel of a layout feeds.  @param layout   A layout. @param channel  The channel's index in an interleaved frame. @return The position. It is zero for the low-frequency channel, which has no direction, and when the layout names no layout or the channel is out of its range. @par Thread safety Safe from any thread.

## `notification.h`

The context's notification queue: typed records of what changed in its devices and streams, drained by the host off the audio thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudNextNotification(maudContext* context, maudNotification* notificationOut);
```
Takes the oldest notification from the context's queue. On native backends whose platform reports changes through a loop the library owns, such as PipeWire, it first takes in what the platform reported since the last call, without blocking, and reconnects to a platform service that went away; a host that drains the queue regularly keeps the device table current.  @param context          The context. @param notificationOut  Receives the record. @return `maud_success`; `maud_empty` when the queue is drained; `maud_errorInvalid` for a NULL pointer; `maud_errorState` on a thread rendering one of the context's streams. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `objects.h`

Object streams: an output stream whose callback places mono objects around the listener beside the stream's ordinary output, the bed, for a platform renderer to spatialize (Windows' spatial sound objects, Apple's spatial mixer). A device's spatializer and spatialObjects say what the platform takes there. The host still computes what the path does to each source (distance, occlusion, air) and applies it to an object's frames; only the binaural or panning step moves to the platform.

```c
MAUD_NODISCARD MAUD_API maudResult maudRenderObjects(maudContext* context, maudStreamId stream, float* bedOut, maudStreamObject* objectsOut, uint32_t objectCount, uint32_t frameCount);
```
Renders the next frames of an offline object stream to the caller, which stands in for the platform's renderer: the bed into bedOut and each object's frames into the buffer its samples point to, with the position, gain and activity the callback last gave it. The block offers every object (objectsAvailable is the stream's objectCount). The stream's clock advances by frameCount.  @param context      The context. @param stream       A started, running object stream of an offline context. @param bedOut       Room for frameCount interleaved frames of the stream's layout. May be NULL when frameCount is 0. @param objectsOut   objectCount records, each samples pointing to room for frameCount frames (or NULL when frameCount is 0); the rest is written. @param objectCount  The stream's objectCount. @param frameCount   The number of frames. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL pointer where frames are due, a stream without objects, another object count or a total that does not fit in memory; `maud_errorUnsupported` on a context that is not offline; `maud_errorState` for a stopped or suspended stream or one already rendering. @par Thread safety Real-time safe: no allocation, lock or wait. A stream renders on one thread at a time.

## `offline.h`

The offline backend's scripted devices: a host adds and removes devices and sets defaults, and the context reacts as it would to the platform, so device changes can be tested without hardware.

```c
maudOfflineDeviceDef maudDefaultOfflineDeviceDef(void);
```
Returns the default offline device def: an output, stereo, at 48,000, of unknown form, with an empty name and key.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudAddOfflineDevice(maudContext* context, const maudOfflineDeviceDef* def, maudDeviceId* deviceOut);
```
Adds a device to an offline context. If its direction had no device it becomes the default for both roles, and streams waiting for one move to it.  @param context      An offline context. @param def          The def, from maudDefaultOfflineDeviceDef. @param deviceOut    Receives the device's id; the null id on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer where one is required, a def without its cookie, or a value out of range; `maud_errorCapacity` past the device limit or the name and key limit; `maud_errorUnsupported` on a context that is not offline; `maud_errorState` on a thread rendering one of the context's streams. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudRemoveOfflineDevice(maudContext* context, maudDeviceId device);
```
Removes a device from an offline context, as an unplug would. A default it was passes to the first remaining device of its direction. Streams on it move or are suspended.  @param context  An offline context. @param device   The device. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL context; `maud_errorUnsupported` on a context that is not offline; `maud_errorState` on a thread rendering one of the context's streams. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSetOfflineDefaultDevice(maudContext* context, maudDeviceRole role, maudDeviceId device);
```
Makes a device the default of its direction for a role. Streams that follow that default move to it.  @param context  An offline context. @param role     The role. @param device   The device. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL context or an unknown role; `maud_errorUnsupported` on a context that is not offline; `maud_errorState` on a thread rendering one of the context's streams. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSetOfflineDeviceForm(maudContext* context, maudDeviceId device, maudDeviceForm form);
```
Changes what a device leads to, as plugging headphones into its jack does: a change posts maud_notifyRouteChanged.  @param context  An offline context. @param device   The device. @param form     Its new form. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL context or an unknown form; `maud_errorUnsupported` on a context that is not offline; `maud_errorState` on a thread rendering one of the context's streams. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSetOfflineDeviceSpatializer( maudContext* context, maudDeviceId device, maudPlatformSpatializer spatializer, bool headTracking, uint32_t objects);
```
Changes what a platform spatializer does on an output device, as a user turning one on does: a change posts maud_notifySpatializerChanged. Offline outputs start with none.  @param context       An offline context. @param device        An output device. @param spatializer   Its new state. @param headTracking  Whether it can follow the listener's head. @param objects       The positioned objects it takes; 0 unless on. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL context, an unknown state, objects while not on, or an input device; `maud_errorUnsupported` on a context that is not offline; `maud_errorState` on a thread rendering one of the context's streams. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `reverb.h`

The parametric reverb: a feedback delay network whose decay follows reverberation times given per band (up to 800 Hz, 800 Hz to 8 kHz, above 8 kHz). It takes the host's mono reverb send and adds a diffuse tail into a first-order ambisonic bed, which the binaural and speaker decoders render. Where a coupled space decays in two slopes, a reverb made with a tail renders the slower one too. One per listener; it allocates nothing once made.

```c
maudReverbDef maudDefaultReverbDef(void);
```
Returns the default reverb def: 48 kHz, no input delay.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateReverb(const maudReverbDef* def, maudReverb** reverbOut);
```
Creates a reverb, silent.  @param def        The def, from maudDefaultReverbDef. @param reverbOut  Receives the reverb; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def without its cookie, a rate or longest delay out of range; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroyReverb(maudReverb* reverb);
```
Destroys a reverb. NULL is ignored.  @param reverb  The reverb. @par Thread safety Safe from any thread; the reverb is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudProcessReverb(maudReverb* reverb, const maudReverbParams* params, const float* in, float* const* bed, uint32_t frames);
```
Runs frames of the send through the reverb, adding its tail into a first-order bed (four channels, ACN order, SN3D), the send delayed and leveled per band first. When the times or levels change they move to the new ones across the call (a new delay takes effect at once); refitting the filters then takes some tens of microseconds on the calling thread, bounded and without allocation.  @param reverb  The reverb. @param params  This call's params. @param in      frames samples of the send. @param bed     Four channels of frames samples, added to. @param frames  The frames; 0 does nothing. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or a time, level or delay out of range or not finite (a tail's too); nothing is written then. @par Thread safety Safe from any thread; the reverb is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudResetReverb(maudReverb* reverb);
```
Silences a reverb's tail.  @param reverb  The reverb. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread; the reverb is used by one thread at a time.

## `scene.h`

Acoustic scenes: the library's own geometry and ray tracer, for hosts without one and for work whose results must match on every platform. A scene's static meshes are fixed when it is made; instances of its instancing meshes are made, moved and destroyed at any time and take effect at a commit. Between commits any number of threads query it at once; changes and commits must not overlap queries. Its query functions have the spatializer's hook signatures, the scene as their context.

```c
maudAcousticSceneDef maudDefaultAcousticSceneDef(void);
```
Returns the default acoustic scene def: no meshes, no instances, 16,777,216 triangles at most.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateAcousticScene(const maudAcousticSceneDef* def, maudAcousticScene** sceneOut);
```
Creates an acoustic scene: copies the meshes and builds a bounding volume hierarchy over their triangles, the same on every platform.  @param def       The def, from maudDefaultAcousticSceneDef. @param sceneOut  Receives the scene; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def without its cookie, a mesh with a NULL array it needs, a vertex index past its vertices or a vertex that is not finite; `maud_errorCapacity` for more than 16,777,216 triangles in all or when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroyAcousticScene(maudAcousticScene* scene);
```
Destroys an acoustic scene. NULL is ignored. No query may be running on it.  @param scene  The scene. @par Thread safety Safe from any thread.

```c
void maudSceneAnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded, void* scene);
```
Answers any-hit queries against a scene (a maudAnyHitFn): a ray hits when a triangle lies within [minDistance, maxDistance] along it. A ray through an edge or vertex shared by triangles hits.  @param rays      count rays. @param count     The rays. @param occluded  Receives 1 or 0 per ray. @param scene     The scene. @par Thread safety Safe from any thread.

```c
void maudSceneClosestHit(const maudRay* rays, uint32_t count, maudRayHit* hits, void* scene);
```
Answers closest-hit queries against a scene (a maudClosestHitFn): the nearest triangle within a ray's distances, equal distances going to the triangle listed first (meshes in order, then their triangles); its unit normal faces the side the triangle is wound counterclockwise from. A miss has distance INFINITY.  @param rays   count rays. @param count  The rays. @param hits   Receives a hit per ray. @param scene  The scene. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateSceneInstance( maudAcousticScene* scene, uint32_t mesh, const maudInstanceTransform* transform, maudSceneInstanceId* instanceOut);
```
Makes an instance of one of the scene's instancing meshes. It is part of queries from the next commit on.  @param scene        The scene. @param mesh         The instancing mesh's index in the def. @param transform    Where it is. @param instanceOut  Receives the instance; 0 on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a mesh out of range or a transform out of range or not finite; `maud_errorCapacity` when the scene has its capacity of instances. @par Thread safety Safe from any thread; the scene is used by one thread at a time. Changes and commits must not overlap its queries.

```c
MAUD_NODISCARD MAUD_API maudResult maudMoveSceneInstance(maudAcousticScene* scene, maudSceneInstanceId instance, const maudInstanceTransform* transform);
```
Moves an instance, from the next commit on.  @param scene      The scene. @param instance   The instance. @param transform  Where it is now. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a 0 or unknown id or a transform out of range or not finite; `maud_errorStale` for a destroyed instance's id. @par Thread safety Safe from any thread; the scene is used by one thread at a time. Changes and commits must not overlap its queries.

```c
MAUD_NODISCARD MAUD_API maudResult maudDestroySceneInstance(maudAcousticScene* scene, maudSceneInstanceId instance);
```
Destroys an instance; it leaves queries at the next commit.  @param scene     The scene. @param instance  The instance. @return `maud_success`, `maud_errorInvalid` for a NULL scene or a 0 or unknown id, or `maud_errorStale` for a destroyed instance's id. @par Thread safety Safe from any thread; the scene is used by one thread at a time. Changes and commits must not overlap its queries.

```c
MAUD_NODISCARD MAUD_API maudResult maudCommitAcousticScene(maudAcousticScene* scene);
```
Commits the instances' changes: the live instances, where they are now, are what queries see from here on. The top level is rebuilt over their boxes, the same on every platform.  @param scene  The scene. @return `maud_success`, or `maud_errorInvalid` for a NULL scene. @par Thread safety Safe from any thread; the scene is used by one thread at a time. Changes and commits must not overlap its queries.

## `spatializer.h`

The Spatial part's root. A spatializer has two sides. The simulation side, called from the host's own threads or tasks, makes sources, sets their poses and runs steps that work out what the direct path does to each source. The rendering side, on the audio thread, latches the newest step and reads each source's result without allocating, locking or waiting; a step is published whole, never torn. The host passes the results to the direct effect and to the binaural effect or a panner. Positions are in metres in the host's world, right-handed with +y up; an orientation turns the frame of the listener or a source (+x right, +y up, -z ahead) into the world's.

```c
maudSpatializerDef maudDefaultSpatializerDef(void);
```
Returns the default spatializer def: 256 sources, up to 64 occlusion points each, transmission paths of up to 4 surfaces, 64 materials, reverberation estimates of 2048 rays through air at 20 degrees and 50 % humidity, no geometric reflections (when asked for: 1 s at 48 kHz), no ray queries, no task hooks.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
maudSourceDef maudDefaultSourceDef(void);
```
Returns the default source def: omnidirectional, occlusion by one ray (for volumetric occlusion a sphere of 1 m with 32 points), transmission walked.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateSpatializer(const maudSpatializerDef* def, maudSpatializer** spatializerOut);
```
Creates a spatializer, allocating everything it will use.  @param def             The def, from maudDefaultSpatializerDef. @param spatializerOut  Receives the spatializer; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def without its cookie, a capacity or ray count out of range, an air absorption below 0 or not finite, a reflection order, duration or rate out of range (or reflections without reverberation rays), or one task hook without the other; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroySpatializer(maudSpatializer* spatializer);
```
Destroys a spatializer and its sources. NULL is ignored. Neither side may be in use.  @param spatializer  The spatializer. @par Thread safety Safe from any thread; the spatializer is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateSource(maudSpatializer* spatializer, const maudSourceDef* def, maudSourceId* sourceOut);
```
Creates a source, at the world's origin facing -z until its pose is set. It appears in results from the next step on.  @param spatializer  The spatializer. @param def          The def, from maudDefaultSourceDef. @param sourceOut    Receives the source; 0 on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def without its cookie, an invalid directivity pattern, an unknown occlusion method, or a volumetric radius or point count out of range; `maud_errorCapacity` when the spatializer has its capacity of sources. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudDestroySource(maudSpatializer* spatializer, maudSourceId source);
```
Destroys a source. Results from steps before this still hold it; later steps do not.  @param spatializer  The spatializer. @param source       The source. @return `maud_success`, `maud_errorInvalid` for a NULL spatializer or a 0 or unknown id, or `maud_errorStale` for a destroyed source's id. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSetSourcePose(maudSpatializer* spatializer, maudSourceId source, const maudPose* pose);
```
Sets a source's pose for the steps that follow.  @param spatializer  The spatializer. @param source       The source. @param pose         The pose. @return `maud_success`, `maud_errorInvalid` for a NULL pointer, a 0 or unknown id, a value that is not finite or a zero orientation, or `maud_errorStale` for a destroyed source's id. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSimulateDirect(maudSpatializer* spatializer, const maudPose* listener);
```
Runs a direct step for a listener and publishes its results. Its occlusion rays go to the any-hit query, and then the transmission walks of the sources it found occluded to the closest-hit query, a surface at a time, in a fixed order and in batches of at most 64, through the task hooks if set; no result depends on how the tasks split the work.  @param spatializer  The spatializer. @param listener     The listener's pose. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer, a value that is not finite or a zero orientation. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSetMaterials(maudSpatializer* spatializer, const maudAcousticMaterial* materials, uint32_t count);
```
Replaces the material table that hits' indices name. A hit on a material outside it lets nothing through.  @param spatializer  The spatializer. @param materials    count materials; NULL if count is 0. @param count        Up to the def's materialCapacity. @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a value outside 0 to 1; `maud_errorCapacity` for more than the capacity. Nothing changes on failure. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSimulateReverb(maudSpatializer* spatializer, const maudPose* listener);
```
Estimates the reverberation times at the listener's position from the scene: rays from the listener in batches of 64, each hit lit back toward it through the any-hit query, energy per band in 10 ms bins, a fit of the decay from -5 to -25 dB. The batches go through the task hooks if set; the times do not depend on how the tasks split them. Without a closest-hit query there is nothing to reflect and the times are 0.1 s. The next direct step publishes the times with its results. If the def asks for reflections, the same rays' energy, on spherical harmonics of its arrival direction, becomes a response (noise shaped per 10 ms and per band) published at once for maudRenderReflections.  @param spatializer  The spatializer. @param listener     The listener's pose. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a value that is not finite or a zero orientation; `maud_errorState` when the def asked for no estimates. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
uint64_t maudLatchResults(maudSpatializer* spatializer);
```
Latches the newest published step for the rendering side; until the next latch, results come from it.  @param spatializer  The spatializer. @return The step's number (the first step is 1), or 0 if none has been published or spatializer is NULL. @par Thread safety Real-time safe: no allocation, lock or wait. The rendering side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetDirectResult(const maudSpatializer* spatializer, maudSourceId source, maudDirectResult* resultOut);
```
Reads a source's result from the latched step.  @param spatializer  The spatializer. @param source       The source. @param resultOut    Receives the result. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a 0 or unknown id, or no latched step; `maud_errorStale` when the latched step does not hold the source (made after it, or destroyed before it). @par Thread safety Real-time safe: no allocation, lock or wait. The rendering side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudRenderReflections(maudSpatializer* spatializer, const maudQuaternion* orientation, const float* send, float* const* bed, uint32_t frames);
```
Renders geometric reflections: convolves the host's mono send with the newest response published, crossfading over 128 frames to a new one, and adds the result into a bed of the def's order (ACN, SN3D) in the listener's frame. The response is advanced by the convolution's 128-frame block, so the output does not lag.  @param spatializer  The spatializer. @param orientation  The listener's orientation at the call's end; the bed turns linearly from the last call's. @param send         frames samples. @param bed          (order + 1)^2 channels of frames samples, added to. @param frames       The frames; 0 does nothing. @return `maud_success`; `maud_errorInvalid` for a NULL pointer or an orientation of zero length or not finite; `maud_errorState` when the def asked for no reflections. @par Thread safety Real-time safe: no allocation, lock or wait. The rendering side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetReverbResult(const maudSpatializer* spatializer, maudReverbResult* resultOut);
```
Reads the reverberation estimate the latched step published.  @param spatializer  The spatializer. @param resultOut    Receives the result. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or no latched step. @par Thread safety Real-time safe: no allocation, lock or wait. The rendering side is used by one thread at a time.

```c
maudProbeSetDef maudDefaultProbeSetDef(void);
```
Returns the default probe set def: no points and an empty box (set one or the other), 2 m spacing, 1.5 m height, a 5 m range.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateProbeSet(maudSpatializer* spatializer, const maudProbeSetDef* def, maudProbeSetId* setOut);
```
Creates a probe set: the def's points, or probes generated over its box; then the links, a ray for each pair within range (every pair is linked without an any-hit query). The rays go through the task hooks; the same scene and def give the same set however they run.  @param spatializer  The spatializer. @param def          The def, from maudDefaultProbeSetDef. @param setOut       Receives the set; 0 on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def without its cookie, a value out of range or not finite, or neither points nor a box (one with height); `maud_errorState` to generate without a closest-hit query; `maud_errorCapacity` when the spatializer has its capacity of sets, past maxProbes or maxProbePairs, past 4,194,304 columns, or when memory runs out. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudDestroyProbeSet(maudSpatializer* spatializer, maudProbeSetId set);
```
Destroys a probe set.  @param spatializer  The spatializer. @param set          The set. @return `maud_success`, `maud_errorInvalid` for a NULL spatializer or a 0 or unknown id, or `maud_errorStale` for a destroyed set's id. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetProbeSet(const maudSpatializer* spatializer, maudProbeSetId set, maudProbeSetInfo* infoOut, uint32_t first, uint32_t count, maudVector3* points);
```
Reads what a probe set holds and, if points is not NULL, copies its probes from first on into points, count of them.  @param spatializer  The spatializer. @param set          The set. @param infoOut      Receives the counts. @param first        The first probe to copy. @param count        The probes to copy. @param points       Receives them, or NULL. @return `maud_success`; `maud_errorInvalid` for a NULL spatializer or info, a 0 or unknown id, or probes past the set's; `maud_errorStale` for a destroyed set's id. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSetPathing(maudSpatializer* spatializer, maudProbeSetId set);
```
Sets the probe set occluded sources look for paths over, from the next direct step on; 0 for none. Destroying the set ends its use.  @param spatializer  The spatializer. @param set          The set, or 0. @return `maud_success`; `maud_errorInvalid` for a NULL spatializer or an unknown id; `maud_errorStale` for a destroyed set's id. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudBakeProbeSet(maudSpatializer* spatializer, maudProbeSetId set);
```
Bakes a probe set: a reverberation estimate at every probe, as maudSimulateReverb makes one there, with the energy field when the spatializer renders reflections; it replaces an earlier bake. The rays go through the task hooks, and the bake's values are the same bits on every platform and however the tasks run. Its memory: 24 bytes a probe, and with reflections 12 times the bed's channels times the response's 10 ms bins in bytes more.  @param spatializer  The spatializer. @param set          The set. @return `maud_success`; `maud_errorInvalid` for a NULL spatializer or a 0 or unknown id; `maud_errorStale` for a destroyed set's id; `maud_errorState` without reverberation rays or a closest-hit query; `maud_errorCapacity` when memory runs out (an earlier bake is then gone). @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudUseBakedReverb(maudSpatializer* spatializer, maudProbeSetId set);
```
Sets the baked probe set reverberation estimates come from, from the next maudSimulateReverb on; 0 for none. An estimate then blends the 4 nearest probes the listener sees within the set's range (Shepard's weights, cut off at the fifth nearest, so moving is smooth) and traces nothing; with no probe in sight it traces. Destroying the set ends its use.  @param spatializer  The spatializer. @param set          The set, or 0. @return `maud_success`; `maud_errorInvalid` for a NULL spatializer or an unknown id; `maud_errorStale` for a destroyed set's id; `maud_errorState` for a set without a bake. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSaveProbeSet(const maudSpatializer* spatializer, maudProbeSetId set, void* bytes, size_t capacity, size_t* sizeOut);
```
Saves a probe set as a `.maudbake` file (docs/bake-format.md): its probes, links and bake. A set saves to the same bytes on every platform.  @param spatializer  The spatializer. @param set          The set. @param bytes        Receives the file, or NULL to ask its size. @param capacity     The bytes bytes holds. @param sizeOut      Receives the file's size. @return `maud_success`; `maud_errorInvalid` for a NULL spatializer or size, or a 0 or unknown id; `maud_errorStale` for a destroyed set's id; `maud_errorCapacity` when capacity is short of the size (nothing written) or the size does not fit in size_t. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudLoadProbeSet(maudSpatializer* spatializer, const void* bytes, size_t size, maudProbeSetId* setOut);
```
Loads a probe set from a `.maudbake` file, treated as hostile: every count is checked against the spatializer's limits before anything is allocated, and every value against its range. A file with a bake loads only if its fields match the spatializer's reflections (none, or the same order and length).  @param spatializer  The spatializer. @param bytes        The file. @param size         Its size in bytes. @param setOut       Receives the set; 0 on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a file not well formed; `maud_errorUnsupported` for another version or fields that do not match; `maud_errorCapacity` past maxProbes or maxProbePairs, with the spatializer's sets all taken or none asked for, or when memory runs out. @par Thread safety Safe from any thread; the simulation side is used by one thread at a time.

## `speakers.h`

Panning to speakers: a panner per channel layout, made once from the layout's nominal positions, pans sources by vector base amplitude panning (VBAP) into the layout's channels. Once made it is read-only: any number of sources share it, from any thread. Panning allocates nothing and does work in proportion to its frames.

```c
maudSpeakerPannerDef maudDefaultSpeakerPannerDef(void);
```
Returns the default speaker panner def: stereo.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateSpeakerPanner(const maudSpeakerPannerDef* def, maudSpeakerPanner** pannerOut);
```
Creates a speaker panner. Mono passes a source through; stereo pans by how far to the side a source is (one behind counts as in front); other layouts by VBAP over the triangles between their speakers, with imaginary speakers above and below whose share goes to their real neighbours. Gains keep the energy; the low-frequency channel gets nothing.  @param def         The def, from maudDefaultSpeakerPannerDef. @param pannerOut   Receives the panner; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a def without its cookie or a layout the library does not have; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroySpeakerPanner(maudSpeakerPanner* panner);
```
Destroys a speaker panner. NULL is ignored.  @param panner  The panner. @par Thread safety Safe from any thread; the panner is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetSpeakerGains(const maudSpeakerPanner* panner, maudVector3 direction, float* gainsOut);
```
Computes a direction's gain for every channel of the layout.  @param panner     The panner. @param direction  The direction, in the listener's frame. @param gainsOut   Receives the layout's channel count of gains. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or a direction that is not finite. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudPanToSpeakers(const maudSpeakerPanner* panner, const maudPanSource* from, const maudPanSource* to, const float* in, float* const* out, uint32_t frames);
```
Adds a mono source into the layout's channels, its direction and gain moving linearly from `from` to `to` across the call: frame n uses the gains (n + 1) / frames of the way.  @param panner  The panner. @param from    The source at the previous call's end. @param to      The source at this call's end. @param in      frames samples. @param out     The layout's channel count of channels, frames each, added to. @param frames  The frames. @return `maud_success`, or `maud_errorInvalid` for a NULL pointer or a value that is not finite; nothing is written then. @par Thread safety Safe from any thread; the output is used by one thread at a time.

## `stream.h`

Streams: one direction of audio between a host and a device, in interleaved 32-bit float frames, handed to the host's real-time callback one fixed period at a time.

```c
maudStreamDef maudDefaultStreamDef(void);
```
Returns the default stream def: an output stream in callback mode, stereo, at the device's native rate, with the backend's default period, following the general role's default device, and no callback.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateStream(maudContext* context, const maudStreamDef* def, maudStreamId* streamOut);
```
Creates a stream, stopped, and allocates everything it will use.  @param context      The context. @param def          The def, from maudDefaultStreamDef, with a callback. @param streamOut    Receives the stream's id; the null id on failure. @return `maud_success`; `maud_errorInvalid` for a NULL argument, a def without its cookie or callback, or a value out of range; `maud_errorUnsupported` for what the backend cannot do (the offline backend has only pull mode and no converter); `maud_errorCapacity` past the stream or period limit or when the allocator fails; `maud_errorState` on a thread rendering one of the context's streams. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudDestroyStream(maudContext* context, maudStreamId stream);
```
Destroys a stream. Its id becomes stale. On a platform backend its callback has returned for the last time when this returns.  @param context  The context. @param stream   The stream. @return `maud_success`; `maud_errorStale` for an id that names no stream; `maud_errorInvalid` for a NULL context; `maud_errorState` on a thread rendering one of the context's streams, or, on the offline backend, while another thread renders the stream. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudStartStream(maudContext* context, maudStreamId stream);
```
Starts a stream. Starting a started stream does nothing.  @param context  The context. @param stream   The stream. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL context; `maud_errorState` on a thread rendering one of the context's streams. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudStopStream(maudContext* context, maudStreamId stream);
```
Stops a stream. Stopping a stopped stream does nothing.  @param context  The context. @param stream   The stream. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL context; `maud_errorState` on a thread rendering one of the context's streams. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetStreamFormat(const maudContext* context, maudStreamId stream, maudStreamFormat* formatOut);
```
Reports what a stream runs at.  @param context    The context. @param stream     The stream. @param formatOut  Receives the format. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetStreamStatus(const maudContext* context, maudStreamId stream, maudStreamStatus* statusOut);
```
Reports whether a stream is started, suspended, and on which device.  @param context    The context. @param stream     The stream. @param statusOut  Receives the status. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetStreamPosition(const maudContext* context, maudStreamId stream, uint64_t* framesOut);
```
Reports how many frames a stream has moved to or from its device.  @param context    The context. @param stream     The stream. @param framesOut  Receives the frame count. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudGetStreamClock(const maudContext* context, maudStreamId stream, maudStreamClock* clockOut);
```
Reports where a stream's frames meet the host clock. The platform stamps it at each callback, so it follows route changes; a frame's time follows from it at the stream's rate.  @param context   The context. @param stream    The stream. @param clockOut  Receives the clock. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL pointer. @par Thread safety Safe from any thread. It never makes the rendering thread wait.

```c
int64_t maudGetHostNanoseconds(void);
```
Returns the host clock streams are stamped with: CLOCK_MONOTONIC on Linux and other POSIX systems, the performance counter on Windows, the HAL's host time on macOS, `performance.now()` on the web.  @return Nanoseconds since an unspecified start. @par Thread safety Real-time safe: no allocation, lock or wait.

```c
MAUD_NODISCARD MAUD_API maudResult maudRenderStream(maudContext* context, maudStreamId stream, float* framesOut, uint32_t frameCount);
```
Renders the next frames of an offline output stream into a caller buffer, calling the stream's callback once per period as needed, on the calling thread. The stream's clock advances by frameCount. A rate change from a move to another device applies from the first block this call produces.  @param context     The context. @param stream      A started, running output stream of an offline context. @param framesOut   Room for frameCount interleaved frames. May be NULL when frameCount is 0. @param frameCount  The number of frames. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL pointer where frames are due, an input stream or a total that does not fit in memory; `maud_errorUnsupported` on a context that is not offline; `maud_errorState` for a stopped or suspended stream or one already rendering. @par Thread safety Real-time safe: no allocation, lock or wait. A stream renders on one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudFeedStream(maudContext* context, maudStreamId stream, const float* frames, uint32_t frameCount);
```
Feeds the next frames to an offline input stream from a caller buffer, calling the stream's callback once per completed period, on the calling thread. The stream's clock advances by frameCount.  @param context     The context. @param stream      A started, running input stream of an offline context. @param frames      frameCount interleaved frames. May be NULL when frameCount is 0. @param frameCount  The number of frames. @return `maud_success`; `maud_errorStale`; `maud_errorInvalid` for a NULL pointer where frames are due, an output stream or a total that does not fit in memory; `maud_errorUnsupported` on a context that is not offline; `maud_errorState` for a stopped or suspended stream or one already rendering. @par Thread safety Real-time safe: no allocation, lock or wait. A stream renders on one thread at a time.

## `voice.h`

First-party voice processing: a voice activity detector and an automatic gain control. They stand apart from contexts and streams; a host runs them on captured frames, in its callback or anywhere else.

```c
maudVoiceDetectorDef maudDefaultVoiceDetectorDef(void);
```
Returns the default voice detector def: 48,000, mono, aggressiveness 1, a 200 ms hangover, the default allocator.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateVoiceDetector(const maudVoiceDetectorDef* def, maudVoiceDetector** detectorOut);
```
Creates a voice activity detector.  @param def          The def, from maudDefaultVoiceDetectorDef. @param detectorOut  Receives the detector; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a def out of range; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroyVoiceDetector(maudVoiceDetector* detector);
```
Destroys a voice activity detector. NULL is ignored.  @param detector  The detector. @par Thread safety Safe from any thread; the detector is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudDetectVoice(maudVoiceDetector* detector, const float* frames, uint32_t frameCount, maudVoiceState* stateOut);
```
Analyzes interleaved frames, in any count: what does not complete a 10 ms frame waits for the next call, so the results do not depend on how the frames are cut.  @param detector    The detector. @param frames      frameCount frames in the def's layout; may be NULL when frameCount is 0. @param frameCount  How many. @param stateOut    Receives the state after them; may be NULL. @return `maud_success`; `maud_errorInvalid` for a NULL detector, or NULL frames with a frameCount. @par Thread safety Real-time safe: no allocation, lock or wait. The detector is used by one thread at a time.

```c
maudGainControlDef maudDefaultGainControlDef(void);
```
Returns the default gain control def: 48,000, mono, a target of -25 dBFS, gains from -10 to 50 dB starting at 15, 6 dB per second, noise kept under -50 dBFS, aggressiveness 1, the default allocator.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateGainControl(const maudGainControlDef* def, maudGainControl** gainOut);
```
Creates an automatic gain control.  @param def      The def, from maudDefaultGainControlDef. @param gainOut  Receives the gain control; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a def out of range; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroyGainControl(maudGainControl* gain);
```
Destroys an automatic gain control. NULL is ignored.  @param gain  The gain control. @par Thread safety Safe from any thread; the gain control is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudApplyGainControl(maudGainControl* gain, float* frames, uint32_t frameCount, maudGainState* stateOut);
```
Applies the gain to interleaved frames in place, in any count; the output does not depend on how the frames are cut. The gain follows what the frames before them showed, so it adds no delay.  @param gain        The gain control. @param frames      frameCount frames in the def's layout; may be NULL when frameCount is 0. @param frameCount  How many. @param stateOut    Receives the state after them; may be NULL. @return `maud_success`; `maud_errorInvalid` for a NULL gain control, or NULL frames with a frameCount. @par Thread safety Real-time safe: no allocation, lock or wait. The gain control is used by one thread at a time.

```c
maudNoiseSuppressorDef maudDefaultNoiseSuppressorDef(void);
```
Returns the default noise suppressor def: 48,000, mono, a floor of -30 dB, a high-pass at 100 Hz, the default allocator.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateNoiseSuppressor( const maudNoiseSuppressorDef* def, maudNoiseSuppressor** suppressorOut);
```
Creates a noise suppressor.  @param def            The def, from maudDefaultNoiseSuppressorDef. @param suppressorOut  Receives the noise suppressor; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a def out of range; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroyNoiseSuppressor(maudNoiseSuppressor* suppressor);
```
Destroys a noise suppressor. NULL is ignored.  @param suppressor  The noise suppressor. @par Thread safety Safe from any thread; the noise suppressor is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudSuppressNoise(maudNoiseSuppressor* suppressor, float* frames, uint32_t frameCount, maudNoiseState* stateOut);
```
Suppresses noise in interleaved frames in place, in any count; the output does not depend on how the frames are cut. It lags the input by 10 ms: the first 10 ms out are silence.  @param suppressor  The noise suppressor. @param frames      frameCount frames in the def's layout; may be NULL when frameCount is 0. @param frameCount  How many. @param stateOut    Receives the state after them; may be NULL. @return `maud_success`; `maud_errorInvalid` for a NULL noise suppressor, or NULL frames with a frameCount. @par Thread safety Real-time safe: no allocation, lock or wait. The noise suppressor is used by one thread at a time.

```c
maudEchoCancellerDef maudDefaultEchoCancellerDef(void);
```
Returns the default echo canceller def: 48,000, a path of 0.2 s, a noise floor of -15 dB, the default allocator.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MAUD_NODISCARD MAUD_API maudResult maudCreateEchoCanceller(const maudEchoCancellerDef* def, maudEchoCanceller** cancellerOut);
```
Creates an echo canceller.  @param def            The def, from maudDefaultEchoCancellerDef. @param cancellerOut   Receives the echo canceller; NULL on failure. @return `maud_success`; `maud_errorInvalid` for a NULL pointer or a def out of range; `maud_errorCapacity` when the allocator fails. @par Thread safety Safe from any thread.

```c
void maudDestroyEchoCanceller(maudEchoCanceller* canceller);
```
Destroys an echo canceller. NULL is ignored.  @param canceller  The echo canceller. @par Thread safety Safe from any thread; the echo canceller is used by one thread at a time.

```c
MAUD_NODISCARD MAUD_API maudResult maudCancelEcho(maudEchoCanceller* canceller, float* capture, const float* render, uint32_t frameCount, maudEchoState* stateOut);
```
Takes the echo of the render out of the capture, in place, in any count of frames; the output does not depend on how the frames are cut. It works in blocks of the smallest power of two of frames lasting 8 ms or more (128 at 16,000, 512 at 48,000) and lags the input by two: the first two blocks out are silence.  @param canceller   The echo canceller. @param capture     frameCount mono frames of the capture; may be NULL when frameCount is 0. @param render      frameCount mono frames of what was played, aligned with the capture; may be NULL when frameCount is 0. @param frameCount  How many. @param stateOut    Receives the state after them; may be NULL. @return `maud_success`; `maud_errorInvalid` for a NULL echo canceller, or NULL frames with a frameCount. @par Thread safety Real-time safe: no allocation, lock or wait. The echo canceller is used by one thread at a time.

---

130 functions across 19 headers.
