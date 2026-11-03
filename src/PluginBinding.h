// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginParameters.h"
#include <juce_data_structures/juce_data_structures.h>
#include <cstdint>
#include <span>
#include <vector>

namespace vibera
{
// Saved declaration, not authority to write a runtime plugin. Preparation must
// validate current native metadata before activating an automation destination.
struct PluginBinding
{
    std::uint32_t id = 0, flags = 1;
    int steps = 0;
    float manual = 0;
    juce::String name, label;
    bool operator==(const PluginBinding &) const = default;
};
bool validPluginBindings(std::span<const PluginBinding>);
juce::ValueTree pluginBindingsState(std::span<const PluginBinding>);
juce::Result parsePluginBindings(const juce::ValueTree &, std::vector<PluginBinding> &);
// Control-thread metadata admission only. Event capacity and worker generation
// must also be admitted at publication/processing; this is not a DSP permit.
enum class PluginBindingStatus
{
    ready,
    invalidDeclaration,
    catalogueUnavailable,
    staleCatalogue,
    missingParameter,
    unavailableParameter,
    notAutomatable,
    schemaChanged
};
struct PluginBindingAdmission
{
    PluginBindingStatus status = PluginBindingStatus::catalogueUnavailable;
    unsigned generation = 0;     // Nonzero only for a complete successful admission.
    std::uint32_t parameter = 0; // Native ID; zero is a valid ID.
};
PluginBindingAdmission admitPluginBindings(std::span<const PluginBinding>, const PluginParameterCatalogue &,
                                           unsigned currentGeneration);
} // namespace vibera
