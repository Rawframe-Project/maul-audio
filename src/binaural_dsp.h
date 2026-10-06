// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The binaural effect's sample loops: a direct-form FIR and a fractional
// delay read. Both work in caller memory, allocate nothing and do work
// in proportion to their frames.

#ifndef MAUL_AUDIO_SRC_BINAURAL_DSP_H
#define MAUL_AUDIO_SRC_BINAURAL_DSP_H

#include <stdint.h>

// out[n] = sum over k of h[k] x[n - k], for n below frames; x has
// taps - 1 samples of history before x[0]. Written tap by tap across the
// frames (output-major), which compilers vectorize without reassociating
// sums.
void maudFir(const float* restrict x, const float* restrict h, uint32_t taps, float* restrict out,
             uint32_t frames);

// out[n] = x read start + step n samples in the past, by cubic Lagrange
// interpolation. Every delay is at least 1 (the read never needs a
// sample after x[n]); x has as much history as the largest delay plus 2.
void maudReadDelayed(const float* restrict x, float start, float step, float* restrict out,
                     uint32_t frames);

#endif // MAUL_AUDIO_SRC_BINAURAL_DSP_H
