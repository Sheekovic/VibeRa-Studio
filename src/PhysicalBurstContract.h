// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginLatency.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <cstdint>
#include <limits>

namespace vibera::transport
{
enum class BurstStatus
{
    ready,
    telemetryUnavailable,
    managerReleased,
    deviceStopped,
    counterExhausted,
    writerConflict,
    deviceChanged,
    unknownProviderBound,
    invalidPreparation,
    packetCapacityExceeded,
    delayCapacityExceeded,
    physicalBurstExceeded,
    sourceBurstExceeded,
    noPhysicalCallback
};
enum class BurstProof
{
    pinnedWindowsFactory,
    explicitProviderBudget
};
struct PhysicalBurstBudget
{
    std::uint64_t deviceEpoch = 0;
    int configuredFrames = 0, physicalFrames = 0, sourceFrames = 0;
    int quantum = 0, packets = 0, transportSamples = 0, maximumSourceScalePpm = 0;
    BurstProof proof = BurstProof::explicitProviderBudget;
};
struct BurstOutcome
{
    BurstStatus status = BurstStatus::invalidPreparation;
    std::uint64_t deviceEpoch = 0, callbackSerial = 0;
    int actualFrames = 0, preparedFrames = 0, requiredPackets = 0;
};
// Pure CONTROL preparation; it never chooses a Host policy or mutates audio/model.
// Explicit provider budget is admission, not proof from an observed maximum.
inline BurstOutcome preparePhysicalBurstBudget(
    bool knownSnapshot, const juce::AudioDeviceManager::PhysicalCallbackSnapshot &device,
    juce::AudioDeviceManager::PhysicalCallbackBound provenance, int quantum, PluginLatencyStamp nativeLatency,
    int maximumSourceScalePpm, int explicitProviderFrames, PhysicalBurstBudget &out) noexcept
{
    BurstOutcome result;
    result.deviceEpoch = device.deviceEpoch;
    result.callbackSerial = device.callbackSerial;
    if (!knownSnapshot)
        result.status = BurstStatus::telemetryUnavailable;
    else if (!device.ownerAlive)
        result.status = BurstStatus::managerReleased;
    else if (device.writerConflict)
        result.status = BurstStatus::writerConflict;
    else if (device.countersExhausted)
        result.status = BurstStatus::counterExhausted;
    else if (!device.active)
        result.status = BurstStatus::deviceStopped;
    else if (device.deviceEpoch == 0 || device.configuredFrames <= 0 || quantum < 16 || quantum > 8192 ||
             maximumSourceScalePpm < 900000 || maximumSourceScalePpm > 1100000 ||
             !nativeLatency.valid(quantum) || explicitProviderFrames < 0)
        result.status = BurstStatus::invalidPreparation;
    else
    {
        PhysicalBurstBudget value;
        value.deviceEpoch = device.deviceEpoch;
        value.configuredFrames = device.configuredFrames;
        value.quantum = quantum;
        value.maximumSourceScalePpm = maximumSourceScalePpm;
        if (provenance == juce::AudioDeviceManager::PhysicalCallbackBound::currentBufferSize)
        {
            value.physicalFrames = device.configuredFrames;
            value.proof = BurstProof::pinnedWindowsFactory;
        }
        else if (explicitProviderFrames > 0)
        {
            value.physicalFrames = explicitProviderFrames;
            value.proof = BurstProof::explicitProviderBudget;
        }
        else
        {
            result.status = BurstStatus::unknownProviderBound;
            return result;
        }
        if (device.maximumOuterFrames > value.physicalFrames)
        {
            result.status = BurstStatus::physicalBurstExceeded;
            result.actualFrames = device.maximumOuterFrames;
            result.preparedFrames = value.physicalFrames;
            return result; // An observed counterexample invalidates a smaller prepared bound.
        }
        const auto scaled = [&](std::int64_t frames) noexcept
        { return (frames * maximumSourceScalePpm + 999999) / 1000000; };
        const std::int64_t blocks = value.physicalFrames / device.configuredFrames;
        const std::int64_t remainder = value.physicalFrames % device.configuredFrames;
        const auto source =
            blocks * scaled(device.configuredFrames) + (remainder != 0 ? scaled(remainder) : 0);
        const auto packets = 1 + (source + quantum - 1) / quantum;
        result.requiredPackets = int(packets); // At most1+ceil(2*INT_MAX/16), within int.
        if (source <= 0 || source > std::numeric_limits<int>::max() || packets > 8)
            result.status = BurstStatus::packetCapacityExceeded;
        else if (packets * quantum + nativeLatency.nativeSamples >= 131072)
            result.status = BurstStatus::delayCapacityExceeded;
        else
        {
            value.sourceFrames = int(source);
            value.packets = int(packets);
            value.transportSamples = value.packets * quantum;
            result.status = BurstStatus::ready;
            result.preparedFrames = value.physicalFrames;
            out = value; // Commit only a genuinely admitted immutable value.
        }
    }
    return result;
}
// DSP may call these bounded scalar checks. Failure contains no allocated text.
// Caller records faults/fallback; this function NEVER silently reports recovery.
inline BurstOutcome admitPhysicalBurst(bool knownSnapshot,
                                       const juce::AudioDeviceManager::PhysicalCallbackSnapshot &device,
                                       const PhysicalBurstBudget &budget) noexcept
{
    BurstOutcome result;
    result.deviceEpoch = device.deviceEpoch;
    result.callbackSerial = device.callbackSerial;
    result.actualFrames = device.outerFrames;
    result.preparedFrames = budget.physicalFrames;
    if (!knownSnapshot)
        result.status = BurstStatus::telemetryUnavailable;
    else if (!device.ownerAlive)
        result.status = BurstStatus::managerReleased;
    else if (device.writerConflict)
        result.status = BurstStatus::writerConflict;
    else if (device.countersExhausted)
        result.status = BurstStatus::counterExhausted;
    else if (!device.active)
        result.status = BurstStatus::deviceStopped;
    else if (budget.deviceEpoch == 0 || budget.quantum < 16 || budget.packets < 1 || budget.packets > 8 ||
             budget.physicalFrames <= 0 || budget.sourceFrames <= 0)
        result.status = BurstStatus::invalidPreparation;
    else if (device.deviceEpoch != budget.deviceEpoch || device.configuredFrames != budget.configuredFrames)
        result.status = BurstStatus::deviceChanged;
    else if (device.callbackSerial == 0 || device.outerFrames <= 0)
        result.status = BurstStatus::noPhysicalCallback;
    else if (device.outerFrames > budget.physicalFrames)
        result.status = BurstStatus::physicalBurstExceeded;
    else
        result.status = BurstStatus::ready;
    return result;
}
// Must describe the TOTAL predicted source frames for this physical callback,
// before any graph slice, not merely the next PluginHost process(count).
inline BurstOutcome admitSourceBurst(bool knownSnapshot,
                                     const juce::AudioDeviceManager::PhysicalCallbackSnapshot &device,
                                     const PhysicalBurstBudget &budget, int totalSourceFrames) noexcept
{
    auto result = admitPhysicalBurst(knownSnapshot, device, budget);
    if (result.status != BurstStatus::ready)
        return result;
    result.actualFrames = totalSourceFrames;
    result.preparedFrames = budget.sourceFrames;
    if (totalSourceFrames <= 0)
        result.status = BurstStatus::invalidPreparation;
    else if (totalSourceFrames > budget.sourceFrames)
        result.status = BurstStatus::sourceBurstExceeded;
    return result;
}
} // namespace vibera::transport
