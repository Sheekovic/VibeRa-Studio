// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace vibera
{
// A tempo change at a Song beat. With `ramp` the tempo glides linearly (in BPM per beat)
// from the previous point to this one instead of jumping at it.
struct TempoPoint
{
    double beat = 0, bpm = 120;
    bool ramp = false;
    bool operator==(const TempoPoint &) const = default;
};

// Song beats <-> edit seconds for a base tempo and sorted tempo points. Prepared on the
// control thread; every query is allocation-free, lock-free and noexcept, so audio
// callbacks may use an immutable published map. Ramps integrate exactly:
// seconds = 60 / k * ln(bpm(b) / bpm0) for a linear slope k in BPM per beat.
class TempoMap
{
  public:
    TempoMap() { prepare(120, {}); }
    TempoMap(double tempo, const std::vector<TempoPoint> &points) { prepare(tempo, points); }

    void prepare(double tempo, const std::vector<TempoPoint> &points)
    {
        segments.clear();
        segments.push_back({0, 0, tempo, 0});
        for (const auto &point : points)
        {
            if (!(point.beat > segments.back().beat))
                continue;
            auto &last = segments.back();
            // Close the previous segment: constant, or ramping toward this point's tempo.
            last.slope = point.ramp ? (point.bpm - last.bpm) / (point.beat - last.beat) : 0;
            const double seconds = last.seconds + span(last, point.beat - last.beat);
            segments.push_back({point.beat, seconds, point.bpm, 0});
        }
    }
    bool constant() const noexcept { return segments.size() == 1; }
    double baseTempo() const noexcept { return segments.front().bpm; }

    double secondsAt(double beat) const noexcept
    {
        if (beat <= 0)
            return beat * 60. / segments.front().bpm; // Pre-roll before the Song keeps the base tempo.
        const auto &s = segmentForBeat(beat);
        return s.seconds + span(s, beat - s.beat);
    }
    double beatAt(double seconds) const noexcept
    {
        if (seconds <= 0)
            return seconds * segments.front().bpm / 60.;
        const auto &s = segmentForSeconds(seconds);
        const double t = seconds - s.seconds;
        if (std::abs(s.slope) < 1.e-12)
            return s.beat + t * s.bpm / 60.;
        return s.beat + s.bpm * (std::exp(s.slope * t / 60.) - 1.) / s.slope;
    }
    double bpmAt(double beat) const noexcept
    {
        const auto &s = segmentForBeat(beat);
        return s.bpm + s.slope * std::max(0., beat - s.beat);
    }
    // Seconds of `beats` starting at `beat` (positive span), and its inverse.
    double secondsBetween(double from, double to) const noexcept { return secondsAt(to) - secondsAt(from); }
    double beatAfter(double beat, double seconds) const noexcept { return beatAt(secondsAt(beat) + seconds); }

  private:
    struct Segment
    {
        double beat, seconds, bpm, slope; // Start beat/second, BPM there, BPM per beat after it.
    };
    std::vector<Segment> segments;
    static double span(const Segment &s, double beats) noexcept
    {
        if (std::abs(s.slope) < 1.e-12)
            return beats * 60. / s.bpm;
        return 60. / s.slope * std::log((s.bpm + s.slope * beats) / s.bpm);
    }
    // Before the first point (including negative pre-roll) the base tempo extends backwards.
    const Segment &segmentForBeat(double beat) const noexcept
    {
        auto it = std::upper_bound(segments.begin() + 1, segments.end(), beat,
                                   [](double b, const Segment &s) { return b < s.beat; });
        return *(it - 1);
    }
    const Segment &segmentForSeconds(double seconds) const noexcept
    {
        auto it = std::upper_bound(segments.begin() + 1, segments.end(), seconds,
                                   [](double t, const Segment &s) { return t < s.seconds; });
        return *(it - 1);
    }
};
} // namespace vibera
