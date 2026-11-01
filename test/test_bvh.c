// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The hierarchy's build: two clusters of triangles 100 m apart, listed
// alternately, with most of the bins between them empty. The surface
// area heuristic must split them at the root, each child's box holding
// one cluster; a build that fell back to halving the list would mix
// them in both children. Four triangles, the most a leaf holds, make
// one leaf. Centroids more than FLT_MAX apart, or a few denormals apart,
// leave the bins no finite scale: such an axis is not binned, and the
// build still places every triangle in one leaf.

#include "bvh.h"
#include "test_harness.h"

// Whether the hierarchy's leaves hold each of the count triangles once.
static bool EachOnce(const maudBvhNode* nodes, uint32_t used, const maudTriangle* triangles,
                     uint32_t count)
{
    uint32_t seen[16] = {0};
    for (uint32_t n = 0; n < used; ++n)
    {
        for (uint32_t k = 0; k < nodes[n].count; ++k)
        {
            seen[triangles[nodes[n].offset + k].index] += 1;
        }
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        if (seen[i] != 1)
        {
            return false;
        }
    }
    return true;
}

// Eight triangles, half with their x at a and half at b, built.
static bool BuildsApart(float a, float b)
{
    maudTriangle triangles[8];
    for (uint32_t i = 0; i < 8; ++i)
    {
        float x = i < 4 ? a : b;
        float y = (float)i;
        triangles[i] = (maudTriangle){{x, y, 0.0f}, {x, y + 1.0f, 0.0f}, {x, y, 1.0f}, i, 0};
    }
    maudBvhNode nodes[15];
    uint32_t used = maudBuildBvh(triangles, 8, nodes);
    return used <= maudBvhCapacity(8) && EachOnce(nodes, used, triangles, 8);
}

int main(void)
{
    maudTriangle triangles[16];
    for (uint32_t i = 0; i < 16; ++i)
    {
        float x = i % 2 == 0 ? 0.0f : 100.0f;
        float y = (float)(i / 2);
        triangles[i] = (maudTriangle){{x, y, 0.0f}, {x + 1.0f, y, 0.0f}, {x, y + 1.0f, 0.0f}, i, i};
    }
    maudBvhNode nodes[31];
    uint32_t used = maudBuildBvh(triangles, 16, nodes);
    CHECK(used <= maudBvhCapacity(16), "within its capacity");
    const maudBvhNode* left = &nodes[1];
    const maudBvhNode* right = &nodes[nodes[0].offset];
    bool apart = (left->max[0] <= 1.0f && right->min[0] >= 100.0f) ||
                 (right->max[0] <= 1.0f && left->min[0] >= 100.0f);
    CHECK(nodes[0].count == 0 && apart, "the root splits the clusters apart");
    // Four triangles, the most a leaf holds: one node.
    maudBvhNode leaf[31];
    CHECK(maudBuildBvh(triangles, 4, leaf) == 1 && leaf[0].count == 4,
          "four triangles in one leaf");
    CHECK(EachOnce(nodes, used, triangles, 16), "every triangle in one leaf");
    // Every vertex finite, the centroids' span past FLT_MAX, and then the
    // span a denormal, whose bins' scale would be infinite.
    CHECK(BuildsApart(3.0e38f, -3.0e38f), "centroids more than FLT_MAX apart");
    CHECK(BuildsApart(0.0f, 3.0e-45f), "centroids a few denormals apart");
    return s_failures == 0 ? 0 : 1;
}
