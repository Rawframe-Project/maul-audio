// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Checks a running stream's clock against the host clock, for the
// backend tests.

#ifndef MAUL_AUDIO_TEST_TEST_CLOCK_H
#define MAUL_AUDIO_TEST_TEST_CLOCK_H

#include "test_harness.h"

#include "maul-audio/stream.h"

#include <stdio.h>
#include <stdlib.h>

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
// The most a clock's latency may move over a window, as a share of its
// span, before the window counts as settling: 0.5 %, a quarter of the
// rate's tolerance. A clock's heard times carry its latency, and after a
// stall the stream's queue moves for a second or two: a stalled
// PulseAudio server drains the stream's buffer and fills it again,
// raising its target after the underflow (#73), and a queue a stall
// filled drains back. Through ALSA's PulseAudio plugin, 33 of 572 windows
// otherwise clean read up to 4 % off by that move alone while the graph
// kept time (#38). A steady stream's latency moves about 1 ms a second.
#define RATE_SETTLE_SHARE 0.005
// One window as measured, for the report of a measure that fails: its
// rate (0 without a span), whether it was clean, whether its latency was
// settling, and the clock read that ends it.
typedef struct RateWindow
{
    double rate;
    bool clean;
    bool settling;
    maudStreamClock end;
} RateWindow;
typedef struct ClockRate
{
    double rate;
    int clean;
    int stalled;
    int settling;
    int windows;
    // The most the latency may move over a window, as a share of its
    // span, before the window counts as settling; 0 judges windows by the
    // platform's stalls alone.
    double settleShare;
    // The clock read the first window starts at, every window, and the
    // clean windows' rates.
    maudStreamClock start;
    RateWindow each[RATE_MAX_WINDOWS];
    double rates[RATE_CLEAN_WINDOWS];
} ClockRate;

// Counts one window of a measure, between the clock reads first and
// second; clean says whether the platform reported no stall in it. A
// clean window between two stamped reads a positive span apart counts,
// at the rate the positions show: 0 for a clock that stood still, which
// then fails as a wrong rate, never as starved. A clock's host time
// carries its latency, so a window's rate is off by the latency's move
// over its span: one that moved by more than the measure's settleShare
// of the span counts as settling, not clean, as after a server's stall
// while it fills its buffer again (#73).
static inline void CountRateWindow(ClockRate* measured, const maudStreamClock* first,
                                   const maudStreamClock* second, bool clean)
{
    double span = (double)(second->hostNanoseconds - first->hostNanoseconds);
    bool timed = first->hostNanoseconds != 0 && span > 0.0;
    double rate = timed ? (double)(second->position - first->position) * 1e9 / span : 0.0;
    double moved = (double)(second->latencyNanoseconds - first->latencyNanoseconds);
    bool settling = clean && timed && measured->settleShare > 0.0 &&
                    (moved < 0 ? -moved : moved) > measured->settleShare * span;
    measured->each[measured->windows] = (RateWindow){rate, clean, settling, *second};
    measured->windows += 1;
    measured->stalled += clean ? 0 : 1;
    measured->settling += settling ? 1 : 0;
    if (clean && timed && !settling)
    {
        measured->rates[measured->clean++] = rate;
    }
}

// A window waiting for the verdict of the one after it. The graph's xrun
// count, as GraphWindow reads it through pw-top, lags its window by one
// window: a stall's frames are lost in the window it falls in and its
// xruns counted in the next (#73). So a window counts as clean only if
// the window after it was clean too, which holds for a lag of 0 or 1.
typedef struct PendingWindow
{
    maudStreamClock first;
    maudStreamClock second;
    bool clean;
    bool waiting;
} PendingWindow;

// Takes the verdict of the window between first and second: counts the
// window waiting before it, clean only if both were, and leaves this one
// waiting for the next.
static inline void JudgeRateWindow(ClockRate* measured, PendingWindow* pending,
                                   const maudStreamClock* first, const maudStreamClock* second,
                                   bool clean)
{
    if (pending->waiting)
    {
        CountRateWindow(measured, &pending->first, &pending->second, pending->clean && clean);
    }
    *pending = (PendingWindow){*first, *second, clean, true};
}

// Sets a measure's rate to the median of its clean windows' once it has
// RATE_CLEAN_WINDOWS of them, else 0.
static inline void SettleRate(ClockRate* measured)
{
    double* rates = measured->rates;
    for (int i = 1; i < measured->clean; ++i)
    {
        for (int j = i; j > 0 && rates[j] < rates[j - 1]; --j)
        {
            double swap = rates[j];
            rates[j] = rates[j - 1];
            rates[j - 1] = swap;
        }
    }
    measured->rate = measured->clean == RATE_CLEAN_WINDOWS ? rates[RATE_CLEAN_WINDOWS / 2] : 0.0;
}

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
// The window before the first only starts the referee's count, and each
// window is judged once the one after it is in (JudgeRateWindow). A
// window whose latency moved by more than settleShare of its span is
// settling (CountRateWindow); 0 leaves that judgement out.
static inline ClockRate StreamClockRate(const maudContext* context, maudStreamId stream,
                                        RateWindowFn* window, void* user, double settleShare)
{
    ClockRate measured = {.settleShare = settleShare};
    // MAUD_TEST_STARVE_RATE stands for a machine that starves every
    // window, so that a suite's inconclusive ending can be checked
    // (pipewire_starved) without loading the machine.
    if (getenv("MAUD_TEST_STARVE_RATE") != nullptr)
    {
        measured.windows = 1;
        measured.stalled = 1;
        return measured;
    }
    maudStreamClock first = {0};
    (void)window(user);
    if (maudGetStreamClock(context, stream, &first) != maud_success)
    {
        return measured;
    }
    measured.start = first;
    int64_t deadline = maudGetHostNanoseconds() + RATE_MAX_SECONDS * 1000000000LL;
    PendingWindow pending = {0};
    while (measured.windows < RATE_MAX_WINDOWS && measured.clean < RATE_CLEAN_WINDOWS &&
           maudGetHostNanoseconds() < deadline)
    {
        bool clean = window(user);
        maudStreamClock second = {0};
        if (maudGetStreamClock(context, stream, &second) != maud_success)
        {
            return measured;
        }
        JudgeRateWindow(&measured, &pending, &first, &second, clean);
        first = second;
    }
    SettleRate(&measured);
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
    int64_t latency = start->latencyNanoseconds;
    for (int i = 0; i < measured->windows; ++i)
    {
        const RateWindow* window = &measured->each[i];
        const char* verdict = !window->clean ? "stalled" : window->settling ? "settling" : "clean";
        fprintf(stderr,
                "rate: window %d %s, %.0f frames/s, to frame %llu, host %lld ns, latency %lld ns "
                "(%+lld)\n",
                i + 1, verdict, window->rate, (unsigned long long)window->end.position,
                (long long)window->end.hostNanoseconds, (long long)window->end.latencyNanoseconds,
                (long long)(window->end.latencyNanoseconds - latency));
        latency = window->end.latencyNanoseconds;
    }
}

// Measures that ended with fewer than RATE_CLEAN_WINDOWS clean windows,
// within RATE_MAX_WINDOWS windows or RATE_MAX_SECONDS, and so judged
// nothing: starved ones saw a stall, the machine's; settling ones saw
// none, only a latency that kept moving, which a rate bug can do too.
[[maybe_unused]] static int s_rateStarved = 0;
[[maybe_unused]] static int s_rateSettling = 0;

// Whether a measure shows the expected rate. A measure without enough
// clean windows judges nothing: it prints why and its windows, counts
// itself starved or settling and holds, so that a machine short of CPU
// reads as such (RateSuiteResult) and never as a wrong rate, nor passes
// on too few. A measure that fails prints its windows.
static inline bool RateHolds(ClockRate measured, double expected)
{
    if (measured.clean < RATE_CLEAN_WINDOWS)
    {
        fprintf(stderr,
                "not enough clean windows: %d of %d had xruns or a late wakeup in them or the "
                "window after, %d a settling latency\n",
                measured.stalled, measured.windows, measured.settling);
        PrintRateWindows(&measured);
        if (measured.stalled != 0)
        {
            s_rateStarved += 1;
        }
        else
        {
            s_rateSettling += 1;
        }
        return true;
    }
    bool holds = RateIsNear(measured.rate, expected);
    if (!holds)
    {
        PrintRateWindows(&measured);
    }
    return holds;
}

// A suite's exit status: 1 after a failure. A suite without one whose
// rate measures were not all judged prints a line starting
// "inconclusive:", which CTest reads as a skip (the rate suites'
// SKIP_REGULAR_EXPRESSION) and CI fails as a test that did not run: the
// machine's fault, never the library's. A failure is never hidden so.
static inline int RateSuiteResult(void)
{
    if (s_failures != 0)
    {
        return 1;
    }
    if (s_rateStarved != 0)
    {
        printf("inconclusive: %d rate measure(s) found fewer than %d clean windows in %d windows "
               "or %d s, with stalls; the machine starved the stream\n",
               s_rateStarved, RATE_CLEAN_WINDOWS, RATE_MAX_WINDOWS, RATE_MAX_SECONDS);
    }
    if (s_rateSettling != 0)
    {
        printf("inconclusive: %d rate measure(s) found fewer than %d clean windows in %d windows "
               "or %d s, with no stall; the clock's latency kept moving, which a rate bug can "
               "do too\n",
               s_rateSettling, RATE_CLEAN_WINDOWS, RATE_MAX_WINDOWS, RATE_MAX_SECONDS);
    }
    return 0;
}

#endif // MAUL_AUDIO_TEST_TEST_CLOCK_H
