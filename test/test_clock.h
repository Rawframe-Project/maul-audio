// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Checks a running stream's clock against the host clock, for the
// backend tests.

#ifndef MAUL_AUDIO_TEST_TEST_CLOCK_H
#define MAUL_AUDIO_TEST_TEST_CLOCK_H

#include "maul-audio/stream.h"

#include <stdio.h>

// Whether two clock reads a while apart, the later at host time now, are
// sound: stamped, with a latency of at most two seconds, above 0 where
// the route is known to buffer (0 is honest where nothing stands
// between an output and a virtual device), the
// later one's time where its direction puts it (ahead of now for
// output, behind for input, within a callback's lateness), and the two
// ten percent or less, plus their latencies' change, plus 30 ms, off
// the stream's rate. Prints the reads when not.
static inline bool ClockIsSound(const maudStreamClock* first, const maudStreamClock* second,
                                bool output, bool buffered, double rate, int64_t now)
{
    const int64_t late = 250000000;
    int64_t latency = second->latencyNanoseconds;
    bool stamped = first->hostNanoseconds != 0 && second->hostNanoseconds != 0 &&
                   second->position > first->position;
    bool sane = latency >= (buffered ? 1 : 0) && latency <= 2000000000;
    int64_t host = second->hostNanoseconds;
    bool placed = output ? host >= now - late && host <= now + latency
                         : host >= now - latency - late && host <= now;
    double span = (double)(second->hostNanoseconds - first->hostNanoseconds);
    double expected = (double)(second->position - first->position) * 1e9 / rate;
    double drift = (double)(latency - first->latencyNanoseconds);
    double off = span - expected;
    bool steady =
        (off < 0 ? -off : off) <= 0.1 * expected + (drift < 0 ? -drift : drift) + 30000000.0;
    if (!(stamped && sane && placed && steady))
    {
        fprintf(stderr,
                "clock: positions %llu, %llu; hosts %lld, %lld at now %lld; latencies %lld, %lld\n",
                (unsigned long long)first->position, (unsigned long long)second->position,
                (long long)first->hostNanoseconds, (long long)host, (long long)now,
                (long long)first->latencyNanoseconds, (long long)latency);
    }
    return stamped && sane && placed && steady;
}

// Reads a running stream's clock a second apart through the test's own
// sleep, up to three times, until two reads pass ClockIsSound. A
// loaded machine's stalls hold the stream back while host time runs
// on, which spoils a window but never makes one sound.
static inline bool StreamClockIsSound(const maudContext* context, maudStreamId stream, bool output,
                                      bool buffered, void (*sleep)(int milliseconds))
{
    maudStreamFormat format = {0};
    maudStreamClock first = {0};
    maudStreamClock second = {0};
    if (maudGetStreamFormat(context, stream, &format) != maud_success ||
        maudGetStreamClock(context, stream, &first) != maud_success)
    {
        return false;
    }
    for (int window = 0; window < 3; ++window)
    {
        sleep(1000);
        if (maudGetStreamClock(context, stream, &second) != maud_success)
        {
            return false;
        }
        if (ClockIsSound(&first, &second, output, buffered, (double)format.sampleRate,
                         maudGetHostNanoseconds()))
        {
            return true;
        }
        first = second;
    }
    return false;
}

#endif // MAUL_AUDIO_TEST_TEST_CLOCK_H
