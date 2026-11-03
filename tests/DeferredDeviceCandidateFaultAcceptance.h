// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Original actual-engine fault acceptance. Include after require and
// RoutingLifetimeAcceptance.h in VibeRaTests, not in a native callback.
// Caller supplies the genuinely scanned original VibeRa Parameter Fixture spec.
// No physical device/OS input/audio continuity/listening claim.
#include "../src/ChannelRoutingCoordinator.h"
#include "SyntheticDevice.h"
#include <stdexcept>
#include <cmath>

// A new ordinary deferred consumer must not survive device appearance and
// obstruct the real device owner. No internal worker/owner injection is used.
__declspec(noinline) static void deferredNativeDeviceAppearanceTests(const juce::File &folder,
                                                                     const vibera::PluginSpec &originalEffect)
{
    using namespace vibera;
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh deferred/device transition attempt");
    Controller controller;
    auto project = Project::empty();
    project.channels.resize(1);
    project.channels.front().source = "builtin:bass";
    project.channels.front().mixer = 1;
    auto effect = originalEffect;
    effect.id = project.allocate();
    project.mixer[1].effects.push_back(effect);
    controller.replace(std::move(project));
    const auto pattern = controller.project.patterns.front().id;
    const auto channel = controller.project.channels.front().id;
    auto audio = std::make_unique<AudioEngine>(false, juce::File{}, 256);
    auto &manager = audio->engine.getDeviceManager();
    require(!manager.deviceManager.getCurrentAudioDevice(), "Ordinary transition begins with no device");
    std::vector<juce::AudioIODeviceType *> physical;
    for (auto *type : manager.deviceManager.getAvailableDeviceTypes())
        if (type->getTypeName() != "VibeRa synthetic")
            physical.push_back(type);
    for (auto *type : physical)
        manager.deviceManager.removeAudioDeviceType(type);
    (void)manager.getHostedAudioDeviceInterface();
    auto ready = audio->rebuild(controller.project, true, pattern);
    const auto initialDeadline = juce::Time::getMillisecondCounter() + 15000;
    while (AudioEngine::preparationPending(ready) && juce::Time::getMillisecondCounter() < initialDeadline)
    {
        audio->reclaimPluginHosts();
        dispatchMessages(2);
        ready = audio->rebuild(controller.project, true, pattern);
    }
    const auto source = audio->preparedPluginHost(controller.project, effect.id);
    require(ready.wasOk() && source && source->alive(),
            "Actual original SDK incumbent before deferred transition");
    const auto deferDeadline = juce::Time::getMillisecondCounter() + 3000;
    while (!audio->canDeferChannelRouting() && juce::Time::getMillisecondCounter() < deferDeadline)
    {
        audio->reclaimPluginHosts();
        dispatchMessages(2);
    }
    ChannelRoutingCoordinator coordinator(controller, *audio);
    CommandArgs route;
    route.channel = channel;
    route.mixer = 2;
    const auto routed = coordinator.assign(route, {true, pattern}, false);
    require(routed.accepted() && audio->documentProjectionDeferred() && !audio->projectionDiagnostics().edit,
            "Canonical route genuinely enters deferred no-device projection");
    // Drain only the previous ordinary source preparation before starting the
    // specific new job below. Never use the device owner's drain API for it.
    const auto previousDeadline = juce::Time::getMillisecondCounter() + 5000;
    while (!audio->deferredWorkerPreparationIdle() && juce::Time::getMillisecondCounter() < previousDeadline)
    {
        audio->reclaimPluginHosts();
        dispatchMessages(2);
    }
    require(audio->deferredWorkerPreparationIdle(), "Previous source preparation actually retired");
    CommandArgs insert;
    insert.mixer = 3;
    insert.text = originalEffect.descriptor;
    require(controller.execute("plugin.effect.insert", insert).wasOk(), "Canonical native target insertion");
    const auto added = mixerInsert(controller.project, 3)->effects.back().id;
    const auto canonical = controller.project.state().toXmlString();
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    const auto units = controller.undo.getNumberOfUnitsTakenUpByStoredCommands();
    const auto undo = controller.undo.getUndoDescriptions();
    const auto redo = controller.undo.getRedoDescriptions();
    // Exactly ONE call. Do not dispatch/wait for startup or consume its result
    // before introducing the device. Pending means owned/unconsumed; the worker
    // thread may already finish independently, which is also a required case.
    const auto pending = audio->rebuild(controller.project, true, pattern);
    const bool ordinaryPending =
        AudioEngine::preparationPending(pending) && !audio->deferredWorkerPreparationIdle();
    require(ordinaryPending && !audio->parameterHost(added) && audio->parameterHost(effect.id) == source,
            "Actual new ordinary SDK preparation is pending and unpublished");
    manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa synthetic");
    setup.setAttribute("audioOutputDeviceName", "In-memory output");
    setup.setAttribute("audioDeviceRate", 48000.);
    setup.setAttribute("audioDeviceBufferSize", 256);
    audio->engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    require(device, "Actual synthetic device appears while ordinary job remains unconsumed");
    const auto drainDeadline = juce::Time::getMillisecondCounter() + 5000;
    while (!audio->deferredWorkerPreparationIdle() && juce::Time::getMillisecondCounter() < drainDeadline)
    {
        audio->reclaimPluginHosts();
        dispatchMessages(2);
    }
    const bool drained = audio->deferredWorkerPreparationIdle();
    const auto before = audio->projectionDiagnostics();
    const bool retained =
        source->alive() && audio->parameterHost(effect.id) == source && !audio->parameterHost(added);
    const bool unchanged = controller.project.state().toXmlString() == canonical &&
                           controller.projectIdentity == identity && controller.revision == revision &&
                           controller.undo.getNumberOfUnitsTakenUpByStoredCommands() == units &&
                           controller.undo.getUndoDescriptions() == undo &&
                           controller.undo.getRedoDescriptions() == redo;
    auto facts = std::make_unique<juce::DynamicObject>();
    facts->setProperty("ordinaryPending", ordinaryPending);
    facts->setProperty("drainedByReclaim", drained);
    facts->setProperty("incumbentRetained", retained);
    facts->setProperty("canonicalAndHistoryUnchanged", unchanged);
    facts->setProperty("deferred", audio->documentProjectionDeferred());
    facts->setProperty("edit", juce::int64(before.edit));
    facts->setProperty("playbackContext", juce::int64(before.playbackContext));
    facts->setProperty("scope", "Actual SDK startup/device appearance; no physical hardware or listening");
    require(folder.getChildFile("ordinary-device-drain.json")
                .replaceWithText(juce::JSON::toString(juce::var(facts.release()))),
            "Retain actual ordinary job drain facts before assertion");
    require(drained && retained && unchanged && audio->documentProjectionDeferred() && !before.edit &&
                !before.playbackContext,
            "Lost ordinary consumer drains without stale publication or canonical/history mutation");
    // Fresh native state is acknowledged before the actual device owner may
    // replace the incumbent. The absent new slot has no earlier runtime state.
    const std::vector<Id> onlySource{effect.id};
    require(audio->requestPluginState(controller.project, &onlySource).wasOk(),
            "Capture actual incumbent for device projection");
    Project captured;
    auto captureResult = juce::Result::ok();
    bool capturedReady = false;
    const auto captureDeadline = juce::Time::getMillisecondCounter() + 12000;
    while (!(capturedReady = audio->pollPluginState(captured, captureResult)) &&
           juce::Time::getMillisecondCounter() < captureDeadline)
    {
        audio->reclaimPluginHosts();
        dispatchMessages(2);
    }
    require(capturedReady && captureResult.wasOk() &&
                audio->capturedPluginRevision(effect.id, source) == source->parameterRevision(),
            "Actual exact incumbent native state receipt");
    std::unique_ptr<PreparedControllerChange> refresh;
    require(controller.prepareNativeStateRefresh(std::move(captured), refresh).wasOk() &&
                controller.validatePreparedChange(*refresh).wasOk() &&
                controller.commitPreparedChange(*refresh),
            "Canonical no-history opaque refresh before real device owner");
    const auto refreshed = controller.project.state().toXmlString();
    const auto refreshedRevision = controller.revision;
    int admitted = 0;
    const auto admission = [&](bool candidateReady)
    {
        if (candidateReady)
            ++admitted;
        return juce::Result::ok();
    };
    auto prepared = audio->rebuildDeferredDeviceProjection(controller.project, true, pattern, admission);
    const auto deviceDeadline = juce::Time::getMillisecondCounter() + 15000;
    while (AudioEngine::preparationPending(prepared) && juce::Time::getMillisecondCounter() < deviceDeadline)
    {
        audio->reclaimPluginHosts();
        dispatchMessages(2);
        prepared = audio->rebuildDeferredDeviceProjection(controller.project, true, pattern, admission);
    }
    const auto first = audio->preparedPluginHost(controller.project, effect.id);
    const auto second = audio->preparedPluginHost(controller.project, added);
    const auto target = audio->projectionDiagnostics();
    const bool nativeReady =
        prepared.wasOk() && admitted == 1 && !audio->documentProjectionDeferred() && target.edit && first &&
        second && first != source && first != second && first->alive() && second->alive() &&
        audio->pluginHostConfigurationCurrent(*controller.project.plugin(effect.id), first, 48000., 256) &&
        audio->pluginHostConfigurationCurrent(*controller.project.plugin(added), second, 48000., 256);
    require(folder.getChildFile("device-target.json")
                .replaceWithText("{\"nativeReady\":" + juce::String(nativeReady ? "true" : "false") +
                                 ",\"finalAdmissions\":" + juce::String(admitted) + ",\"error\":" +
                                 juce::JSON::toString(juce::var(prepared.getErrorMessage())) + "}"),
            "Retain actual device preparation result before assertion");
    require(nativeReady && first->recoveryState() == controller.project.plugin(effect.id)->state &&
                controller.project.state().toXmlString() == refreshed &&
                controller.revision == refreshedRevision &&
                controller.undo.getNumberOfUnitsTakenUpByStoredCommands() == units &&
                controller.undo.getUndoDescriptions() == undo &&
                controller.undo.getRedoDescriptions() == redo,
            "Actual device owner prepares exact current native target before callbacks without new history");
    require(nativeParameter(parameterCatalogue(*second, true), 0).value == .5f,
            "Genuine original new SDK target has independently known startup parameter");
    juce::AudioBuffer<float> output(2, 256);
    bool callbackSafe = true;
    juce::String callbacks = "index,new,delete,seconds,peak\n";
    for (int n = 0; n < 8; ++n)
    {
        const auto allocations = callbackAllocations.load();
        const auto destructions = callbackDestructions.load();
        const auto began = juce::Time::getHighResolutionTicks();
        {
            struct Probe
            {
                bool prior = callbackAllocationProbe;
                Probe()
                {
                    callbackAllocationProbe = true;
                }
                ~Probe()
                {
                    callbackAllocationProbe = prior;
                }
            } probe;
            device->process(output);
        }
        const auto seconds =
            juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - began);
        bool finite = true;
        for (int c = 0; c < 2; ++c)
            for (int f = 0; f < 256; ++f)
                finite &= std::isfinite(output.getSample(c, f));
        const auto peak = output.getMagnitude(0, 256);
        const auto allocated = callbackAllocations.load() - allocations;
        const auto destroyed = callbackDestructions.load() - destructions;
        callbacks += juce::String(n) + "," + juce::String(allocated) + "," + juce::String(destroyed) + "," +
                     juce::String(seconds, 12) + "," + juce::String(peak, 9) + "\n";
        callbackSafe &= finite && peak == 0.f && allocated == 0 && destroyed == 0 && seconds <= 256. / 48000.;
        audio->reclaimPluginHosts();
        dispatchMessages(2);
    }
    require(folder.getChildFile("callbacks.csv").replaceWithText(callbacks),
            "Retain every actual device callback before assertion");
    require(callbackSafe && audio->projectionDiagnostics().pluginSourceState == "active" &&
                controller.project.state().toXmlString() == refreshed &&
                controller.revision == refreshedRevision,
            "Current target actually closes source callbacks, exact silent no-note PCM, noheap and "
            "original256-frame deadline");
}

__declspec(noinline) static void deferredDeviceCandidateFaultTests(
    const juce::File& folder, const vibera::PluginSpec& originalEffect)
{
    using namespace vibera;
    require(originalEffect.name == "VibeRa Parameter Fixture" && !originalEffect.instrument,
            "Exact original native SDK effect fixture, not a mock-host compatibility claim");
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh owned candidate-fault output");
    test::routing_lifetime::Storage storage(folder.getChildFile("storage"));
    for (const bool throwFinal : {true, false})
    {
        const auto output = folder.getChildFile(throwFinal ? "throw-final" : "refuse-final");
        require(!output.exists() && output.createDirectory().wasOk(), "Fresh candidate failure attempt");
        Controller controller;
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels.front().source = "builtin:bass";
        project.channels.front().mixer = 1;
        auto effect = originalEffect;
        effect.id = project.allocate();
        project.mixer[1].effects.push_back(effect);
        controller.replace(std::move(project));
        const auto pattern = controller.project.patterns.front().id;
        const auto channel = controller.project.channels.front().id;
        // Heap-owned to preserve default Windows test-stack budget.
        auto audio = std::make_unique<AudioEngine>(false, juce::File{}, 256);
        auto& manager = audio->engine.getDeviceManager();
        require(!manager.deviceManager.getCurrentAudioDevice(), "Fault fixture starts without a device");
        std::vector<juce::AudioIODeviceType*> physical;
        for (auto* type : manager.deviceManager.getAvailableDeviceTypes())
            if (type->getTypeName() != "VibeRa synthetic") physical.push_back(type);
        for (auto* type : physical) manager.deviceManager.removeAudioDeviceType(type);
        (void)manager.getHostedAudioDeviceInterface(); // No native MIDI devices in this fixture.
        auto ready = audio->rebuild(controller.project, true, pattern);
        const auto initialDeadline = juce::Time::getMillisecondCounter() + 15000;
        while (AudioEngine::preparationPending(ready) && juce::Time::getMillisecondCounter() < initialDeadline)
        {
            audio->reclaimPluginHosts(); dispatchMessages(2);
            ready = audio->rebuild(controller.project, true, pattern);
        }
        require(ready.wasOk(), "Actual original stopped no-device worker projection");
        const auto source = audio->preparedPluginHost(controller.project, effect.id);
        require(source && source->alive(), "Native source host exists before failure experiment");
        PluginParameterCatalogue catalogue;
        require(source->requestParameterCatalogue() != 0, "Actual original parameter metadata admission");
        const auto metadataDeadline = juce::Time::getMillisecondCounter() + 5000;
        while (!source->readParameterCatalogue(catalogue) && juce::Time::getMillisecondCounter() < metadataDeadline)
        { source->service(); dispatchMessages(2); }
        require(catalogue.generation && !catalogue.error, "Actual original controller metadata");
        const auto request = source->setParameter(0, .625f, catalogue.generation);
        PluginParameterResult changed;
        bool applied = false;
        const auto writeDeadline = juce::Time::getMillisecondCounter() + 5000;
        while (!(applied = source->readParameterResult(request, changed)) && juce::Time::getMillisecondCounter() < writeDeadline)
        { source->service(); dispatchMessages(2); }
        require(request && applied && changed.result == 0 && changed.value == .625f,
                "Genuine native state differs before deferred routing/capture");
        const auto deferDeadline = juce::Time::getMillisecondCounter() + 3000;
        while (!audio->canDeferChannelRouting() && juce::Time::getMillisecondCounter() < deferDeadline)
        { audio->reclaimPluginHosts(); dispatchMessages(2); }
        ChannelRoutingCoordinator coordinator(controller, *audio);
        CommandArgs route; route.channel = channel; route.mixer = 2;
        const auto routed = coordinator.assign(route, {true, pattern}, false);
        require(routed.accepted() && audio->documentProjectionDeferred() &&
                    !audio->projectionDiagnostics().edit, "Real offline route retires native Edit");

        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio->engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2); dispatchMessages(2);
        require(dynamic_cast<SyntheticDevice*>(manager.deviceManager.getCurrentAudioDevice()) &&
                    !source->matches(*controller.project.plugin(effect.id), 48000., 256),
                "Actual different-rate target requires a newly prepared worker");
        require(audio->requestPluginState(controller.project).wasOk(), "Shared actual capture before candidate failure");
        Project captured;
        auto captureResult = juce::Result::ok();
        bool capturedReady = false;
        const auto captureDeadline = juce::Time::getMillisecondCounter() + 12000;
        while (!(capturedReady = audio->pollPluginState(captured, captureResult)) &&
               juce::Time::getMillisecondCounter() < captureDeadline)
        { audio->reclaimPluginHosts(); dispatchMessages(2); }
        require(capturedReady && captureResult.wasOk() &&
                    audio->capturedPluginRevision(effect.id, source) == source->parameterRevision(),
                "Actual native state/revision capture succeeds without a file Save");
        std::unique_ptr<PreparedControllerChange> refresh;
        require(controller.prepareNativeStateRefresh(std::move(captured), refresh).wasOk() &&
                    controller.validatePreparedChange(*refresh).wasOk() &&
                    controller.commitPreparedChange(*refresh), "Actual no-file canonical opaque refresh");
        const auto canonical = controller.project.state().toXmlString();
        const auto revision = controller.revision;
        const auto units = controller.undo.getNumberOfUnitsTakenUpByStoredCommands();
        const auto capturedState = controller.project.plugin(effect.id)->state;
        int beforeCandidate = 0, afterCandidate = 0;
        bool genuineReplacement = false;
        const auto admission = [&](bool candidateReady) -> juce::Result
        {
            if (!candidateReady) { ++beforeCandidate; return juce::Result::ok(); }
            ++afterCandidate;
            const auto candidate = audio->parameterHost(effect.id);
            genuineReplacement = candidate && candidate != source && candidate->alive() &&
                                 audio->pluginHostConfigurationCurrent(*controller.project.plugin(effect.id),
                                                                       candidate, 48000., 256) &&
                                 candidate->recoveryState() == capturedState;
            if (throwFinal) throw std::runtime_error("Original final-admission fault AFTER real native candidate construction");
            return juce::Result::fail("device.originalFinalRefusal: Candidate deliberately refused before publication");
        };
        auto result = audio->rebuildDeferredDeviceProjection(controller.project, true, pattern, admission);
        const auto candidateDeadline = juce::Time::getMillisecondCounter() + 15000;
        while (AudioEngine::preparationPending(result) && juce::Time::getMillisecondCounter() < candidateDeadline)
        {
            audio->reclaimPluginHosts(); dispatchMessages(2);
            result = audio->rebuildDeferredDeviceProjection(controller.project, true, pattern, admission);
        }
        const auto diagnostics = audio->projectionDiagnostics();
        const bool sourceRetained = source->alive() &&
            audio->preparedPluginHost(controller.project, effect.id) == source;
        auto receipt = std::make_unique<juce::DynamicObject>();
        receipt->setProperty("throwFinal", throwFinal);
        receipt->setProperty("beforeCandidateCalls", beforeCandidate);
        receipt->setProperty("afterCandidateCalls", afterCandidate);
        receipt->setProperty("genuineReplacement", genuineReplacement);
        receipt->setProperty("result", result.getErrorMessage());
        receipt->setProperty("sourceRetained", sourceRetained);
        receipt->setProperty("edit", juce::int64(diagnostics.edit));
        receipt->setProperty("playbackContext", juce::int64(diagnostics.playbackContext));
        receipt->setProperty("deferred", audio->documentProjectionDeferred());
        receipt->setProperty("scope", "Actual original SDK worker/candidate construction; no audio callbacks, hardware, OS input or listening");
        require(output.getChildFile("candidate-failure.json").replaceWithText(juce::JSON::toString(juce::var(receipt.release()))),
                "Retain genuine fault result before musical/ownership assertions");
        require(result.failed() && !AudioEngine::preparationPending(result) && beforeCandidate > 0 &&
                    afterCandidate == 1 && genuineReplacement && sourceRetained &&
                    audio->documentProjectionDeferred() && !diagnostics.edit && !diagnostics.playbackContext &&
                    controller.project.state().toXmlString() == canonical && controller.revision == revision &&
                    controller.undo.getNumberOfUnitsTakenUpByStoredCommands() == units,
                "Both throw/refusal preserve source workers/canonical/history and retire candidate references before its Edit");
        bool drained = false;
        const auto drainDeadline = juce::Time::getMillisecondCounter() + 5000;
        while (!(drained = audio->drainDeferredWorkerPreparation(true)) && juce::Time::getMillisecondCounter() < drainDeadline)
        { audio->reclaimPluginHosts(); dispatchMessages(2); }
        require(drained && source->alive() && audio->deferredWorkerPreparationIdle(),
                "Owned completed candidate preparations drain without clearing the working source");
        auto retry = audio->rebuildDeferredDeviceProjection(controller.project, true, pattern,
            [](bool) { return juce::Result::ok(); });
        const auto retryDeadline = juce::Time::getMillisecondCounter() + 15000;
        while (AudioEngine::preparationPending(retry) && juce::Time::getMillisecondCounter() < retryDeadline)
        {
            audio->reclaimPluginHosts(); dispatchMessages(2);
            retry = audio->rebuildDeferredDeviceProjection(controller.project, true, pattern,
                [](bool) { return juce::Result::ok(); });
        }
        const auto replacement = audio->preparedPluginHost(controller.project, effect.id);
        require(retry.wasOk() && !audio->documentProjectionDeferred() && audio->projectionDiagnostics().edit &&
                    replacement && replacement != source && replacement->alive() &&
                    replacement->recoveryState() == capturedState &&
                    controller.project.state().toXmlString() == canonical && controller.revision == revision,
                "Subsequent real preparation recovers captured state without file Save or history mutation");
        if (throwFinal)
        {
            // Actual same-rate/same-buffer restart must invalidate plan identity
            // and capture NEW dirty native state, not reload the prior .625.
            const auto values = parameterCatalogue(*replacement, true);
            require(parameterResult(*replacement, replacement->setParameter(0, .375f, values.generation))
                            .result == 0,
                    "Actual second native edit before same-format restart");
            const auto oldEpoch = audio->projectionDiagnostics().pluginDeviceEpoch;
            juce::AudioBuffer<float> outputBlock(2, 256);
            auto *activeDevice =
                dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
            require(activeDevice && oldEpoch > 0, "Actual prepared source before restart");
            for (int n = 0; n < 8; ++n)
            {
                activeDevice->process(outputBlock);
                replacement->service();
                dispatchMessages(2);
            }
            require(audio->projectionDiagnostics().pluginSourceState == "active",
                    "Actual source end, not arm acknowledgement");
            manager.deviceManager.closeAudioDevice();
            manager.deviceManager.restartLastAudioDevice();
            dispatchMessages(20);
            juce::AudioDeviceManager::PhysicalCallbackSnapshot restarted;
            require(manager.deviceManager.readPhysicalCallbackSnapshot(restarted) && restarted.active &&
                        restarted.deviceEpoch != oldEpoch && manager.getSampleRate() == 48000. &&
                        manager.getBlockSize() == 256 && audio->deviceRestorationRequired(),
                    "Real same-format epoch change refuses obsolete native graph");
            const auto oldEdit = audio->projectionDiagnostics().edit;
            const auto refused = audio->prepareDeviceRestoration(controller.project);
            require(refused.failed() && !AudioEngine::preparationPending(refused) &&
                        audio->projectionDiagnostics().edit == oldEdit && replacement->alive(),
                    "Uncaptured second native edit cannot retire old owner");
            require(audio->requestPluginState(controller.project).wasOk(),
                    "Actual restart-state capture admission");
            Project restartState;
            auto stateResult = juce::Result::ok();
            bool stateReady = false;
            const auto stateDeadline = juce::Time::getMillisecondCounter() + 12000;
            while (!(stateReady = audio->pollPluginState(restartState, stateResult)) &&
                   juce::Time::getMillisecondCounter() < stateDeadline)
            {
                audio->reclaimPluginHosts();
                dispatchMessages(2);
            }
            require(stateReady && stateResult.wasOk(), "Actual restarted dirty state captured");
            std::unique_ptr<PreparedControllerChange> restartRefresh;
            require(controller.prepareNativeStateRefresh(std::move(restartState), restartRefresh).wasOk() &&
                        controller.validatePreparedChange(*restartRefresh).wasOk() &&
                        controller.commitPreparedChange(*restartRefresh),
                    "Canonical restart-state refresh");
            const auto newestState = controller.project.plugin(effect.id)->state;
            require(newestState != capturedState,
                    "Independent newest native state differs from stale snapshot");
            auto nativeReady = audio->prepareDeviceRestoration(controller.project);
            require(AudioEngine::preparationPending(nativeReady) && !audio->projectionDiagnostics().edit &&
                        audio->documentProjectionDeferred() && replacement->alive(),
                    "Retire old context before pending native retry; retain worker/state");
            const auto nativeDeadline = juce::Time::getMillisecondCounter() + 5000;
            while (AudioEngine::preparationPending(nativeReady) &&
                   juce::Time::getMillisecondCounter() < nativeDeadline)
            {
                dispatchMessages(2);
                nativeReady = audio->prepareDeviceRestoration(controller.project);
            }
            require(nativeReady.wasOk() && !audio->projectionDiagnostics().edit &&
                        manager.getPreparationFenceStatus() ==
                            te::DeviceManager::PreparationFenceStatus::passed,
                    "Actual native preparation passes with no new app context");
            auto rebuilt = audio->rebuildDeferredDeviceProjection(controller.project, true, pattern,
                                                                  [](bool) { return juce::Result::ok(); });
            const auto rebuiltDeadline = juce::Time::getMillisecondCounter() + 15000;
            while (AudioEngine::preparationPending(rebuilt) &&
                   juce::Time::getMillisecondCounter() < rebuiltDeadline)
            {
                audio->reclaimPluginHosts();
                dispatchMessages(2);
                rebuilt = audio->rebuildDeferredDeviceProjection(controller.project, true, pattern,
                                                                 [](bool) { return juce::Result::ok(); });
            }
            const auto restored = audio->preparedPluginHost(controller.project, effect.id);
            auto restartFacts = std::make_unique<juce::DynamicObject>();
            restartFacts->setProperty("result", rebuilt.getErrorMessage());
            restartFacts->setProperty("restoredExists", bool(restored));
            restartFacts->setProperty("differentHost", restored != replacement);
            restartFacts->setProperty("stateMatches", restored && restored->recoveryState() == newestState);
            restartFacts->setProperty("oldEpoch", juce::int64(oldEpoch));
            restartFacts->setProperty("newEpoch", juce::int64(restarted.deviceEpoch));
            restartFacts->setProperty("projectedEpoch",
                                      juce::int64(audio->projectionDiagnostics().pluginDeviceEpoch));
            if (restored)
                restartFacts->setProperty("nativeValue",
                                          nativeParameter(parameterCatalogue(*restored, true), 0).value);
            require(output.getChildFile("same-format-restart-diagnostic.json")
                        .replaceWithText(juce::JSON::toString(juce::var(restartFacts.release()))),
                    "Retain restart preparation and actual native readback before assertion");
            require(rebuilt.wasOk() && restored && restored != replacement && restored->alive() &&
                        restored->recoveryState() == newestState &&
                        nativeParameter(parameterCatalogue(*restored, true), 0).value == .375f &&
                        audio->projectionDiagnostics().pluginDeviceEpoch == restarted.deviceEpoch,
                    "Fresh exact-epoch worker restores actual newest .375 native value");
            audio->reclaimPluginHosts(); // Actual timer path must not restart the newly armed graph.
            activeDevice = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
            for (int n = 0; n < 8; ++n)
            {
                activeDevice->process(outputBlock);
                restored->service();
                dispatchMessages(2);
            }
            require(audio->projectionDiagnostics().pluginSourceState == "active" &&
                        controller.revision == revision &&
                        controller.undo.getNumberOfUnitsTakenUpByStoredCommands() == units,
                    "Restored context actually closes callbacks without musical history mutation");
            require(output.getChildFile("same-format-restart.json")
                        .replaceWithText(
                            "{\"passed\":true,\"oldEpoch\":" + juce::String(juce::int64(oldEpoch)) +
                            ",\"newEpoch\":" + juce::String(juce::int64(restarted.deviceEpoch)) +
                            ",\"actualNativeValue\":0.375,\"physicalHardware\":false,\"listening\":false}"),
                    "Retain actual same-format restart result");
            // An actual CONTROL observer can throw after old-Edit retirement.
            // This must return a committed/stopped failure, then reuse healthy
            // workers through explicit source-only recovery without state loss.
            auto expanded = controller.project;
            auto extra = expanded.channels.front();
            extra.id = expanded.allocate();
            extra.name = "Recovery source";
            expanded.channels.push_back(extra);
            const auto latestCatalogue = parameterCatalogue(*restored, true);
            require(parameterResult(*restored, restored->setParameter(0, .8125f, latestCatalogue.generation))
                            .result == 0,
                    "Uncaptured warm native value before CONTROL failure");
            audio->midiInput.setHistoryTarget(channel);
            audio->midiInput.historyInterrupted = [](Id, double)
            { throw std::runtime_error("Original postcommit CONTROL observer failure"); };
            const auto incumbent = audio->projectionDiagnostics().edit;
            const auto starts = activeDevice->starts, stops = activeDevice->stops;
            auto activation = audio->rebuild(expanded, true, pattern);
            const auto activationDeadline = juce::Time::getMillisecondCounter() + 15000;
            while (AudioEngine::preparationPending(activation) &&
                   juce::Time::getMillisecondCounter() < activationDeadline)
            {
                audio->reclaimPluginHosts();
                dispatchMessages(2);
                activation = audio->rebuild(expanded, true, pattern);
            }
            audio->midiInput.historyInterrupted = {};
            auto activationFacts = std::make_unique<juce::DynamicObject>();
            activationFacts->setProperty("result", activation.getErrorMessage());
            activationFacts->setProperty("editExists", bool(audio->projectionDiagnostics().edit));
            activationFacts->setProperty("differentEdit", audio->projectionDiagnostics().edit != incumbent);
            activationFacts->setProperty("playing", audio->playing());
            activationFacts->setProperty("restorationRequired", audio->deviceRestorationRequired());
            activationFacts->setProperty("workerAlive", restored->alive());
            require(output.getChildFile("source-only-activation-diagnostic.json")
                        .replaceWithText(juce::JSON::toString(juce::var(activationFacts.release()))),
                    "Retain actual postcommit activation result before assertion");
            require(
                activation.failed() && activation.getErrorMessage().startsWith("source.activationFailed:") &&
                    audio->projectionDiagnostics().edit && audio->projectionDiagnostics().edit != incumbent &&
                    !audio->playing() && audio->deviceRestorationRequired() && restored->alive(),
                "Actual postcommit exception reports stopped committed replacement and explicit recovery");
            const auto sourceReady = audio->prepareDeviceRestoration(expanded);
            require(sourceReady.wasOk() && audio->documentProjectionDeferred() &&
                        !audio->projectionDiagnostics().edit &&
                        manager.getPreparationFenceStatus() ==
                            te::DeviceManager::PreparationFenceStatus::passed,
                    ("Source-only recovery retires context without native device retry or capture/reload: " +
                     sourceReady.getErrorMessage())
                        .toStdString());
            auto warm = audio->rebuildDeferredDeviceProjection(expanded, true, pattern,
                                                               [](bool) { return juce::Result::ok(); });
            const auto warmDeadline = juce::Time::getMillisecondCounter() + 15000;
            while (AudioEngine::preparationPending(warm) &&
                   juce::Time::getMillisecondCounter() < warmDeadline)
            {
                audio->reclaimPluginHosts();
                dispatchMessages(2);
                warm = audio->rebuildDeferredDeviceProjection(expanded, true, pattern,
                                                              [](bool) { return juce::Result::ok(); });
            }
            require(warm.wasOk() && audio->preparedPluginHost(expanded, effect.id) == restored &&
                        nativeParameter(parameterCatalogue(*restored, true), 0).value == .8125f,
                    "Warm source recovery preserves exact worker and newer uncaptured native state");
            for (int n = 0; n < 8; ++n)
            {
                activeDevice->process(outputBlock);
                audio->reclaimPluginHosts();
                dispatchMessages(2);
            }
            require(audio->projectionDiagnostics().pluginSourceState == "active" &&
                        activeDevice->starts == starts && activeDevice->stops == stops &&
                        controller.revision == revision &&
                        controller.undo.getNumberOfUnitsTakenUpByStoredCommands() == units,
                    "Source-only recovery actually closes callbacks without device or canonical history "
                    "mutation");
            require(
                output.getChildFile("source-only-recovery.json")
                    .replaceWithText("{\"passed\":true,\"actualNativeValue\":0.8125,\"sameWorker\":true,"
                                     "\"deviceReset\":false,\"physicalHardware\":false,\"listening\":false}"),
                "Retain actual source-only recovery and postcommit exception result");
        }
    }
    deferredNativeDeviceAppearanceTests(folder.getChildFile("ordinary-device-appearance"), originalEffect);
}
