// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginAutomationBlock.h"

namespace vibera
{
// One processing owner; allocate off DSP. Each slice may have a different clock
// position (loop/seek) or immutable program. No program pointer survives render.
class PluginAutomationPacket
{
  public:
    using Result = PluginAutomationBlock::Result;
    Result render(const AutomationProgram &, std::int64_t plugin, int packetFrame, int quantum, double beat,
                  double beatsPerFrame, int frames, bool playing) noexcept;
    Result hold(std::int64_t plugin, int packetFrame, int quantum, int frames, bool playing) noexcept;
    std::span<const ipc::ParameterEvent> events() const noexcept
    {
        return failed || !outputReady ? std::span<const ipc::ParameterEvent>{} : block.events();
    }
    bool pending() const noexcept
    {
        return tracking;
    }
    std::size_t points() const noexcept
    {
        return total;
    }
    void reset() noexcept;

  private:
    PluginAutomationBlock block;
    std::array<PluginAutomationValue, 4096> held{};
    std::size_t heldCount = 0, total = 0;
    int expectedFrame = 0, packetQuantum = 0;
    std::int64_t owner = 0;
    bool failed = false, tracking = false, outputReady = false;
    Result failure = Result::ok;
    Result beginSlice(std::int64_t, int, int, int) noexcept;
    Result refuse(Result) noexcept;
};
} // namespace vibera
