// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace vibera
{
enum class MixerAutomationControl : unsigned
{
    gain = 0,
    pan = 1,
    channelGain = 2, // Channel (Rack) volume/pan: owner is the stable Channel ID.
    channelPan = 3,
    stereoSeparation = 5,
    send = 6
};
inline bool validMixerAutomationControl(MixerAutomationControl control) noexcept
{
    return control == MixerAutomationControl::gain || control == MixerAutomationControl::pan ||
           control == MixerAutomationControl::stereoSeparation || control == MixerAutomationControl::send ||
           control == MixerAutomationControl::channelGain || control == MixerAutomationControl::channelPan;
}
inline const char *mixerAutomationControlName(MixerAutomationControl control) noexcept
{
    switch (control)
    {
    case MixerAutomationControl::gain:
        return "gain";
    case MixerAutomationControl::pan:
        return "pan";
    case MixerAutomationControl::stereoSeparation:
        return "stereoSeparation";
    case MixerAutomationControl::send:
        return "send";
    case MixerAutomationControl::channelGain:
        return "channelGain";
    case MixerAutomationControl::channelPan:
        return "channelPan";
    }
    return "unsupported";
}
inline std::optional<MixerAutomationControl> parseMixerAutomationControl(const juce::var &value)
{
    if (!value.isString())
        return {};
    const auto text = value.toString();
    if (text == "gain")
        return MixerAutomationControl::gain;
    if (text == "pan")
        return MixerAutomationControl::pan;
    if (text == "stereoSeparation")
        return MixerAutomationControl::stereoSeparation;
    if (text == "channelGain")
        return MixerAutomationControl::channelGain;
    if (text == "channelPan")
        return MixerAutomationControl::channelPan;
    if (text == "send")
        return MixerAutomationControl::send;
    return {};
}
inline bool isMixerAutomationTargetCommand(const juce::String &id)
{
    return id == "automation.create" || id == "automation.init" || id == "automation.init.remove" ||
           id == "automation.link" || id == "automation.unlink" || id == "automation.range" ||
           id == "automation.modulator";
}
struct MixerAutomationTarget
{
    AutomationKey key;
    float minimum = 0, maximum = 2, manual = 1;
    juce::String name, units;
    bool contains(double value) const noexcept
    {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    }
    float normalizedManual() const noexcept
    {
        return (manual - minimum) / (maximum - minimum);
    }
};
// CONTROL-only shared resolver. No row/ordinal, selected-target fallback or DSP access.
inline juce::Result resolveMixerAutomationTarget(const Project &project, int insert,
                                                 MixerAutomationControl control, MixerAutomationTarget &out,
                                                 std::optional<int> sendTarget = {}, std::int64_t channel = 0)
{
    if (!validMixerAutomationControl(control))
        return juce::Result::fail(
            "automation.unsupportedTarget: Supported targets are gain, pan, stereoSeparation, send, channelGain and channelPan");
    if (control == MixerAutomationControl::channelGain || control == MixerAutomationControl::channelPan)
    {
        // Channel Rack volume (linear0..2 like the Rack knob range) or pan (-1..1).
        const auto *owner = project.channel(Id(channel));
        if (!owner || sendTarget)
            return juce::Result::fail("automation.missingTarget: An existing Channel ID is required");
        MixerAutomationTarget value;
        value.key = {static_cast<AutomationParameter>(unsigned(control)), channel, 0};
        const bool gain = control == MixerAutomationControl::channelGain;
        value.minimum = gain ? 0.f : -1.f;
        value.maximum = gain ? 2.f : 1.f;
        value.manual = gain ? owner->gain : owner->pan;
        value.name = owner->name + (gain ? " - Volume" : " - Pan");
        value.units = gain ? "linearGain" : "bipolarPan";
        out = std::move(value);
        return juce::Result::ok();
    }
    if ((control == MixerAutomationControl::send) != sendTarget.has_value())
        return juce::Result::fail(
            "automation.invalidArguments: Send automation requires its exact destination only");
    const auto found = std::find_if(project.mixer.begin(), project.mixer.end(),
                                    [insert](const auto &owner) { return owner.index == insert; });
    if (found == project.mixer.end())
        return juce::Result::fail("automation.missingTarget: Stable Mixer insert is unavailable");
    MixerAutomationTarget value;
    value.key = {static_cast<AutomationParameter>(unsigned(control)), insert, 0};
    value.name = found->name + " - ";
    if (control == MixerAutomationControl::send)
    {
        const auto destination = std::find_if(project.mixer.begin(), project.mixer.end(),
                                              [&](const auto &row) { return row.index == *sendTarget; });
        if (insert <= 0 || *sendTarget < 0 || *sendTarget == insert || destination == project.mixer.end())
            return juce::Result::fail("automation.invalidSendTarget: Existing distinct source and "
                                      "destination required; Master is destination only");
        const auto edge = std::find_if(found->routes.begin(), found->routes.end(),
                                       [&](const auto &route) { return route.target == *sendTarget; });
        if (edge == found->routes.end())
            return juce::Result::fail("automation.missingRoute: Enable the exact Mixer route first");
        value.key.parameter = std::uint32_t(*sendTarget);
        value.manual = edge->gain;
        value.name = found->name + " -> " + destination->name + " - Send";
        value.units = "linearGain";
    }
    else if (control == MixerAutomationControl::gain)
    {
        value.manual = found->gain;
        value.name += "Volume";
        value.units = "linearGain";
    }
    else
    {
        value.minimum = -1;
        value.maximum = 1;
        value.manual = control == MixerAutomationControl::pan ? found->pan : found->stereoSeparation;
        value.name += control == MixerAutomationControl::pan ? "Pan" : "Stereo separation";
        value.units = control == MixerAutomationControl::pan ? "bipolarPan" : "bipolarSeparation";
    }
    out = std::move(value);
    return juce::Result::ok();
}
inline juce::var mixerAutomationTargetState(const MixerAutomationTarget &target)
{
    auto item = std::make_unique<juce::DynamicObject>();
    item->setProperty(
        "kind", mixerAutomationControlName(static_cast<MixerAutomationControl>(unsigned(target.key.kind))));
    item->setProperty("owner", juce::int64(target.key.owner));
    item->setProperty("parameter", juce::int64(target.key.parameter));
    if (target.key.kind == AutomationParameter::mixerSend)
    {
        item->setProperty("sourceInsert", juce::int64(target.key.owner));
        item->setProperty("destinationInsert", juce::int64(target.key.parameter));
    }
    item->setProperty("minimum", target.minimum);
    item->setProperty("maximum", target.maximum);
    item->setProperty("manual", target.manual);
    item->setProperty("name", target.name);
    item->setProperty("units", target.units);
    item->setProperty("completionScope", "canonicalAutomationTarget");
    item->setProperty("dspSettlingAcknowledged", false);
    return juce::var(item.release());
}
} // namespace vibera
