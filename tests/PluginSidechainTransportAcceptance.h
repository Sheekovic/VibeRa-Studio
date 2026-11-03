// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginAutomationStream.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

static void pluginSidechainAutomationSpanTests(const juce::File &folder, const PluginSpec &spec)
{
    constexpr int quantum = 256, samples = 1024, mainOffset = 13, auxOffset = 23;
    constexpr double rate = 48000;
    juce::String report = "Actual native automation stream: four quanta with independent detector PCM.\n";
    {
        PluginHost host;
        require(host.start(spec, rate, quantum, true).wasOk(), "Actual worker for invalid main-span guard");
        juce::AudioBuffer<float> audio(2, quantum);
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < quantum; ++n)
                audio.setSample(c, n, .125f);
        auto stream = std::make_unique<PluginAutomationStream>();
        unsigned errors = 0;
        for (const auto [offset, count] :
             {std::pair{-1, quantum}, std::pair{std::numeric_limits<int>::max(), quantum},
              std::pair{1, std::numeric_limits<int>::max()}, std::pair{0, -1}, std::pair{quantum, 1}})
        {
            require(stream->process(host, nullptr, spec.id, audio, offset, count, {}, 0, rate, 120, true,
                                    false) == PluginAutomationStream::Status::invalidTiming,
                    "Malformed main span refused before stream fast path or partition arithmetic");
            host.process(audio, offset, count, nullptr, 0, 0, 120, true, false);
            errors += 2;
        }
        bool intact = true;
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < quantum; ++n)
                intact &= audio.getSample(c, n) == .125f;
        require(intact && host.packetFrame() == 0 && host.statistics().submitted == 0 &&
                    host.automationStatistics().invalidTiming == errors && host.deliveryFailed(),
                "Invalid main windows preserve caller memory and packet cursor, with sticky render failure");
        report += "invalidMainSpans=10; memoryAndCursorPreserved=true; deliveryFailed=true\n";
    }
    for (const bool malformedOffset : {false, true})
    {
        PluginHost host;
        require(host.start(spec, rate, quantum, true, {}, {1, 2}).wasOk(),
                "Actual stereo-detector worker for multi-quantum native automation");
        const auto catalogue = parameterCatalogue(host);
        const auto pid = host.processId();
        LiveDocument document;
        AutomationDocument curves;
        const AutomationKey amount{AutomationParameter::pluginNormalized, spec.id, 71};
        curves.initialValues = {{amount, 0.f}};
        curves.sources = {{1, 4, {{2, 0, .1f}, {3, 4, .9f}}, {{amount, 0, 1}}}};
        curves.placements = {{4, 1, 0, 4, 0}};
        std::string error;
        require(AutomationProgram::compile(curves, true, document.automation, error),
                "Actual native detector-amount curve compiled");
        document.nativeAutomation = {{spec.id, catalogue.generation, host.runtimeIdentity()}};
        auto stream = std::make_unique<PluginAutomationStream>();
        juce::AudioBuffer<float> audio(2, mainOffset + samples + 7), detector(2, auxOffset + samples + 11);
        audio.clear();
        detector.clear();
        const auto main = [](int channel, int n)
        { return n == 0 || n == 317 ? (channel == 0 ? .125f : -.25f) : 0.f; };
        const auto side = [](int channel, int n)
        { return n >= 0 ? float((n * (channel == 0 ? 13 : 19) + 7) % 97 - 48) * .001f : 0.f; };
        for (int channel = 0; channel < 2; ++channel)
            for (int n = 0; n < samples; ++n)
            {
                audio.setSample(channel, mainOffset + n, main(channel, n));
                detector.setSample(channel, auxOffset + n, side(channel, n));
            }
        const auto status = stream->process(host, &document, spec.id, audio, mainOffset, samples, {}, .5,
                                            rate, 120, true, false, 4, 4, &detector,
                                            malformedOffset ? std::numeric_limits<int>::max() : auxOffset);
        double maximumError = 0;
        for (int channel = 0; channel < 2; ++channel)
            for (int n = 0; n < samples; ++n)
            {
                const int source = n - quantum - 31;
                float expected = source >= 0 ? main(channel, source) : 0.f;
                if (source >= 0 && !malformedOffset)
                {
                    const double beat = 1. + source * 2. / rate;
                    const float amountValue = float(double(.1f) + (double(.9f) - .1f) * beat / 4.);
                    expected += amountValue * side(channel, source);
                }
                maximumError = std::max(
                    maximumError, std::abs(double(audio.getSample(channel, mainOffset + n)) - expected));
            }
        const auto stats = host.statistics();
        report += "malformedOffset=" + juce::String(int(malformedOffset)) +
                  " maximumPCMError=" + juce::String(maximumError, 10) +
                  " auxiliaryRejected=" + juce::String(juce::int64(stats.auxiliaryRejected)) +
                  " parametersApplied=" + juce::String(juce::int64(stats.parametersApplied)) + "\n";
        require(folder.getChildFile("sidechain-automation-span-measurements.txt").replaceWithText(report),
                "Multi-quantum aux/automation evidence retained before oracle");
        require(
            status == PluginAutomationStream::Status::ready && maximumError < 3.e-7 &&
                stats.parametersApplied > 0 && stats.parametersRejected == 0 && host.alive() &&
                host.processId() == pid && stats.missed == 0 && stats.dropped == 0,
            "Real multi-quantum native amount events preserve exact main/detector timing and owned worker");
        require(malformedOffset ? stats.auxiliaryRejected == 4 && host.deliveryFailed() &&
                                      host.error().containsIgnoreCase("auxiliary")
                                : stats.auxiliaryRejected == 0 && !host.deliveryFailed(),
                "Whole invalid INT_MAX detector span produces four explicit silent/refused aux slices; valid "
                "span is clean");
    }
}

// Include after ParameterAcceptance.h. Backend experiment only: no graph/UI or
// provider compatibility claim follows from this original native SDK fixture.
static void pluginSidechainTransportTests(const juce::File &folder, const juce::File &binary)
{
    require(folder.createDirectory().wasOk(), "Sidechain transport evidence directory");
    require(binary.existsAsFile(), "Explicit original sidechain/latency fixture exists");
    auto spec = parameterSpec(binary);
    require(!spec.instrument && spec.name == "VibeRa Mixer Latency Fixture",
            "Sidechain probe uses genuine independent SDK effect");
    juce::String report = "Original native SDK sidechain transport; no graph/native-input/listening claim.\n";
    const auto retain = [&]
    {
        require(folder.getChildFile("sidechain-transport-measurements.txt").replaceWithText(report),
                "Sidechain measurements retained before musical oracle assertions");
    };
    constexpr int frames = 256, samples = 4096, mainOffset = 13, auxOffset = 23;
    const auto mainSignal = [](int channel, int n)
    {
        if (n == 0)
            return channel == 0 ? .375f : -.25f;
        if (n == 9)
            return channel == 0 ? -.125f : .5f;
        if (n == 733)
            return channel == 0 ? .0625f : .125f;
        return 0.f;
    };
    const auto detectorSignal = [](int channel, int n)
    {
        if (n < 0 || n >= 2000)
            return 0.f;
        // Dense, signed, independent L/R detector PCM exposes swapped channels,
        // stale packet tails, wrong source offsets and accidental main copying.
        return float((n * (channel == 0 ? 13 : 19) + (channel == 0 ? 7 : 29)) % 97 - 48) * .001f;
    };
    const auto probe = [&](PluginHost &host, int channels, float amount, bool bypass, bool missingWindow,
                           std::int64_t &clock)
    {
        juce::AudioBuffer<float> main(2, samples + mainOffset + 7);
        juce::AudioBuffer<float> auxiliary(std::max(1, channels), samples + auxOffset + 11);
        main.clear();
        auxiliary.clear();
        for (int channel = 0; channel < 2; ++channel)
            for (int n = 0; n < samples; ++n)
                main.setSample(channel, mainOffset + n, mainSignal(channel, n));
        for (int channel = 0; channel < auxiliary.getNumChannels(); ++channel)
            for (int n = 0; n < samples; ++n)
                auxiliary.setSample(channel, auxOffset + n, detectorSignal(channel, n));
        constexpr std::array<int, 7> partitions{1, 17, 127, 31, 80, 263, 109};
        int cursor = 0, partition = 0;
        while (cursor < samples)
        {
            int count = std::min(partitions[size_t(partition++ % partitions.size())], samples - cursor);
            if (missingWindow && cursor < 257)
                count = std::min(count, 257 - cursor);
            else if (missingWindow && cursor < 1023)
                count = std::min(count, 1023 - cursor);
            const bool present = channels != 0 && !(missingWindow && cursor >= 257 && cursor < 1023);
            host.process(main, mainOffset + cursor, count, nullptr, 0, clock / 48000., 120, true, bypass, 4,
                         4, nullptr, 0, 0, true, present ? &auxiliary : nullptr, auxOffset + cursor);
            cursor += count;
            clock += count;
        }
        double maximumError = 0;
        for (int channel = 0; channel < 2; ++channel)
            for (int n = 0; n < samples; ++n)
            {
                const auto sourceFrame = n - frames - 31;
                const bool present = channels != 0 && sourceFrame >= 0 &&
                                     !(missingWindow && sourceFrame >= 257 && sourceFrame < 1023);
                const auto sidechain =
                    present && !bypass ? detectorSignal(channels == 1 ? 0 : channel, sourceFrame) : 0.f;
                const auto expected = mainSignal(channel, sourceFrame) + amount * sidechain;
                maximumError = std::max(maximumError,
                                        std::abs(double(main.getSample(channel, mainOffset + n)) - expected));
            }
        report += "channels=" + juce::String(channels) + " amount=" + juce::String(amount) +
                  " bypass=" + juce::String(int(bypass)) +
                  " missingWindow=" + juce::String(int(missingWindow)) +
                  " maximumPCMError=" + juce::String(maximumError, 10) + "\n";
        retain();
        require(
            maximumError < 2.e-7,
            "Independent main/detector PCM survives exact offsets, channel mapping and packet partitioning");
        bool detectorUnchanged = true;
        for (int channel = 0; channel < auxiliary.getNumChannels(); ++channel)
            for (int n = 0; n < samples; ++n)
                detectorUnchanged = detectorUnchanged &&
                                    auxiliary.getSample(channel, auxOffset + n) == detectorSignal(channel, n);
        require(detectorUnchanged, "Host does not mutate independent detector source PCM");
        require(host.alive() && !host.deliveryFailed(), "Offline sidechain transport delivers every packet");
    };
    juce::String savedState;
    for (const int channels : {0, 1, 2})
    {
        const PluginBusRequest layout{channels == 0 ? -1 : 1, channels};
        PluginHost host;
        const auto result = host.start(spec, 48000, frames, true, {}, layout);
        require(result.wasOk(), "Actual native sidechain worker accepts requested layout: " +
                                    result.getErrorMessage().toStdString());
        require(host.busLayout().auxiliaryInputBus == layout.auxiliaryInputBus &&
                    host.busLayout().auxiliaryChannels == channels,
                "Actual validated layout readback equals immutable request");
        const auto pid = host.processId();
        const auto catalogue = parameterCatalogue(host);
        require(nativeParameter(catalogue, 71).automatable() && host.latencySamples() == frames + 31,
                "Real sidechain amount parameter and native delay31 available");
        std::int64_t clock = 0;
        require(parameterResult(host, host.setParameter(71, 0, catalogue.generation)).result == 0,
                "Detector amount0 acknowledged through supported API");
        probe(host, channels, 0, false, false, clock);
        require(parameterResult(host, host.setParameter(71, 1, catalogue.generation)).result == 0,
                "Detector amount1 acknowledged through supported API");
        probe(host, channels, 1, false, false, clock);
        probe(host, channels, 1, false, true, clock);
        probe(host, channels, 1, true, false, clock);
        require(host.processId() == pid && host.alive(),
                "Amount, missing input and bypass retain same worker");
        if (channels == 2)
        {
            const auto request = host.requestState();
            require(request != 0, "Sidechain native state capture admitted");
            const auto deadline = juce::Time::getMillisecondCounter() + 10000;
            while (!host.readState(request, savedState) && juce::Time::getMillisecondCounter() < deadline)
                juce::Thread::sleep(2);
            require(savedState.isNotEmpty(), "Sidechain native state capture actually completed");
        }
    }
    {
        auto restored = spec;
        restored.state = savedState;
        PluginHost host;
        const auto result = host.start(restored, 48000, frames, true, {}, PluginBusRequest{1, 2});
        require(result.wasOk(), "Stereo auxiliary layout revalidated after actual native state restore");
        const auto catalogue = parameterCatalogue(host);
        require(nativeParameter(catalogue, 71).value == 1 && host.busLayout().auxiliaryChannels == 2,
                "Native amount and explicit connection both restored without silent fallback");
        std::int64_t clock = 0;
        probe(host, 2, 1, false, true, clock);
    }
    {
        juce::String mutationState;
        {
            PluginHost author;
            require(author.start(spec, 48000, frames, true, {}, {1, 2}).wasOk(),
                    "Normal original worker creates genuine preparation-mutation state");
            const auto catalogue = parameterCatalogue(author);
            require(nativeParameter(catalogue, 113).writable() &&
                        !nativeParameter(catalogue, 113).automatable() &&
                        parameterResult(author, author.setParameter(113, 1, catalogue.generation)).result ==
                            0,
                    "Supported native control schedules layout mutation only at next preparation");
            const auto request = author.requestState();
            require(request != 0, "Actual mutation state capture admitted");
            const auto deadline = juce::Time::getMillisecondCounter() + 10000;
            while (!author.readState(request, mutationState) &&
                   juce::Time::getMillisecondCounter() < deadline)
                juce::Thread::sleep(2);
            require(mutationState.isNotEmpty() && author.alive(),
                    "Native state records future mutation without corrupting current stereo worker");
        }
        auto mutation = spec;
        mutation.state = mutationState;
        PluginHost refused;
        const auto result = refused.start(mutation, 48000, frames, true, {}, {1, 2});
        const auto statistics = refused.statistics();
        report += "preparedNativeLayoutMutation startupError=" + result.getErrorMessage() +
                  " heartbeat=" + juce::String(juce::int64(statistics.workerHeartbeat)) +
                  " processed=" + juce::String(juce::int64(statistics.processed)) + "\n";
        retain();
        require(result.failed() && result.getErrorMessage().containsIgnoreCase("bus") && !refused.alive() &&
                    statistics.workerHeartbeat == 0 && statistics.processed == 0,
                "Genuinely mutated native main/aux arrangements rejected before starting any DSP loop");
    }
    pluginSidechainAutomationSpanTests(folder, spec);
    // These are unsupported/malformed topology requests, not arbitrary malformed
    // audio pointers. Worker-side failures may still retain an owned PID until
    // retirement, so availability rather than PID0 is the correctness assertion.
    for (const auto layout :
         {PluginBusRequest{-1, 1}, PluginBusRequest{1, 0}, PluginBusRequest{1, 3}, PluginBusRequest{2, 1},
          PluginBusRequest{0, 1}, PluginBusRequest{99, 2}, PluginBusRequest{-2, 0}})
    {
        PluginHost host;
        const auto result = host.start(spec, 48000, frames, true, {}, layout);
        report += "rejected request bus=" + juce::String(layout.auxiliaryInputBus) +
                  " channels=" + juce::String(layout.auxiliaryChannels) +
                  " error=" + result.getErrorMessage() + "\n";
        retain();
        require(result.failed() && result.getErrorMessage().isNotEmpty() && !host.alive(),
                "Invalid/unsupported aux requests fail explicitly instead of silently disabling detector");
    }
    {
        auto instrument = spec;
        instrument.instrument = true;
        PluginHost host;
        const auto result = host.start(instrument, 48000, frames, true, {}, PluginBusRequest{1, 1});
        require(result.failed() && !host.alive(), "Instrument aux hosting remains explicitly unsupported");
    }
    {
        auto host = std::make_shared<PluginHost>();
        require(host->start(spec, 48000, frames, true, {}, {1, 1}).wasOk(),
                "Prepare real worker for detector-buffer error/commit guard");
        juce::AudioBuffer<float> audio(2, frames), tooShort(1, 7), wrongChannels(0, frames);
        audio.clear();
        tooShort.clear();
        host->process(audio, 0, frames, nullptr, 0, 0, 120, true, false, 4, 4, nullptr, 0, 0, true,
                      &tooShort);
        host->process(audio, 0, frames, nullptr, 0, frames / 48000., 120, true, false, 4, 4, nullptr, 0, 0,
                      true, &tooShort, -1);
        host->process(audio, 0, frames, nullptr, 0, 2 * frames / 48000., 120, true, false, 4, 4, nullptr, 0,
                      0, true, &wrongChannels);
        require(host->alive() && host->statistics().auxiliaryRejected == 3 && host->deliveryFailed() &&
                    host->error().containsIgnoreCase("auxiliary"),
                "Malformed detector buffers become silence with sticky truthful delivery errors");
        require(audio.getMagnitude(0, frames) == 0, "Rejected detector never replays stale packet samples");
        const auto destination = folder.getChildFile("owned-commit-fault.wav");
        const juce::String original =
            "Owned non-audio overwrite sentinel for commit fault, not a rendered WAV";
        require(destination.replaceWithText(original), "Owned overwrite sentinel created explicitly");
        juce::TemporaryFile replacement(destination);
        require(replacement.getFile().replaceWithText("Unqualified replacement must not commit"),
                "Owned negative commit fixture prepared");
        const std::array<std::shared_ptr<PluginHost>, 1> hosts{host};
        require(
            AudioEngine::commitRenderedFile(replacement, hosts, true, {}).failed() &&
                destination.loadFileAsString() == original,
            "Actual shared render commit refuses auxiliary delivery loss without overwriting destination");
        report += "invalidAuxiliaryBuffers=3; sharedRenderCommitRejected=true; destinationPreserved=true\n";
        retain();
    }
    report += "PASS bounded backend transport only; graph tap/PDC/project/UI/MCP/native/listening remain "
              "separate.\n";
    retain();
}
