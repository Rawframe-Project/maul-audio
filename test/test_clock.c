// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The backend tests' rate measure (test_clock.h) on clock reads written
// here: a running clock holds its rate, a clock that stood still fails
// at once as a rate of 0 rather than as starved, and a stalled or
// unstamped window does not count as clean.

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
    CHECK(measured.rate == 0.0 && !RateHolds(measured, 48000.0), "short of clean windows: starved");
    ClockRate unstamped = {0};
    maudStreamClock before = {0};
    maudStreamClock after = Read(48000, 1);
    CountRateWindow(&unstamped, &before, &after, true);
    CHECK(unstamped.clean == 0 && unstamped.stalled == 0 && unstamped.windows == 1,
          "a window from an unstamped read counts as neither");
}

int main(void)
{
    TestRunning();
    TestStoodStill();
    TestNotClean();
    return s_failures == 0 ? 0 : 1;
}
