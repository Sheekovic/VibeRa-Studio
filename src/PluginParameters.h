// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginProtocol.h"
#include <juce_core/juce_core.h>
#include <vector>
namespace vibera
{
struct PluginParameter
{
    std::uint32_t id = 0, flags = 0;
    std::uint32_t availability = 3;
    int steps = 0, unit = 0;
    float value = 0, defaultValue = 0;
    juce::String name, label;
    bool operator==(const PluginParameter &) const = default;
    bool writable() const noexcept
    {
        return (availability & 1u) != 0 && (flags & ((1u << 1) | (1u << 4))) == 0;
    }
    bool automatable() const noexcept
    {
        return writable() && (flags & 1u) != 0;
    }
    bool hasDefault() const noexcept
    {
        return (availability & 2u) != 0;
    }
};
struct PluginParameterCatalogue
{
    unsigned serial = 0, generation = 0;
    int reportedCount = 0, error = 0;
    juce::String diagnostic;
    std::vector<PluginParameter> parameters;
};
struct PluginParameterResult
{
    unsigned serial = 0;
    std::uint32_t id = 0;
    float value = 0;
    // Worker:0 applied,1 stale,2 unavailable,3 malformed. Host:4 worker unavailable,
    // 5 invalid acknowledgement,6 control timeout. Failure values are placeholders.
    int result = 0;
    // Appended so existing four-field aggregate fixtures remain explicitly unknown.
    float previousValue = 0;
    bool beforeKnown = false;
    unsigned patchCount = 0;
    std::array<ipc::ParameterPatchValue, 64> patch{};
};
} // namespace vibera
