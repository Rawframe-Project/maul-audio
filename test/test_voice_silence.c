// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The voice processors in digital silence, at 48 kHz: after three
// seconds of noise (and its echo), forty seconds of zeros. The noise
// suppressor and the echo canceller put out zeros again, not subnormal
// leftovers of their filters; on x86-64, where the processor flags an
// operation on a subnormal operand, the last two seconds of the noise
// suppressor, the gain control and the detector raise no such flag, as
// their decaying states stop at zero instead (each such operation is
// many times slower). So do two echo cancellers, one that heard a far
// end and its echo (the filter's windows and weights, the de-emphasis,
// the suppressor's held levels) and one that heard only a near end (the
// notch).

#include "test_harness.h"

#include "maul-audio/voice.h"

#include <stdlib.h>
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#define FLAGS 1
#else
#define FLAGS 0
#endif

enum
{
    RATE = 48000,
    BLOCK = 480,
    LOUD = 3 * RATE / BLOCK,
    QUIET = 40 * RATE / BLOCK,
    LAST = 2 * RATE / BLOCK
};

static void Clear(void)
{
#if FLAGS
    _mm_setcsr(_mm_getcsr() & ~0x3fu);
#endif
}

// Whether an operation since Clear had a subnormal operand.
static bool Flagged(void)
{
#if FLAGS
    return (_mm_getcsr() & 0x2u) != 0;
#else
    return false;
#endif
}

int main(void)
{
    maudNoiseSuppressorDef nd = maudDefaultNoiseSuppressorDef();
    nd.sampleRate = RATE;
    maudGainControlDef gd = maudDefaultGainControlDef();
    gd.sampleRate = RATE;
    maudVoiceDetectorDef vd = maudDefaultVoiceDetectorDef();
    vd.sampleRate = RATE;
    maudEchoCancellerDef ed = maudDefaultEchoCancellerDef();
    ed.sampleRate = RATE;
    maudNoiseSuppressor* noise = nullptr;
    maudGainControl* gain = nullptr;
    maudVoiceDetector* detector = nullptr;
    maudEchoCanceller* echo = nullptr;
    maudEchoCanceller* near = nullptr;
    CHECK(maudCreateNoiseSuppressor(&nd, &noise) == maud_success &&
              maudCreateGainControl(&gd, &gain) == maud_success &&
              maudCreateVoiceDetector(&vd, &detector) == maud_success &&
              maudCreateEchoCanceller(&ed, &echo) == maud_success &&
              maudCreateEchoCanceller(&ed, &near) == maud_success,
          "the four processors at 48 kHz");
    if (s_failures != 0)
    {
        return 1;
    }
    static float render[BLOCK];
    static float capture[BLOCK];
    static float frames[BLOCK];
    static float none[BLOCK];
    uint32_t state = 21;
    float path = 0.0f;
    uint32_t flagged[5] = {0, 0, 0, 0, 0};
    bool zeros = true;
    for (uint32_t b = 0; b < LOUD + QUIET; ++b)
    {
        bool loud = b < LOUD;
        for (uint32_t i = 0; i < BLOCK; ++i)
        {
            state = state * 1664525u + 1013904223u;
            float n = (float)(state >> 8) / 16777216.0f - 0.5f;
            render[i] = loud ? 0.2f * n : 0.0f;
            path = loud ? 0.9f * path + 0.05f * render[i] : 0.0f;
            capture[i] = loud ? path + 0.05f * n * n : 0.0f;
        }
        bool last = b >= LOUD + QUIET - LAST;
        memcpy(frames, capture, sizeof(frames));
        Clear();
        maudResult r = maudSuppressNoise(noise, frames, BLOCK, nullptr);
        flagged[0] += last && Flagged() ? 1u : 0u;
        for (uint32_t i = 0; i < BLOCK && last; ++i)
        {
            zeros = zeros && frames[i] == 0.0f;
        }
        memcpy(frames, capture, sizeof(frames));
        Clear();
        r = r != maud_success ? r : maudApplyGainControl(gain, frames, BLOCK, nullptr);
        flagged[1] += last && Flagged() ? 1u : 0u;
        Clear();
        r = r != maud_success ? r : maudDetectVoice(detector, capture, BLOCK, nullptr);
        flagged[2] += last && Flagged() ? 1u : 0u;
        memcpy(frames, capture, sizeof(frames));
        Clear();
        r = r != maud_success ? r : maudCancelEcho(echo, frames, render, BLOCK, nullptr);
        flagged[3] += last && Flagged() ? 1u : 0u;
        for (uint32_t i = 0; i < BLOCK && last; ++i)
        {
            zeros = zeros && frames[i] == 0.0f;
        }
        memcpy(frames, capture, sizeof(frames));
        Clear();
        r = r != maud_success ? r : maudCancelEcho(near, frames, none, BLOCK, nullptr);
        flagged[4] += last && Flagged() ? 1u : 0u;
        for (uint32_t i = 0; i < BLOCK && last; ++i)
        {
            zeros = zeros && frames[i] == 0.0f;
        }
        if (r != maud_success)
        {
            CHECK(false, "every call succeeds");
            break;
        }
    }
    CHECK(zeros, "silence in, zeros out: no subnormal leftovers");
    uint32_t total = flagged[0] + flagged[1] + flagged[2] + flagged[3] + flagged[4];
    CHECK(total == 0, "no subnormal operand in silence (x86-64)");
    if (total != 0)
    {
        printf(
            "voice_silence: calls flagged of %u: noise %u, gain %u, detector %u, echo %u and %u\n",
            (unsigned)LAST, flagged[0], flagged[1], flagged[2], flagged[3], flagged[4]);
    }
    maudDestroyEchoCanceller(near);
    maudDestroyEchoCanceller(echo);
    maudDestroyVoiceDetector(detector);
    maudDestroyGainControl(gain);
    maudDestroyNoiseSuppressor(noise);
    return s_failures == 0 ? 0 : 1;
}
