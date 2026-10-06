// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Baked reverberation in an office (5 x 3 x 4 m, a material per wall):
// on a probe a baked estimate has the bits of a live one there; between
// probes (1 m apart) it is within 3 % and 1.5 dB of the live one (near
// the corners, where the level follows the near walls' early
// reflections more finely than the probes, 1.3 dB); walking 2 cm at a
// time it never jumps more than 0.2 dB; tasks run one at a time in
// reverse give the same bits; with no probe in sight it traces; with
// reflections, the baked field on a probe renders the live one's
// samples. Using a set before its bake, baking without rays or a
// closest-hit query, and a destroyed set's id are refused. Saved and
// loaded into another spatializer a bake gives the same estimates and
// saves to the same bytes; a file with fields loads only where they
// match; the office's bake is, byte for byte, the shipped one in
// data/bake (MAUD_WRITE_GOLDEN set rewrites it), and on the web, which
// has no file system, its hash.

#include "test_harness.h"

#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    FRAMES = 9600
};

static maudAcousticScene* Office(void)
{
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[12] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
    maudVector3 v[8];
    for (int i = 0; i < 8; ++i)
    {
        v[i] = (maudVector3){(i & 1) ? 5.0f : 0.0f, (i & 2) ? 3.0f : 0.0f, (i & 4) ? 4.0f : 0.0f};
    }
    maudMesh mesh = {v, 8, faces, materials, 12};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "an office");
    return scene;
}

static void* Reverse(maudTaskFn* task, uint32_t itemCount, uint32_t minRange, void* taskContext,
                     void* userContext)
{
    (void)minRange;
    for (uint32_t i = itemCount; i-- > 0;)
    {
        task(i, i + 1, taskContext);
    }
    return userContext;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
}

static maudSpatializer* Create(maudAcousticScene* scene, bool reversed, uint32_t order)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudSceneAnyHit;
    def.closestHit = maudSceneClosestHit;
    def.rayContext = scene;
    def.reverbRays = 1024;
    def.probeSetCapacity = 1;
    def.reflectionOrder = order;
    def.reflectionDuration = 0.2f;
    def.enqueueTask = reversed ? Reverse : nullptr;
    def.finishTask = reversed ? Finish : nullptr;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudAcousticMaterial m[6];
    for (int i = 0; i < 6; ++i)
    {
        m[i] = (maudAcousticMaterial){
            {0.04f + 0.03f * (float)i, 0.06f + 0.04f * (float)i, 0.1f + 0.05f * (float)i},
            0.3f,
            {0.0f, 0.0f, 0.0f}};
    }
    CHECK(maudSetMaterials(s, m, 6) == maud_success, "materials");
    return s;
}

// Probes on a 1 m grid at 1.5 m, baked and in use.
static maudProbeSetId Bake(maudSpatializer* s)
{
    static maudVector3 points[20];
    uint32_t n = 0;
    for (int x = 0; x < 5; ++x)
    {
        for (int z = 0; z < 4; ++z)
        {
            points[n++] = (maudVector3){(float)x + 0.5f, 1.5f, (float)z + 0.5f};
        }
    }
    maudProbeSetDef def = maudDefaultProbeSetDef();
    def.points = points;
    def.pointCount = n;
    def.range = 3.0f;
    maudProbeSetId set = {0, 0};
    maudProbeSetInfo info = {0};
    CHECK(maudCreateProbeSet(s, &def, &set) == maud_success &&
              maudGetProbeSet(s, set, &info, 0, 0, nullptr) == maud_success && !info.baked,
          "a set, not yet baked");
    CHECK(maudUseBakedReverb(s, set) == maud_errorState, "no bake to use");
    CHECK(maudBakeProbeSet(s, set) == maud_success &&
              maudGetProbeSet(s, set, &info, 0, 0, nullptr) == maud_success && info.baked &&
              maudUseBakedReverb(s, set) == maud_success,
          "baked and in use");
    return set;
}

static maudReverbResult At(maudSpatializer* s, float x, float y, float z)
{
    maudPose pose = {{x, y, z}, {0.0f, 0.0f, 0.0f, 1.0f}};
    maudReverbResult r = {0};
    CHECK(maudSimulateReverb(s, &pose) == maud_success &&
              maudSimulateDirect(s, &pose) == maud_success,
          "a step");
    maudLatchResults(s);
    CHECK(maudGetReverbResult(s, &r) == maud_success, "a result");
    return r;
}

static bool Same(const maudReverbResult* a, const maudReverbResult* b)
{
    return memcmp(a->reverbTime, b->reverbTime, sizeof(a->reverbTime)) == 0 &&
           memcmp(a->level, b->level, sizeof(a->level)) == 0 && a->delay == b->delay;
}

static void TestReverb(maudAcousticScene* scene)
{
    maudSpatializer* baked = Create(scene, false, 0);
    maudSpatializer* live = Create(scene, false, 0);
    maudSpatializer* reversed = Create(scene, true, 0);
    maudProbeSetId set = Bake(baked);
    (void)Bake(reversed);
    maudReverbResult a = At(baked, 2.5f, 1.5f, 1.5f);
    maudReverbResult b = At(live, 2.5f, 1.5f, 1.5f);
    CHECK(Same(&a, &b), "on a probe, the live estimate's bits");
    double worstTime = 0.0;
    double worstLevel = 0.0;
    const float between[4][3] = {
        {1.0f, 1.5f, 1.0f}, {2.2f, 1.6f, 2.7f}, {3.7f, 1.4f, 1.9f}, {4.1f, 1.5f, 3.2f}};
    bool same = true;
    for (int i = 0; i < 4; ++i)
    {
        a = At(baked, between[i][0], between[i][1], between[i][2]);
        b = At(live, between[i][0], between[i][1], between[i][2]);
        maudReverbResult c = At(reversed, between[i][0], between[i][1], between[i][2]);
        same = same && Same(&a, &c);
        for (int band = 0; band < 3; ++band)
        {
            worstTime = fmax(worstTime,
                             fabs((double)a.reverbTime[band] / (double)b.reverbTime[band] - 1.0));
            worstLevel = fmax(worstLevel, fabs((double)a.level[band] - (double)b.level[band]));
        }
    }
    printf("between probes: times within %.2f %%, levels within %.3f dB\n", 100.0 * worstTime,
           worstLevel);
    CHECK(worstTime < 0.03 && worstLevel < 1.5, "close to live between probes");
    CHECK(same, "the same bits however the tasks run");
    // A walk in 2 cm steps across two probes' midway.
    double jump = 0.0;
    maudReverbResult last = At(baked, 1.2f, 1.5f, 1.3f);
    for (int k = 1; k <= 60; ++k)
    {
        maudReverbResult now = At(baked, 1.2f + 0.02f * (float)k, 1.5f, 1.3f);
        for (int band = 0; band < 3; ++band)
        {
            jump = fmax(jump, fabs((double)now.level[band] - (double)last.level[band]));
        }
        last = now;
    }
    printf("walking: the largest step %.3f dB\n", jump);
    CHECK(jump < 0.2, "no jumps walking");
    // Outside the office no probe is in sight: it traces.
    a = At(baked, 7.0f, 1.5f, 2.0f);
    b = At(live, 7.0f, 1.5f, 2.0f);
    CHECK(Same(&a, &b), "with no probe in sight, a trace");
    CHECK(maudDestroyProbeSet(baked, set) == maud_success &&
              maudUseBakedReverb(baked, set) == maud_errorStale,
          "a destroyed set's id");
    maudDestroySpatializer(reversed);
    maudDestroySpatializer(live);
    maudDestroySpatializer(baked);
}

static float s_send[FRAMES];
static float s_bed[2][4][FRAMES];

static void Render(maudSpatializer* s, int which)
{
    memset(s_send, 0, sizeof(s_send));
    memset(s_bed[which], 0, sizeof(s_bed[which]));
    s_send[0] = 1.0f;
    const maudQuaternion upright = {0.0f, 0.0f, 0.0f, 1.0f};
    for (int at = 0; at < FRAMES; at += 480)
    {
        float* bed[4] = {s_bed[which][0] + at, s_bed[which][1] + at, s_bed[which][2] + at,
                         s_bed[which][3] + at};
        CHECK(maudRenderReflections(s, &upright, s_send + at, bed, 480) == maud_success, "render");
    }
}

static void TestReflections(maudAcousticScene* scene)
{
    maudSpatializer* baked = Create(scene, false, 1);
    maudSpatializer* live = Create(scene, false, 1);
    (void)Bake(baked);
    maudReverbResult a = At(baked, 3.5f, 1.5f, 2.5f);
    maudReverbResult b = At(live, 3.5f, 1.5f, 2.5f);
    Render(baked, 0);
    Render(live, 1);
    double energy = 0.0;
    for (int i = 0; i < FRAMES; ++i)
    {
        energy += (double)s_bed[0][0][i] * (double)s_bed[0][0][i];
    }
    CHECK(Same(&a, &b) && energy > 0.0 && memcmp(s_bed[0], s_bed[1], sizeof(s_bed[0])) == 0,
          "on a probe, the live reflections' samples");
    maudDestroySpatializer(live);
    maudDestroySpatializer(baked);
}

static void TestRefused(maudAcousticScene* scene)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudSceneAnyHit;
    def.rayContext = scene;
    def.probeSetCapacity = 1;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "no closest-hit query");
    const maudVector3 point = {1.0f, 1.5f, 1.0f};
    maudProbeSetDef pd = maudDefaultProbeSetDef();
    pd.points = &point;
    pd.pointCount = 1;
    maudProbeSetId set = {0, 0};
    CHECK(maudCreateProbeSet(s, &pd, &set) == maud_success &&
              maudBakeProbeSet(s, set) == maud_errorState,
          "baking without a closest-hit query");
    maudDestroySpatializer(s);
    def.closestHit = maudSceneClosestHit;
    def.reverbRays = 0;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success &&
              maudCreateProbeSet(s, &pd, &set) == maud_success &&
              maudBakeProbeSet(s, set) == maud_errorState,
          "baking without rays");
    const maudProbeSetId none = {0, 0};
    CHECK(maudBakeProbeSet(s, none) == maud_errorInvalid &&
              maudBakeProbeSet(nullptr, set) == maud_errorInvalid &&
              maudUseBakedReverb(s, none) == maud_success,
          "0, NULL, and none in use");
    maudDestroySpatializer(s);
}

static uint32_t s_closest;

static void Counted(const maudRay* rays, uint32_t count, maudRayHit* hits, void* context)
{
    s_closest += count;
    maudSceneClosestHit(rays, count, hits, context);
}

// A baked estimate traces nothing; without the set in use it traces.
static void TestTraces(maudAcousticScene* scene)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudSceneAnyHit;
    def.closestHit = Counted;
    def.rayContext = scene;
    def.reverbRays = 1024;
    def.probeSetCapacity = 1;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a counting spatializer");
    maudProbeSetId set = Bake(s);
    s_closest = 0;
    (void)At(s, 2.2f, 1.6f, 2.7f);
    uint32_t baked = s_closest;
    const maudProbeSetId none = {0, 0};
    CHECK(maudUseBakedReverb(s, none) == maud_success, "the set out of use");
    (void)At(s, 2.2f, 1.6f, 2.7f);
    printf("closest-hit rays: %u baked, %u traced\n", baked, s_closest - baked);
    CHECK(baked == 0 && s_closest > 0, "a baked estimate traces nothing");
    (void)set;
    maudDestroySpatializer(s);
}

static uint8_t* Save(const maudSpatializer* s, maudProbeSetId set, size_t* size)
{
    CHECK(maudSaveProbeSet(s, set, nullptr, 0, size) == maud_success && *size > 40, "a size");
    uint8_t* bytes = malloc(*size);
    size_t short_ = 0;
    CHECK(maudSaveProbeSet(s, set, bytes, *size - 1, &short_) == maud_errorCapacity &&
              short_ == *size,
          "a buffer a byte short");
    size_t written = 0;
    CHECK(maudSaveProbeSet(s, set, bytes, *size, &written) == maud_success && written == *size,
          "saved");
    return bytes;
}

static void TestFile(maudAcousticScene* scene)
{
    maudSpatializer* baked = Create(scene, false, 1);
    maudProbeSetId set = Bake(baked);
    size_t size = 0;
    uint8_t* bytes = Save(baked, set, &size);
    maudSpatializer* loaded = Create(scene, false, 1);
    maudProbeSetId again = {0, 0};
    CHECK(maudLoadProbeSet(loaded, bytes, size, &again) == maud_success &&
              maudUseBakedReverb(loaded, again) == maud_success,
          "loaded and in use");
    bool same = true;
    const float at[3][3] = {{1.0f, 1.5f, 1.0f}, {2.5f, 1.5f, 1.5f}, {3.7f, 1.4f, 1.9f}};
    for (int i = 0; i < 3; ++i)
    {
        maudReverbResult a = At(baked, at[i][0], at[i][1], at[i][2]);
        maudReverbResult b = At(loaded, at[i][0], at[i][1], at[i][2]);
        same = same && Same(&a, &b);
    }
    CHECK(same, "the same estimates from the loaded set");
    size_t size2 = 0;
    uint8_t* bytes2 = Save(loaded, again, &size2);
    CHECK(size2 == size && memcmp(bytes, bytes2, size) == 0, "saving it again, the same bytes");
    maudSpatializer* plain = Create(scene, false, 0);
    maudProbeSetId refused = {1, 1};
    CHECK(maudLoadProbeSet(plain, bytes, size, &refused) == maud_errorUnsupported &&
              refused.index1 == 0,
          "fields where there are no reflections");
    bytes[size - 1] ^= 1;
    CHECK(maudLoadProbeSet(loaded, bytes, size, &refused) == maud_errorInvalid, "a bad checksum");
    maudProbeSetDef def = maudDefaultProbeSetDef();
    const maudVector3 point = {1.0f, 1.5f, 1.0f};
    def.points = &point;
    def.pointCount = 1;
    maudProbeSetId bare = {0, 0};
    CHECK(maudCreateProbeSet(plain, &def, &bare) == maud_success, "an unbaked set");
    size_t bareSize = 0;
    uint8_t* bareBytes = Save(plain, bare, &bareSize);
    CHECK(maudDestroyProbeSet(loaded, again) == maud_success &&
              maudLoadProbeSet(loaded, bareBytes, bareSize, &again) == maud_success,
          "probes and links alone load anywhere");
    CHECK(maudLoadProbeSet(nullptr, bytes, size, &again) == maud_errorInvalid &&
              maudSaveProbeSet(plain, bare, nullptr, 0, nullptr) == maud_errorInvalid,
          "NULL");
    free(bareBytes);
    free(bytes2);
    free(bytes);
    maudDestroySpatializer(plain);
    maudDestroySpatializer(loaded);
    maudDestroySpatializer(baked);
}

// The office's bake with reflections of order 1 over 0.05 s, against
// the shipped file.
static void TestGolden(maudAcousticScene* scene)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = maudSceneAnyHit;
    def.closestHit = maudSceneClosestHit;
    def.rayContext = scene;
    def.reverbRays = 1024;
    def.probeSetCapacity = 1;
    def.reflectionOrder = 1;
    def.reflectionDuration = 0.05f;
    maudSpatializer* s = nullptr;
    CHECK(maudCreateSpatializer(&def, &s) == maud_success, "a spatializer");
    maudAcousticMaterial m[6];
    for (int i = 0; i < 6; ++i)
    {
        m[i] = (maudAcousticMaterial){
            {0.04f + 0.03f * (float)i, 0.06f + 0.04f * (float)i, 0.1f + 0.05f * (float)i},
            0.3f,
            {0.0f, 0.0f, 0.0f}};
    }
    CHECK(maudSetMaterials(s, m, 6) == maud_success, "materials");
    maudProbeSetId set = Bake(s);
    size_t size = 0;
    uint8_t* bytes = Save(s, set, &size);
    // Its bytes' hash, which needs no file system (the web has none).
    uint64_t hash = 1469598103934665603u;
    for (size_t i = 0; i < size; ++i)
    {
        hash = (hash ^ bytes[i]) * 1099511628211u;
    }
    printf("golden: %zu bytes, hash %016llx\n", size, (unsigned long long)hash);
    CHECK(hash == 0x3c3cd93cdb68581cu, "the shipped bake's hash");
#ifndef __EMSCRIPTEN__
    const char* path = MAUD_DATA_DIR "/bake/office.maudbake";
    if (getenv("MAUD_WRITE_GOLDEN") != nullptr)
    {
        FILE* out = fopen(path, "wb");
        CHECK(out != nullptr && fwrite(bytes, 1, size, out) == size && fclose(out) == 0,
              "the golden file written");
    }
    FILE* in = fopen(path, "rb");
    uint8_t* golden = malloc(size + 1);
    size_t read = in != nullptr ? fread(golden, 1, size + 1, in) : 0;
    if (in != nullptr)
    {
        fclose(in);
    }
    CHECK(read == size && memcmp(golden, bytes, size) == 0, "the shipped bake, byte for byte");
    free(golden);
#endif
    free(bytes);
    maudDestroySpatializer(s);
}

int main(void)
{
    maudAcousticScene* scene = Office();
    TestReverb(scene);
    TestReflections(scene);
    TestRefused(scene);
    TestTraces(scene);
    TestFile(scene);
    TestGolden(scene);
    maudDestroyAcousticScene(scene);
    return s_failures == 0 ? 0 : 1;
}
