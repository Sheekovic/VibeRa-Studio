// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginAutomationBlock.h"

static void nativeAutomationPacketSafetyTests(const juce::File &folder, const juce::File &binary)
{
    const auto spec = parameterSpec(binary);
    constexpr int frames = 256;
    juce::String report;
    for (const int scenario : {0, 1, 2, 3, 4})
    {
        PluginHost host;
        require(host.start(spec, 48000, frames, true).wasOk(), "packet safety worker prepared");
        const auto pid = host.processId();
        const auto original = parameterCatalogue(host);
        juce::AudioBuffer<float> audio(2, frames);
        for (int c = 0; c < 2; ++c)
            std::fill_n(audio.getWritePointer(c), frames, 1.f);
        const int firstCount = scenario == 2 ? ipc::parameterEvents : 1;
        std::vector<ipc::ParameterEvent> prefix(std::size_t(firstCount), {0, 0, .2f});
        host.process(audio, 0, 1, nullptr, 0, 0, 120, true, false, 4, 4, prefix.data(), firstCount,
                     original.generation);
        require(host.statistics().submitted == 0, "partial packet remains unsubmitted");
        auto generation = original.generation;
        if (scenario <= 1)
        {
            require(parameterResult(host, host.setParameter(42, 1.f, generation)).result == 0,
                    "actual native schema changed while partial host audio packet is retained");
            const auto until = juce::Time::getMillisecondCounter() + 2000;
            do
            {
                generation = parameterCatalogue(host, true).generation;
                if (generation != original.generation)
                    break;
                juce::Thread::sleep(2);
            } while (juce::Time::getMillisecondCounter() < until);
            require(generation != original.generation, "native schema change observed before packet submit");
        }
        ipc::ParameterEvent suffix{0, scenario == 3 ? frames - 1 : 0, .9f};
        const auto *event = scenario == 0 ? nullptr : &suffix;
        const int count = event ? 1 : 0;
        host.process(audio, 1, frames - 1, nullptr, 0, 1. / 48000, 120, true, false, 4, 4, event, count,
                     scenario == 4 ? 0 : generation);
        audio.clear();
        host.process(audio, 0, frames, nullptr, 0, frames / 48000., 120, true, false);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < frames; ++n)
                require(std::abs(audio.getSample(c, n) - .5f) < .000001f,
                        "refused packet applies no earlier prefix and preserves native gain/audio");
        require(
            host.statistics().parametersApplied == 0 &&
                host.statistics().parametersRejected == std::uint64_t(firstCount + count) &&
                host.processId() == pid && host.alive() && host.statistics().dropped == 0,
            "stale/mixed/overflow/malformed/missing-generation packet refuses atomically without restart");
        report += "scenario=" + juce::String(scenario) +
                  "; rejected=" + juce::String(juce::int64(host.statistics().parametersRejected)) +
                  "; applied=0; PCM retained; worker retained PASS\n";
    }
    require(folder.getChildFile("native-automation-packet-safety.txt").replaceWithText(report),
            "packet safety evidence saved");
}

static void nativeDenseAutomationTests(const juce::File &folder, const juce::File &binary)
{
    const auto spec = parameterSpec(binary);
    juce::String report;
    double maximumError = 0;
    for (const int frames : {256, 512, 1024, 4096})
    {
        PluginHost host;
        require(host.start(spec, 48000, frames, true).wasOk(), "dense native worker prepared");
        const auto catalogue = parameterCatalogue(host);
        AutomationDocument doc;
        const AutomationKey gain{AutomationParameter::pluginNormalized, spec.id, 0};
        const AutomationKey bias{AutomationParameter::pluginNormalized, spec.id, 0xfedcba98u};
        doc.initialValues = {{gain, .5f}, {bias, 0.f}};
        doc.sources = {{1, 4, {{2, 0, .1f}, {3, 4, .9f}}, {{gain, 0, 1}, {bias, 1, 0}}}};
        doc.placements = {{4, 1, 0, 4, 0}};
        AutomationProgram program;
        std::string error;
        require(AutomationProgram::compile(doc, true, program, error), "dense native curves prepared");
        const auto admission = PluginAutomationBlock::admit(program, spec.id, 2. / 48000, frames);
        auto batch = std::make_unique<PluginAutomationBlock>();
        require(admission.result == PluginAutomationBlock::Result::ok &&
                    admission.maximumEvents == std::size_t(frames * 2) &&
                    batch->render(program, spec.id, 1, 2. / 48000, frames, true) ==
                        PluginAutomationBlock::Result::ok &&
                    batch->events().size() == std::size_t(frames * 2),
                "two simultaneous native ramps retain every changing sample through4096-frame quantum");
        juce::AudioBuffer<float> audio(2, frames);
        for (int c = 0; c < 2; ++c)
            std::fill_n(audio.getWritePointer(c), frames, c ? -.5f : .75f);
        const auto events = batch->events();
        host.process(audio, 0, frames, nullptr, 0, .5, 120, true, false, 4, 4, events.data(),
                     int(events.size()), catalogue.generation);
        audio.clear();
        host.process(audio, 0, frames, nullptr, 0, .5 + frames / 48000., 120, true, false);
        for (int n = 0; n < frames; ++n)
        {
            const double t = (1 + n * 2. / 48000) / 4;
            const double delta = (double(.9f) - .1f) * t;
            const float expectedGain = float(double(.1f) + delta);
            const float expectedBias = float(1 - double(.1f) - delta);
            for (int c = 0; c < 2; ++c)
            {
                const auto difference = std::abs(
                    double(audio.getSample(c, n) - ((c ? -.5f : .75f) * expectedGain + expectedBias)));
                maximumError = std::max(maximumError, difference);
                require(difference < .000001, "dense two-native-parameter PCM matches independent oracle");
            }
        }
        require(host.statistics().parametersApplied == std::uint64_t(frames * 2) &&
                    host.statistics().parametersRejected == 0 && host.statistics().missed == 0,
                "complete dense native packet accepted without loss");
        report += "frames=" + juce::String(frames) + "; sample-offset events=" + juce::String(frames * 2) +
                  "; PCM PASS\n";
    }
    report += "Maximum PCM error=" + juce::String(maximumError, 10) +
              "; original SDK fixture only; no actual-provider changing-response/listening claim.\n";
    require(folder.getChildFile("native-dense-automation.txt").replaceWithText(report),
            "dense native curve evidence saved");
}

static void nativeAutomationCurveTests(const juce::File &folder, const juce::File &binary)
{
    const auto spec = parameterSpec(binary);
    const AutomationKey key{AutomationParameter::pluginNormalized, spec.id, 0};
    double maximumError = 0;
    unsigned rendered = 0, delivered = 0;
    for (const double rate : {44100., 48000., 96000.})
        for (const int frames : {64, 256, 512})
        {
            PluginHost host;
            require(host.start(spec, rate, frames, true).wasOk(), "native curve fixture preparation");
            const auto catalogue = parameterCatalogue(host);
            require(nativeParameter(catalogue, 0).automatable(),
                    "native curve destination is advertised as automatable");
            auto batch = std::make_unique<PluginAutomationBlock>();
            juce::AudioBuffer<float> audio(2, frames);
            for (const auto shape : {AutomationShape::single, AutomationShape::smooth, AutomationShape::hold})
            {
                AutomationDocument doc;
                doc.initialValues = {{key, .5f}};
                doc.sources = {{1, 4, {{2, 0, .1f}, {3, 4, .9f, shape, -.5f}}, {{key, 0, 1}}}};
                doc.placements = {{4, 1, 0, 4, 0}};
                AutomationProgram program;
                std::string error;
                require(AutomationProgram::compile(doc, true, program, error),
                        "original native curve program preparation");
                // Includes pre-roll, a seek, exact end, fractional boundary crossing and loop-like return.
                for (const double beat : {-.001, 0., 1.25, 3.999, 4., 0.})
                {
                    callbackAllocationProbe = true;
                    const auto result = batch->render(program, spec.id, beat, 2 / rate, frames, true);
                    callbackAllocationProbe = false;
                    require(result == PluginAutomationBlock::Result::ok,
                            "native curve complete prepared batch fits bridge capacity");
                    const auto events = batch->events();
                    delivered += unsigned(events.size());
                    for (int c = 0; c < 2; ++c)
                        for (int n = 0; n < frames; ++n)
                            audio.setSample(c, n, c ? -.25f : .75f);
                    host.process(audio, 0, frames, nullptr, 0, beat / 2, 120, true, false, 4, 4,
                                 events.data(), int(events.size()), catalogue.generation);
                    audio.clear();
                    host.process(audio, 0, frames, nullptr, 0, beat / 2 + frames / rate, 120, true, false);
                    for (int n = 0; n < frames; ++n)
                    {
                        const double position = beat + n * 2 / rate;
                        const double t = std::clamp(position / 4, 0., 1.);
                        const double f = shape == AutomationShape::hold     ? 0.
                                         : shape == AutomationShape::smooth ? t * t * (3 - 2 * t)
                                                                            : t - .5 * t * (1 - t);
                        const float gain = position < 0    ? .5f
                                           : position >= 4 ? .9f
                                                           : float(double(.1f) + (double(.9f) - .1f) * f);
                        for (int c = 0; c < 2; ++c)
                        {
                            const double difference =
                                std::abs(double(audio.getSample(c, n) - (c ? -.25f : .75f) * gain));
                            maximumError = std::max(maximumError, difference);
                            require(difference < .000001, "actual isolated native PCM matches curve oracle");
                        }
                    }
                    ++rendered;
                }
            }
            require(host.statistics().parametersApplied == delivered &&
                        host.statistics().parametersRejected == 0 && host.statistics().dropped == 0,
                    "complete generated automation batches accepted by original native SDK fixture");
            delivered = 0;
        }
    PluginHost live;
    require(live.start(spec, 48000, 256, false).wasOk(), "prepared event pipeline live worker start");
    const auto liveGeneration = parameterCatalogue(live).generation;
    AutomationDocument liveDoc;
    liveDoc.initialValues = {{key, .5f}};
    liveDoc.sources = {{1, 4, {{2, 0, .1f}, {3, 4, .9f}}, {{key, 0, 1}}}};
    liveDoc.placements = {{4, 1, 0, 4, 0}};
    AutomationProgram liveProgram;
    std::string error;
    require(AutomationProgram::compile(liveDoc, true, liveProgram, error),
            "live curve prepared off callback");
    auto generated = std::make_unique<PluginAutomationBlock>();
    juce::AudioBuffer<float> buffer(2, 256);
    double maximumCallbackMs = 0;
    int overruns = 0;
    juce::String trace = "callback,wallMs,eventCount\n";
    for (int i = 0; i < 16; ++i)
    {
        buffer.clear();
        const double beat = 1 + i * 256 * 2. / 48000;
        const auto begin = juce::Time::getHighResolutionTicks();
        callbackAllocationProbe = true;
        const auto result = generated->render(liveProgram, spec.id, beat, 2. / 48000, 256, true);
        const auto events = generated->events();
        live.process(buffer, 0, 256, nullptr, 0, beat / 2, 120, true, false, 4, 4, events.data(),
                     int(events.size()), liveGeneration);
        callbackAllocationProbe = false;
        const double elapsed =
            juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - begin) * 1000;
        maximumCallbackMs = std::max(maximumCallbackMs, elapsed);
        overruns += elapsed > 256000. / 48000 ? 1 : 0;
        require(result == PluginAutomationBlock::Result::ok && events.size() == 256,
                "live original curve generates every changing sample without decimation");
        trace +=
            juce::String(i) + "," + juce::String(elapsed, 6) + "," + juce::String(int(events.size())) + "\n";
        juce::Thread::sleep(3);
    }
    const auto drainDeadline = juce::Time::getMillisecondCounter() + 3000;
    while (live.statistics().processed < live.statistics().submitted &&
           juce::Time::getMillisecondCounter() < drainDeadline)
        juce::Thread::sleep(2);
    require(folder.getChildFile("generated-automation-callbacks.csv").replaceWithText(trace),
            "live generated event callback evidence saved");
    require(live.statistics().parametersApplied == 4096 && live.statistics().parametersRejected == 0 &&
                live.statistics().dropped == 0,
            "all live generated samples delivered to native worker");
    require(overruns == 0, "generated event pipeline retained 256-frame callback deadline");
    require(callbackAllocations.load() == 0 && callbackDestructions.load() == 0,
            "new native automation event emitter has no observed C++ callback allocation/destruction");
    require(folder.getChildFile("native-automation-curves.txt")
                .replaceWithText(
                    "Original compiled curves -> prepared native events -> isolated SDK fixture PCM. Cases=" +
                    juce::String(rendered) + "; rates44100/48000/96000; buffers64/256/512; max PCM error=" +
                    juce::String(maximumError, 10) +
                    "; live16callbacks/4096events maxMs=" + juce::String(maximumCallbackMs, 6) +
                    ". No DAW project-target UI, actual-provider response, hardware or listening claim.\n"),
            "native curve processing evidence saved");
}
