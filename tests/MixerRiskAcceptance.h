// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/MixerRouting.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

// Include after ParameterAcceptance.h/readWav. Parent adds one --mixer-risk-check
// runner and target; this header never builds, packages or controls the desktop.
static void mixerRiskTests(const juce::File &folder, const juce::File &binary)
{
    const PluginLatencyStamp stamp{31, 1};
    require(PluginLatencyStamp::unpack(stamp.packed()) == stamp && stamp.next(31) == stamp &&
                stamp.next(1024) == PluginLatencyStamp{1024, 2} &&
                PluginLatencyStamp{0, UINT32_MAX}.next(31).generation == 1 &&
                !PluginLatencyStamp{-1, 1}.valid(256) && !PluginLatencyStamp{131072 - 256, 1}.valid(256) &&
                !PluginLatencyStamp{0, 0}.valid(256),
            "Latency epochs preserve unchanged packets, advance changes and refuse invalid/sentinel "
            "declarations");
    require(folder.createDirectory().wasOk(), "Mixer risk evidence output exists");
    require(binary.existsAsFile(), "Explicit original native latency fixture exists");
    auto spec = parameterSpec(binary);
    require(!spec.instrument && spec.name == "VibeRa Mixer Latency Fixture",
            "Genuine independent delay effect descriptor, not parameter/mock fixture");
    juce::String report = "Original SDK Mixer latency fixture; no listening/hardware claim.\n";
    const auto retain = [&]
    {
        require(folder.getChildFile("mixer-risk-measurements.txt").replaceWithText(report),
                "Risk observations retained, including failure before strong oracle assertion");
    };
    const auto signal = [](int channel, int sample)
    {
        if (sample == 0)
            return channel == 0 ? .5f : -.25f;
        if (sample == 7)
            return channel == 0 ? -.125f : .375f;
        if (sample == 513)
            return channel == 0 ? .0625f : .125f;
        return 0.f;
    };
    constexpr std::array<int, 4> nativeDelay{0, 31, 257, 1024};
    juce::String delayedState;
    for (const auto configuration : {std::pair{48000., 256}, std::pair{44100., 128}})
    {
        const auto [rate, blockSize] = configuration;
        PluginHost host;
        auto started = host.start(spec, rate, blockSize, true);
        require(started.wasOk(), "Original delayed stereo/disabled-aux worker starts: " +
                                     started.getErrorMessage().toStdString());
        const auto pid = host.processId();
        require(host.declaredLatency() == PluginLatencyStamp{31, 1} && !host.consumedLatency(),
                "Native startup declaration is distinct from not-yet-consumed wet audio");
        const auto catalogue = parameterCatalogue(host);
        require(nativeParameter(catalogue, 17).writable() && !nativeParameter(catalogue, 17).automatable() &&
                    nativeParameter(catalogue, 71).automatable(),
                "Control-only dynamic latency and automatable sidechain have truthful metadata");
        require(host.latencySamples() == blockSize + 31,
                "Startup reports real native31 plus exactly one IPC block");
        juce::AudioBuffer<float> audio(2, blockSize);
        std::int64_t elapsed = 0;
        for (int mode = 0; mode < 4; ++mode)
        {
            const auto ack = parameterResult(host, host.setParameter(17, mode / 3.f, catalogue.generation));
            require(ack.result == 0, "Supported control API acknowledges latency mode");
            const auto beforeProcessing = host.latencySamples();
            // Native delay changes are published by the worker after processing.
            // Flush a bounded amount of SILENT original fixture input first;
            // this is test preparation, never an application buffer workaround.
            for (int n = 0; n < 12; ++n)
            {
                audio.clear();
                host.process(audio, 0, blockSize, nullptr, 0, elapsed / rate, 120, true, false);
                elapsed += blockSize;
                host.service();
            }
            report += "rate=" + juce::String(rate, 0) + " block=" + juce::String(blockSize) +
                      " mode=" + juce::String(mode) + " afterAck=" + juce::String(beforeProcessing) +
                      " afterPCM=" + juce::String(host.latencySamples()) +
                      " expected=" + juce::String(blockSize + nativeDelay[size_t(mode)]) + "\n";
            retain();
            require(host.latencySamples() == blockSize + nativeDelay[size_t(mode)],
                    "Dynamic declared native latency propagates to host without worker replacement");
            const auto consumed = host.consumedLatency();
            require(consumed && consumed->stamp == host.declaredLatency() &&
                        consumed->stamp.nativeSamples == nativeDelay[size_t(mode)] && consumed->sequence > 0,
                    "Actual consumed wet packet carries its native declaration and processing sequence");
            for (const bool bypass : {false, true})
            {
                // Each sparse probe has enough zero tail to drain both native
                // and bypass delay histories before the next independent case.
                constexpr int samples = 4096;
                double maximumError = 0;
                for (int start = 0; start < samples; start += blockSize)
                {
                    for (int channel = 0; channel < 2; ++channel)
                        for (int n = 0; n < blockSize; ++n)
                            audio.setSample(channel, n, signal(channel, start + n));
                    host.process(audio, 0, blockSize, nullptr, 0, elapsed / rate, 120, true, bypass);
                    elapsed += blockSize;
                    for (int channel = 0; channel < 2; ++channel)
                        for (int n = 0; n < blockSize; ++n)
                            maximumError = std::max(
                                maximumError,
                                std::abs(double(audio.getSample(channel, n)) -
                                         signal(channel, start + n - blockSize - nativeDelay[size_t(mode)])));
                }
                report += "  bypass=" + juce::String(int(bypass)) +
                          " actualSparsePCMMaximumError=" + juce::String(maximumError, 10) + "\n";
                retain();
                require(maximumError < 1.e-7,
                        "Real isolated PCM equals original sparse oracle delayed by block plus native delay");
                require(host.alive() && host.processId() == pid && !host.deliveryFailed(),
                        "Native mode/bypass controls retain worker and every offline packet");
            }
            if (mode == 2 && blockSize == 256)
            {
                const auto serial = host.requestState();
                require(serial != 0, "Actual native state capture admitted");
                const auto deadline = juce::Time::getMillisecondCounter() + 10000;
                while (!host.readState(serial, delayedState) &&
                       juce::Time::getMillisecondCounter() < deadline)
                    juce::Thread::sleep(2);
                require(delayedState.isNotEmpty(), "Actual state capture completed, not merely requested");
            }
        }
    }
    {
        auto restored = spec;
        restored.state = delayedState;
        PluginHost host;
        const auto result = host.start(restored, 48000, 256, true);
        require(result.wasOk() && host.latencySamples() == 513,
                "Restored native state declares257 plus256 before graph preparation");
    }
    // Route-wide strong oracle: all fixture effects are unity, so one and two
    // unequal-latency chains MUST cancel their timing difference through PDC.
    // Keep both buses present in all graphs, and compare actual exported PCM.
    auto project = Project::empty();
    auto &channel = project.channels.front();
    channel.mixer = 1;
    channel.gain = .2f;
    project.patterns.front().notes = {{project.allocate(), channel.id, 0, 1, 60, .8f}};
    project.placements = {{project.allocate(), project.patterns.front().id, 0, 0, 4, 0}};
    for (auto &insert : project.mixer)
        insert.gain = 1;
    auto effect = spec;
    effect.id = project.allocate();
    project.mixer[3].effects = {effect};
    effect.id = project.allocate();
    project.mixer[4].effects = {effect};
    effect.id = project.allocate();
    project.mixer[4].effects.push_back(effect);
    AudioEngine engine(false);
    double rate = 0;
    const auto render = [&](const juce::String &name)
    {
        const auto output = folder.getChildFile(name + ".wav");
        const auto result = engine.renderSync(project, output, 48000, 24, 256);
        require(result.wasOk(), "Actual latency graph render: " + result.getErrorMessage().toStdString());
        return readWav(output, rate);
    };
    require(setMixerRoute(project, 1, 3, true, true).wasOk(), "One delayed chain routing prepared");
    const auto one = render("native-latency-one-chain");
    require(setMixerRoute(project, 1, 4, true, true).wasOk(), "Two delayed chains routing prepared");
    const auto two = render("native-latency-two-chain");
    require(setMixerRoute(project, 1, 3, true).wasOk(), "Unequal parallel effect chains prepared");
    const auto parallel = render("native-latency-unequal-parallel");
    require(one.getNumSamples() == two.getNumSamples() && one.getNumSamples() == parallel.getNumSamples(),
            "Latency render durations invariant");
    double chainError = 0, parallelError = 0;
    for (int channelIndex = 0; channelIndex < 2; ++channelIndex)
        for (int n = 0; n < one.getNumSamples(); ++n)
        {
            const auto sample = one.getSample(channelIndex, n);
            chainError = std::max(chainError, std::abs(double(sample) - two.getSample(channelIndex, n)));
            parallelError =
                std::max(parallelError, std::abs(double(sample) * 2 - parallel.getSample(channelIndex, n)));
        }
    report += "render unequalChainError=" + juce::String(chainError, 10) +
              " parallelDoubleUnityError=" + juce::String(parallelError, 10) + "\n";
    retain();
    require(one.getMagnitude(0, one.getNumSamples()) > .0001f, "Independent musical signal is nonzero");
    require(
        chainError < 3.e-6 && parallelError < 3.e-6,
        "Declared nonzero unequal chains align sample-for-sample and parallel unity equals twice one path");
    project.mixer[4].effects.front().bypass = true;
    const auto bypassed = render("native-latency-unequal-bypass");
    double bypassError = 0;
    require(bypassed.getNumSamples() == parallel.getNumSamples(), "Bypass render duration invariant");
    for (int channelIndex = 0; channelIndex < 2; ++channelIndex)
        for (int n = 0; n < parallel.getNumSamples(); ++n)
            bypassError = std::max(bypassError, std::abs(double(bypassed.getSample(channelIndex, n)) -
                                                         parallel.getSample(channelIndex, n)));
    report += "render bypassParallelError=" + juce::String(bypassError, 10) + "\n";
    retain();
    require(bypassError < 3.e-6, "Bypass retains declared-delay compensation in unequal parallel paths");
}
