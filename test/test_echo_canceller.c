// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The echo canceller: a render of noise heard through a fixed room
// response (10 ms late, 60 ms long) is taken down 50 dB or more within
// three seconds at 8, 16 and 48 kHz (at 8 kHz, the suppressor's bands
// above the 4 kHz Nyquist hold no frequencies), and at 16 kHz scaled by
// 2^63, and it reports having adapted with a low leakage; a near end of
// noise at about the echo's level
// (the hardest double talk) comes through within 3 dB with the rest 6
// dB under it, and the echo stays down 30 dB between its bursts; with
// nothing played, tone bursts keep their level within a decibel while
// the noise between them falls; the output lags by exactly two blocks
// and does not depend on how the frames are cut; on a bent echo path a
// first second with nothing played leaves the echo taken down as from
// the start, and silence alone leaves it unadapted with a high leakage;
// an echo 18 dB louder than the render, with the near end speaking from
// the start, is learnt within three seconds; a capture muted for the
// first second leaves it unadapted, and the echo after is learnt as
// after a second of both silent; a capture muted in a call leaves the
// output silent and the echo path kept; a path 40 dB quieter and moved
// is learnt anew, and a near end over it comes through;
// the probability of speech is 0 until two blocks have been processed,
// and from 0.1 after; refusals; and no allocation while processing. (Its quality on
// recorded speech is measured outside the tests, against Speex's.)

#include "test_harness.h"

#include "maul-audio/voice.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SECONDS    4u
#define MAX_RATE   48000u
#define MAX_FRAMES (MAX_RATE * SECONDS)

static int s_allocations = 0;

static void* Counted(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_allocations++;
    return malloc(size);
}

static void Release(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

static uint32_t s_state = 11u;

static float Gaussian(void)
{
    float sum = 0.0f;
    for (int i = 0; i < 12; ++i)
    {
        s_state = s_state * 1664525u + 1013904223u;
        sum += (float)(s_state >> 8) / 16777216.0f;
    }
    return sum - 6.0f;
}

static float s_render[MAX_FRAMES];
static float s_echo[MAX_FRAMES];
static float s_near[MAX_FRAMES];
static float s_capture[MAX_FRAMES];
static float s_other[MAX_FRAMES];

// The render (noise at -20 dBFS), its echo through a room response 10 ms
// late with 60 ms of exponentially decaying taps, and a near end of
// noise bursts (a quarter second on, a quarter off) from the third
// second on, at the echo's level when near is set. Nothing is played
// for the first quiet frames, and curve bends the echo path: a sample s
// of the linear echo is heard as s (1 + curve |s|).
static void SceneWith(uint32_t rate, uint32_t frames, bool near, uint32_t quiet, float curve)
{
    s_state = 11u;
    uint32_t delay = rate / 100;
    uint32_t taps = rate * 6 / 100;
    static float response[MAX_RATE * 6 / 100];
    for (uint32_t t = 0; t < taps; ++t)
    {
        response[t] = 0.08f * Gaussian() * expf(-6.0f * (float)t / (float)taps);
    }
    for (uint32_t i = 0; i < frames; ++i)
    {
        s_render[i] = i < quiet ? 0.0f : 0.1f * Gaussian();
    }
    for (uint32_t i = 0; i < frames; ++i)
    {
        float sum = 0.0f;
        for (uint32_t t = 0; t < taps && t + delay <= i; ++t)
        {
            sum += response[t] * s_render[i - delay - t];
        }
        s_echo[i] = sum * (1.0f + curve * fabsf(sum));
        bool speaks = near && i >= 2 * rate && (i / (rate / 4)) % 2 == 0;
        s_near[i] = speaks ? 0.05f * Gaussian() : 0.0f;
        s_capture[i] = s_echo[i] + s_near[i];
    }
}

static void Scene(uint32_t rate, uint32_t frames, bool near)
{
    SceneWith(rate, frames, near, 0, 0.0f);
}

static double Energy(const float* x, uint32_t from, uint32_t to)
{
    double sum = 0.0;
    for (uint32_t i = from; i < to; ++i)
    {
        sum += (double)x[i] * (double)x[i];
    }
    return sum;
}

static maudEchoCanceller* Create(uint32_t rate)
{
    maudEchoCancellerDef def = maudDefaultEchoCancellerDef();
    def.sampleRate = rate;
    def.allocator = (maudAllocator){Counted, Release, nullptr};
    maudEchoCanceller* c = nullptr;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_success && c != nullptr, "a canceller");
    return c;
}

// Runs the capture through a canceller in chunks of chunk frames.
static maudEchoState Run(maudEchoCanceller* c, float* capture, uint32_t frames, uint32_t chunk)
{
    maudEchoState state = {0};
    for (uint32_t at = 0; at < frames; at += chunk)
    {
        uint32_t count = frames - at < chunk ? frames - at : chunk;
        CHECK(maudEchoCanceller_Cancel(c, capture + at, s_render + at, count, &state) ==
                  maud_success,
              "cancel");
    }
    return state;
}

static void TestConverges(uint32_t rate, uint32_t block)
{
    uint32_t frames = rate * SECONDS;
    Scene(rate, frames, false);
    maudEchoCanceller* c = Create(rate);
    int before = s_allocations;
    maudEchoState state = Run(c, s_capture, frames, rate / 100);
    CHECK(s_allocations == before, "no allocation while processing");
    // The output lags by two blocks: compare it with the echo two back.
    double erle = 10.0 * log10(Energy(s_echo, 3 * rate - 2 * block, frames - 2 * block) /
                               Energy(s_capture, 3 * rate, frames));
    printf("%u Hz: %.1f dB taken from the echo, leakage %.4f\n", rate, erle, (double)state.leakage);
    CHECK(erle > 50.0, "the echo taken down 50 dB or more");
    CHECK(state.adapted && state.leakage < 0.05f && state.frameCount == frames,
          "adapted, little left");
    maudDestroyEchoCanceller(c);
}

// The same scene with the echo 18 dB louder than the render, as a loud
// speaker playing a quiet render, and the near end in bursts at the
// echo's level from the start: learnt within three seconds (an echo at
// the render's level is learnt in one), and the echo taken down 20 dB or
// more between the bursts of the last second (25 dB at the render's
// level).
static void TestLoudEcho(void)
{
    const uint32_t rate = 16000;
    const uint32_t block = 256;
    uint32_t frames = rate * SECONDS;
    Scene(rate, frames, false);
    float gain = sqrtf((float)(63.1 * Energy(s_render, 0, frames) / Energy(s_echo, 0, frames)));
    float level = sqrtf((float)(Energy(s_render, 0, frames) * 63.1 / frames));
    for (uint32_t i = 0; i < frames; ++i)
    {
        s_echo[i] *= gain;
        s_near[i] = (i / (rate / 4)) % 2 == 0 ? level * Gaussian() : 0.0f;
        s_capture[i] = s_echo[i] + s_near[i];
    }
    maudEchoCanceller* c = Create(rate);
    bool learnt = false;
    for (uint32_t at = 0; at < frames; at += 160)
    {
        maudEchoState state = {0};
        CHECK(maudEchoCanceller_Cancel(c, s_capture + at, s_render + at, 160, &state) ==
                  maud_success,
              "cancel");
        learnt = learnt || (state.adapted && at < 3 * rate);
    }
    double gap = 0.0;
    double echo = 0.0;
    for (uint32_t i = frames - rate; i < frames; ++i)
    {
        if (s_near[i - block] == 0.0f && s_near[i] == 0.0f)
        {
            gap += (double)s_capture[i] * (double)s_capture[i];
            echo += (double)s_echo[i - block] * (double)s_echo[i - block];
        }
    }
    double held = 10.0 * log10(echo / gap);
    printf("an echo 18 dB over the render: %s in three seconds, %.1f dB taken between the bursts\n",
           learnt ? "learnt" : "not learnt", held);
    CHECK(learnt, "an echo louder than the render learnt within three seconds");
    CHECK(held > 20.0, "and taken down between the near end's bursts");
    maudDestroyEchoCanceller(c);
}

// The echo taken down from the second second to the third after the
// echo is first heard, at 16 kHz, when the capture is muted (zeros) for
// the first second while the render plays, or while it too is silent
// (dB under the echo); adapted says whether the canceller reported
// having adapted at the end of that second.
static double MutedErle(bool played, bool* adapted)
{
    const uint32_t rate = 16000;
    const uint32_t block = 256;
    uint32_t muted = rate;
    uint32_t frames = muted + 3 * rate;
    Scene(rate, frames, false);
    for (uint32_t i = 0; i < muted; ++i)
    {
        s_render[i] = played ? s_render[i] : 0.0f;
        s_echo[i] = 0.0f;
        s_capture[i] = 0.0f;
    }
    maudEchoCanceller* c = Create(rate);
    *adapted = false;
    for (uint32_t at = 0; at < frames; at += 160)
    {
        maudEchoState state = {0};
        CHECK(maudEchoCanceller_Cancel(c, s_capture + at, s_render + at, 160, &state) ==
                  maud_success,
              "cancel");
        *adapted = at + 160 == muted ? state.adapted : *adapted;
    }
    maudDestroyEchoCanceller(c);
    return 10.0 * log10(Energy(s_echo, frames - rate - 2 * block, frames - 2 * block) /
                        Energy(s_capture, frames - rate, frames));
}

// A capture muted for the first second while the render plays: no echo
// path learnt from the silence, and the echo after learnt within 6 dB of
// one heard after a second of both silent.
static void TestMutedStart(void)
{
    bool adapted = false;
    double from = MutedErle(false, &adapted);
    double muted = MutedErle(true, &adapted);
    printf("a muted first second: %s while muted, %.1f dB taken in the third second heard, "
           "%.1f dB after a silent second\n",
           adapted ? "adapted" : "unadapted", muted, from);
    CHECK(!adapted, "no echo path learnt from a muted capture");
    CHECK(muted > from - 6.0, "and the echo learnt once it is heard");
}

// The echo taken down over the third second after a capture muted
// (zeros) for muted frames from the third second comes back, at 16 kHz
// (dB under the echo); held is the output while muted, in dB under the
// echo it stands for, and adapted the state at the end.
static double MidcallErle(uint32_t muted, double* held, bool* adapted)
{
    const uint32_t rate = 16000;
    const uint32_t block = 256;
    uint32_t from = 3 * rate;
    uint32_t frames = from + muted + 3 * rate;
    Scene(rate, frames, false);
    memset(s_capture + from, 0, muted * sizeof(float));
    maudEchoCanceller* c = Create(rate);
    maudEchoState state = Run(c, s_capture, frames, 160);
    maudDestroyEchoCanceller(c);
    *adapted = state.adapted;
    *held = 10.0 * log10(Energy(s_echo, from, from + muted - 2 * block) /
                         (Energy(s_capture, from + 2 * block, from + muted) + 1e-30));
    return 10.0 * log10(Energy(s_echo, frames - rate - 2 * block, frames - 2 * block) /
                        Energy(s_capture, frames - rate, frames));
}

// A capture muted in a call, for half a second or for three: the output
// stays silent while muted, nothing of the estimate subtracted, and the
// echo after is taken down within 6 dB of one never muted.
static void TestMidcallMute(void)
{
    double held = 0.0;
    bool adapted = false;
    double never = MidcallErle(0, &held, &adapted);
    double brief = MidcallErle(16000 / 2, &held, &adapted);
    printf("a mute in a call: %.1f dB taken in the third second after half a second, ", brief);
    CHECK(held > 60.0 && adapted, "silent while muted half a second, still adapted");
    double heldBrief = held;
    double longer = MidcallErle(3 * 16000, &held, &adapted);
    printf("%.1f dB after three, %.1f dB never muted; the output %.1f and %.1f dB under the echo "
           "while muted\n",
           longer, never, heldBrief, held);
    CHECK(held > 60.0 && adapted, "silent while muted three seconds, still adapted");
    CHECK(brief > never - 6.0 && longer > never - 6.0, "the echo path kept through a mute");
}

// The echo after the third second heard through a path 40 dB quieter
// and 2 ms later, at 16 kHz, with a near end of noise bursts (half a
// second on, half off) at near times the old echo's level from then on.
// first and last are the echo taken down over the first and the ninth
// second after the change, between the bursts (dB under the echo);
// kept is the near end's level in the output while it speaks, and rest
// what is not the near end, under it (dB).
static void Moved(float near, double* first, double* last, double* kept, double* rest)
{
    const uint32_t rate = 16000;
    // The output lags two blocks of 128.
    const uint32_t lag = 256;
    uint32_t from = 3 * rate;
    uint32_t frames = from + 9 * rate;
    Scene(rate, frames, false);
    float level = sqrtf((float)(Energy(s_echo, 0, from) / from));
    memcpy(s_other, s_echo, frames * sizeof(float));
    for (uint32_t i = from; i < frames; ++i)
    {
        s_echo[i] = 0.01f * s_other[i - rate / 500];
        s_near[i] = (i / (rate / 2)) % 2 == 0 ? near * level * Gaussian() : 0.0f;
        s_capture[i] = s_echo[i] + s_near[i];
    }
    maudEchoCanceller* c = Create(rate);
    (void)Run(c, s_capture, frames, 160);
    maudDestroyEchoCanceller(c);
    double echo[2] = {0.0, 0.0};
    double gap[2] = {0.0, 0.0};
    double said = 0.0;
    double heard = 0.0;
    double other = 0.0;
    for (uint32_t i = from + lag; i < frames; ++i)
    {
        uint32_t second = (i - from - lag) / rate;
        float n = s_near[i - lag];
        bool on = n != 0.0f && s_near[i - lag - rate / 10] != 0.0f;
        bool off = n == 0.0f && s_near[i - lag - rate / 10] == 0.0f;
        if (off && (second == 0 || second == 8))
        {
            echo[second / 8] += (double)s_echo[i - lag] * (double)s_echo[i - lag];
            gap[second / 8] += (double)s_capture[i] * (double)s_capture[i];
        }
        if (on && second >= 3)
        {
            said += (double)n * (double)n;
            heard += (double)s_capture[i] * (double)s_capture[i];
            other += ((double)s_capture[i] - (double)n) * ((double)s_capture[i] - (double)n);
        }
    }
    *first = 10.0 * log10(echo[0] / gap[0]);
    *last = 10.0 * log10(echo[1] / gap[1]);
    *kept = near > 0.0f ? 10.0 * log10(heard / said) : 0.0;
    *rest = near > 0.0f ? 10.0 * log10(said / other) : 0.0;
}

// A path that turns 40 dB quieter and moves (a speaker turned down and
// set aside) is learnt anew, not taken for a mute: the output is never
// louder than the quiet echo, which is taken down 20 dB by the ninth
// second. A near end 20 dB under the old echo over it, as quiet as a
// mute's capture against the estimate, comes through within a decibel.
static void TestQuietPath(void)
{
    double first = 0.0;
    double last = 0.0;
    double kept = 0.0;
    double rest = 0.0;
    Moved(0.0f, &first, &last, &kept, &rest);
    printf("a path 40 dB quieter and moved: %.1f dB taken in the first second, %.1f dB in the "
           "ninth; ",
           first, last);
    CHECK(first > 0.0 && last > 20.0, "a quieter path learnt anew");
    Moved(0.1f, &first, &last, &kept, &rest);
    printf("a near end 20 dB under the old echo over it at %+.2f dB, the rest %.1f dB under it\n",
           kept, rest);
    CHECK(fabs(kept) < 1.0 && rest > 10.0, "a quiet near end kept over a quiet path");
}

// The echo left over the last second of a bent echo path at 16 kHz,
// after the first quiet frames played nothing (dB under the echo).
static double BentErle(uint32_t quiet)
{
    const uint32_t rate = 16000;
    uint32_t frames = rate * SECONDS;
    SceneWith(rate, frames, false, quiet, 10.0f);
    maudEchoCanceller* c = Create(rate);
    (void)Run(c, s_capture, frames, rate / 100);
    maudDestroyEchoCanceller(c);
    return 10.0 * log10(Energy(s_echo, 3 * rate - 2 * 128, frames - 2 * 128) /
                        Energy(s_capture, 3 * rate, frames));
}

// An echo path bent enough to leave some echo the filter cannot model:
// a first second with nothing played, every band's echo estimate
// silent, leaves the leakage to be learnt as from the start, the echo
// taken down within 2 dB of a run that played from the first frame.
static void TestQuietFirst(void)
{
    // Nothing played or heard for a second: not adapted, its leakage
    // still high, as while it learns.
    memset(s_render, 0, 16000 * sizeof(float));
    memset(s_capture, 0, 16000 * sizeof(float));
    maudEchoCanceller* c = Create(16000);
    maudEchoState silent = Run(c, s_capture, 16000, 160);
    maudDestroyEchoCanceller(c);
    CHECK(!silent.adapted && silent.leakage >= 0.25f, "silence learns nothing");
    double played = BentErle(0);
    double quiet = BentErle(16000);
    printf("a bent echo path: %.1f dB taken down, %.1f dB after a quiet second\n", played, quiet);
    CHECK(quiet > 20.0 && fabs(quiet - played) < 2.0, "a quiet start leaves the leakage to learn");
}

// Until two blocks have been processed, nothing has been weighed: the
// probability of speech is 0. From then on it is 0.1 or more, even over
// silence.
static void TestTwoBlocks(void)
{
    memset(s_render, 0, 2 * 128 * sizeof(float));
    memset(s_capture, 0, 2 * 128 * sizeof(float));
    maudEchoCanceller* c = Create(16000);
    maudEchoState before = Run(c, s_capture, 2 * 128 - 1, 2 * 128 - 1);
    maudEchoState after = Run(c, s_capture + 2 * 128 - 1, 1, 1);
    maudDestroyEchoCanceller(c);
    CHECK(before.speechProbability == 0.0f, "0 until two blocks have been processed");
    CHECK(after.speechProbability >= 0.1f && after.speechProbability <= 1.0f,
          "from 0.1 once two blocks have been processed");
}

// The same scene scaled by 2^63, its peaks near 2^62, far past full scale
// but within the 2^64 the header names: the echo is taken down as at
// full scale. The filter's steps multiply two transforms' bins, each
// past 2^64 at that level.
static void TestLoud(void)
{
    const uint32_t rate = 16000;
    uint32_t frames = rate * SECONDS;
    Scene(rate, frames, false);
    const float scale = ldexpf(1.0f, 63);
    for (uint32_t i = 0; i < frames; ++i)
    {
        s_render[i] *= scale;
        s_echo[i] *= scale;
        s_capture[i] *= scale;
    }
    maudEchoCanceller* c = Create(rate);
    maudEchoState state = Run(c, s_capture, frames, rate / 100);
    maudDestroyEchoCanceller(c);
    double erle = 10.0 * log10(Energy(s_echo, 3 * rate - 2 * 128, frames - 2 * 128) /
                               Energy(s_capture, 3 * rate, frames));
    printf("scaled by 2^63: %.1f dB taken from the echo, leakage %.4f\n", erle,
           (double)state.leakage);
    CHECK(erle > 50.0 && state.adapted && state.leakage < 0.05f,
          "the echo taken down at 2^63 as at full scale");
}

static void TestDoubleTalk(void)
{
    const uint32_t rate = 16000;
    const uint32_t block = 256;
    uint32_t frames = rate * SECONDS;
    Scene(rate, frames, true);
    maudEchoCanceller* c = Create(rate);
    (void)Run(c, s_capture, frames, 160);
    // From the third second on: while the near end speaks, the output's
    // level and what in it is not the near end; between its bursts, the
    // echo still taken.
    double out = 0.0;
    double error = 0.0;
    double near = 0.0;
    double gap = 0.0;
    double echo = 0.0;
    for (uint32_t i = 2 * rate + block; i < frames; ++i)
    {
        double x = (double)s_capture[i];
        double n = (double)s_near[i - block];
        if (n != 0.0)
        {
            out += x * x;
            error += (x - n) * (x - n);
            near += n * n;
        }
        else
        {
            gap += x * x;
            echo += (double)s_echo[i - block] * (double)s_echo[i - block];
        }
    }
    double level = 10.0 * log10(out / near);
    double rest = 10.0 * log10(error / near);
    double held = 10.0 * log10(echo / gap);
    printf("double talk: the near end at %+.2f dB, the rest %.1f dB under it; between, %.1f dB "
           "taken\n",
           level, rest, held);
    CHECK(fabs(level) < 3.0, "the near end comes through");
    CHECK(rest < -6.0, "with little echo left over it");
    CHECK(held > 30.0, "and the filter holds between its bursts");
    maudDestroyEchoCanceller(c);
}

static void TestFraming(void)
{
    const uint32_t rate = 16000;
    uint32_t frames = rate * 2;
    Scene(rate, frames, true);
    memcpy(s_other, s_capture, frames * sizeof(float));
    maudEchoCanceller* a = Create(rate);
    maudEchoCanceller* b = Create(rate);
    (void)Run(a, s_capture, frames, 480);
    uint32_t at = 0;
    for (uint32_t n = 1; at < frames; n = n % 37 + 1)
    {
        uint32_t count = frames - at < n ? frames - at : n;
        CHECK(maudEchoCanceller_Cancel(b, s_other + at, s_render + at, count, nullptr) ==
                  maud_success,
              "cancel");
        at += count;
    }
    CHECK(memcmp(s_capture, s_other, frames * sizeof(float)) == 0, "the same however cut");
    maudDestroyEchoCanceller(a);
    maudDestroyEchoCanceller(b);
}

// With nothing played: speech-like bursts (a 1 kHz tone, 0.2 s on and
// off) over quiet noise keep their level within a decibel while the
// noise between them falls; the first two blocks out are silence.
static void TestNearAlone(void)
{
    const uint32_t rate = 16000;
    const uint32_t lag = 256;
    uint32_t frames = 3 * rate;
    s_state = 3u;
    for (uint32_t i = 0; i < frames; ++i)
    {
        bool on = (i / (rate / 5)) % 2 == 1;
        float tone = 0.1f * sinf(2.0f * 3.14159265f * 1000.0f * (float)i / (float)rate);
        s_render[i] = 0.0f;
        s_near[i] = on ? tone : 0.0f;
        s_capture[i] = s_near[i] + 0.003f * Gaussian();
        s_other[i] = s_capture[i];
    }
    maudEchoCanceller* c = Create(rate);
    maudEchoState state = Run(c, s_capture, frames, 160);
    double kept = 0.0;
    double near = 0.0;
    double gapOut = 0.0;
    double gapIn = 0.0;
    for (uint32_t i = rate; i < frames; ++i)
    {
        // Away from the bursts' edges, where the gain moves.
        uint32_t into = (i - lag) % (rate / 5);
        if (into < rate / 50 || into > rate / 5 - rate / 50)
        {
            continue;
        }
        bool on = s_near[i - lag] != 0.0f;
        double out = (double)s_capture[i] * (double)s_capture[i];
        double in = (double)s_other[i - lag] * (double)s_other[i - lag];
        kept += on ? out : 0.0;
        near += on ? in : 0.0;
        gapOut += on ? 0.0 : out;
        gapIn += on ? 0.0 : in;
    }
    double change = 10.0 * log10(kept / near);
    double fell = 10.0 * log10(gapIn / gapOut);
    printf("no render: the bursts changed by %+.2f dB, the noise between them down %.1f dB\n",
           change, fell);
    CHECK(fabs(change) < 1.0, "the near end kept");
    CHECK(fell > 5.0, "the noise taken down");
    CHECK(state.speechProbability > 0.1f && state.speechProbability <= 1.0f, "a probability");
    bool silent = true;
    for (uint32_t i = 0; i < lag; ++i)
    {
        silent = silent && s_capture[i] == 0.0f;
    }
    CHECK(silent && s_capture[lag + 1] != 0.0f, "two blocks late: the first two out are silence");
    maudDestroyEchoCanceller(c);
}

static void TestRefused(void)
{
    maudEchoCanceller* c = nullptr;
    maudEchoCancellerDef def = maudDefaultEchoCancellerDef();
    CHECK(maudCreateEchoCanceller(nullptr, &c) == maud_errorInvalid && c == nullptr, "no def");
    CHECK(maudCreateEchoCanceller(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    def.cookie = 0;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_errorInvalid, "no cookie");
    def = maudDefaultEchoCancellerDef();
    def.sampleRate = 7999;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_errorInvalid, "a rate too low");
    def = maudDefaultEchoCancellerDef();
    def.tailTime = 1.5f;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_errorInvalid, "a path too long");
    def.tailTime = NAN;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_errorInvalid, "a path not a number");
    def = maudDefaultEchoCancellerDef();
    def.floorDb = -50.0f;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_errorInvalid, "a floor too deep");
    // The highest rate, the longest path and the shallowest floor.
    def = maudDefaultEchoCancellerDef();
    def.sampleRate = 384000;
    def.tailTime = 1.0f;
    def.floorDb = -6.0f;
    CHECK(maudCreateEchoCanceller(&def, &c) == maud_success, "every bound's upper end taken");
    maudDestroyEchoCanceller(c);
    c = Create(16000);
    float x = 0.0f;
    CHECK(maudEchoCanceller_Cancel(nullptr, &x, &x, 1, nullptr) == maud_errorInvalid,
          "no canceller");
    CHECK(maudEchoCanceller_Cancel(c, nullptr, &x, 1, nullptr) == maud_errorInvalid, "no capture");
    CHECK(maudEchoCanceller_Cancel(c, &x, nullptr, 1, nullptr) == maud_errorInvalid, "no render");
    CHECK(maudEchoCanceller_Cancel(c, nullptr, nullptr, 0, nullptr) == maud_success, "no frames");
    maudDestroyEchoCanceller(c);
    maudDestroyEchoCanceller(nullptr);
}

int main(void)
{
    TestConverges(8000, 64);
    TestConverges(16000, 128);
    TestConverges(48000, 512);
    TestLoud();
    TestDoubleTalk();
    TestLoudEcho();
    TestMutedStart();
    TestMidcallMute();
    TestQuietPath();
    TestQuietFirst();
    TestTwoBlocks();
    TestFraming();
    TestNearAlone();
    TestRefused();
    return s_failures == 0 ? 0 : 1;
}
