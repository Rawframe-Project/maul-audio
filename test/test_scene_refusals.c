// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every refusal scene.h documents, each made on its own: a def with one
// field or mesh out of range (and nothing else wrong) is maud_errorLimit
// past a published bound and maud_errorInvalid otherwise, with no scene
// made, triangles past the def's limit are
// maud_errorCapacity, and the limits at their bounds are taken; an
// instance's transform out of range, a placement past the float range,
// its ids out of range and the pointers each function needs are refused.

#include "test_harness.h"

#include "maul-audio/scene.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>

static const uint32_t s_faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                     2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
static const uint32_t s_materials[12] = {0};
static maudVector3 s_v[8];
static uint32_t s_badFaces[36];

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    return malloc(size);
}

// A def of one 12-triangle box and room for one instance of it.
static maudSceneDef Valid(maudMesh* mesh, maudMesh* instanced)
{
    for (int i = 0; i < 8; ++i)
    {
        s_v[i] = (maudVector3){(i & 1) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f, (i & 4) ? 1.0f : 0.0f};
    }
    *mesh = (maudMesh){s_v, 8, s_faces, s_materials, 12};
    *instanced = *mesh;
    maudSceneDef def = maudDefaultSceneDef();
    def.meshes = mesh;
    def.meshCount = 1;
    def.instanceMeshes = instanced;
    def.instanceMeshCount = 1;
    def.instanceCapacity = 1;
    return def;
}

// Puts case k out of range; false past the last case.
static bool Spoil(maudSceneDef* d, maudMesh* mesh, maudMesh* instanced, int k, const char** what,
                  maudResult* want)
{
    *want = maud_errorInvalid;
    switch (k)
    {
    case 0:
        d->cookie = 0;
        *what = "no cookie";
        return true;
    case 1:
        d->allocator = (maudAllocator){Alloc, nullptr, nullptr};
        *what = "an allocator without free";
        return true;
    case 2:
        d->instanceCapacity = 65537;
        *what = "65,537 instances";
        *want = maud_errorLimit;
        return true;
    case 3:
        d->maxTriangles = 0;
        *what = "no triangles allowed";
        return true;
    case 4:
        d->maxTriangles = 16777217;
        *what = "16,777,217 triangles allowed";
        *want = maud_errorLimit;
        return true;
    case 5:
        d->meshes = nullptr;
        *what = "a mesh count without meshes";
        return true;
    case 6:
        d->instanceMeshes = nullptr;
        *what = "an instance mesh count without meshes";
        return true;
    case 7:
        mesh->vertices = nullptr;
        *what = "a mesh without vertices";
        return true;
    case 8:
        mesh->indices = nullptr;
        *what = "a mesh without indices";
        return true;
    case 9:
        mesh->materials = nullptr;
        *what = "a mesh without materials";
        return true;
    case 10:
        s_v[3].y = NAN;
        *what = "a vertex not a number";
        return true;
    case 11:
        s_v[5].z = INFINITY;
        *what = "an infinite vertex";
        return true;
    case 12:
        for (int i = 0; i < 36; ++i)
        {
            s_badFaces[i] = s_faces[i];
        }
        s_badFaces[35] = 8;
        mesh->indices = s_badFaces;
        *what = "an index past the vertices";
        return true;
    case 13:
        instanced->vertices = nullptr;
        *what = "an instance mesh without vertices";
        return true;
    default:
        return false;
    }
}

static void TestDefs(void)
{
    maudMesh mesh;
    maudMesh instanced;
    maudSceneDef def = Valid(&mesh, &instanced);
    maudScene* scene = (maudScene*)&scene;
    CHECK(maudCreateScene(nullptr, &scene) == maud_errorInvalid && scene == nullptr, "no def");
    CHECK(maudCreateScene(&def, nullptr) == maud_errorInvalid, "nowhere to put it");
    CHECK(maudCreateScene(&def, &scene) == maud_success, "the valid def");
    maudDestroyScene(scene);
    const char* what = "";
    for (int k = 0;; ++k)
    {
        def = Valid(&mesh, &instanced);
        maudResult want = maud_errorInvalid;
        if (!Spoil(&def, &mesh, &instanced, k, &what, &want))
        {
            break;
        }
        scene = (maudScene*)&scene;
        bool refused = maudCreateScene(&def, &scene) == want && scene == nullptr;
        if (!refused)
        {
            printf("not refused: %s\n", what);
            maudDestroyScene(scene);
        }
        CHECK(refused, "a def out of range refused");
    }
    def = Valid(&mesh, &instanced);
    def.maxTriangles = 23;
    CHECK(maudCreateScene(&def, &scene) == maud_errorCapacity && scene == nullptr,
          "24 triangles past a limit of 23");
    def.maxTriangles = 24;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "24 at a limit of 24");
    maudDestroyScene(scene);
    def = Valid(&mesh, &instanced);
    mesh.vertices = nullptr;
    mesh.indices = nullptr;
    mesh.materials = nullptr;
    mesh.triangleCount = 0;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "a mesh of no triangles needs no arrays");
    maudDestroyScene(scene);
}

static void TestInstances(void)
{
    maudMesh mesh;
    maudMesh instanced;
    maudSceneDef def = Valid(&mesh, &instanced);
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "a scene");
    maudSceneInstanceTransform t = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 1.0f};
    maudSceneInstanceId id = {7, 7};
    CHECK(maudCreateSceneInstance(nullptr, 0, &t, &id) == maud_errorInvalid && id.index1 == 0,
          "an instance without a scene");
    CHECK(maudCreateSceneInstance(scene, 0, nullptr, &id) == maud_errorInvalid,
          "without a transform");
    CHECK(maudCreateSceneInstance(scene, 0, &t, nullptr) == maud_errorInvalid, "nowhere to put it");
    maudSceneInstanceTransform bad[6] = {t, t, t, t, t, t};
    bad[0].position.x = NAN;
    bad[1].position.z = INFINITY;
    bad[2].orientation = (maudQuaternion){0.0f, 0.0f, 0.0f, 0.0f};
    bad[3].orientation.w = INFINITY;
    bad[4].scale = 0.00099f;
    bad[5].scale = 1000.5f;
    bool refused = true;
    for (int i = 0; i < 6; ++i)
    {
        refused = refused && maudCreateSceneInstance(scene, 0, &bad[i], &id) == maud_errorInvalid;
    }
    CHECK(refused, "a transform not finite, without a turn, or scaled out of range");
    maudSceneInstanceTransform bounds[2] = {t, t};
    bounds[0].scale = 0.001f;
    bounds[1].scale = 1000.0f;
    maudSceneInstanceId small = {0, 0};
    CHECK(maudCreateSceneInstance(scene, 0, &bounds[0], &small) == maud_success &&
              maudDestroySceneInstance(scene, small) == maud_success &&
              maudCreateSceneInstance(scene, 0, &bounds[1], &id) == maud_success,
          "the smallest and largest scales taken");
    maudSceneInstanceId zero = {0, 1};
    maudSceneInstanceId past = {2, 1};
    maudSceneInstanceId unborn = {1, 0};
    CHECK(maudSceneInstance_Move(nullptr, id, &t) == maud_errorInvalid &&
              maudSceneInstance_Move(scene, zero, &t) == maud_errorInvalid &&
              maudSceneInstance_Move(scene, past, &t) == maud_errorInvalid &&
              maudSceneInstance_Move(scene, unborn, &t) == maud_errorInvalid,
          "a move for no scene, index 0, past the capacity or generation 0");
    CHECK(maudSceneInstance_Move(scene, id, nullptr) == maud_errorInvalid &&
              maudSceneInstance_Move(scene, id, &bad[2]) == maud_errorInvalid,
          "a move without a transform or to one out of range");
    CHECK(maudSceneInstance_Move(scene, small, &t) == maud_errorStale, "a destroyed one's id");
    CHECK(maudDestroySceneInstance(nullptr, id) == maud_errorInvalid &&
              maudDestroySceneInstance(scene, zero) == maud_errorInvalid,
          "destroying none");
    CHECK(maudScene_Commit(nullptr) == maud_errorInvalid, "a commit without a scene");
    maudDestroyScene(scene);
}

// The cube from low to high on each axis, into v.
static void Cube(maudVector3* v, float low, float high)
{
    for (int i = 0; i < 8; ++i)
    {
        v[i] = (maudVector3){(i & 1) ? high : low, (i & 2) ? high : low, (i & 4) ? high : low};
    }
}

// Finite vertices and a transform in range can still place a mesh past
// the float range: one far out and scaled up, or one whose corners are
// so far apart that their distance overflows. Either is refused, where
// it is made or moved to, and a commit after takes the last placement, as
// a ray against it shows.
static void TestInstancesPastRange(void)
{
    static maudVector3 far[8];
    static maudVector3 wide[8];
    static maudVector3 near[8];
    Cube(far, 0.0f, 1e36f);
    Cube(wide, -2e38f, 2e38f);
    Cube(near, 0.0f, 1e30f);
    maudMesh meshes[3] = {
        {far, 8, s_faces, s_materials, 12},
        {wide, 8, s_faces, s_materials, 12},
        {near, 8, s_faces, s_materials, 12},
    };
    maudSceneDef def = maudDefaultSceneDef();
    def.instanceMeshes = meshes;
    def.instanceMeshCount = 3;
    def.instanceCapacity = 1;
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "a scene");
    maudSceneInstanceTransform up = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f}, 1000.0f};
    maudSceneInstanceTransform one = up;
    one.scale = 1.0f;
    maudSceneInstanceId id = {0, 0};
    CHECK(maudCreateSceneInstance(scene, 0, &up, &id) == maud_errorInvalid && id.index1 == 0,
          "a mesh scaled past the float range refused");
    CHECK(maudCreateSceneInstance(scene, 1, &one, &id) == maud_errorInvalid && id.index1 == 0,
          "a mesh whose corners are farther apart than a float holds refused");
    CHECK(maudCreateSceneInstance(scene, 2, &up, &id) == maud_success, "a mesh in range taken");
    maudSceneInstanceTransform out = up;
    out.position.x = FLT_MAX;
    CHECK(maudSceneInstance_Move(scene, id, &out) == maud_errorInvalid,
          "a move past the float range refused");
    CHECK(maudScene_Commit(scene) == maud_success, "a commit of the placement in range");
    // The instance stays where it was: its cube, 1e33 on a side from the
    // origin, lies 1e33 below a ray from 2e33 straight down.
    maudRay down = {{5e32f, 2e33f, 5e32f}, {0.0f, -1.0f, 0.0f}, 0.0f, 3e33f};
    maudRayHit hit;
    maudFindClosestHit(&down, 1, &hit, scene);
    CHECK(fabsf(hit.distance - 1e33f) <= 1e30f, "the refused move left the instance where it was");
    maudDestroyScene(scene);
}

// A mesh near the float range, turned and scaled down, lies well inside
// it, and is taken where it is made or moved to: its corners, turned
// first and scaled after, would sum past the range.
static void TestInstancesTurnedNearRange(void)
{
    static maudVector3 edge[8];
    for (int i = 0; i < 8; ++i)
    {
        edge[i] = (maudVector3){(i & 1) ? 2.5e38f : 2.4e38f, (i & 2) ? 2.5e38f : 2.4e38f,
                                (i & 4) ? 1e37f : -1e37f};
    }
    maudMesh mesh = {edge, 8, s_faces, s_materials, 12};
    maudSceneDef def = maudDefaultSceneDef();
    def.instanceMeshes = &mesh;
    def.instanceMeshCount = 1;
    def.instanceCapacity = 1;
    maudScene* scene = nullptr;
    CHECK(maudCreateScene(&def, &scene) == maud_success, "a scene");
    // 45 degrees about z: the corner at (2.5e38, 2.5e38) turns to
    // y = 3.5e38, which a scale of 0.001 brings to 3.5e35.
    maudSceneInstanceTransform turned = {
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.38268343f, 0.92387953f}, 0.001f};
    maudSceneInstanceId id = {0, 0};
    CHECK(maudCreateSceneInstance(scene, 0, &turned, &id) == maud_success,
          "a turned mesh near the float range, scaled into it, taken");
    maudSceneInstanceTransform moved = turned;
    moved.position.z = 1e30f;
    CHECK(maudSceneInstance_Move(scene, id, &moved) == maud_success, "and moved");
    CHECK(maudScene_Commit(scene) == maud_success, "a commit of it");
    maudDestroyScene(scene);
}

int main(void)
{
    TestDefs();
    TestInstances();
    TestInstancesPastRange();
    TestInstancesTurnedNearRange();
    return s_failures == 0 ? 0 : 1;
}
