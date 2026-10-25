// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The integration suites' waits on a real platform, which runs in real
// time: a pause of a stated length, a poll of a condition every 10 ms up
// to a stated bound that says what it waited for when the bound passes,
// and steps repeated for a stated time. AGENTS.md lets a suite that
// drives a real platform wait on it, bounded well above the wait's
// measured worst case, and skip only when the platform is absent; these
// are the only sleeps and the only skip those suites make.

#ifndef MAUL_AUDIO_TEST_TEST_WAIT_H
#define MAUL_AUDIO_TEST_TEST_WAIT_H

#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

// Lets the platform run for milliseconds.
static inline void Pause(int milliseconds)
{
#if defined(_WIN32)
    // hygiene: a real platform runs in real time, so its suites wait on it
    Sleep((DWORD)milliseconds);
#else
    struct timespec span = {milliseconds / 1000, (long)(milliseconds % 1000) * 1000000L};
    // hygiene: a real platform runs in real time, so its suites wait on it
    nanosleep(&span, nullptr);
#endif
}

// Runs step, then tests condition, every 10 ms until the condition holds
// or milliseconds have passed in polls; then, if it never held, says
// what it waited for and how long. The caller checks the condition
// after it, so a wait that ran out fails there.
#define POLL_UNTIL(milliseconds, what, step, condition)                                            \
    do                                                                                             \
    {                                                                                              \
        for (int polled = 0;; polled += 10)                                                        \
        {                                                                                          \
            step;                                                                                  \
            if (condition)                                                                         \
            {                                                                                      \
                break;                                                                             \
            }                                                                                      \
            if (polled >= (milliseconds))                                                          \
            {                                                                                      \
                printf("waited %d ms for %s\n", (int)(milliseconds), what);                        \
                break;                                                                             \
            }                                                                                      \
            Pause(10);                                                                             \
        }                                                                                          \
    } while (0)

// Runs step every 10 ms for milliseconds, whatever comes of it.
#define STEP_FOR(milliseconds, step)                                                               \
    do                                                                                             \
    {                                                                                              \
        for (int polled = 0; polled < (milliseconds); polled += 10)                                \
        {                                                                                          \
            step;                                                                                  \
            Pause(10);                                                                             \
        }                                                                                          \
    } while (0)

// The exit code CTest reads as skipped (SKIP_RETURN_CODE), for a suite
// whose platform is absent, after saying why. A suite whose platform
// CI or the fixture provides fails instead, where MAUD_REQUIRE_<BACKEND>
// is set.
static inline int PlatformAbsent(const char* why)
{
    printf("skipped: %s\n", why);
    return 77; // hygiene: a suite skips only where its platform is absent
}

#endif // MAUL_AUDIO_TEST_TEST_WAIT_H
