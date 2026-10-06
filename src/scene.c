// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Acoustic scenes (maul-audio/scene.h): the meshes' triangles copied in
// order, numbered across the meshes, and a hierarchy over them.

#include "maul-audio/scene.h"

#include "allocator.h"
#include "bvh.h"

#include <math.h>

#define SCENE_DEF_COOKIE 0x6D617363u
#define MAX_TRIANGLES    16777216u

struct maudAcousticScene
{
    maudAllocator allocator;
    uint32_t triangleCount;
    uint32_t nodeCount;
    maudTriangle* triangles;
    maudBvhNode* nodes;
};

maudAcousticSceneDef maudDefaultAcousticSceneDef(void)
{
    return (maudAcousticSceneDef){
        .cookie = SCENE_DEF_COOKIE,
        .meshes = nullptr,
        .meshCount = 0,
        .allocator = {nullptr, nullptr, nullptr},
    };
}

static bool MeshValid(const maudMesh* mesh)
{
    if (mesh->triangleCount == 0)
    {
        return true;
    }
    if (mesh->vertices == nullptr || mesh->indices == nullptr || mesh->materials == nullptr)
    {
        return false;
    }
    for (uint32_t i = 0; i < mesh->vertexCount; ++i)
    {
        maudVector3 v = mesh->vertices[i];
        if (!isfinite(v.x) || !isfinite(v.y) || !isfinite(v.z))
        {
            return false;
        }
    }
    for (uint64_t i = 0; i < 3 * (uint64_t)mesh->triangleCount; ++i)
    {
        if (mesh->indices[i] >= mesh->vertexCount)
        {
            return false;
        }
    }
    return true;
}

// Checks the def and counts its triangles.
static maudResult Count(const maudAcousticSceneDef* def, uint32_t* total)
{
    if (def->cookie != SCENE_DEF_COOKIE || (def->meshCount > 0 && def->meshes == nullptr) ||
        !maudIsAllocatorValid(&def->allocator))
    {
        return maud_errorInvalid;
    }
    uint64_t sum = 0;
    for (uint32_t m = 0; m < def->meshCount; ++m)
    {
        if (!MeshValid(&def->meshes[m]))
        {
            return maud_errorInvalid;
        }
        sum += def->meshes[m].triangleCount;
    }
    if (sum > MAX_TRIANGLES)
    {
        return maud_errorCapacity;
    }
    *total = (uint32_t)sum;
    return maud_success;
}

static void Copy(const maudAcousticSceneDef* def, maudTriangle* triangles)
{
    uint32_t n = 0;
    for (uint32_t m = 0; m < def->meshCount; ++m)
    {
        const maudMesh* mesh = &def->meshes[m];
        for (uint32_t i = 0; i < mesh->triangleCount; ++i, ++n)
        {
            const maudVector3* v = mesh->vertices;
            maudVector3 a = v[mesh->indices[3 * (size_t)i]];
            maudVector3 b = v[mesh->indices[3 * (size_t)i + 1]];
            maudVector3 c = v[mesh->indices[3 * (size_t)i + 2]];
            triangles[n] = (maudTriangle){
                {a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}, n, mesh->materials[i]};
        }
    }
}

static void Release(maudAcousticScene* scene)
{
    maudAllocator allocator = scene->allocator;
    uint32_t capacity = maudBvhCapacity(scene->triangleCount);
    if (scene->triangles != nullptr)
    {
        maudRelease(&allocator, scene->triangles,
                    (size_t)scene->triangleCount * sizeof(maudTriangle), alignof(maudTriangle));
    }
    if (scene->nodes != nullptr)
    {
        maudRelease(&allocator, scene->nodes, (size_t)capacity * sizeof(maudBvhNode),
                    alignof(maudBvhNode));
    }
    maudRelease(&allocator, scene, sizeof(maudAcousticScene), alignof(maudAcousticScene));
}

maudResult maudCreateAcousticScene(const maudAcousticSceneDef* def, maudAcousticScene** sceneOut)
{
    if (sceneOut != nullptr)
    {
        *sceneOut = nullptr;
    }
    if (def == nullptr || sceneOut == nullptr)
    {
        return maud_errorInvalid;
    }
    uint32_t total = 0;
    maudResult result = Count(def, &total);
    if (result != maud_success)
    {
        return result;
    }
    maudAcousticScene* scene =
        maudAllocate(&def->allocator, sizeof(maudAcousticScene), alignof(maudAcousticScene));
    if (scene == nullptr)
    {
        return maud_errorCapacity;
    }
    *scene = (maudAcousticScene){.allocator = def->allocator, .triangleCount = total};
    if (total > 0)
    {
        scene->triangles = maudAllocate(&def->allocator, (size_t)total * sizeof(maudTriangle),
                                        alignof(maudTriangle));
        scene->nodes =
            maudAllocate(&def->allocator, (size_t)maudBvhCapacity(total) * sizeof(maudBvhNode),
                         alignof(maudBvhNode));
        if (scene->triangles == nullptr || scene->nodes == nullptr)
        {
            Release(scene);
            return maud_errorCapacity;
        }
        Copy(def, scene->triangles);
        scene->nodeCount = maudBuildBvh(scene->triangles, total, scene->nodes);
    }
    *sceneOut = scene;
    return maud_success;
}

void maudDestroyAcousticScene(maudAcousticScene* scene)
{
    if (scene != nullptr)
    {
        Release(scene);
    }
}

static void Unpack(const maudRay* ray, float* origin, float* direction)
{
    origin[0] = ray->origin.x;
    origin[1] = ray->origin.y;
    origin[2] = ray->origin.z;
    direction[0] = ray->direction.x;
    direction[1] = ray->direction.y;
    direction[2] = ray->direction.z;
}

void maudSceneAnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded, void* scene)
{
    const maudAcousticScene* s = scene;
    for (uint32_t i = 0; i < count; ++i)
    {
        float origin[3];
        float direction[3];
        Unpack(&rays[i], origin, direction);
        occluded[i] =
            s->triangleCount > 0 && maudBvhAnyHit(s->nodes, s->triangles, origin, direction,
                                                  rays[i].minDistance, rays[i].maxDistance);
    }
}

static maudVector3 Normal(const maudTriangle* t)
{
    double u[3];
    double v[3];
    for (int i = 0; i < 3; ++i)
    {
        u[i] = (double)t->b[i] - (double)t->a[i];
        v[i] = (double)t->c[i] - (double)t->a[i];
    }
    double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
    double length = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (length == 0.0)
    {
        return (maudVector3){0.0f, 0.0f, 0.0f};
    }
    return (maudVector3){(float)(n[0] / length), (float)(n[1] / length), (float)(n[2] / length)};
}

void maudSceneClosestHit(const maudRay* rays, uint32_t count, maudRayHit* hits, void* scene)
{
    const maudAcousticScene* s = scene;
    for (uint32_t i = 0; i < count; ++i)
    {
        hits[i] = (maudRayHit){INFINITY, {0.0f, 0.0f, 0.0f}, 0};
        if (s->triangleCount == 0)
        {
            continue;
        }
        float origin[3];
        float direction[3];
        Unpack(&rays[i], origin, direction);
        float t = 0.0f;
        const maudTriangle* hit = maudBvhClosestHit(s->nodes, s->triangles, origin, direction,
                                                    rays[i].minDistance, rays[i].maxDistance, &t);
        if (hit != nullptr)
        {
            hits[i] = (maudRayHit){t, Normal(hit), hit->material};
        }
    }
}
