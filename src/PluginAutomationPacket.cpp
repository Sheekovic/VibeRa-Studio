// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginAutomationPacket.h"

namespace vibera
{
void PluginAutomationPacket::reset() noexcept
{
    heldCount = total = 0;
    expectedFrame = packetQuantum = 0;
    owner = 0;
    failed = tracking = outputReady = false;
    failure = Result::ok;
}
PluginAutomationPacket::Result PluginAutomationPacket::refuse(Result result) noexcept
{
    failed = true;
    failure = result;
    total = 0;
    return result;
}
PluginAutomationPacket::Result PluginAutomationPacket::beginSlice(std::int64_t plugin, int offset,
                                                                  int quantum, int frames) noexcept
{
    outputReady = false;
    if (offset == 0 || !tracking)
    {
        reset();
        expectedFrame = offset;
        packetQuantum = quantum;
        owner = plugin;
    }
    tracking = true;
    if (offset < 0 || quantum < 1 || quantum > ipc::frames || frames < 1 || offset >= quantum ||
        frames > quantum - offset || expectedFrame != offset || packetQuantum != quantum || owner != plugin)
        return refuse(Result::invalidTiming);
    expectedFrame += frames;
    tracking = expectedFrame != quantum;
    if (failed)
        return failure;
    return Result::ok;
}
PluginAutomationPacket::Result PluginAutomationPacket::hold(std::int64_t plugin, int offset, int quantum,
                                                            int frames, bool playing) noexcept
{
    const auto result = beginSlice(plugin, offset, quantum, frames);
    if (!playing)
        heldCount = 0;
    return result;
}
PluginAutomationPacket::Result PluginAutomationPacket::render(const AutomationProgram &program,
                                                              std::int64_t plugin, int offset, int quantum,
                                                              double beat, double beatsPerFrame, int frames,
                                                              bool playing) noexcept
{
    const auto positioned = beginSlice(plugin, offset, quantum, frames);
    if (positioned != Result::ok)
        return positioned;
    const auto lanes = program.pluginLanes(plugin);
    if (lanes.size() > held.size())
        return refuse(Result::capacityExceeded);
    const auto result =
        block.render(program, plugin, beat, beatsPerFrame, frames, playing, {held.data(), heldCount});
    if (result != Result::ok)
        return refuse(result);
    if (block.events().size() > std::size_t(ipc::parameterEvents) - total)
        return refuse(Result::capacityExceeded);
    total += block.events().size();
    if (playing)
    {
        heldCount = lanes.size();
        const auto end = beat + (frames - 1) * beatsPerFrame;
        for (std::size_t i = 0; i < lanes.size(); ++i)
            held[i] = {lanes[i].key.parameter, lanes[i].valueAt(end)};
    }
    else
        heldCount = 0; // Resume must reassert automation after stopped manual control.
    outputReady = true;
    return Result::ok;
}
} // namespace vibera
