// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Outputs on failure, so a caller that ignores a status reads zero, never
// stale data: a call on an owner object
// clears its outputs on any failure, before it checks the owner (a
// missing owner's failure clears them too), except an output the failure
// itself defines, which it writes. Each call here runs with its outputs
// filled with a pattern, once with a NULL owner and once with a valid
// owner and a stale or unknown id, and each output must read zero after.
// A caller's array reads zero over its whole room on a failure that
// defines none of its items (an empty queue, a name that does not fit);
// a success writes only the items it reports and leaves the room past
// them as it was (fewer devices, a short name).

#include "test_harness.h"

#include "maul-audio/ambisonics.h"
#include "maul-audio/buffer.h"
#include "maul-audio/context.h"
#include "maul-audio/device.h"
#include "maul-audio/direct.h"
#include "maul-audio/focus.h"
#include "maul-audio/hrtf.h"
#include "maul-audio/notification.h"
#include "maul-audio/objects.h"
#include "maul-audio/offline.h"
#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"
#include "maul-audio/speakers.h"
#include "maul-audio/stream.h"
#include "maul-audio/voice.h"

#include <math.h>
#include <string.h>

// Whether every float reads the fill pattern.
static bool Untouched(const float* values, size_t count)
{
    float pattern;
    memset(&pattern, 0xAB, sizeof(pattern));
    for (size_t i = 0; i < count; ++i)
    {
        if (memcmp(&values[i], &pattern, sizeof(pattern)) != 0)
        {
            return false;
        }
    }
    return true;
}

// Whether bytes all read the fill pattern.
static bool Filled(const void* bytes, size_t size)
{
    const unsigned char* p = bytes;
    for (size_t i = 0; i < size; ++i)
    {
        if (p[i] != 0xAB)
        {
            return false;
        }
    }
    return true;
}

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
        CHECK(maudDevice_GetInfo(owner, stale, &info) != maud_success && Zero(&info, sizeof(info)),
              "maudDevice_GetInfo clears its info");
        maudDeviceId device;
        FILL(device);
        CHECK(maudContext_GetDefaultDevice(owner, (maudDirection)7, maud_roleGeneral, &device) !=
                      maud_success &&
                  Zero(&device, sizeof(device)),
              "maudContext_GetDefaultDevice clears its device");
        size_t length;
        FILL(length);
        char name[8];
        FILL(name);
        CHECK(maudDevice_GetName(owner, stale, name, sizeof(name), &length) != maud_success &&
                  length == 0 && Zero(name, sizeof(name)),
              "maudDevice_GetName clears its length and bytes");
        FILL(length);
        FILL(name);
        CHECK(maudDevice_GetKey(owner, stale, name, sizeof(name), &length) != maud_success &&
                  length == 0 && Zero(name, sizeof(name)),
              "maudDevice_GetKey clears its length and bytes");
        uint32_t count;
        FILL(count);
        maudDeviceId ids[2];
        FILL(ids);
        CHECK(maudContext_GetDevices(owner, (maudDirection)7, ids, 2, &count) != maud_success &&
                  count == 0 && Zero(ids, sizeof(ids)),
              "maudContext_GetDevices clears its count and ids");

        maudStreamFormat format;
        FILL(format);
        CHECK(maudStream_GetFormat(owner, staleStream, &format) != maud_success &&
                  Zero(&format, sizeof(format)),
              "maudStream_GetFormat clears its format");
        maudStreamStatus status;
        FILL(status);
        CHECK(maudStream_GetStatus(owner, staleStream, &status) != maud_success &&
                  Zero(&status, sizeof(status)),
              "maudStream_GetStatus clears its status");
        uint64_t frames;
        FILL(frames);
        CHECK(maudStream_GetPosition(owner, staleStream, &frames) != maud_success && frames == 0,
              "maudStream_GetPosition clears its frames");
        maudStreamClock clock;
        FILL(clock);
        CHECK(maudStream_GetClock(owner, staleStream, &clock) != maud_success &&
                  Zero(&clock, sizeof(clock)),
              "maudStream_GetClock clears its clock");
        if (pass == 0)
        {
            maudFocus focus;
            FILL(focus);
            CHECK(maudContext_GetFocus(owner, &focus) != maud_success &&
                      Zero(&focus, sizeof(focus)),
                  "maudContext_GetFocus clears its focus");
            maudNotification record;
            FILL(record);
            CHECK(maudContext_NextNotification(nullptr, &record) != maud_success &&
                      Zero(&record, sizeof(record)),
                  "maudContext_NextNotification clears its record");
            maudStreamId stream;
            FILL(stream);
            maudStreamDef def = maudDefaultStreamDef();
            CHECK(maudCreateStream(nullptr, &def, &stream) != maud_success &&
                      Zero(&stream, sizeof(stream)),
                  "maudCreateStream clears its stream");
            maudDeviceId added;
            FILL(added);
            maudOfflineDeviceDef offline = maudDefaultOfflineDeviceDef();
            CHECK(maudCreateOfflineDevice(nullptr, &offline, &added) != maud_success &&
                      Zero(&added, sizeof(added)),
                  "maudCreateOfflineDevice clears its device");
        }
        (void)what;
    }
    maudNotification record;
    while (maudContext_NextNotification(context, &record) == maud_success)
    {
    }
    FILL(record);
    CHECK(maudContext_NextNotification(context, &record) == maud_empty &&
              Zero(&record, sizeof(record)),
          "an empty queue clears the record");
    // Fewer devices than the room: the ids past them are left. A name
    // shorter than the room: the bytes past it are left. A name longer
    // than the room: the bytes read zero.
    maudDeviceId room[16];
    uint32_t listed = 0;
    FILL(room);
    CHECK(maudContext_GetDevices(context, maud_directionOutput, room, 16, &listed) ==
                  maud_success &&
              listed >= 1 && listed < 16 && Filled(room + listed, (16 - listed) * sizeof(room[0])),
          "the ids past the devices listed are left");
    char text[256];
    size_t length = 0;
    FILL(text);
    CHECK(maudDevice_GetName(context, room[0], text, sizeof(text), &length) == maud_success &&
              length < sizeof(text) && Filled(text + length, sizeof(text) - length),
          "the bytes past a name are left");
    FILL(text);
    CHECK(maudDevice_GetKey(context, room[0], text, sizeof(text), &length) == maud_success &&
              length < sizeof(text) && Filled(text + length, sizeof(text) - length),
          "the bytes past a key are left");
    char shortName[1];
    size_t needed = 0;
    FILL(shortName);
    CHECK(maudDevice_GetName(context, room[0], shortName, sizeof(shortName), &needed) ==
                  maud_errorCapacity &&
              needed > sizeof(shortName) && shortName[0] == 0,
          "a name past the room leaves zeros");
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
        CHECK(maudSource_GetDirectResult(owner, staleSource, &direct) != maud_success &&
                  Zero(&direct, sizeof(direct)),
              "maudSource_GetDirectResult clears its result");
        maudProbeSetInfo info;
        FILL(info);
        CHECK(maudProbeSet_GetInfo(owner, staleSet, 0, &info, nullptr, 0) != maud_success &&
                  Zero(&info, sizeof(info)),
              "maudProbeSet_GetInfo clears its info");
        maudVector3 points[4];
        FILL(info);
        FILL(points);
        CHECK(maudProbeSet_GetInfo(owner, staleSet, 0, &info, points, 4) != maud_success &&
                  Zero(&info, sizeof(info)) && Zero(points, sizeof(points)),
              "maudProbeSet_GetInfo clears its info and points");
        size_t size;
        FILL(size);
        CHECK(maudProbeSet_Save(owner, staleSet, nullptr, 0, &size) != maud_success && size == 0,
              "maudProbeSet_Save clears its size");
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
            CHECK(maudSpatializer_GetReverbResult(owner, &reverb) != maud_success &&
                      Zero(&reverb, sizeof(reverb)),
                  "maudSpatializer_GetReverbResult clears its result");
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
    CHECK(maudHrtf_GetInfo(nullptr, &hrtf) != maud_success && Zero(&hrtf, sizeof(hrtf)),
          "maudHrtf_GetInfo clears its info");
    float frames[4] = {0};
    maudVoiceState voice;
    FILL(voice);
    CHECK(maudVoiceDetector_Detect(nullptr, frames, 4, &voice) != maud_success &&
              Zero(&voice, sizeof(voice)),
          "maudVoiceDetector_Detect clears its state");
    maudGainState gain;
    FILL(gain);
    CHECK(maudGainControl_Apply(nullptr, frames, 4, &gain) != maud_success &&
              Zero(&gain, sizeof(gain)),
          "maudGainControl_Apply clears its state");
    maudNoiseState noise;
    FILL(noise);
    CHECK(maudNoiseSuppressor_Suppress(nullptr, frames, 4, &noise) != maud_success &&
              Zero(&noise, sizeof(noise)),
          "maudNoiseSuppressor_Suppress clears its state");
    maudEchoState echo;
    FILL(echo);
    CHECK(maudEchoCanceller_Cancel(nullptr, frames, frames, 4, &echo) != maud_success &&
              Zero(&echo, sizeof(echo)),
          "maudEchoCanceller_Cancel clears its state");
    maudSceneInstanceId instance;
    FILL(instance);
    CHECK(maudCreateSceneInstance(nullptr, 0, nullptr, &instance) != maud_success &&
              Zero(&instance, sizeof(instance)),
          "maudCreateSceneInstance clears its instance");
}

// Samples without an object leave their outputs as they were; an
// object's array whose length the object defines is cleared when the
// object is there, left as it was when it is NULL.
static void TestArrays(void)
{
    float gains[64];
    maudVector3 bad = {NAN, 0.0f, 0.0f};
    maudVector3 ahead = {0.0f, 0.0f, -1.0f};
    FILL(gains);
    CHECK(maudGetAmbisonicGains(99, ahead, gains) != maud_success && Untouched(gains, 64),
          "maudGetAmbisonicGains leaves its gains");
    FILL(gains);
    CHECK(maudGetAirAbsorption(NAN, 50.0f, gains) != maud_success && Untouched(gains, 3),
          "maudGetAirAbsorption leaves its exponents");
    FILL(gains);
    CHECK(maudGetDirectivity(nullptr, ahead, gains) != maud_success && Untouched(gains, 3),
          "maudGetDirectivity leaves its gains");
    FILL(gains);
    CHECK(maudInterleave(nullptr, 2, 4, gains) != maud_success && Untouched(gains, 8),
          "maudInterleave leaves its samples");
    FILL(gains);
    CHECK(maudSpeakerPanner_GetGains(nullptr, ahead, gains) != maud_success && Untouched(gains, 64),
          "maudSpeakerPanner_GetGains leaves its gains without a panner");
    maudSpeakerPannerDef def = maudDefaultSpeakerPannerDef();
    maudSpeakerPanner* panner = nullptr;
    if (maudCreateSpeakerPanner(&def, &panner) == maud_success)
    {
        FILL(gains);
        CHECK(maudSpeakerPanner_GetGains(panner, bad, gains) != maud_success &&
                  Zero(gains, 2 * sizeof(float)),
              "and clears them with one");
        maudDestroySpeakerPanner(panner);
    }
    FILL(gains);
    CHECK(maudSpeakerDecoder_GetMatrix(nullptr, gains) != maud_success && Untouched(gains, 64),
          "maudSpeakerDecoder_GetMatrix leaves its matrix without a decoder");
}

// A stream that renders itself again from its own callback, into frames
// and records filled with the pattern: refused as busy, they keep it.
typedef struct Reentry
{
    maudContext* context;
    maudStreamId stream;
    bool objects;
    bool tried;
    bool kept;
} Reentry;

static void RenderAgain(const maudStreamBlock* block, void* user)
{
    (void)block;
    Reentry* again = user;
    if (again->tried)
    {
        return;
    }
    again->tried = true;
    float frames[2 * 8];
    float samples[8];
    maudStreamObject record = {.samples = samples};
    FILL(frames);
    FILL(samples);
    maudResult result =
        again->objects
            ? maudStream_RenderObjects(again->context, again->stream, frames, &record, 1, 8)
            : maudStream_Render(again->context, again->stream, frames, 8);
    again->kept = result == maud_errorBusy && Untouched(frames, 16) && Untouched(samples, 8) &&
                  record.samples == samples;
}

// Renders refused on an offline context: silence where the stream gives
// the frames' size, the frames left where no stream does (a NULL context,
// a stale id) or where a busy refusal turns away a call back in from the
// stream's own callback, and the object records cleared but for their
// samples pointers, whose frames the caller sized.
static void TestRefusedRenders(void)
{
    maudContextDef cdef = maudDefaultContextDef();
    cdef.backend = maud_backendOffline;
    maudContext* context = nullptr;
    CHECK(maudCreateContext(&cdef, &context) == maud_success, "an offline context");
    if (context == nullptr)
    {
        return;
    }
    Reentry plainAgain = {.context = context};
    Reentry objectsAgain = {.context = context, .objects = true};
    maudStreamDef def = maudDefaultStreamDef();
    def.mode = maud_modePull;
    def.layout = maud_layoutStereo;
    def.periodFrames = 8;
    def.callback = RenderAgain;
    def.user = &plainAgain;
    maudStreamId plain = {0, 0};
    CHECK(maudCreateStream(context, &def, &plain) == maud_success, "a stereo stream");
    plainAgain.stream = plain;
    def.objectCount = 1;
    def.user = &objectsAgain;
    maudStreamId objects = {0, 0};
    CHECK(maudCreateStream(context, &def, &objects) == maud_success, "an object stream");
    objectsAgain.stream = objects;
    maudStreamId stale = {77, 3};
    float frames[2 * 8];
    float samples[8];
    maudStreamObject record = {.samples = samples};

    FILL(frames);
    CHECK(maudStream_Render(context, plain, frames, 8) == maud_errorState && Zero(frames, 64),
          "a stopped stream's render leaves silence");
    CHECK(maudStream_Start(context, plain) == maud_success &&
              maudStream_Start(context, objects) == maud_success,
          "both started");
    FILL(frames);
    CHECK(maudStream_Render(context, objects, frames, 8) == maud_errorInvalid && Zero(frames, 64),
          "an object stream's plain render leaves silence");
    FILL(frames);
    CHECK(maudStream_Render(nullptr, plain, frames, 8) == maud_errorInvalid &&
              Untouched(frames, 16),
          "no context, no size: the frames are left");
    FILL(frames);
    CHECK(maudStream_Render(context, stale, frames, 8) == maud_errorStale && Untouched(frames, 16),
          "a stale stream, no size: the frames are left");

    FILL(frames);
    FILL(samples);
    FILL(record.position);
    FILL(record.gain);
    record.active = true;
    CHECK(maudStream_RenderObjects(context, plain, frames, &record, 1, 8) == maud_errorInvalid &&
              Zero(frames, 64) && Zero(samples, sizeof(samples)) && record.samples == samples &&
              Zero(&record.position, sizeof(record.position)) && record.gain == 0.0f &&
              !record.active,
          "objects from a plain stream: the bed and the record cleared");
    for (int pass = 0; pass < 2; ++pass)
    {
        maudContext* owner = pass == 0 ? nullptr : context;
        maudStreamId id = pass == 0 ? objects : stale;
        FILL(frames);
        FILL(samples);
        FILL(record.position);
        FILL(record.gain);
        CHECK(maudStream_RenderObjects(owner, id, frames, &record, 1, 8) != maud_success &&
                  Untouched(frames, 16) && Zero(samples, sizeof(samples)) &&
                  record.samples == samples && record.gain == 0.0f,
              "no stream: the bed is left, the record and its frames cleared");
    }

    CHECK(maudStream_Render(context, plain, frames, 8) == maud_success && plainAgain.tried &&
              plainAgain.kept,
          "a render called back in from the callback keeps its frames");
    maudStreamObject outer = {.samples = samples};
    CHECK(maudStream_RenderObjects(context, objects, frames, &outer, 1, 8) == maud_success &&
              objectsAgain.tried && objectsAgain.kept,
          "an object render called back in keeps its bed and records");
    CHECK(maudDestroyContext(context) == maud_success, "destroy the context");
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
    TestArrays();
    TestRefusedRenders();
    return s_failures == 0 ? 0 : 1;
}
