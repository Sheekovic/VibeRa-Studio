// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveTimeline.h"
#include <map>

namespace vibera
{
// Control thread only, serialized with this host's metadata readers. Does not wait
// for a catalogue: an unavailable snapshot requests refresh and returns for retry.
// Success replaces runtime routes atomically in the unpublished document; failure
// leaves its existing routes intact. Saved bindings alone never authorize DSP.
juce::Result prepareNativeAutomationRoutes(const Project &, LiveDocument &,
                                           const std::map<Id, std::shared_ptr<PluginHost>> &,
                                           double sampleRate, bool allowMissingWorkers = false);
} // namespace vibera
