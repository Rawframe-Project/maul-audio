// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The focus a backend reports reaches the host once per change: the same
// focus reported again posts nothing, and the context reads it back.

#include "context.h"
#include "focus.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/focus.h"
#include "maul-audio/notification.h"

static uint32_t FocusNotes(maudContext* context, maudFocus* lastOut)
{
    uint32_t count = 0;
    maudNotification record;
    while (maudNextNotification(context, &record) == maud_success)
    {
        if (record.kind == maud_notifyFocusChanged)
        {
            ++count;
            *lastOut = record.focus;
        }
    }
    return count;
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "an offline context");
    if (context == nullptr)
    {
        return 1;
    }
    maudFocus last = maud_focusNone;
    (void)FocusNotes(context, &last);
    maudReportFocus(context, maud_focusHeld);
    CHECK(FocusNotes(context, &last) == 1 && last == maud_focusHeld, "a change is posted");
    maudReportFocus(context, maud_focusHeld);
    CHECK(FocusNotes(context, &last) == 0, "the same focus again posts nothing");
    maudReportFocus(context, maud_focusDucked);
    maudFocus now = maud_focusNone;
    CHECK(FocusNotes(context, &last) == 1 && last == maud_focusDucked &&
              maudGetContextFocus(context, &now) == maud_success && now == maud_focusDucked,
          "the next change is posted and read back");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
