// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "web_clock.h"

#include "clock.h"

void maudWebResetClock(maudWebClock* clock)
{
    for (int i = 0; i < 5; ++i)
    {
        clock->second[i] = INT64_MIN;
    }
}

int64_t maudWebEarliest(maudWebClock* clock, int64_t origin, int64_t now)
{
    int64_t second = now / 1000000000LL;
    int slot = (int)(second % 5);
    if (clock->second[slot] != second)
    {
        clock->second[slot] = second;
        clock->earliest[slot] = origin;
    }
    else if (origin < clock->earliest[slot])
    {
        clock->earliest[slot] = origin;
    }
    int64_t least = origin;
    for (int i = 0; i < 5; ++i)
    {
        if (clock->second[i] > second - 5 && clock->earliest[i] < least)
        {
            least = clock->earliest[i];
        }
    }
    return least;
}

int64_t maudWebFilterLatency(maudWebClock* clock, int* clockEpoch, const maudStreamCore* core,
                             int64_t latency, int epoch, bool output)
{
    if (epoch != *clockEpoch)
    {
        maudWebResetClock(clock);
        *clockEpoch = epoch;
    }
    uint32_t rate = core->period.sampleRate;
    if (rate == 0)
    {
        return latency;
    }
    uint64_t position = atomic_load_explicit(&core->position, memory_order_relaxed);
    int64_t place = (int64_t)((double)position * 1e9 / rate);
    int64_t now = maudNowNanoseconds();
    int64_t origin = output ? now + latency - place : now - latency - place;
    int64_t least = maudWebEarliest(clock, origin, now);
    int64_t filtered = output ? least + place - now : now - least - place;
    return filtered > 0 ? filtered : 0;
}
