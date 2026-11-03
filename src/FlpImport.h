// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"

namespace vibera
{
// What an FLP import kept and what it could not convert.
struct FlpImportReport
{
    juce::String flVersion;
    int channels = 0, patterns = 0, notes = 0, patternClips = 0, audioClips = 0;
    juce::StringArray unconvertedPlugins; // FL generator/effect names left as empty placeholders.
    juce::StringArray missingSamples;
    int automationClipsSkipped = 0, unusedChannelsSkipped = 0;
    juce::String summary() const;
};
// Imports an FL Studio project (.flp) into a new VibeRa project: tempo, time signature,
// sampler and audio-clip Channels with their samples, Channel volume/pan/Mixer insert,
// Patterns with notes, and the first arrangement's Playlist (Pattern and audio clips).
// FL's own plugins cannot be converted; their Channels are kept, named and silent.
// Independent implementation of the publicly documented FLP event format.
juce::Result importFlp(const juce::File &file, Project &result, FlpImportReport &report);
} // namespace vibera
