// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
namespace juce
{
class AudioPluginInstance;
// Internal statically paired JUCE patch entrypoint; isolated DSP thread, while audio packets are paused.
bool viberaFlushVST3Parameters(AudioPluginInstance &) noexcept;
} // namespace juce
