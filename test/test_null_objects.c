// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every call that answers a value rather than a status, given a NULL
// object: the misuse counts and the latch answer 0, the backend
// maud_backendNative, and the ray queries a miss for every ray, writing
// nothing when the rays or the output are NULL. None reads through the
// NULL pointer.

#include "test_harness.h"

#include "maul-audio/context.h"
#include "maul-audio/scene.h"
#include "maul-audio/spatializer.h"

#include <math.h>
#include <stdint.h>

static void TestGetters(void)
{
    CHECK(maudContext_GetMisuse(nullptr) == 0, "a NULL context's misuse");
    CHECK(maudContext_GetBackend(nullptr) == maud_backendNative, "a NULL context's backend");
    CHECK(maudScene_GetMisuse(nullptr) == 0, "a NULL scene's misuse");
    CHECK(maudSpatializer_GetMisuse(nullptr) == 0, "a NULL spatializer's misuse");
    CHECK(maudSpatializer_LatchResults(nullptr) == 0, "a NULL spatializer's latch");
}

static void TestRayQueries(void)
{
    maudRay rays[2] = {
        {{0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.0f, 10.0f},
        {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.0f, 10.0f},
    };
    uint8_t occluded[2] = {7, 7};
    maudFindAnyHit(rays, 2, occluded, nullptr);
    CHECK(occluded[0] == 0 && occluded[1] == 0, "a NULL scene occludes no ray");
    maudRayHit hits[2] = {{1.0f, {1.0f, 0.0f, 0.0f}, 7}, {1.0f, {1.0f, 0.0f, 0.0f}, 7}};
    maudFindClosestHit(rays, 2, hits, nullptr);
    CHECK(isinf(hits[0].distance) && isinf(hits[1].distance) && hits[0].material == 0 &&
              hits[1].normal.x == 0.0f,
          "a NULL scene: every ray misses");

    occluded[0] = 7;
    maudFindAnyHit(nullptr, 2, occluded, nullptr);
    CHECK(occluded[0] == 7, "NULL rays: nothing written");
    hits[0].material = 7;
    maudFindClosestHit(nullptr, 2, hits, nullptr);
    CHECK(hits[0].material == 7, "NULL rays: no hit written");
    maudFindAnyHit(rays, 2, nullptr, nullptr);
    maudFindClosestHit(rays, 2, nullptr, nullptr);
}

int main(void)
{
    TestGetters();
    TestRayQueries();
    return s_failures == 0 ? 0 : 1;
}
