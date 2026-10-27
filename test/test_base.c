// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version, the result names and the family's result values.

#include "test_harness.h"

#include "maul-audio/base.h"

#include <string.h>

static void TestVersionMatchesHeader(void)
{
    maudVersion version = maudGetVersion();
    CHECK(version.major == MAUD_VERSION_MAJOR, "major version");
    CHECK(version.minor == MAUD_VERSION_MINOR, "minor version");
    CHECK(version.patch == MAUD_VERSION_PATCH, "patch version");
}

// Each shared result has the value the Maul family gives it.
static void TestFamilyValues(void)
{
    CHECK(maud_success == 0 && maud_errorInvalid == -1 && maud_errorCapacity == -2 &&
              maud_errorStale == -3 && maud_errorUnsupported == -4 && maud_errorPlatform == -5 &&
              maud_errorState == -6 && maud_errorVersion == -7 && maud_errorFormat == -8 &&
              maud_errorLimit == -9 && maud_errorBusy == -10,
          "the family's values");
}

static void TestResultNames(void)
{
    CHECK(strcmp(maudResultName(maud_success), "maud_success") == 0, "success name");
    CHECK(strcmp(maudResultName(maud_empty), "maud_empty") == 0, "empty name");
    CHECK(strcmp(maudResultName(maud_errorInvalid), "maud_errorInvalid") == 0, "errorInvalid name");
    CHECK(strcmp(maudResultName(maud_errorCapacity), "maud_errorCapacity") == 0,
          "errorCapacity name");
    CHECK(strcmp(maudResultName(maud_errorStale), "maud_errorStale") == 0, "errorStale name");
    CHECK(strcmp(maudResultName(maud_errorUnsupported), "maud_errorUnsupported") == 0,
          "errorUnsupported name");
    CHECK(strcmp(maudResultName(maud_errorPlatform), "maud_errorPlatform") == 0,
          "errorPlatform name");
    CHECK(strcmp(maudResultName(maud_errorState), "maud_errorState") == 0, "errorState name");
    CHECK(strcmp(maudResultName(maud_errorVersion), "maud_errorVersion") == 0, "errorVersion name");
    CHECK(strcmp(maudResultName(maud_errorFormat), "maud_errorFormat") == 0, "errorFormat name");
    CHECK(strcmp(maudResultName(maud_errorLimit), "maud_errorLimit") == 0, "errorLimit name");
    CHECK(strcmp(maudResultName(maud_errorBusy), "maud_errorBusy") == 0, "errorBusy name");
    CHECK(strcmp(maudResultName(12345), "unknown result") == 0, "unknown name");
}

int main(void)
{
    TestVersionMatchesHeader();
    TestResultNames();
    TestFamilyValues();
    return s_failures == 0 ? 0 : 1;
}
