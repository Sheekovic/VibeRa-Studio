// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"
#include "../src/AudioEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
// Include after ParameterAcceptance.h: actual isolated SDK scan, existing require.
static void pluginBusChoiceTests(const juce::File &folder, const juce::File &binary)
{
    require(folder.createDirectory().wasOk() && binary.existsAsFile(),
            "Explicit original bus-choice fixture/output");
    const auto original = parameterSpec(binary);
    require(!original.instrument && original.name == "VibeRa Native Bus Choice Fixture",
            "Actual independently owned native multi-bus SDK fixture");
    constexpr int quantum = 256, samples = 2048, offset = 13, auxOffset = 23;
    const auto main = [](int c, int n) { return float((n * (c ? 7 : 3) + (c ? 11 : 5)) % 43 - 21) * .001f; };
    const auto aux = [](int c, int n) { return float((n * (c ? 11 : 5) + (c ? 3 : 17)) % 31 - 15) * .0005f; };
    struct Case
    {
        int in, out, channels, mono, auxBus, auxChannels;
        float inputWeight, auxWeight;
    };
    // Literal native weights1/3/7 and output multipliers1/2 are independently
    // defined by the SDK fixture, never derived from runtime metadata or PCM.
    const std::array<Case, 9> cases{{{0, 0, 2, 0, -1, 0, 1, 0},
                                     {1, 1, 2, 0, -1, 0, 3, 0},
                                     {1, 1, 1, 1, -1, 0, 3, 0},
                                     {1, 1, 1, 2, -1, 0, 3, 0},
                                     {1, 1, 1, 3, -1, 0, 3, 0},
                                     {0, 1, 2, 0, 2, 2, 1, 7},
                                     {2, 1, 2, 0, 1, 1, 7, 3},
                                     {2, 1, 1, 3, 1, 2, 7, 3},
                                     {1, 0, 1, 1, 2, 2, 3, 7}}};
    juce::String report = "Original SDK native bus choice; exact offline1Q PCM, not "
                          "UI/installed-provider/listening/hardware qualification.\n";
    const auto retain = [&]
    {
        require(folder.getChildFile("bus-choice.txt").replaceWithText(report),
                "Retain actual bus case measurements before assertions");
    };
    for (const auto &test : cases)
    {
        PluginBusRequest request{test.auxBus, test.auxChannels};
        request.mainInputBus = test.in;
        request.mainOutputBus = test.out;
        request.mainInputChannels = test.channels;
        request.mainInputMonoSource = test.mono;
        require(request.valid(), "Original case declares coherent explicit main and detector buses");
        PluginHost host;
        PluginBusCatalogue untouched;
        untouched.inputCount = 31;
        require(!host.readBusCatalogue(untouched) && untouched.inputCount == 31,
                "Unprepared metadata refuses without replacing caller output");
        const auto started = host.start(original, 48000, quantum, true, {}, request);
        report += "input=" + juce::String(test.in) + " output=" + juce::String(test.out) +
                  " channels=" + juce::String(test.channels) + " mono=" + juce::String(test.mono) +
                  " aux=" + juce::String(test.auxBus) + " result=" + started.getErrorMessage() + "\n";
        retain();
        require(started.wasOk(), "Actual selected native buses negotiate before any packet: " +
                                     started.getErrorMessage().toStdString());
        const auto pid = host.processId();
        const auto instance = host.runtimeIdentity();
        PluginBusCatalogue catalogue;
        require(host.busLayout() == request && host.readBusCatalogue(catalogue) && catalogue.valid() &&
                    catalogue.inputCount == 3 && catalogue.outputCount == 3,
                "Actual complete immutable bus catalogue and selected request match");
        for (int i = 0; i < 3; ++i)
        {
            const auto &input = catalogue.entries[size_t(i)], &output = catalogue.entries[size_t(3 + i)];
            require(input.input && input.index == i && !output.input && output.index == i &&
                        juce::String::fromUTF8(input.name).isNotEmpty() &&
                        juce::String::fromUTF8(output.name).isNotEmpty(),
                    "Actual native bus indices/directions/names retained");
            require(input.actualChannels == (i == test.in       ? test.channels
                                             : i == test.auxBus ? test.auxChannels
                                                                : 0) &&
                        output.actualChannels == (i == test.out ? 2 : 0),
                    "Only selected main/detector/result buses enabled");
        }
        require(catalogue.entries[5].defaultChannels == 6 && catalogue.entries[5].actualChannels == 0,
                "Real surround default is reported honestly but disabled, never treated as supported stereo");
        juce::AudioBuffer<float> audio(2, offset + samples + 7), detector(2, auxOffset + samples + 9);
        audio.clear();
        detector.clear();
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < samples; ++n)
            {
                audio.setSample(c, offset + n, main(c, n));
                detector.setSample(c, auxOffset + n, aux(c, n));
            }
        constexpr std::array<int, 6> spans{1, 17, 127, 31, 256, 109};
        int cursor = 0, part = 0;
        while (cursor < samples)
        {
            const int count = std::min(spans[size_t(part++ % spans.size())], samples - cursor);
            host.process(audio, offset + cursor, count, nullptr, 0, cursor / 48000., 120, true, false, 4, 4,
                         nullptr, 0, 0, true, test.auxChannels ? &detector : nullptr, auxOffset + cursor);
            cursor += count;
        }
        double error = 0;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < samples; ++n)
            {
                const int source = n - 256;
                float expected = 0;
                if (source >= 0)
                {
                    const float selected = test.channels == 2 ? main(c, source)
                                           : test.mono == 1   ? main(0, source)
                                           : test.mono == 2   ? main(1, source)
                                                              : .5f * main(0, source) + .5f * main(1, source);
                    const float side = test.auxChannels ? aux(test.auxChannels == 1 ? 0 : c, source) : 0.f;
                    expected = float(test.out + 1) * (test.inputWeight * selected + test.auxWeight * side);
                }
                error = std::max(error, std::abs(double(audio.getSample(c, offset + n)) - expected));
            }
        const auto stats = host.statistics();
        report += "maximumError=" + juce::String(error, 12) +
                  " submitted=" + juce::String(juce::int64(stats.submitted)) +
                  " processed=" + juce::String(juce::int64(stats.processed)) +
                  " missed=" + juce::String(juce::int64(stats.missed)) + "\n";
        retain();
        require(error < 3.e-7 && host.alive() && host.processId() == pid &&
                    host.runtimeIdentity() == instance && !host.deliveryFailed() && stats.missed == 0 &&
                    stats.dropped == 0 && stats.auxiliaryRejected == 0,
                "All-frame independent main/output/mono/aux PCM with exact256 transport and real worker "
                "identity");
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < samples; ++n)
                require(detector.getSample(c, auxOffset + n) == aux(c, n),
                        "Independent auxiliary source is never overwritten by native bus packing");
        require(host.matches(original, 48000, quantum, request),
                "Exact actual selected-layout Host reuse identity");
        auto different = request;
        different.mainOutputBus = 1 - test.out;
        require(!host.matches(original, 48000, quantum, different),
                "Different output choice cannot silently reuse old native layout");
    }
    constexpr std::array<const char *, 4> errors{"pluginIo.busUnavailable:", "pluginIo.busUnavailable:",
                                                 "pluginIo.unsupportedLayout:", "pluginIo.invalidRequest:"};
    int negative = 0;
    for (const auto &bad : std::array<PluginBusRequest, 4>{
             {{-1, 0, 31, 0, 2, 0}, {-1, 0, 0, 31, 2, 0}, {-1, 0, 0, 2, 2, 0}, {1, 2, 1, 0, 2, 0}}})
    {
        PluginHost host;
        const auto result = host.start(original, 48000, quantum, true, {}, bad);
        report += "negative=" + result.getErrorMessage() + "\n";
        retain();
        require(result.failed() && result.getErrorMessage().startsWith(errors[size_t(negative++)]) &&
                    host.statistics().submitted == 0,
                "Unavailable/unsupported/colliding native bus has exact typed refusal before packets, no "
                "fallback");
    }
    for (const PluginMainIo bad : std::array<PluginMainIo, 5>{
             {{-1, 0, 2, 0}, {0, 32, 2, 0}, {0, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 2, 3}}})
        require(!bad.valid(),
                "Invalid canonical input/output/mapping choice cannot manufacture a native request");
    // Actual application graph/export, independent from the direct IPC packets
    // above. Two built-in voices form unequal L/R input without external assets.
    // All gains remain below clipping even under the literal14x native output.
    auto dryProject = Project::empty();
    dryProject.tempo = 120;
    auto &left = dryProject.channels[0];
    auto &right = dryProject.channels[1];
    left.mixer = right.mixer = 1;
    left.gain = .02f;
    right.gain = .01f;
    left.pan = -1;
    right.pan = 1;
    dryProject.patterns[0].notes = {{dryProject.allocate(), left.id, 0, 1, 60, .8f},
                                    {dryProject.allocate(), right.id, .25, .75, 64, .6f}};
    dryProject.placements = {{dryProject.allocate(), dryProject.patterns[0].id, 0, 0, 4, 0}};
    for (auto &insert : dryProject.mixer)
        insert.gain = 1;
    require(dryProject.validate().wasOk(), "Original low-gain stereo export project validates");
    AudioEngine renderEngine(false, folder.getChildFile("graph-guide-owned.json"));
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto renderPcm = [&](const Project &project, const juce::String &name)
    {
        const auto output = folder.getChildFile(name + ".wav");
        const auto result = renderEngine.renderSync(project, output, 48000, 24, quantum);
        report += "graphRender=" + name + " result=" + result.getErrorMessage() + "\n";
        retain();
        require(result.wasOk(),
                "Actual selected-bus AudioEngine export: " + result.getErrorMessage().toStdString());
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(output));
        require(reader && reader->sampleRate == 48000 && reader->numChannels == 2 &&
                    reader->lengthInSamples == 96000,
                "Original four-beat Song exports exactly96000 stereo frames at48k");
        juce::AudioBuffer<float> pcm(2, 96000);
        require(reader->read(&pcm, 0, pcm.getNumSamples(), 0, true, true),
                "Genuine exported WAV reads completely");
        return pcm;
    };
    const auto dry = renderPcm(dryProject, "graph-original-dry");
    int signalFrames = 0;
    double stereoDifference = 0;
    for (int n = 0; n < dry.getNumSamples(); ++n)
    {
        if (std::abs(dry.getSample(0, n)) + std::abs(dry.getSample(1, n)) > .00001f)
            ++signalFrames;
        stereoDifference =
            std::max(stereoDifference, std::abs(double(dry.getSample(0, n)) - dry.getSample(1, n)));
    }
    require(signalFrames > 1000 && stereoDifference > .001,
            "Independent dry reference is musical/nonzero and L/R are observably different");
    for (const bool monoAverage : {false, true})
    {
        auto selected = dryProject;
        auto effect = original;
        effect.id = selected.allocate();
        effect.mainInputBus = 2;
        effect.mainOutputBus = 1;
        effect.mainInputChannels = monoAverage ? 1 : 2;
        effect.mainInputMonoSource = monoAverage ? 3 : 0;
        selected.mixer[1].effects = {effect};
        const juce::String name = monoAverage ? "graph-selected-mono-average" : "graph-selected-stereo";
        const auto projectFile = folder.getChildFile(name + ".vibra");
        require(saveProject(selected, projectFile).wasOk(), "Actual selected native layout project saves");
        Project reopened;
        require(loadProject(projectFile, reopened).wasOk() &&
                    reopened.state().isEquivalentTo(selected.state()) &&
                    reopened.plugin(effect.id)->mainInputBus == 2 &&
                    reopened.plugin(effect.id)->mainOutputBus == 1 &&
                    reopened.plugin(effect.id)->mainInputChannels == (monoAverage ? 1 : 2) &&
                    reopened.plugin(effect.id)->mainInputMonoSource == (monoAverage ? 3 : 0),
                "Actual save/reopen preserves exact selected main I/O before graph construction");
        const auto actual = renderPcm(reopened, name);
        double maximumError = 0, maximumOutput = 0;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < dry.getNumSamples(); ++n)
            {
                // Original fixture input bus2 weight7, output bus1 multiplier2.
                // No phase fitting, sample skipping or metadata-derived gain.
                const double input = monoAverage
                                         ? .5 * double(dry.getSample(0, n)) + .5 * double(dry.getSample(1, n))
                                         : double(dry.getSample(c, n));
                const double expected = 14. * input;
                const double value = actual.getSample(c, n);
                require(std::isfinite(value), "Every rendered native-layout sample is finite");
                maximumError = std::max(maximumError, std::abs(value - expected));
                maximumOutput = std::max(maximumOutput, std::abs(value));
            }
        report += "graphOracle=" + name +
                  " frames=96000 gain=14 maximumError=" + juce::String(maximumError, 12) +
                  " outputPeak=" + juce::String(maximumOutput, 12) +
                  " referenceSignalFrames=" + juce::String(signalFrames) + "\n";
        retain();
        // Independent24-bit quantization error bound:14*.5LSB + .5LSB,
        // plus float graph multiplication rounding; no loosened phase oracle.
        require(
            maximumOutput > .01 && maximumOutput < 1 && maximumError < 2.e-6,
            "Full96000-frame native graph/export matches selected stereo/mono-average14x PCM at exact phase");
    }
}
