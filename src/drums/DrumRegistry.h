// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

namespace vibera::drums
{
inline juce::PluginDescription nativeDescription()
{
    juce::PluginDescription d;
    d.name = d.descriptiveName = "VibeRa Drum Synth";
    d.pluginFormatName = "VibeRa";
    d.category = "Instrument";
    d.manufacturerName = "VibeRa";
    d.version = "1";
    d.fileOrIdentifier = "vibera.instrument.drums";
    d.uniqueId = d.deprecatedUid = 0x56531002;
    d.numInputChannels = 0;
    d.numOutputChannels = 2;
    d.isInstrument = true;
    return d;
}
inline bool isNativeDescription(const juce::PluginDescription &d)
{
    const auto e = nativeDescription();
    return d.name == e.name && d.descriptiveName == e.descriptiveName && d.pluginFormatName == e.pluginFormatName &&
           d.category == e.category && d.manufacturerName == e.manufacturerName && d.version == e.version &&
           d.fileOrIdentifier == e.fileOrIdentifier && d.uniqueId == e.uniqueId && d.deprecatedUid == e.deprecatedUid &&
           d.numInputChannels == 0 && d.numOutputChannels == 2 && d.isInstrument && !d.hasSharedContainer &&
           !d.hasARAExtension;
}
} // namespace vibera::drums
