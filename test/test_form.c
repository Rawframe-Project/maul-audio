// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Forms from the names the platforms give a port (whitebox): each known
// name its form, "internal" the speakers of an output and the
// microphone of an input, a connection or no name unknown.

#include "form.h"
#include "test_harness.h"

int main(void)
{
    CHECK(maudFormOfName("headphones", maud_directionOutput) == maud_formHeadphones &&
              maudFormOfName("headset", maud_directionInput) == maud_formHeadset &&
              maudFormOfName("hdmi", maud_directionOutput) == maud_formDigital &&
              maudFormOfName("mic", maud_directionInput) == maud_formMicrophone,
          "known names, their forms");
    CHECK(maudFormOfName("internal", maud_directionOutput) == maud_formSpeakers &&
              maudFormOfName("internal", maud_directionInput) == maud_formMicrophone,
          "internal by direction");
    CHECK(maudFormOfName("usb", maud_directionOutput) == maud_formUnknown &&
              maudFormOfName(nullptr, maud_directionOutput) == maud_formUnknown,
          "a connection or no name, unknown");
    return s_failures == 0 ? 0 : 1;
}
