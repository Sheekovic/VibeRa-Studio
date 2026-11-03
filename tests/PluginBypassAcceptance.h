// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

// Include after ParameterAcceptance.h. Original actual-worker PCM experiment;
// offline waiting here is not a live-callback, native-input or listening test.
static void pluginBypassTransitionTests(const juce::File &folder, const juce::File &binary)
{
    require(folder.createDirectory().wasOk() && binary.existsAsFile(),
            "Bypass evidence directory and explicit original fixture exist");
    const auto spec = parameterSpec(binary);
    require(!spec.instrument && spec.name == "VibeRa Mixer Latency Fixture",
            "Bypass probe uses genuine original independent main/detector effect");
    juce::String report = "Original actual-worker bypass;5ms fixed-slope linear wet/aligned-dry.\n";
    const auto retain = [&]
    {
        require(folder.getChildFile("plugin-bypass-measurements.txt").replaceWithText(report),
                "Actual bypass observations retained before asserting the independent oracle");
    };
    const auto capture = [](PluginHost &host)
    {
        const auto request = host.requestState();
        require(request != 0, "Bypass state capture admitted");
        const auto deadline = juce::Time::getMillisecondCounter() + 10000;
        juce::String state;
        while (!host.readState(request, state) && juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep(2);
        require(state.isNotEmpty(), "Bypass state capture completed, not merely requested");
        return state;
    };
    constexpr int frames = 64, offset = 7;
    constexpr std::array<float, 2> mainValue{.125f, -.0625f}, detectorValue{.25f, -.375f};
    // 95 samples is shorter than either5ms ramp, making an erroneous startup fade
    // observable. With block256+31 it could finish before any PCM becomes audible.
    constexpr int latency = frames + 31;
    for (const double rate : {48000., 44100.})
        for (const bool initialBypass : {false, true})
        {
            PluginHost host;
            const auto result = host.start(spec, rate, frames, true, {}, PluginBusRequest{1, 2});
            require(result.wasOk(), "Actual worker prepared for bypass experiment: " +
                                        result.getErrorMessage().toStdString());
            const auto pid = host.processId();
            const auto identity = host.runtimeIdentity();
            const auto catalogue = parameterCatalogue(host);
            require(parameterResult(host, host.setParameter(71, 1, catalogue.generation)).result == 0,
                    "Supported native amount1 makes wet distinguishable from aligned dry");
            const auto baseline = capture(host);
            const auto revision = host.parameterRevision();
            require(host.latencySamples() == latency, "Native31 plus worker64 reported before PCM");
            const auto rampSamples = int(std::ceil(rate * .005));
            require(host.bypassTransitionSamples() == rampSamples && !host.bypassBlend().has_value(),
                    "Truthful immutable5ms duration and unavailable blend before first process");
            const double step = 1. / rampSamples;
            double mix = initialBypass ? 1. : 0.;
            std::int64_t clock = 0;
            double maximumError = 0, maximumTransitionJump = 0;
            std::array<float, 2> previous{};
            bool havePrevious = false;
            juce::AudioBuffer<float> audio(2, 1024 + offset), auxiliary(2, 1024 + offset);
            const auto process = [&](int count, bool bypass, bool immediateFailure = false)
            {
                audio.clear();
                auxiliary.clear();
                for (int channel = 0; channel < 2; ++channel)
                    for (int n = 0; n < count; ++n)
                    {
                        audio.setSample(channel, offset + n, mainValue[size_t(channel)]);
                        auxiliary.setSample(channel, offset + n, detectorValue[size_t(channel)]);
                    }
                host.process(audio, offset, count, nullptr, 0, clock / rate, 120, true, bypass, 4, 4, nullptr,
                             0, 0, true, &auxiliary, offset);
                for (int n = 0; n < count; ++n)
                {
                    if (immediateFailure)
                        mix = 1.;
                    else if (bypass)
                        mix = std::min(1., mix + step);
                    else
                        mix = std::max(0., mix - step);
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        const auto dry = clock + n >= latency ? mainValue[size_t(channel)] : 0.f;
                        const auto wet = clock + n >= latency
                                             ? mainValue[size_t(channel)] + detectorValue[size_t(channel)]
                                             : 0.f;
                        const auto expected = wet * (1. - mix) + dry * mix;
                        const auto sample = audio.getSample(channel, offset + n);
                        maximumError = std::max(maximumError, std::abs(double(sample) - expected));
                        if (havePrevious && clock + n > 512 && !immediateFailure)
                            maximumTransitionJump = std::max(
                                maximumTransitionJump, std::abs(double(sample) - previous[size_t(channel)]));
                        previous[size_t(channel)] = sample;
                    }
                    havePrevious = true;
                }
                clock += count;
            };
            // Irregular prefixes also verify the first process initializes to its
            // target immediately; correct startup-bypassed PCM is dry at sample95.
            for (const int count : {1, 17, 127, 31, 80, 256})
                process(count, initialBypass);
            report += "rate=" + juce::String(rate, 0) + " initialBypass=" + juce::String(int(initialBypass)) +
                      " rampSamples=" + juce::String(rampSamples) +
                      " startupError=" + juce::String(maximumError, 10) + "\n";
            retain();
            require(maximumError < 2.e-6, "Initial wet/dry target is immediate through native/worker delay");
            // Exercise unfinished ramps, reversal, single-frame repeated toggles,
            // cross-quantum calls and full linear settling without resetting DSP.
            process(31, !initialBypass);
            process(17, initialBypass);
            process(1, !initialBypass);
            process(2, initialBypass);
            process(rampSamples + 11, !initialBypass);
            process(337, !initialBypass);
            constexpr std::array<int, 6> rapidCounts{1, 3, 7, 13, 29, 67};
            for (int toggle = 0; toggle < 36; ++toggle)
                process(rapidCounts[size_t(toggle % rapidCounts.size())], (toggle & 1) != 0);
            process(rampSamples + 37, false);
            process(113, false);
            process(rampSamples + 37, true);
            process(113, true);
            report += "  transitionError=" + juce::String(maximumError, 10) +
                      " maximumSteadyInputJump=" + juce::String(maximumTransitionJump, 10) +
                      " slopeLimit=" + juce::String(.375 / rampSamples, 10) + "\n";
            retain();
            require(maximumError < 2.e-6, "Actual native wet/delayed-dry PCM follows independent per-sample "
                                          "fixed-slope reversal oracle");
            const auto blend = host.bypassBlend();
            require(blend.has_value() && std::abs(double(*blend) - mix) < 1.e-6 &&
                        host.bypassBlend() == blend,
                    "Read-only bypass blend reports actual settled state without consuming it");
            require(maximumTransitionJump <= .375 / rampSamples + 2.e-6,
                    "Healthy bypass/reversal transitions have no abrupt steady-input discontinuity");
            require(host.alive() && host.processId() == pid && host.runtimeIdentity() == identity &&
                        host.latencySamples() == latency && host.parameterRevision() == revision &&
                        !host.deliveryFailed(),
                    "Bypass toggles preserve native state revision, latency, worker and all offline packets");
            // Align to a complete quantum before capture, preserving an explicit
            // state boundary and all processing ownership.
            if (clock % frames != 0)
                process(frames - int(clock % frames), true);
            const auto after = capture(host);
            require(after == baseline, "Healthy runtime bypass does not mutate original plugin state");
            // Separate owned-host failure-flag simulation from actual worker-crash
            // injection: settle wet, enter a partial quantum, retire this worker.
            process(rampSamples + 37, false);
            if (clock % frames != 1)
                process(int((frames + 1 - clock % frames) % frames), false);
            host.beginRetirement();
            const auto beforeFailure = maximumError;
            process(1, false, true);
            report += "  immediateOwnedRetirementFallbackError=" + juce::String(maximumError, 10) +
                      " priorError=" + juce::String(beforeFailure, 10) + "\n";
            retain();
            require(
                maximumError < 2.e-6 && !host.alive() && host.bypassBlend() == std::optional<float>(1.f),
                "Failed owned host uses immediate aligned dry fallback rather than fading stale wet output");
        }
    report += "PASS original offline worker PCM only; live timing/graph/UI/MCP/real-provider/listening are "
              "separate.\n";
    retain();
}
