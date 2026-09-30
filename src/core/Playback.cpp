// SPDX-License-Identifier: AGPL-3.0-only
#include "Playback.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
namespace sineweave {
std::size_t PlaybackProgram::bytes() const noexcept {
    std::size_t n = sizeof(*this) + tracks.capacity() * sizeof(PartialTrajectory) +
                    lanes.capacity() * sizeof(PlaybackLane);
    for (const auto &t : tracks)
        n += t.points.capacity() * sizeof(PartialBreakpoint);
    for (const auto &l : lanes)
        n += l.intervals.capacity() * sizeof(LaneInterval);
    return n;
}
PlaybackSettings validatePlayback(PlaybackSettings s, double begin, double end) {
    if (!std::isfinite(s.start) || !std::isfinite(s.end) || !std::isfinite(s.loopStart) ||
        !std::isfinite(s.loopEnd) || !std::isfinite(s.crossfadeMs) || s.start < begin - .00051 ||
        s.end > end + .00051 || s.start >= s.end || s.crossfadeMs < 0 || s.crossfadeMs > 100 ||
        (s.direction != 1 && s.direction != -1) || static_cast<unsigned>(s.loop) > 2)
        throw std::runtime_error("Playback range must have start < end inside the analysed range");
    s.start = std::clamp(s.start, begin, end);
    s.end = std::clamp(s.end, begin, end);
    if (s.loopStart < s.start - .00051 || s.loopEnd > s.end + .00051 || s.loopStart >= s.loopEnd ||
        (s.loop != LoopMode::off && s.loopEnd - s.loopStart < .0009))
        throw std::runtime_error("Loop range must be at least 1 ms and inside the playback range");
    {
        s.loopStart = std::clamp(s.loopStart, s.start, s.end);
        s.loopEnd = std::clamp(s.loopEnd, s.start, s.end);
        if (s.loopEnd <= s.loopStart)
            throw std::runtime_error("Invalid loop range");
    }
    return s;
}
SpectralPartial trajectoryAt(const PartialTrajectory &t, double time, int &cursor,
                             bool evaluatePhase) noexcept {
    const auto &points = t.points;
    time = std::clamp(time, points.front().time, points.back().time);
    if (cursor < 0 || cursor >= static_cast<int>(points.size()) || time < points[cursor].time ||
        (cursor + 1 < static_cast<int>(points.size()) && time >= points[cursor + 1].time)) {
        auto hi = std::upper_bound(points.begin(), points.end(), time,
                                   [](double x, const auto &p) { return x < p.time; });
        cursor = static_cast<int>(hi - points.begin()) - 1;
    }
    const auto &lo = points[cursor];
    SpectralPartial result{t.id, lo.frequencyHz, lo.amplitude, lo.phaseRadians, lo.bandwidth};
    if (cursor + 1 < static_cast<int>(points.size())) {
        const auto &hi = points[cursor + 1];
        const double dt = time - lo.time, a = dt / (hi.time - lo.time);
        result.frequencyHz += a * (hi.frequencyHz - lo.frequencyHz);
        result.amplitude += a * (hi.amplitude - lo.amplitude);
        if (lo.bandwidth && hi.bandwidth)
            result.bandwidth = *lo.bandwidth + a * (*hi.bandwidth - *lo.bandwidth);
        if (evaluatePhase)
            result.phaseRadians += 2 * pi * dt * .5 * (lo.frequencyHz + result.frequencyHz);
    }
    if (evaluatePhase)
        result.phaseRadians = wrapPhase(result.phaseRadians);
    return result;
}
int laneIntervalAt(const PlaybackLane &lane, double time, int direction, int previous) noexcept {
    const auto &a = lane.intervals;
    auto contains = [&](int i) {
        if (i < 0 || i >= static_cast<int>(a.size()))
            return false;
        return direction > 0 ? time >= a[i].begin && time < a[i].end : time > a[i].begin && time <= a[i].end;
    };
    if (contains(previous))
        return previous;
    auto hi = std::upper_bound(a.begin(), a.end(), time, [direction](double x, const auto &p) {
        return direction > 0 ? x < p.begin : x <= p.begin;
    });
    const int i = static_cast<int>(hi - a.begin()) - 1;
    return contains(i) ? i : -1;
}
std::unique_ptr<PlaybackProgram> compilePlayback(const AnalysedSound &sound, int limit, std::size_t budget) {
    validate(sound);
    if (limit < 1 || limit > maxPartials || budget > memoryBudget)
        throw std::runtime_error("Invalid playback program limit/budget");
    struct Event {
        double time;
        int track;
        bool start;
    };
    struct GainEvent {
        double time, value, slope;
    };
    std::size_t points = 0;
    for (const auto &t : sound.partials)
        points += t.points.size();
    const auto baseline = sizeof(PlaybackProgram) +
                          sound.partials.size() * (sizeof(PartialTrajectory) + 2 * sizeof(Event) + 80) +
                          points * (sizeof(PartialBreakpoint) + sizeof(GainEvent));
    if (baseline > budget)
        throw std::runtime_error("Playback program exceeds available per-instance budget");
    auto program = std::make_unique<PlaybackProgram>();
    program->start = sound.startSeconds;
    program->end = sound.endSeconds;
    program->tracks = sound.partials;
    std::vector<double> peaks(sound.partials.size());
    std::vector<Event> events;
    events.reserve(2 * sound.partials.size());
    for (std::size_t i = 0; i < sound.partials.size(); ++i) {
        const auto &t = sound.partials[i];
        for (const auto &p : t.points)
            peaks[i] = std::max(peaks[i], p.amplitude);
        if (t.points.front().time < t.points.back().time && peaks[i] > 0) {
            events.push_back({t.points.front().time, static_cast<int>(i), true});
            events.push_back({t.points.back().time, static_cast<int>(i), false});
        }
    }
    std::sort(events.begin(), events.end(), [](const auto &a, const auto &b) { return a.time < b.time; });
    auto priority = [&](int a, int b) {
        return peaks[a] != peaks[b] ? peaks[a] > peaks[b] : sound.partials[a].id < sound.partials[b].id;
    };
    std::set<int, decltype(priority)> active(priority);
    program->lanes.resize(std::min<std::size_t>(limit, sound.partials.size()));
    std::vector<int> assigned(program->lanes.size(), -1);
    std::vector<bool> selected(sound.partials.size()), everSelected(sound.partials.size());
    std::vector<int> ranked;
    ranked.reserve(limit);
    std::size_t intervalBytes = 0;
    for (std::size_t e = 0; e < events.size();) {
        const double time = events[e].time;
        while (e < events.size() && events[e].time == time) {
            if (events[e].start)
                active.insert(events[e].track);
            else
                active.erase(events[e].track);
            ++e;
        }
        for (int track : ranked)
            selected[track] = false;
        ranked.clear();
        program->limited |= active.size() > static_cast<std::size_t>(limit);
        int n = 0;
        for (int track : active) {
            if (n++ >= limit)
                break;
            selected[track] = everSelected[track] = true;
            ranked.push_back(track);
        }
        for (auto &track : assigned)
            if (track >= 0 && !selected[track])
                track = -1;
        for (int track : assigned)
            if (track >= 0)
                selected[track] = false;
        auto free = assigned.begin();
        for (int track : active)
            if (selected[track]) {
                free = std::find(free, assigned.end(), -1);
                if (free == assigned.end())
                    break;
                *free++ = track;
            }
        if (e == events.size())
            break;
        const double end = events[e].time;
        for (std::size_t lane = 0; lane < assigned.size(); ++lane)
            if (assigned[lane] >= 0) {
                auto &segments = program->lanes[lane].intervals;
                if (!segments.empty() && segments.back().track == assigned[lane] &&
                    segments.back().end == time)
                    segments.back().end = end;
                else {
                    intervalBytes += 2 * sizeof(LaneInterval);
                    if (baseline + intervalBytes > budget)
                        throw std::runtime_error("Playback lane schedule exceeds available budget");
                    segments.push_back({time, end, assigned[lane]});
                }
            }
    }
    program->excludedTracks = std::count(everSelected.begin(), everSelected.end(), false);
    while (!program->lanes.empty() && program->lanes.back().intervals.empty())
        program->lanes.pop_back();
    // Sum piecewise-linear amplitudes at all slope/jump events. A single fixed
    // scale bounds every frame without pumping or summing non-overlapping peaks.
    std::vector<GainEvent> gain;
    for (const auto &lane : program->lanes)
        for (const auto &interval : lane.intervals) {
            const auto &track = program->tracks[interval.track];
            int cursor = -1;
            auto first = trajectoryAt(track, interval.begin, cursor);
            double previousSlope = 0;
            if (cursor + 1 < static_cast<int>(track.points.size())) {
                const auto &lo = track.points[cursor], &hi = track.points[cursor + 1];
                previousSlope = (hi.amplitude - lo.amplitude) / (hi.time - lo.time);
            }
            gain.push_back({interval.begin, first.amplitude, previousSlope});
            for (std::size_t i = static_cast<std::size_t>(cursor + 1);
                 i < track.points.size() && track.points[i].time < interval.end; ++i) {
                const auto &lo = track.points[i];
                double nextSlope = 0;
                if (i + 1 < track.points.size()) {
                    const auto &hi = track.points[i + 1];
                    nextSlope = (hi.amplitude - lo.amplitude) / (hi.time - lo.time);
                }
                gain.push_back({lo.time, 0, nextSlope - previousSlope});
                previousSlope = nextSlope;
            }
            gain.push_back(
                {interval.end, -trajectoryAt(track, interval.end, cursor).amplitude, -previousSlope});
            if (program->bytes() + gain.capacity() * sizeof(GainEvent) > budget)
                throw std::runtime_error("Playback normalization exceeds available budget");
        }
    std::sort(gain.begin(), gain.end(), [](const auto &a, const auto &b) { return a.time < b.time; });
    double sum = 0, slope = 0, peak = 0, previousTime = program->start;
    for (std::size_t i = 0; i < gain.size();) {
        const double time = gain[i].time;
        sum += slope * (time - previousTime);
        peak = std::max(peak, sum);
        while (i < gain.size() && gain[i].time == time) {
            sum += gain[i].value;
            slope += gain[i++].slope;
        }
        if (!std::isfinite(sum) || !std::isfinite(slope) || !std::isfinite(peak))
            throw std::runtime_error("Non-finite playback amplitude sum/slope");
        peak = std::max(peak, sum);
        previousTime = time;
    }
    program->attenuation = 1 / std::max(1., peak);
    if (program->bytes() > budget)
        throw std::runtime_error("Playback program exceeds budget");
    return program;
}
} // namespace sineweave
