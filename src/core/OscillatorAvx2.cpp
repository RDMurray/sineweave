// SPDX-License-Identifier: AGPL-3.0-only
#include "OscillatorKernel.h"
#include <immintrin.h>
namespace sineweave {
void accumulateChirpAvx2(float *bank, int count, float amplitude, float amplitudeStep, const float *phaseReal,
                         const float *phaseImag, const float *rotationReal, const float *rotationImag,
                         float deltaReal, float deltaImag) noexcept {
    const auto lowRe = _mm_loadu_ps(phaseReal), lowIm = _mm_loadu_ps(phaseImag);
    const auto r4 = _mm_loadu_ps(rotationReal), i4 = _mm_loadu_ps(rotationImag);
    auto real = _mm256_set_m128(_mm_sub_ps(_mm_mul_ps(lowRe, r4), _mm_mul_ps(lowIm, i4)), lowRe);
    auto imag = _mm256_set_m128(_mm_add_ps(_mm_mul_ps(lowIm, r4), _mm_mul_ps(lowRe, i4)), lowIm);
    const auto d16Re = _mm_set1_ps(deltaReal), d16Im = _mm_set1_ps(deltaImag);
    const auto r4Squared = _mm_sub_ps(_mm_mul_ps(r4, r4), _mm_mul_ps(i4, i4));
    const auto i4Squared = _mm_mul_ps(_mm_set1_ps(2), _mm_mul_ps(r4, i4));
    const auto r8 = _mm_sub_ps(_mm_mul_ps(r4Squared, d16Re), _mm_mul_ps(i4Squared, d16Im));
    const auto i8 = _mm_add_ps(_mm_mul_ps(i4Squared, d16Re), _mm_mul_ps(r4Squared, d16Im));
    const float d32Re = deltaReal * deltaReal - deltaImag * deltaImag, d32Im = 2 * deltaReal * deltaImag;
    auto rotRe = _mm256_set_m128(
        _mm_sub_ps(_mm_mul_ps(r8, _mm_set1_ps(d32Re)), _mm_mul_ps(i8, _mm_set1_ps(d32Im))), r8);
    auto rotIm = _mm256_set_m128(
        _mm_add_ps(_mm_mul_ps(i8, _mm_set1_ps(d32Re)), _mm_mul_ps(r8, _mm_set1_ps(d32Im))), i8);
    const auto deltaRe = _mm256_set1_ps(d32Re * d32Re - d32Im * d32Im);
    const auto deltaIm = _mm256_set1_ps(2 * d32Re * d32Im);
    auto amp = _mm256_setr_ps(amplitude, amplitude + amplitudeStep, amplitude + 2 * amplitudeStep,
                              amplitude + 3 * amplitudeStep, amplitude + 4 * amplitudeStep,
                              amplitude + 5 * amplitudeStep, amplitude + 6 * amplitudeStep,
                              amplitude + 7 * amplitudeStep);
    const auto ampDelta = _mm256_set1_ps(8 * amplitudeStep);
    int j = 0;
    for (; j + 8 <= count; j += 8) {
        _mm256_storeu_ps(bank + j, _mm256_add_ps(_mm256_loadu_ps(bank + j), _mm256_mul_ps(real, amp)));
        const auto next = _mm256_sub_ps(_mm256_mul_ps(real, rotRe), _mm256_mul_ps(imag, rotIm));
        imag = _mm256_add_ps(_mm256_mul_ps(imag, rotRe), _mm256_mul_ps(real, rotIm));
        real = next;
        const auto nextRot = _mm256_sub_ps(_mm256_mul_ps(rotRe, deltaRe), _mm256_mul_ps(rotIm, deltaIm));
        rotIm = _mm256_add_ps(_mm256_mul_ps(rotIm, deltaRe), _mm256_mul_ps(rotRe, deltaIm));
        rotRe = nextRot;
        amp = _mm256_add_ps(amp, ampDelta);
    }
    float remainder[8];
    _mm256_storeu_ps(remainder, real);
    for (int lane = 0; j < count; ++j, ++lane)
        bank[j] += remainder[lane] * (amplitude + j * amplitudeStep);
}
void accumulateAvx2(float *bank, int count, float amplitude, float phaseReal, float phaseImag,
                    const float *offsetsReal, const float *offsetsImag, float rotationReal,
                    float rotationImag) noexcept {
    const auto c = _mm_set1_ps(phaseReal), s = _mm_set1_ps(phaseImag), re = _mm_loadu_ps(offsetsReal),
               im = _mm_loadu_ps(offsetsImag);
    const auto lowReal = _mm_sub_ps(_mm_mul_ps(c, re), _mm_mul_ps(s, im));
    const auto lowImag = _mm_add_ps(_mm_mul_ps(s, re), _mm_mul_ps(c, im));
    const auto c4 = _mm_set1_ps(rotationReal), s4 = _mm_set1_ps(rotationImag);
    const auto highReal = _mm_sub_ps(_mm_mul_ps(lowReal, c4), _mm_mul_ps(lowImag, s4));
    const auto highImag = _mm_add_ps(_mm_mul_ps(lowImag, c4), _mm_mul_ps(lowReal, s4));
    auto real = _mm256_set_m128(highReal, lowReal), imag = _mm256_set_m128(highImag, lowImag);
    const auto c8 = _mm256_set1_ps(rotationReal * rotationReal - rotationImag * rotationImag);
    const auto s8 = _mm256_set1_ps(2 * rotationReal * rotationImag), amp = _mm256_set1_ps(amplitude);
    int j = 0;
    for (; j + 8 <= count; j += 8) {
        _mm256_storeu_ps(bank + j, _mm256_add_ps(_mm256_loadu_ps(bank + j), _mm256_mul_ps(real, amp)));
        const auto next = _mm256_sub_ps(_mm256_mul_ps(real, c8), _mm256_mul_ps(imag, s8));
        imag = _mm256_add_ps(_mm256_mul_ps(imag, c8), _mm256_mul_ps(real, s8));
        real = next;
    }
    if (j < count) {
        float remainder[8];
        _mm256_storeu_ps(remainder, real);
        for (int lane = 0; j < count; ++j, ++lane)
            bank[j] += remainder[lane] * amplitude;
    }
}
} // namespace sineweave
