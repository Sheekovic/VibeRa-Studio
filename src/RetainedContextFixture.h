// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"
#include <tracktion_engine/tracktion_engine.h>
#include <array>
#include <functional>

namespace vibera
{
// Internal original-fixture seam, CONTROL ONLY. No UI/MCP product operation and
// no canonical transaction. Valid only for the callback's owning AudioEngine.
struct RetainedContextFixture
{
    tracktion::Edit &edit;
    tracktion::EditPlaybackContext &context;
    // FixedSampler, RemoteEffect, MixerSidechainSend, in this exact order.
    std::array<tracktion::Plugin::Ptr, 3> original;
    std::shared_ptr<PluginHost> worker;
    std::function<tracktion::Plugin::Ptr(unsigned, const juce::ValueTree &)> create;
    // Explicit supported positive arm; create(kind=0) remains the fresh-clone
    // negative diagnostic. Neither arm rewrites an already prepared MIDI lane.
    std::function<tracktion::Plugin::Ptr(const juce::ValueTree &)> createRetainingSampler;
    std::function<std::uintptr_t(const tracktion::Plugin::Ptr &)> samplerRuntimeIdentity;
    std::function<unsigned(const tracktion::Plugin::Ptr &)> samplerRuntimeAttachments;
    using Replacements = std::array<tracktion::Plugin::Ptr, 3>;
    std::function<std::function<bool(const tracktion::Plugin &)>(const Replacements &)>
        fixedInitialisationAdmission;
    std::function<juce::String()> captureOwners;
    std::function<std::uint64_t(const tracktion::Plugin::Ptr &)> samplerCalls;
    std::function<int(const tracktion::Plugin::Ptr &)> samplerHeldKeys;
};
} // namespace vibera
