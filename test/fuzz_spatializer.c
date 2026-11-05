// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the spatializer's calls and the host it runs: sources, poses,
// materials, direct steps, reverberation estimates and the rendering of
// reflections, against a host whose ray queries answer anything. The
// first bytes are the host: a seed its answers are hashed from, a mode
// (plausible hit distances or raw ones, which queries it sets) and four
// hits' distances, normals and materials. One byte shapes the def
// (sources, occlusion points, surfaces, whether estimates are made) and
// the next chooses the reflections' order. Then come up to 24 calls, a
// byte each naming the call and the slot it is about, followed by its
// arguments: floats mostly small multiples of 1/8, sometimes scaled by
// a power of two or raw bits. Each call is held to its header's
// promises:
//
// - a pose, a step's listener and an estimate's listener are taken
//   exactly when they are finite with a nonzero orientation, materials
//   exactly when they fit the capacity and are 0 to 1, and a render's
//   orientation exactly when it is finite and nonzero;
// - every refusal of an invalid argument counts as misuse, and nothing
//   else does;
// - a direct result has a finite distance, a unit direction, and
//   directivity, occlusion and transmission 0 to 1, with at most the
//   def's surfaces counted;
// - a reverberation result has its times within 0.1 to 20 s and finite
//   levels and delay;
// - a render adds finite samples to the bed.
//
// A broken promise prints one line, "test/fuzz_spatializer.c:LINE: expectation
// failed: " with the promise and the values that broke it, never the
// input's bytes: the line a finding's report keeps (tools/fuzz_target.sh).

#include "maul-audio/spatializer.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

enum
{
    MAX_CALLS = 24,
    SLOTS = 4,
    FRAMES = 128
};

// Prints the promise a line broke and the values that broke it, flushed
// before the abort, and aborts.
[[noreturn]] [[gnu::format(printf, 2, 3)]] static void Broken(int line, const char* format, ...)
{
    fprintf(stderr, "test/fuzz_spatializer.c:%d: expectation failed: ", line);
    va_list values;
    va_start(values, format);
    vfprintf(stderr, format, values);
    va_end(values);
    fputc('\n', stderr);
    fflush(stderr);
    abort();
}

#define EXPECT(condition, ...) ((condition) ? (void)0 : Broken(__LINE__, __VA_ARGS__))

// The input read from its start, zeros past its end.
typedef struct Input
{
    const uint8_t* data;
    size_t size;
    size_t at;
} Input;

static uint8_t Byte(Input* in)
{
    return in->at < in->size ? in->data[in->at++] : 0;
}

static uint32_t Word(Input* in)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
    {
        v |= (uint32_t)Byte(in) << (8 * i);
    }
    return v;
}

// Mostly a small multiple of 1/8, sometimes one scaled by a power of
// two, sometimes raw bits.
static float Value(Input* in)
{
    uint8_t mode = Byte(in);
    if (mode < 160)
    {
        return (float)(int8_t)Byte(in) * 0.125f;
    }
    if (mode < 200)
    {
        float v = (float)(int8_t)Byte(in);
        return ldexpf(v, (int8_t)Byte(in));
    }
    uint32_t bits = Word(in);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

// The host: four hits, each ray answered with one of them, chosen by a
// hash of the ray, so that an answer depends only on the ray.
typedef struct Host
{
    uint32_t seed;
    bool plausible;
    float distances[4];
    maudVector3 normals[4];
    uint32_t materials[4];
} Host;

static uint32_t Hash(const maudRay* ray, uint32_t seed)
{
    uint32_t words[sizeof(maudRay) / 4];
    memcpy(words, ray, sizeof(words));
    uint32_t h = seed * 2654435761u;
    for (size_t i = 0; i < sizeof(words) / 4; ++i)
    {
        h = (h ^ words[i]) * 16777619u;
    }
    return h ^ (h >> 15);
}

static void AnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    const Host* host = context;
    EXPECT(count <= 64, "any-hit asks at most 64 rays at a time, %u", count);
    for (uint32_t i = 0; i < count; ++i)
    {
        occluded[i] = (uint8_t)((Hash(&rays[i], host->seed) >> 3) & 1u);
    }
}

static void ClosestHit(const maudRay* rays, uint32_t count, maudRayHit* hits, void* context)
{
    const Host* host = context;
    EXPECT(count <= 64, "closest-hit asks at most 64 rays at a time, %u", count);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t h = Hash(&rays[i], host->seed);
        uint32_t k = h & 3u;
        float distance = host->distances[k];
        if (host->plausible)
        {
            // Within the ray's distances, at most 100 m along.
            float end = fminf(rays[i].maxDistance, rays[i].minDistance + 100.0f);
            distance = rays[i].minDistance +
                       (float)((h >> 8) & 255u) / 255.0f * (end - rays[i].minDistance);
        }
        hits[i] = (maudRayHit){distance, host->normals[k], host->materials[k]};
    }
}

static bool Unit(float v)
{
    return v >= 0.0f && v <= 1.0f;
}

static bool Finite(const float* values, int count)
{
    for (int i = 0; i < count; ++i)
    {
        if (!isfinite(values[i]))
        {
            return false;
        }
    }
    return true;
}

static bool OrientationValid(maudQuaternion q)
{
    const float v[4] = {q.x, q.y, q.z, q.w};
    return Finite(v, 4) && (q.x != 0.0f || q.y != 0.0f || q.z != 0.0f || q.w != 0.0f);
}

static bool PoseValid(const maudPose* p)
{
    const float v[3] = {p->position.x, p->position.y, p->position.z};
    return Finite(v, 3) && OrientationValid(p->orientation);
}

static maudPose Pose(Input* in)
{
    maudPose p = {{Value(in), Value(in), Value(in)}, {0.0f, 0.0f, 0.0f, 1.0f}};
    if (Byte(in) >= 128)
    {
        p.orientation = (maudQuaternion){Value(in), Value(in), Value(in), Value(in)};
    }
    return p;
}

static bool MaterialValid(const maudAcousticMaterial* m)
{
    bool valid = Unit(m->scattering);
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        valid = valid && Unit(m->absorption[b]) && Unit(m->transmission[b]);
    }
    return valid;
}

static void CheckDirect(const maudDirectResult* r, uint32_t maxSurfaces)
{
    EXPECT(isfinite(r->distance) && r->distance >= 0.0f,
           "a direct result's distance is finite and not negative, %a", (double)r->distance);
    double x = (double)r->direction.x;
    double y = (double)r->direction.y;
    double z = (double)r->direction.z;
    double length = sqrt(x * x + y * y + z * z);
    EXPECT(fabs(length - 1.0) < 1e-5, "a direct result's direction is a unit vector, length %a",
           length);
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        EXPECT(Unit(r->directivity[b]) && Unit(r->transmission[b]),
               "a band's directivity and transmission are within [0, 1], band %d, %a and %a", b,
               (double)r->directivity[b], (double)r->transmission[b]);
    }
    EXPECT(Unit(r->occlusion) && r->surfaceCount <= maxSurfaces,
           "occlusion is within [0, 1] and the surfaces within the limit, %a, %u of %u",
           (double)r->occlusion, r->surfaceCount, maxSurfaces);
}

static void CheckReverb(const maudReverbResult* r)
{
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        EXPECT(r->reverbTime[b] >= 0.1f && r->reverbTime[b] <= 20.0f,
               "a band's reverb time is within [0.1, 20] s, band %d, %a", b,
               (double)r->reverbTime[b]);
        EXPECT(r->tailTime[b] == 0.0f || (r->tailTime[b] >= 0.1f && r->tailTime[b] <= 20.0f),
               "a band's tail time is 0 or within [0.1, 20] s, band %d, %a", b,
               (double)r->tailTime[b]);
        EXPECT(isfinite(r->level[b]) && isfinite(r->tailLevel[b]),
               "a band's levels are finite, band %d, %a and %a", b, (double)r->level[b],
               (double)r->tailLevel[b]);
    }
    EXPECT(isfinite(r->delay) && r->delay >= 0.0f,
           "the reverb's delay is finite and not negative, %a", (double)r->delay);
}

// A step's results, read after a latch.
static void CheckStep(maudSpatializer* s, const maudSourceId* ids, uint32_t maxSurfaces)
{
    uint64_t latched = maudSpatializer_LatchResults(s);
    EXPECT(latched > 0, "a step's results are latched, step %llu", (unsigned long long)latched);
    for (int i = 0; i < SLOTS; ++i)
    {
        maudDirectResult r;
        if (ids[i].index1 != 0 && maudSource_GetDirectResult(s, ids[i], &r) == maud_success)
        {
            CheckDirect(&r, maxSurfaces);
        }
    }
    maudReverbResult reverb;
    maudResult got = maudSpatializer_GetReverbResult(s, &reverb);
    EXPECT(got == maud_success, "a step's reverb result is read, result %d", (int)got);
    CheckReverb(&reverb);
}

static maudResult CreateSource(maudSpatializer* s, Input* in, maudSourceId* id)
{
    maudSourceDef def = maudDefaultSourceDef();
    uint8_t flags = Byte(in);
    def.occlusion = (flags & 1u) ? maud_occlusionVolumetric : maud_occlusionRay;
    def.occlusionRadius = Value(in);
    def.occlusionSamples = Byte(in) & 15u;
    def.enableTransmission = (flags & 2u) != 0;
    for (int b = 0; b < MAUD_BANDS; ++b)
    {
        def.directivity.weight[b] = Value(in);
        def.directivity.power[b] = Value(in);
    }
    maudResult r = maudCreateSource(s, &def, id);
    EXPECT((r == maud_success) == (id->index1 != 0),
           "a source has an id exactly when made, result %d, index %u", (int)r, id->index1);
    return r;
}

static maudResult SetMaterials(maudSpatializer* s, Input* in, uint32_t capacity)
{
    maudAcousticMaterial m[5];
    uint32_t count = Byte(in) % 6u;
    bool valid = count <= capacity;
    for (uint32_t i = 0; i < count; ++i)
    {
        for (int b = 0; b < MAUD_BANDS; ++b)
        {
            m[i].absorption[b] = Value(in);
            m[i].transmission[b] = Value(in);
        }
        m[i].scattering = Value(in);
        valid = valid && MaterialValid(&m[i]);
    }
    maudResult r = maudSpatializer_SetMaterials(s, count > 0 ? m : nullptr, count);
    EXPECT((r == maud_success) == valid,
           "materials are set exactly when valid, result %d, valid %d", (int)r, valid);
    return r;
}

static maudResult Render(maudSpatializer* s, Input* in, uint32_t order)
{
    static float send[FRAMES];
    static float channels[16][FRAMES];
    float* bed[16];
    maudQuaternion q = {Value(in), Value(in), Value(in), Value(in)};
    uint32_t frames = Byte(in) % (FRAMES + 1u);
    for (uint32_t i = 0; i < frames; ++i)
    {
        send[i] = (float)(int8_t)Byte(in) / 128.0f;
    }
    for (int c = 0; c < 16; ++c)
    {
        bed[c] = channels[c];
        memset(channels[c], 0, sizeof(channels[c]));
    }
    maudResult r = maudSpatializer_RenderReflections(s, &q, send, bed, frames);
    if (order == 0)
    {
        EXPECT(r == (OrientationValid(q) ? maud_errorUnsupported : maud_errorInvalid),
               "order 0 is unsupported for a valid orientation, invalid otherwise, result %d, "
               "valid %d",
               (int)r, OrientationValid(q));
        return r;
    }
    EXPECT((r == maud_success) == OrientationValid(q),
           "a field rotates exactly when its orientation is valid, result %d, valid %d", (int)r,
           OrientationValid(q));
    for (uint32_t c = 0; c < (order + 1) * (order + 1); ++c)
    {
        for (uint32_t i = 0; i < frames; ++i)
        {
            EXPECT(isfinite(channels[c][i]), "a rotated field is finite, channel %u, frame %u, %a",
                   c, i, (double)channels[c][i]);
        }
    }
    return r;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Input in = {data, size, 0};
    Host host = {.seed = Word(&in)};
    uint8_t mode = Byte(&in);
    host.plausible = (mode & 1u) != 0;
    for (int k = 0; k < 4; ++k)
    {
        host.distances[k] = Value(&in);
        host.normals[k] = (maudVector3){Value(&in), Value(&in), Value(&in)};
        host.materials[k] = Byte(&in) < 200 ? Byte(&in) & 7u : Word(&in);
    }
    uint8_t shape = Byte(&in);
    uint8_t order = Byte(&in) & 3u;
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.sourceCapacity = 1 + (shape & 3u);
    def.maxOcclusionSamples = 1 + ((shape >> 2) & 7u);
    def.maxSurfaces = 1 + ((shape >> 5) & 3u);
    def.materialCapacity = 4;
    def.reverbRays = (shape & 0x80u) != 0 ? MAUD_MIN_REVERB_RAYS : 0;
    def.anyHit = (mode & 2u) != 0 ? AnyHit : nullptr;
    def.closestHit = (mode & 4u) != 0 ? ClosestHit : nullptr;
    def.rayContext = &host;
    if (def.reverbRays > 0)
    {
        def.reflectionOrder = order;
        def.reflectionDuration = 0.05f;
    }
    maudSpatializer* s = nullptr;
    maudResult made = maudCreateSpatializer(&def, &s);
    EXPECT(made == maud_success, "the spatializer is made, result %d", (int)made);
    maudSourceId ids[SLOTS] = {{0, 0}};
    uint64_t misuse = 0;
    for (int call = 0; call < MAX_CALLS && in.at < in.size; ++call)
    {
        uint8_t code = Byte(&in);
        maudSourceId* id = &ids[(code >> 4) & 3u];
        maudResult r = maud_success;
        switch (code & 7u)
        {
        case 0:
            r = CreateSource(s, &in, id);
            break;
        case 1:
            r = maudDestroySource(s, *id);
            if (r == maud_success)
            {
                *id = (maudSourceId){0, 0};
            }
            break;
        case 2:
        {
            maudPose pose = Pose(&in);
            r = maudSource_SetPose(s, *id, &pose);
            if (id->index1 != 0)
            {
                EXPECT((r == maud_success) == PoseValid(&pose),
                       "a source's pose is set exactly when valid, result %d, valid %d", (int)r,
                       PoseValid(&pose));
            }
            break;
        }
        case 3:
            r = SetMaterials(s, &in, def.materialCapacity);
            break;
        case 4:
        case 5:
        {
            maudPose listener = Pose(&in);
            r = maudSpatializer_SimulateDirect(s, &listener);
            EXPECT(
                (r == maud_success) == PoseValid(&listener),
                "a direct step runs exactly when the listener's pose is valid, result %d, valid %d",
                (int)r, PoseValid(&listener));
            if (r == maud_success)
            {
                CheckStep(s, ids, def.maxSurfaces);
            }
            break;
        }
        case 6:
        {
            maudPose listener = Pose(&in);
            r = maudSpatializer_SimulateReverb(s, &listener);
            EXPECT(r == (!PoseValid(&listener) ? maud_errorInvalid
                         : def.reverbRays == 0 ? maud_errorUnsupported
                                               : maud_success),
                   "a reverb step answers by the pose and the rays, result %d, valid %d, rays %u",
                   (int)r, PoseValid(&listener), def.reverbRays);
            break;
        }
        default:
            r = Render(s, &in, def.reflectionOrder);
            break;
        }
        misuse += r == maud_errorInvalid ? 1u : 0u;
        uint64_t counted = maudSpatializer_GetMisuse(s);
        EXPECT(counted == misuse, "each refusal for an invalid argument counts, %llu of %llu",
               (unsigned long long)counted, (unsigned long long)misuse);
    }
    maudDestroySpatializer(s);
    return 0;
}
