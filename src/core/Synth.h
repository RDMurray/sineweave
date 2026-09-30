// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "FrameExchange.h"
#include <array>
#include <cstdint>
namespace sineweave {
struct SynthControls {
    float gainDb = -24, referenceNote = 69, attackMs = 10, decayMs = 100, sustain = 1, releaseMs = 250,
          bendRange = 2;
    int voices = maxVoices;
    float speed = 1;
};
class Synth {
    static constexpr int tableSize = 16384, tailCount = 8;
    enum class Stage { attack, decay, sustain, release, idle };
    struct Bank {
        std::array<double, maxPartials> frequency{}, phase{};
        std::array<float, maxPartials> amplitude{};
        std::array<double, maxPartials> step{};
        std::array<std::array<float, 4>, maxPartials> offsetsReal{}, offsetsImag{};
        std::array<float, maxPartials> rotationReal{}, rotationImag{}, filteredAmplitude{};
        std::array<int, maxPartials> track{}, interval{}, point{};
        std::array<float, maxPartials> laneGain{};
        int count = 0;
        double cachedRatio = -1;
        bool fadeEntries = true;
    };
    struct Voice : Bank {
        Bank overlap;
        const PlaybackProgram *program = nullptr;
        PlaybackSettings playback;
        double sourceTime = 0, sourceSpeed = 1;
        double speedTarget = 1, speedDelta = 0;
        int speedRampLeft = 0;
        int direction = 1, overlapDirection = 1;
        int crossfadeTotal = 0, crossfadeLeft = 0;
        bool atEnd = false;
        int note = 0, channel = 0;
        std::uint64_t age = 0;
        bool keyDown = false;
        Stage stage = Stage::idle;
        float envelope = 0, velocity = 0, releaseStep = 0;
        double ratio = 1, targetRatio = 1;
        int tailSamples = 0;
        bool preview = false;
        int previewHoldSamples = 0;
    };
    std::array<Voice, maxVoices> voices;
    std::array<Voice, tailCount> tails;
    Voice previewVoice, previewTail;
    std::array<float, tableSize + 1> table{};
    std::array<float, 16> bend{};
    std::array<bool, 16> pedal{};
    SynthControls controls;
    double rate = 48000, smoothing = 0;
    float gain = 0, targetGain = 0;
    std::uint64_t clock = 0;
    bool avx2 = false;
    SpscQueue<const PlaybackProgram *, 64> *retirements = nullptr;
    void release(Voice &) noexcept;
    void retune(Voice &) noexcept;
    void retime(Voice &) noexcept;
    void makeTail(Voice &) noexcept;
    void copyTail(const Voice &, Voice &) noexcept;
    void copyBank(const Bank &, Bank &) noexcept;
    void finishVoice(Voice &) noexcept;
    void startVoice(Voice &, int channel, int note, float velocity, const SpectralFrame &) noexcept;
    float tick(Voice &, bool tail) noexcept;
    float cosine(double cycles) const noexcept;
    void renderVoice(Voice &, float *output, int count, bool tail) noexcept;
    void renderTrajectory(Voice &, float *output, int count, bool tail) noexcept;
    void renderBank(Bank &, const PlaybackProgram &, double time, double endTime, int direction,
                    double ratioPerSample, float *output, int count, bool hold) noexcept;
    void advancePlayhead(Voice &, double movement) noexcept;

  public:
    explicit Synth(SpscQueue<const PlaybackProgram *, 64> *returns = nullptr);
    ~Synth();
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void setControls(SynthControls) noexcept;
    void noteOn(int channel, int note, float velocity, const SpectralFrame *,
                const PlaybackProgram * = nullptr, const PlaybackSettings * = nullptr) noexcept;
    void noteOff(int channel, int note) noexcept;
    void pitchBend(int channel, float normalised) noexcept;
    void sustainPedal(int channel, bool down) noexcept;
    void allNotesOff(int channel, bool immediately) noexcept;
    void startPreview(const SpectralFrame *) noexcept;
    void stopPreview(bool immediately = false) noexcept;
    void render(float *left, float *right, int count) noexcept;
    int activeVoices() const noexcept;
    double voicePosition(int index) const noexcept {
        return voices[index].sourceTime;
    }
    bool usingAvx2() const noexcept {
        return avx2;
    }
    void disableAvx2() noexcept {
        avx2 = false;
    }
    static double pitchRatio(double note, double root, double bendSemitones = 0) noexcept;
};
} // namespace sineweave
