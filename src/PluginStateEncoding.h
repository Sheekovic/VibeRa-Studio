// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>

namespace vibera
{
// Persistence/preparation only. Validate the length-prefixed JUCE representation
// before allowing its decoder to allocate. Failed decoding preserves output.
bool validPluginStateEncoding(const juce::String &) noexcept;
bool decodePluginState(const juce::String &, juce::MemoryBlock &);
} // namespace vibera
