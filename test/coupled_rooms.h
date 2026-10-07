// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Two coupled rooms for the reverberation tests (research 34's): one
// live, x 0 to 6 m, one damped, x 6 to 12 m, 3 m high and 4 m deep,
// walls 0.1 m thick, joined by a door at x 6, z 1.5 to 2.5 m, up to
// 2.1 m. Material 0 is the live room's, 1 the damped room's;
// CoupledMaterials gives research 34's (absorption 0.05 to 0.08, and 0.4
// to 0.6). A decay in the damped room has the live room's slow slope
// behind its own fast one.

#ifndef MAUL_AUDIO_TEST_COUPLED_ROOMS_H
#define MAUL_AUDIO_TEST_COUPLED_ROOMS_H

#include "test_harness.h"

#include "maul-audio/scene.h"

static const maudAcousticMaterial CoupledMaterials[2] = {{{0.05f, 0.05f, 0.08f}, 0.3f, {0, 0, 0}},
                                                         {{0.4f, 0.5f, 0.6f}, 0.3f, {0, 0, 0}}};

static inline maudAcousticScene* CoupledRooms(void)
{
    static maudVector3 v[8 * 13];
    static uint32_t indices[36 * 13];
    static uint32_t materials[12 * 13];
    static const uint32_t faces[36] = {0, 2, 6, 0, 6, 4, 1, 5, 7, 1, 7, 3, 0, 4, 5, 0, 5, 1,
                                       2, 3, 7, 2, 7, 6, 0, 1, 3, 0, 3, 2, 4, 6, 7, 4, 7, 5};
    static const float boxes[13][7] = {
        {0, -0.1f, 0, 6, 0, 4, 0},
        {6, -0.1f, 0, 12, 0, 4, 1},
        {0, 3, 0, 6, 3.1f, 4, 0},
        {6, 3, 0, 12, 3.1f, 4, 1},
        {0, 0, -0.1f, 6, 3, 0, 0},
        {6, 0, -0.1f, 12, 3, 0, 1},
        {0, 0, 4, 6, 3, 4.1f, 0},
        {6, 0, 4, 12, 3, 4.1f, 1},
        {-0.1f, 0, 0, 0, 3, 4, 0},
        {12, 0, 0, 12.1f, 3, 4, 1},
        {5.95f, 0, 0, 6.05f, 3, 1.5f, 0},
        {5.95f, 0, 2.5f, 6.05f, 3, 4, 0},
        {5.95f, 2.1f, 1.5f, 6.05f, 3, 2.5f, 0},
    };
    for (uint32_t n = 0; n < 13; ++n)
    {
        const float* b = boxes[n];
        for (uint32_t i = 0; i < 8; ++i)
        {
            v[n * 8 + i] =
                (maudVector3){(i & 1) ? b[3] : b[0], (i & 2) ? b[4] : b[1], (i & 4) ? b[5] : b[2]};
        }
        for (uint32_t i = 0; i < 36; ++i)
        {
            indices[n * 36 + i] = n * 8 + faces[i];
        }
        for (uint32_t i = 0; i < 12; ++i)
        {
            materials[n * 12 + i] = (uint32_t)b[6];
        }
    }
    maudMesh mesh = {v, 8 * 13, indices, materials, 12 * 13};
    maudAcousticSceneDef def = maudDefaultAcousticSceneDef();
    def.meshes = &mesh;
    def.meshCount = 1;
    maudAcousticScene* scene = nullptr;
    CHECK(maudCreateAcousticScene(&def, &scene) == maud_success, "the two rooms");
    return scene;
}

#endif // MAUL_AUDIO_TEST_COUPLED_ROOMS_H
