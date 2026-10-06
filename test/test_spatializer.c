// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Spatializers: sources fill the capacity, a destroyed source's id is
// stale and its slot comes back with a new generation; a step gives the
// distance and the direction in the listener's frame, turning with the
// listener, and the source's directivity turning with the source; the
// rendering side sees a step only once it latches it, and a source made
// or destroyed after a step is stale in it; bad calls are refused; a
// step allocates nothing.

#include "test_harness.h"

#include "maul-audio/spatializer.h"

#include <math.h>
#include <stdlib.h>

static long s_allocations;

static void* CountedAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_allocations += 1;
    return malloc(size);
}

static void CountedFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

static maudSpatializer* Create(uint32_t capacity)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.sourceCapacity = capacity;
    def.allocator = (maudAllocator){CountedAlloc, CountedFree, nullptr};
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    return s;
}

static maudSourceId Source(maudSpatializer* s)
{
    maudSourceDef def = maudDefaultSourceDef();
    maudSourceId id = {0, 0};
    CHECK(maudCreateSource(s, &def, &id) == maud_success, "a source");
    return id;
}

// A turn by degrees about +y (counterclockwise seen from above).
static maudQuaternion Yaw(double degrees)
{
    double half = degrees * 3.14159265358979323846 / 360.0;
    return (maudQuaternion){0.0f, (float)sin(half), 0.0f, (float)cos(half)};
}

static bool Near(maudVector3 v, float x, float y, float z)
{
    return fabsf(v.x - x) < 1e-5f && fabsf(v.y - y) < 1e-5f && fabsf(v.z - z) < 1e-5f;
}

static void TestIds(void)
{
    maudSpatializer* s = Create(3);
    maudSourceId a = Source(s);
    maudSourceId b = Source(s);
    maudSourceId c = Source(s);
    maudSourceDef def = maudDefaultSourceDef();
    maudSourceId d = {9, 9};
    CHECK(maudCreateSource(s, &def, &d) == maud_errorCapacity && d.index1 == 0, "full");
    CHECK(a.index1 == 1 && b.index1 == 2 && c.index1 == 3, "the lowest slots first");
    CHECK(maudDestroySource(s, b) == maud_success, "destroy");
    CHECK(maudDestroySource(s, b) == maud_errorStale, "twice: stale");
    maudPose pose = {{1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSetSourcePose(s, b, &pose) == maud_errorStale, "a stale pose");
    d = Source(s);
    CHECK(d.index1 == b.index1 && d.generation != b.generation, "the slot again, a new generation");
    CHECK(maudSetSourcePose(s, b, &pose) == maud_errorStale, "the old id stays stale");
    CHECK(maudSetSourcePose(s, (maudSourceId){0, 0}, &pose) == maud_errorInvalid, "id 0");
    CHECK(maudSetSourcePose(s, (maudSourceId){4, 1}, &pose) == maud_errorInvalid, "past the end");
    pose.orientation = (maudQuaternion){0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(maudSetSourcePose(s, a, &pose) == maud_errorInvalid, "a zero orientation");
    pose.orientation.w = 1.0f;
    pose.position.x = NAN;
    CHECK(maudSetSourcePose(s, a, &pose) == maud_errorInvalid, "NaN");
    maudDestroySpatializer(s);
}

static void TestGeometry(void)
{
    maudSpatializer* s = Create(4);
    maudSourceId id = Source(s);
    maudPose source = {{3.0f, 1.0f, -4.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSetSourcePose(s, id, &source) == maud_success, "pose");
    maudPose listener = {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    long before = s_allocations;
    CHECK(maudSimulateDirect(s, &listener) == maud_success, "step");
    CHECK(s_allocations == before, "a step allocates nothing");
    CHECK(maudLatchResults(s) == 1, "the first step");
    maudDirectResult r;
    CHECK(maudGetDirectResult(s, id, &r) == maud_success, "result");
    CHECK(fabsf(r.distance - 5.0f) < 1e-5f && Near(r.direction, 0.6f, 0.0f, -0.8f),
          "5 m ahead and to the right");
    CHECK(r.occlusion == 0.0f && r.transmission[0] == 1.0f && r.directivity[2] == 1.0f,
          "a clear path from an omnidirectional source");
    // The listener turns 90 degrees left, to face -x: the source is now
    // to its right and behind.
    listener.orientation = Yaw(90.0);
    CHECK(maudSimulateDirect(s, &listener) == maud_success && maudLatchResults(s) == 2, "step");
    CHECK(maudGetDirectResult(s, id, &r) == maud_success && Near(r.direction, 0.8f, 0.0f, 0.6f),
          "the direction turns with the listener");
    // A cardioid source facing the listener, then turned away.
    maudSourceDef cardioid = maudDefaultSourceDef();
    cardioid.directivity = (maudDirectivityPattern){{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}};
    maudSourceId loud = {0, 0};
    CHECK(maudCreateSource(s, &cardioid, &loud) == maud_success, "a cardioid");
    maudPose ahead = {{0.0f, 1.0f, -2.0f}, Yaw(180.0)};
    CHECK(maudSetSourcePose(s, loud, &ahead) == maud_success, "pose");
    listener.orientation = Yaw(0.0);
    CHECK(maudSimulateDirect(s, &listener) == maud_success && maudLatchResults(s) == 3, "step");
    CHECK(maudGetDirectResult(s, loud, &r) == maud_success &&
              fabsf(r.directivity[1] - 1.0f) < 1e-5f,
          "facing the listener: full");
    ahead.orientation = Yaw(0.0);
    CHECK(maudSetSourcePose(s, loud, &ahead) == maud_success, "pose");
    CHECK(maudSimulateDirect(s, &listener) == maud_success && maudLatchResults(s) == 4, "step");
    CHECK(maudGetDirectResult(s, loud, &r) == maud_success && fabsf(r.directivity[1]) < 1e-5f,
          "turned away: the cardioid's null");
    maudPose same = listener;
    CHECK(maudSetSourcePose(s, loud, &same) == maud_success, "pose");
    CHECK(maudSimulateDirect(s, &listener) == maud_success && maudLatchResults(s) == 5, "step");
    CHECK(maudGetDirectResult(s, loud, &r) == maud_success && r.distance == 0.0f &&
              Near(r.direction, 0.0f, 0.0f, -1.0f),
          "at the listener: ahead");
    maudDestroySpatializer(s);
}

// v turned by the unit quaternion q, through its rotation matrix.
static maudVector3 Rotate(maudQuaternion q, maudVector3 v)
{
    double x = (double)q.x;
    double y = (double)q.y;
    double z = (double)q.z;
    double w = (double)q.w;
    double m[3][3] = {
        {1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
        {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
        {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)},
    };
    double in[3] = {(double)v.x, (double)v.y, (double)v.z};
    double out[3];
    for (int i = 0; i < 3; ++i)
    {
        out[i] = m[i][0] * in[0] + m[i][1] * in[1] + m[i][2] * in[2];
    }
    return (maudVector3){(float)out[0], (float)out[1], (float)out[2]};
}

// For listeners turned every way: a source placed along a direction d
// in the listener's frame (turned into the world by the listener's
// orientation) is reported along d.
static void TestOrientations(void)
{
    maudSpatializer* s = Create(2);
    maudSourceId id = Source(s);
    uint32_t seed = 21;
    double worst = 0.0;
    for (int trial = 0; trial < 200; ++trial)
    {
        double v[7];
        for (int i = 0; i < 7; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            v[i] = (double)(seed >> 8) / 8388608.0 - 1.0;
        }
        double n = sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
        maudQuaternion q = {(float)(v[0] / n), (float)(v[1] / n), (float)(v[2] / n),
                            (float)(v[3] / n)};
        double m = sqrt(v[4] * v[4] + v[5] * v[5] + v[6] * v[6]);
        maudVector3 d = {(float)(v[4] / m), (float)(v[5] / m), (float)(v[6] / m)};
        maudVector3 w = Rotate(q, d);
        maudPose listener = {{1.0f, -2.0f, 3.0f}, q};
        maudPose source = {{1.0f + 4.0f * w.x, -2.0f + 4.0f * w.y, 3.0f + 4.0f * w.z},
                           {0.0f, 0.0f, 0.0f, 1.0f}};
        // An unnormalized orientation means the same turn.
        listener.orientation.x *= 3.0f;
        listener.orientation.y *= 3.0f;
        listener.orientation.z *= 3.0f;
        listener.orientation.w *= 3.0f;
        maudDirectResult r;
        CHECK(maudSetSourcePose(s, id, &source) == maud_success &&
                  maudSimulateDirect(s, &listener) == maud_success && maudLatchResults(s) > 0 &&
                  maudGetDirectResult(s, id, &r) == maud_success,
              "a result");
        worst =
            fmax(worst, fabs((double)(r.direction.x - d.x)) + fabs((double)(r.direction.y - d.y)) +
                            fabs((double)(r.direction.z - d.z)));
    }
    CHECK(worst < 1e-4, "directions come back in the listener's frame");
    // An id for a slot never used is stale, not a source.
    maudPose pose = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSetSourcePose(s, (maudSourceId){2, 1}, &pose) == maud_errorStale,
          "a slot never used holds no source");
    maudDestroySpatializer(s);
}

static void TestLatch(void)
{
    maudSpatializer* s = Create(4);
    maudSourceId a = Source(s);
    maudDirectResult r;
    CHECK(maudLatchResults(s) == 0, "nothing published");
    CHECK(maudGetDirectResult(s, a, &r) == maud_errorInvalid, "no step latched");
    maudPose listener = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudPose near = {{0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSetSourcePose(s, a, &near) == maud_success, "pose");
    CHECK(maudSimulateDirect(s, &listener) == maud_success, "step 1");
    CHECK(maudGetDirectResult(s, a, &r) == maud_errorInvalid, "published but not latched");
    CHECK(maudLatchResults(s) == 1, "latched");
    maudPose far = {{0.0f, 0.0f, -7.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSetSourcePose(s, a, &far) == maud_success, "pose");
    maudSourceId b = Source(s);
    CHECK(maudSimulateDirect(s, &listener) == maud_success, "step 2");
    CHECK(maudSimulateDirect(s, &listener) == maud_success, "step 3");
    CHECK(maudGetDirectResult(s, a, &r) == maud_success && fabsf(r.distance - 1.0f) < 1e-6f,
          "the latched step holds until the next latch");
    CHECK(maudGetDirectResult(s, b, &r) == maud_errorStale, "made after the latched step");
    CHECK(maudLatchResults(s) == 3, "the newest step, skipping 2");
    CHECK(maudLatchResults(s) == 3, "and no newer one");
    CHECK(maudGetDirectResult(s, a, &r) == maud_success && fabsf(r.distance - 7.0f) < 1e-6f,
          "the newer pose");
    CHECK(maudDestroySource(s, a) == maud_success, "destroy");
    CHECK(maudGetDirectResult(s, a, &r) == maud_success, "still held by the latched step");
    CHECK(maudSimulateDirect(s, &listener) == maud_success && maudLatchResults(s) == 4, "step");
    CHECK(maudGetDirectResult(s, a, &r) == maud_errorStale, "gone from later steps");
    CHECK(maudGetDirectResult(s, b, &r) == maud_success, "the newer source is there");
    maudDestroySpatializer(s);
}

static void TestMisuse(void)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    maudSpatializer* none = (maudSpatializer*)&def;
    def.sourceCapacity = 0;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid && none == nullptr, "no room");
    def.sourceCapacity = 70000;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid, "too many");
    def = maudDefaultSpatializerDef();
    def.cookie = 0;
    CHECK(maudCreateSpatializer(&def, &none) == maud_errorInvalid, "no cookie");
    maudSpatializer* s = Create(2);
    maudSourceDef bad = maudDefaultSourceDef();
    bad.directivity.weight[1] = 2.0f;
    maudSourceId id = {0, 0};
    CHECK(maudCreateSource(s, &bad, &id) == maud_errorInvalid, "a bad pattern");
    bad = maudDefaultSourceDef();
    bad.cookie = 0;
    CHECK(maudCreateSource(s, &bad, &id) == maud_errorInvalid, "a source def without its cookie");
    maudPose listener = {{0.0f, INFINITY, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    CHECK(maudSimulateDirect(s, &listener) == maud_errorInvalid, "an infinite listener");
    CHECK(maudSimulateDirect(s, nullptr) == maud_errorInvalid, "no listener");
    CHECK(maudLatchResults(nullptr) == 0, "latch nothing");
    maudDestroySpatializer(s);
    maudDestroySpatializer(nullptr);
}

int main(void)
{
    TestIds();
    TestGeometry();
    TestOrientations();
    TestLatch();
    TestMisuse();
    return s_failures == 0 ? 0 : 1;
}
