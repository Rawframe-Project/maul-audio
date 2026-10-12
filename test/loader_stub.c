// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A stand-in for a Linux sound library (test_loaders): built once per
// library under its soname, holding none of its functions.

int maudLoaderStub(void);

int maudLoaderStub(void)
{
    return 1;
}
