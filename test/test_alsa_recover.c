// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// ALSA's recovery from a failed transfer, against a fake PCM: an -EPIPE
// is an xrun, counted by the stream's direction, and other failures are
// not; a recovered capture is started again and a recovered playback
// is not; a PCM that cannot recover ends the stream.

#include "alsa_stream.h"
#include "test_harness.h"

#include <errno.h>

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
    return s_failures == 0 ? 0 : 1;
}
