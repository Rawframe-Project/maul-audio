// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The web streams' clock filter. Through a PulseAudio server a browser's
// output timestamp drifts late for seconds and then snaps back, while the
// frames reach the device evenly (research in the docs): the earliest
// time a stream's frame 0 is heard or was captured, over the last five
// seconds, is the one that holds.

#ifndef MAUL_AUDIO_SRC_WEB_CLOCK_H
#define MAUL_AUDIO_SRC_WEB_CLOCK_H

#include "context_core.h"

#include <stdint.h>

// The earliest frame-0 time of each of the last five seconds, by the
// second it was taken in.
typedef struct maudWebClock
{
    int64_t second[5];
    int64_t earliest[5];
} maudWebClock;

// Forgets every second: the stream started again, ran short or moved.
void maudWebResetClock(maudWebClock* clock);

// Takes a frame-0 time from a timestamp taken at now, both in
// nanoseconds, and returns the earliest of the last five seconds.
int64_t maudWebEarliest(maudWebClock* clock, int64_t origin, int64_t now);

// Filters a latency from the browser's timestamp for the frame at the
// stream's position: through the earliest time frame 0 is heard (an
// output) or was captured (an input) over the last five seconds. A route
// epoch other than *clockEpoch starts the filter again.
int64_t maudWebFilterLatency(maudWebClock* clock, int* clockEpoch, const maudStreamCore* core,
                             int64_t latency, int epoch, bool output);

#endif // MAUL_AUDIO_SRC_WEB_CLOCK_H
