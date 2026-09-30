// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "core/FrameExchange.h"
#include "core/Synth.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <memory>
#include <mutex>
namespace sineweave {
class Worker;
struct ParameterSpec {
    const char *id;
    const char *name;
    const char *unit;
    float minimum, maximum, initial, interval, skew;
};
inline constexpr std::array<ParameterSpec, 20> specs{
    {{"start", "Analysis start", "s", 0, 86400, 0, .001f, .3f},
     {"end", "Analysis end (0 = file end)", "s", 0, 86400, 0, .001f, .3f},
     {"position", "Sustain position", "s", 0, 86400, 0, .001f, .3f},
     {"resolution", "Analysis resolution", "Hz", 1, 2000, 40, .1f, .4f},
     {"floor", "Analysis amplitude floor", "dB", -160, 0, -80, .1f, 1},
     {"root", "Reference pitch", "MIDI note", 0, 127, 69, .01f, 1},
     {"gain", "Master gain", "dB", -80, 6, -24, .1f, 1},
     {"attack", "Attack", "ms", 0, 10000, 10, .1f, .3f},
     {"decay", "Decay", "ms", 0, 10000, 100, .1f, .3f},
     {"sustain", "Sustain level", "%", 0, 1, 1, .001f, 1},
     {"release", "Release", "ms", 0, 20000, 250, .1f, .3f},
     {"bend", "Pitch bend range", "semitones", 0, 24, 2, .1f, 1},
     {"voices", "Polyphony", "voices", 1, 16, 16, 1, 1},
     {"partials", "Rendered partial limit", "partials", 1, 4096, 4096, 1, .5f},
     {"speed", "Playback speed", "x", 0, 4, 1, .001f, 1},
     {"playStart", "Playback start", "s", 0, 86400, 0, .001f, .3f},
     {"playEnd", "Playback end", "s", 0, 86400, 0, .001f, .3f},
     {"loopStart", "Loop start", "s", 0, 86400, 0, .001f, .3f},
     {"loopEnd", "Loop end", "s", 0, 86400, 0, .001f, .3f},
     {"crossfade", "Loop crossfade", "ms", 0, 100, 20, .1f, 1}}};
class Processor final : public juce::AudioProcessor, private juce::AsyncUpdater {
  public:
    SpscQueue<const PlaybackProgram *, 64> programReturns;

  private:
    std::array<std::atomic<float> *, specs.size()> values{};
    std::atomic<float> *channelValue = nullptr;
    std::atomic<float> *modeValue = nullptr, *directionValue = nullptr, *loopValue = nullptr;
    std::unique_ptr<Synth> synth;
    std::unique_ptr<Worker> worker;
    std::mutex defaultsMutex;
    double pendingDuration = 0;
    std::uint64_t defaultsGeneration = 0;
    bool pendingFileDefaults = false, pendingRangeDefaults = false;
    double pendingRangeStart = 0, pendingRangeEnd = 0;
    void handleAsyncUpdate() override;

  public:
    juce::AudioProcessorValueTreeState parameters;
    FrameExchange frames;
    std::atomic<std::uint64_t> generation{0};
    std::atomic<bool> previewEnabled{false};
    Processor();
    ~Processor() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    float value(std::size_t index) const noexcept {
        return values[index]->load(std::memory_order_relaxed);
    }
    InputChannel inputChannel() const noexcept {
        return static_cast<InputChannel>(static_cast<int>(channelValue->load()));
    }
    void loadFile(const juce::File &);
    void analyse();
    juce::String status() const;
    void fileDefaults(double duration, std::uint64_t requestGeneration);
    void rangeDefaults(double start, double end, std::uint64_t requestGeneration);
    int playbackMode() const noexcept {
        return static_cast<int>(modeValue->load());
    }
    int playbackDirection() const noexcept {
        return static_cast<int>(directionValue->load());
    }
    int loopMode() const noexcept {
        return static_cast<int>(loopValue->load());
    }
    std::array<float, 8> playbackInputs() const noexcept {
        return {value(15),
                value(16),
                value(17),
                value(18),
                value(19),
                static_cast<float>(playbackMode()),
                static_cast<float>(playbackDirection()),
                static_cast<float>(loopMode())};
    }
    const juce::String getName() const override {
        return "Sineweave";
    }
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    bool isBusesLayoutSupported(const BusesLayout &) const override;
    bool acceptsMidi() const override {
        return true;
    }
    bool producesMidi() const override {
        return false;
    }
    bool isMidiEffect() const override {
        return false;
    }
    double getTailLengthSeconds() const override {
        return 20;
    }
    int getNumPrograms() override {
        return 1;
    }
    int getCurrentProgram() override {
        return 0;
    }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {
        return {};
    }
    void changeProgramName(int, const juce::String &) override {}
    bool hasEditor() const override {
        return true;
    }
    juce::AudioProcessorEditor *createEditor() override;
    void getStateInformation(juce::MemoryBlock &) override;
    void setStateInformation(const void *, int) override;
};
} // namespace sineweave
