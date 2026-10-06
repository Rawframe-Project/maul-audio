// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Acoustic scenes: the library's own geometry and ray tracer, for hosts
// without one and for work whose results must match on every platform.
// A scene is made whole from its meshes and is read-only afterwards, so
// any number of threads query it at once. Its query functions have the
// spatializer's hook signatures, the scene as their context.

#ifndef MAUL_AUDIO_SCENE_H
#define MAUL_AUDIO_SCENE_H

#include "maul-audio/base.h"
#include "maul-audio/spatializer.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // An acoustic scene.
    typedef struct maudAcousticScene maudAcousticScene;

    // Triangles over shared vertices, in the world's coordinates.
    typedef struct maudMesh
    {
        const maudVector3* vertices;
        uint32_t vertexCount;
        // Three vertex indices per triangle, counterclockwise seen from
        // the triangle's front.
        const uint32_t* indices;
        // Each triangle's index in the spatializer's material table.
        const uint32_t* materials;
        uint32_t triangleCount;
    } maudMesh;

    // How to create an acoustic scene. Build it with
    // maudDefaultAcousticSceneDef.
    typedef struct maudAcousticSceneDef
    {
        uint32_t cookie;
        // The meshes, copied at creation.
        const maudMesh* meshes;
        uint32_t meshCount;
        maudAllocator allocator;
    } maudAcousticSceneDef;

    /// Returns the default acoustic scene def: no meshes.
    ///
    /// @return The def, with a valid cookie.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API maudAcousticSceneDef maudDefaultAcousticSceneDef(void);

    /// Creates an acoustic scene: copies the meshes and builds a bounding
    /// volume hierarchy over their triangles, the same on every platform.
    ///
    /// @param def       The def, from maudDefaultAcousticSceneDef.
    /// @param sceneOut  Receives the scene; NULL on failure.
    /// @return `maud_success`; `maud_errorInvalid` for a NULL pointer, a
    ///         def without its cookie, a mesh with a NULL array it needs, a
    ///         vertex index past its vertices or a vertex that is not
    ///         finite; `maud_errorCapacity` for more than 16,777,216
    ///         triangles in all or when the allocator fails.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_NODISCARD MAUD_API maudResult maudCreateAcousticScene(const maudAcousticSceneDef* def,
                                                               maudAcousticScene** sceneOut);

    /// Destroys an acoustic scene. NULL is ignored. No query may be
    /// running on it.
    ///
    /// @param scene  The scene.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API void maudDestroyAcousticScene(maudAcousticScene* scene);

    /// Answers any-hit queries against a scene (a maudAnyHitFn): a ray
    /// hits when a triangle lies within [minDistance, maxDistance] along
    /// it. A ray through an edge or vertex shared by triangles hits.
    ///
    /// @param rays      count rays.
    /// @param count     The rays.
    /// @param occluded  Receives 1 or 0 per ray.
    /// @param scene     The scene.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API void maudSceneAnyHit(const maudRay* rays, uint32_t count, uint8_t* occluded,
                                  void* scene);

    /// Answers closest-hit queries against a scene (a maudClosestHitFn):
    /// the nearest triangle within a ray's distances, equal distances
    /// going to the triangle listed first (meshes in order, then their
    /// triangles); its unit normal faces the side the triangle is wound
    /// counterclockwise from. A miss has distance INFINITY.
    ///
    /// @param rays   count rays.
    /// @param count  The rays.
    /// @param hits   Receives a hit per ray.
    /// @param scene  The scene.
    /// @par Thread safety
    /// Safe from any thread.
    MAUD_API void maudSceneClosestHit(const maudRay* rays, uint32_t count, maudRayHit* hits,
                                      void* scene);

#ifdef __cplusplus
}
#endif

#endif // MAUL_AUDIO_SCENE_H
