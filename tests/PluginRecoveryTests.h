// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void pluginRecoveryTests(const juce::File &folder, const juce::File &binary)
{
    require(folder.createDirectory().wasOk(), "Owned plugin recovery evidence directory");
    auto spec = parameterSpec(binary);
    juce::String report;
    for (const bool pattern : {true, false})
    {
        // This provider really delivers the 256-frame callbacks below. Admit its
        // bound explicitly; it is not a Windows native-device proof.
        auto owner = std::make_unique<AudioEngine>(false, juce::File{}, 256);
        auto &audio = *owner;
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device != nullptr, "recovery virtual audio device");
        Controller controller;
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels[0].source = "builtin:bass";
        project.patterns[0].length = 16;
        project.patterns[0].notes.push_back(
            {project.allocate(), project.channels[0].id, 0, 16, 48, .8f, false});
        project.placements.push_back({project.allocate(), project.patterns[0].id, 0, 0, 16, 0});
        spec.id = project.allocate();
        project.mixer[0].effects.push_back(spec);
        auto healthySpec = spec;
        healthySpec.id = project.allocate();
        project.mixer[0].effects.push_back(healthySpec);
        controller.replace(project);
        const auto patternId = project.patterns[0].id;
        juce::AudioBuffer<float> block(2, 256);
        const auto publish = [&]
        {
            const auto until = juce::Time::getMillisecondCounter() + 30000;
            juce::Result result = juce::Result::ok();
            double cursor = 0;
            do
            {
                cursor = audio.positionBeat();
                result = audio.rebuild(controller.project, pattern, patternId);
                if (AudioEngine::preparationPending(result))
                {
                    device->process(block);
                    audio.reclaimPluginHosts();
                    dispatchMessages(5);
                }
            } while (AudioEngine::preparationPending(result) && juce::Time::getMillisecondCounter() < until);
            require(result.wasOk(), "recovery publication: " + result.getErrorMessage().toStdString());
            return cursor;
        };
        publish();
        auto failed = audio.parameterHost(spec.id), healthy = audio.parameterHost(healthySpec.id);
        require(failed && healthy && failed != healthy, "recovery starts independent workers");
        struct WorkerObservation
        {
            HANDLE handle;
            ~WorkerObservation()
            {
                if (handle)
                    CloseHandle(handle);
            }
        } originalWorker{
            OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, failed->processId())};
        require(originalWorker.handle && GetProcessId(originalWorker.handle) == failed->processId(),
                "retain the exact owned worker object before the deliberate failure");
        require(audio.requestPluginRecovery(controller.project, spec.id).failed() &&
                    audio.requestPluginRecovery(controller.project, 999999).failed(),
                "healthy and unknown plugins cannot be replaced by recovery");
        auto catalogue = parameterCatalogue(*failed);
        require(parameterResult(*failed, failed->setParameter(0, .625f, catalogue.generation)).result == 0,
                "recovery baseline native value applied");
        const auto serial = failed->requestState();
        juce::String captured;
        const auto captureDeadline = juce::Time::getMillisecondCounter() + 10000;
        while (!failed->readState(serial, captured) && juce::Time::getMillisecondCounter() < captureDeadline)
            dispatchMessages(5);
        require(captured.isNotEmpty() && failed->recoveryState() == captured &&
                    controller.project.plugin(spec.id)->state.isEmpty(),
                "successful capture retained separately from older canonical state");
        require(parameterResult(*failed, failed->setParameter(0, .875f, catalogue.generation)).result == 0 &&
                    failed->recoveryState() == captured,
                "uncaptured manual edits do not pretend to update recovery state");
        audio.seekBeat(1);
        audio.playStop();
        dispatchMessages(25);
        for (int i = 0; i < 8; ++i)
        {
            device->process(block);
            dispatchMessages(6);
        }
        const auto before = audio.projectionDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        const auto document = controller.project.state();
        const auto failure = failed->setParameter(0, pattern ? .27182818f : .31415927f, catalogue.generation);
        PluginParameterResult reply;
        const auto failureDeadline = juce::Time::getMillisecondCounter() + 6500;
        int callbacks = 0;
        double maximum = 0;
        while (!failed->readParameterResult(failure, reply) &&
               juce::Time::getMillisecondCounter() < failureDeadline)
        {
            const auto stamp = juce::Time::getHighResolutionTicks();
            device->process(block);
            maximum = std::max(maximum, juce::Time::highResolutionTicksToSeconds(
                                            juce::Time::getHighResolutionTicks() - stamp) *
                                            1000);
            ++callbacks;
            audio.reclaimPluginHosts();
            dispatchMessages(5);
        }
        require(failure && reply.serial == failure && !failed->alive() && healthy->alive(),
                "failure isolation leaves unrelated worker running");
        const auto verifyFailedSnapshot = [&]
        {
            require(audio.requestPluginState(controller.project).wasOk(), "failed-plugin snapshot admitted");
            Project snapshot;
            auto result = juce::Result::ok();
            bool done = false;
            const auto until = juce::Time::getMillisecondCounter() + 10000;
            while (!(done = audio.pollPluginState(snapshot, result)) &&
                   juce::Time::getMillisecondCounter() < until)
            {
                device->process(block);
                audio.reclaimPluginHosts();
                dispatchMessages(5);
            }
            require(done && result.wasOk() && snapshot.plugin(spec.id)->state == captured &&
                        controller.project.plugin(spec.id)->state.isEmpty(),
                    "failed/placeholder snapshot retains captured state without rewriting the live model");
        };
        verifyFailedSnapshot();
        unsigned expectedReplacements = 1;
        if (pattern)
        {
            struct FailedInitialization
            {
                juce::String previous =
                    juce::SystemStats::getEnvironmentVariable("VIBERA_FIXTURE_INIT_FAIL", {});
                FailedInitialization()
                {
                    require(_wputenv_s(L"VIBERA_FIXTURE_INIT_FAIL", L"1") == 0,
                            "original recovery fixture failure environment");
                }
                ~FailedInitialization()
                {
                    _wputenv_s(L"VIBERA_FIXTURE_INIT_FAIL", previous.toWideCharPointer());
                }
            } initialization;
            require(audio.requestPluginRecovery(controller.project, spec.id).wasOk(),
                    "first recovery attempt prepared independently");
            publish();
            require(!audio.parameterHost(spec.id) && audio.pluginStatus(spec.id).startsWith("Placeholder:") &&
                        audio.parameterHost(healthySpec.id) == healthy && healthy->alive(),
                    "failed replacement creates recoverable placeholder and retains healthy worker");
            verifyFailedSnapshot();
            expectedReplacements = 2;
        }
        require(audio.requestPluginRecovery(controller.project, spec.id).wasOk() &&
                    audio.requestPluginRecovery(controller.project, spec.id).failed(),
                "explicit recovery admitted once while duplicate request refused");
        const auto cursor = publish();
        auto replacement = audio.parameterHost(spec.id);
        const auto after = audio.projectionDiagnostics();
        require(replacement && replacement->alive() && replacement != failed &&
                    replacement->runtimeIdentity() != failed->runtimeIdentity() &&
                    audio.parameterHost(healthySpec.id) == healthy && healthy->alive(),
                "only failed worker replaced; healthy runtime identity retained");
        require(std::abs(nativeParameter(parameterCatalogue(*replacement), 0).value - .625f) < .00001f &&
                    replacement->recoveryState() == captured,
                "replacement restores captured native value rather than startup or uncaptured edit");
        require(after.replacements == before.replacements + expectedReplacements && audio.playing() &&
                    std::abs(audio.positionBeat() - cursor) < .05 && device->starts == starts &&
                    device->stops == stops && document.isEquivalentTo(controller.project.state()) &&
                    after.cache.preparations == before.cache.preparations,
                "explicit structural recovery retains musical cursor, device, samples and canonical project");
        float restoredPeak = 0;
        for (int i = 0; i < 12; ++i)
        {
            device->process(block);
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                for (int frame = 0; frame < block.getNumSamples(); ++frame)
                {
                    const auto sample = block.getSample(channel, frame);
                    require(std::isfinite(sample), "recovered source output remains finite");
                    restoredPeak = std::max(restoredPeak, std::abs(sample));
                }
            audio.reclaimPluginHosts();
            dispatchMessages(6);
        }
        require(restoredPeak > .00001f && audio.projectionDiagnostics().pluginSourceState == "active" &&
                    audio.parameterHost(spec.id) == replacement &&
                    audio.parameterHost(healthySpec.id) == healthy,
                "actual post-recovery callbacks close the source session and produce audio through retained "
                "workers");
        // Production has no test-local reference keeping this failed worker alive.
        // Dropping it must not turn the next UI maintenance tick into a shutdown wait.
        std::weak_ptr<PluginHost> failedLifetime = failed;
        failed.reset();
        double retirementMaximum = 0;
        const auto retire = [&]
        {
            const auto stamp = juce::Time::getMillisecondCounterHiRes();
            audio.reclaimPluginHosts();
            retirementMaximum = std::max(retirementMaximum, juce::Time::getMillisecondCounterHiRes() - stamp);
        };
        retire();
        std::cout << "Owned retirement " << (pattern ? "Pattern" : "Song")
                  << " first maintenanceMs=" << retirementMaximum << std::endl;
        require(retirementMaximum < 50,
                "orphan-worker maintenance must not wait for graceful shutdown on the control/UI thread");
        const auto retirementDeadline = juce::Time::getMillisecondCounterHiRes() + 3500;
        while (WaitForSingleObject(originalWorker.handle, 0) != WAIT_OBJECT_0 &&
               juce::Time::getMillisecondCounterHiRes() < retirementDeadline)
        {
            device->process(block);
            dispatchMessages(5);
            retire();
        }
        retire();
        DWORD failedExitCode = STILL_ACTIVE;
        require(GetExitCodeProcess(originalWorker.handle, &failedExitCode) &&
                    failedExitCode == (pattern ? 3u : 73u),
                "specific failed worker exits through owned timeout termination or deliberate fixture crash");
        require(
            WaitForSingleObject(originalWorker.handle, 0) == WAIT_OBJECT_0 && failedLifetime.expired() &&
                retirementMaximum < 50 && audio.parameterHost(spec.id) == replacement &&
                replacement->alive() && healthy->alive() && device->starts == starts &&
                device->stops == stops && audio.playing(),
            "owned retirement completes asynchronously with replacement, healthy worker and device retained");
        CommandArgs edit;
        edit.pattern = patternId;
        edit.ids = {controller.project.patterns[0].notes.front().id};
        edit.value = .45;
        require(controller.execute("notes.velocity", edit).wasOk(), "post-recovery musical edit");
        publish();
        controller.undoEdit();
        publish();
        controller.redoEdit();
        publish();
        require(audio.projectionDiagnostics().replacements == after.replacements &&
                    audio.parameterHost(spec.id) == replacement &&
                    audio.parameterHost(healthySpec.id) == healthy && device->starts == starts &&
                    device->stops == stops && audio.playing(),
                "post-recovery editing and Undo/Redo remain incremental");
        WorkerObservation healthyWorker{
            OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, healthy->processId())};
        require(healthyWorker.handle && GetProcessId(healthyWorker.handle) == healthy->processId(),
                "retain the specific healthy worker before ordinary plugin removal");
        CommandArgs remove;
        remove.target = healthySpec.id;
        require(controller.execute("plugin.remove", remove).wasOk(), "ordinary effect removal");
        publish();
        const auto removalGraph = audio.projectionDiagnostics();
        std::weak_ptr<PluginHost> healthyLifetime = healthy;
        healthy.reset();
        retire();
        const auto removalDeadline = juce::Time::getMillisecondCounterHiRes() + 3500;
        while (WaitForSingleObject(healthyWorker.handle, 0) != WAIT_OBJECT_0 &&
               juce::Time::getMillisecondCounterHiRes() < removalDeadline)
        {
            device->process(block);
            dispatchMessages(5);
            retire();
        }
        retire();
        DWORD exitCode = STILL_ACTIVE;
        require(GetExitCodeProcess(healthyWorker.handle, &exitCode) && exitCode == 0 &&
                    healthyLifetime.expired() && retirementMaximum < 50 &&
                    audio.parameterHost(spec.id) == replacement && replacement->alive() && audio.playing() &&
                    audio.projectionDiagnostics().replacements == removalGraph.replacements &&
                    device->starts == starts && device->stops == stops,
                "normal removed worker exits gracefully and is reclaimed without further graph/device "
                "changes or UI wait");
        audio.stop();
        report += juce::String(pattern ? "Pattern control timeout" : "Song control crash") +
                  ": captured0.625 restored; uncaptured0.875 discarded; healthy worker/device/sample "
                  "retained; " +
                  juce::String(expectedReplacements) + " explicit graph replacement(s); " +
                  "incremental subsequent edit/Undo/Redo PASS; post-recovery source active, peak=" +
                  juce::String(restoredPeak, 6) + ". Failure-phase " + juce::String(callbacks) +
                  " virtual callbacks, maximumMs=" + juce::String(maximum, 6) +
                  "; failed and ordinary healthy removal retirement maximum maintenanceMs=" +
                  juce::String(retirementMaximum, 6) +
                  "; structural recovery is not a voice-continuity/listening claim.\n";
    }
    require(folder.getChildFile("plugin-recovery.txt").replaceWithText(report), "plugin recovery evidence");
}
