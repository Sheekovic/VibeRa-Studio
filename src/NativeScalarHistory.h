// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>
namespace vibera
{
// Bounded CONTROL XML-rewrite admission. Larger gestures must finish as separate
// actual owned operations, never an unbounded callback/native-history batch.
inline constexpr std::size_t nativeScalarHistoryMaxEdits = 96; // One whole native patch (ipc::patchValues), e.g. a Synth preset.
inline constexpr std::size_t nativeScalarHistoryRewriteBytes = 64 * 1024 * 1024;
// CONTROL-only witnessed scalar edit. Native opaque state is always freshly
// captured by the application; this is never a substitute for that capture.
struct NativeScalarEdit
{
    std::int64_t plugin = 0;
    std::uint32_t parameter = 0;
    float before = 0, after = 0;
    std::optional<float> manualBefore; // Filled canonically, only for an existing binding.
};
} // namespace vibera
