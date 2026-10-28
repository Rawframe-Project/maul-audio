// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The backend tests' rate measure (test_clock.h) on clock reads written
// here: a running clock holds its rate, a clock that stood still fails
// at once as a rate of 0 rather than as starved, a measure short of clean
// windows is unjudged rather than failed, a stalled or unstamped
// window does not count as clean, and a window whose latency moved past
// the measure's share of it counts as settling. A window is judged with
// the window after it, whose graph count may carry its stall.

// test_clock.h's getenv reads the test's switches; the C runtime's warning
// that it is unsafe is about its result's lifetime, which the test does
// not keep.
#define _CRT_SECURE_NO_WARNINGS

#include "test_clock.h"

#include "test_harness.h"

// A clock read: a frame and when it is heard, a second per window.
static maudStreamClock Read(uint64_t position, int window)
{
    return (maudStreamClock){position, 1000000000LL * (1 + window), 0};
}

// A measure of RATE_CLEAN_WINDOWS windows from frame 0, each advancing
// by frames and clean as given.
static ClockRate Measure(uint64_t frames, const bool* clean)
{
    ClockRate measured = {0};
    maudStreamClock first = Read(0, 0);
    measured.start = first;
    for (int i = 0; i < RATE_CLEAN_WINDOWS; ++i)
    {
        maudStreamClock second = Read(frames * (uint64_t)(i + 1), i + 1);
        CountRateWindow(&measured, &first, &second, clean[i]);
        first = second;
    }
    SettleRate(&measured);
    return measured;
}

static void TestRunning(void)
{
    const bool clean[RATE_CLEAN_WINDOWS] = {true, true, true};
    ClockRate measured = Measure(48000, clean);
    CHECK(measured.clean == RATE_CLEAN_WINDOWS && measured.rate == 48000.0,
          "a running clock: three clean windows at its rate");
    CHECK(RateHolds(measured, 48000.0), "and it holds");
}

static void TestStoodStill(void)
{
    const bool clean[RATE_CLEAN_WINDOWS] = {true, true, true};
    ClockRate measured = Measure(0, clean);
    CHECK(measured.clean == RATE_CLEAN_WINDOWS && measured.stalled == 0 && measured.rate == 0.0,
          "a clock that stood still: three clean windows at a rate of 0");
    CHECK(!RateHolds(measured, 48000.0), "and it fails at once, not as starved");
}

static void TestNotClean(void)
{
    const bool clean[RATE_CLEAN_WINDOWS] = {true, false, true};
    ClockRate measured = Measure(48000, clean);
    CHECK(measured.clean == 2 && measured.stalled == 1 && measured.windows == 3,
          "a stalled window counts as stalled, not clean");
    CHECK(measured.rate == 0.0 && RateHolds(measured, 48000.0) && s_rateUnjudged == 1,
          "short of clean windows: unjudged, not failed");
    s_rateUnjudged = 0;
    ClockRate unstamped = {0};
    maudStreamClock before = {0};
    maudStreamClock after = Read(48000, 1);
    CountRateWindow(&unstamped, &before, &after, true);
    CHECK(unstamped.clean == 0 && unstamped.stalled == 0 && unstamped.windows == 1,
          "a window from an unstamped read counts as neither");
}

// A clock read whose heard time carries latency nanoseconds.
static maudStreamClock ReadLate(uint64_t position, int window, int64_t latency)
{
    return (maudStreamClock){position, 1000000000LL * (1 + window) + latency, latency};
}

static void TestSettling(void)
{
    ClockRate measured = {.settleShare = RATE_SETTLE_SHARE};
    // The latency of a steady stream, then a buffer filled again by 20 ms,
    // which reads 2 % low, then steady at its new latency.
    const int64_t latencies[5] = {13000000, 14000000, 34000000, 34500000, 35000000};
    maudStreamClock first = ReadLate(0, 0, latencies[0]);
    for (int i = 1; i < 5; ++i)
    {
        maudStreamClock second = ReadLate(48000 * (uint64_t)i, i, latencies[i]);
        CountRateWindow(&measured, &first, &second, true);
        first = second;
    }
    SettleRate(&measured);
    CHECK(measured.settling == 1 && measured.stalled == 0 && measured.clean == RATE_CLEAN_WINDOWS,
          "the window the buffer filled in is settling, the steady ones clean");
    CHECK(measured.each[1].settling && measured.each[1].rate < 48000.0 * 0.981,
          "the settling window read 2 % low");
    CHECK(RateHolds(measured, 48000.0), "and the measure holds by the clean ones");
    ClockRate unjudged = {0};
    maudStreamClock before = ReadLate(0, 0, 14000000);
    maudStreamClock after = ReadLate(48000, 1, 34000000);
    CountRateWindow(&unjudged, &before, &after, true);
    CHECK(unjudged.settling == 0 && unjudged.clean == 1,
          "a measure without a share judges no window settling");
}

// A window that lost frames but whose stall was counted in the window
// after: judged with that one, it is not clean, and the measure holds by
// the others.
static void TestJudgedWithTheNext(void)
{
    ClockRate measured = {0};
    PendingWindow pending = {0};
    // Each window's frames and its own verdict: the second lost 7,000
    // frames, and its xruns came in the third.
    const uint64_t frames[7] = {48000, 41000, 48000, 48000, 48000, 48000, 48000};
    const bool verdicts[7] = {true, true, false, true, true, true, true};
    maudStreamClock first = Read(0, 0);
    uint64_t position = 0;
    for (int i = 0; i < 7 && measured.clean < RATE_CLEAN_WINDOWS; ++i)
    {
        position += frames[i];
        maudStreamClock second = Read(position, i + 1);
        JudgeRateWindow(&measured, &pending, &first, &second, verdicts[i]);
        first = second;
    }
    SettleRate(&measured);
    CHECK(measured.windows == 5 && measured.stalled == 2 && measured.clean == RATE_CLEAN_WINDOWS,
          "the window before a stalled one counts as stalled too");
    CHECK(!measured.each[1].clean && measured.each[1].rate == 41000.0,
          "the one whose frames the stall took among them");
    CHECK(RateHolds(measured, 48000.0), "and the measure holds by the clean ones");
}

int main(void)
{
    TestRunning();
    TestStoodStill();
    TestNotClean();
    TestSettling();
    TestJudgedWithTheNext();
    return s_failures == 0 ? 0 : 1;
}
