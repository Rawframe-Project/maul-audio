# maud-0004. Every root def carries the program's version

Status: Accepted

## Context

Within a minor release the ABI holds; across minors a struct may grow
or reorder, so a program built against other headers lays every def
out differently from the library it runs with (conventions section
15). Since 0.2.0 the context def carried the version of the headers
the program was built with, and maudCreateContext refused another.
That covered the Device part only: the Spatial part's objects (an HRTF
set, the binaural effect and decoder, the speaker decoder and panner,
the direct effect, the reverb, a scene, a spatializer) and the four
voice processors are made from their own defs with no context, so a
program using them alone was never checked.

## Decision

- Every def a program makes an object from without a context or
  another object carries `version` second after its cookie:
  maudHrtfDef, maudBinauralDef, maudBinauralDecoderDef,
  maudSpeakerDecoderDef, maudSpeakerPannerDef, maudDirectEffectDef,
  maudReverbDef, maudAcousticSceneDef, maudSpatializerDef,
  maudVoiceDetectorDef, maudGainControlDef, maudNoiseSuppressorDef and
  maudEchoCancellerDef. The cookie and the version stay the first two
  fields in every later layout.
- Their defaults are `static inline` functions in the headers, so the
  program's build writes `MAUD_ABI_VERSION`; their cookies are public
  macros for it.
- Each creation returns `maud_errorVersion` for a def with the cookie
  and another major or minor version, before it reads anything else of
  it; a def without the cookie stays `maud_errorInvalid`.
- The defs of objects made from another (a stream, a source, a probe
  set, an offline device) carry no version: their parent was checked.

## Consequences

A program built against another minor gets a status at the first
object it makes, whichever part it uses, instead of a silent misread.
The defaults' values move into the headers, where a host reads them;
a def written field by field must set the cookie and the version as
the default does. A macro over a library function would have kept the
values in the library but taken the default's symbol away; a version
folded into the cookie would still have needed inline defaults and
lost the difference between "not a def" and "another version".
