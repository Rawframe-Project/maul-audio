// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pathing through the spatializer, in an L-shaped corridor whose inside
// corner is an opaque block: a source around the corner is pathed, its
// distance the path's length through the corner within 0.1 %, its
// direction the corner's within a degree, its transmission the
// diffraction of the corner's 72.8 degree turn in every band; a source
// that does not ask, one in plain sight and one past maxPaths are not;
// the same results when the tasks run one at a time in reverse. A block
// letting a quarter through (two faces at a half): the transmission is
// the energy sum of the corner's diffraction and what passes, raised by
// the straight line's shorter spread, and the direction blends the two
// arrivals by their energy. A wall closing the corridor after the set
// was made: its links are found blocked and no path is left. A
// destroyed set's id is stale and its paths stop.

#include "test_harness.h"

#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <math.h>
#include <string.h>

// The scene the rays see, switched under a spatializer by the tests.
static maudAcousticScene* s_current;

static void AnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded, void* context)
{
    (void)context;
    maudSceneAnyHit(rays, count, occluded, s_current);
}

static void ClosestHit(const maudRay* rays, uint32_t count, maudRayHit* hits, void* context)
{
    (void)context;
    maudSceneClosestHit(rays, count, hits, s_current);
}

// The corridor, closed across leg A at x = 10 if asked.
static maudAcousticScene* Corridor(bool closed)
{
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const uint32_t materials[24] = {0};
    uint32_t indices[72];
    maudVector3 v[16];
    const maudVector3 low[2] = {{0.0f, 0.0f, 3.0f}, {9.9f, 0.0f, -1.0f}};
    const maudVector3 high[2] = {{17.0f, 3.0f, 20.0f}, {10.1f, 3.0f, 3.5f}};
    for (uint32_t b = 0; b < 2; ++b)
    {
        for (uint32_t i = 0; i < 8; ++i)
        {
            v[b * 8 + i] =
                (maudVector3){(i & 1) ? high[b].x : low[b].x, (i & 2) ? high[b].y : low[b].y,
                              (i & 4) ? high[b].z : low[b].z};
        }
        for (uint32_t k = 0; k < 36; ++k)
        {
            indices[b * 36 + k] = b * 8 + faces[k];
        }
    }
    maudMesh mesh = {v, closed ? 16u : 8u, indices, materials, closed ? 24u : 12u};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "a corridor");
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

typedef struct World
{
    maudSpatializer* s;
    maudProbeSetId set;
    maudSourceId sources[4];
} World;

static maudPose s_listener = {{18.5f, 1.5f, 12.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};

// Sources: around the corner; the same not asking; in sight; around
// the corner again, past maxPaths.
static World Build(bool reversed, float transmission)
{
    maudSpatializerDef def = maudDefaultSpatializerDef();
    def.anyHit = AnyHit;
    def.closestHit = ClosestHit;
    def.probeSetCapacity = 1;
    def.maxPaths = 1;
    def.enqueueTask = reversed ? Reverse : nullptr;
    def.finishTask = reversed ? Finish : nullptr;
    World w = {0};
    CHECK(maudCreateSpatializer(&def, &w.s) == maud_success, "a spatializer");
    maudAcousticMaterial wall = {
        {0.1f, 0.1f, 0.1f}, 0.5f, {transmission, transmission, transmission}};
    CHECK(maudSetMaterials(w.s, &wall, 1) == maud_success, "materials");
    static maudVector3 points[400];
    uint32_t count = 0;
    for (int x = 0; x < 20; ++x)
    {
        for (int z = 0; z < 20; ++z)
        {
            if (z < 3 || x >= 17)
            {
                points[count++] = (maudVector3){(float)x + 0.5f, 1.5f, (float)z + 0.5f};
            }
        }
    }
    maudProbeSetDef pd = maudDefaultProbeSetDef();
    pd.points = points;
    pd.pointCount = count;
    pd.range = 3.0f;
    CHECK(maudCreateProbeSet(w.s, &pd, &w.set) == maud_success &&
              maudSetPathing(w.s, w.set) == maud_success,
          "a set in use");
    const maudVector3 at[4] = {
        {6.0f, 1.5f, 1.5f}, {6.0f, 1.5f, 1.5f}, {18.5f, 1.5f, 5.0f}, {8.0f, 1.5f, 1.0f}};
    // The one not asking takes the first slot, so that were it pathed it
    // would take the only path a step allows.
    const int order[4] = {1, 0, 2, 3};
    for (int n = 0; n < 4; ++n)
    {
        int i = order[n];
        maudSourceDef sd = maudDefaultSourceDef();
        sd.pathing = i != 1;
        maudPose pose = {at[i], {0.0f, 0.0f, 0.0f, 1.0f}};
        CHECK(maudCreateSource(w.s, &sd, &w.sources[i]) == maud_success &&
                  maudSetSourcePose(w.s, w.sources[i], &pose) == maud_success,
              "a source");
    }
    return w;
}

static void Step(const World* w, maudDirectResult* results)
{
    CHECK(maudSimulateDirect(w->s, &s_listener) == maud_success && maudLatchResults(w->s) > 0,
          "a step");
    for (int i = 0; i < 4; ++i)
    {
        CHECK(maudGetDirectResult(w->s, w->sources[i], &results[i]) == maud_success, "a result");
    }
}

static double Length(maudVector3 a, maudVector3 b)
{
    double x = (double)b.x - (double)a.x;
    double y = (double)b.y - (double)a.y;
    double z = (double)b.z - (double)a.z;
    return sqrt(x * x + y * y + z * z);
}

static void CheckCorner(const maudDirectResult* r)
{
    const maudVector3 corner = {17.0f, 1.5f, 3.0f};
    const maudVector3 source = {6.0f, 1.5f, 1.5f};
    double exact = Length(s_listener.position, corner) + Length(corner, source);
    double toCorner = Length(s_listener.position, corner);
    double cosine =
        ((double)r->direction.x * (17.0 - 18.5) + (double)r->direction.z * (3.0 - 12.0)) / toCorner;
    double degrees = acos(fmin(1.0, cosine)) * 180.0 / 3.14159265358979323846;
    printf("around the corner: %.4f m (exact %.4f), %.3f degrees off, %.1f / %.1f / %.1f dB\n",
           (double)r->distance, exact, degrees, 20.0 * log10((double)r->transmission[0]),
           20.0 * log10((double)r->transmission[1]), 20.0 * log10((double)r->transmission[2]));
    CHECK(r->pathed && r->occlusion == 1.0f, "pathed");
    CHECK(fabs((double)r->distance / exact - 1.0) < 1e-3, "the path's length");
    CHECK(degrees < 1.0, "arriving from the corner");
    // A turn of 72.8 degrees: between the table's 60 and 90.
    const double low[3] = {-4.1, -11.3, -16.4};
    const double high[3] = {-2.9, -8.7, -13.4};
    bool diffracted = true;
    for (int b = 0; b < 3; ++b)
    {
        double db = 20.0 * log10((double)r->transmission[b]);
        diffracted = diffracted && db > low[b] && db < high[b];
    }
    CHECK(diffracted, "the corner's diffraction");
}

static maudVector3 Unit(maudVector3 v)
{
    float n = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    return (maudVector3){v.x / n, v.y / n, v.z / n};
}

// Through a block letting half through each face, against the opaque
// run's diffraction and arrival.
static void CheckThrough(const maudDirectResult* opaque)
{
    World w = Build(false, 0.5f);
    maudDirectResult r[4];
    Step(&w, r);
    const maudVector3 source = {6.0f, 1.5f, 1.5f};
    maudVector3 toSource = {source.x - s_listener.position.x, source.y - s_listener.position.y,
                            source.z - s_listener.position.z};
    float straight =
        sqrtf(toSource.x * toSource.x + toSource.y * toSource.y + toSource.z * toSource.z);
    float through = 0.25f * opaque->distance / straight;
    bool summed = r[0].pathed;
    for (int b = 0; b < 3; ++b)
    {
        float want = fminf(
            1.0f, sqrtf(opaque->transmission[b] * opaque->transmission[b] + through * through));
        summed = summed && fabsf(r[0].transmission[b] - want) < 1e-4f;
    }
    float ws = through * through;
    float wp = opaque->transmission[1] * opaque->transmission[1];
    maudVector3 s = Unit(toSource);
    maudVector3 a = opaque->direction;
    maudVector3 want =
        Unit((maudVector3){ws * s.x + wp * a.x, ws * s.y + wp * a.y, ws * s.z + wp * a.z});
    float off = fabsf(r[0].direction.x - want.x) + fabsf(r[0].direction.y - want.y) +
                fabsf(r[0].direction.z - want.z);
    printf("half through: %.3f / %.3f / %.3f, direction %.6f off\n", (double)r[0].transmission[0],
           (double)r[0].transmission[1], (double)r[0].transmission[2], (double)off);
    CHECK(summed, "around and through summed in energy");
    CHECK(off < 1e-4f, "the arrivals blended by energy");
    maudDestroySpatializer(w.s);
}

int main(void)
{
    maudAcousticScene* scene = Corridor(false);
    maudAcousticScene* closed = Corridor(true);
    s_current = scene;
    World w = Build(false, 0.0f);
    maudDirectResult r[4];
    Step(&w, r);
    CheckCorner(&r[0]);
    CHECK(!r[1].pathed && r[1].transmission[1] == 0.0f, "not asking, not pathed");
    CHECK(!r[2].pathed && r[2].occlusion == 0.0f, "in sight, not pathed");
    CHECK(!r[3].pathed && r[3].occlusion == 1.0f, "past maxPaths, not pathed");
    // Turned a quarter to the left about +y: the world's (X, Y, Z) is the
    // listener's (-Z, Y, X).
    s_listener.orientation = (maudQuaternion){0.0f, 0.70710678f, 0.0f, 0.70710678f};
    maudDirectResult turned[4];
    Step(&w, turned);
    s_listener.orientation = (maudQuaternion){0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(fabsf(turned[0].direction.x + r[0].direction.z) < 1e-4f &&
              fabsf(turned[0].direction.z - r[0].direction.x) < 1e-4f,
          "arriving in the listener's frame");
    CheckThrough(&r[0]);
    World other = Build(true, 0.0f);
    maudDirectResult again[4];
    Step(&other, again);
    CHECK(memcmp(&again[0], &r[0], sizeof(maudDirectResult)) == 0, "the same however tasks run");
    maudDestroySpatializer(other.s);
    // The corridor closed after the set was made.
    s_current = closed;
    Step(&w, r);
    CHECK(!r[0].pathed, "a closed corridor, no path");
    s_current = scene;
    // A destroyed set: its id stale, its paths gone.
    CHECK(maudDestroyProbeSet(w.s, w.set) == maud_success, "destroyed");
    CHECK(maudSetPathing(w.s, w.set) == maud_errorStale, "a destroyed set's id is stale");
    Step(&w, r);
    CHECK(!r[0].pathed, "no set, no path");
    const maudProbeSetId none = {0, 0};
    CHECK(maudSetPathing(w.s, none) == maud_success &&
              maudSetPathing(nullptr, none) == maud_errorInvalid,
          "pathing off; NULL");
    maudDestroySpatializer(w.s);
    maudDestroyAcousticScene(closed);
    maudDestroyAcousticScene(scene);
    return s_failures == 0 ? 0 : 1;
}
