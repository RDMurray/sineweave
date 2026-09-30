// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Processor.h"
#include <condition_variable>
#include <optional>
#include <thread>
namespace sineweave {
class Worker {
    enum class Kind { load, analyse, restore };
    struct Job {
        Kind kind;
        std::uint64_t generation;
        juce::File file;
        juce::MemoryBlock model;
        juce::ValueTree params;
        double start = 0, end = 0;
        InputChannel channel = InputChannel::mix;
        AnalysisSettings settings;
        bool legacy = false;
    };
    Processor &processor;
    mutable std::mutex mutex;
    std::condition_variable wake;
    bool stopping = false;
    std::optional<Job> job;
    juce::String statusText = "Load an audio file, then choose Analyse. No timbre is loaded.";
    juce::MemoryBlock cachedModel;
    juce::File source;
    std::unique_ptr<SoundState> state;
    std::unique_ptr<PreparedFrame> pendingFrame;
    bool samplingEnabled = false;
    double lastTime = -1;
    int lastLimit = -1;
    int frameLimit = -1;
    std::array<float, 8> lastInputs{}, validInputs{};
    std::vector<std::unique_ptr<PlaybackProgram>> programs;
    const PlaybackProgram *currentProgram = nullptr;
    std::thread thread;
    void run();
    void execute(Job &);
    void prepareFrame(std::uint64_t generation);
    void enqueue(Job);
    std::size_t availableBudget() const;
    std::size_t budgetAfter(std::size_t reserve) const;
    void requireBudget(std::size_t additionalBytes) const;
    void report(juce::String, std::uint64_t requestGeneration = 0);
    void reclaimPrograms();
    void commitProgram(std::unique_ptr<PlaybackProgram>);

  public:
    explicit Worker(Processor &);
    ~Worker();
    void stop();
    void load(const juce::File &);
    void analyse();
    void restore(juce::MemoryBlock, juce::ValueTree, bool legacy = false);
    juce::MemoryBlock modelSnapshot() const;
    std::array<float, 8> configurationSnapshot() const;
    struct Snapshot {
        juce::MemoryBlock model;
        std::array<float, 8> configuration;
    };
    Snapshot snapshot() const;
    juce::String status() const;
    void error(juce::String s) {
        report(std::move(s));
    }
};
} // namespace sineweave
