// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A probe set's bake: per probe the reverberation times and levels and,
// when the spatializer renders reflections, the energy field. A point
// takes the 4 nearest probes it sees within the set's range, weighted
// by 1/d less 1/d of the fifth nearest (0 without a fifth), normalised,
// so a probe's weight reaches 0 before it leaves the four; times blend
// in log, levels in dB, fields linearly. A point on a probe takes that
// probe's values exactly.

#ifndef MAUL_AUDIO_SRC_PROBE_BAKE_H
#define MAUL_AUDIO_SRC_PROBE_BAKE_H

#include "probe_graph.h"

#include <stdint.h>

typedef struct maudProbeBake
{
    uint32_t count;
    // Floats of a probe's field; 0 for none.
    uint32_t fieldFloats;
    float* times;
    float* levels;
    float* fields;
    void* memory;
    size_t bytes;
} maudProbeBake;

// Allocates a bake for count probes; false when memory runs out or the
// sizes overflow (nothing to release then).
bool maudCreateProbeBake(const maudAllocator* allocator, uint32_t count, uint32_t fieldFloats,
                         maudProbeBake* bake);
void maudReleaseProbeBake(const maudAllocator* allocator, maudProbeBake* bake);

// The bake's values at a point: times and levels (3 each) and, if the
// bake has fields and field is not NULL, the field. False when no probe
// is in sight (nothing written).
bool maudInterpolateBake(const maudProbeGraph* graph, const maudProbeBake* bake,
                         maudAnyHitFn* anyHit, void* context, maudVector3 point, float* times,
                         float* levels, float* field);

#endif // MAUL_AUDIO_SRC_PROBE_BAKE_H
