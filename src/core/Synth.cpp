// SPDX-License-Identifier: AGPL-3.0-only
#include "Synth.h"
#include <algorithm>
#include <cmath>
#if SINEWEAVE_AVX2_AVAILABLE
#include "OscillatorKernel.h"
#include <intrin.h>
static bool supportsAvx2() noexcept {
    int cpu[4];
    __cpuid(cpu, 0);
    if (cpu[0] < 7)
        return false;
    __cpuid(cpu, 1);
    if ((cpu[2] & (1 << 27)) == 0 || (cpu[2] & (1 << 28)) == 0)
        return false;
    if ((_xgetbv(0) & 6) != 6)
        return false;
    __cpuidex(cpu, 7, 0);
    return (cpu[1] & (1 << 5)) != 0;
}
#endif
#if defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#define SINEWEAVE_SSE2 1
#endif
namespace sineweave {
Synth::Synth(SpscQueue<const PlaybackProgram *, 64> *returns) : retirements(returns) {
#if SINEWEAVE_AVX2_AVAILABLE
    avx2 = supportsAvx2();
#endif
    for (int i = 0; i <= tableSize; ++i)
        table[i] = static_cast<float>(std::cos(2 * pi * i / tableSize));
    prepare(48000);
}
Synth::~Synth() {
    reset();
}
void Synth::finishVoice(Voice &v) noexcept {
    v.stage = Stage::idle;
    if (v.program) {
        // The worker still owns every registered program. If this bounded notification
        // queue fills, its registry scan defers reclamation until the next worker cycle.
        if (retirements)
            retirements->push(v.program);
        v.program->release();
        v.program = nullptr;
    }
    v.crossfadeLeft = 0;
}
void Synth::prepare(double r) noexcept {
    rate = std::isfinite(r) && r >= 8000 && r <= 768000 ? r : 48000;
    smoothing = 1 - std::exp(-1 / (.005 * rate));
    reset();
    gain = targetGain = std::pow(10.f, controls.gainDb / 20);
}
void Synth::reset() noexcept {
    for (auto &v : voices)
        finishVoice(v);
    for (auto &v : tails)
        finishVoice(v);
    finishVoice(previewVoice);
    finishVoice(previewTail);
    bend.fill(0);
    pedal.fill(false);
    clock = 0;
}
double Synth::pitchRatio(double note, double root, double b) noexcept {
    return std::exp2((note - root + b) / 12);
}
void Synth::retune(Voice &v) noexcept {
    v.targetRatio = pitchRatio(v.note, controls.referenceNote, bend[v.channel] * controls.bendRange);
}
void Synth::retime(Voice &v) noexcept {
    if (v.program && v.speedTarget != controls.speed) {
        v.speedTarget = controls.speed;
        v.speedRampLeft = std::max(1, static_cast<int>(.05 * rate));
        v.speedDelta = (v.speedTarget - v.sourceSpeed) / v.speedRampLeft;
    }
}
void Synth::setControls(SynthControls c) noexcept {
    c.voices = std::clamp(c.voices, 1, maxVoices);
    c.speed = std::isfinite(c.speed) ? std::clamp(c.speed, 0.f, 4.f) : 1.f;
    controls = c;
    targetGain = std::pow(10.f, c.gainDb / 20);
    for (int i = 0; i < maxVoices; ++i) {
        auto &v = voices[i];
        if (i >= c.voices && v.stage != Stage::idle) {
            makeTail(v);
            finishVoice(v);
        } else if (v.stage != Stage::idle) {
            retune(v);
            retime(v);
        }
    }
    for (auto &v : tails)
        if (v.stage != Stage::idle) {
            retune(v);
            retime(v);
        }
}
void Synth::release(Voice &v) noexcept {
    v.stage = Stage::release;
    v.releaseStep = v.envelope / static_cast<float>(std::max(1., controls.releaseMs * .001 * rate));
}
void Synth::makeTail(Voice &v) noexcept {
    // An exhausted tail pool reuses its quietest tail. Bounded memory and work.
    auto *tail = &tails[0];
    for (auto &t : tails) {
        if (t.stage == Stage::idle) {
            tail = &t;
            break;
        }
        if (t.envelope * t.velocity < tail->envelope * tail->velocity)
            tail = &t;
    }
    copyTail(v, *tail);
}
void Synth::copyTail(const Voice &v, Voice &destination) noexcept {
    finishVoice(destination);
    auto *tail = &destination;
    copyBank(v, destination);
    tail->program = v.program;
    if (tail->program)
        tail->program->retain();
    tail->playback = v.playback;
    tail->sourceTime = v.sourceTime;
    tail->sourceSpeed = v.sourceSpeed;
    tail->speedTarget = v.speedTarget;
    tail->speedDelta = v.speedDelta;
    tail->speedRampLeft = v.speedRampLeft;
    tail->direction = v.direction;
    tail->atEnd = v.atEnd;
    tail->crossfadeLeft = v.crossfadeLeft;
    tail->crossfadeTotal = v.crossfadeTotal;
    tail->overlapDirection = v.overlapDirection;
    if (v.crossfadeLeft)
        copyBank(v.overlap, destination.overlap);
    tail->note = v.note;
    tail->channel = v.channel;
    tail->velocity = v.velocity;
    tail->ratio = v.ratio;
    tail->targetRatio = v.targetRatio;
    tail->cachedRatio = -1;
    tail->envelope = v.envelope;
    tail->stage = Stage::release;
    tail->tailSamples = std::max(1, static_cast<int>(.005 * rate));
    tail->releaseStep = tail->envelope / tail->tailSamples;
    tail->preview = v.preview;
}
void Synth::copyBank(const Bank &source, Bank &destination) noexcept {
    destination.count = source.count;
    destination.cachedRatio = -1;
    destination.fadeEntries = source.fadeEntries;
    std::copy_n(source.frequency.begin(), source.count, destination.frequency.begin());
    std::copy_n(source.phase.begin(), source.count, destination.phase.begin());
    std::copy_n(source.amplitude.begin(), source.count, destination.amplitude.begin());
    std::copy_n(source.track.begin(), source.count, destination.track.begin());
    std::copy_n(source.interval.begin(), source.count, destination.interval.begin());
    std::copy_n(source.point.begin(), source.count, destination.point.begin());
    std::copy_n(source.laneGain.begin(), source.count, destination.laneGain.begin());
}
void Synth::noteOn(int ch, int note, float velocity, const SpectralFrame *frame,
                   const PlaybackProgram *program, const PlaybackSettings *playback) noexcept {
    if (ch < 0 || ch >= 16 || note < 0 || note > 127)
        return;
    if (velocity <= 0) {
        noteOff(ch, note);
        return;
    }
    if (!playback)
        program = nullptr;
    if ((!program || !playback) && (!frame || frame->partials.empty()))
        return;
    Voice *chosen = nullptr;
    for (int i = 0; i < controls.voices; ++i)
        if (voices[i].stage == Stage::idle) {
            chosen = &voices[i];
            break;
        }
    if (!chosen)
        for (int i = 0; i < controls.voices; ++i) {
            auto &v = voices[i];
            if (v.stage == Stage::release &&
                (!chosen || v.envelope * v.velocity < chosen->envelope * chosen->velocity))
                chosen = &v;
        }
    if (!chosen) {
        chosen = &voices[0];
        for (int i = 1; i < controls.voices; ++i)
            if (voices[i].age < chosen->age)
                chosen = &voices[i];
    }
    auto &v = *chosen;
    if (v.stage != Stage::idle)
        makeTail(v);
    const SpectralFrame empty;
    startVoice(v, ch, note, velocity, program ? empty : *frame);
    if (program && playback) {
        v.program = program;
        program->retain();
        v.playback = *playback;
        v.direction = playback->direction;
        v.sourceTime = v.direction > 0 ? playback->start : playback->end;
        v.sourceSpeed = controls.speed;
        v.speedTarget = controls.speed;
        v.speedRampLeft = 0;
        v.atEnd = false;
        v.fadeEntries = true;
        v.count = static_cast<int>(program->lanes.size());
        std::fill_n(v.track.begin(), v.count, -1);
        std::fill_n(v.interval.begin(), v.count, -1);
        std::fill_n(v.point.begin(), v.count, -1);
        std::fill_n(v.laneGain.begin(), v.count, 0.f);
    }
}
void Synth::startVoice(Voice &v, int ch, int note, float velocity, const SpectralFrame &frame) noexcept {
    finishVoice(v);
    v.count = std::min(static_cast<int>(frame.partials.size()), maxPartials);
    for (int i = 0; i < v.count; ++i) {
        const auto &p = frame.partials[i];
        v.frequency[i] = p.frequencyHz;
        v.phase[i] = p.phaseRadians / (2 * pi);
        v.phase[i] -= std::floor(v.phase[i]);
        v.amplitude[i] = static_cast<float>(p.amplitude);
    }
    v.note = note;
    v.channel = ch;
    v.age = ++clock;
    v.keyDown = true;
    v.velocity = std::clamp(velocity, 0.f, 1.f);
    v.envelope = 0;
    v.stage = Stage::attack;
    v.cachedRatio = -1;
    v.preview = false;
    retune(v);
    v.ratio = v.targetRatio;
}
void Synth::startPreview(const SpectralFrame *frame) noexcept {
    stopPreview();
    if (!frame || frame->partials.empty())
        return;
    startVoice(previewVoice, 0, 69, .8f, *frame);
    // Audition at the exact reference pitch, including fractional MIDI roots:
    // the selected frame's frequencies are unchanged and ignore MIDI bend.
    previewVoice.ratio = previewVoice.targetRatio = 1;
    previewVoice.preview = true;
    previewVoice.previewHoldSamples = std::max(1, static_cast<int>(.25 * rate));
}
void Synth::stopPreview(bool immediately) noexcept {
    if (!immediately && previewVoice.stage != Stage::idle)
        copyTail(previewVoice, previewTail);
    previewVoice.stage = Stage::idle;
    if (immediately)
        previewTail.stage = Stage::idle;
}
void Synth::noteOff(int ch, int note) noexcept {
    if (ch < 0 || ch >= 16)
        return;
    for (auto &v : voices)
        if (v.stage != Stage::idle && v.channel == ch && v.note == note && v.keyDown) {
            v.keyDown = false;
            if (!pedal[ch])
                release(v);
        }
}
void Synth::pitchBend(int ch, float b) noexcept {
    if (ch < 0 || ch >= 16)
        return;
    bend[ch] = std::clamp(b, -1.f, 1.f);
    for (auto &v : voices)
        if (v.stage != Stage::idle && v.channel == ch)
            retune(v);
    for (auto &v : tails)
        if (v.stage != Stage::idle && v.channel == ch)
            retune(v);
}
void Synth::sustainPedal(int ch, bool down) noexcept {
    if (ch < 0 || ch >= 16)
        return;
    pedal[ch] = down;
    if (!down)
        for (auto &v : voices)
            if (v.stage != Stage::idle && v.channel == ch && !v.keyDown && v.stage != Stage::release)
                release(v);
}
void Synth::allNotesOff(int ch, bool now) noexcept {
    if (ch < 0 || ch >= 16)
        return;
    pedal[ch] = false;
    for (auto &v : voices)
        if (v.stage != Stage::idle && v.channel == ch) {
            v.keyDown = false;
            if (now)
                finishVoice(v);
            else
                release(v);
        }
    if (now)
        for (auto &v : tails)
            if (v.channel == ch)
                finishVoice(v);
}
float Synth::tick(Voice &v, bool tail) noexcept {
    if (v.stage == Stage::idle)
        return 0;
    if (v.preview && !tail && v.stage != Stage::release && --v.previewHoldSamples <= 0) {
        v.stage = Stage::release;
        v.releaseStep = v.envelope / static_cast<float>(std::max(1., .05 * rate));
    }
    const auto sustain = v.preview ? 1.f : controls.sustain;
    switch (v.stage) {
    case Stage::attack:
        v.envelope +=
            1.f / static_cast<float>(std::max(1., (v.preview ? 10. : controls.attackMs) * .001 * rate));
        if (v.envelope >= 1) {
            v.envelope = 1;
            v.stage = Stage::decay;
        }
        break;
    case Stage::decay:
        v.envelope -= (1 - sustain) / static_cast<float>(std::max(1., controls.decayMs * .001 * rate));
        if (v.envelope <= sustain) {
            v.envelope = sustain;
            v.stage = Stage::sustain;
        }
        break;
    case Stage::sustain:
        v.envelope = sustain;
        break;
    case Stage::release:
        v.envelope -= v.releaseStep;
        if (v.envelope <= 0 || (tail && --v.tailSamples <= 0)) {
            v.envelope = 0;
            v.stage = Stage::idle;
            return 0;
        }
        break;
    case Stage::idle:
        return 0;
    }
    return v.envelope * v.velocity;
}
float Synth::cosine(double cycles) const noexcept {
    // Bounded source frequencies and MIDI ratios make truncation safe here.
    // Avoid the MSVC floor() library call in the oscillator hot path.
    cycles -= static_cast<std::int64_t>(cycles);
    if (cycles < 0)
        cycles += 1;
    const double position = cycles * tableSize;
    const int index = static_cast<int>(position);
    const float fraction = static_cast<float>(position - index);
    return table[index] + fraction * (table[index + 1] - table[index]);
}
void Synth::renderVoice(Voice &v, float *output, int count, bool tail) noexcept {
    if (v.stage == Stage::idle)
        return;
    if (v.program) {
        renderTrajectory(v, output, count, tail);
        return;
    }
    float envelopes[256], bank[256]{};
    // During pitch ramps use <=16-sample tiles; constant pitch uses <=256. Phase is
    // integrated per tile in double. Recurrence is re-seeded every tile, bounding
    // float drift rather than accumulating it across a sustained note.
    int cursor = 0;
    while (cursor < count && v.stage != Stage::idle) {
        const int n = std::min(count - cursor, std::abs(v.ratio - v.targetRatio) > 1e-9 ? 16 : 256);
        double ratioSum = 0;
        for (int j = 0; j < n; ++j) {
            envelopes[j] = tick(v, tail);
            v.ratio += (v.targetRatio - v.ratio) * smoothing;
            ratioSum += v.ratio;
            bank[j] = 0;
        }
        const double ratioPerSample = ratioSum / (n * rate);
        if (ratioPerSample != v.cachedRatio) {
            v.cachedRatio = ratioPerSample;
            for (int i = 0; i < v.count; ++i) {
                const double step = v.frequency[i] * ratioPerSample;
                v.step[i] = step;
                v.filteredAmplitude[i] =
                    v.amplitude[i] * static_cast<float>(std::clamp((.49 - step) / .04, 0., 1.));
                auto &re = v.offsetsReal[i];
                auto &im = v.offsetsImag[i];
                re[0] = 1;
                im[0] = 0;
                const float c = cosine(step), s = cosine(step - .25);
                for (int lane = 1; lane < 4; ++lane) {
                    re[lane] = re[lane - 1] * c - im[lane - 1] * s;
                    im[lane] = im[lane - 1] * c + re[lane - 1] * s;
                }
                v.rotationReal[i] = cosine(4 * step);
                v.rotationImag[i] = cosine(4 * step - .25);
            }
        }
        for (int i = 0; i < v.count; ++i) {
            const double step = v.step[i], phase = v.phase[i];
            const float amplitude = v.filteredAmplitude[i];
            const double next = phase + step * n;
            v.phase[i] = next - static_cast<std::uint64_t>(next);
            if (amplitude == 0)
                continue;
#if SINEWEAVE_AVX2_AVAILABLE
            if (avx2) {
                accumulateAvx2(bank, n, amplitude, cosine(phase), cosine(phase - .25),
                               v.offsetsReal[i].data(), v.offsetsImag[i].data(), v.rotationReal[i],
                               v.rotationImag[i]);
                continue;
            }
#endif
            int j = 0;
#if SINEWEAVE_SSE2
            const auto c = _mm_set1_ps(cosine(phase)), s = _mm_set1_ps(cosine(phase - .25));
            const auto re = _mm_loadu_ps(v.offsetsReal[i].data()), im = _mm_loadu_ps(v.offsetsImag[i].data());
            auto real = _mm_sub_ps(_mm_mul_ps(c, re), _mm_mul_ps(s, im));
            auto imag = _mm_add_ps(_mm_mul_ps(s, re), _mm_mul_ps(c, im));
            const auto rotationReal = _mm_set1_ps(v.rotationReal[i]),
                       rotationImag = _mm_set1_ps(v.rotationImag[i]), amp = _mm_set1_ps(amplitude);
            for (; j + 4 <= n; j += 4) {
                _mm_storeu_ps(bank + j, _mm_add_ps(_mm_loadu_ps(bank + j), _mm_mul_ps(real, amp)));
                const auto nextReal =
                    _mm_sub_ps(_mm_mul_ps(real, rotationReal), _mm_mul_ps(imag, rotationImag));
                imag = _mm_add_ps(_mm_mul_ps(imag, rotationReal), _mm_mul_ps(real, rotationImag));
                real = nextReal;
            }
#endif
            for (; j < n; ++j)
                bank[j] += amplitude * cosine(phase + j * step);
        }
        for (int j = 0; j < n; ++j)
            output[cursor + j] += bank[j] * envelopes[j];
        cursor += n;
    }
}
void Synth::renderBank(Bank &bank, const PlaybackProgram &program, double time, double endTime, int direction,
                       double ratioPerSample, float *output, int count, bool hold) noexcept {
    const double timeStep = (endTime - time) / count;
    for (int lane = 0; lane < bank.count; ++lane) {
        int offset = 0;
        while (offset < count) {
            int n = count - offset;
            const double position = time + timeStep * offset;
            double f0 = bank.frequency[lane], f1 = f0;
            float a0 = bank.amplitude[lane], a1 = a0;
            if (!hold) {
                const auto &schedule = program.lanes[lane];
                const int interval = laneIntervalAt(schedule, position, direction, bank.interval[lane]);
                bank.interval[lane] = interval;
                if (interval < 0) {
                    bank.track[lane] = -1;
                    bank.amplitude[lane] = 0;
                    if (timeStep == 0)
                        break;
                    const auto &segments = schedule.intervals;
                    double nextTime = endTime;
                    if (direction > 0) {
                        auto next = std::upper_bound(segments.begin(), segments.end(), position,
                                                     [](double t, const auto &p) { return t < p.begin; });
                        if (next != segments.end())
                            nextTime = next->begin;
                    } else {
                        auto next = std::lower_bound(segments.begin(), segments.end(), position,
                                                     [](const auto &p, double t) { return p.end < t; });
                        if (next != segments.begin())
                            nextTime = (next - 1)->end;
                    }
                    const double distance = (nextTime - position) / timeStep;
                    if (distance >= n)
                        offset += n;
                    else {
                        const int whole = static_cast<int>(std::max(0., distance));
                        offset += std::clamp(whole + (distance > whole ? 1 : 0), 1, n);
                    }
                    continue;
                }
                const auto &segment = schedule.intervals[interval];
                const auto &track = program.tracks[segment.track];
                auto first = trajectoryAt(track, position, bank.point[lane], false);
                if (bank.track[lane] != segment.track) {
                    bank.track[lane] = segment.track;
                    bank.phase[lane] =
                        trajectoryAt(track, position, bank.point[lane]).phaseRadians / (2 * pi);
                    bank.phase[lane] -= std::floor(bank.phase[lane]);
                    bank.laneGain[lane] = bank.fadeEntries ? 0.f : 1.f;
                }
                if (timeStep != 0) {
                    double boundary = direction > 0 ? segment.end : segment.begin;
                    const int point = bank.point[lane];
                    if (direction > 0 && point + 1 < static_cast<int>(track.points.size()))
                        boundary = std::min(boundary, track.points[point + 1].time);
                    if (direction < 0) {
                        const int preceding = track.points[point].time < position ? point : point - 1;
                        if (preceding >= 0)
                            boundary = std::max(boundary, track.points[preceding].time);
                    }
                    const double samples = (boundary - position) / timeStep;
                    if (samples < n) {
                        const int whole = static_cast<int>(std::max(0., samples));
                        n = std::max(1, whole + (samples > whole ? 1 : 0));
                    }
                }
                const double nextPosition = position + timeStep * n;
                auto last = trajectoryAt(track, nextPosition, bank.point[lane], false);
                f0 = first.frequencyHz;
                f1 = last.frequencyHz;
                const float gain0 = bank.laneGain[lane];
                const float gain1 = std::min(1.f, gain0 + static_cast<float>(n / (.005 * rate)));
                bank.laneGain[lane] = gain1;
                auto fade = [&](double t) {
                    if (timeStep == 0)
                        return 1.;
                    if ((direction > 0 && segment.end >= program.end) ||
                        (direction < 0 && segment.begin <= program.start))
                        return 1.;
                    const double distance = direction > 0 ? segment.end - t : t - segment.begin;
                    return std::clamp(distance / (std::abs(timeStep) * rate * .005), 0., 1.);
                };
                a0 = static_cast<float>(first.amplitude * program.attenuation * gain0 * fade(position));
                a1 = static_cast<float>(last.amplitude * program.attenuation * gain1 * fade(nextPosition));
                bank.frequency[lane] = f1;
                bank.amplitude[lane] = a1;
            }
            const double step0 = direction * f0 * ratioPerSample;
            const double step1 = direction * f1 * ratioPerSample;
            a0 *= static_cast<float>(std::clamp((.49 - std::abs(step0)) / .04, 0., 1.));
            a1 *= static_cast<float>(std::clamp((.49 - std::abs(step1)) / .04, 0., 1.));
            const double delta = (step1 - step0) / n, phase = bank.phase[lane];
            const double nextPhase = phase + .5 * (step0 + step1) * n;
            double wrapped = nextPhase - static_cast<std::int64_t>(nextPhase);
            bank.phase[lane] = wrapped < 0 ? wrapped + 1 : wrapped;
            if (a0 == 0 && a1 == 0) {
                offset += n;
                continue;
            }
            const float amplitudeStep = (a1 - a0) / n;
            float real[4], imag[4], rotReal[4], rotImag[4];
            for (int i = 0; i < 4; ++i) {
                const double p = phase + step0 * i + .5 * delta * i * i;
                real[i] = cosine(p);
                imag[i] = cosine(p - .25);
                const double rotation = 4 * step0 + delta * (4 * i + 8);
                rotReal[i] = cosine(rotation);
                rotImag[i] = cosine(rotation - .25);
            }
            const float dc = cosine(16 * delta), ds = cosine(16 * delta - .25);
#if SINEWEAVE_AVX2_AVAILABLE
            if (avx2) {
                accumulateChirpAvx2(output + offset, n, a0, amplitudeStep, real, imag, rotReal, rotImag, dc,
                                    ds);
                offset += n;
                continue;
            }
#endif
            int j = 0;
#if SINEWEAVE_SSE2
            auto re = _mm_loadu_ps(real), im = _mm_loadu_ps(imag);
            auto rc = _mm_loadu_ps(rotReal), rs = _mm_loadu_ps(rotImag);
            const auto deltaC = _mm_set1_ps(dc), deltaS = _mm_set1_ps(ds);
            auto amp = _mm_setr_ps(a0, a0 + amplitudeStep, a0 + 2 * amplitudeStep, a0 + 3 * amplitudeStep);
            const auto ampDelta = _mm_set1_ps(4 * amplitudeStep);
            for (; j + 4 <= n; j += 4) {
                _mm_storeu_ps(output + offset + j,
                              _mm_add_ps(_mm_loadu_ps(output + offset + j), _mm_mul_ps(re, amp)));
                const auto next = _mm_sub_ps(_mm_mul_ps(re, rc), _mm_mul_ps(im, rs));
                im = _mm_add_ps(_mm_mul_ps(im, rc), _mm_mul_ps(re, rs));
                re = next;
                const auto nextRot = _mm_sub_ps(_mm_mul_ps(rc, deltaC), _mm_mul_ps(rs, deltaS));
                rs = _mm_add_ps(_mm_mul_ps(rs, deltaC), _mm_mul_ps(rc, deltaS));
                rc = nextRot;
                amp = _mm_add_ps(amp, ampDelta);
            }
#endif
            for (; j < n; ++j)
                output[offset + j] +=
                    (a0 + amplitudeStep * j) * cosine(phase + step0 * j + .5 * delta * j * j);
            offset += n;
        }
    }
    bank.fadeEntries = true;
}
void Synth::advancePlayhead(Voice &v, double movement) noexcept {
    if (v.atEnd || movement == 0)
        return;
    v.sourceTime += v.direction * movement;
    if (v.playback.loop == LoopMode::off) {
        if (v.sourceTime >= v.playback.end || v.sourceTime <= v.playback.start) {
            v.sourceTime = std::clamp(v.sourceTime, v.playback.start, v.playback.end);
            v.atEnd = true;
            v.keyDown = false;
            if (v.stage != Stage::release && v.stage != Stage::idle)
                release(v);
        }
        return;
    }
    const auto lo = v.playback.loopStart, hi = v.playback.loopEnd;
    if ((v.direction > 0 && v.sourceTime >= hi) || (v.direction < 0 && v.sourceTime <= lo)) {
        // Conservative clamp ensures even a subsequent speed ramp cannot overlap
        // another boundary while this bounded two-bank transition is active.
        const double duration = std::min(v.playback.crossfadeMs * .001, (hi - lo) / 4 * .25);
        v.crossfadeTotal = v.crossfadeLeft = std::max(0, static_cast<int>(duration * rate));
        if (v.crossfadeLeft) {
            copyBank(v, v.overlap);
            v.overlapDirection = v.direction;
        }
        if (v.playback.loop == LoopMode::wrap) {
            v.sourceTime = v.direction > 0 ? lo + std::fmod(v.sourceTime - hi, hi - lo)
                                           : hi - std::fmod(lo - v.sourceTime, hi - lo);
        } else {
            const double length = hi - lo;
            double p = std::fmod(v.sourceTime - lo, 2 * length);
            if (p < 0)
                p += 2 * length;
            const int incoming = v.direction;
            if (p < length) {
                v.sourceTime = lo + p;
                v.direction = incoming;
            } else {
                v.sourceTime = hi - (p - length);
                v.direction = -incoming;
            }
            // At exact boundaries the next leg must travel into the loop.
            if (std::abs(v.sourceTime - hi) < 1e-12)
                v.direction = -1;
            if (std::abs(v.sourceTime - lo) < 1e-12)
                v.direction = 1;
        }
        if (v.crossfadeLeft) {
            v.fadeEntries = false; // The overlap provides the entry fade, including tiny loops.
            std::fill_n(v.track.begin(), v.count, -1);
            std::fill_n(v.interval.begin(), v.count, -1);
            std::fill_n(v.point.begin(), v.count, -1);
            std::fill_n(v.laneGain.begin(), v.count, 0.f);
        }
    }
}
void Synth::renderTrajectory(Voice &v, float *output, int count, bool tail) noexcept {
    const auto *program = v.program; // Its voice lease remains held until this method returns.
    const double speedSmoothingRate = rate;
    int cursor = 0;
    while (cursor < count && v.stage != Stage::idle) {
        int n =
            std::min(count - cursor, v.speedRampLeft || std::abs(v.ratio - v.targetRatio) > 1e-9 ? 16 : 64);
        if (!v.atEnd && std::max(v.sourceSpeed, v.speedTarget) > 0) {
            const double boundary = v.playback.loop == LoopMode::off
                                        ? (v.direction > 0 ? v.playback.end : v.playback.start)
                                        : (v.direction > 0 ? v.playback.loopEnd : v.playback.loopStart);
            const double distance = std::max(0., v.direction * (boundary - v.sourceTime));
            const double samples = distance * rate / std::max(v.sourceSpeed, v.speedTarget);
            if (samples < n) {
                const int whole = static_cast<int>(samples);
                n = std::max(1, whole + (samples > whole ? 1 : 0));
            }
        }
        if (v.crossfadeLeft)
            n = std::min(n, v.crossfadeLeft);
        float envelopes[64], current[64]{}, previous[64]{};
        double speedSum = 0, ratioSum = 0;
        for (int j = 0; j < n; ++j) {
            envelopes[j] = tick(v, tail);
            if (v.speedRampLeft) {
                v.sourceSpeed += v.speedDelta;
                if (--v.speedRampLeft == 0)
                    v.sourceSpeed = v.speedTarget;
            }
            speedSum += v.sourceSpeed;
            v.ratio += (v.targetRatio - v.ratio) * smoothing;
            ratioSum += v.ratio;
        }
        const double movement = v.atEnd ? 0 : speedSum / speedSmoothingRate;
        const double endTime = v.sourceTime + v.direction * movement;
        renderBank(v, *program, v.sourceTime, endTime, v.direction, ratioSum / (n * rate), current, n,
                   v.atEnd);
        if (v.crossfadeLeft)
            renderBank(v.overlap, *program, 0, 0, v.overlapDirection, ratioSum / (n * rate), previous, n,
                       true);
        for (int j = 0; j < n; ++j) {
            float value = current[j];
            if (v.crossfadeLeft) {
                const float old = static_cast<float>(v.crossfadeLeft) / v.crossfadeTotal;
                value = previous[j] * old + value * (1 - old);
                --v.crossfadeLeft;
            }
            output[cursor + j] += value * envelopes[j];
        }
        if (v.stage != Stage::idle)
            advancePlayhead(v, movement);
        cursor += n;
    }
    if (v.stage == Stage::idle)
        finishVoice(v);
}
void Synth::render(float *left, float *right, int count) noexcept {
    for (int cursor = 0; cursor < count; cursor += 256) {
        const int n = std::min(256, count - cursor);
        float mix[256]{};
        for (auto &v : voices)
            renderVoice(v, mix, n, false);
        for (auto &v : tails)
            renderVoice(v, mix, n, true);
        renderVoice(previewVoice, mix, n, false);
        renderVoice(previewTail, mix, n, true);
        for (int j = 0; j < n; ++j) {
            gain += (targetGain - gain) * static_cast<float>(smoothing);
            left[cursor + j] = mix[j] * gain;
            if (right)
                right[cursor + j] = left[cursor + j];
        }
    }
}
int Synth::activeVoices() const noexcept {
    int n = 0;
    for (const auto &v : voices)
        if (v.stage != Stage::idle)
            ++n;
    return n;
}
} // namespace sineweave
