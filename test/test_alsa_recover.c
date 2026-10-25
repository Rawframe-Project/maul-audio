// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// ALSA's recovery from a failed transfer, against a fake PCM: an -EPIPE
// is an xrun, counted by the stream's direction, and other failures are
// not; a recovered capture is started again and a recovered playback
// is not; a PCM that cannot recover ends the stream. A transfer whose
// PCM has nothing to move now (-EAGAIN) leaves it as it is, and one
// whose PCM ran dry recovers it. A running output that finds its PCM's
// queue empty before a write, as ALSA's PulseAudio plugin leaves it with no
// -EPIPE, counts one underrun per gap: never on the first fill after a
// prepare or a recovery, after silence written while it did not run,
// when the delay fails, as a PCM in an xrun does, or when the queue is
// reported empty before the last write's frames could have played out.

#include "alsa_stream.h"
#include "test_harness.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static int s_recoverResult;
static int s_recovered;
static int s_recoveredError;
static int s_started;

static int FakeRecover(snd_pcm_t* pcm, int error, int silent)
{
    (void)pcm;
    (void)silent;
    s_recovered++;
    s_recoveredError = error;
    return s_recoverResult;
}

static int FakeStart(snd_pcm_t* pcm)
{
    (void)pcm;
    s_started++;
    return 0;
}

static snd_pcm_sframes_t s_avail;

static snd_pcm_sframes_t FakeAvail(snd_pcm_t* pcm)
{
    (void)pcm;
    return s_avail;
}

static void Reset(maudStreamCore* core, maudDirection direction, int recoverResult)
{
    *core = (maudStreamCore){0};
    core->def.direction = direction;
    atomic_init(&core->underruns, 0);
    atomic_init(&core->overruns, 0);
    s_recoverResult = recoverResult;
    s_recovered = 0;
    s_recoveredError = 0;
    s_started = 0;
}

static snd_pcm_sframes_t s_delay;
static int s_delayResult;

static int FakeDelay(snd_pcm_t* pcm, snd_pcm_sframes_t* delay)
{
    (void)pcm;
    *delay = s_delay;
    return s_delayResult;
}

static snd_pcm_sframes_t FakeWrite(snd_pcm_t* pcm, const void* buffer, snd_pcm_uframes_t frames)
{
    (void)pcm;
    (void)buffer;
    return (snd_pcm_sframes_t)frames;
}

static void Silence(const maudStreamBlock* block, void* user)
{
    (void)user;
    memset(block->output, 0, (size_t)block->frameCount * 2 * sizeof(float));
}

#define FRAMES 64

// One transfer of FRAMES into a PCM with delay frames queued before it,
// pause milliseconds after the last one, and the stream's underruns after
// it. FRAMES at 48 kHz play out in 1.3 ms, so 10 ms is time to drain on
// any machine; a pause is a minimum on the monotonic clock.
static uint64_t WriteWith(maudAlsaStream* entry, snd_pcm_sframes_t delay, int pause)
{
    struct timespec span = {0, (long)pause * 1000000L};
    nanosleep(&span, nullptr);
    s_avail = FRAMES;
    s_delay = delay;
    CHECK(maudAlsaTransfer(entry), "a write");
    return atomic_load(&entry->core->underruns);
}

// The gaps a running output counts from its PCM's queue.
static void TestSilentGaps(void)
{
    static float samples[FRAMES * 2];
    static float periodSamples[FRAMES * 2];
    maudAlsaApi api = {.pcmRecover = FakeRecover,
                       .pcmStart = FakeStart,
                       .pcmAvailUpdate = FakeAvail,
                       .pcmDelay = FakeDelay,
                       .pcmWritei = FakeWrite};
    int dummy = 0;
    maudStreamCore core;
    Reset(&core, maud_directionOutput, 0);
    maudStreamDef def = maudDefaultStreamDef();
    def.callback = Silence;
    core.def = def;
    core.format = (maudStreamFormat){
        .sampleRate = 48000, .periodFrames = FRAMES, .layout = maud_layoutStereo};
    atomic_init(&core.blockRate, 48000);
    maudInitPeriod(&core.period, &def, &core.format, periodSamples);
    atomic_init(&core.state, maud_streamRunning);
    maudAlsaStream entry = {.api = &api,
                            .core = &core,
                            .pcm = (snd_pcm_t*)&dummy,
                            .samples = samples,
                            .bufferFrames = FRAMES};
    s_delayResult = 0;
    CHECK(WriteWith(&entry, 0, 0) == 0, "the first fill after a prepare is no gap");
    CHECK(WriteWith(&entry, 0, 10) == 1, "a queue drained of the stream's frames: one underrun");
    CHECK(WriteWith(&entry, 2 * FRAMES, 10) == 1, "a queue with frames left: none");
    CHECK(WriteWith(&entry, 0, 10) == 2, "the next gap is another");
    s_delayResult = -EPIPE;
    CHECK(WriteWith(&entry, 0, 10) == 2, "a delay that fails counts none: the write's -EPIPE does");
    s_delayResult = 0;
    atomic_store(&core.state, maud_streamIdle);
    CHECK(WriteWith(&entry, 0, 10) == 2, "silence while the stream does not run: none");
    atomic_store(&core.state, maud_streamRunning);
    CHECK(WriteWith(&entry, 0, 10) == 2, "nor the first fill once it runs again");
    CHECK(WriteWith(&entry, 0, 10) == 3, "then a gap counts");
    s_avail = -EPIPE;
    CHECK(maudAlsaTransfer(&entry) && atomic_load(&core.underruns) == 4,
          "an -EPIPE: recovered, one underrun");
    CHECK(WriteWith(&entry, 0, 10) == 4, "the first fill after the recovery: none");
    // Half a second's frames queued, then a queue reported empty at once
    // and for several writes after: the PCM misreports, and those frames
    // and the writes' own are still to play. Once they could have, a
    // queue found empty is a real drain.
    CHECK(WriteWith(&entry, 24000, 10) == 4, "half a second queued: none");
    CHECK(WriteWith(&entry, 0, 0) == 4, "an empty queue reported before it could drain: none");
    CHECK(WriteWith(&entry, 0, 10) == 4 && WriteWith(&entry, 0, 10) == 4 &&
              WriteWith(&entry, 0, 10) == 4,
          "nor while the misreport goes on");
    CHECK(WriteWith(&entry, 0, 600) == 5, "a real drain once the frames could have played");
}

int main(void)
{
    maudAlsaApi api = {.pcmRecover = FakeRecover, .pcmStart = FakeStart};
    int dummy = 0;
    snd_pcm_t* pcm = (snd_pcm_t*)&dummy;
    maudStreamCore core;

    Reset(&core, maud_directionOutput, 0);
    CHECK(maudAlsaRecover(&api, pcm, &core, -EPIPE), "a playback underrun recovers");
    CHECK(atomic_load(&core.underruns) == 1 && atomic_load(&core.overruns) == 0,
          "counted as an underrun");
    CHECK(s_recovered == 1 && s_recoveredError == -EPIPE && s_started == 0,
          "recovered with its error, and not started again");

    Reset(&core, maud_directionInput, 0);
    CHECK(maudAlsaRecover(&api, pcm, &core, -EPIPE), "a capture overrun recovers");
    CHECK(atomic_load(&core.overruns) == 1 && atomic_load(&core.underruns) == 0,
          "counted as an overrun");
    CHECK(s_recovered == 1 && s_started == 1, "and the capture is started again");

    Reset(&core, maud_directionOutput, 0);
    CHECK(maudAlsaRecover(&api, pcm, &core, -ESTRPIPE), "a suspended PCM recovers");
    CHECK(atomic_load(&core.underruns) == 0, "with no xrun counted");

    Reset(&core, maud_directionInput, -ENODEV);
    CHECK(!maudAlsaRecover(&api, pcm, &core, -ENODEV), "a PCM gone past recovery");
    CHECK(s_started == 0, "is not started again");

    api.pcmAvailUpdate = FakeAvail;
    maudAlsaStream entry = {.api = &api, .core = &core, .pcm = pcm};
    Reset(&core, maud_directionOutput, 0);
    s_avail = -EAGAIN;
    CHECK(maudAlsaTransfer(&entry) && s_recovered == 0, "nothing to move now: left as it is");
    s_avail = -EPIPE;
    CHECK(maudAlsaTransfer(&entry) && s_recovered == 1 && atomic_load(&core.underruns) == 1,
          "a playback run dry: recovered, an underrun counted");
    Reset(&core, maud_directionOutput, -ENODEV);
    s_avail = -ENODEV;
    CHECK(!maudAlsaTransfer(&entry), "a PCM gone: the transfer fails");
    TestSilentGaps();
    return s_failures == 0 ? 0 : 1;
}
