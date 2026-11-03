// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginAutomationPreparation.h"
#include "PluginAutomationBlock.h"
#include <cmath>

namespace vibera
{
juce::Result prepareNativeAutomationRoutes(const Project &project, LiveDocument &document,
                                           const std::map<Id, std::shared_ptr<PluginHost>> &hosts,
                                           double rate, bool allowMissingWorkers)
{
    std::map<Id, std::vector<PluginBinding>> selected;
    for (const auto &lane : document.automation.compiledLanes())
    {
        if (lane.key.kind != AutomationParameter::pluginNormalized)
            continue;
        const auto *spec = project.plugin(lane.key.owner);
        if (!spec)
            return juce::Result::fail("Automation plugin is absent from this project");
        const auto binding = std::find_if(spec->automationBindings.begin(), spec->automationBindings.end(),
                                          [&](const auto &b) { return b.id == lane.key.parameter; });
        if (binding == spec->automationBindings.end())
            return juce::Result::fail("Declare native parameter " + juce::String(lane.key.parameter) +
                                      " before automating " + spec->name);
        selected[spec->id].push_back(*binding);
    }
    std::vector<LiveDocument::NativeAutomationRoute> prepared;
    prepared.reserve(selected.size());
    for (const auto &[id, bindings] : selected)
    {
        const auto *spec = project.plugin(id);
        const auto found = hosts.find(id);
        // A visibly missing editing placeholder retains saved curves/state.
        // There is no processor to target; renderer admission remains strict.
        if (allowMissingWorkers && (found == hosts.end() || !found->second))
            continue;
        if (found == hosts.end() || !found->second || !found->second->alive())
            return juce::Result::fail("Automation worker unavailable: " + spec->name +
                                      "; saved state retained");
        auto &host = *found->second;
        const auto admission = host.validateAutomationBindings(bindings);
        if (admission.status != PluginBindingStatus::ready)
        {
            if (admission.status == PluginBindingStatus::catalogueUnavailable ||
                admission.status == PluginBindingStatus::staleCatalogue)
            {
                host.requestParameterCatalogue();
                return juce::Result::fail("Preparing native automation metadata for " + spec->name +
                                          "; retrying without replacing playback");
            }
            return juce::Result::fail("Native automation declaration no longer matches " + spec->name +
                                      " parameter " + juce::String(admission.parameter) +
                                      "; review its availability and capabilities");
        }
        if (!std::isfinite(rate) || rate <= 0 || !std::isfinite(document.tempo) || document.tempo <= 0)
            return juce::Result::fail("Invalid native automation device/tempo configuration");
        const auto workload = PluginAutomationBlock::admit(
            document.automation, id, document.tempo / (60 * rate), host.processingQuantum());
        if (workload.result != PluginAutomationBlock::Result::ok)
            return juce::Result::fail("Native automation exceeds the prepared packet budget for " +
                                      spec->name + "; simplify concurrent curves or their rate");
        prepared.push_back({id, admission.generation, host.runtimeIdentity()});
    }
    document.nativeAutomation.swap(prepared);
    return juce::Result::ok();
}
} // namespace vibera
