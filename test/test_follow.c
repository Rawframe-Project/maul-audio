// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Streams following their devices, through a backend's hooks (whitebox,
// the offline backend's table copied with hooks that count): starting a
// stream lets the platform run it once; a host hold suspends it once,
// however often it is asked, and releasing it resumes it, as awaiting
// permission and being granted it do; a stream that follows its default
// moves with it, the platform told to reopen it only when the new
// device's native rate differs; a duplex stream keeps its one rate.

#include "backend.h"
#include "context.h"
#include "follow.h"
#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/notification.h"
#include "maul-audio/offline.h"
#include "maul-audio/stream.h"

static int s_activations;
static int s_deactivations;
static int s_retargets;

static void Active(maudContext* context, maudStreamSlot* slot, bool active)
{
    (void)context;
    (void)slot;
    s_activations += active ? 1 : 0;
    s_deactivations += active ? 0 : 1;
}

static void Retargeted(maudContext* context, maudStreamSlot* slot)
{
    (void)context;
    (void)slot;
    s_retargets++;
}

static void Ignore(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

static uint32_t Count(maudContext* context, maudNotificationKind kind)
{
    uint32_t count = 0;
    maudNotification record;
    while (maudNextNotification(context, &record) == maud_success)
    {
        count += record.kind == kind ? 1u : 0u;
    }
    return count;
}

static maudDeviceId Output(maudContext* context, uint32_t rate, const char* key)
{
    maudOfflineDeviceDef def = maudDefaultOfflineDeviceDef();
    def.sampleRate = rate;
    def.key = key;
    def.keyLength = 2;
    maudDeviceId id = {0, 0};
    CHECK(maudAddOfflineDevice(context, &def, &id) == maud_success, "an output");
    return id;
}

int main(void)
{
    maudContextDef cd = maudDefaultContextDef();
    cd.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&cd, &context) == maud_success, "an offline context");
    if (context == nullptr)
    {
        return 1;
    }
    const maudBackend* offline = context->backend;
    maudBackend hooks = *offline;
    hooks.setStreamActive = Active;
    hooks.retargetStream = Retargeted;
    hooks.reopensOnMove = false;
    context->backend = &hooks;
    maudStreamDef sd = maudDefaultStreamDef();
    sd.mode = maud_modePull;
    sd.callback = Ignore;
    maudStreamId stream = {0, 0};
    CHECK(maudCreateStream(context, &sd, &stream) == maud_success &&
              maudStartStream(context, stream) == maud_success && s_activations == 1,
          "started: the platform runs it, once");
    (void)Count(context, maud_notifyStreamSuspended);
    maudHoldStreams(context, true);
    maudHoldStreams(context, true);
    CHECK(Count(context, maud_notifyStreamSuspended) == 1 && s_deactivations == 1,
          "a hold suspends it once, however often asked");
    maudHoldStreams(context, false);
    CHECK(Count(context, maud_notifyStreamResumed) == 1 && s_activations == 2,
          "and its release resumes it");
    maudAwaitPermission(context, maudFindStream(context, stream), true);
    CHECK(Count(context, maud_notifyStreamSuspended) == 1, "awaiting permission suspends it");
    maudAwaitPermission(context, maudFindStream(context, stream), false);
    CHECK(Count(context, maud_notifyStreamResumed) == 1, "and permission resumes it");
    // Its default moves to a device at the same rate, then to one at
    // 44.1 kHz.
    maudDeviceId same = Output(context, 48000, "s1");
    maudDeviceId other = Output(context, 44100, "s2");
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleGeneral, same) == maud_success &&
              Count(context, maud_notifyStreamMoved) == 1 && s_retargets == 0,
          "moved at the same rate: nothing to reopen");
    maudStreamFormat format = {0};
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleGeneral, other) == maud_success &&
              s_retargets == 1 && maudGetStreamFormat(context, stream, &format) == maud_success &&
              format.sampleRate == 44100,
          "moved to another rate: reopened at it");
    // A duplex stream keeps the one rate of its halves: its output, moved
    // to a device at another rate, is converted by the platform instead,
    // with no format change.
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleGeneral, same) == maud_success,
          "back at 48 kHz");
    maudStreamDef dd = maudDefaultStreamDef();
    dd.direction = maud_directionDuplex;
    dd.mode = maud_modePull;
    dd.callback = Ignore;
    maudStreamId duplex = {0, 0};
    CHECK(maudCreateStream(context, &dd, &duplex) == maud_success &&
              maudGetStreamFormat(context, duplex, &format) == maud_success &&
              format.sampleRate == 48000 && format.ratePolicy == maud_rateNative,
          "a duplex stream at 48 kHz");
    (void)Count(context, maud_notifyStreamMoved);
    s_retargets = 0;
    CHECK(maudSetOfflineDefaultDevice(context, maud_roleGeneral, other) == maud_success,
          "its output's default moves to 44.1 kHz");
    uint32_t changed = 0;
    maudNotification record;
    while (maudNextNotification(context, &record) == maud_success)
    {
        changed +=
            record.kind == maud_notifyStreamFormatChanged && record.stream.index1 == duplex.index1
                ? 1u
                : 0u;
    }
    CHECK(changed == 0 && maudGetStreamFormat(context, duplex, &format) == maud_success &&
              format.sampleRate == 48000 && format.ratePolicy == maud_ratePlatformConverted,
          "the duplex stream keeps 48 kHz, converted");
    CHECK(maudDestroyStream(context, duplex) == maud_success, "destroy the duplex stream");
    context->backend = offline;
    CHECK(maudDestroyStream(context, stream) == maud_success, "destroy the stream");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
    return s_failures == 0 ? 0 : 1;
}
