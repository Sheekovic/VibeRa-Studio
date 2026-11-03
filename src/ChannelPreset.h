// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"

namespace vibera
{
// Portable Channel preset (.vibch): a sampler Channel or a native/VST3
// instrument Channel with its captured plugin state. The payload is a minimal
// one-Channel VibeRa project, so loading reuses the full project validation.
// Score, routing, group membership, mute state and controller links are NOT
// part of a preset; applying one never touches notes or Mixer assignment.
inline constexpr const char *channelPresetExtension = ".vibch";
inline constexpr int channelPresetFormat = 1;

// Fails for missing and Audio Clip Channels. Callers must pass FRESH plugin state.
juce::Result encodeChannelPreset(const Project &, Id channel, juce::String &xml);
juce::Result encodeChannelPreset(const Channel &, juce::String &xml);
// Decodes and validates; the returned Channel has id 0 and neutral routing.
juce::Result decodeChannelPreset(const juce::String &xml, Channel &);
// Copies the preset's sound/appearance (and instrument with a new plugin ID, or
// none) onto an existing score Channel, preserving its id, Mixer insert, group,
// mute and lock state. Plugin automation bindings are not carried.
juce::Result applyChannelPreset(Project &, Id channel, const Channel &preset);
} // namespace vibera
