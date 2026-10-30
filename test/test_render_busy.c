// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An offline stream that one thread is rendering, used from a second
// thread while its callback runs: rendering it again and destroying it
// are refused with maud_errorBusy, the destruction counted as misuse,
// and both succeed once the render returns. The second thread's render
// is no call back in, so its own frames read silence after the refusal.
// Nor is it one when another stream's callback renders a stream the first
// thread holds: rendering some stream is not rendering that one.

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
    bool silenced;
} Race;

// The second thread: the stream is being rendered on the first.
static void* Other(void* user)
{
    Race* race = user;
    float frame[2] = {0.5f, 0.5f};
    race->render = maudStream_Render(race->context, race->stream, frame, 1);
    race->silenced = frame[0] == 0.0f && frame[1] == 0.0f;
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

typedef struct Cross
{
    maudContext* context;
    maudStreamId held;
    maudStreamId other;
    bool inside;
    bool joined;
    bool rendered;
    maudResult render;
    bool silenced;
} Cross;

// The other stream's callback, on the second thread: renders the held
// stream, which the first thread is rendering.
static void RenderHeld(const maudStreamBlock* block, void* user)
{
    (void)block;
    Cross* cross = user;
    float frame[2] = {0.5f, 0.5f};
    cross->render = maudStream_Render(cross->context, cross->held, frame, 1);
    cross->silenced = frame[0] == 0.0f && frame[1] == 0.0f;
}

static void* RenderOther(void* user)
{
    Cross* cross = user;
    float frames[2 * 64];
    cross->rendered = maudStream_Render(cross->context, cross->other, frames, 64) == maud_success;
    return nullptr;
}

// The held stream's callback, on the first thread: runs the second
// thread, which renders the other stream, to its end.
static void Hold(const maudStreamBlock* block, void* user)
{
    (void)block;
    Cross* cross = user;
    if (cross->inside)
    {
        return;
    }
    cross->inside = true;
    pthread_t other;
    cross->joined = pthread_create(&other, nullptr, RenderOther, cross) == 0 &&
                    pthread_join(other, nullptr) == 0;
}

static maudStreamId Running(maudContext* context, maudStreamFunction* callback, void* user)
{
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.callback = callback;
    def.user = user;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &def, &stream) == maud_success &&
              maudStream_Start(context, stream) == maud_success,
          "a running stream");
    return stream;
}

static void TestHeldFromAnotherCallback(maudContext* context)
{
    Cross cross = {.context = context};
    cross.held = Running(context, Hold, &cross);
    cross.other = Running(context, RenderHeld, &cross);
    float frames[2 * 64];
    CHECK(maudStream_Render(context, cross.held, frames, 64) == maud_success && cross.joined &&
              cross.rendered,
          "rendered, the other stream rendered inside on the second thread");
    CHECK(cross.render == maud_errorBusy, "the held stream refused to the other's callback");
    CHECK(cross.silenced, "its frames silenced: rendering another stream is no call back in");
    CHECK(maudDestroyStream(context, cross.held) == maud_success &&
              maudDestroyStream(context, cross.other) == maud_success,
          "both destroyed");
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "an offline context");
    TestHeldFromAnotherCallback(context);
    Race race = {.context = context};
    maudStreamDef streamDef = maudDefaultStreamDef();
    streamDef.mode = maud_modePull;
    streamDef.callback = Callback;
    streamDef.user = &race;
    CHECK(maudCreateStream(context, &streamDef, &race.stream) == maud_success &&
              maudStream_Start(context, race.stream) == maud_success,
          "a running stream");
    uint64_t misuse = maudContext_GetMisuse(context);
    float frames[2 * 64];
    CHECK(maudStream_Render(context, race.stream, frames, 64) == maud_success && race.joined,
          "rendered, the second thread run inside");
    CHECK(race.render == maud_errorBusy, "the second thread's render refused while it renders");
    CHECK(race.silenced, "the second thread's frames silenced, since it did not call back in");
    CHECK(race.destroy == maud_errorBusy, "its destruction refused while it renders");
    CHECK(maudContext_GetMisuse(context) == misuse + 1,
          "the destruction counted as misuse, the render not");
    CHECK(maudStream_Render(context, race.stream, frames, 64) == maud_success,
          "rendered again once the render returned");
    CHECK(maudDestroyStream(context, race.stream) == maud_success,
          "destroyed once the render returned");
    CHECK(maudDestroyContext(context) == maud_success, "destroy");
    return s_failures == 0 ? 0 : 1;
}
