// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>

namespace vibera
{
// Immutable preparation contract. Native main buses are selected independently;
// the app graph remains stereo. Mono wet input explicitly selects L/R/average.
// Auxiliary fields stay FIRST for genuine existing {bus,channels} call sites.
struct PluginBusRequest
{
    int auxiliaryInputBus = -1;
    int auxiliaryChannels = 0;
    int mainInputBus = 0, mainOutputBus = 0, mainInputChannels = 2, mainInputMonoSource = 0;
    constexpr bool valid() const noexcept
    {
        return mainInputBus >= 0 && mainInputBus <= 31 && mainOutputBus >= 0 && mainOutputBus <= 31 &&
               ((mainInputChannels == 2 && mainInputMonoSource == 0) ||
                (mainInputChannels == 1 && mainInputMonoSource >= 1 && mainInputMonoSource <= 3)) &&
               (auxiliaryChannels == 0 ? auxiliaryInputBus == -1
                                       : auxiliaryInputBus >= 1 && auxiliaryInputBus <= 31 &&
                                             auxiliaryInputBus != mainInputBus &&
                                             (auxiliaryChannels == 1 || auxiliaryChannels == 2));
    }
    bool operator==(const PluginBusRequest &) const = default;
};
// Shared typed canonical command payload; persisted PluginSpec keeps its four
// direct fields. Absence of this payload preserves genuine older commands.
struct PluginMainIo
{
    int inputBus = 0, outputBus = 0, inputChannels = 2, monoSource = 0;
    constexpr bool valid() const noexcept
    {
        PluginBusRequest request;
        request.mainInputBus = inputBus;
        request.mainOutputBus = outputBus;
        request.mainInputChannels = inputChannels;
        request.mainInputMonoSource = monoSource;
        return request.valid();
    }
    bool operator==(const PluginMainIo &) const = default;
};
// Actual preparation metadata, NOT a promise that another combined arrangement
// will negotiate. Other main/aux choices must prepare a genuine private worker.
struct PluginBusInfo
{
    int index = 0;
    bool input = false;
    int defaultChannels = 0, actualChannels = 0;
    char name[160]{};
};
struct PluginBusCatalogue
{
    static constexpr int maximumPerDirection = 32;
    int inputCount = 0, outputCount = 0;
    std::array<PluginBusInfo, 2 * maximumPerDirection> entries{};
    bool valid() const noexcept
    {
        if (inputCount < 0 || inputCount > maximumPerDirection || outputCount < 1 ||
            outputCount > maximumPerDirection)
            return false;
        for (int i = 0; i < inputCount + outputCount; ++i)
        {
            const auto &row = entries[static_cast<std::size_t>(i)];
            if (row.input != (i < inputCount) || row.index != (row.input ? i : i - inputCount) ||
                row.defaultChannels < 0 || row.actualChannels < 0 || row.actualChannels > 2 ||
                row.name[159] != 0)
                return false;
        }
        return true;
    }
};
} // namespace vibera
