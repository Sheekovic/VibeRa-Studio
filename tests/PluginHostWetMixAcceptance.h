// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
// Include after actual parameterSpec / parameterCatalogue / parameterResult helpers.
// Original native Mixer Latency SDK fixture only; not installed-provider or live timing evidence.
__declspec(noinline) static void pluginHostWetMixTests(const juce::File &folder, const juce::File &binary)
{
    require(folder.createDirectory().wasOk() && binary.existsAsFile(),
            "Owned wet-mix output and original binary");
    const auto spec = parameterSpec(binary);
    require(!spec.instrument && spec.name == "VibeRa Mixer Latency Fixture",
            "Explicit original detector/native31 effect");
    constexpr int quantum = 64, offset = 7;
    constexpr std::array<float, 2> main{.125f, -.0625f}, detector{.25f, -.375f};
    for (const double rate : {48000., 44100.})
        for (const auto policy :
             {PluginHost::TransportPolicy::oneQuantum, PluginHost::TransportPolicy::twoQuanta})
        {
            PluginHost host(policy);
            require(host.start(spec, rate, quantum, true, {}, PluginBusRequest{1, 2}).wasOk(),
                    "Actual original SDK worker");
            const auto catalogue = parameterCatalogue(host);
            require(parameterResult(host, host.setParameter(71, 1, catalogue.generation)).result == 0,
                    "Native amount1 makes processed signal independently distinguishable");
            const auto pid = host.processId();
            const auto instance = host.runtimeIdentity();
            const auto revision = host.parameterRevision();
            const int delay = (policy == PluginHost::TransportPolicy::twoQuanta ? 128 : 64) + 31;
            require(host.latencySamples() == delay && !host.wetBlend(),
                    "Explicit native31 and transport contract; no calibrated phase");
            const int ramp = int(std::ceil(rate * .005));
            double wet = .25, goal = .25, step = 0, bypass = 0;
            int remaining = 0, clock = 0;
            juce::AudioBuffer<float> audio(2, 1031), auxiliary(2, 1031);
            juce::String csv = "frame,count,target,bypass,maximumError,submitted,processed,missed,dropped\n";
            const auto file = folder.getChildFile("host-wet-" + juce::String(int(rate)) + "-" +
                                                  juce::String(int(policy)) + ".csv");
            require(!file.exists(), "Never overwrite prior wet-mix failure evidence");
            const auto process = [&](int frames, float target, bool bypassTarget)
            {
                if (goal != double(target))
                {
                    goal = target;
                    step = (goal - wet) / ramp;
                    remaining = ramp;
                }
                audio.clear();
                auxiliary.clear();
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < frames; ++n)
                    {
                        audio.setSample(c, offset + n, main[size_t(c)]);
                        auxiliary.setSample(c, offset + n, detector[size_t(c)]);
                    }
                host.process(audio, offset, frames, nullptr, 0, clock / rate, 120, true, bypassTarget, 4, 4,
                             nullptr, 0, 0, true, &auxiliary, offset, target);
                double error = 0;
                for (int n = 0; n < frames; ++n)
                {
                    if (remaining > 0)
                        wet = --remaining == 0 ? goal : wet + step;
                    bypass =
                        bypassTarget ? std::min(1., bypass + 1. / ramp) : std::max(0., bypass - 1. / ramp);
                    for (int c = 0; c < 2; ++c)
                    {
                        const double dry = clock + n >= delay ? main[size_t(c)] : 0;
                        const double processed =
                            clock + n >= delay ? main[size_t(c)] + detector[size_t(c)] : 0;
                        const double expected =
                            (processed * wet + dry * (1 - wet)) * (1 - bypass) + dry * bypass;
                        const auto sample = audio.getSample(c, offset + n);
                        error = std::isfinite(sample) ? std::max(error, std::abs(double(sample) - expected))
                                                      : std::numeric_limits<double>::infinity();
                    }
                }
                const auto stats = host.statistics();
                csv += juce::String(clock) + "," + juce::String(frames) + "," + juce::String(target) + "," +
                       juce::String(int(bypassTarget)) + "," + juce::String(error, 12) + "," +
                       juce::String(juce::int64(stats.submitted)) + "," +
                       juce::String(juce::int64(stats.processed)) + "," +
                       juce::String(juce::int64(stats.missed)) + "," +
                       juce::String(juce::int64(stats.dropped)) + "\n";
                require(file.replaceWithText(csv),
                        "Every observed native mix span retained before assertion");
                require(error < 2.e-6,
                        "Independent unchanged native delay + host mix + bypass per-sample oracle");
                require(host.wetBlend() && std::abs(*host.wetBlend() - wet) < 1.e-6 && host.alive() &&
                            host.processId() == pid && host.runtimeIdentity() == instance &&
                            host.parameterRevision() == revision && host.latencySamples() == delay &&
                            !host.deliveryFailed(),
                        "Actual wet progress, worker/state/latency retained");
                clock += frames;
            };
            for (const int frames : {1, 17, 127, 31, 80, 256})
                process(frames, .25f, false);
            process(7, .71875f, false);
            process(13, .5f, false);
            process(ramp + 17, 0, false);
            process(31, 1, true);
            process(17, .25f, false);
            process(ramp + 17, 1, false);
            process(ramp + 17, .75f, true);
            process(ramp + 17, .75f, false);
            const auto cursor = host.packetFrame();
            const auto before = host.statistics();
            audio.clear();
            audio.setSample(0, offset, .125f);
            audio.setSample(1, offset + 7, -.0625f);
            host.process(audio, offset, 8, nullptr, 0, clock / rate, 120, true, false, 4, 4, nullptr, 0, 0,
                         true, &auxiliary, offset, 1.1f);
            require(host.automationStatistics().last == PluginHost::AutomationStatus::invalidTiming &&
                        host.packetFrame() == cursor && host.statistics().submitted == before.submitted &&
                        audio.getSample(0, offset) == .125f && audio.getSample(1, offset + 7) == -.0625f,
                    "Invalid mix refuses before any packet/audio mutation");
        }
}
