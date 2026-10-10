// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Retrying a stream the platform failed to reopen: due at once, then
// after a wait that doubles from a quarter second up to four seconds,
// and due at once again after a try that worked.

#include "retry.h"
#include "test_harness.h"

static void TestBackoff(void)
{
    maudStreamBinding binding = {0};
    const int64_t second = 1000000000;
    int64_t now = 10 * second;
    CHECK(maudRetryDue(&binding, now), "due before any failure");
    maudRetryFailed(&binding, now);
    CHECK(binding.retryWait == MAUD_RETRY_FIRST_NS && !maudRetryDue(&binding, now) &&
              !maudRetryDue(&binding, now + MAUD_RETRY_FIRST_NS - 1) &&
              maudRetryDue(&binding, now + MAUD_RETRY_FIRST_NS),
          "a quarter second after the first failure");
    int64_t waits[6] = {0};
    for (int i = 0; i < 6; ++i)
    {
        now = binding.retryAt;
        maudRetryFailed(&binding, now);
        waits[i] = binding.retryAt - now;
    }
    CHECK(waits[0] == 2 * MAUD_RETRY_FIRST_NS && waits[1] == 4 * MAUD_RETRY_FIRST_NS &&
              waits[2] == 8 * MAUD_RETRY_FIRST_NS && waits[3] == MAUD_RETRY_LAST_NS &&
              waits[4] == MAUD_RETRY_LAST_NS && waits[5] == MAUD_RETRY_LAST_NS,
          "doubling up to four seconds");
    maudRetrySucceeded(&binding);
    CHECK(maudRetryDue(&binding, now) && binding.retryWait == 0, "due again after a success");
    maudRetryFailed(&binding, now);
    CHECK(binding.retryWait == MAUD_RETRY_FIRST_NS, "and the first wait again");
}

int main(void)
{
    TestBackoff();
    return s_failures == 0 ? 0 : 1;
}
