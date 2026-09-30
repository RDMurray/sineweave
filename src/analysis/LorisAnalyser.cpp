// SPDX-License-Identifier: AGPL-3.0-only
#include "Analyser.h"
#include <Analyzer.h>
#include <Partial.h>
#include <cmath>
#include <stdexcept>
namespace sineweave {
AnalysedSound LorisAnalyser::analyse(const std::vector<double> &mono, AnalysedSound s) {
    validate(s);
    std::size_t used = sizeof(s) + s.sourcePath.size();
    if (outputBudget < used || outputBudget > memoryBudget)
        throw std::runtime_error("Invalid analysis output budget");
    if (mono.empty() || mono.size() > memoryBudget / (sizeof(double) * 8))
        throw std::runtime_error("Audio exceeds analysis budget (32 MiB mono PCM with Loris headroom)");
    for (double x : mono)
        if (!std::isfinite(x))
            throw std::runtime_error("Audio contains non-finite samples");
    Loris::Analyzer analyser(s.settings.resolutionHz, s.settings.windowWidthHz);
    analyser.setAmpFloor(s.settings.amplitudeFloorDb);
    analyser.storeResidueBandwidth();
    auto partials = analyser.analyze(mono, s.sourceSampleRate);
    s.partials.clear();
    std::uint64_t id = 1;
    for (const auto &partial : partials) {
        if (!partial.size())
            continue;
        if (used > outputBudget - sizeof(PartialTrajectory) ||
            partial.size() > (outputBudget - used - sizeof(PartialTrajectory)) / sizeof(PartialBreakpoint))
            throw std::runtime_error(
                "Analysis exceeds available per-instance budget; choose a shorter range");
        used += sizeof(PartialTrajectory) + partial.size() * sizeof(PartialBreakpoint);
        PartialTrajectory t;
        t.id = id++;
        t.points.reserve(partial.size());
        for (auto it = partial.begin(); it != partial.end(); ++it) {
            const double time = it.time() + s.startSeconds;
            if (time < s.startSeconds || time > s.endSeconds)
                continue;
            t.points.push_back({time, it->frequency(), it->amplitude(), it->phase(), it->bandwidth()});
        }
        if (!t.points.empty())
            s.partials.push_back(std::move(t));
    }
    validate(s);
    return s;
}
} // namespace sineweave
