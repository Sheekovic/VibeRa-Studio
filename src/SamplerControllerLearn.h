// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerControllerLinks.h"
#include <vector>

namespace vibera
{
// CONTROL-owned asynchronous operation; no component or callback owns project pointers.
struct SamplerControllerLearn
{
    juce::String token, project, state = "pending", error;
    std::uint64_t serial = 0, revision = 0, projection = 0;
    SamplerControllerLink candidate;
    bool captured = false, documentChanged = false, effectiveApplied = false;
    std::vector<std::int64_t> replaced;
};
} // namespace vibera
