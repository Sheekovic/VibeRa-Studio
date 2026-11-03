// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PhysicalBurstContract.h"
#include "PluginProtocol.h"
#include <cmath>
#include <memory>
#include <new>

namespace vibera
{
enum class PluginTransportStatus : unsigned
{
    ready,
    invalidPlan,
    invalidRate,
    sourceRefused,
    allocationFailed,
    preparationFailed,
    burstMissing,
    burstAlreadyActive,
    serialReused,
    sourceSpanExceeded,
    sequenceExhausted,
    workerUnavailable
};
struct PluginTransportOutcome
{
    PluginTransportStatus status = PluginTransportStatus::invalidPlan;
    transport::BurstOutcome source;
    int sourceQuota = 0, processedFrames = 0;
    bool succeeded() const noexcept
    {
        return status == PluginTransportStatus::ready;
    }
};

// CONTROL-created immutable session identity, never a cast integer policy.
// Pins an actual manager's safe metadata reader and the full prepared source
// budget. Reconstructing an equivalent plan is a DIFFERENT reuse identity.
// Caller serializes manager device/configuration changes during CONTROL preparation.
// No owning handle may be acquired, copied or released in a real-time callback.
class PreparedPluginTransport final
{
  public:
    using Ptr = std::shared_ptr<const PreparedPluginTransport>;
    static constexpr int dryCapacity = 131072;
    PreparedPluginTransport(const PreparedPluginTransport &) = delete;
    PreparedPluginTransport &operator=(const PreparedPluginTransport &) = delete;

    static PluginTransportOutcome prepare(juce::AudioDeviceManager &manager, int quantum,
                                          PluginLatencyStamp nativeLatency, int maximumSourceScalePpm,
                                          int explicitProviderFrames, Ptr &output) noexcept
    {
        PluginTransportOutcome result;
        try
        {
            auto *device = manager.getCurrentAudioDevice();
            if (!device)
                return result;
            const double rate = device->getCurrentSampleRate();
            if (!std::isfinite(rate) || rate < 8000 || rate > 192000)
            {
                result.status = PluginTransportStatus::invalidRate;
                return result;
            }
            auto reader = manager.getPhysicalCallbackReader(); // CONTROL ownership acquisition only.
            juce::AudioDeviceManager::PhysicalCallbackSnapshot snapshot;
            transport::PhysicalBurstBudget budget;
            const bool known = reader.read(snapshot);
            result.source = transport::preparePhysicalBurstBudget(
                known, snapshot, manager.getCurrentPhysicalCallbackBound(), quantum, nativeLatency,
                maximumSourceScalePpm, explicitProviderFrames, budget);
            if (result.source.status != transport::BurstStatus::ready)
            {
                result.status = PluginTransportStatus::sourceRefused;
                return result;
            }
            if (budget.packets < 2 || budget.packets > ipc::slots ||
                budget.packets != 1 + (std::int64_t(budget.sourceFrames) + quantum - 1) / quantum ||
                budget.transportSamples != budget.packets * quantum ||
                nativeLatency.nativeSamples >= dryCapacity - budget.transportSamples)
                return result;
            Ptr prepared(new PreparedPluginTransport(rate, budget, std::move(reader)));
            output = std::move(prepared); // No output mutation until all validation/allocation succeeds.
            result.status = PluginTransportStatus::ready;
            result.sourceQuota = budget.sourceFrames;
            return result;
        }
        catch (const std::bad_alloc &)
        {
            result.status = PluginTransportStatus::allocationFailed;
            return result;
        }
        catch (...)
        {
            result.status = PluginTransportStatus::preparationFailed;
            return result;
        }
    }

    const transport::PhysicalBurstBudget &budget() const noexcept
    {
        return prepared;
    }
    double sampleRate() const noexcept
    {
        return rate;
    }
    bool read(juce::AudioDeviceManager::PhysicalCallbackSnapshot &out) const noexcept
    {
        return reader.read(out);
    }

  private:
    PreparedPluginTransport(double sampleRate, transport::PhysicalBurstBudget value,
                            juce::AudioDeviceManager::PhysicalCallbackReader source)
        : rate(sampleRate), prepared(value), reader(std::move(source))
    {
    }
    const double rate;
    const transport::PhysicalBurstBudget prepared;
    const juce::AudioDeviceManager::PhysicalCallbackReader reader;
};
} // namespace vibera
