// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version and result names.

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

static void TestResultNames(void)
{
    CHECK(strcmp(maudResultName(maud_success), "maud_success") == 0, "success name");
    CHECK(strcmp(maudResultName(maud_errorInvalid), "maud_errorInvalid") == 0, "invalid name");
    CHECK(strcmp(maudResultName(maud_errorCapacity), "maud_errorCapacity") == 0, "capacity name");
    CHECK(strcmp(maudResultName(12345), "unknown result") == 0, "unknown name");
}

int main(void)
{
    TestVersionMatchesHeader();
    TestResultNames();
    return s_failures == 0 ? 0 : 1;
}
