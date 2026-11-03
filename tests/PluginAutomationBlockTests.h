// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginAutomationBlock.h"
#include <limits>

static void pluginAutomationBlocks(const juce::File &folder)
{
    const AutomationKey gain{AutomationParameter::pluginNormalized, 77, 0};
    const AutomationKey stepped{AutomationParameter::pluginNormalized, 77, 42};
    auto block = std::make_unique<PluginAutomationBlock>();
    double maximumMs = 0;
    unsigned blocks = 0, rejected = 0;
    for (const double clipStart : {2., 999990.})
        for (const auto shape : {AutomationShape::single, AutomationShape::smooth, AutomationShape::hold})
        {
            AutomationDocument doc;
            doc.initialValues = {{gain, .25f},
                                 {stepped, .3f},
                                 {{AutomationParameter::pluginNormalized, 78, 0}, .7f},
                                 {master, 1.f}};
            doc.sources = {
                {1, 4, {{2, 0, .1f}, {3, 4, .9f, shape, .5f}}, {{gain, 0, 1}}},
                {4,
                 4,
                 {{5, 0, .2f}, {6, 1, .6f, AutomationShape::hold}, {7, 4, .9f, AutomationShape::hold}},
                 {{stepped, 0, 1}}}};
            doc.placements = {{8, 1, clipStart, 4, 0}, {9, 4, clipStart, 4, 0}};
            const auto program = compile(doc);
            require(program.pluginLanes(77).size() == 2 && program.pluginLanes(78).size() == 1 &&
                        program.pluginLanes(0).empty() && program.pluginLanes(79).empty(),
                    "native lanes are selected by persistent plugin identity");
            for (const int frames : {32, 64, 256, 512, 8192})
                for (const double offset : {-1., -.001, 0., .999, 2., 3.999, 4., 8.})
                {
                    const double start = clipStart + offset;
                    const double step = 2. / 48000;
                    auto expected = [&](int frame, int parameter)
                    {
                        const double local = start + frame * step - clipStart;
                        if (local < 0)
                            return parameter == 0 ? .25f : .3f;
                        if (local >= 4)
                            return .9f;
                        if (parameter == 42)
                            return local < 1 ? .2f : .6f;
                        const double t = local / 4;
                        const double f = shape == AutomationShape::hold     ? 0.
                                         : shape == AutomationShape::smooth ? t * t * (3 - 2 * t)
                                                                            : t + .5 * t * (1 - t);
                        return float(double(.1f) + (double(.9f) - .1f) * f);
                    };
                    int expectedCount = 2;
                    for (int n = 1; n < frames; ++n)
                        for (const int id : {0, 42})
                            expectedCount += expected(n, id) != expected(n - 1, id) ? 1 : 0;
                    const auto admission = PluginAutomationBlock::admit(program, 77, step, frames);
                    require(admission.maximumEvents >= std::size_t(expectedCount) &&
                                (admission.result != PluginAutomationBlock::Result::ok ||
                                 expectedCount <= ipc::parameterEvents),
                            "control-thread workload bound covers independently counted sample events");
                    const auto begin = std::chrono::steady_clock::now();
                    realtimeCheck = true;
                    const auto result = block->render(program, 77, start, step, frames, true);
                    realtimeCheck = false;
                    maximumMs = std::max(maximumMs, std::chrono::duration<double, std::milli>(
                                                        std::chrono::steady_clock::now() - begin)
                                                        .count());
                    ++blocks;
                    if (expectedCount > ipc::parameterEvents)
                    {
                        require(result == PluginAutomationBlock::Result::capacityExceeded &&
                                    block->events().empty(),
                                "over-capacity automation rejects the complete batch");
                        ++rejected;
                        continue;
                    }
                    require(result == PluginAutomationBlock::Result::ok, "prepared event block succeeds");
                    auto events = block->events();
                    float values[2]{};
                    std::size_t cursor = 0;
                    int previousFrame = -1;
                    std::uint32_t previousID = 0;
                    for (int n = 0; n < frames; ++n)
                    {
                        while (cursor < events.size() && events[cursor].frame == n)
                        {
                            const auto &event = events[cursor++];
                            require(event.frame > previousFrame || event.id > previousID,
                                    "events sorted by frame/native ID with no duplicate target at one frame");
                            require(event.id == 0 || event.id == 42,
                                    "foreign plugin and Mixer lanes excluded");
                            values[event.id == 0 ? 0 : 1] = event.value;
                            previousFrame = event.frame;
                            previousID = event.id;
                        }
                        near(values[0], expected(n, 0), "native queue reconstructs independent curve sample");
                        near(values[1], expected(n, 42), "native queue reconstructs exact Hold boundaries");
                    }
                    require(cursor == events.size(), "all native events are within the submitted buffer");
                }
        }
    AutomationDocument constants;
    constexpr int constantTargets = 4096;
    for (int id = 0; id < constantTargets; ++id)
        constants.initialValues.push_back(
            {{AutomationParameter::pluginNormalized, 77, std::uint32_t(id)}, .5f});
    const auto constantProgram = compile(constants);
    realtimeCheck = true;
    const auto constantResult = block->render(constantProgram, 77, 0, .0001, ipc::frames, true);
    realtimeCheck = false;
    require(constantResult == PluginAutomationBlock::Result::ok && block->events().size() == constantTargets,
            "maximum constant target set skips entire unchanged spans");
    double plateauMaximumMs = 0;
    for (const bool descending : {false, true})
    {
        auto plateau = constants;
        const float high = std::nextafter(.5f, 1.f);
        AutomationSource source{1, 4, {{2, 0, descending ? high : .5f}, {3, 4, descending ? .5f : high}}, {}};
        for (const auto &value : plateau.initialValues)
            source.links.push_back({value.key, 0, 1});
        plateau.sources.push_back(source);
        plateau.placements.push_back({4, 1, 0, 4, 0});
        const auto prepared = compile(plateau);
        const auto begin = std::chrono::steady_clock::now();
        realtimeCheck = true;
        const auto result = block->render(prepared, 77, 0, 2. / 48000, ipc::frames, true);
        realtimeCheck = false;
        plateauMaximumMs = std::max(
            plateauMaximumMs,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
        require(result == PluginAutomationBlock::Result::ok && block->events().size() == constantTargets,
                "ascending/descending float plateaus preserve full target set without scanning every sample");
        require(block->render(prepared, 77, 1.999, 2. / 48000, 256, true) ==
                        PluginAutomationBlock::Result::ok &&
                    block->events().size() == 2 * constantTargets,
                "full-capacity float plateau transition retains both events for every target");
    }
    const auto denseDocument = [](int targets)
    {
        AutomationDocument doc;
        AutomationSource source{1, 4, {{2, 0, .1f}, {3, 4, .9f}}, {}};
        for (int id = 0; id < targets; ++id)
        {
            const AutomationKey key{AutomationParameter::pluginNormalized, 77, std::uint32_t(id)};
            doc.initialValues.push_back({key, .5f});
            source.links.push_back({key, 0, 1});
        }
        doc.sources.push_back(source);
        doc.placements.push_back({4, 1, 0, 4, 0});
        return doc;
    };
    const auto full = compile(denseDocument(16));
    const auto fullAdmission = PluginAutomationBlock::admit(full, 77, 2. / 48000, 512);
    require(fullAdmission.result == PluginAutomationBlock::Result::ok &&
                fullAdmission.maximumEvents == ipc::parameterEvents &&
                block->render(full, 77, 1, 2. / 48000, 512, true) == PluginAutomationBlock::Result::ok &&
                block->events().size() == ipc::parameterEvents,
            "sixteen simultaneous sample-by-sample ramps fit without decimation at512 frames");
    const auto overloaded = compile(denseDocument(17));
    require(PluginAutomationBlock::admit(overloaded, 77, 2. / 48000, 512).result ==
                PluginAutomationBlock::Result::capacityExceeded,
            "excess dense workload is refused before publishing a playback projection");
    require(block->render(overloaded, 77, 1, 2. / 48000, 512, true) ==
                    PluginAutomationBlock::Result::capacityExceeded &&
                block->events().empty(),
            "excess dense workload cannot expose a partial event batch");
    require(block->render(overloaded, 77, 0, .0001, 32, false) == PluginAutomationBlock::Result::ok &&
                block->events().empty(),
            "stopped monitoring never overwrites manual plugin controls");
    AutomationDocument separated;
    for (int id = 0; id < 64; ++id)
    {
        const AutomationKey key{AutomationParameter::pluginNormalized, 77, std::uint32_t(id)};
        const auto base = std::int64_t(id) * 4 + 1;
        separated.initialValues.push_back({key, .5f});
        separated.sources.push_back({base, 4, {{base + 1, 0, .1f}, {base + 2, 4, .9f}}, {{key, 0, 1}}});
        separated.placements.push_back({base + 3, base, id * 10., 4, 0});
    }
    const auto separatedProgram = compile(separated);
    const auto separatedAdmission = PluginAutomationBlock::admit(separatedProgram, 77, 2. / 48000, 512);
    require(separatedAdmission.result == PluginAutomationBlock::Result::ok &&
                separatedAdmission.maximumEvents == 512 + 63,
            "separated curves use temporal concurrency instead of treating all64 lanes as dense");
    for (const int quantum : {1, 64, 128, 256, 512, 8192})
        for (const double rate : {8000., 44100., 48000., 96000., 192000.})
        {
            const double step = 5 / rate;
            const auto admitted = PluginAutomationBlock::admit(separatedProgram, 77, step, quantum);
            for (const double beat : {-1., -.00001, 0., 3.999, 4., 9.999, 10., 43.999, 250., 639.999})
            {
                const auto result = block->render(separatedProgram, 77, beat, step, quantum, true);
                require(admitted.result != PluginAutomationBlock::Result::ok ||
                            (result == PluginAutomationBlock::Result::ok &&
                             block->events().size() <= admitted.maximumEvents),
                        "admitted complete timeline survives independent rate/quantum/seek/boundary cases");
            }
        }
    unsigned boundaryPackets = 0;
    for (const double placement : {0., 999990.})
    {
        AutomationDocument held;
        for (int id = 0; id < 16; ++id)
        {
            const auto base = std::int64_t(id) * 20 + 1;
            held.initialValues.push_back(
                {{AutomationParameter::pluginNormalized, 77, std::uint32_t(id)}, .5f});
            AutomationSource source{base,
                                    4,
                                    {{base + 1, 0, .1f}},
                                    {{{AutomationParameter::pluginNormalized, 77, std::uint32_t(id)}, 0, 1}}};
            for (int point = 1; point < 9; ++point)
                source.points.push_back({base + point + 1, point * .003 + id * .00001, point % 2 ? .8f : .2f,
                                         AutomationShape::hold});
            source.points.push_back({base + 10, 4, .2f, AutomationShape::hold});
            held.sources.push_back(source);
            held.placements.push_back({base + 19, base, placement, 4, 0});
        }
        const auto program = compile(held);
        for (const int quantum : {64, 256})
        {
            const double step = 2. / 44100;
            const auto admission = PluginAutomationBlock::admit(program, 77, step, quantum);
            for (int point = 0; point < 9; ++point)
            {
                const double boundary = placement + point * .003;
                for (const double edge : {boundary, boundary - (quantum - 1) * step})
                    for (const double begin :
                         {std::nextafter(edge, -INFINITY), edge, std::nextafter(edge, INFINITY)})
                    {
                        std::size_t expectedEvents = 0;
                        for (const auto &lane : program.pluginLanes(77))
                        {
                            ++expectedEvents;
                            auto value = lane.valueAt(begin);
                            for (int n = 1; n < quantum; ++n)
                            {
                                const auto next = lane.valueAt(begin + n * step);
                                expectedEvents += next != value ? 1 : 0;
                                value = next;
                            }
                        }
                        require(admission.maximumEvents >= expectedEvents,
                                "Hold-boundary admission covers neighboring floating-point packet starts");
                        ++boundaryPackets;
                    }
            }
        }
    }
    for (const auto invalid :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -1., 0.})
    {
        require(block->render(constantProgram, 77, 0, invalid, 32, true) ==
                        PluginAutomationBlock::Result::invalidTiming &&
                    block->events().empty(),
                "invalid event timing rejected");
        require(PluginAutomationBlock::admit(constantProgram, 77, invalid, 32).result ==
                    PluginAutomationBlock::Result::invalidTiming,
                "invalid admission timing refused before publication");
    }
    for (const int quantum : {0, -1, ipc::frames + 1})
        require(PluginAutomationBlock::admit(constantProgram, 77, .0001, quantum).result ==
                    PluginAutomationBlock::Result::invalidTiming,
                "unsupported admission quantum refuses before publication");
    require(block->render(constantProgram, 77, 1.e300, 1.e-300, 32, true) ==
                PluginAutomationBlock::Result::invalidTiming,
            "floating-point collapsed sample clock rejected");
    require(realtimeAllocations == 0 && realtimeDestructions == 0,
            "plugin event generation allocates and destroys nothing in measured path");
    std::ofstream report(folder.getChildFile("plugin-automation-events.txt").getFullPathName().toStdString());
    report << "Original compiled curves -> exact sample-offset native events; standalone bridge preparation "
              "only.\n"
           << "blocks=" << blocks << "; capacityRejections=" << rejected << "; maxMs=" << maximumMs
           << "; plateau4096Targets8192FramesMaxMs=" << plateauMaximumMs
           << "; C++ callback allocations=" << realtimeAllocations
           << "; destructions=" << realtimeDestructions << ".\n"
           << "Control-thread conservative admission:16dense lanes x512 frames=8192 events; "
              "17dense lanes refused;64 separated lanes bound575;300 rate/quantum/seek boundary cases.\n"
           << "Floating-point Hold boundary packets=" << boundaryPackets << ".\n"
           << "No DAW target UI/project link, listening or actual provider response timing claim.\n";
}
