// SPDX-License-Identifier: AGPL-3.0-only
#include "Model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace sineweave {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool finite(double d) {
    return std::isfinite(d);
}
void checkPoint(const PartialBreakpoint &p) {
    require(finite(p.time) && finite(p.frequencyHz) && p.frequencyHz > 0 && finite(p.amplitude) &&
                p.amplitude >= 0 && finite(p.phaseRadians) && std::abs(p.phaseRadians) <= 1e12 &&
                (!p.bandwidth || (finite(*p.bandwidth) && *p.bandwidth >= 0 && *p.bandwidth <= 1)),
            "Invalid partial breakpoint");
}
struct Writer {
    std::vector<std::uint8_t> bytes;
    void integer(std::uint64_t n) {
        require(bytes.size() <= memoryBudget - 8, "State exceeds 256 MiB");
        for (int i = 0; i < 8; ++i)
            bytes.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
    }
    void real(double d) {
        std::uint64_t bits;
        std::memcpy(&bits, &d, 8);
        integer(bits);
    }
    void string(const std::string &s) {
        integer(s.size());
        require(s.size() <= memoryBudget - bytes.size(), "State exceeds 256 MiB");
        bytes.insert(bytes.end(), s.begin(), s.end());
    }
    void bandwidth(std::optional<double> b) {
        integer(b.has_value());
        if (b)
            real(*b);
    }
};
struct Reader {
    const std::uint8_t *p;
    std::size_t left;
    std::uint64_t integer() {
        require(left >= 8, "Truncated state");
        std::uint64_t n = 0;
        for (int i = 0; i < 8; ++i)
            n |= std::uint64_t(p[i]) << (8 * i);
        p += 8;
        left -= 8;
        return n;
    }
    double real() {
        auto bits = integer();
        double d;
        std::memcpy(&d, &bits, 8);
        require(finite(d), "Non-finite state value");
        return d;
    }
    std::size_t count(std::size_t minimumBytes) {
        auto n = integer();
        require(n <= left / minimumBytes, "Invalid state count");
        return static_cast<std::size_t>(n);
    }
    std::string string() {
        auto n = count(1);
        require(n <= 32768, "Source path too long");
        std::string s(reinterpret_cast<const char *>(p), n);
        p += n;
        left -= n;
        return s;
    }
    std::optional<double> bandwidth() {
        auto has = integer();
        require(has <= 1, "Invalid optional value");
        if (!has)
            return {};
        auto d = real();
        require(d >= 0 && d <= 1, "Invalid bandwidth");
        return d;
    }
};
} // namespace
double wrapPhase(double r) noexcept {
    auto wrapped = std::remainder(r, 2 * pi);
    return wrapped >= pi ? wrapped - 2 * pi : wrapped;
}
void validate(const AnalysedSound &s) {
    require(s.sourcePath.size() <= 32768, "Source path too long");
    require(finite(s.sourceSampleRate) && s.sourceSampleRate > 0 && s.sourceSampleRate <= 768000 &&
                finite(s.sourceDuration) && s.sourceDuration > 0 && s.sourceDuration <= 86400 &&
                finite(s.startSeconds) && finite(s.endSeconds) && s.startSeconds >= 0 &&
                s.endSeconds > s.startSeconds && s.endSeconds <= s.sourceDuration + 1e-6 &&
                static_cast<unsigned>(s.channel) <= 2,
            "Invalid source metadata/range");
    require(finite(s.settings.resolutionHz) && s.settings.resolutionHz >= 1 &&
                s.settings.resolutionHz <= 2000 && finite(s.settings.windowWidthHz) &&
                s.settings.windowWidthHz >= 1 && s.settings.windowWidthHz <= 4000 &&
                finite(s.settings.amplitudeFloorDb) && s.settings.amplitudeFloorDb >= -160 &&
                s.settings.amplitudeFloorDb <= 0,
            "Invalid analysis settings");
    std::size_t used = sizeof(s) + s.sourcePath.size();
    std::unordered_set<std::uint64_t> ids;
    for (const auto &t : s.partials) {
        require(ids.insert(t.id).second && !t.points.empty(), "Duplicate ID or empty trajectory");
        require(t.points.size() <= (memoryBudget - used) / sizeof(PartialBreakpoint),
                "Analysis exceeds 256 MiB");
        used += sizeof(t) + t.points.size() * sizeof(PartialBreakpoint);
        require(used <= memoryBudget, "Analysis exceeds 256 MiB");
        double previous = -1;
        for (const auto &p : t.points) {
            checkPoint(p);
            require(p.frequencyHz <= s.sourceSampleRate * .5 && p.time > previous &&
                        p.time >= s.startSeconds - 1e-6 && p.time <= s.endSeconds + 1e-6,
                    "Invalid breakpoint frequency/ordering/time");
            previous = p.time;
        }
    }
}
SpectralFrame sampleFrame(const AnalysedSound &s, double time, int limit) {
    // Controls have millisecond precision; tolerate only the half-step rounding at edges.
    require(finite(time) && time >= s.startSeconds - .00051 && time <= s.endSeconds + .00051,
            "Sustain position is outside analysed range");
    time = std::clamp(time, s.startSeconds, s.endSeconds);
    require(limit >= 1 && limit <= maxPartials, "Invalid partial limit");
    SpectralFrame frame;
    frame.sourceSeconds = time;
    for (const auto &t : s.partials) {
        if (t.points.empty() || time < t.points.front().time || time > t.points.back().time)
            continue;
        auto hi = std::upper_bound(t.points.begin(), t.points.end(), time,
                                   [](double x, const auto &p) { return x < p.time; });
        const auto &lo = *(hi == t.points.begin() ? hi : hi - 1);
        double frequency = lo.frequencyHz, amplitude = lo.amplitude, phase = lo.phaseRadians;
        auto bw = lo.bandwidth;
        if (hi != t.points.end()) {
            const double a = (time - lo.time) / (hi->time - lo.time);
            frequency += a * (hi->frequencyHz - frequency);
            amplitude += a * (hi->amplitude - amplitude);
            phase += 2 * pi * (time - lo.time) * .5 * (lo.frequencyHz + frequency);
            if (bw && hi->bandwidth)
                *bw += a * (*hi->bandwidth - *bw);
        }
        if (amplitude > 0)
            frame.partials.push_back({t.id, frequency, amplitude, wrapPhase(phase), bw});
    }
    frame.activeCount = frame.partials.size();
    auto strongest = [](const auto &a, const auto &b) {
        return a.amplitude != b.amplitude ? a.amplitude > b.amplitude : a.id < b.id;
    };
    if (frame.partials.size() > static_cast<std::size_t>(limit)) {
        std::partial_sort(frame.partials.begin(), frame.partials.begin() + limit, frame.partials.end(),
                          strongest);
        frame.partials.resize(limit);
    }
    std::sort(frame.partials.begin(), frame.partials.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    double sum = 0;
    for (const auto &p : frame.partials)
        sum += p.amplitude;
    if (sum > 1)
        for (auto &p : frame.partials)
            p.amplitude /= sum;
    return frame;
}
std::vector<std::uint8_t> encodeState(const SoundState &state) {
    return encodeState(state.sound, state.frame);
}
std::vector<std::uint8_t> encodeState(const AnalysedSound &s, const SpectralFrame &frame) {
    validate(s);
    Writer w;
    w.integer(0x31565753);
    w.integer(1);
    w.string(s.sourcePath);
    w.real(s.sourceSampleRate);
    w.real(s.sourceDuration);
    w.real(s.startSeconds);
    w.real(s.endSeconds);
    w.integer(static_cast<unsigned>(s.channel));
    w.real(s.settings.resolutionHz);
    w.real(s.settings.windowWidthHz);
    w.real(s.settings.amplitudeFloorDb);
    w.integer(s.partials.size());
    for (const auto &t : s.partials) {
        w.integer(t.id);
        w.integer(t.points.size());
        for (const auto &p : t.points) {
            w.real(p.time);
            w.real(p.frequencyHz);
            w.real(p.amplitude);
            w.real(p.phaseRadians);
            w.bandwidth(p.bandwidth);
        }
    }
    w.real(frame.sourceSeconds);
    w.integer(frame.activeCount);
    w.integer(frame.partials.size());
    for (const auto &p : frame.partials) {
        w.integer(p.id);
        w.real(p.frequencyHz);
        w.real(p.amplitude);
        w.real(p.phaseRadians);
        w.bandwidth(p.bandwidth);
    }
    // Two reserved optional audio sections. V1 intentionally does not embed PCM.
    require(!s.sourceAudio && !s.residualAudio, "PCM model sections are reserved for a future state version");
    w.integer(0);
    w.integer(0);
    return std::move(w.bytes);
}
SoundState decodeState(const void *data, std::size_t size) {
    require(data && size <= memoryBudget, "Invalid/oversized state");
    Reader r{static_cast<const std::uint8_t *>(data), size};
    require(r.integer() == 0x31565753 && r.integer() == 1, "Unsupported state version");
    SoundState state;
    auto &s = state.sound;
    s.sourcePath = r.string();
    s.sourceSampleRate = r.real();
    s.sourceDuration = r.real();
    s.startSeconds = r.real();
    s.endSeconds = r.real();
    s.channel = static_cast<InputChannel>(r.integer());
    s.settings = {r.real(), r.real(), r.real()};
    auto tracks = r.count(56);
    s.partials.reserve(tracks);
    std::size_t used = sizeof(s) + s.sourcePath.size();
    for (std::size_t i = 0; i < tracks; ++i) {
        PartialTrajectory t;
        t.id = r.integer();
        auto count = r.count(40);
        require(used <= memoryBudget - sizeof(t) &&
                    count <= (memoryBudget - used - sizeof(t)) / sizeof(PartialBreakpoint),
                "Decoded model exceeds 256 MiB");
        used += sizeof(t) + count * sizeof(PartialBreakpoint);
        t.points.reserve(count);
        for (std::size_t j = 0; j < count; ++j)
            t.points.push_back({r.real(), r.real(), r.real(), r.real(), r.bandwidth()});
        s.partials.push_back(std::move(t));
    }
    validate(s);
    auto &f = state.frame;
    f.sourceSeconds = r.real();
    f.activeCount = static_cast<std::size_t>(r.integer());
    auto count = r.count(40);
    require(count <= maxPartials && count <= f.activeCount && f.activeCount <= s.partials.size(),
            "Invalid frame count");
    require(f.sourceSeconds >= s.startSeconds && f.sourceSeconds <= s.endSeconds, "Invalid frame time");
    std::unordered_set<std::uint64_t> ids, frameIds;
    for (const auto &t : s.partials)
        ids.insert(t.id);
    double amplitudeSum = 0;
    std::uint64_t previousFrameId = 0;
    for (std::size_t i = 0; i < count; ++i) {
        SpectralPartial p{r.integer(), r.real(), r.real(), r.real(), r.bandwidth()};
        checkPoint({f.sourceSeconds, p.frequencyHz, p.amplitude, p.phaseRadians, p.bandwidth});
        require(p.frequencyHz <= s.sourceSampleRate * .5 && ids.count(p.id) && frameIds.insert(p.id).second,
                "Invalid frame frequency/ID");
        require(i == 0 || p.id > previousFrameId, "Invalid frame ordering");
        previousFrameId = p.id;
        amplitudeSum += p.amplitude;
        f.partials.push_back(p);
    }
    require(amplitudeSum <= 1.000001, "Unnormalised frame");
    require(r.integer() == 0 && r.integer() == 0 && r.left == 0, "Unsupported audio sections/trailing state");
    return state;
}
} // namespace sineweave
