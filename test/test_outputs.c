// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Outputs on failure, conventions section 8: a call on an owner object
// clears its outputs on any failure, before it checks the owner (a
// missing owner's failure clears them too), except an output the failure
// itself defines, which it writes. Each call here runs with its outputs
// filled with a pattern, once with a NULL owner and once with a valid
// owner and a stale or unknown id, and each output must read zero after.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/focus.h"
#include "maul-audio/hrtf.h"
#include "maul-audio/notification.h"
#include "maul-audio/offline.h"
#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"
#include "maul-audio/stream.h"
#include "maul-audio/voice.h"

#include <string.h>

// Whether bytes are all zero.
static bool Zero(const void* bytes, size_t size)
{
    const unsigned char* p = bytes;
    for (size_t i = 0; i < size; ++i)
    {
        if (p[i] != 0)
        {
            return false;
        }
    }
    return true;
}

#define FILL(value) memset(&(value), 0xAB, sizeof(value))

static void TestContextCalls(maudContext* context)
{
    maudDeviceId stale = {77, 3};
    maudStreamId staleStream = {77, 3};
    const maudContext* none = nullptr;
    for (int pass = 0; pass < 2; ++pass)
    {
        const maudContext* owner = pass == 0 ? none : context;
        const char* what = pass == 0 ? "a NULL context" : "an unknown id";

        maudDeviceInfo info;
        FILL(info);
        CHECK(maudGetDeviceInfo(owner, stale, &info) != maud_success && Zero(&info, sizeof(info)),
              "maudGetDeviceInfo clears its info");
        maudDeviceId device;
        FILL(device);
        CHECK(maudGetDefaultDevice(owner, (maudDirection)7, maud_roleGeneral, &device) !=
                      maud_success &&
                  Zero(&device, sizeof(device)),
              "maudGetDefaultDevice clears its device");
        size_t length;
        FILL(length);
        char name[8];
        CHECK(maudGetDeviceName(owner, stale, name, sizeof(name), &length) != maud_success &&
                  length == 0,
              "maudGetDeviceName clears its length");
        FILL(length);
        CHECK(maudGetDeviceKey(owner, stale, name, sizeof(name), &length) != maud_success &&
                  length == 0,
              "maudGetDeviceKey clears its length");
        uint32_t count;
        FILL(count);
        maudDeviceId ids[2];
        CHECK(maudGetDevices(owner, (maudDirection)7, ids, 2, &count) != maud_success && count == 0,
              "maudGetDevices clears its count");

        maudStreamFormat format;
        FILL(format);
        CHECK(maudGetStreamFormat(owner, staleStream, &format) != maud_success &&
                  Zero(&format, sizeof(format)),
              "maudGetStreamFormat clears its format");
        maudStreamStatus status;
        FILL(status);
        CHECK(maudGetStreamStatus(owner, staleStream, &status) != maud_success &&
                  Zero(&status, sizeof(status)),
              "maudGetStreamStatus clears its status");
        uint64_t frames;
        FILL(frames);
        CHECK(maudGetStreamPosition(owner, staleStream, &frames) != maud_success && frames == 0,
              "maudGetStreamPosition clears its frames");
        maudStreamClock clock;
        FILL(clock);
        CHECK(maudGetStreamClock(owner, staleStream, &clock) != maud_success &&
                  Zero(&clock, sizeof(clock)),
              "maudGetStreamClock clears its clock");
        if (pass == 0)
        {
            maudFocus focus;
            FILL(focus);
            CHECK(maudGetContextFocus(owner, &focus) != maud_success && Zero(&focus, sizeof(focus)),
                  "maudGetContextFocus clears its focus");
            maudNotification record;
            FILL(record);
            CHECK(maudNextNotification(nullptr, &record) != maud_success &&
                      Zero(&record, sizeof(record)),
                  "maudNextNotification clears its record");
            maudStreamId stream;
            FILL(stream);
            maudStreamDef def = maudDefaultStreamDef();
            CHECK(maudCreateStream(nullptr, &def, &stream) != maud_success &&
                      Zero(&stream, sizeof(stream)),
                  "maudCreateStream clears its stream");
            maudDeviceId added;
            FILL(added);
            maudOfflineDeviceDef offline = maudDefaultOfflineDeviceDef();
            CHECK(maudAddOfflineDevice(nullptr, &offline, &added) != maud_success &&
                      Zero(&added, sizeof(added)),
                  "maudAddOfflineDevice clears its device");
        }
        (void)what;
    }
    maudNotification record;
    while (maudNextNotification(context, &record) == maud_success)
    {
    }
    FILL(record);
    CHECK(maudNextNotification(context, &record) == maud_empty && Zero(&record, sizeof(record)),
          "an empty queue clears the record");
}

static void TestSpatializerCalls(void)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    maudSpatializer* spatializer = nullptr;
    CHECK(maudCreateSpatializer(&def, &spatializer) == maud_success, "a spatializer");
    maudSourceId staleSource = {77, 3};
    maudProbeSetId staleSet = {77, 3};
    for (int pass = 0; pass < 2; ++pass)
    {
        maudSpatializer* owner = pass == 0 ? nullptr : spatializer;
        maudDirectResult direct;
        FILL(direct);
        CHECK(maudGetDirectResult(owner, staleSource, &direct) != maud_success &&
                  Zero(&direct, sizeof(direct)),
              "maudGetDirectResult clears its result");
        maudProbeSetInfo info;
        FILL(info);
        CHECK(maudGetProbeSet(owner, staleSet, &info, 0, 0, nullptr) != maud_success &&
                  Zero(&info, sizeof(info)),
              "maudGetProbeSet clears its info");
        size_t size;
        FILL(size);
        CHECK(maudSaveProbeSet(owner, staleSet, nullptr, 0, &size) != maud_success && size == 0,
              "maudSaveProbeSet clears its size");
        maudProbeSetId set;
        FILL(set);
        CHECK(maudLoadProbeSet(owner, "x", 1, &set) != maud_success && Zero(&set, sizeof(set)),
              "maudLoadProbeSet clears its set");
        maudSourceDef sourceDef = maudDefaultSourceDef();
        maudSourceId source;
        FILL(source);
        if (pass == 0)
        {
            CHECK(maudCreateSource(owner, &sourceDef, &source) != maud_success &&
                      Zero(&source, sizeof(source)),
                  "maudCreateSource clears its source");
            maudReverbResult reverb;
            FILL(reverb);
            CHECK(maudGetReverbResult(owner, &reverb) != maud_success &&
                      Zero(&reverb, sizeof(reverb)),
                  "maudGetReverbResult clears its result");
            maudProbeSetDef setDef = maudDefaultProbeSetDef();
            FILL(set);
            CHECK(maudCreateProbeSet(owner, &setDef, &set) != maud_success &&
                      Zero(&set, sizeof(set)),
                  "maudCreateProbeSet clears its set");
        }
    }
    maudDestroySpatializer(spatializer);
}

static void TestOtherOwners(void)
{
    maudHrtfInfo hrtf;
    FILL(hrtf);
    CHECK(maudGetHrtfInfo(nullptr, &hrtf) != maud_success && Zero(&hrtf, sizeof(hrtf)),
          "maudGetHrtfInfo clears its info");
    float frames[4] = {0};
    maudVoiceState voice;
    FILL(voice);
    CHECK(maudDetectVoice(nullptr, frames, 4, &voice) != maud_success &&
              Zero(&voice, sizeof(voice)),
          "maudDetectVoice clears its state");
    maudGainState gain;
    FILL(gain);
    CHECK(maudApplyGainControl(nullptr, frames, 4, &gain) != maud_success &&
              Zero(&gain, sizeof(gain)),
          "maudApplyGainControl clears its state");
    maudNoiseState noise;
    FILL(noise);
    CHECK(maudSuppressNoise(nullptr, frames, 4, &noise) != maud_success &&
              Zero(&noise, sizeof(noise)),
          "maudSuppressNoise clears its state");
    maudEchoState echo;
    FILL(echo);
    CHECK(maudCancelEcho(nullptr, frames, frames, 4, &echo) != maud_success &&
              Zero(&echo, sizeof(echo)),
          "maudCancelEcho clears its state");
    maudSceneInstanceId instance;
    FILL(instance);
    CHECK(maudCreateSceneInstance(nullptr, 0, nullptr, &instance) != maud_success &&
              Zero(&instance, sizeof(instance)),
          "maudCreateSceneInstance clears its instance");
}

int main(void)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&def, &context) == maud_success, "an offline context");
    if (context != nullptr)
    {
        TestContextCalls(context);
        CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
    }
    TestSpatializerCalls();
    TestOtherOwners();
    return s_failures == 0 ? 0 : 1;
}
