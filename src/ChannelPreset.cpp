// SPDX-License-Identifier: AGPL-3.0-or-later
#include "ChannelPreset.h"

namespace vibera
{
namespace
{
bool scoreChannel(const Channel &c)
{
    return !c.audioClip;
}
} // namespace

juce::Result encodeChannelPreset(const Project &project, Id channel, juce::String &xml)
{
    const auto *source = project.channel(channel);
    if (!source)
        return juce::Result::fail("channelPreset.missingChannel: Existing Channel required");
    return encodeChannelPreset(*source, xml);
}

juce::Result encodeChannelPreset(const Channel &source, juce::String &xml)
{
    if (!scoreChannel(source))
        return juce::Result::fail("channelPreset.unsupportedTarget: Audio Clip Channels have no preset");
    auto document = Project::empty();
    document.name = "Channel preset";
    document.channels.clear();
    auto copy = source;
    copy.id = document.allocate();
    copy.mixer = 0;
    copy.group = 0;
    copy.muted = false;
    copy.muteLocked = false;
    if (copy.instrument)
    {
        copy.instrument->id = document.allocate();
        copy.instrument->automationBindings.clear();
        copy.instrument->bypass = false;
    }
    document.channels.push_back(std::move(copy));
    if (auto valid = document.validate(); valid.failed())
        return juce::Result::fail("channelPreset.encodeFailed: " + valid.getErrorMessage());
    juce::XmlElement root("VIBERA_CHANNEL_PRESET");
    root.setAttribute("format", channelPresetFormat);
    auto body = document.state().createXml();
    if (!body)
        return juce::Result::fail("channelPreset.encodeFailed: Project encoding failed");
    root.addChildElement(body.release());
    xml = root.toString();
    return juce::Result::ok();
}

juce::Result decodeChannelPreset(const juce::String &xml, Channel &out)
{
    const auto root = juce::parseXML(xml);
    if (!root || !root->hasTagName("VIBERA_CHANNEL_PRESET"))
        return juce::Result::fail("channelPreset.decodeFailed: Not a VibeRa Channel preset");
    if (root->getIntAttribute("format", 0) != channelPresetFormat)
        return juce::Result::fail("channelPreset.unsupportedFormat: Unsupported Channel preset format");
    if (root->getNumChildElements() != 1)
        return juce::Result::fail("channelPreset.decodeFailed: Exactly one project payload required");
    Project document;
    if (auto parsed = Project::parse(juce::ValueTree::fromXml(*root->getFirstChildElement()), document);
        parsed.failed())
        return juce::Result::fail("channelPreset.decodeFailed: " + parsed.getErrorMessage());
    if (document.channels.size() != 1 || !scoreChannel(document.channels.front()))
        return juce::Result::fail("channelPreset.decodeFailed: Exactly one score Channel required");
    out = document.channels.front();
    out.id = 0;
    out.mixer = 0;
    out.group = 0;
    out.muted = false;
    out.muteLocked = false;
    return juce::Result::ok();
}

juce::Result applyChannelPreset(Project &project, Id channel, const Channel &preset)
{
    auto *target = project.channel(channel);
    if (!target)
        return juce::Result::fail("channelPreset.missingChannel: Existing Channel required");
    if (!scoreChannel(*target))
        return juce::Result::fail("channelPreset.unsupportedTarget: Audio Clip Channels cannot take a preset");
    if (!scoreChannel(preset) || (!preset.instrument && preset.source.isEmpty()))
        return juce::Result::fail("channelPreset.invalidPreset: Sampler source or instrument required");
    target->name = preset.name;
    target->source = preset.source;
    target->gain = preset.gain;
    target->pan = preset.pan;
    target->sampler = preset.sampler;
    target->swingMix = preset.swingMix;
    target->truncateSwing = preset.truncateSwing;
    target->colour = preset.colour;
    target->icon = preset.icon;
    if (preset.instrument)
    {
        auto instrument = *preset.instrument;
        instrument.id = project.allocate(); // Fresh plugin identity; never reuse a removed worker.
        instrument.automationBindings.clear();
        target->instrument = std::move(instrument);
    }
    else
        target->instrument.reset();
    return juce::Result::ok();
}
} // namespace vibera
