// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The hierarchy's build: two clusters of triangles 100 m apart, listed
// alternately, with most of the bins between them empty. The surface
// area heuristic must split them at the root, each child's box holding
// one cluster; a build that fell back to halving the list would mix
// them in both children.

#include "bvh.h"
#include "test_harness.h"

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
    return s_failures == 0 ? 0 : 1;
}
