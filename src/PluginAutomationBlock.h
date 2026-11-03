// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Automation.h"
#include "PluginProtocol.h"

namespace vibera
{
struct PluginAutomationValue
{
    std::uint32_t id = 0;
    float value = 0;
};
// Prepare/own this storage off the callback. Retain the program's immutable lease
// throughout render(). This does not perform metadata lookup or call plugin/UI code.
class PluginAutomationBlock
{
  public:
    enum class Result
    {
        ok,
        invalidTiming,
        capacityExceeded
    };
    struct Admission
    {
        Result result = Result::invalidTiming;
        std::size_t maximumEvents = 0;
    };
    // Control thread only: conservative full-timeline bound for any packet start,
    // including seeks/loops. Re-admit when the program or timing/quantum changes.
    static Admission admit(const AutomationProgram &, std::int64_t plugin, double beatsPerFrame, int quantum);
    Result render(const AutomationProgram &, std::int64_t plugin, double beat, double beatsPerFrame,
                  int frames, bool playing, std::span<const PluginAutomationValue> previous = {}) noexcept;
    std::span<const ipc::ParameterEvent> events() const noexcept
    {
        return {storage.data(), std::size_t(count)};
    }

  private:
    std::array<ipc::ParameterEvent, ipc::parameterEvents> storage{};
    int count = 0;
};
} // namespace vibera
