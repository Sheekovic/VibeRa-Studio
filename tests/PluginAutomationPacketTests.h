// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginAutomationPacket.h"

static void pluginAutomationPackets(const juce::File &folder)
{
    using Result = PluginAutomationPacket::Result;
    const auto constants = [](float value, int targets)
    {
        AutomationDocument doc;
        for (int id = 0; id < targets; ++id)
            doc.initialValues.push_back(
                {{AutomationParameter::pluginNormalized, 77, std::uint32_t(id)}, value});
        return compile(doc);
    };
    const auto constant = constants(.5f, 4096);
    auto packet = std::make_unique<PluginAutomationPacket>();
    unsigned slices = 0;
    double maximumMs = 0;
    for (int offset = 0; offset < 256;)
    {
        const int frames = std::min(offset == 0 ? 1 : 17, 256 - offset);
        const auto start = std::chrono::steady_clock::now();
        realtimeCheck = true;
        const auto result =
            packet->render(constant, 77, offset, 256, offset % 2 ? 0. : 999990., 2. / 48000, frames, true);
        realtimeCheck = false;
        maximumMs = std::max(
            maximumMs,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
        require(result == Result::ok && packet->events().size() == (offset == 0 ? 4096 : 0),
                "4096 constant targets do not repeat offset-zero values across partial callbacks or seeks");
        offset += frames;
        ++slices;
    }
    require(packet->points() == 4096 && !packet->pending(), "constant packet completes within native budget");
    packet->reset();
    require(packet->events().empty() && !packet->pending(), "reset exposes no stale outgoing event view");
    const auto heldOne = constants(.4f, 1);
    require(
        packet->render(heldOne, 77, 0, 64, 0, .0001, 7, true) == Result::ok &&
            packet->hold(77, 7, 64, 16, true) == Result::ok && packet->events().empty() &&
            packet->render(heldOne, 77, 23, 64, 0, .0001, 41, true) == Result::ok &&
            packet->events().empty() && packet->points() == 1,
        "snapshot miss advances packet timing and retains scalar values without duplicate initial points");
    packet->reset();
    const auto changed = constants(.75f, 4096);
    require(packet->render(constant, 77, 0, 8, 0, .0001, 1, true) == Result::ok &&
                packet->render(changed, 77, 1, 8, 1, .0001, 1, true) == Result::ok &&
                packet->points() == ipc::parameterEvents,
            "changed immutable program values are reconciled within a partial packet");
    require(packet->render(constant, 77, 2, 8, 0, .0001, 1, true) == Result::capacityExceeded &&
                packet->events().empty() && packet->points() == 0,
            "rapid revision overflow exposes no new or earlier partial batch");
    for (int i = 3; i < 8; ++i)
        require(packet->render(changed, 77, i, 8, 0, .0001, 1, true) == Result::capacityExceeded &&
                    packet->events().empty(),
                "failed packet remains refused until the native quantum boundary");
    require(packet->render(constant, 77, 0, 8, 0, .0001, 8, true) == Result::ok,
            "next packet recovers without reallocating prepared storage");
    const auto one = constants(.4f, 1);
    packet->reset();
    require(packet->render(one, 77, 0, 8, 0, .0001, 1, true) == Result::ok &&
                packet->render(one, 77, 1, 8, 0, .0001, 1, false) == Result::ok && packet->events().empty() &&
                packet->render(one, 77, 2, 8, 0, .0001, 6, true) == Result::ok &&
                packet->events().size() == 1,
            "resume reasserts native values after stopped manual monitoring");
    packet->reset();
    require(packet->render(one, 77, 0, 64, 0, .0001, 7, true) == Result::ok &&
                packet->render(one, 77, 8, 64, 0, .0001, 4, true) == Result::invalidTiming &&
                packet->events().empty(),
            "out-of-sequence packet slice rejects safely");

    AutomationDocument curves;
    const AutomationKey first{AutomationParameter::pluginNormalized, 77, 0};
    const AutomationKey second{AutomationParameter::pluginNormalized, 77, 42};
    curves.initialValues = {{first, .5f}, {second, .2f}};
    curves.sources = {{1, 4, {{2, 0, .1f}, {3, 4, .9f}}, {{first, 0, 1}, {second, 1, 0}}}};
    curves.placements = {{4, 1, 0, 4, 0}};
    auto program = compile(curves);
    unsigned oracleSamples = 0;
    for (const int quantum : {64, 256, 512, 4096})
        for (const double rate : {44100., 48000., 96000.})
        {
            packet->reset();
            float values[2]{};
            int offset = 0;
            for (const int end : {7, 23, 36, quantum})
            {
                const double beat = offset == 0 ? 1. : offset == 7 ? 3.999 : offset == 23 ? -.001 : 0.;
                realtimeCheck = true;
                const auto result =
                    packet->render(program, 77, offset, quantum, beat, 2 / rate, end - offset, true);
                realtimeCheck = false;
                require(result == Result::ok, "piecewise loop/seek clock fits the native packet");
                const auto events = packet->events();
                std::size_t cursor = 0;
                for (int n = 0; n < end - offset; ++n)
                {
                    while (cursor < events.size() && events[cursor].frame == n)
                    {
                        const auto &event = events[cursor++];
                        values[event.id == 0 ? 0 : 1] = event.value;
                    }
                    const double position = beat + n * 2 / rate;
                    const double delta = (double(.9f) - .1f) * std::clamp(position / 4, 0., 1.);
                    near(values[0], position < 0 ? .5f : float(double(.1f) + delta),
                         "carried gain reconstructs independent piecewise clock oracle");
                    near(values[1], position < 0 ? .2f : float(1 - double(.1f) - delta),
                         "carried inverse target reconstructs independent piecewise clock oracle");
                    ++oracleSamples;
                }
                require(cursor == events.size(),
                        "partitioned native events remain inside each callback slice");
                offset = end;
                ++slices;
            }
        }
    require(realtimeAllocations == 0 && realtimeDestructions == 0,
            "packet carry/loop/seek path has no observed C++ callback allocation/destruction");
    std::ofstream output(
        folder.getChildFile("plugin-automation-packets.txt").getFullPathName().toStdString());
    output << "Prepared native packet carry: slices=" << slices << "; oracleSamples=" << oracleSamples
           << "; constant4096TargetsMaximumSliceMs=" << maximumMs
           << "; C++ allocations=" << realtimeAllocations << "; destructions=" << realtimeDestructions
           << ". Whole-prefix refusal, reset/recovery, stopped/resume, piecewise loops/seeks PASS.\n"
           << "Standalone packet composer, not activated DAW plugin clips or physical/listening evidence.\n";
}
