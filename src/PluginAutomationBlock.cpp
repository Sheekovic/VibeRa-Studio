// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginAutomationBlock.h"
#include <algorithm>
#include <cmath>
#include <tuple>
#include <limits>

namespace vibera
{
PluginAutomationBlock::Admission PluginAutomationBlock::admit(const AutomationProgram &program,
                                                              std::int64_t plugin, double beatsPerFrame,
                                                              int quantum)
{
    const double window = (quantum - 1.) * beatsPerFrame;
    if (plugin <= 0 || quantum < 1 || quantum > ipc::frames || !std::isfinite(beatsPerFrame) ||
        beatsPerFrame <= 0 || !std::isfinite(window))
        return {};
    const auto lanes = program.pluginLanes(plugin);
    struct Event
    {
        double beat;
        std::size_t lane;
        bool start, dense;
    };
    struct Activity
    {
        int dense = 0, boundaries = 0;
    };
    std::vector<Event> events;
    std::vector<Activity> activity(lanes.size());
    const auto interval = [&](double begin, double end, std::size_t lane, bool dense)
    {
        // Cover rounding in callback beat + frame * step, including cancellation
        // when the packet window is close to a large absolute Song position.
        constexpr auto epsilon = 8 * std::numeric_limits<double>::epsilon();
        const auto guard = epsilon * (std::abs(begin) + std::abs(window) + 1);
        events.push_back({begin - window - guard, lane, true, dense});
        if (std::isfinite(end))
            events.push_back({end + epsilon * (std::abs(end) + 1), lane, false, dense});
    };
    for (std::size_t index = 0; index < lanes.size(); ++index)
    {
        const auto &lane = lanes[index];
        for (std::size_t i = 0; i < lane.spans.size(); ++i)
        {
            const auto &span = lane.spans[i];
            if (!std::isfinite(span.beat - window))
                return {};
            const auto previous = i == 0 ? lane.initial : lane.spans[i - 1].valueAt(span.beat);
            if (span.valueAt(span.beat) != previous)
                interval(span.beat, span.beat, index, false);
            if (span.inverseLength != 0 &&
                (span.coefficients[1] != 0 || span.coefficients[2] != 0 || span.coefficients[3] != 0))
                interval(span.beat,
                         i + 1 < lane.spans.size() ? lane.spans[i + 1].beat
                                                   : std::numeric_limits<double>::infinity(),
                         index, true);
        }
    }
    std::sort(events.begin(), events.end(),
              [](const auto &a, const auto &b)
              {
                  if (a.beat != b.beat)
                      return a.beat < b.beat;
                  return a.start > b.start; // Include both sides of a shared boundary conservatively.
              });
    std::size_t total = lanes.size(), maximum = total;
    const auto contribution = [&](const Activity &a)
    {
        return a.dense > 0 ? std::size_t(quantum)
                           : std::min(std::size_t(quantum), std::size_t(1 + a.boundaries));
    };
    for (const auto &event : events)
    {
        auto &a = activity[event.lane];
        total -= contribution(a);
        (event.dense ? a.dense : a.boundaries) += event.start ? 1 : -1;
        total += contribution(a);
        maximum = std::max(maximum, total);
    }
    return {maximum <= ipc::parameterEvents ? Result::ok : Result::capacityExceeded, maximum};
}
PluginAutomationBlock::Result
PluginAutomationBlock::render(const AutomationProgram &program, std::int64_t plugin, double beat,
                              double beatsPerFrame, int frames, bool playing,
                              std::span<const PluginAutomationValue> previousValues) noexcept
{
    count = 0;
    if (plugin <= 0 || frames < 0 || frames > ipc::frames || !std::isfinite(beat) ||
        !std::isfinite(beatsPerFrame) || beatsPerFrame <= 0 ||
        !std::isfinite(beat + std::max(0, frames - 1) * beatsPerFrame) ||
        (frames > 1 && beat + beatsPerFrame <= beat))
        return Result::invalidTiming;
    if (!playing || frames == 0)
        return Result::ok;
    const auto firstSampleAtOrAfter = [&](double boundary, int low)
    {
        int high = frames;
        while (low < high)
        {
            const int middle = low + (high - low) / 2;
            if (beat + middle * beatsPerFrame < boundary)
                low = middle + 1;
            else
                high = middle;
        }
        return low;
    };
    for (const auto &lane : program.pluginLanes(plugin))
    {
        const auto old = std::lower_bound(previousValues.begin(), previousValues.end(), lane.key.parameter,
                                          [](const auto &value, auto id) { return value.id < id; });
        const bool known = old != previousValues.end() && old->id == lane.key.parameter;
        float previous = known ? old->value : 0;
        int frame = 0;
        while (frame < frames)
        {
            const double position = beat + frame * beatsPerFrame;
            const auto next =
                std::upper_bound(lane.spans.begin(), lane.spans.end(), position,
                                 [](double value, const auto &span) { return value < span.beat; });
            const auto *span = next == lane.spans.begin() ? nullptr : &*std::prev(next);
            const float value = span ? span->valueAt(position) : lane.initial;
            if ((frame == 0 && !known) || value != previous)
            {
                if (count == ipc::parameterEvents)
                {
                    count = 0; // No partial batch may be submitted to a worker.
                    return Result::capacityExceeded;
                }
                storage[std::size_t(count++)] = {lane.key.parameter, frame, value};
                previous = value;
            }
            const bool constant =
                !span || span->inverseLength == 0 ||
                (span->coefficients[1] == 0 && span->coefficients[2] == 0 && span->coefficients[3] == 0);
            if (!constant)
            {
                ++frame;
                if (frame < frames &&
                    (next == lane.spans.end() || beat + frame * beatsPerFrame < next->beat) &&
                    span->valueAt(beat + frame * beatsPerFrame) == previous)
                {
                    // Each compiled polynomial span is monotonic. Skip an unchanged
                    // float plateau, but never search across a curve/overlap boundary.
                    int high = next == lane.spans.end() ? frames : firstSampleAtOrAfter(next->beat, frame);
                    while (frame < high)
                    {
                        const int middle = frame + (high - frame) / 2;
                        if (span->valueAt(beat + middle * beatsPerFrame) == previous)
                            frame = middle + 1;
                        else
                            high = middle;
                    }
                }
                continue;
            }
            if (next == lane.spans.end())
                break;
            // Find the first actual sample at/after the boundary, avoiding ceil's
            // cancellation/rounding errors at large Song positions and loop seeks.
            frame = firstSampleAtOrAfter(next->beat, frame + 1);
        }
    }
    std::sort(storage.begin(), storage.begin() + count,
              [](const auto &a, const auto &b) { return std::tie(a.frame, a.id) < std::tie(b.frame, b.id); });
    return Result::ok;
}
} // namespace vibera
