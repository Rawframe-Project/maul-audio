// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reverberation times from geometry: rays from the listener; at
// each hit a shadow ray back adds the reflected energy into 10 ms bins
// per band; each band's histogram is integrated backwards and fitted
// from -5 to -25 dB. Rays go out in batches of 64, each batch into its
// own histogram, so that the batches can run on any threads and the
// sum, taken in batch order, is the same every time.

#ifndef MAUL_AUDIO_SRC_REVERB_ESTIMATE_H
#define MAUL_AUDIO_SRC_REVERB_ESTIMATE_H

#include "maul-audio/spatializer.h"

#include <stdint.h>

#define MAUD_REVERB_BATCH 64u
// Bins of 10 ms: 10 s, past the -25 dB of the longest time (20 s).
#define MAUD_REVERB_BINS 1000u

typedef struct maudReverbTrace
{
    maudClosestHitFn* closestHit;
    maudAnyHitFn* anyHit;
    void* context;
    const maudAcousticMaterial* materials;
    uint32_t materialCount;
    // The air's amplitude exponent per metre and band.
    float air[MAUD_DIRECT_BANDS];
    maudVector3 listener;
    // All rays (a multiple of the batch or not); bounces at most.
    uint32_t rays;
    uint32_t maxBounces;
} maudReverbTrace;

// One batch's energy per band and bin, and when its first ray was cut
// short by the bounce cap (INFINITY if none was): past that, energy is
// missing.
typedef struct maudReverbHistogram
{
    float energy[MAUD_DIRECT_BANDS][MAUD_REVERB_BINS];
    float truncated;
} maudReverbHistogram;

// The batches rays take.
uint32_t maudReverbBatches(uint32_t rays);

// Traces batch (rays batch * 64 onward) into histogram, overwriting it.
void maudTraceReverbBatch(const maudReverbTrace* trace, uint32_t batch,
                          maudReverbHistogram* histogram);

// Sums count histograms into the first, in order, and fits each band:
// times in seconds, 0.1 to 20; the floor for a band without energy (an
// open field), the ceiling for one that never falls 25 dB. Where rays
// were cut short, the bins from the cut on are filled at the rate the
// bins before it decay at (as ISO 3382 part 1 compensates a truncated
// decay), and a band whose bins do not decay takes the ceiling.
void maudFitReverb(maudReverbHistogram* histograms, uint32_t count, float times[MAUD_DIRECT_BANDS]);

#endif // MAUL_AUDIO_SRC_REVERB_ESTIMATE_H
