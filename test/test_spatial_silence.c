// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The spatial effects in digital silence, at 48 kHz in 10 ms blocks:
// after a second of noise, nine seconds of zeros (reverbs of 0.3 s, a
// tail of 0.4, reach the subnormal range within five). A binaural effect
// with its near field, its source still and moving throughout (on the
// shipped set; not on the web, which has no file system), a direct
// effect behind a wall, and reverbs with and without a tail, and one in
// blocks of 100 ms, longer than its shortest delay line, put out
// zeros again, not subnormal leftovers of their filters and delay
// lines; on x86-64, where the processor flags an operation on a
// subnormal operand, none of their calls in the last 200 blocks raises
// that flag (each such operation is many times slower).

// fopen reads the shipped set; the C runtime's warning that it is unsafe
// is about the Annex K alternative, which the family does not use.
#define _CRT_SECURE_NO_WARNINGS

#include "test_harness.h"

#include "maul-audio/binaural.h"
#include "maul-audio/direct.h"
#include "maul-audio/reverb.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#define FLAGS 1
#else
#define FLAGS 0
#endif

enum
{
    FRAMES = 480,
    LONG = 4800,
    LOUD = 100,
    QUIET = 900,
    LAST = 200
};

static float s_in[LONG];
static float s_out[4][LONG];
static uint32_t s_state = 17;

static void Fill(bool loud, uint32_t frames)
{
    for (uint32_t i = 0; i < frames; ++i)
    {
        s_state = s_state * 1664525u + 1013904223u;
        s_in[i] = loud ? 0.3f * ((float)(s_state >> 8) / 16777216.0f - 0.5f) : 0.0f;
    }
}

static void Clear(void)
{
#if FLAGS
    _mm_setcsr(_mm_getcsr() & ~0x3fu);
#endif
}

static bool Flagged(void)
{
#if FLAGS
    return (_mm_getcsr() & 0x2u) != 0;
#else
    return false;
#endif
}

// One effect's run: the calls flagged and whether its outputs (channels
// of them) were all zero in the last blocks.
typedef struct Tally
{
    uint32_t flagged;
    bool zeros;
} Tally;

static void Note(Tally* t, uint32_t block, uint32_t channels, uint32_t frames)
{
    if (block < LOUD + QUIET - LAST)
    {
        return;
    }
    t->flagged += Flagged() ? 1u : 0u;
    for (uint32_t c = 0; c < channels; ++c)
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            t->zeros = t->zeros && s_out[c][i] == 0.0f;
        }
    }
}

static void Report(const char* name, const Tally* t)
{
    CHECK(t->zeros, "silence in, zeros out");
    CHECK(t->flagged == 0, "no subnormal operand in silence (x86-64)");
    if (!t->zeros || t->flagged != 0)
    {
        printf("spatial_silence: %s: %u of %u calls flagged, zeros %d\n", name, t->flagged,
               (unsigned)LAST, (int)t->zeros);
    }
}

static void TestBinaural(bool moving)
{
#ifdef __EMSCRIPTEN__
    (void)moving;
    printf("binaural skipped on the web: no file system\n");
#else
    FILE* file = fopen(MAUD_DATA_DIR "/hrtf/sadie2-ku100-48k.maudhrtf", "rb");
    CHECK(file != nullptr, "the shipped set");
    if (file == nullptr)
    {
        return;
    }
    static unsigned char bytes[900000];
    size_t count = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    maudHrtfDef hd = maudDefaultHrtfDef();
    hd.bytes = bytes;
    hd.byteCount = count;
    maudHrtf* hrtf = nullptr;
    CHECK(maudLoadHrtf(&hd, &hrtf) == maud_success, "the set loads");
    maudBinauralDef def = maudDefaultBinauralDef();
    def.hrtf = hrtf;
    def.maxFrames = FRAMES;
    def.nearField = true;
    maudBinaural* effect = nullptr;
    CHECK(maudCreateBinaural(&def, &effect) == maud_success, "a binaural effect");
    maudBinauralParams params = {{0.2f, 0.1f, -0.3f}, 1.0f};
    float* out[2] = {s_out[0], s_out[1]};
    Tally t = {0, true};
    for (uint32_t b = 0; b < LOUD + QUIET && effect != nullptr; ++b)
    {
        Fill(b < LOUD, FRAMES);
        if (moving)
        {
            // Nearer and farther within the near field, and round.
            float angle = 0.05f * (float)b;
            float distance = 0.37f + 0.03f * sinf(0.01f * (float)b);
            params.position = (maudVector3){distance * sinf(angle), 0.1f, -distance * cosf(angle)};
        }
        Clear();
        if (maudProcessBinaural(effect, &params, s_in, out, FRAMES) != maud_success)
        {
            CHECK(false, "binaural: every call succeeds");
            break;
        }
        Note(&t, b, 2, FRAMES);
    }
    Report(moving ? "binaural, near field, moving" : "binaural, near field", &t);
    maudDestroyBinaural(effect);
    maudDestroyHrtf(hrtf);
#endif
}

static void TestDirect(void)
{
    maudDirectEffectDef def = maudDefaultDirectEffectDef();
    maudDirectEffect* effect = nullptr;
    CHECK(maudCreateDirectEffect(&def, &effect) == maud_success, "a direct effect");
    maudDirectParams params = maudDefaultDirectParams();
    params.occlusion = 0.8f;
    params.transmission[0] = 0.5f;
    params.transmission[1] = 0.2f;
    params.transmission[2] = 0.05f;
    params.distance = 20.0f;
    Tally t = {0, true};
    for (uint32_t b = 0; b < LOUD + QUIET && effect != nullptr; ++b)
    {
        Fill(b < LOUD, FRAMES);
        Clear();
        if (maudProcessDirect(effect, &params, s_in, s_out[0], FRAMES) != maud_success)
        {
            CHECK(false, "direct: every call succeeds");
            break;
        }
        Note(&t, b, 1, FRAMES);
    }
    Report("direct", &t);
    maudDestroyDirectEffect(effect);
}

static void TestReverb(bool tail, uint32_t frames)
{
    maudReverbDef def = maudDefaultReverbDef();
    def.tail = tail;
    maudReverb* reverb = nullptr;
    CHECK(maudCreateReverb(&def, &reverb) == maud_success, "a reverb");
    maudReverbParams params = {{0.3f, 0.25f, 0.2f},
                               {0.0f, 0.0f, 0.0f},
                               0.0f,
                               {tail ? 0.4f : 0.0f, tail ? 0.35f : 0.0f, tail ? 0.3f : 0.0f},
                               {-10.0f, -10.0f, -10.0f}};
    float* bed[4] = {s_out[0], s_out[1], s_out[2], s_out[3]};
    Tally t = {0, true};
    // The same 10 s in longer blocks.
    uint32_t loud = LOUD * FRAMES / frames;
    uint32_t blocks = (LOUD + QUIET) * FRAMES / frames;
    for (uint32_t b = 0; b < blocks && reverb != nullptr; ++b)
    {
        Fill(b < loud, frames);
        // The reverb adds into the bed.
        memset(s_out, 0, sizeof(s_out));
        Clear();
        if (maudProcessReverb(reverb, &params, s_in, bed, frames) != maud_success)
        {
            CHECK(false, "reverb: every call succeeds");
            break;
        }
        // Counted in 10 ms blocks, so the window is the same 2 s.
        Note(&t, b * frames / FRAMES, 4, frames);
    }
    Report(tail ? "reverb with a tail" : frames > FRAMES ? "reverb in long blocks" : "reverb", &t);
    maudDestroyReverb(reverb);
}

int main(void)
{
    TestBinaural(false);
    TestBinaural(true);
    TestDirect();
    TestReverb(false, FRAMES);
    TestReverb(true, FRAMES);
    TestReverb(false, LONG);
    return s_failures == 0 ? 0 : 1;
}
