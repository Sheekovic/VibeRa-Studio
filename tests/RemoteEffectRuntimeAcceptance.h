// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "RemoteEffect.h"
#include <array>
#include <cmath>
#include <limits>
// SOURCE-ONLY original proposed actual-worker qualification. Include after
// ParameterAcceptance helpers. Direct native component lifecycle + real worker
// PCM, NOT graph adoption, native OS input, hardware/listening or live timing.
__declspec(noinline) static void remoteEffectRuntimeTests(const juce::File &folder,
                                                          const juce::File &parameterBinary,
                                                          const juce::File &latencyBinary)
{
    require(folder.createDirectory().wasOk(), "owned effect runtime evidence directory");
    const auto parameter = parameterSpec(parameterBinary), latency = parameterSpec(latencyBinary);
    require(parameter.name == "VibeRa Parameter Fixture" && latency.name == "VibeRa Mixer Latency Fixture",
            "exact original real SDK fixtures required");
    AudioEngine audio(false);
    auto project = Project::empty();
    juce::String report;
    unsigned cases = 0;
    for (const double rate : {48000., 44100.})
        for (const int aux : {0, 1, 2})
            for (const bool adopt : {false, true})
            {
                constexpr int quantum = 64, graphBlock = 71, count = 384, prefix = 5;
                // Own each native cache per case, so completed fixture Hosts cannot
                // accumulate behind the cache's ordinary timer between scenarios.
                juce::String error;
                auto edit = audio.buildEdit(project, false, project.patterns.front().id,
                                            tracktion::Edit::forRendering, error);
                require(bool(edit),
                        "actual production Edit for real shell lifecycle: " + error.toStdString());
                auto spec = aux == 0 ? parameter : latency;
                auto host = std::make_shared<PluginHost>();
                const PluginBusRequest buses = aux ? PluginBusRequest{1, aux} : PluginBusRequest{};
                require(host->start(spec, rate, quantum, true, {}, buses).wasOk(),
                        "actual SDK partial-packet runtime worker");
                const auto pid = host->processId();
                const auto catalogue = parameterCatalogue(*host);
                const auto identity = host->runtimeIdentity();
                auto timeline = std::make_shared<LiveTimeline>();
                auto makeDocument = [&]
                {
                    auto document = std::make_unique<LiveDocument>();
                    document->generation = 1;
                    AutomationDocument constant;
                    if (aux == 0)
                        constant.initialValues = {
                            {{AutomationParameter::pluginNormalized, spec.id, 0}, .25f},
                            {{AutomationParameter::pluginNormalized, spec.id, 0xfedcba98u}, .125f}};
                    else
                        constant.initialValues = {
                            {{AutomationParameter::pluginNormalized, spec.id, 71}, .75f}};
                    std::string why;
                    require(AutomationProgram::compile(constant, true, document->automation, why),
                            "original constant musical target program");
                    document->nativeAutomation = {{spec.id, catalogue.generation, identity}};
                    return document;
                };
                auto document = makeDocument();
                require(timeline->publish(document), "source shell real immutable timeline published");
                // A distinct graph-local publication proves no timeline pointer is
                // required in the shared worker stream, while musical source IDs stay.
                auto nextTimeline = std::make_shared<LiveTimeline>();
                document = makeDocument();
                require(nextTimeline->publish(document),
                        "successor independent immutable timeline publication");
                auto source = edit->getPluginCache().createNewPlugin(RemoteEffect::xmlTypeName, {});
                auto *incumbent = dynamic_cast<RemoteEffect *>(source.get());
                require(incumbent != nullptr, "actual registered production RemoteEffect factory");
                incumbent->configure(host, false, timeline, spec.id);
                const tracktion::PluginInitialisationInfo info{{}, rate, graphBlock};
                source->baseClassInitialise(info);
                struct InitialisationRelease
                {
                    tracktion::Plugin::Ptr source, candidate;
                    ~InitialisationRelease()
                    {
                        if (candidate && !candidate->baseClassNeedsInitialising())
                            candidate->baseClassDeinitialise();
                        if (source && !source->baseClassNeedsInitialising())
                            source->baseClassDeinitialise();
                    }
                } release{source, {}};
                const auto owner = incumbent->runtimeIdentity();
                const auto epoch = incumbent->runtimeEpochControl();
                require(incumbent->runtimeAttachmentsControl() == 1 && owner != 0,
                        "one initialized shell, independent of shared_ptr owners");
                juce::AudioBuffer<float> buffer(2 + aux, count + 2 * prefix);
                std::array<std::array<float, count>, 2> observed{};
                const auto main = [](int c, int frame)
                { return frame < 256 ? float(((frame * (c ? 7 : 11) + 3) % 31) - 15) * .0125f : 0.f; };
                const auto detector = [](int c, int frame)
                { return frame < 256 ? float(((frame * (c ? 17 : 13) + 5) % 29) - 14) * .015625f : 0.f; };
                auto process = [&](RemoteEffect &shell, int start, int frames)
                {
                    for (int c = 0; c < buffer.getNumChannels(); ++c)
                        for (int n = 0; n < buffer.getNumSamples(); ++n)
                            buffer.setSample(c, n, -9.f);
                    for (int n = 0; n < frames; ++n)
                    {
                        for (int c = 0; c < 2; ++c)
                            buffer.setSample(c, prefix + n, main(c, start + n));
                        for (int c = 0; c < aux; ++c)
                            buffer.setSample(2 + c, prefix + n, detector(c, start + n));
                    }
                    const auto begin = tracktion::TimePosition::fromSeconds(start / rate);
                    const auto end = tracktion::TimePosition::fromSeconds((start + frames) / rate);
                    tracktion::PluginRenderContext fc(&buffer, prefix, frames, nullptr, 0, {begin, end}, true,
                                                      false, false, false);
                    shell.applyToBuffer(fc); // The ONE processing owner; never preview-render a candidate.
                    for (int c = 0; c < 2; ++c)
                    {
                        for (int n = 0; n < frames; ++n)
                            observed[c][start + n] = buffer.getSample(c, prefix + n);
                        for (int n = 0; n < prefix; ++n)
                            require(buffer.getSample(c, n) == -9.f &&
                                        buffer.getSample(c, prefix + frames + n) == -9.f,
                                    "real adapter preserves destination guards");
                    }
                };
                process(*incumbent, 0, 7);
                require(host->packetFrame() == 7,
                        "genuine worker packet is incomplete before candidate preparation");
                if (rate == 48000 && aux == 0 && !adopt)
                {
                    auto wrong = RemoteEffect::createDetachedGraph(*edit, source->state.createCopy());
                    auto *other = dynamic_cast<RemoteEffect *>(wrong.get());
                    other->configure(host, false, nextTimeline, spec.id + 1);
                    require(other->shareCompatibleRuntimeFrom(*incumbent).failed() &&
                                incumbent->runtimeAttachmentsControl() == 1 && host->packetFrame() == 7,
                            "different plugin ID refuses before changing real incumbent packet");
                    auto replacement = std::make_shared<PluginHost>();
                    require(replacement->start(spec, rate, quantum, true).wasOk() &&
                                replacement->runtimeIdentity() != identity,
                            "actual replacement worker, same descriptor and rate/quantum, distinct identity");
                    other->configure(replacement, false, nextTimeline, spec.id);
                    require(other->shareCompatibleRuntimeFrom(*incumbent).failed() &&
                                incumbent->runtimeAttachmentsControl() == 1 && host->packetFrame() == 7,
                            "matching plugin metadata is insufficient to share a replacing Host");
                    other->configure(host, false, nextTimeline, spec.id);
                    require(other->shareCompatibleRuntimeFrom(*incumbent).wasOk(),
                            "fresh candidate can retry exact compatible identity");
                    bool refused = false;
                    try
                    {
                        wrong->attachFixedGraphInitialisation({{}, rate, graphBlock + 1});
                    }
                    catch (const std::logic_error &)
                    {
                        refused = true;
                    }
                    require(refused && wrong->baseClassNeedsInitialising() &&
                                incumbent->runtimeAttachmentsControl() == 1 && host->packetFrame() == 7,
                            "incompatible native graph block fails balanced private initialization without "
                            "resetting partial incumbent packet");
                }
                auto candidate = RemoteEffect::createDetachedGraph(*edit, source->state.createCopy());
                auto *successor = dynamic_cast<RemoteEffect *>(candidate.get());
                successor->configure(host, false, nextTimeline, spec.id);
                require(successor->shareCompatibleRuntimeFrom(*incumbent).wasOk(),
                        "exact same Host/source ID runtime admission");
                candidate->attachFixedGraphInitialisation(info);
                release.candidate = candidate;
                require(successor->runtimeIdentity() == owner && successor->runtimeEpochControl() == epoch &&
                            successor->runtimeAttachmentsControl() == 2 && host->packetFrame() == 7,
                        "candidate first initialization attaches without resetting/copying partial packet");
                process(*incumbent, 7, 16); // Candidate remains private while old graph keeps rendering.
                require(host->packetFrame() == 23, "incumbent continues after real candidate preparation");
                if (!adopt)
                {
                    candidate->baseClassDeinitialise();
                    release.candidate = nullptr;
                    candidate = nullptr;
                    require(incumbent->runtimeAttachmentsControl() == 1 && host->packetFrame() == 23,
                            "discarded candidate cannot reset old partial packet");
                    process(*incumbent, 23, 13);
                }
                else
                {
                    process(*successor, 23, 13); // Sequential adoption boundary, no simultaneous renderers.
                    source->baseClassDeinitialise();
                    release.source = nullptr;
                    require(successor->runtimeAttachmentsControl() == 1 && host->packetFrame() == 36 &&
                                successor->runtimeEpochControl() == epoch,
                            "old initialized shell retirement preserves successor pending packet/epoch");
                }
                auto &active = adopt ? *successor : *incumbent;
                process(active, 36, 28);
                for (int start = 64; start < count; start += quantum)
                {
                    process(active, start, 17);
                    process(active, start + 17, quantum - 17);
                }
                double maximumError = 0;
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < count; ++n)
                    {
                        const int input = n - quantum - (aux ? 31 : 0);
                        const float expected = input < 0 ? 0.f
                                               : aux
                                                   ? main(c, input) + .75f * detector(aux == 1 ? 0 : c, input)
                                                   : main(c, input) * .25f + .125f;
                        maximumError =
                            !std::isfinite(observed[c][n]) || !std::isfinite(expected)
                                ? std::numeric_limits<double>::infinity()
                                : std::max(maximumError, std::abs(double(observed[c][n]) - expected));
                    }
                const auto deadline = juce::Time::getMillisecondCounter() + 3000;
                while (host->statistics().processed < host->statistics().submitted &&
                       juce::Time::getMillisecondCounter() < deadline)
                    juce::Thread::sleep(2);
                const auto statistics = host->statistics();
                report += "rate=" + juce::String(rate) + ";aux=" + juce::String(aux) +
                          ";adopt=" + juce::String(int(adopt)) +
                          ";maxError=" + juce::String(maximumError, 12) +
                          ";applied=" + juce::String(juce::int64(statistics.parametersApplied)) +
                          ";rejected=" + juce::String(juce::int64(statistics.parametersRejected)) + "\n";
                require(folder.getChildFile("effect-runtime-measurements.txt").replaceWithText(report),
                        "retain real measurements before independent oracles");
                require(maximumError < 1.e-6 && statistics.parametersRejected == 0 && !host->deliveryFailed(),
                        "actual real-worker signed stereo PCM matches independent analytical "
                        "source/automation/detector/latency oracle");
                require(statistics.processed == statistics.submitted &&
                            statistics.parametersApplied == (count / quantum) * (aux ? 1u : 2u),
                        "exact one constant native target per parameter per worker quantum; reset/replay "
                        "cannot hide behind identical PCM");
                require(host->processId() == pid && host->runtimeIdentity() == identity && host->alive() &&
                            active.runtimeIdentity() == owner && active.runtimeEpochControl() == epoch,
                        "same live SDK worker and nonmoving packet owner survive "
                        "preparation/discard/adoption/retirement");
                ++cases;
            }
    require(
        folder.getChildFile("effect-runtime-result.txt")
            .replaceWithText("PASS " + juce::String(cases) +
                             " actual original SDK component/worker cases; partial native automation + "
                             "mono/stereo detector "
                             "PCM, exact event counts, candidate discard, sequential shell "
                             "adoption/retirement, same instance. "
                             "Direct lifecycle/backend checks only: no actual retained graph publication, "
                             "live timing, OS input, installed provider or listening claim.\n"),
        "retain actual shell runtime result");
}
