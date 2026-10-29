# Spatial audio

The Spatial part renders mono sources in space: for headphones through a head-related transfer function (HRTF) set, or for a speaker layout. It is a set of effects the program chains on its own audio thread, each processing a block in place or into caller buffers. None of them needs a context, a device or a thread; they work as well in an offline renderer as in a game's mixer.

A source usually goes through two stages: the [direct effect](#what-the-path-does-to-a-source) applies what the path does to it (distance, walls, air), then the [binaural effect](#a-source-for-headphones), the [ambisonic bed](#many-sources-the-ambisonic-bed) or a [speaker panner](#speakers) places it. A [reverb](#the-room-the-reverb) adds the room. The [spatializer](environments.md) can compute every effect's parameters from a scene.

## Contents

- [Coordinates](#coordinates)
- [An HRTF set](#an-hrtf-set)
- [A source for headphones](#a-source-for-headphones)
- [Many sources: the ambisonic bed](#many-sources-the-ambisonic-bed)
- [Speakers](#speakers)
- [What the path does to a source](#what-the-path-does-to-a-source)
- [The room: the reverb](#the-room-the-reverb)
- [What every effect shares](#what-every-effect-shares)

## Coordinates

Positions are in metres in the listener's frame: +x to the right, +y up, -z straight ahead, the convention of OpenAL, Steam Audio and Resonance Audio. A program with another convention converts at the call. Rotations are quaternions, normalized where they are used.

## An HRTF set

Headphone rendering needs an HRTF set in the library's own format (see [File formats](../internals/file-formats.md)). `data/hrtf/sadie2-ku100-48k.maudhrtf` is the default set, a KU100 dummy head from the SADIE II database, under the Apache License 2.0. `tools/sofa_to_maudhrtf.py` makes one from any SOFA file.

The library opens no files. The program reads the file and hands the bytes over; the loader treats them as hostile, checks every count and the checksum before it allocates, and copies what it keeps:

```c
maudHrtfDef def = maudDefaultHrtfDef();
def.bytes = bytes;          // the file's bytes, the host's to keep or free
def.byteCount = byteCount;
maudHrtf* hrtf = NULL;
maudResult result = maudLoadHrtf(&def, &hrtf);
// maud_errorFormat: a damaged or foreign file; maud_errorVersion: another version.
```

Set `def.sampleRate` to the rate the effects run at, and the loader resamples the set to it. One set serves every effect that uses it and must outlive them. `maudHrtf_GetInfo` gives its name and license text, which a program that ships the set shows with its credits.

## A source for headphones

The binaural effect renders one mono source for both ears and moves smoothly between calls:

```c
maudBinauralDef def = maudDefaultBinauralDef();   // the near field on
def.hrtf = hrtf;
maudBinaural* effect = NULL;
maudResult result = maudCreateBinaural(&def, &effect);
maudBinauralParams params = {{2.0f, 0.0f, -1.0f}, 0.5f};   // ahead and right; its gain
float* ears[2] = {left, right};
if (result == maud_success)
{
    result = maudBinaural_Process(effect, &params, mono, ears, 480);
}
```

- The gain is the program's own, distance attenuation included: the effect applies none, so the program keeps its distance curve. Gain and position move across the call from the previous call's.
- With the near field on (the default), a source closer than the set's measuring distance sounds closer: each ear is filtered for the head's shadow, and each ear looks its response up from where it sees the source. `headRadius` sets the head's size.
- `maudBinaural_Reset` forgets the previous call, for a source that jumps (a cut, a respawn).

## Many sources: the ambisonic bed

A binaural effect per moving source costs about 12 microseconds per 10 ms block of 48 kHz audio on a desktop core. Distant sources and ambience cost less through a bed: each source is encoded into a sound field, the field turns with the listener's head, and one decoder renders it for the ears.

```c
uint32_t order = 3;
maudPanSource was = {{0.0f, 0.0f, -1.0f}, 1.0f};   // where it was at the last call
maudPanSource now = {{1.0f, 0.0f, -1.0f}, 1.0f};   // and is at this one's end
maudResult result = maudEncodeAmbisonic(order, &was, &now, mono, bed, 480);
maudQuaternion head = {0.0f, 0.0f, 0.0f, 1.0f};   // the listener's turn, from its pose
if (result == maud_success)
{
    result = maudRotateAmbisonic(order, &head, &head, bed, 480);
}
maudBinauralDecoderDef def = maudDefaultBinauralDecoderDef();
def.hrtf = hrtf;
def.order = order;
maudBinauralDecoder* decoder = NULL;
if (result == maud_success)
{
    result = maudCreateBinauralDecoder(&def, &decoder);
}
float* ears[2] = {left, right};
if (result == maud_success)
{
    result = maudBinauralDecoder_Decode(decoder, (const float* const*)bed, ears, 480);
}
maudDestroyBinauralDecoder(decoder);
```

A bed of order 1 to 3 has `maudGetAmbisonicChannelCount(order)` planar channels (16 at the third), in AmbiX's ACN order with SN3D weights, so other ambisonic tools read it. Higher orders place sources more sharply and cost more. Encoding adds into the bed, so clear it before each block. The bed's channels are the program's buffers; the encoder and rotation keep no state.

## Speakers

For a speaker layout there are two ways:

- A **speaker panner** (`maudCreateSpeakerPanner`) pans each source straight into the layout's channels by vector base amplitude panning. Once made it is read only: any number of sources share it, from any thread.
- A **speaker decoder** (`maudCreateSpeakerDecoder`, `maudSpeakerDecoder_Decode`) renders an ambisonic bed to the layout, so the same bed serves headphones and speakers.

The layout's speaker positions are the nominal ones of ITU-R BS.2051.

## What the path does to a source

The direct effect filters a source by what lies between it and the listener, in three bands (up to 800 Hz, 800 Hz to 8 kHz, above 8 kHz): air absorption over the distance, occlusion and transmission through obstacles, and the source's directivity.

```c
maudDirectEffectDef def = maudDefaultDirectEffectDef();
maudDirectEffect* effect = NULL;
maudResult result = maudCreateDirectEffect(&def, &effect);
maudDirectParams params = maudDefaultDirectParams();   // a clear path
params.distance = 30.0f;          // air absorbs the highs over 30 m
params.occlusion = 1.0f;          // behind a wall...
params.transmission[0] = 0.3f;    // ...that lets some lows through
params.transmission[1] = 0.1f;
params.transmission[2] = 0.02f;
if (result == maud_success)
{
    result = maudDirectEffect_Process(effect, &params, mono, filtered, 480);
}
maudDestroyDirectEffect(effect);
```

`maudGetAirAbsorption` computes the def's absorption per band from ISO 9613-1 for a temperature and humidity; `maudGetDirectivity` turns a directivity pattern and the angle to the listener into the per-band values. The effect sits before the binaural effect or panner, mono in and mono out.

## The room: the reverb

The reverb takes a mono send and adds the room's late sound into a first-order bed, decaying in three bands as its parameters say. One per listener is usual; decode its bed with the binaural or speaker decoder.

```c
maudReverbDef def = maudDefaultReverbDef();   // 48 kHz
def.maxDelay = 0.1f;                          // the longest send delay it will take, s
maudReverb* reverb = NULL;
maudResult result = maudCreateReverb(&def, &reverb);
maudReverbParams params = {
    .reverbTime = {1.8f, 1.2f, 0.6f},   // seconds to fall 60 dB: lows, mids, highs
    .level = {-12.0f, -12.0f, -18.0f},  // the send's level per band, dB
    .delay = 0.02f,                     // the send's delay, s
};
if (result == maud_success)
{
    result = maudReverb_Process(reverb, &params, mono, bed, 480);   // four channels
}
maudDestroyReverb(reverb);
```

Coupled spaces (a hall behind an open door) decay in two slopes. A reverb made with `tail` on also renders the slower one from `tailTime` and `tailLevel`, at twice the memory.

## What every effect shares

- Processing allocates nothing, takes no lock and costs in proportion to its frames, so it runs on the audio thread. Making and destroying an effect allocates and belongs elsewhere.
- Parameters change smoothly across a call: a program sets the values for the end of the block, and the effect ramps from the last call's.
- Each effect is used by one thread at a time; the HRTF set and the speaker panner may be shared.
- Results are the same on one platform and build for the same inputs. The test suite's golden renders hold them there and within a small tolerance across platforms.
- The README's cost table gives what each effect costs on one core.
