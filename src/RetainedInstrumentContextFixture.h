// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"
#include <tracktion_engine/tracktion_engine.h>
#include <functional>
namespace vibera
{
// Closed internal experiment; CONTROL-only and valid only during its owning
// AudioEngine callback. No canonical command or general mutable DSP access.
struct RetainedInstrumentContextFixture
{
    tracktion::Edit &edit;
    tracktion::EditPlaybackContext &context;
    tracktion::Plugin::Ptr original;
    std::shared_ptr<PluginHost> worker;
    std::function<tracktion::Plugin::Ptr(const tracktion::Plugin::Ptr &, const juce::ValueTree &)>
        createRetaining;
    std::function<std::uintptr_t(const tracktion::Plugin::Ptr &)> runtimeIdentity;
    std::function<unsigned(const tracktion::Plugin::Ptr &)> runtimeAttachments;
    std::function<std::uint64_t(const tracktion::Plugin::Ptr &)> sourceCalls;
    std::function<int(const tracktion::Plugin::Ptr &)> heldKeys;
    std::function<std::function<bool(const tracktion::Plugin &)>(const tracktion::Plugin::Ptr &)>
        fixedInitialisationAdmission;
    std::function<juce::String()> captureOwners;
    // Closed CONTROL readback from the actual committed projection, never cache.
    std::function<tracktion::Plugin::Ptr()> currentSampler;
};
} // namespace vibera
