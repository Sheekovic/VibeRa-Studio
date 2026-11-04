// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// The built-in instruments, in picker order: VibeRa Synthesizer, VibeRa Drum Synth.
#include "drums/DrumRegistry.h"
#include "synth/SynthRegistry.h"
#include <vector>

namespace vibera
{
inline bool isNativeInstrument(const juce::PluginDescription &d)
{
    return synth::isNativeDescription(d) || drums::isNativeDescription(d);
}
inline std::vector<juce::PluginDescription> nativeInstrumentDescriptions()
{
    return {synth::nativeDescription(), drums::nativeDescription()};
}
} // namespace vibera
