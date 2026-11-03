// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

namespace vibera::synth
{
inline juce::PluginDescription nativeDescription()
{
    juce::PluginDescription d;
    d.name = d.descriptiveName = "VibeRa Synthesizer";
    d.pluginFormatName = "VibeRa";
    d.category = "Instrument";
    d.manufacturerName = "VibeRa";
    d.version = "1";
    d.fileOrIdentifier = "vibera.instrument.synthesizer";
    d.uniqueId = d.deprecatedUid = 0x56531001;
    d.numInputChannels = 0;
    d.numOutputChannels = 2;
    d.isInstrument = true;
    return d;
}
inline bool isNativeDescription(const juce::PluginDescription &d)
{
    const auto expected = nativeDescription();
    return d.name == expected.name && d.descriptiveName == expected.descriptiveName &&
           d.pluginFormatName == expected.pluginFormatName && d.category == expected.category &&
           d.manufacturerName == expected.manufacturerName && d.version == expected.version &&
           d.fileOrIdentifier == expected.fileOrIdentifier && d.uniqueId == expected.uniqueId &&
           d.deprecatedUid == expected.deprecatedUid && d.numInputChannels == 0 &&
           d.numOutputChannels == 2 && d.isInstrument && !d.hasSharedContainer && !d.hasARAExtension;
}
}
