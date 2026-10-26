// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Checks a running stream's clock against the host clock, for the
// backend tests.

#ifndef MAUL_AUDIO_TEST_TEST_CLOCK_H
#define MAUL_AUDIO_TEST_TEST_CLOCK_H

#include "maul-audio/stream.h"

#include <stdio.h>

// The most latency a sound clock reports, in nanoseconds; a test may set
// it before including this header.
#ifndef TEST_CLOCK_MAX_LATENCY
#define TEST_CLOCK_MAX_LATENCY 2000000000
#endif

// Whether two clock reads a while apart, the later at host time now, are
// sound: stamped, with a latency of at most TEST_CLOCK_MAX_LATENCY, above 0 where
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
    bool sane = latency >= (buffered ? 1 : 0) && latency <= TEST_CLOCK_MAX_LATENCY;
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
// pause, up to five times, until two reads pass ClockIsSound. A
// loaded machine's stalls hold the stream back while host time runs
// on, which spoils a window but never makes one sound.
static inline bool StreamClockIsSound(const maudContext* context, maudStreamId stream, bool output,
                                      bool buffered, void (*pause)(int milliseconds))
{
    maudStreamFormat format = {0};
    maudStreamClock first = {0};
    maudStreamClock second = {0};
    if (maudGetStreamFormat(context, stream, &format) != maud_success ||
        maudGetStreamClock(context, stream, &first) != maud_success)
    {
        return false;
    }
    for (int window = 0; window < 5; ++window)
    {
        // hygiene: a second between the two reads of the clock
        pause(1000);
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

// One window of a rate measure: waits a second or two and says whether
// the platform reported no stall in it, so that the window shows the
// stream's rate rather than the machine's.
typedef bool RateWindowFn(void* user);

// A rate measured by the stream clock: the median of the clean windows'
// rates once RATE_CLEAN_WINDOWS came up, out of how many windows, and how
// many a reported stall spoiled. A measure gives up after RATE_MAX_WINDOWS
// windows or RATE_MAX_SECONDS, whichever comes first, so that a suite of
// several measures in 2 s windows fails as starved within its CTest
// timeout rather than running past it.
#define RATE_CLEAN_WINDOWS 3
#define RATE_MAX_WINDOWS   30
#define RATE_MAX_SECONDS   45
// One window as measured, for the report of a measure that fails: its
// rate (0 without a span), whether it was clean, and the clock read that
// ends it.
typedef struct RateWindow
{
    double rate;
    bool clean;
    maudStreamClock end;
} RateWindow;
typedef struct ClockRate
{
    double rate;
    int clean;
    int stalled;
    int windows;
    // The clock read the first window starts at, and every window.
    maudStreamClock start;
    RateWindow each[RATE_MAX_WINDOWS];
} ClockRate;

// The rate a running stream plays at by its own clock: the frames
// between two clock reads over the time between the host times the clock
// gave them, in windows `window` waits out; the median of the first
// RATE_CLEAN_WINDOWS clean ones, out of the most the limits allow. Each
// host time is the platform's own report at a callback (when it read the
// host clock, plus or less the delay it reported then), so a callback a
// loaded machine holds back reads a later time and a shorter delay
// together and the measure does not move with it, where frames counted
// against the wall clock fall short in a stall or run over with its
// backlog. A stall the platform reports (a graph that skipped cycles
// really plays fewer frames) spoils the window: it measures the machine.
// The window before the first only starts the referee's count.
static inline ClockRate StreamClockRate(const maudContext* context, maudStreamId stream,
                                        RateWindowFn* window, void* user)
{
    ClockRate measured = {0};
    double rates[RATE_CLEAN_WINDOWS] = {0};
    maudStreamClock first = {0};
    (void)window(user);
    if (maudGetStreamClock(context, stream, &first) != maud_success)
    {
        return measured;
    }
    measured.start = first;
    int64_t deadline = maudGetHostNanoseconds() + RATE_MAX_SECONDS * 1000000000LL;
    while (measured.windows < RATE_MAX_WINDOWS && measured.clean < RATE_CLEAN_WINDOWS &&
           maudGetHostNanoseconds() < deadline)
    {
        bool clean = window(user);
        maudStreamClock second = {0};
        if (maudGetStreamClock(context, stream, &second) != maud_success)
        {
            return measured;
        }
        double span = (double)(second.hostNanoseconds - first.hostNanoseconds);
        double rate = first.hostNanoseconds != 0 && span > 0.0
                          ? (double)(second.position - first.position) * 1e9 / span
                          : 0.0;
        measured.each[measured.windows] = (RateWindow){rate, clean, second};
        measured.windows += 1;
        measured.stalled += clean ? 0 : 1;
        if (clean && rate > 0.0)
        {
            rates[measured.clean++] = rate;
        }
        first = second;
    }
    for (int i = 1; i < measured.clean; ++i)
    {
        for (int j = i; j > 0 && rates[j] < rates[j - 1]; --j)
        {
            double swap = rates[j];
            rates[j] = rates[j - 1];
            rates[j - 1] = swap;
        }
    }
    measured.rate = measured.clean == RATE_CLEAN_WINDOWS ? rates[RATE_CLEAN_WINDOWS / 2] : 0.0;
    return measured;
}

// Whether a rate is the expected one within 2 %, either way: the
// ranges of 44.1 and 48 kHz do not meet. Prints the rate when not.
static inline bool RateIsNear(double rate, double expected)
{
    bool within = rate > expected * 0.98 && rate < expected * 1.02;
    if (!within)
    {
        fprintf(stderr, "measured %.0f frames/s by the stream clock, expected %.0f\n", rate,
                expected);
    }
    return within;
}

// Prints every window of a measure and the clock reads that bound it, so
// that a failed one shows what the clock did (#73).
static inline void PrintRateWindows(const ClockRate* measured)
{
    const maudStreamClock* start = &measured->start;
    fprintf(stderr, "rate: starts at frame %llu, host %lld ns, latency %lld ns\n",
            (unsigned long long)start->position, (long long)start->hostNanoseconds,
            (long long)start->latencyNanoseconds);
    for (int i = 0; i < measured->windows; ++i)
    {
        const RateWindow* window = &measured->each[i];
        fprintf(stderr,
                "rate: window %d %s, %.0f frames/s, to frame %llu, host %lld ns, latency %lld ns\n",
                i + 1, window->clean ? "clean" : "stalled", window->rate,
                (unsigned long long)window->end.position, (long long)window->end.hostNanoseconds,
                (long long)window->end.latencyNanoseconds);
    }
}

// Whether a measure shows the expected rate. Without enough clean
// windows it fails as starved, with the count, so that a machine short of
// CPU reads as such and never as a wrong rate. A measure that fails
// prints its windows.
static inline bool RateHolds(ClockRate measured, double expected)
{
    bool holds = measured.clean == RATE_CLEAN_WINDOWS && RateIsNear(measured.rate, expected);
    if (measured.clean < RATE_CLEAN_WINDOWS)
    {
        fprintf(stderr, "starved: %d of %d windows had xruns or a late wakeup\n", measured.stalled,
                measured.windows);
    }
    if (!holds)
    {
        PrintRateWindows(&measured);
    }
    return holds;
}

#endif // MAUL_AUDIO_TEST_TEST_CLOCK_H
