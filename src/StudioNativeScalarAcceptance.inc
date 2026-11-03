// SPDX-License-Identifier: AGPL-3.0-or-later
// Original SDK fixture only. Actual application/MCP/Controls/native event path;
// no global input, hardware, listening or universal proprietary-state claim.
juce::Result Studio::verifyNativeScalarHistory(const juce::File &folder, const juce::File &binary)
{
    const bool originalFixture = !bool(getProperties()["viberaNativeScalarInstalledFixture"]);
    stopTimer();
    juce::String stage = "setup", trace = "callback,stage,peak,milliseconds\n";
    juce::Array<juce::var> outcomes;
    std::vector<float> pcm;
    pcm.reserve(1024 * 512);
    int callbacks = 0, overruns = 0;
    double maximumMs = 0;
    const auto retain = [&](bool passed, const juce::String &error)
    {
        auto *o = new juce::DynamicObject;
        o->setProperty("passed", passed);
        o->setProperty("stage", stage);
        o->setProperty("error", error);
        o->setProperty("callbacks", callbacks);
        o->setProperty("overruns", overruns);
        o->setProperty("maximumMs", maximumMs);
        o->setProperty("operations", outcomes);
        o->setProperty(
            "evidenceKind",
            originalFixture
                ? "Original SDK actual Studio/MCP/Controls and native parameter messages"
                : "Actual installed Analog Lab V scalar ACK/history/native restoration; finite PCM");
        o->setProperty("nativeOsInput", false);
        o->setProperty("hardware", false);
        o->setProperty("listening", false);
        o->setProperty("structuralInterruptionAllowed", true);
        const auto json = folder.getChildFile("scalar-history.json")
                              .replaceWithText(juce::JSON::toString(juce::var(o), true));
        const auto csv = folder.getChildFile("callbacks.csv").replaceWithText(trace);
        auto file = folder.getChildFile("callbacks-planar-f32.raw").createOutputStream();
        return json && csv && file && file->setPosition(0) && file->truncate().wasOk() &&
               file->write(pcm.data(), pcm.size() * sizeof(float)) &&
               (file->flush(), file->getStatus().wasOk());
    };
    const auto check = [](bool value, const char *why)
    {
        if (!value)
            throw std::runtime_error(why);
    };
    try
    {
        check(controllerTestMode && folder.createDirectory().wasOk(),
              "Owned isolated scalar history fixture");
        std::vector<juce::PluginDescription> types;
        check(PluginCatalog::scanOne(binary, types).wasOk(), "Actual supplied provider scan");
        const auto type =
            std::find_if(types.begin(), types.end(),
                         [&](const auto &p)
                         {
                             return originalFixture
                                        ? p.name == "VibeRa Parameter Fixture"
                                        : p.isInstrument && p.name.containsIgnoreCase("Analog Lab");
                         });
        check(type != types.end(), originalFixture
                                       ? "This oracle requires the genuine original ParameterFixture class"
                                       : "Installed branch requires actual Analog Lab V instrument class");
        auto project = Project::empty();
        // Literal zero-input+bias oracle requires explicit unity Mixer gains;
        // Project::empty intentionally starts strips at0.8, not unity.
        for (auto &row : project.mixer)
            row.gain = 1.f;
        project.channels.resize(1);
        project.channels.front().source = "builtin:bass";
        project.channels.front().mixer = 1;
        for (auto &pattern : project.patterns)
            pattern.notes.clear(); // Literal zero source, not observed-zero inference.
        const Id plugin = project.allocate();
        if (originalFixture)
            project.mixer[1].effects.push_back(
                {plugin, type->name, type->createXml()->toString(), {}, false, false});
        else
        {
            project.channels.front().instrument =
                PluginSpec{plugin, type->name, type->createXml()->toString(), {}, true, false};
            project.patterns.front().length = 32;
            project.patterns.front().notes.push_back(
                {project.allocate(), project.channels.front().id, 0, 32, 60, .5f, false});
            project.placements.push_back({project.allocate(), project.patterns.front().id, 0, 0, 32, 0});
        }
        controller.replace(project);
        controller.dirty = false;
        selectedPattern = project.patterns.front().id;
        selectedChannel = project.channels.front().id;
        selectedInsert = 1;
        patternMode = false;
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
        check(device && device->getCurrentSampleRate() == 48000 &&
                  device->getCurrentBufferSizeSamples() == 256,
              "Actual fixed 48k/256 provider; construct Studio(false,256)");
        juce::AudioBuffer<float> block(2, 256);
        const auto tick = [&]
        {
            const auto began = juce::Time::getHighResolutionTicks();
            device->process(block);
            const double ms =
                juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - began) *
                1000.;
            maximumMs = std::max(maximumMs, ms);
            overruns += ms > 1000. * 256 / 48000.;
            for (int c = 0; c < 2; ++c)
                for (int n = 0; n < 256; ++n)
                {
                    check(std::isfinite(block.getSample(c, n)),
                          "Every actual callback sample remains finite");
                    pcm.push_back(block.getSample(c, n));
                }
            trace += juce::String(callbacks++) + "," + stage + "," +
                     juce::String(block.getMagnitude(0, 256), 9) + "," + juce::String(ms, 6) + "\n";
            // The real-provider branch uses the established256/48k device
            // cadence, rather than asking it to render at >2x physical speed.
            // The measured outer callback budget remains exactly5.333333ms.
            dispatchMessages(originalFixture ? 2 : 6);
            timerCallback();
        };
        const auto await = [&](auto done, const char *why)
        {
            const auto began = juce::Time::getMillisecondCounter();
            while (!done() && juce::Time::getMillisecondCounter() - began < 30000)
                tick();
            check(done(), why);
        };
        juce::Result prepared = juce::Result::fail("pending");
        await(
            [&]
            {
                prepared = audio.rebuild(controller.project, false, selectedPattern);
                return !AudioEngine::preparationPending(prepared);
            },
            "Initial actual worker preparation closes");
        check(prepared.wasOk(), "Actual fixture projection prepared");
        needsRebuild = false;
        engineRevision = controller.revision;
        audio.setMetronome(false);
        audio.setCountInBars(0);
        audio.seekBeat(0);
        play.onClick();
        for (int n = 0; n < 12; ++n)
            tick();
        const auto scalar = [&](std::uint32_t parameter)
        {
            const auto host = audio.parameterHost(plugin);
            PluginParameterCatalogue catalogue;
            check(host && host->alive() && host->readParameterCatalogue(catalogue) && !catalogue.error,
                  "Actual native catalogue available");
            auto p = std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                  [&](const auto &v) { return v.id == parameter; });
            check(p != catalogue.parameters.end(), "Actual selected native scalar exists");
            return p->value;
        };
        const auto awaitScalar = [&](std::uint32_t parameter, float expected)
        {
            auto host = audio.parameterHost(plugin);
            check(host != nullptr, "Actual current Host");
            (void)host->requestParameterCatalogue();
            await([&] { return scalar(parameter) == expected; },
                  "Actual native scalar matches literal witnessed endpoint");
        };
        const auto finishPlugin = [&](const juce::String &operation)
        {
            await([&] { return !nativeScalarPending(operation); },
                  "Actual scalar ACK plus fresh history reaches terminal");
            auto *o = new juce::DynamicObject;
            o->setProperty("action", "job");
            o->setProperty("operation", operation);
            auto result = mcpCall("vibera_plugin", juce::var(o))["structuredContent"];
            outcomes.add(result);
            check(result["completionState"].toString() == "completed" && bool(result["nativeApplied"]) &&
                      bool(result["historyCommitted"]),
                  "Plugin completion requires actual native ACK and canonical scalar history");
        };
        const auto history = [&](bool undo, bool semanticRedo = false)
        {
            if (undo)
                undoButton.onClick();
            else if (originalFixture && !semanticRedo)
                redoButton.onClick();
            else
            {
                auto *args = new juce::DynamicObject;
                args->setProperty("command", "edit.redo");
                (void)mcpCall("vibera_command", juce::var(args));
            }
            check(lastMixerChainHistoryOperation.isNotEmpty(),
                  "Scalar history dispatches actual bounded native restoration");
            const auto operation = lastMixerChainHistoryOperation;
            await([&] { return mixerChainReadback(operation)["completionState"].toString() != "pending"; },
                  "Actual native history activation closes");
            auto result = mixerChainReadback(operation);
            outcomes.add(result);
            check(result["completionState"].toString() == "completed" &&
                      result["audioState"].toString() == "active",
                  "Scalar Undo/Redo waits genuine device/source activation, not document-only completion");
        };
        if (!originalFixture)
        {
            const auto steadyRecovery = [&](const char *operation)
            {
                const auto current = audio.parameterHost(plugin);
                check(current && current->alive(), "Recovered installed worker is alive");
                // Bounded pipeline fill, not a retry-until-green measurement.
                for (int n = 0; n < 8; ++n)
                    tick();
                const auto before = current->statistics();
                for (int n = 0; n < 64; ++n)
                    tick();
                const auto after = current->statistics();
                auto *facts = new juce::DynamicObject;
                facts->setProperty("recoveryAfter", operation);
                facts->setProperty("measuredCallbacks", 64);
                facts->setProperty("warmupCallbacks", 8);
                facts->setProperty("priorMissed", juce::int64(before.missed));
                facts->setProperty("priorDropped", juce::int64(before.dropped));
                facts->setProperty("additionalMissed", juce::int64(after.missed - before.missed));
                facts->setProperty("additionalDropped", juce::int64(after.dropped - before.dropped));
                facts->setProperty("midiOverflow", juce::int64(after.midiOverflow));
                facts->setProperty("parametersRejected", juce::int64(after.parametersRejected));
                facts->setProperty("transportHealthy", current->transportOutcome().succeeded());
                outcomes.add(juce::var(facts));
                check(current->alive() && current->transportOutcome().succeeded() &&
                          after.missed == before.missed && after.dropped == before.dropped &&
                          after.midiOverflow == 0 && after.parametersRejected == 0 &&
                          after.auxiliaryRejected == 0,
                      "Installed structural operation automatically recovers to steady packet delivery");
            };
            stage = "installed-supported-parameter-selection";
            auto host = audio.parameterHost(plugin);
            PluginParameterCatalogue catalogue;
            check(host && host->alive() && host->readParameterCatalogue(catalogue) && !catalogue.error,
                  "Actual installed instrument native catalogue available");
            const PluginParameter *selected = nullptr;
            for (const auto &p : catalogue.parameters)
            {
                if (!p.automatable() || (p.flags & (1u << 15)) || p.steps != 0 || !std::isfinite(p.value) ||
                    p.value < 0 || p.value > 1)
                    continue;
                if (!selected)
                    selected = &p;
                if ((p.name.containsIgnoreCase("master") || p.name.containsIgnoreCase("output")) &&
                    (p.name.containsIgnoreCase("volume") || p.name.containsIgnoreCase("gain")))
                {
                    selected = &p;
                    break;
                }
            }
            check(selected != nullptr,
                  "Actual metadata offers a supported writable continuous non-program scalar");
            const auto parameter = selected->id;
            const auto initial = selected->value;
            const float requested = initial < .5f ? .75f : .25f;
            auto *facts = new juce::DynamicObject;
            facts->setProperty("provider", type->name);
            facts->setProperty("descriptor", type->createXml()->toString());
            facts->setProperty("binaryPath", binary.getFullPathName());
            facts->setProperty("instance", host->runtimeIdentity());
            facts->setProperty("parameter", juce::int64(parameter));
            facts->setProperty("name", selected->name);
            facts->setProperty("flags", juce::int64(selected->flags));
            facts->setProperty("generation", juce::int64(catalogue.generation));
            facts->setProperty("initialNativeCatalogueValue", initial);
            facts->setProperty("requested", requested);
            facts->setProperty("sonicOracle",
                               "Finite PCM only; no proprietary sonic, listening or hardware claim");
            outcomes.add(juce::var(facts));
            check(controller.project.plugin(plugin)->automationBindings.empty(),
                  "Installed scalar starts genuinely unbound");
            const auto revision = controller.revision;
            auto *set = new juce::DynamicObject;
            set->setProperty("action", "set");
            set->setProperty("plugin", juce::int64(plugin));
            set->setProperty("instance", host->runtimeIdentity());
            set->setProperty("parameter", juce::int64(parameter));
            set->setProperty("generation", juce::int64(catalogue.generation));
            set->setProperty("value", requested);
            set->setProperty("revision", juce::int64(revision));
            stage = "installed-actual-MCP-set";
            const auto admitted = mcpCall("vibera_plugin", juce::var(set))["structuredContent"];
            const auto operation = admitted["operation"].toString();
            check(operation.isNotEmpty() && admitted["completionState"].toString() == "pending",
                  "Actual installed set owns a genuine asynchronous operation");
            finishPlugin(operation);
            auto *query = new juce::DynamicObject;
            query->setProperty("action", "job");
            query->setProperty("operation", operation);
            const auto observed = mcpCall("vibera_plugin", juce::var(query))["structuredContent"];
            check(bool(observed["beforeKnown"]) && bool(observed["nativeApplied"]) &&
                      bool(observed["historyCommitted"]),
                  "Installed native ACK and fresh captured history both complete");
            const float before = float(double(observed["before"]));
            const float after = float(double(observed["after"]));
            check(std::isfinite(before) && before >= 0 && before <= 1 && before == initial &&
                      std::isfinite(after) && after >= 0 && after <= 1 && after != before &&
                      controller.revision == revision + 1 &&
                      controller.project.plugin(plugin)->automationBindings.empty(),
                  "Actual installed acknowledged endpoints create one unbound scalar history edge");
            awaitScalar(parameter, after);
            host.reset(); // Do not pin the superseded worker during structural history.
            stage = "installed-actual-UI-Undo";
            history(true);
            awaitScalar(parameter, before);
            steadyRecovery("UI Undo");
            stage = "installed-actual-MCP-Redo";
            history(false);
            awaitScalar(parameter, after);
            steadyRecovery("MCP Redo");
            stage = "installed-fresh-save-reopen";
            const auto savedFile = folder.getChildFile("installed-native-scalar.vibra");
            check(!savedFile.exists(), "Fresh owned save output without overwrite");
            struct Saved
            {
                bool done = false;
                juce::Result result = juce::Result::fail("pending");
            };
            const auto saved = std::make_shared<Saved>();
            saveTo(savedFile,
                   [saved](juce::Result result)
                   {
                       saved->result = result;
                       saved->done = true;
                   });
            await([&] { return saved->done; }, "Actual fresh native save capture/file completion");
            check(saved->result.wasOk() && savedFile.existsAsFile(), "Actual installed native state saved");
            const auto savedState = controller.project.state();
            const auto savedIdentity = controller.projectIdentity;
            check(openProjectFile(savedFile, true).wasOk(),
                  "Actual normal Open uses saved installed native state");
            check(controller.projectIdentity != savedIdentity &&
                      controller.project.state().isEquivalentTo(savedState),
                  "Reopen preserves exact current saved Project/native state in a new document session");
            await(
                [&]
                {
                    const auto current = audio.parameterHost(plugin);
                    return current && current->alive() && !needsRebuild &&
                           engineRevision == controller.revision &&
                           audio.projectionDiagnostics().pluginSourceState == "active";
                },
                "Reopened actual installed source preparation/activation completes");
            awaitScalar(parameter, after);
            if (!audio.playing())
                play.onClick();
            steadyRecovery("saved project reopen");
            for (int n = 0; n < 32; ++n)
                tick(); // All samples were checked finite/retained by tick.
            check(retain(false, "Actual installed facts retained before strict callback deadline assertion"),
                  "Persist actual installed provider facts before assertions");
            check(overruns == 0, "Every actual installed256-frame callback retains5.333333ms deadline");
            check(retain(true, {}), "Persist actual installed scalar state/history acceptance");
            return juce::Result::ok();
        }
        stage = "first-unbound-UI-drag";
        showPluginParameters(plugin);
        auto *controls = parameterWindow
                             ? dynamic_cast<PluginControls *>(parameterWindow->getContentComponent())
                             : nullptr;
        check(controls != nullptr, "Actual shared Controls window");
        controls->stopTimer();
        await(
            [&]
            {
                controls->timerCallback();
                return controls->metadataAvailable;
            },
            "Actual Controls metadata");
        check(controls->selectNative(0), "Select true native ID zero");
        const auto beforeRevision = controller.revision;
        controls->value.onDragStart();
        controls->value.setValue(.625, juce::sendNotificationSync);
        tick();
        controls->value.setValue(.75, juce::sendNotificationSync);
        controls->value.onDragEnd();
        const auto drag = controls->scalarOperation;
        finishPlugin(drag);
        check(controller.revision == beforeRevision + 1 &&
                  controller.project.plugin(plugin)->automationBindings.empty(),
              "Entire drag creates one history action; first unbound parameter stays unbound");
        awaitScalar(0, .75f);
        history(true);
        awaitScalar(0, .5f);
        history(false);
        awaitScalar(0, .75f);
        stage = "actual-MCP-bias-and-PCM";
        auto host = audio.parameterHost(plugin);
        PluginParameterCatalogue catalogue;
        check(host->readParameterCatalogue(catalogue), "Current schema for actual set");
        auto *set = new juce::DynamicObject;
        set->setProperty("action", "set");
        set->setProperty("plugin", juce::int64(plugin));
        set->setProperty("instance", host->runtimeIdentity());
        set->setProperty("parameter", juce::int64(0xfedcba98u));
        set->setProperty("generation", juce::int64(catalogue.generation));
        set->setProperty("value", .125);
        const auto admitted = mcpCall("vibera_plugin", juce::var(set))["structuredContent"];
        const auto armRevision = controller.revision;
        const auto armProject = controller.project.state();
        const auto armDuringScalar = startAutomationRecording(1, 0);
        check(armDuringScalar.failed() &&
                  armDuringScalar.getErrorMessage().startsWith("automationRecording.busy:") &&
                  !automationRecordingActive() && nativeScalarPending(admitted["operation"].toString()) &&
                  controller.revision == armRevision && controller.project.state().isEquivalentTo(armProject),
              "Queued native scalar refuses automation Arm before competing owner or model mutation");
        finishPlugin(admitted["operation"].toString());
        awaitScalar(0xfedcba98u, .125f);
        const auto pcmLiteral = [&](float expected)
        {
            for (int i = 0; i < 32; ++i)
                tick(); // Declared warm-up, not oracle fitting.
            for (int i = 0; i < 4; ++i)
            {
                tick();
                for (int c = 0; c < 2; ++c)
                    for (int n = 0; n < 256; ++n)
                        check(std::abs(block.getSample(c, n) - expected) <= 3.e-6f,
                              "Every steady sample equals independent zero-input plus original SDK literal "
                              "bias");
            }
        };
        pcmLiteral(.125f);
        history(true);
        awaitScalar(0xfedcba98u, 0.f);
        pcmLiteral(0.f);
        history(false);
        awaitScalar(0xfedcba98u, .125f);
        pcmLiteral(.125f);
        stage = "original-native-gesture-FIFO-history";
        awaitScalar(0, .75f);
        const auto editor = [&](bool open)
        {
            const auto current = audio.parameterHost(plugin);
            check(current && current->alive(), "Actual SDK native editor Host");
            juce::String operation, code;
            // Catalogue refresh can still own the native control pipe. Retry
            // only explicit pre-admission busy, never an accepted mutation.
            await(
                [&]
                {
                    if (operation.isNotEmpty())
                        return true; // await also rechecks the predicate after success.
                    const auto admitted =
                        requestPluginEditor(plugin, open, current->runtimeIdentity(), operation, code);
                    check(admitted.wasOk() || code == "busy", "Native editor admission only retries busy");
                    return admitted.wasOk();
                },
                "Actual SDK native editor admission settles");
            check(operation.isNotEmpty(), "Actual SDK native editor operation admitted");
            await(
                [&]
                {
                    const auto job = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                                  [&](const auto &j) { return j.id == operation; });
                    return job != pluginOperations.end() && job->state != "pending";
                },
                "Actual SDK native editor ACK reaches terminal");
            auto *query = new juce::DynamicObject;
            query->setProperty("action", "job");
            query->setProperty("operation", operation);
            const auto actual = mcpCall("vibera_plugin", juce::var(query))["structuredContent"];
            outcomes.add(actual);
            check(actual["completionState"].toString() == "completed",
                  "Actual native editor open/close acknowledged");
        };
        editor(true);
        auto nativeHost = audio.parameterHost(plugin);
        check(nativeHost && nativeHost->alive(), "Actual current native gesture worker");
        HWND nativeWindow = nullptr;
        await(
            [&]
            {
                nativeWindow = test::gestureFixtureWindow(nativeHost->processId());
                return nativeWindow != nullptr;
            },
            "Actual original SDK editor message target belongs to worker PID");
        const auto previousNativeOperation = lastNativeScalarOperation;
        const auto nativeBeforeRevision = controller.revision;
        // Both genuine native begin/value/end triples arrive before any Studio
        // drain. Its fixed FIFO must preserve the first BEFORE, not just latest.
        check(test::clickGestureFixture(nativeWindow, nativeHost->processId(), 0) &&
                  test::clickGestureFixture(nativeWindow, nativeHost->processId(), 0),
              "Actual owned SDK editor emits two supported complete gestures (.75->.2->.8)");
        await([&] { return lastNativeScalarOperation != previousNativeOperation && !nativeScalarBusy(); },
              "Actual native completions reach automatic scalar history terminal");
        const auto nativeOperation = lastNativeScalarOperation;
        finishPlugin(nativeOperation);
        const auto nativeJob = std::find_if(pluginOperations.begin(), pluginOperations.end(),
                                            [&](const auto &j) { return j.id == nativeOperation; });
        check(nativeJob != pluginOperations.end() && nativeJob->action == "nativeGesture" &&
                  nativeJob->scalarBeforeKnown && nativeJob->scalarBefore == .75f &&
                  nativeJob->scalarAfter == .8f && controller.revision == nativeBeforeRevision + 1 &&
                  controller.project.plugin(plugin)->automationBindings.empty(),
              "Actual native FIFO coalesces one history edge with first BEFORE/final AFTER and no invented "
              "binding");
        awaitScalar(0, .8f);
        editor(false);      // Structural Undo requires actual native editor closure.
        nativeHost.reset(); // CONTROL releases fixture's superseded-worker pin before history.
        history(true);
        awaitScalar(0, .75f);
        history(false, true);
        awaitScalar(0, .8f);
        stage = "recording-refusal";
        const auto before = controller.project.state();
        const auto revision = controller.revision;
        recordArmed = true;
        juce::String rejected;
        const auto refused =
            requestNativeScalar(plugin, 0, catalogue.generation, .25f,
                                audio.parameterHost(plugin)->runtimeIdentity(), false, rejected);
        recordArmed = false;
        check(refused.failed() && rejected.isEmpty() && controller.revision == revision &&
                  controller.project.state().isEquivalentTo(before),
              "Armed recording refuses before any native/model/history write");
        check(retain(false, "All actual facts retained before strict callback assertion"),
              "Persist original scalar acceptance facts");
        check(overruns == 0, "Every actual256-frame callback keeps unchanged5.333333ms deadline");
        check(retain(true, {}), "Persist successful original scalar acceptance");
        return juce::Result::ok();
    }
    catch (const std::exception &e)
    {
        (void)retain(false, e.what());
        return juce::Result::fail(e.what());
    }
}
