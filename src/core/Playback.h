// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Model.h"
#include <atomic>
#include <memory>
namespace sineweave {
enum class LoopMode { off, wrap, pingPong };
struct PlaybackSettings {
    double start = 0, end = 0, loopStart = 0, loopEnd = 0, crossfadeMs = 20;
    int direction = 1;
    LoopMode loop = LoopMode::off;
};
struct LaneInterval {
    double begin, end;
    int track;
};
struct PlaybackLane {
    std::vector<LaneInterval> intervals;
};
// Immutable after compilation. References never delete: only the worker owns/deletes programs.
struct PlaybackProgram {
    static_assert(std::atomic<unsigned>::is_always_lock_free, "Program leases must be lock-free");
    mutable std::atomic<unsigned> references{0};
    std::vector<PartialTrajectory> tracks;
    std::vector<PlaybackLane> lanes;
    double start = 0, end = 0, attenuation = 1;
    std::size_t excludedTracks = 0;
    bool limited = false;
    std::size_t bytes() const noexcept;
    void retain() const noexcept {
        references.fetch_add(1, std::memory_order_relaxed);
    }
    void release() const noexcept {
        references.fetch_sub(1, std::memory_order_release);
    }
};
std::unique_ptr<PlaybackProgram> compilePlayback(const AnalysedSound &, int limit,
                                                 std::size_t budget = memoryBudget);
PlaybackSettings validatePlayback(PlaybackSettings, double start, double end);
int laneIntervalAt(const PlaybackLane &, double time, int direction, int previous) noexcept;
SpectralPartial trajectoryAt(const PartialTrajectory &, double time, int &cursor,
                             bool evaluatePhase = true) noexcept;
} // namespace sineweave
