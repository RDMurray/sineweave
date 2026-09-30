#include "AllocationProbe.h"
#include "analysis/Analyser.h"
#include "core/FrameExchange.h"
#include "core/Model.h"
#include "core/Synth.h"
#include <Analyzer.h>
#include <Partial.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
using namespace sineweave;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Check failed: " #x);                                                   \
    } while (false)
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    CHECK(rejected);
}
AnalysedSound metadata() {
    AnalysedSound s;
    s.sourceSampleRate = 48000;
    s.sourceDuration = 1;
    s.endSeconds = 1;
    s.sourcePath = "missing.wav";
    return s;
}
double energy(const std::vector<float> &x) {
    double e = 0;
    for (float v : x)
        e += v * v;
    return e / x.size();
}
SpectralFrame tone(double freq) {
    return {.5, 1, {{1, freq, .5, 0, {}}}};
}
void modelTests() {
    auto s = metadata();
    s.partials = {{7, {{0, 100, .5, 3.1, .1}, {1, 200, .25, -3.1, .5}}}};
    validate(s);
    auto f = sampleFrame(s, .5);
    CHECK(f.partials.size() == 1);
    CHECK(std::abs(f.partials[0].frequencyHz - 150) < 1e-9);
    CHECK(std::abs(f.partials[0].amplitude - .375) < 1e-9);
    CHECK(std::abs(f.partials[0].phaseRadians - wrapPhase(3.1 + 2 * pi * .5 * 125)) < 1e-9);
    CHECK(std::abs(*f.partials[0].bandwidth - .3) < 1e-9);
    CHECK(sampleFrame(s, 0).partials.size() == 1);
    CHECK(sampleFrame(s, 1).partials.size() == 1);
    rejects([&] { sampleFrame(s, 1.1); });
    auto bytes = encodeState({s, f});
    auto restored = decodeState(bytes.data(), bytes.size());
    CHECK(restored.sound.sourcePath == "missing.wav");
    CHECK(restored.frame.partials[0].frequencyHz == 150);
    CHECK(restored.sound.partials[0].points.size() == 2);
    rejects([&] { decodeState(bytes.data(), bytes.size() - 1); });
    auto malformed = bytes;
    malformed[8] = 2;
    rejects([&] { decodeState(malformed.data(), malformed.size()); });
    for (std::size_t n = 0; n < bytes.size(); ++n)
        rejects([&] { decodeState(bytes.data(), n); });
    auto bad = s;
    bad.partials[0].points[1].time = 0;
    rejects([&] { validate(bad); });
    bad = s;
    bad.partials[0].points[0].frequencyHz = INFINITY;
    rejects([&] { validate(bad); });
    s.partials.push_back({2, {{0, 613.7, 2, 0, {}}, {1, 613.7, 2, 0, {}}}});
    auto limited = sampleFrame(s, .5, 1);
    CHECK(limited.activeCount == 2);
    CHECK(limited.partials[0].id == 2);
    CHECK(limited.partials[0].frequencyHz == 613.7);
    CHECK(limited.partials[0].amplitude == 1);
    CHECK(std::isfinite(wrapPhase(1e308)));
}
void analysisTests() {
    auto s = metadata();
    std::vector<double> pcm(48000);
    for (std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] =
            .4 * std::cos(2 * pi * 613.7 * i / 48000 + .2) + .3 * std::cos(2 * pi * 947.3 * i / 48000 - 1.1);
    auto result = LorisAnalyser{}.analyse(pcm, s);
    auto frame = sampleFrame(result, .5);
    bool first = false, second = false;
    for (const auto &p : frame.partials) {
        first |= std::abs(p.frequencyHz - 613.7) < 1;
        second |= std::abs(p.frequencyHz - 947.3) < 1;
        CHECK(p.bandwidth.has_value());
    }
    CHECK(first && second);
    rejects([&] { LorisAnalyser{sizeof(AnalysedSound) + 100}.analyse(pcm, s); });
    Loris::Analyzer reference(40, 80);
    reference.setAmpFloor(-80);
    auto partials = reference.analyze(pcm, 48000);
    std::size_t index = 0;
    for (const auto &p : partials) {
        if (p.size() == 0)
            continue;
        const auto &t = result.partials[index++];
        auto one = result;
        one.partials = {t};
        for (double time : {.251, .5, .749}) {
            auto sampled = sampleFrame(one, time);
            if (sampled.partials.empty())
                continue;
            const auto &q = sampled.partials[0];
            CHECK(std::abs(q.frequencyHz - p.frequencyAt(time)) < 1e-9);
            CHECK(std::abs(q.amplitude - p.amplitudeAt(time)) < 1e-9);
            CHECK(std::abs(wrapPhase(q.phaseRadians - p.phaseAt(time))) < 1e-8);
        }
    }
    std::fill(pcm.begin(), pcm.end(), 0);
    auto silent = LorisAnalyser{}.analyse(pcm, s);
    CHECK(sampleFrame(silent, .5).partials.empty());
    rejects([&] { LorisAnalyser{}.analyse({}, s); });
    s.startSeconds = .9;
    s.endSeconds = .1;
    rejects([&] { LorisAnalyser{}.analyse(pcm, s); });
}
void synthTests() {
    CHECK(std::abs(Synth::pitchRatio(81, 69) * 613.7 - 1227.4) < 1e-9);
    CHECK(std::abs(Synth::pitchRatio(69, 69, 12) - 2) < 1e-12);
    auto synth = std::make_unique<Synth>();
    SynthControls controls;
    controls.gainDb = 0;
    controls.attackMs = 0;
    controls.decayMs = 0;
    controls.sustain = 1;
    controls.releaseMs = 10;
    controls.voices = 2;
    synth->setControls(controls);
    synth->prepare(48000);
    auto f = tone(613.7);
    std::vector<float> left(48000), right(48000);
    synth->noteOn(0, 81, 1, &f);
    synth->render(left.data(), right.data(), 48000);
    CHECK(left == right);
    for (int i = 1000; i < 48000; ++i)
        CHECK(std::abs(left[i] - .5 * std::cos(2 * pi * 1227.4 * i / 48000)) < 5e-6);
    synth->noteOff(0, 81);
    synth->render(left.data(), right.data(), 1024);
    CHECK(synth->activeVoices() == 0);
    synth->reset();
    synth->noteOn(0, 69, .5f, &f);
    synth->render(left.data(), nullptr, 48000);
    CHECK(std::abs(energy(left) - .03125) < .0001);
    synth->sustainPedal(0, true);
    synth->noteOff(0, 69);
    synth->render(left.data(), nullptr, 1024);
    CHECK(synth->activeVoices() == 1);
    synth->sustainPedal(0, false);
    synth->render(left.data(), nullptr, 1024);
    CHECK(synth->activeVoices() == 0);
    synth->noteOn(0, 69, 1, &f);
    synth->noteOn(1, 70, 1, &f);
    synth->noteOn(2, 71, 1, &f);
    CHECK(synth->activeVoices() == 2);
    synth->allNotesOff(0, true);
    synth->allNotesOff(1, true);
    synth->allNotesOff(2, true);
    CHECK(synth->activeVoices() == 0);
    synth->reset();
    auto high = tone(24000);
    synth->noteOn(0, 69, 1, &high);
    synth->render(left.data(), nullptr, 48000);
    CHECK(energy(left) == 0);
    synth->reset();
    high = tone(22560);
    synth->noteOn(0, 69, 1, &high);
    synth->render(left.data(), nullptr, 48000);
    CHECK(std::abs(energy(left) - .03125) < .0001);
    synth->reset();
    synth->prepare(44100);
    high = tone(23000);
    synth->noteOn(0, 69, 1, &high);
    synth->render(left.data(), nullptr, 48000);
    CHECK(energy(left) == 0);
    synth->prepare(48000);
    synth->noteOn(0, 69, 1, &f);
    synth->pitchBend(0, 1);
    synth->render(left.data(), nullptr, 48000);
    const auto bent = 613.7 * std::exp2(2. / 12);
    double correlationCos = 0, correlationSin = 0;
    for (int i = 24000; i < 48000; ++i) {
        correlationCos += left[i] * std::cos(2 * pi * bent * i / 48000);
        correlationSin += left[i] * std::sin(2 * pi * bent * i / 48000);
    }
    CHECK(std::hypot(correlationCos, correlationSin) / 24000 > .24);
    {
        probe::AudioScope guard;
        for (int i = 0; i < 32; ++i) {
            synth->noteOn(i % 16, 60 + i, 1, &f);
            synth->render(left.data(), right.data(), 128);
            synth->noteOff(i % 16, 60 + i);
        }
        synth->allNotesOff(0, true);
    }
    CHECK(probe::allocations == 0 && probe::deallocations == 0);
    synth->prepare(48000);
    synth->noteOn(0, 69, 1, &f);
    synth->render(left.data(), nullptr, 48000);
    synth->disableAvx2();
    synth->prepare(48000);
    synth->noteOn(0, 69, 1, &f);
    synth->render(right.data(), nullptr, 48000);
    for (std::size_t i = 0; i < left.size(); ++i)
        CHECK(std::abs(left[i] - right[i]) < 5e-6);

    // Recover the envelope from a known cosine at non-zero points.
    controls.attackMs = 10;
    controls.decayMs = 100;
    controls.sustain = .25f;
    controls.releaseMs = 250;
    synth->setControls(controls);
    synth->prepare(48000);
    f = tone(100);
    synth->noteOn(0, 69, 1, &f);
    synth->render(left.data(), nullptr, 12000);
    auto envelopeAt = [&](int i) { return left[i] / (.5 * std::cos(2 * pi * 100 * i / 48000)); };
    CHECK(std::abs(envelopeAt(239) - .5) < .001);
    CHECK(std::abs(envelopeAt(479) - 1) < .001);
    CHECK(std::abs(envelopeAt(2879) - .625) < .001);
    CHECK(std::abs(envelopeAt(6000) - .25) < .001);
    synth->noteOff(0, 69);
    synth->render(left.data(), nullptr, 12001);
    CHECK(synth->activeVoices() == 0);

    controls.attackMs = controls.decayMs = 0;
    controls.sustain = 1;
    synth->setControls(controls);
    synth->prepare(48000);
    synth->noteOn(0, 69, 1, &f);
    synth->noteOn(1, 70, .1f, &f);
    synth->render(left.data(), nullptr, 128);
    synth->noteOff(0, 69);
    synth->noteOff(1, 70);
    synth->noteOn(2, 71, 1, &f); // Quietest releasing voice (channel 1) is stolen.
    synth->allNotesOff(0, true);
    synth->allNotesOff(2, true);
    CHECK(synth->activeVoices() == 0);
    synth->prepare(48000);
    synth->noteOn(0, 69, 1, &f);
    synth->noteOn(1, 70, 1, &f);
    synth->noteOn(2, 71, 1, &f); // Oldest active voice (channel 0) is stolen.
    synth->allNotesOff(1, true);
    synth->allNotesOff(2, true);
    CHECK(synth->activeVoices() == 0);
}
void previewTests() {
    auto synth = std::make_unique<Synth>();
    SynthControls c;
    c.gainDb = 0;
    c.referenceNote = 69.5f;
    c.attackMs = c.decayMs = 0;
    c.sustain = 1;
    c.voices = 1;
    synth->setControls(c);
    synth->prepare(48000);
    auto frame = tone(613.7);
    std::vector<float> preview(20000), baseline(20000);
    {
        probe::AudioScope guard;
        synth->pitchBend(0, 1);
        synth->startPreview(&frame);
        synth->render(preview.data(), nullptr, 20000);
    }
    CHECK(probe::allocations == 0 && probe::deallocations == 0);
    for (int i = 1000; i < 11000; ++i)
        CHECK(std::abs(preview[i] - .4 * std::cos(2 * pi * 613.7 * i / 48000)) < 5e-6);
    for (int i = 15000; i < 20000; ++i)
        CHECK(preview[i] == 0);
    CHECK(synth->activeVoices() == 0);

    synth->prepare(48000);
    synth->noteOn(0, 69, 1, &frame);
    synth->startPreview(&frame);
    CHECK(synth->activeVoices() == 1);
    synth->render(preview.data(), nullptr, 20000);
    synth->prepare(48000);
    synth->noteOn(0, 69, 1, &frame);
    synth->render(baseline.data(), nullptr, 20000);
    for (int i = 15000; i < 20000; ++i)
        CHECK(std::abs(preview[i] - baseline[i]) < 5e-6); // MIDI voice survives audition.
    synth->reset();
    {
        probe::AudioScope guard;
        for (int i = 0; i < 12; ++i) {
            synth->startPreview(&frame);
            synth->render(preview.data(), nullptr, 128);
        }
        synth->stopPreview();
        synth->render(preview.data(), nullptr, 1024);
    }
    CHECK(probe::allocations == 0 && probe::deallocations == 0);
    for (int i = 512; i < 1024; ++i)
        CHECK(preview[i] == 0);
    SpectralFrame silent;
    synth->startPreview(&silent);
    synth->render(preview.data(), nullptr, 1024);
    CHECK(energy(std::vector<float>(preview.begin(), preview.begin() + 1024)) == 0);
}
void exchangeTests() {
    FrameExchange exchange;
    auto publish = [&](std::uint64_t gen, double frequency) {
        auto p = std::make_unique<PreparedFrame>();
        p->generation = gen;
        p->frame = tone(frequency);
        return exchange.publish(p);
    };
    CHECK(publish(1, 100));
    {
        probe::AudioScope guard;
        CHECK(exchange.adopt(1)->partials[0].frequencyHz == 100);
    }
    CHECK(probe::allocations == 0 && probe::deallocations == 0);
    for (int i = 0; i < 3; ++i) {
        CHECK(publish(2, 200 + i));
        probe::AudioScope guard;
        exchange.adopt(2);
    }
    CHECK(probe::allocations == 0 && probe::deallocations == 0);
    CHECK(publish(3, 300));
    {
        probe::AudioScope guard;
        CHECK(exchange.adopt(3)->partials[0].frequencyHz == 202);
    }
    CHECK(probe::deallocations == 0);
    exchange.reclaim();
    CHECK(exchange.adopt(3)->partials[0].frequencyHz == 300);
    exchange.reclaim();
    CHECK(publish(3, 999));
    CHECK(exchange.adopt(4)->partials[0].frequencyHz == 300);
    exchange.reclaim();
    auto audition = std::make_unique<PreparedFrame>();
    audition->generation = 4;
    audition->frame = tone(400);
    audition->previewOnAdopt = true;
    CHECK(exchange.publish(audition));
    bool preview = false;
    exchange.adopt(4, &preview);
    CHECK(preview);
    exchange.adopt(4, &preview);
    CHECK(!preview); // Only the adoption triggers, not subsequent audio blocks.
    exchange.reclaim();
    std::atomic<bool> done{false};
    std::thread producer([&] {
        for (int i = 0; i < 1000; ++i) {
            auto p = std::make_unique<PreparedFrame>();
            p->generation = 5;
            p->frame = tone(i + 1);
            while (!exchange.publish(p)) {
                exchange.reclaim();
                std::this_thread::yield();
            }
            exchange.reclaim();
        }
        done = true;
    });
    while (!done) {
        probe::AudioScope guard;
        exchange.adopt(5);
    }
    producer.join();
    for (int i = 0; i < 4; ++i) {
        exchange.reclaim();
        exchange.adopt(5);
    }
    exchange.reclaim();
}
void playbackTests() {
    auto sound = metadata();
    sound.partials = {{9, {{0, 613.7, .5, .2, .1}, {1, 813.7, .25, .3, .5}}}};
    auto program = compilePlayback(sound, 4096);
    CHECK(program->lanes.size() == 1 && program->attenuation == 1);
    auto overflow = sound;
    for (auto &p : overflow.partials[0].points)
        p.amplitude = 1e308;
    overflow.partials.push_back(overflow.partials[0]);
    overflow.partials.back().id = 10;
    rejects([&] { compilePlayback(overflow, 2); });
    int point = -1;
    auto mid = trajectoryAt(sound.partials[0], .5, point);
    CHECK(std::abs(mid.frequencyHz - 713.7) < 1e-8 && std::abs(*mid.bandwidth - .3) < 1e-8);
    CHECK(std::abs(mid.phaseRadians - sampleFrame(sound, .5).partials[0].phaseRadians) < 1e-8);
    rejects([&] { compilePlayback(sound, 1, 64); });
    PlaybackSettings cfg{0, 1, .2, .6, 20, 1, LoopMode::off};
    rejects([&] {
        auto bad = cfg;
        bad.start = 1;
        validatePlayback(bad, 0, 1);
    });
    rejects([&] {
        auto bad = cfg;
        bad.loop = LoopMode::wrap;
        bad.loopEnd = bad.loopStart;
        validatePlayback(bad, 0, 1);
    });
    auto synth = std::make_unique<Synth>();
    auto scalar = std::make_unique<Synth>();
    scalar->disableAvx2();
    SynthControls controls;
    controls.gainDb = 0;
    controls.attackMs = 0;
    controls.decayMs = 0;
    controls.releaseMs = 20;
    std::vector<float> a(24000), b(24000);
    synth->prepare(48000);
    scalar->prepare(48000);
    synth->setControls(controls);
    scalar->setControls(controls);
    {
        probe::AudioScope guard;
        synth->noteOn(0, 69, 1, nullptr, program.get(), &cfg);
        scalar->noteOn(0, 69, 1, nullptr, program.get(), &cfg);
        synth->render(a.data(), nullptr, 12000);
        scalar->render(b.data(), nullptr, 12000);
    }
    CHECK(probe::allocations == 0 && probe::deallocations == 0);
    CHECK(std::abs(synth->voicePosition(0) - .25) < 1e-8);
    double error = 0;
    for (int i = 0; i < 12000; ++i)
        error = std::max(error, std::abs(static_cast<double>(a[i] - b[i])));
    CHECK(error < 1e-5);
    // Exact integrated chirp phase after the initial gain/attack fades.
    for (int i = 5000; i < 10000; i += 101) {
        const double t = i / 48000.;
        const double expected = (.5 - .25 * t) * std::cos(.2 + 2 * pi * (613.7 * t + 100 * t * t));
        CHECK(std::abs(a[i] - expected) < .002);
    }
    synth->reset();
    scalar->reset();
    CHECK(program->references == 0);
    controls.speed = .5;
    synth->setControls(controls);
    synth->noteOn(0, 69, 1, nullptr, program.get(), &cfg);
    synth->render(a.data(), nullptr, 12000);
    CHECK(std::abs(synth->voicePosition(0) - .125) < 1e-8);
    for (int i = 5000; i < 10000; i += 101) {
        const double t = i / 48000.;
        const double expected = (.5 - .125 * t) * std::cos(.2 + 2 * pi * (613.7 * t + 50 * t * t));
        CHECK(std::abs(a[i] - expected) < .002); // Half-speed evolution still starts at 613.7 Hz.
    }
    synth->noteOn(1, 81, .5, nullptr, program.get(), &cfg);
    synth->render(a.data(), nullptr, 2400);
    CHECK(std::abs(synth->voicePosition(1) - .025) < 1e-8);
    CHECK(std::abs(synth->voicePosition(0) - .15) < 1e-8);
    controls.speed = 0;
    synth->setControls(controls);
    synth->render(a.data(), nullptr, 2400); // 50 ms linear speed ramp.
    const double held = synth->voicePosition(0);
    CHECK(held > .162 && held < .163);
    synth->render(a.data(), nullptr, 2400);
    CHECK(synth->voicePosition(0) == held && energy(std::vector<float>(a.begin(), a.begin() + 2400)) > .01);
    synth->pitchBend(0, 1);
    controls.referenceNote = 69.5f;
    synth->setControls(controls);
    synth->render(a.data(), nullptr, 2400);
    CHECK(synth->voicePosition(0) == held);
    synth->reset();
    controls.speed = 1;
    controls.referenceNote = 69;
    synth->setControls(controls);
    cfg.direction = -1;
    synth->noteOn(0, 69, 1, nullptr, program.get(), &cfg);
    synth->render(a.data(), nullptr, 12000);
    CHECK(std::abs(synth->voicePosition(0) - .75) < 1e-8);
    // Reverse oscillator phase advances negatively, independently of source speed.
    for (int i = 5000; i < 10000; i += 101) {
        const double t = i / 48000.;
        const double expected = (.25 + .25 * t) * std::cos(.3 - 2 * pi * (813.7 * t - 100 * t * t));
        CHECK(std::abs(a[i] - expected) < .002);
    }
    synth->reset();
    cfg.direction = 1;
    cfg.loop = LoopMode::wrap;
    synth->noteOn(0, 69, 1, nullptr, program.get(), &cfg);
    for (int i = 0; i < 7; ++i)
        synth->render(a.data(), nullptr, 4800);
    CHECK(std::abs(synth->voicePosition(0) - .3) < 1e-8); // Intro then wrap.
    cfg.direction = -1;
    cfg.loop = LoopMode::pingPong; // Captured configuration remains unchanged.
    synth->render(a.data(), nullptr, 4800);
    CHECK(std::abs(synth->voicePosition(0) - .4) < 1e-8);
    synth->noteOn(1, 69, 1, nullptr, program.get(), &cfg);
    synth->render(a.data(), nullptr, 4800);
    CHECK(std::abs(synth->voicePosition(1) - .9) < 1e-8);
    synth->reset();
    cfg.direction = 1;
    cfg.crossfadeMs = 0;
    synth->noteOn(0, 69, 1, nullptr, program.get(), &cfg);
    for (int i = 0; i < 7; ++i)
        synth->render(a.data(), nullptr, 4800);
    CHECK(std::abs(synth->voicePosition(0) - .5) < 1e-8); // Ping-pong reflected at .6.
    for (int i = 0; i < 4; ++i)
        synth->render(a.data(), nullptr, 4800);
    CHECK(std::abs(synth->voicePosition(0) - .3) < 1e-8); // Reflected at .2.
    synth->sustainPedal(0, true);
    synth->noteOff(0, 69);
    synth->render(a.data(), nullptr, 2400);
    CHECK(synth->activeVoices() == 1);
    synth->sustainPedal(0, false);
    synth->render(a.data(), nullptr, 2400);
    CHECK(synth->activeVoices() == 0 && program->references == 0);
    // Very short loops, all loop types/directions, repeated stealing and retirement stay bounded.
    controls.voices = 1;
    controls.speed = 4;
    synth->setControls(controls);
    cfg.loopStart = .2;
    cfg.loopEnd = .201;
    cfg.crossfadeMs = 100;
    {
        probe::AudioScope guard;
        for (int d : {-1, 1})
            for (auto mode : {LoopMode::wrap, LoopMode::pingPong}) {
                cfg.direction = d;
                cfg.loop = mode;
                cfg.start = .2;
                cfg.end = .201;
                for (int i = 0; i < 20; ++i) {
                    synth->noteOn(0, 60 + i, 1, nullptr, program.get(), &cfg);
                    synth->render(a.data(), nullptr, 512);
                    CHECK(synth->voicePosition(0) >= .2 - 1e-9 && synth->voicePosition(0) <= .201 + 1e-9);
                }
                synth->allNotesOff(0, true);
            }
    }
    CHECK(probe::allocations == 0 && probe::deallocations == 0 && program->references == 0);
    synth->reset();
    controls.voices = 16;
    controls.speed = 1;
    synth->setControls(controls);
    cfg = {0, .05, 0, .05, 20, 1, LoopMode::off};
    synth->noteOn(0, 69, 1, nullptr, program.get(), &cfg);
    synth->render(a.data(), nullptr, 3000);
    CHECK(std::abs(synth->voicePosition(0) - .05) < 1e-8 && synth->activeVoices() == 1);
    synth->render(a.data(), nullptr, 2400);
    CHECK(synth->activeVoices() == 0 && program->references == 0);
    // Reuse a lane after a death, with deterministic peak/ID priority during overlap.
    sound.partials = {{10, {{0, 100, 2, 0, {}}, {.4, 100, 2, 0, {}}}},
                      {2, {{.4, 200, 2, 0, {}}, {1, 200, 2, 0, {}}}},
                      {7, {{.2, 300, 2, 0, {}}, {.8, 300, 2, 0, {}}}}};
    program = compilePlayback(sound, 1);
    CHECK(program->lanes.size() == 1 && program->limited && program->tracks.size() == 3);
    CHECK(program->lanes[0].intervals.size() == 3 && program->attenuation == .5);
    CHECK(program->tracks[program->lanes[0].intervals[1].track].id == 7);
    CHECK(program->tracks[program->lanes[0].intervals[2].track].id == 2);
    CHECK(laneIntervalAt(program->lanes[0], .4, 1, -1) == 2);
    CHECK(laneIntervalAt(program->lanes[0], .4, -1, -1) == 1);
    // Two disjoint peaks require attenuation 1/2, rather than 1/4.
    sound.partials.pop_back();
    program = compilePlayback(sound, 2);
    CHECK(program->attenuation == .5);
    auto silence = metadata();
    auto empty = compilePlayback(silence, 4096);
    CHECK(empty->lanes.empty());
    // A birth/death gap produces silence in either travel direction, with later lane reuse.
    sound.partials = {{1, {{.1, 613.7, .5, 0, {}}, {.3, 613.7, .5, 0, {}}}},
                      {2, {{.7, 947.3, .5, 0, {}}, {.9, 947.3, .5, 0, {}}}}};
    program = compilePlayback(sound, 1);
    CHECK(program->lanes.size() == 1 && !program->limited);
    cfg = {0, 1, 0, 1, 20, 1, LoopMode::off};
    for (int direction : {1, -1}) {
        synth->reset();
        cfg.direction = direction;
        synth->noteOn(0, 69, 1, nullptr, program.get(), &cfg);
        synth->render(a.data(), nullptr, 2400);
        CHECK(energy(std::vector<float>(a.begin(), a.begin() + 2400)) == 0);
        synth->render(a.data(), nullptr, 9600);
        CHECK(energy(std::vector<float>(a.begin(), a.begin() + 9600)) > .03);
        synth->render(a.data(), nullptr, 24000);
        CHECK(energy(std::vector<float>(a.begin() + 4000, a.begin() + 16000)) == 0);
    }
    synth->reset();
    // Lease retirement and repeated replacement cannot destroy a held note's old program.
    SpscQueue<const PlaybackProgram *, 64> returns;
    auto leased = std::make_unique<Synth>(&returns);
    leased->setControls(controls);
    FrameExchange exchange;
    auto prepared = std::make_unique<PreparedFrame>();
    prepared->generation = 1;
    prepared->attach(program.get());
    CHECK(exchange.publish(prepared));
    exchange.adopt(1);
    cfg.direction = 1;
    cfg.loop = LoopMode::wrap;
    leased->noteOn(0, 69, 1, nullptr, exchange.prepared()->program, &cfg);
    CHECK(program->references == 2);
    prepared = std::make_unique<PreparedFrame>();
    prepared->generation = 2;
    prepared->attach(empty.get());
    CHECK(exchange.publish(prepared));
    exchange.adopt(2);
    exchange.reclaim();
    CHECK(program->references == 1);
    {
        probe::AudioScope guard;
        leased->render(a.data(), nullptr, 2400);
        for (int i = 0; i < 100; ++i) {
            leased->noteOn(0, 60 + i % 12, 1, nullptr, program.get(), &cfg);
            leased->allNotesOff(0, true);
        }
    }
    CHECK(probe::allocations == 0 && probe::deallocations == 0 && program->references == 0);
    const PlaybackProgram *returned;
    int tokens = 0;
    while (returns.pop(returned))
        ++tokens;
    CHECK(tokens == 63); // Bounded retirement queue fills; registry ownership remains valid.
    // Trajectory mode shares fractional root/bend tuning and sample-rate safety with frozen mode.
    auto constant = metadata();
    constant.partials = {{1, {{0, 613.7, .5, 0, {}}, {1, 613.7, .5, 0, {}}}}};
    auto pitched = compilePlayback(constant, 1);
    cfg = {0, 1, 0, 1, 0, 1, LoopMode::off};
    synth->reset();
    controls.speed = 0;
    controls.referenceNote = 69.5f;
    synth->setControls(controls);
    synth->pitchBend(0, 1);
    synth->noteOn(0, 69, 1, nullptr, pitched.get(), &cfg);
    synth->render(a.data(), nullptr, 12000);
    const double ratio = Synth::pitchRatio(69, 69.5, 2);
    for (int i = 5000; i < 10000; i += 101)
        CHECK(std::abs(a[i] - .5 * std::cos(2 * pi * 613.7 * ratio * i / 48000)) < 5e-5);
    synth->reset();
    controls.referenceNote = 69;
    synth->setControls(controls);
    synth->noteOn(0, 81, 1, nullptr, pitched.get(), &cfg);
    synth->render(a.data(), nullptr, 12000);
    for (int i = 5000; i < 10000; i += 101)
        CHECK(std::abs(a[i] - .5 * std::cos(2 * pi * 1227.4 * i / 48000)) < 5e-5);
    synth->reset();
    controls.speed = 4;
    synth->setControls(controls);
    cfg = {.2, .201, .2, .201, 100, 1, LoopMode::wrap};
    synth->noteOn(0, 69, 1, nullptr, pitched.get(), &cfg);
    synth->render(a.data(), nullptr, 12000);
    CHECK(energy(std::vector<float>(a.begin() + 5000, a.begin() + 10000)) > .01);
    synth->reset();
    controls.speed = 0;
    cfg = {0, 1, 0, 1, 0, 1, LoopMode::off};
    for (double rate : {32000., 48000., 96000.}) {
        constant.sourceSampleRate = rate * 2;
        constant.partials[0].points[0].frequencyHz = constant.partials[0].points[1].frequencyHz = rate * .5;
        auto ultrasonic = compilePlayback(constant, 1);
        synth->prepare(rate);
        synth->setControls(controls);
        synth->noteOn(0, 69, 1, nullptr, ultrasonic.get(), &cfg);
        synth->render(a.data(), nullptr, 12000);
        CHECK(energy(std::vector<float>(a.begin(), a.begin() + 12000)) == 0);
        synth->reset();
    }
}
int main() {
    try {
        modelTests();
        analysisTests();
        synthTests();
        previewTests();
        exchangeTests();
        playbackTests();
        std::cout << "Core model, Loris, synth, state, and realtime ownership checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
