// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
namespace vibera
{
const MixerInsert *mixerInsert(const Project &, int);
const MixerRoute *mixerRoute(const Project &, int source, int target);
juce::Result canMixerRoute(const Project &, int source, int target);
juce::Result validateMixerRouting(const Project &);
juce::Result setMixerRoute(Project &, int source, int target, bool enabled, bool only = false);
juce::Result setMixerSend(Project &, int source, int target, double gain);
// Strong private-model mutation: exact effect ID, one aux assignment or clear.
// No native bus negotiation, state capture, graph publication or history here.
juce::Result setPluginDetector(Project &, Id effect, std::optional<PluginDetectorAssignment>);
juce::var pluginDetectorState(const Project &, Id effect); // Saved model, no runtime-availability claim.
juce::var mixerDetectorReferences(const Project &, int sourceMixer);
juce::var mixerRoutesState(const Project &, int source, bool availability = false);
} // namespace vibera
