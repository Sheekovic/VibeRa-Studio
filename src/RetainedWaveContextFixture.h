// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>
#include <tracktion_engine/playback/graph/tracktion_WaveNode.h>
#include <tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h>
#include <functional>
namespace vibera
{
struct RetainedWaveObservation
{
    unsigned constructed = 0, waveNodes = 0, realTimeWaveNodes = 0;
    std::atomic<unsigned> calls{0};
    std::atomic<juce::int64> firstReference{-1};
};
// Closed CONTROL-only source fixture. No product command, general DSP access,
// model mutation, native state capture, custom audio generator or fake decoder.
struct RetainedWaveContextFixture
{
    tracktion::Edit &edit;
    tracktion::EditPlaybackContext &context;
    tracktion::AudioTrack &source;
    std::function<std::shared_ptr<tracktion::EditPlaybackContext::GraphOverlay>(
        int, const std::shared_ptr<RetainedWaveObservation> &, bool)>
        overlayFor;
    std::function<juce::String()> captureOwners;
};
} // namespace vibera
