// SPDX-License-Identifier: AGPL-3.0-or-later
#include "StoppedEffectWorkers.h"
#include "PluginStateEncoding.h"
#include "effects/NativeEffectsRegistry.h"
#include <cmath>
#include <set>

namespace vibera
{
namespace
{
bool currentTransport(const PreparedPluginTransport::Ptr &plan) noexcept
{
    if (!plan)
        return false;
    juce::AudioDeviceManager::PhysicalCallbackSnapshot physical;
    const bool known = plan->read(physical);
    const auto &budget = plan->budget();
    const auto admitted = transport::admitPhysicalBurst(known, physical, budget);
    return known && physical.maximumOuterFrames <= budget.physicalFrames &&
           (admitted.status == transport::BurstStatus::ready ||
            (admitted.status == transport::BurstStatus::noPhysicalCallback && physical.outerFrames == 0 &&
             physical.maximumOuterFrames == 0));
}
} // namespace
StoppedEffectWorkers::~StoppedEffectWorkers() = default;
juce::Result StoppedEffectWorkers::prepare(std::vector<PluginSpec> specs, double sampleRate, int blockSize,
                                           const std::function<bool()> &cancelled, Ptr &output)
{
    return prepareImpl(std::move(specs), sampleRate, blockSize, cancelled, output, {});
}
juce::Result StoppedEffectWorkers::prepare(std::vector<PluginSpec> specs,
                                           const PreparedPluginTransport::Ptr &plan,
                                           const std::function<bool()> &cancelled, Ptr &output)
{
    if (!plan)
        return juce::Result::fail("effects.transportRequired: Exact live source transport plan required");
    return prepareImpl(std::move(specs), plan->sampleRate(), plan->budget().quantum, cancelled, output, plan);
}
juce::Result StoppedEffectWorkers::prepareImpl(std::vector<PluginSpec> specs, double sampleRate,
                                               int blockSize, const std::function<bool()> &cancelled,
                                               Ptr &output, const PreparedPluginTransport::Ptr &plan)
{
    try
    {
        if (specs.empty() || specs.size() > 16 || !std::isfinite(sampleRate) || sampleRate <= 0 ||
            blockSize <= 0)
            return juce::Result::fail(
                "effects.invalidWorkers: Expected1..16 effects and exact native configuration");
        if (plan && !currentTransport(plan))
            return juce::Result::fail("effects.transportChanged: Actual live device contract changed");
        auto prepared = std::shared_ptr<StoppedEffectWorkers>(new StoppedEffectWorkers);
        prepared->rate = sampleRate;
        prepared->block = blockSize;
        prepared->transportPlan = plan;
        prepared->specs = std::move(specs);
        std::set<std::int64_t> seen;
        for (const auto &spec : prepared->specs)
        {
            if (spec.id <= 0 || !seen.insert(spec.id).second || spec.instrument || spec.name.isEmpty() ||
                spec.descriptor.length() > 65536 || spec.state.length() > 24 * 1024 * 1024 ||
                !validPluginStateEncoding(spec.state) || !validPluginBindings(spec.automationBindings) ||
                !std::isfinite(spec.wet) || spec.wet < 0 || spec.wet > 1)
                return juce::Result::fail("effects.invalidWorkers: Invalid effect state");
            auto xml = juce::XmlDocument::parse(spec.descriptor);
            juce::PluginDescription type;
            if (!xml || !type.loadFromXml(*xml) || type.isInstrument ||
                (type.pluginFormatName != "VST3" && !effects::isNativeDescription(type)))
                return juce::Result::fail(
                    "effects.invalidWorkers: Exact VibeRa native or Windows VST3 effect required");
            if (cancelled && cancelled())
                return juce::Result::fail(
                    "effects.cancelled: Worker preparation cancelled before publication");
            auto host = plan ? std::make_shared<PluginHost>(plan) : std::make_shared<PluginHost>();
            if (auto result = host->start(spec, sampleRate, blockSize, false, cancelled); result.failed())
                return juce::Result::fail("effects.workerPreparationFailed: " + result.getErrorMessage());
            if (!host->alive() || host->busLayout().auxiliaryChannels != 0 ||
                host->processingQuantum() != blockSize || host->latencySamples() < 0 ||
                host->editorWindowState().exists || host->editorBusy() || host->pendingParameterRequests())
                return juce::Result::fail(
                    "effects.invalidWorkers: Unsupported or incomplete private worker state");
            if (plan && (!host->matchesPrepared(spec, sampleRate, blockSize, plan) ||
                         host->transportPolicy() != PluginHost::TransportPolicy::preparedBurst ||
                         host->transportPacketCount() != plan->budget().packets ||
                         host->transportLatencySamples() != plan->budget().transportSamples))
                return juce::Result::fail("effects.transportChanged: Private worker transport differs");
            prepared->hosts.emplace(spec.id, std::move(host));
        }
        if (cancelled && cancelled())
            return juce::Result::fail(
                "effects.cancelled: Private workers discarded before graph preparation");
        if (plan && !currentTransport(plan))
            return juce::Result::fail("effects.transportChanged: Device changed during private startup");
        output = std::move(prepared);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("effects.workerPreparationFailed: Existing workers and model retained");
    }
}
} // namespace vibera
