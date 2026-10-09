// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Counting xruns: an output's are underruns and an input's overruns,
// and a platform's running total (AAudio's) counts each xrun once, as
// it grows, and none while it stays.

#include "test_harness.h"
#include "xrun.h"

static void InitCore(maudStreamCore* core, maudDirection direction)
{
    core->def.direction = direction;
    atomic_init(&core->underruns, 0);
    atomic_init(&core->overruns, 0);
}

static void TestDirection(void)
{
    maudStreamCore output = {0};
    InitCore(&output, maud_directionOutput);
    maudCountXrun(&output);
    CHECK(atomic_load(&output.underruns) == 1 && atomic_load(&output.overruns) == 0,
          "an output's xrun is an underrun");
    maudStreamCore input = {0};
    InitCore(&input, maud_directionInput);
    maudCountXrun(&input);
    CHECK(atomic_load(&input.overruns) == 1 && atomic_load(&input.underruns) == 0,
          "an input's is an overrun");
}

static void TestRunningTotal(void)
{
    maudStreamCore core = {0};
    InitCore(&core, maud_directionOutput);
    int32_t counted = 0;
    maudCountXrunsTo(&core, &counted, 0);
    CHECK(atomic_load(&core.underruns) == 0 && counted == 0, "a total of none counts none");
    maudCountXrunsTo(&core, &counted, 3);
    CHECK(atomic_load(&core.underruns) == 3 && counted == 3, "a total of three counts three");
    maudCountXrunsTo(&core, &counted, 3);
    CHECK(atomic_load(&core.underruns) == 3 && counted == 3, "and none again while it stays");
    maudCountXrunsTo(&core, &counted, 5);
    CHECK(atomic_load(&core.underruns) == 5 && counted == 5, "two more as it grows by two");
}

int main(void)
{
    TestDirection();
    TestRunningTotal();
    return s_failures == 0 ? 0 : 1;
}
