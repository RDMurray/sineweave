// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "core/Model.h"
namespace sineweave {
class IAnalyser {
  public:
    virtual ~IAnalyser() = default;
    virtual AnalysedSound analyse(const std::vector<double> &mono, AnalysedSound metadata) = 0;
};
class LorisAnalyser final : public IAnalyser {
    std::size_t outputBudget;

  public:
    explicit LorisAnalyser(std::size_t budget = memoryBudget) : outputBudget(budget) {}
    AnalysedSound analyse(const std::vector<double> &mono, AnalysedSound metadata) override;
};
} // namespace sineweave
