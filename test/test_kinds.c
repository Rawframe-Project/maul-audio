// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every enumeration a call takes, by value or in a def, given -1 and
// INT32_MIN: values below its first constant, which a check of the upper
// bound alone lets through to index a table. The layout queries answer
// none; every other call and creation answers maud_errorInvalid, a
// creation with nothing made.

#include "test_harness.h"

#include "maul-audio/ambisonics.h"
#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/focus.h"
#include "maul-audio/layout.h"
#include "maul-audio/offline.h"
#include "maul-audio/spatializer.h"
#include "maul-audio/speakers.h"
#include "maul-audio/stream.h"
#include "maul-audio/voice.h"

#include <stdint.h>

static const int32_t s_below[2] = {-1, INT32_MIN};

static void Silence(const maudStreamBlock* block, void* user)
{
    (void)block;
    (void)user;
}

static void TestLayouts(int32_t below)
{
    maudSpeakerPosition position = maudGetLayoutSpeakerPosition(below, 0);
    CHECK(maudGetLayoutChannelCount(below) == 0 &&
              maudGetLayoutSpeaker(below, 0) == maud_speakerNone &&
              position.azimuthDegrees == 0.0f && position.elevationDegrees == 0.0f,
          "a layout below the first: no channels, no speaker, no position");
}

static void TestContext(int32_t below)
{
    maudContextDef def = maudDefaultContextDef();
    def.backend = below;
    maudContext* context = (maudContext*)&def;
    CHECK(maudCreateContext(&def, &context) == maud_errorInvalid && context == nullptr,
          "a context's backend");
}

static void TestQueries(maudContext* context, int32_t below)
{
    maudDeviceId ids[2];
    uint32_t count = 0;
    maudDeviceId device = {0, 0};
    CHECK(maudContext_GetDevices(context, below, ids, 2, &count) == maud_errorInvalid,
          "the devices of a direction");
    CHECK(maudContext_GetDefaultDevice(context, below, maud_roleGeneral, &device) ==
                  maud_errorInvalid &&
              maudContext_GetDefaultDevice(context, maud_directionOutput, below, &device) ==
                  maud_errorInvalid,
          "a default's direction and role");
    CHECK(maudContext_RequestFocus(context, below, maud_roleGeneral) == maud_errorInvalid &&
              maudContext_RequestFocus(context, maud_focusLasting, below) == maud_errorInvalid,
          "a focus request and its role");
}

static void TestOffline(maudContext* context, int32_t below)
{
    maudOfflineDeviceDef good = maudDefaultOfflineDeviceDef();
    maudOfflineDeviceDef bad[3] = {good, good, good};
    bad[0].direction = below;
    bad[1].form = below;
    bad[2].layout = below;
    bool refused = true;
    for (int i = 0; i < 3; ++i)
    {
        maudDeviceId id = {7, 7};
        bool one =
            maudCreateOfflineDevice(context, &bad[i], &id) == maud_errorInvalid && id.index1 == 0;
        if (!one)
        {
            printf("offline device def field %d not refused\n", i);
        }
        refused = refused && one;
    }
    CHECK(refused, "an offline device's direction, form and layout");
    maudDeviceId device = {0, 0};
    CHECK(maudCreateOfflineDevice(context, &good, &device) == maud_success, "an offline device");
    CHECK(maudOfflineDevice_SetDefault(context, device, below) == maud_errorInvalid &&
              maudOfflineDevice_SetForm(context, device, below) == maud_errorInvalid &&
              maudOfflineDevice_SetFormat(context, device, below, 48000) == maud_errorInvalid &&
              maudOfflineDevice_SetSpatializer(context, device, below, false, 0) ==
                  maud_errorInvalid,
          "an offline device's role, form, layout and spatializer");
    CHECK(maudDestroyOfflineDevice(context, device) == maud_success, "destroyed");
}

static void TestStream(maudContext* context, int32_t below)
{
    maudStreamDef good = maudDefaultStreamDef();
    good.mode = maud_modePull;
    good.callback = Silence;
    maudStreamDef bad[7];
    for (int i = 0; i < 7; ++i)
    {
        bad[i] = good;
    }
    bad[0].direction = below;
    bad[1].mode = below;
    bad[2].ratePolicy = below;
    bad[3].layout = below;
    bad[4].share = below;
    bad[5].role = below;
    // A set of flags: -1 and INT32_MIN set bits that name no part.
    bad[6].direction = maud_directionInput;
    bad[6].voice = (maudVoiceProcessing)below;
    bool refused = true;
    for (int i = 0; i < 7; ++i)
    {
        maudStreamId stream = {7, 7};
        bool one =
            maudCreateStream(context, &bad[i], &stream) == maud_errorInvalid && stream.index1 == 0;
        if (!one)
        {
            printf("stream def field %d not refused\n", i);
        }
        refused = refused && one;
    }
    CHECK(refused, "a stream's direction, mode, rate policy, layout, share, role and voice");
}

static void TestSpatial(int32_t below)
{
    maudSpatializerDef spatializerDef = maudDefaultSpatializerDef();
    maudSpatializer* spatializer = nullptr;
    CHECK(maudCreateSpatializer(&spatializerDef, &spatializer) == maud_success, "a spatializer");
    maudSourceDef sourceDef = maudDefaultSourceDef();
    sourceDef.occlusion = below;
    maudSourceId source = {7, 7};
    CHECK(maudCreateSource(spatializer, &sourceDef, &source) == maud_errorInvalid &&
              source.index1 == 0,
          "a source's occlusion method");
    maudDestroySpatializer(spatializer);
    maudSpeakerDecoderDef decoderDef = maudDefaultSpeakerDecoderDef();
    decoderDef.layout = below;
    maudSpeakerDecoder* decoder = (maudSpeakerDecoder*)&decoderDef;
    CHECK(maudCreateSpeakerDecoder(&decoderDef, &decoder) == maud_errorInvalid &&
              decoder == nullptr,
          "a speaker decoder's layout");
    maudSpeakerPannerDef pannerDef = maudDefaultSpeakerPannerDef();
    pannerDef.layout = below;
    maudSpeakerPanner* panner = (maudSpeakerPanner*)&pannerDef;
    CHECK(maudCreateSpeakerPanner(&pannerDef, &panner) == maud_errorInvalid && panner == nullptr,
          "a speaker panner's layout");
}

static void TestVoice(int32_t below)
{
    maudVoiceDetectorDef detectorDef = maudDefaultVoiceDetectorDef();
    detectorDef.layout = below;
    maudVoiceDetector* detector = (maudVoiceDetector*)&detectorDef;
    CHECK(maudCreateVoiceDetector(&detectorDef, &detector) == maud_errorInvalid &&
              detector == nullptr,
          "a voice detector's layout");
    maudGainControlDef gainDef = maudDefaultGainControlDef();
    gainDef.layout = below;
    maudGainControl* gain = (maudGainControl*)&gainDef;
    CHECK(maudCreateGainControl(&gainDef, &gain) == maud_errorInvalid && gain == nullptr,
          "a gain control's layout");
    maudNoiseSuppressorDef suppressorDef = maudDefaultNoiseSuppressorDef();
    suppressorDef.layout = below;
    maudNoiseSuppressor* suppressor = (maudNoiseSuppressor*)&suppressorDef;
    CHECK(maudCreateNoiseSuppressor(&suppressorDef, &suppressor) == maud_errorInvalid &&
              suppressor == nullptr,
          "a noise suppressor's layout");
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
    for (int i = 0; i < 2; ++i)
    {
        int32_t below = s_below[i];
        printf("below the first constant: %d\n", (int)below);
        TestLayouts(below);
        TestContext(below);
        TestQueries(context, below);
        TestOffline(context, below);
        TestStream(context, below);
        TestSpatial(below);
        TestVoice(below);
    }
    CHECK(maudDestroyContext(context) == maud_success, "destroyed");
    return s_failures == 0 ? 0 : 1;
}
