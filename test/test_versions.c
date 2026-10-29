// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every root def carries the version of the headers it was built from:
// its default stamps MAUD_ABI_VERSION, and the object's creation
// refuses a def of another interface (another release before 1.0,
// another major, a zeroed version) with maud_errorVersion before
// it reads anything else of it (here the HRTF set those that need one
// are never given), leaving nothing made; a def without its cookie is
// maud_errorInvalid whatever its version says.

#include "test_harness.h"

#include "maul-audio/ambisonics.h"
#include "maul-audio/binaural.h"
#include "maul-audio/direct.h"
#include "maul-audio/hrtf.h"
#include "maul-audio/reverb.h"
#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"
#include "maul-audio/speakers.h"
#include "maul-audio/voice.h"

// Creates from def with its version shifted by shift, and without its
// cookie unless keep;
// true when the result is expected and nothing was made.
#define REFUSES(Type, def, create, shift, keep, expected)                                          \
    do                                                                                             \
    {                                                                                              \
        Type copy = (def);                                                                         \
        copy.version += (shift);                                                                   \
        copy.cookie = (keep) ? copy.cookie : 0u;                                                   \
        void* made = &copy;                                                                        \
        ok = ok && create(&copy, (void*)&made) == (expected) && made == nullptr;                   \
    } while (0)

static maudResult Hrtf(const maudHrtfDef* def, void* out)
{
    return maudLoadHrtf(def, out);
}

static maudResult Binaural(const maudBinauralDef* def, void* out)
{
    return maudCreateBinaural(def, out);
}

static maudResult BinauralDecoder(const maudBinauralDecoderDef* def, void* out)
{
    return maudCreateBinauralDecoder(def, out);
}

static maudResult SpeakerDecoder(const maudSpeakerDecoderDef* def, void* out)
{
    return maudCreateSpeakerDecoder(def, out);
}

static maudResult SpeakerPanner(const maudSpeakerPannerDef* def, void* out)
{
    return maudCreateSpeakerPanner(def, out);
}

static maudResult DirectEffect(const maudDirectEffectDef* def, void* out)
{
    return maudCreateDirectEffect(def, out);
}

static maudResult Reverb(const maudReverbDef* def, void* out)
{
    return maudCreateReverb(def, out);
}

static maudResult Scene(const maudSceneDef* def, void* out)
{
    return maudCreateScene(def, out);
}

static maudResult Spatializer(const maudSpatializerDef* def, void* out)
{
    return maudCreateSpatializer(def, out);
}

static maudResult VoiceDetector(const maudVoiceDetectorDef* def, void* out)
{
    return maudCreateVoiceDetector(def, out);
}

static maudResult GainControl(const maudGainControlDef* def, void* out)
{
    return maudCreateGainControl(def, out);
}

static maudResult NoiseSuppressor(const maudNoiseSuppressorDef* def, void* out)
{
    return maudCreateNoiseSuppressor(def, out);
}

static maudResult EchoCanceller(const maudEchoCancellerDef* def, void* out)
{
    return maudCreateEchoCanceller(def, out);
}

// Each def's refusals: another release, another major, a zeroed
// version, no cookie.
#define ALL(Type, def, create)                                                                     \
    do                                                                                             \
    {                                                                                              \
        Type d = (def);                                                                            \
        ok = ok && d.version == MAUD_ABI_VERSION;                                                  \
        REFUSES(Type, d, create, 1u, true, maud_errorVersion);                                     \
        REFUSES(Type, d, create, 1u << 16, true, maud_errorVersion);                               \
        REFUSES(Type, d, create, 0u - d.version, true, maud_errorVersion);                         \
        REFUSES(Type, d, create, 1u, false, maud_errorInvalid);                                    \
        if (!ok)                                                                                   \
        {                                                                                          \
            printf("%s not refused as it should be\n", #Type);                                     \
        }                                                                                          \
        all = all && ok;                                                                           \
        ok = true;                                                                                 \
    } while (0)

int main(void)
{
    // The stamp: the top bit always, the major, and before 1.0, when any
    // release may break the interface, the minor and the patch too.
    CHECK((MAUD_ABI_VERSION & 0x80000000u) != 0 &&
              (MAUD_ABI_VERSION >> 16 & 0x7FFFu) == MAUD_VERSION_MAJOR &&
              (MAUD_VERSION_MAJOR != 0 ||
               (MAUD_ABI_VERSION & 0xFFFFu) == (MAUD_VERSION_MINOR << 8 | MAUD_VERSION_PATCH)),
          "the stamp names the release's interface");
    bool ok = true;
    bool all = true;
    ALL(maudHrtfDef, maudDefaultHrtfDef(), Hrtf);
    ALL(maudBinauralDef, maudDefaultBinauralDef(), Binaural);
    ALL(maudBinauralDecoderDef, maudDefaultBinauralDecoderDef(), BinauralDecoder);
    ALL(maudSpeakerDecoderDef, maudDefaultSpeakerDecoderDef(), SpeakerDecoder);
    ALL(maudSpeakerPannerDef, maudDefaultSpeakerPannerDef(), SpeakerPanner);
    ALL(maudDirectEffectDef, maudDefaultDirectEffectDef(), DirectEffect);
    ALL(maudReverbDef, maudDefaultReverbDef(), Reverb);
    ALL(maudSceneDef, maudDefaultSceneDef(), Scene);
    ALL(maudSpatializerDef, maudDefaultSpatializerDef(), Spatializer);
    ALL(maudVoiceDetectorDef, maudDefaultVoiceDetectorDef(), VoiceDetector);
    ALL(maudGainControlDef, maudDefaultGainControlDef(), GainControl);
    ALL(maudNoiseSuppressorDef, maudDefaultNoiseSuppressorDef(), NoiseSuppressor);
    ALL(maudEchoCancellerDef, maudDefaultEchoCancellerDef(), EchoCanceller);
    CHECK(all, "every root def stamped, and another version or no cookie refused");
    return s_failures == 0 ? 0 : 1;
}
