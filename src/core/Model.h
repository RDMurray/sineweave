// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sineweave {
constexpr double pi = 3.14159265358979323846;
constexpr std::size_t memoryBudget = 256u * 1024u * 1024u;
constexpr int maxPartials = 4096, maxVoices = 16;
enum class InputChannel : std::uint32_t { mix, left, right };
struct AnalysisSettings {
    double resolutionHz = 40, windowWidthHz = 80, amplitudeFloorDb = -80;
};
struct PartialBreakpoint {
    double time = 0, frequencyHz = 0, amplitude = 0, phaseRadians = 0;
    std::optional<double> bandwidth;
};
struct PartialTrajectory {
    std::uint64_t id = 0;
    std::vector<PartialBreakpoint> points;
};
// Reserved model sections for a later transient/residual renderer.
struct AudioSection {
    double sampleRate = 0, originSeconds = 0;
    std::vector<float> mono;
};
struct AnalysedSound {
    std::string sourcePath;
    double sourceSampleRate = 0, sourceDuration = 0, startSeconds = 0, endSeconds = 0;
    InputChannel channel = InputChannel::mix;
    AnalysisSettings settings;
    std::vector<PartialTrajectory> partials;
    std::optional<AudioSection> sourceAudio, residualAudio;
};
struct SpectralPartial {
    std::uint64_t id;
    double frequencyHz, amplitude, phaseRadians;
    std::optional<double> bandwidth;
};
struct SpectralFrame {
    double sourceSeconds = 0;
    std::size_t activeCount = 0;
    std::vector<SpectralPartial> partials;
};
struct SoundState {
    AnalysedSound sound;
    SpectralFrame frame;
};
double wrapPhase(double radians) noexcept;
void validate(const AnalysedSound &);
SpectralFrame sampleFrame(const AnalysedSound &, double sourceSeconds, int limit = maxPartials);
std::vector<std::uint8_t> encodeState(const SoundState &);
std::vector<std::uint8_t> encodeState(const AnalysedSound &, const SpectralFrame &);
SoundState decodeState(const void *, std::size_t);
} // namespace sineweave
