// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MixerAutomationTarget.h"
#include <array>
#include <memory>
#include <limits>

namespace vibera
{
// Original CONTROL-only Song recorder. Input callbacks only stamp their existing
// queues; no Project, vector, String or history object is touched by a producer.
class MixerAutomationTake
{
  public:
    static constexpr size_t maximumPoints = 65536, maximumTargets = 64;
    struct Event
    {
        AutomationKey key;
        double seconds = 0;
        float value = 0;
    };
    enum class Phase
    {
        armed,
        capturing,
        sealing,
        ready,
        invalid,
        failed
    };
    std::unique_ptr<Project> before, expected;
    std::vector<MixerAutomationTarget> targets;
    std::vector<Event> events;
    std::optional<Event> pendingEvent;
    juce::String identity, token, error, controllerProfileBefore;
    bool beforeDirty = false, inputFailed = false, cutoffFrozen = false;
    int insert = 0, lane = 0;
    unsigned session = 0, epoch = 0, dropped = 0;
    std::array<unsigned, 2> controlGenerations{};
    unsigned controllerCcGeneration = 0;
    std::uint64_t externalChangesBefore = 0;
    Id channel = 0;
    double rate = 0, originSeconds = 0, originBeat = 0, stopSeconds = 0;
    double tempo = 120;
    Phase phase = Phase::armed;

    static juce::Result prepare(const Project &p, bool dirty, int insert, int lane,
                                const juce::String &identity, const juce::String &token, unsigned session,
                                std::unique_ptr<MixerAutomationTake> &output)
    {
        try
        {
            if (auto valid = p.validate(); valid.failed())
                return valid;
            if (insert < 0 || lane < 0 || lane > 127 || !session)
                return juce::Result::fail(
                    "automationRecording.invalidArguments: Existing insert/lane required");
            auto next = std::make_unique<MixerAutomationTake>();
            next->before = std::make_unique<Project>(p);
            next->expected = std::make_unique<Project>(p);
            next->beforeDirty = dirty;
            next->insert = insert;
            next->lane = lane;
            next->identity = identity;
            next->token = token;
            next->session = session;
            next->tempo = p.tempo;
            for (auto control : {MixerAutomationControl::gain, MixerAutomationControl::pan,
                                 MixerAutomationControl::stereoSeparation})
            {
                MixerAutomationTarget t;
                if (auto result = resolveMixerAutomationTarget(p, insert, control, t); result.failed())
                    return result;
                next->targets.push_back(std::move(t));
            }
            const auto row = std::find_if(p.mixer.begin(), p.mixer.end(),
                                          [=](const auto &v) { return v.index == insert; });
            for (const auto &edge : row->routes)
            {
                MixerAutomationTarget t;
                if (auto result =
                        resolveMixerAutomationTarget(p, insert, MixerAutomationControl::send, t, edge.target);
                    result.failed())
                    return result;
                next->targets.push_back(std::move(t));
            }
            if (next->targets.size() > maximumTargets)
                return juce::Result::fail("automationRecording.capacity: Too many prepared send targets");
            for (const auto &t : next->targets)
            {
                // Initialized values also override manual controls in Song mode.
                // Refuse rather than silently recording an inaudible manual edit.
                if (std::any_of(p.automation.initialValues.begin(), p.automation.initialValues.end(),
                                [&](const auto &v) { return v.key == t.key; }))
                    return juce::Result::fail(
                        "automationRecording.overlap: Remove existing target initialization before arming");
                for (const auto &source : p.automation.sources)
                    if (std::any_of(source.links.begin(), source.links.end(),
                                    [&](const auto &v) { return v.key == t.key; }))
                        return juce::Result::fail("automationRecording.overlap: Existing linked automation "
                                                  "is retained; overdub is unsupported");
            }
            next->events.reserve(maximumPoints);
            output = std::move(next);
            return juce::Result::ok();
        }
        catch (...)
        {
            return juce::Result::fail("automationRecording.preparationFailed: Recorder was not armed");
        }
    }
    bool sourceCurrent(const Project &p) const
    {
        return expected && expected->state().isEquivalentTo(p.state());
    }
    const MixerAutomationTarget *target(AutomationKey key) const noexcept
    {
        for (const auto &t : targets)
            if (t.key == key)
                return &t;
        return nullptr;
    }
    static std::optional<AutomationKey> keyForCommand(const juce::String &id, int mixer,
                                                      Id destination) noexcept
    {
        if (id == "mixer.gain")
            return AutomationKey{AutomationParameter::mixerGain, mixer, 0};
        if (id == "mixer.pan")
            return AutomationKey{AutomationParameter::mixerPan, mixer, 0};
        if (id == "mixer.stereoSeparation")
            return AutomationKey{AutomationParameter::mixerStereoSeparation, mixer, 0};
        if (id == "mixer.send" && destination >= 0 && destination <= std::numeric_limits<int>::max())
            return AutomationKey{AutomationParameter::mixerSend, mixer, std::uint32_t(destination)};
        return {};
    }
    juce::Result admitEvent(const Event &e) const
    {
        const auto *t = target(e.key);
        if (!t || !t->contains(e.value))
            return juce::Result::fail("automationRecording.invalidTarget: Only prepared insert controls and "
                                      "existing sends are recordable");
        if ((phase != Phase::capturing && phase != Phase::sealing) || !std::isfinite(e.seconds) ||
            e.seconds < originSeconds || (phase == Phase::sealing && e.seconds > stopSeconds) ||
            std::any_of(
                events.begin(), events.end(),
                [&](const auto &old) { return old.key == e.key && old.seconds > e.seconds; }))
            return juce::Result::fail(
                "automationRecording.invalidClock: Timed event is outside the owned recording interval");
        if (events.size() == maximumPoints)
            return juce::Result::fail(
                "automationRecording.capacity: Full take retained; cancel without losing Project history");
        return juce::Result::ok();
    }
    void accepted(const Event &e, std::unique_ptr<Project> &nextExpected) noexcept
    {
        // Capacity/admission and complete nextExpected were prepared before commit.
        const auto position =
            std::upper_bound(events.begin(), events.end(), e.seconds,
                             [](double seconds, const Event &old) { return seconds < old.seconds; });
        events.insert(position, e);
        expected.swap(nextExpected);
        pendingEvent.reset();
    }
    juce::Result destination(bool cancel, Project &output) const
    {
        try
        {
            if (!before || !expected)
                return juce::Result::fail("automationRecording.invalidTake: Missing owned baseline");
            if (!cancel && inputFailed)
                return juce::Result::fail("automationRecording.invalidCapture: Partial input cannot be "
                                          "committed; cancel restores baseline");
            if (cancel || events.empty())
            {
                output = *before;
                return juce::Result::ok();
            }
            if (phase != Phase::ready && phase != Phase::failed)
                return juce::Result::fail(
                    "automationRecording.notReady: Seal producer input before committing");
            if (!std::isfinite(rate) || rate <= 0 || !std::isfinite(stopSeconds) ||
                stopSeconds < originSeconds)
                return juce::Result::fail("automationRecording.invalidClock: Missing fixed source cutoff");
            auto next = *expected;
            const double length = std::max(1. / rate, stopSeconds - originSeconds) * tempo / 60.;
            for (const auto &t : targets)
            {
                if (std::none_of(events.begin(), events.end(), [&](const auto &e) { return e.key == t.key; }))
                    continue;
                AutomationSource source;
                source.id = next.allocate();
                source.length = length;
                source.name = (t.name + " - Recorded MIDI").toStdString();
                source.links = {{t.key, t.minimum, t.maximum}};
                source.points.push_back({next.allocate(), 0, t.normalizedManual(), AutomationShape::hold, 0});
                float last = t.normalizedManual();
                for (const auto &e : events)
                    if (e.key == t.key)
                    {
                        const double beat =
                            juce::jlimit(0., length, (e.seconds - originSeconds) * tempo / 60.);
                        last = (e.value - t.minimum) / (t.maximum - t.minimum);
                        if (beat == source.points.back().beat)
                            source.points.back().value = last;
                        else
                            source.points.push_back({next.allocate(), beat, last, AutomationShape::hold, 0});
                    }
                if (source.points.back().beat == length && source.points.size() == 1)
                    return juce::Result::fail(
                        "automationRecording.invalidClock: Positive capture span required");
                if (source.points.back().beat < length)
                    source.points.push_back({next.allocate(), length, last, AutomationShape::hold, 0});
                next.automation.initialValues.push_back({t.key, t.manual});
                next.automation.placements.push_back(
                    {next.allocate(), source.id, originBeat, length, 0, lane, false});
                next.automation.sources.push_back(std::move(source));
            }
            if (auto result = next.validate(); result.failed())
                return result;
            output = std::move(next);
            return juce::Result::ok();
        }
        catch (...)
        {
            return juce::Result::fail(
                "automationRecording.preparationFailed: Complete recorded events retained");
        }
    }
};
} // namespace vibera
