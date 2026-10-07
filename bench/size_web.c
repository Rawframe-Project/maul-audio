// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What one feature costs a web page to download: built once per
// feature (MAUD_SIZE_FEATURE: 1 a playback stream, 2 the binaural
// effect, 3 the reverb, 4 the voice processors), linked alone, so the
// .wasm holds that feature and what it needs. The CI's web cell
// reports each size; nothing runs it.

#include "maul-audio/base.h"

#include <stdint.h>

#if MAUD_SIZE_FEATURE == 1
#include "maul-audio/context.h"
#include "maul-audio/stream.h"

static void Silence(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    maudContext* context = nullptr;
    if (maudCreateContext(&def, &context) != maud_success)
    {
        return 1;
    }
    maudStreamDef stream = maudDefaultStreamDef();
    stream.callback = Silence;
    maudStreamId id = {0, 0};
    int failed = maudCreateStream(context, &stream, &id) != maud_success ||
                 maudStartStream(context, id) != maud_success;
    return maudDestroyContext(context) != maud_success || failed;
}

#elif MAUD_SIZE_FEATURE == 2
#include "maul-audio/binaural.h"
#include "maul-audio/hrtf.h"

// The set's bytes, which a page fetches and copies in (the file system
// would add its own code to the count).
uint8_t g_set[1 << 20];
static float s_in[480];
static float s_left[480];
static float s_right[480];

int main(int argc, char** argv)
{
    (void)argv;
    maudHrtfDef def = maudDefaultHrtfDef();
    def.bytes = g_set;
    def.byteCount = (size_t)argc << 10;
    maudHrtf* hrtf = nullptr;
    if (maudLoadHrtf(&def, &hrtf) != maud_success)
    {
        return 1;
    }
    maudBinauralDef effectDef = maudDefaultBinauralDef();
    effectDef.hrtf = hrtf;
    maudBinaural* effect = nullptr;
    maudBinauralParams params = {{1.0f, 0.0f, 0.0f}, 1.0f};
    float* out[2] = {s_left, s_right};
    int failed = maudCreateBinaural(&effectDef, &effect) != maud_success ||
                 maudProcessBinaural(effect, &params, s_in, out, 480) != maud_success;
    maudDestroyBinaural(effect);
    maudDestroyHrtf(hrtf);
    return failed;
}

#elif MAUD_SIZE_FEATURE == 3
#include "maul-audio/reverb.h"

static float s_in[480];
static float s_bed[16][480];

int main(int argc, char** argv)
{
    maudReverbDef def = maudDefaultReverbDef();
    maudReverb* reverb = nullptr;
    if (maudCreateReverb(&def, &reverb) != maud_success)
    {
        return 1;
    }
    maudReverbParams params = {0};
    for (int b = 0; b < MAUD_DIRECT_BANDS; ++b)
    {
        params.reverbTime[b] = (float)(argc + 1) * 0.5f;
    }
    (void)argv;
    float* bed[16];
    for (int c = 0; c < 16; ++c)
    {
        bed[c] = s_bed[c];
    }
    int failed = maudProcessReverb(reverb, &params, s_in, bed, 480) != maud_success;
    maudDestroyReverb(reverb);
    return failed;
}

#elif MAUD_SIZE_FEATURE == 4
#include "maul-audio/voice.h"

static float s_capture[480];
static float s_render[480];

int main(void)
{
    maudVoiceDetectorDef vadDef = maudDefaultVoiceDetectorDef();
    maudGainControlDef agcDef = maudDefaultGainControlDef();
    maudNoiseSuppressorDef nsDef = maudDefaultNoiseSuppressorDef();
    maudEchoCancellerDef aecDef = maudDefaultEchoCancellerDef();
    maudVoiceDetector* vad = nullptr;
    maudGainControl* agc = nullptr;
    maudNoiseSuppressor* ns = nullptr;
    maudEchoCanceller* aec = nullptr;
    int failed = maudCreateVoiceDetector(&vadDef, &vad) != maud_success ||
                 maudCreateGainControl(&agcDef, &agc) != maud_success ||
                 maudCreateNoiseSuppressor(&nsDef, &ns) != maud_success ||
                 maudCreateEchoCanceller(&aecDef, &aec) != maud_success ||
                 maudCancelEcho(aec, s_capture, s_render, 480, nullptr) != maud_success ||
                 maudSuppressNoise(ns, s_capture, 480, nullptr) != maud_success ||
                 maudApplyGainControl(agc, s_capture, 480, nullptr) != maud_success ||
                 maudDetectVoice(vad, s_capture, 480, nullptr) != maud_success;
    maudDestroyEchoCanceller(aec);
    maudDestroyNoiseSuppressor(ns);
    maudDestroyGainControl(agc);
    maudDestroyVoiceDetector(vad);
    return failed;
}
#endif
