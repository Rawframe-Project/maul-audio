# Spatial rendering

How the Spatial part's effects turn a mono source into what reaches the ears or the speakers, and why each is built the way it is. Every effect runs on the host's audio thread, allocates nothing once made (one block from its def's allocator holds everything), and costs in proportion to its frames.

## Contents

- [Three bands](#three-bands)
- [HRTF sets](#hrtf-sets)
- [The binaural effect](#the-binaural-effect)
- [The near field](#the-near-field)
- [The ambisonic bed](#the-ambisonic-bed)
- [Decoders and the panner](#decoders-and-the-panner)
- [The direct effect](#the-direct-effect)
- [The reverb](#the-reverb)
- [Ramps and denormals](#ramps-and-denormals)

## Three bands

Every frequency-dependent quantity (air absorption, transmission, directivity, reverberation times, materials) has three bands: up to 800 Hz, 800 Hz to 8 kHz, above 8 kHz. Three bands are what material tables and game audio tools commonly carry, cheap enough to filter per source, and enough to make occlusion sound muffled and distance sound dull. Each effect that applies band gains turns them into smooth filters rather than a crossover, so no band edge is audible. The count is `MAUD_BANDS` ([direct.h](../../include/maul-audio/direct.h)). It sizes public structs and the bake's fields, so from 1.0.0 it never changes.

## HRTF sets

`maudLoadHrtf` checks the whole file first ([file formats](file-formats.md#hrtf-sets-maudhrtf)), then lays out one block for what the set keeps, and copies the responses at the file's rate or resamples them to the def's ([hrtf_resample.h](../../src/hrtf_resample.h)). A set is read only afterwards and shared by any number of effects.

A direction is found on the set's rings ([hrtf_lookup.h](../../src/hrtf_lookup.h)): the two rings around its elevation, and on each the two azimuths around its azimuth, weighted bilinearly. Rings of evenly spaced azimuths make the lookup a division, not a search, and a set from any SOFA grid is resampled onto them by the conversion tool.

## The binaural effect

Each ear ([binaural.c](../../src/binaural.c)) blends the four measured responses and delays around the source's direction as that ear sees it, reads its delayed input from the history by cubic interpolation, filters it for the near field, and convolves it with a direct-form FIR ([binaural_dsp.h](../../src/binaural_dsp.h)). Responses are minimum phase, so the FIR is short (128 taps for the shipped set at 48 kHz), and the delay carries the interaural time difference precisely, at a fraction of a sample.

A change of direction runs the old and new responses side by side for a fixed fade and crossfades them, while the delays and near-field filters move continuously. Blending FIR coefficients directly would comb-filter between measured directions; crossfading outputs does not.

## The near field

An HRTF set is measured at one distance, about a metre or more. Closer than that, the head shadows the far ear more and boosts the near ear's lows, and each ear sees the source from a different angle. The near field ([near_field.h](../../src/near_field.h)) models the head as a rigid sphere of `headRadius`: per ear, the sphere's response to a point source at the source's distance over its response to one at the set's distance, as a gain and an analog first-order high shelf. The parameters come from a table ([near_field_table.h](../../src/near_field_table.h)) that `tools/near_field_table.py` generates from the sphere's series solution (Duda and Martens, 1998), interpolated bilinearly in angle and inverse distance (the generator checks the error stays within 0.15 dB), and the shelf is made digital by the bilinear transform warped to keep its corner. Each ear also looks its response up in the direction it sees the source from (parallax). Only the head's effect is modeled: the 1/r fall of pressure is the host's distance attenuation.

## The ambisonic bed

The bed is AmbiX: ACN channel order, SN3D normalization, no Condon-Shortley phase, the field's axes x ahead, y left and z up ([spherical_harmonics.h](../../src/spherical_harmonics.h)). Directions and rotations come in the listener frame and are converted. Encoding moves a source's gains linearly across the call. Rotation builds the rotation matrices of real spherical harmonics order by order by Ivanic and Ruedenberg's recursion ([ambisonic_rotation.h](../../src/ambisonic_rotation.h)); a rotation acts within each order, so the matrices serve any normalization. The encoder and rotation keep no state: the bed's channels are the host's.

## Decoders and the panner

- **Binaural decoder** ([magls.h](../../src/magls.h)). A least-squares decoder from a low-order bed to an HRTF set's dense directions loses the highs: low orders cannot represent the fine phase structure of high frequencies. Magnitude least squares (Schörkhuber, Zaunschirm and Höldrich, 2018) fits plain least squares below 1.5 kHz and the magnitudes alone above, continuing the phase from the bin below and advancing it by a group delay, which keeps the filters causal. The filters are fitted once, at creation; decoding is each ear's sum of every channel through its filter.
- **Speaker panner** ([speaker_panner.c](../../src/speaker_panner.c)). Vector base amplitude panning (Pulkki, 1997). The layout's speakers, with imaginary ones at the zenith and nadir, are triangulated once into the faces of their convex hull; a direction's gains come from the face whose inverse matrix gives no negative gain, an imaginary speaker's gain is shared by the real speakers next to it, and the gains are normalized for energy. Once made, a panner is read only.
- **Speaker decoder** ([speaker_decoder.c](../../src/speaker_decoder.c)). All-round ambisonic decoding (AllRAD, Zotter and Frank, 2012): the bed is decoded to 240 near-uniform virtual speakers with max-rE weights, and each virtual speaker is panned to the layout by the panner. Both steps are linear and fold into one matrix at creation, scaled so a source through the bed reaches the speakers with the energy it would have panned straight. Irregular layouts (5.1, 7.1.4) are why: a mode-matching decoder for them is ill-conditioned.

## The direct effect

A call's band gains ([direct_effect.c](../../src/direct_effect.c)) are air absorption times directivity times `(1 - occlusion) + occlusion * transmission`. The loudest band joins the host's gain as the overall gain, and the bands become dB targets below it, floored at -24 dB, which a three-band equalizer meets ([band_eq.h](../../src/band_eq.h)): a low shelf at 800 Hz, a bell at the middle band's centre and a high shelf at 8 kHz, as state-variable filters in their topology-preserving form, whose coefficients may ramp sample by sample without instability. The filters interact, so band targets become filter gains through an inverted interaction matrix and a few refinements against the filters' actual band means. The filters are refitted only when a target moves by 0.05 dB or more, and skipped while every target is 0 dB.

Air absorption comes from ISO 9613-1 ([air_absorption.h](../../src/air_absorption.h)), averaged in dB over each band. Directivity is a weighted dipole per band, `|(1 - w) + w cos(angle)|^p`.

## The reverb

A feedback delay network ([reverb.c](../../src/reverb.c)): sixteen delay lines of 23 to 69 ms (prime lengths at the rate), each read through an attenuation filter, mixed by a scaled 16-point Hadamard matrix and written back with the send. Each line's filter targets its loss per pass, -60 dB times its delay over the reverberation time, octave by octave: an octave graphic equalizer of ten peak-notch filters and a high shelf after Prawda, Välimäki and Schlecht (2019) ([octave_eq.h](../../src/octave_eq.h)), fitted by least squares with the error weighted relative to reverberation time. Three band times would be too coarse a target for one shelf per line; an octave fit keeps the decay smooth across frequency. Each line leaves into a first-order bed from one of 16 directions spread over the sphere, so the tail is diffuse after decoding.

A change of times ramps the filters in steps of 8 frames: a biquad is stable for any denominator inside the stability triangle, which holds every blend of two stable ones. A reverb with a tail runs a second network for the slower slope, its lines between the first's (24 to 72 ms) so the two do not ring together, fed the same delayed send at its own levels.

## Ramps and denormals

Parameters move across a call from the previous call's values to the new ones, linearly per frame for gains and positions, in short steps for filter coefficients. [ramp.h](../../src/ramp.h) gives a frame's place in a block as a float by a signed conversion, one instruction on every target, where the unsigned one took several in SSE2's inner loops. Every recursive state is kept out of the subnormal range ([flush.h](../../src/flush.h)), as [Real-time rules](realtime.md#keeping-denormals-away) explains.

Rendered output is reproducible on one platform and build. The golden renders in `data/golden/` hold four scenes (an HRTF sweep, a rotating bed, an occlusion walk, a reverb tail) to within -50 dB of each block's level across platforms; the C library's transcendental functions and compilers' rounding make bit-exact output across platforms impossible without the portable math the Spatial simulation uses, which rendering does not need.
