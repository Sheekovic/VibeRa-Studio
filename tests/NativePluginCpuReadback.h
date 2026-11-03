// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTROL-only diagnostic. Include/call only in separate overhead acceptance.
#pragma once
#include <tracktion_engine/tracktion_engine.h>
#include <cmath>
#include <cstdint>

namespace source_burst_acceptance
{
inline juce::String cpuCsvText(const juce::String &text)
{
    return "\"" + text.replace("\"", "\"\"").replaceCharacter('\n', ' ').replaceCharacter('\r', ' ') + "\"";
}
inline constexpr const char *nativePluginCpuCsvHeader =
    "outer,physical,capturedQpc,meterCompiled,editPointer,workerEdit,contextPointer,pluginPointer,"
    "nativeItemId,type,name,enabled,measuredByPluginPolicy,cpuRatio,finite,endpoint\n";

// Caller: serialized CONTROL immediately AFTER callback timestamp is frozen,
// with all fixture Edits alive. No ownership survives this function. The returned
// data is smoothed/clamped native telemetry, NEVER a raw per-callback duration.
inline void appendNativePluginCpuControl(tracktion::engine::Engine &engine,
                                         const tracktion::engine::Edit *workerEdit, int outer, int physical,
                                         juce::String &csv)
{
    const auto captured = juce::Time::getHighResolutionTicks();
    const auto pointer = [](const void *value)
    { return juce::String::toHexString(juce::uint64(reinterpret_cast<std::uintptr_t>(value))); };
    for (auto *edit : engine.getActiveEdits().getEdits())
    {
        if (!edit)
            continue;
        auto *context = edit->getTransport().getCurrentPlaybackContext();
        // Existing API returns borrowed Plugin pointers. Enumerate on
        // CONTROL before the next callback or Edit destruction; retain none.
        const auto plugins = tracktion::engine::getAllPlugins(*edit, true);
        for (const auto &plugin : plugins)
        {
            if (!plugin)
                continue;
            const auto ratio = plugin->getCpuUsage();
            csv += juce::String(outer) + "," + juce::String(physical) + "," + juce::String(captured) + "," +
                   juce::String(int(TRACKTION_ENABLE_PLUGIN_CPU_MEASUREMENT)) + "," + pointer(edit) + "," +
                   juce::String(int(edit == workerEdit)) + "," + pointer(context) + "," +
                   pointer(plugin) + "," + cpuCsvText(plugin->itemID.toString()) + "," +
                   cpuCsvText(plugin->getPluginType()) + "," + cpuCsvText(plugin->getName()) + "," +
                   juce::String(int(plugin->isEnabled())) + "," +
                   juce::String(int(plugin->shouldMeasureCpuUsage())) + "," + juce::String(ratio, 12) + "," +
                   juce::String(int(std::isfinite(ratio))) + "," +
                   juce::String(int(ratio <= 0 || ratio >= 1)) + "\n";
        }
    }
}
} // namespace source_burst_acceptance
