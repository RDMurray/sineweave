// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
namespace sineweave {
// Compiled in its own AVX2 translation unit. Call only after CPU/OS capability
// detection in the baseline synth; no AVX instructions leak into the fallback.
void accumulateAvx2(float *bank, int count, float amplitude, float phaseReal, float phaseImag,
                    const float *offsetsReal, const float *offsetsImag, float rotationReal,
                    float rotationImag) noexcept;
void accumulateChirpAvx2(float *bank, int count, float amplitude, float amplitudeStep, const float *phaseReal,
                         const float *phaseImag, const float *rotationReal, const float *rotationImag,
                         float deltaReal, float deltaImag) noexcept;
} // namespace sineweave
