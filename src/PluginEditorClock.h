// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>
namespace vibera
{
// Control/message threads only. A common raw Windows QPC epoch, converted to
// wrap-safe32-bit milliseconds identically in host and worker. No per-process
// multimedia timer precision or JUCE high-resolution clock calibration.
std::uint32_t pluginEditorMilliseconds();
} // namespace vibera
