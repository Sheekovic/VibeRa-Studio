// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginAutomationPreparation.h"
#include <juce_cryptography/juce_cryptography.h>
#include "PluginGraphRollbackTests.h"

static void nativeAutomationPreparationTests(const juce::File &folder, const juce::File &binary)
{
    auto project = Project::empty();
    project.channels.resize(1);
    auto spec = parameterSpec(binary);
    spec.id = project.allocate();
    auto host = std::make_shared<PluginHost>();
    require(host->start(spec, 48000, 8192, true).wasOk(), "native route preparation fixture worker");
    const auto metadata = parameterCatalogue(*host);
    const auto declare = [&](std::uint32_t id)
    {
        const auto p = std::find_if(metadata.parameters.begin(), metadata.parameters.end(),
                                    [&](const auto &entry) { return entry.id == id; });
        require(p != metadata.parameters.end(), "preparation uses actual native fixture metadata");
        return PluginBinding{id, p->flags, p->steps, p->value, p->name, p->label};
    };
    spec.automationBindings = {declare(0), declare(0xfedcba98u)};
    project.channels.front().instrument = spec;
    const AutomationKey gain{AutomationParameter::pluginNormalized, spec.id, 0};
    const AutomationKey bias{AutomationParameter::pluginNormalized, spec.id, 0xfedcba98u};
    std::map<Id, std::shared_ptr<PluginHost>> hosts{{spec.id, host}};
    LiveDocument document;
    document.tempo = project.tempo;
    auto compile = [&](const AutomationDocument &curves)
    {
        std::string error;
        require(AutomationProgram::compile(curves, true, document.automation, error),
                "route candidate compiled outside callback");
    };
    AutomationDocument constant;
    constant.initialValues = {{gain, .6f}, {bias, .1f}};
    compile(constant);
    require(prepareNativeAutomationRoutes(project, document, hosts, 48000).wasOk(),
            "current native bindings plus actual device workload prepare a runtime route");
    const auto *route = document.nativeRoute(spec.id);
    require(route && route->generation == metadata.generation && route->instance == host->runtimeIdentity(),
            "prepared runtime authority is tied to the exact owned native worker");
    const auto previous = document.nativeAutomation;
    const auto unchanged = [&]
    {
        require(document.nativeAutomation.size() == previous.size() &&
                    document.nativeAutomation.front().plugin == previous.front().plugin &&
                    document.nativeAutomation.front().generation == previous.front().generation &&
                    document.nativeAutomation.front().instance == previous.front().instance,
                "failed preparation preserves the complete previous route set");
    };
    auto unavailable = hosts;
    unavailable[spec.id].reset();
    require(prepareNativeAutomationRoutes(project, document, unavailable, 48000).failed(),
            "missing owned worker refuses route preparation");
    unchanged();
    LiveDocument placeholder;
    placeholder.tempo = project.tempo;
    std::string placeholderError;
    require(AutomationProgram::compile(constant, true, placeholder.automation, placeholderError),
            "missing-placeholder program");
    require(prepareNativeAutomationRoutes(project, placeholder, unavailable, 48000, true).wasOk() &&
                placeholder.nativeAutomation.empty(),
            "editing placeholder retains curves without granting authority to a missing worker");
    require(prepareNativeAutomationRoutes(project, placeholder, unavailable, 48000).failed(),
            "offline/default preparation still refuses the same missing worker");
    auto invalid = project;
    invalid.channels.front().instrument->automationBindings.front().flags ^= 1u;
    require(prepareNativeAutomationRoutes(invalid, document, hosts, 48000).failed(),
            "changed declaration cannot authorize a current native parameter");
    unchanged();
    invalid = project;
    invalid.channels.front().instrument->automationBindings.clear();
    require(prepareNativeAutomationRoutes(invalid, document, hosts, 48000).failed(),
            "undeclared program key fails before native delivery");
    unchanged();
    require(prepareNativeAutomationRoutes(project, document, hosts, 0).failed(),
            "invalid device timing preserves the prepared projection");
    unchanged();
    AutomationDocument dense;
    dense.initialValues = {{gain, .5f}, {bias, 0.f}};
    dense.sources = {{1, 4, {{2, 0, .1f}, {3, 4, .9f}}, {{gain, 0, 1}, {bias, 1, 0}}}};
    dense.placements = {{4, 1, 0, 4, 0}};
    compile(dense);
    require(prepareNativeAutomationRoutes(project, document, hosts, 48000).failed(),
            "two dense native lanes at8192 refuse before publication, without decimation");
    unchanged();
    compile(constant);
    auto multiple = project;
    auto missing = spec;
    missing.id = multiple.allocate();
    multiple.mixer.front().effects.push_back(missing);
    auto mixed = constant;
    mixed.initialValues.push_back({{AutomationParameter::pluginNormalized, missing.id, 0}, .7f});
    compile(mixed);
    require(prepareNativeAutomationRoutes(multiple, document, hosts, 48000).failed(),
            "a later unavailable target prevents publication of a partial runtime route set");
    unchanged();
    compile(constant);
    require(prepareNativeAutomationRoutes(project, document, hosts, 48000).wasOk(),
            "valid retained worker recovers route preparation without startup or transport change");
    auto stream = std::make_unique<PluginAutomationStream>();
    juce::AudioBuffer<float> audio(2, 8192);
    for (int c = 0; c < 2; ++c)
        std::fill_n(audio.getWritePointer(c), 8192, 1.f);
    const auto pid = host->processId();
    require(stream->process(*host, &document, spec.id, audio, 0, 8192, {}, 0, 48000, 120, true, false) ==
                PluginAutomationStream::Status::ready,
            "control-prepared route reaches actual native SDK processing");
    audio.clear();
    host->process(audio, 0, 8192, nullptr, 0, 8192. / 48000, 120, false, false);
    for (int c = 0; c < 2; ++c)
        for (int n = 0; n < 8192; ++n)
            require(std::abs(audio.getSample(c, n) - .7f) < .000001f,
                    "prepared gain and bias native points match independent PCM oracle");
    require(!host->deliveryFailed() && host->processId() == pid && host->alive(),
            "prepared native route preserves worker and has no delivery failure");
    const auto output = folder.getChildFile("native-delivery-guard.wav");
    const auto writeCandidate = [&](const juce::File &file, float value)
    {
        auto stream = file.createOutputStream();
        require(bool(stream), "render guard original WAV stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000., 2, 24, {}, 0));
        juce::AudioBuffer<float> pcm(2, 64);
        for (int c = 0; c < 2; ++c)
            std::fill_n(pcm.getWritePointer(c), 64, value);
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, 64),
                "render guard original WAV written");
    };
    const std::array<std::shared_ptr<PluginHost>, 1> renderHosts{host};
    {
        juce::TemporaryFile candidate(output);
        writeCandidate(candidate.getFile(), .25f);
        require(AudioEngine::commitRenderedFile(candidate, renderHosts, true, {}).wasOk(),
                "renderer commit boundary accepts healthy native delivery and a completed WAV");
    }
    const auto originalOutput = juce::MD5(output).toHexString();
    juce::TemporaryFile rejected(output);
    writeCandidate(rejected.getFile(), .75f);
    require(AudioEngine::commitRenderedFile(rejected, renderHosts, true, {}, true).failed() &&
                juce::MD5(output).toHexString() == originalOutput,
            "sampler automation DSP failure preserves previous WAV at the shared commit boundary");
    require(AudioEngine::commitRenderedFile(rejected, renderHosts, false, {}).failed() &&
                juce::MD5(output).toHexString() == originalOutput,
            "cancelled render preserves existing WAV bytes at the shared commit boundary");
    require(AudioEngine::commitRenderedFile(rejected, renderHosts, true, "fixture render error").failed() &&
                juce::MD5(output).toHexString() == originalOutput,
            "renderer task error preserves existing WAV bytes");
    require(stream->process(*host, &document, spec.id, audio, 0, 8192, {}, 0,
                            std::numeric_limits<double>::quiet_NaN(), 120, true,
                            false) == PluginAutomationStream::Status::invalidTiming &&
                host->automationStatistics().invalidTiming == 1 && host->deliveryFailed(),
            "invalid stream timing is latched for renderer failure without advancing native processing");
    require(AudioEngine::commitRenderedFile(rejected, renderHosts, true, {}).failed() &&
                juce::MD5(output).toHexString() == originalOutput,
            "actual native stream refusal prevents the renderer from replacing a prior successful WAV");
    require(folder.getChildFile("native-automation-preparation.txt")
                .replaceWithText("PASS control-thread native metadata/workload/worker-instance preparation; "
                                 "missing/changed/undeclared/timing/dense/later-target refusals preserve "
                                 "routes; recovered prepared route reaches SDK fixture gain+bias PCM .7; "
                                 "shared renderer commit accepts healthy WAV and preserves prior WAV on "
                                 "cancellation/task error/actual native-stream refusal. "
                                 "Project target activation/complete graph/UI are separate.\n"),
            "native route preparation evidence saved");
    pluginGraphRollbackTests(folder, binary);
}
