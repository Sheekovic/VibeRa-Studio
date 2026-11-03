// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RemoteEffect.h"
#include "MixerRoutePlugins.h"
#include "AutomationGain.h"
#include "MixerRouting.h"
#include <tracktion_graph/tracktion_graph.h>
#include <3rd_party/crill/seqlock_object.h>
#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>
#include <tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h>
#include <array>
#include <cmath>

// Draft internal backend experiment. The caller explicitly supplies a disposable
// test-output directory; generated WAV names below may be overwritten there.
// Include after ParameterAcceptance.h/readWav. No project/UI/MCP sidechain feature
// completion, native input, hardware, live callback timing or listening claim.
static void pluginSidechainGraphTests(const juce::File &folder, const juce::File &binary)
{
    require(folder.createDirectory().wasOk() && binary.existsAsFile(),
            "Sidechain graph disposable output and original native fixture exist");
    const auto nativeSpec = parameterSpec(binary);
    require(!nativeSpec.instrument && nativeSpec.name == "VibeRa Mixer Latency Fixture",
            "Real original sparse-signal sidechain fixture selected");
    juce::String report = "Actual production pre-Master sidechain tap/RemoteEffect/Tracktion RenderTask.\n";
    const auto retain = [&]
    {
        require(folder.getChildFile("sidechain-graph-measurements.txt").replaceWithText(report),
                "Whole graph PCM measurements retained before musical oracle");
    };
    constexpr double duration = .125;
    constexpr int preparedBus = 20000;
    const auto writeSource = [&](const juce::File &file, double rate, bool detector)
    {
        juce::AudioBuffer<float> samples(2, int(std::lround(rate * duration)));
        samples.clear();
        const std::array<double, 3> times =
            detector ? std::array{.035, .065, .095} : std::array{.025, .055, .085};
        for (int event = 0; event < 3; ++event)
            for (int channel = 0; channel < 2; ++channel)
                for (int n = 0; n < 17; ++n)
                {
                    const float amplitude =
                        detector ? (channel == 0 ? .0625f : -.09375f) : (channel == 0 ? .125f : .03125f);
                    samples.setSample(channel, int(std::lround(rate * times[size_t(event)])) + n,
                                      (event == 1 ? -1.f : 1.f) * amplitude);
                }
        auto stream = file.createOutputStream();
        require(bool(stream), "Original sparse graph source stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), rate, 2, 24, {}, 0));
        require(bool(writer) && writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()),
                "Independent stereo sparse graph source WAV written");
    };
    enum class MasterRoute
    {
        off,
        on,
        quarter,
        routeOnlySilentBus
    };
    struct Scenario
    {
        juce::String name;
        float amount = 1, detectorFader = 1;
        bool detectorMuted = false, mainClipMuted = false, detectorLatency = false, mainLatency = false;
        MasterRoute master = MasterRoute::off;
    };
    struct Capture
    {
        std::array<std::array<float, 65536>, 4> samples{};
        int written = 0;
        bool overflow = false;
        static void record(void *context, const te::PluginRenderContext &fc) noexcept
        {
            auto &capture = *static_cast<Capture *>(context);
            if (fc.bufferNumSamples > 65536 - capture.written)
            {
                capture.overflow = true;
                return;
            }
            for (int c = 0; c < std::min(4, fc.destBuffer->getNumChannels()); ++c)
                std::copy_n(fc.destBuffer->getReadPointer(c, fc.bufferStartSample), fc.bufferNumSamples,
                            capture.samples[size_t(c)].data() + capture.written);
            capture.written += fc.bufferNumSamples;
        }
        juce::String pulses() const
        {
            juce::String result;
            for (int c = 0; c < 4; ++c)
            {
                result += " ch" + juce::String(c) + "=";
                for (int n = 0, hits = 0; n < written && hits < 6; ++n)
                    if (std::abs(samples[size_t(c)][size_t(n)]) > 1.e-6f &&
                        (n == 0 || std::abs(samples[size_t(c)][size_t(n - 1)]) <= 1.e-6f))
                    {
                        result += juce::String(n) + ",";
                        ++hits;
                    }
            }
            return result;
        }
    };
    for (const auto configuration : {std::pair{48000., 256}, std::pair{44100., 128}})
    {
        const auto [rate, block] = configuration;
        const int channels = rate == 48000 ? 2 : 1;
        const auto tag =
            juce::String(int(rate)) + "-" + juce::String(block) + "-aux" + juce::String(channels);
        const auto detectorFile = folder.getChildFile("original-detector-" + tag + ".wav");
        const auto mainFile = folder.getChildFile("original-main-" + tag + ".wav");
        writeSource(detectorFile, rate, true);
        writeSource(mainFile, rate, false);
        const auto render = [&](const Scenario &scenario)
        {
            AudioEngine audio(false);
            auto project = Project::empty();
            project.channels.clear();
            project.placements.clear();
            project.patterns.front().notes.clear();
            for (auto &insert : project.mixer)
                insert.gain = 1;
            project.mixer[1].name = "Original detector insert";
            project.mixer[2].name = "Original main target insert";
            project.mixer[1].gain = scenario.detectorFader;
            project.mixer[1].muted = scenario.detectorMuted;
            project.mixer[1].routes.clear();
            project.mixer[3].routes.clear(); // An audible Route Only destination which cannot leak to Master.
            if (scenario.master == MasterRoute::on || scenario.master == MasterRoute::quarter)
                project.mixer[1].routes.push_back({0, scenario.master == MasterRoute::quarter ? .25f : 1.f});
            else if (scenario.master == MasterRoute::routeOnlySilentBus)
                require(setMixerRoute(project, 1, 3, true, true).wasOk(),
                        "Real Route Only source graph prepared");
            Channel detector;
            detector.id = project.allocate();
            detector.name = "Original detector Audio Clip";
            detector.source = detectorFile.getFullPathName();
            detector.mixer = 1;
            detector.gain = 1;
            detector.audioClip = true;
            detector.audioDuration = duration;
            Channel main = detector;
            main.id = project.allocate();
            main.name = "Original main Audio Clip";
            main.source = mainFile.getFullPathName();
            main.mixer = 2;
            project.channels = {detector, main};
            project.audioPlacements = {
                {project.allocate(), detector.id, 0, 0, 0, duration},
                {project.allocate(), main.id, 1, 0, 0, duration, scenario.mainClipMuted}};
            // No opaque plugin state is injected into a project model. This is
            // deliberately the internal graph adapter experiment, not persistence.
            require(project.validate().wasOk(), "Genuine two-Audio-Clip routing project validates");
            std::vector<std::shared_ptr<PluginHost>> hosts;
            auto sourceCapture = std::make_unique<Capture>();
            auto targetCapture = std::make_unique<Capture>();
            auto targetOutput = std::make_unique<Capture>();
            auto host = std::make_shared<PluginHost>();
            require(host->start(nativeSpec, rate, block, true, {}, {1, channels}).wasOk(),
                    "Actual native worker prepared with mono/stereo detector layout");
            hosts.push_back(host);
            const auto metadata = parameterCatalogue(*host);
            require(
                parameterResult(*host, host->setParameter(71, scenario.amount, metadata.generation)).result ==
                    0,
                "Actual detector amount acknowledged before graph construction");
            const auto effectId = project.allocate();
            juce::String error;
            auto edit =
                audio.buildEdit(project, false, project.patterns.front().id, te::Edit::forRendering, error);
            require(bool(edit), "Actual production Edit prepared: " + error.toStdString());
            const auto findTrack = [&](const juce::String &name)
            {
                for (auto *track : te::getAudioTracks(*edit))
                    if (track->getName() == name)
                        return track;
                return static_cast<te::AudioTrack *>(nullptr);
            };
            auto *source = findTrack(project.mixer[1].name);
            auto *target = findTrack(project.mixer[2].name);
            require(source && target, "Production Mixer source/target tracks identified uniquely");
            auto timeline = std::make_shared<LiveTimeline>();
            auto document = compileLiveDocument(project, false, project.patterns.front().id, 100);
            require(document && timeline->publish(document), "Immutable graph experiment timeline published");
            const auto beforeGain = [](const te::PluginList &list)
            {
                int index = 0;
                for (auto *plugin : list)
                {
                    if (dynamic_cast<AutomationGain *>(plugin))
                        return index;
                    ++index;
                }
                return -1;
            };
            if (scenario.detectorLatency)
            {
                auto delay = std::make_shared<PluginHost>();
                require(delay->start(nativeSpec, rate, block, true).wasOk(),
                        "Independent native delayed detector chain worker prepared");
                hosts.push_back(delay);
                auto plugin = edit->getPluginCache().createNewPlugin(RemoteEffect::xmlTypeName, {});
                auto *remote = dynamic_cast<RemoteEffect *>(plugin.get());
                const auto position = beforeGain(source->pluginList);
                require(remote && position >= 0, "Production detector-chain adapter and gain stage exist");
                remote->configure(delay, false, timeline, project.allocate());
                remote->setInputProbe(sourceCapture.get(), Capture::record);
                source->pluginList.insertPlugin(plugin, position, nullptr);
            }
            if (scenario.mainLatency)
            {
                auto delay = std::make_shared<PluginHost>();
                require(delay->start(nativeSpec, rate, block, true).wasOk(),
                        "Independent native delayed main chain worker prepared");
                hosts.push_back(delay);
                auto plugin = edit->getPluginCache().createNewPlugin(RemoteEffect::xmlTypeName, {});
                auto *remote = dynamic_cast<RemoteEffect *>(plugin.get());
                const auto position = beforeGain(target->pluginList);
                require(remote && position >= 0, "Prepared main delay before detector processor");
                remote->configure(delay, false, timeline, project.allocate());
                target->pluginList.insertPlugin(plugin, position, nullptr);
            }
            {
                auto plugin = edit->getPluginCache().createNewPlugin(MixerSidechainSend::xmlTypeName, {});
                auto *send = dynamic_cast<MixerSidechainSend *>(plugin.get());
                require(send != nullptr, "Production sidechain tap factory registered");
                send->configure(preparedBus);
                int position = 0;
                bool gateFound = false;
                for (auto *existing : source->pluginList)
                {
                    if (dynamic_cast<MixerMasterGate *>(existing))
                    {
                        gateFound = true;
                        break;
                    }
                    ++position;
                }
                require(gateFound, "Actual direct-only Master gate found for pre-gate tap placement");
                source->pluginList.insertPlugin(plugin, position, nullptr);
            }
            {
                auto plugin = edit->getPluginCache().createNewPlugin(RemoteEffect::xmlTypeName, {});
                auto *remote = dynamic_cast<RemoteEffect *>(plugin.get());
                const auto position = beforeGain(target->pluginList);
                require(remote && position >= 0, "Production target remote adapter and gain stage exist");
                remote->configure(host, false, timeline, effectId);
                remote->setInputProbe(targetCapture.get(), Capture::record);
                remote->setOutputProbe(targetOutput.get(), Capture::record);
                require(remote->connectSidechain(source->itemID, preparedBus).wasOk(),
                        "Prepared native auxiliary graph connection accepted");
                target->pluginList.insertPlugin(plugin, position, nullptr);
                require(remote->getNumWires() == (channels == 1 ? 3 : 4),
                        "Explicit direct and detector channel wiring is complete");
            }
            const auto output = folder.getChildFile("graph-" + tag + "-" + scenario.name + ".wav");
            // The engine writer opens existing streams at EOF. Use the actual
            // application replacement/commit policy, so reruns cannot parse an
            // older RIFF at the front of an accidentally appended fixture.
            juce::TemporaryFile replacement(output);
            juce::WavAudioFormat format;
            te::Renderer::Parameters parameters(*edit);
            parameters.destFile = replacement.getFile();
            parameters.audioFormat = &format;
            parameters.sampleRateForAudio = rate;
            parameters.bitDepth = 24;
            parameters.blockSizeForAudio = block;
            parameters.time = {te::TimePosition::fromSeconds(0), te::TimePosition::fromSeconds(duration)};
            parameters.canRenderInMono = false;
            parameters.useMasterPlugins = true;
            parameters.ditheringEnabled = false;
            // Use the same public Edit graph builder as the normal Renderer,
            // inspect its resolved graph before playback, then render that exact
            // graph via the supported ownership-taking RenderTask overload.
            auto playHead = std::make_unique<tracktion::graph::PlayHead>();
            auto playHeadState = std::make_unique<tracktion::graph::PlayHeadState>(*playHead);
            auto processState = std::make_unique<te::ProcessState>(*playHeadState, edit->tempoSequence);
            te::CreateNodeParams graphParameters{*processState};
            graphParameters.sampleRate = rate;
            graphParameters.blockSize = block;
            graphParameters.forRendering = true;
            graphParameters.includeBypassedPlugins = false;
            graphParameters.allowClipSlots = audio.engine.getEngineBehaviour().areClipSlotsEnabled();
            auto graph =
                tracktion::graph::createNodeGraph(te::createNodeForEdit(*edit, graphParameters), false);
            const auto graphLatency = graph->rootNode->getNodeProperties().latencyNumSamples;
            if (scenario.detectorLatency)
            {
                juce::String topology;
                for (size_t i = 0; i < graph->orderedNodes.size(); ++i)
                {
                    auto *node = graph->orderedNodes[i];
                    const auto properties = node->getNodeProperties();
                    topology += juce::String(int(i)) + " " + typeid(*node).name() +
                                " latency=" + juce::String(properties.latencyNumSamples) +
                                " channels=" + juce::String(properties.numberOfChannels);
                    topology += " inputs=";
                    for (auto *input : node->getDirectInputNodes())
                        topology += juce::String(int(std::find(graph->orderedNodes.begin(),
                                                               graph->orderedNodes.end(), input) -
                                                     graph->orderedNodes.begin())) +
                                    ",";
                    topology += "\n";
                }
                require(folder.getChildFile("topology-" + tag + "-" + scenario.name + ".txt")
                            .replaceWithText(topology),
                        "Resolved graph dependencies retained");
            }
            const auto expectedLatency =
                (block + 31) * (1 + int(scenario.detectorLatency || scenario.mainLatency));
            const auto latencyNodes =
                std::count_if(graph->orderedNodes.begin(), graph->orderedNodes.end(), [](auto *node)
                              { return dynamic_cast<tracktion::graph::LatencyNode *>(node) != nullptr; });
            report += tag + " " + scenario.name + " graphLatency=" + juce::String(graphLatency) +
                      " expectedLatency=" + juce::String(expectedLatency) +
                      " delayNodes=" + juce::String(int(latencyNodes)) + "\n";
            retain();
            require(graphLatency == expectedLatency, "Resolved graph latency equals max main/detector "
                                                     "preparation plus processor, without stacked padding");
            auto graphRoot = std::move(graph->rootNode);
            graph.reset();
            std::atomic<float> progress{0};
            {
                te::Renderer::RenderTask task(
                    "Original sidechain graph risk", parameters, std::move(graphRoot), std::move(playHead),
                    std::move(playHeadState), std::move(processState), &progress, nullptr);
                task.setCancellationCheck(
                    [&]
                    {
                        return std::any_of(hosts.begin(), hosts.end(), [](const auto &worker)
                                           { return !worker->alive() || worker->deliveryFailed(); });
                    });
                while (task.runJob() != juce::ThreadPoolJob::jobHasFinished)
                {
                }
                require(task.errorMessage.isEmpty(),
                        "Actual Tracktion sidechain RenderTask: " + task.errorMessage.toStdString());
            }
            for (const auto &worker : hosts)
            {
                const auto statistics = worker->statistics();
                report += tag + " " + scenario.name +
                          " workerLatency=" + juce::String(worker->latencySamples()) +
                          " processed=" + juce::String(juce::int64(statistics.processed)) +
                          " submitted=" + juce::String(juce::int64(statistics.submitted)) + "\n";
                require(worker->alive() && !worker->deliveryFailed(),
                        "Every actual worker completes graph PCM without lost aux/audio/automation delivery");
            }
            require(!sourceCapture->overflow && !targetCapture->overflow && !targetOutput->overflow,
                    "Prepared fixed capture storage holds complete diagnostic input");
            report += tag + " " + scenario.name + " sourceInput" + sourceCapture->pulses() + "\n";
            report += tag + " " + scenario.name + " targetInput" + targetCapture->pulses() + "\n";
            report += tag + " " + scenario.name + " targetOutput" + targetOutput->pulses() + "\n";
            retain();
            require(AudioEngine::commitRenderedFile(replacement, hosts, true, {}).wasOk(),
                    "Actual shared atomic render commit replaces owned output without stale RIFF data");
            double actualRate = 0;
            auto result = readWav(output, actualRate);
            require(actualRate == rate && result.getNumChannels() == 2 &&
                        std::abs(result.getNumSamples() - int(std::lround(rate * duration))) <= 1,
                    "Actual graph WAV retains requested rate/stereo/render duration");
            return result;
        };
        const auto runCases = [&](bool unequal, bool mainLatency = false)
        {
            const auto suffix =
                unequal ? juce::String(mainLatency ? "-both-delayed" : "-unequal") : juce::String();
            Scenario mainScenario;
            mainScenario.name = "main-baseline" + suffix;
            mainScenario.amount = 0;
            mainScenario.detectorLatency = unequal;
            mainScenario.mainLatency = mainLatency;
            const auto main = render(mainScenario);
            auto detectorScenario = mainScenario;
            detectorScenario.name = "detector-baseline" + suffix;
            detectorScenario.mainClipMuted = true;
            detectorScenario.master = MasterRoute::on;
            const auto detector = render(detectorScenario);
            require(main.getMagnitude(0, main.getNumSamples()) > .02f &&
                        detector.getMagnitude(0, detector.getNumSamples()) > .02f,
                    "Independent calibrated sparse main/detector references are nonzero");
            double independentStereo = 0, overlap = 0;
            for (int n = 0; n < detector.getNumSamples(); ++n)
            {
                independentStereo = std::max(
                    independentStereo, std::abs(double(detector.getSample(0, n)) - detector.getSample(1, n)));
                overlap =
                    std::max(overlap, std::abs(double(main.getSample(0, n)) * detector.getSample(0, n)));
            }
            require(independentStereo > .02 && overlap < 1.e-7,
                    "Original source references have distinct stereo signatures and independent sparse "
                    "positions");
            struct Expectation
            {
                Scenario scenario;
                float direct = 0, auxiliary = 1;
            };
            std::vector<Expectation> cases;
            const auto add = [&](juce::String name, MasterRoute master, float direct, float auxiliary,
                                 float fader = 1, bool muted = false)
            {
                Scenario scenario;
                scenario.name = name + suffix;
                scenario.master = master;
                scenario.detectorFader = fader;
                scenario.detectorMuted = muted;
                scenario.detectorLatency = unequal;
                scenario.mainLatency = mainLatency;
                cases.push_back({scenario, direct, auxiliary});
            };
            add("master-off", MasterRoute::off, 0, 1);
            if (!unequal)
            {
                add("master-on", MasterRoute::on, 1, 1);
                add("master-quarter", MasterRoute::quarter, .25f, 1);
                add("route-only", MasterRoute::routeOnlySilentBus, 0, 1);
                add("detector-half-fader", MasterRoute::off, 0, .5f, .5f);
                add("detector-muted", MasterRoute::off, 0, 0, 1, true);
            }
            for (const auto &item : cases)
            {
                const auto actual = render(item.scenario);
                require(actual.getNumSamples() == main.getNumSamples() &&
                            actual.getNumSamples() == detector.getNumSamples(),
                        "Whole graph calibration durations match");
                double maximumError = 0;
                for (int channel = 0; channel < 2; ++channel)
                    for (int n = 0; n < actual.getNumSamples(); ++n)
                    {
                        const double expected =
                            main.getSample(channel, n) + item.direct * detector.getSample(channel, n) +
                            item.auxiliary * detector.getSample(channels == 1 ? 0 : channel, n);
                        maximumError =
                            std::max(maximumError, std::abs(double(actual.getSample(channel, n)) - expected));
                    }
                report += tag + " " + item.scenario.name +
                          " wholePCMError=" + juce::String(maximumError, 10) + "\n";
                retain();
                require(
                    maximumError < 3.e-6,
                    "Production graph equals independent main plus explicitly mapped direct/detector PCM");
            }
        };
        runCases(false);
        if (channels == 2)
        {
            runCases(true);
            runCases(true, true);
        }
    }
    report += "PASS internal graph tap/mono/stereo/PDC PCM only; "
              "project/UI/MCP/live/native/hardware/listening remain separate.\n";
    retain();
}
