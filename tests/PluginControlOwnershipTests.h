// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void pluginControlOwnershipTests(const juce::File &folder, const juce::File &binary)
{
    const auto spec = parameterSpec(binary);
    {
        PluginHost retired;
        retired.beginRetirement();
        require(retired.retirementComplete() && retired.start(spec, 48000, 256, false).failed() &&
                    retired.processId() == 0,
                "retired unused host is terminal and cannot start a new worker");
    }
    PluginHost host;
    require(host.start(spec, 48000, 256, false).wasOk(), "control ownership worker starts");
    const auto catalogue = parameterCatalogue(host);
    const auto waitReady = [&](unsigned request)
    {
        const auto until = juce::Time::getMillisecondCounter() + 3000;
        while (!host.parameterResultReady(request) && juce::Time::getMillisecondCounter() < until)
            juce::Thread::sleep(2);
        return host.parameterResultReady(request);
    };
    const auto first = host.setParameter(0, .25f, catalogue.generation);
    require(first && waitReady(first), "first native control completes before its owner polls");
    const auto second = host.setParameter(0, .75f, catalogue.generation);
    require(second && second != first && waitReady(second),
            "second caller can complete without consuming the first owner's receipt");
    PluginParameterResult unrelated{98765, 42, .9f, 3};
    require(!host.readParameterResult(second + 100, unrelated) && unrelated.serial == 98765 &&
                unrelated.id == 42 && unrelated.value == .9f,
            "unowned read neither steals a real worker reply nor changes its output");
    const auto firstReply = parameterResult(host, first), secondReply = parameterResult(host, second);
    require(firstReply.result == 0 && firstReply.value == .25f && secondReply.result == 0 &&
                secondReply.value == .75f && !host.readParameterResult(first, unrelated) &&
                host.pendingParameterRequests() == 0,
            "native acknowledgement data survives mixed caller polling and is consumed once");
    std::array<unsigned, 64> tokens{};
    for (unsigned i = 0; i < tokens.size(); ++i)
    {
        tokens[i] = host.setParameter(0, float(i + 1) / 128, catalogue.generation);
        require(tokens[i] && waitReady(tokens[i]), "real worker receipt retained at bounded capacity");
    }
    require(host.pendingParameterRequests() == 64 && host.setParameter(0, .9f, catalogue.generation) == 0,
            "receipt saturation refuses writes without evicting completed owners");
    host.abandonParameterRequest(tokens[13]);
    const auto replacement = host.setParameter(0, .9f, catalogue.generation);
    require(replacement && waitReady(replacement), "releasing one receipt restores admission");
    for (unsigned i = 0; i < tokens.size(); ++i)
        if (i != 13)
        {
            const auto result = parameterResult(host, tokens[i]);
            require(result.result == 0 && result.value == float(i + 1) / 128,
                    "all retained worker replies match their independently chosen values");
        }
    host.abandonParameterRequest(replacement);
    const auto abandoned = host.setParameter(0, .4f, catalogue.generation);
    require(abandoned != 0, "abandoned in-flight control admitted");
    host.abandonParameterRequest(abandoned);
    for (int i = 0; i < 500 && host.pendingParameterRequests(); ++i)
    {
        host.service();
        juce::Thread::sleep(2);
    }
    require(host.pendingParameterRequests() == 0 && !host.readParameterResult(abandoned, unrelated),
            "abandon drains the physical result without leaking a slot or reviving a closed owner");
    require(std::abs(nativeParameter(parameterCatalogue(host, true), 0).value - .4f) < .00001f,
            "abandon releases acknowledgement ownership; it does not pretend to undo an applied control");
    juce::String report = "Original SDK: mixed caller tokens,64 retained replies, exact native values, "
                          "saturation/refusal/release and in-flight abandon PASS.\n";
    for (bool crash : {false, true})
    {
        PluginHost failing;
        require(failing.start(spec, 48000, 256, false).wasOk(), "owned failure-control worker starts");
        const auto metadata = parameterCatalogue(failing);
        const auto request = failing.setParameter(0, crash ? .31415927f : .27182818f, metadata.generation);
        require(request != 0, "fixture-only failure control admitted");
        const auto began = juce::Time::getMillisecondCounter();
        const auto until = began + 6500;
        PluginParameterResult result;
        juce::AudioBuffer<float> block(2, 256);
        block.clear();
        int callbacks = 0;
        double maximum = 0;
        while (!failing.readParameterResult(request, result) && juce::Time::getMillisecondCounter() < until)
        {
            // Only eight subquantum calls,128 frames total: no packet is submitted.
            // The otherwise-idle control watchdog must work without a DSP timeout.
            if (callbacks < 8)
            {
                const auto stamp = juce::Time::getHighResolutionTicks();
                failing.process(block, 0, 16, nullptr, 0, 0, 120, false, false);
                maximum = std::max(maximum, juce::Time::highResolutionTicksToSeconds(
                                                juce::Time::getHighResolutionTicks() - stamp) *
                                                1000);
                ++callbacks;
            }
            failing.service();
            juce::Thread::sleep(3);
        }
        require(result.serial == request && result.id == 0 && result.result == (crash ? 4 : 6) &&
                    !failing.alive() && failing.statistics().submitted == 0 &&
                    (crash || failing.error().contains("Plugin control timed out")) &&
                    maximum < 16000. / 48000.,
                "crash/control timeout returns the exact owner's explicit outcome without waiting in audio");
        // Retain the host/owned process until its normal recovery deadline is serviced.
        const auto recoveryUntil = juce::Time::getMillisecondCounter() + 2500;
        while (juce::Time::getMillisecondCounter() < recoveryUntil)
        {
            failing.service();
            juce::Thread::sleep(5);
        }
        report += juce::String(crash ? "Crash" : "Control stall") +
                  ": outcome=" + juce::String(result.result) +
                  ", subquantumCalls=" + juce::String(callbacks) + ", maximumMs=" + juce::String(maximum, 6) +
                  "; callbacks do not wait for the worker; not continuity/listening evidence.\n";
    }
    require(folder.getChildFile("parameter-control-ownership.txt").replaceWithText(report),
            "exact request-ownership/failure evidence retained");
}
