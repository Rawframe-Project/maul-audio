// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An offline stream that one thread is rendering, used from a second
// thread while its callback runs: rendering it again and destroying it
// are refused with maud_errorBusy, the destruction counted as misuse,
// and both succeed once the render returns.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/stream.h"

#include <pthread.h>

typedef struct Race
{
    maudContext* context;
    maudStreamId stream;
    bool inside;
    bool joined;
    maudResult render;
    maudResult destroy;
} Race;

// The second thread: the stream is being rendered on the first.
static void* Other(void* user)
{
    Race* race = user;
    float frame[2];
    race->render = maudRenderStream(race->context, race->stream, frame, 1);
    race->destroy = maudDestroyStream(race->context, race->stream);
    return nullptr;
}

// Runs the second thread to its end from the first block's callback.
static void Callback(const maudStreamBlock* block, void* user)
{
    (void)block;
    Race* race = user;
    if (race->inside)
    {
        return;
    }
    race->inside = true;
    pthread_t other;
    race->joined =
        pthread_create(&other, nullptr, Other, race) == 0 && pthread_join(other, nullptr) == 0;
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "an offline context");
    Race race = {.context = context};
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.mode = maud_modePull;
    streamDef.callback = Callback;
    streamDef.user = &race;
    CHECK(maudCreateStream(context, &streamDef, &race.stream) == maud_success &&
              maudStartStream(context, race.stream) == maud_success,
          "a running stream");
    uint64_t misuse = maudGetContextMisuse(context);
    float frames[2 * 64];
    CHECK(maudRenderStream(context, race.stream, frames, 64) == maud_success && race.joined,
          "rendered, the second thread run inside");
    CHECK(race.render == maud_errorBusy, "the second thread's render refused while it renders");
    CHECK(race.destroy == maud_errorBusy, "its destruction refused while it renders");
    CHECK(maudGetContextMisuse(context) == misuse + 1,
          "the destruction counted as misuse, the render not");
    CHECK(maudRenderStream(context, race.stream, frames, 64) == maud_success,
          "rendered again once the render returned");
    CHECK(maudDestroyStream(context, race.stream) == maud_success,
          "destroyed once the render returned");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
