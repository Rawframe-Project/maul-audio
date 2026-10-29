// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every refusal scene.h documents, each made on its own: a def with one
// field or mesh out of range (and nothing else wrong) is maud_errorLimit
// past a published bound and maud_errorInvalid otherwise, with no scene
// made, triangles past the def's limit are
// maud_errorCapacity, and the limits at their bounds are taken; an
// instance's transform out of range, its ids out of range and the
// pointers each function needs are refused.

#include "test_harness.h"

#include "maul-audio/scene.h"

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

int main(void)
{
    TestDefs();
    TestInstances();
    return s_failures == 0 ? 0 : 1;
}
