// SPDX-License-Identifier: AGPL-3.0-or-later
// Include INSIDE namespace vibera at end of StudioPluginParameters.cpp, after
// PluginControls/PluginControlWindow definitions. Root owns actual integration.
juce::Result Studio::verifyMixerEffectOpening(const juce::File &folder, const juce::File &binary,
                                              bool originalFixture, bool expectNoEditor)
{
    using namespace test::fx_opening;
    if (!controllerTestMode || !pluginTestSettings || !workflowTestSettings ||
        !bool(getProperties()["viberaMixerFxOpeningFixture"]) ||
        getProperties()["viberaMixerFxOpeningOutput"].toString() != folder.getFullPathName())
        return juce::Result::fail("Effect opening requires an explicitly owned disposable Studio(false)");
    if (expectNoEditor && !originalFixture)
        return juce::Result::fail("No-editor mode is restricted to the original SDK fixture");
    if (midiTaking || recordArmed || hasPendingMidiTake() || afterPluginState || pendingExport ||
        audio.rendering() || juce::Component::getCurrentlyModalComponent())
        return juce::Result::fail("Effect opening requires idle capture/export/modal ownership");
    const auto receiptFile = folder.getChildFile("effect-slot-opening.json");
    if (receiptFile.exists())
        return juce::Result::fail("Use a fresh owned output; retain prior effect opening evidence");
    if (auto ready = folder.createDirectory(); ready.failed())
        return ready;
    const bool timerWasRunning = isTimerRunning();
    stopTimer();
    struct TimerRestore
    {
        Studio &studio;
        bool running;
        ~TimerRestore()
        {
            if (running)
                studio.startTimerHz(20);
        }
    } timerRestore{*this, timerWasRunning};
    struct FixtureEditorMode
    {
        bool original, configured = false;
        juce::String previous =
            juce::SystemStats::getEnvironmentVariable("VIBERA_FIXTURE_EDITOR_UNSUPPORTED", {});
        FixtureEditorMode(bool fixture, bool unsupported) : original(fixture)
        {
            configured =
                !original || _wputenv_s(L"VIBERA_FIXTURE_EDITOR_UNSUPPORTED", unsupported ? L"1" : L"") == 0;
        }
        ~FixtureEditorMode()
        {
            if (original)
                _wputenv_s(L"VIBERA_FIXTURE_EDITOR_UNSUPPORTED", previous.toWideCharPointer());
        }
    } editorMode(originalFixture, expectNoEditor);
    auto check = [](bool ok, const juce::String &why)
    {
        if (!ok)
            throw std::runtime_error(why.toStdString());
    };
    juce::String phase = "setup", audioSummary, failure, providerName, providerDescriptor;
    juce::Array<juce::var> outcomes;
    juce::File modeFolder;
    std::unique_ptr<Capture> capture;
    std::shared_ptr<PluginHost> activeHost;
    std::uint64_t baselineMissed = 0, baselineDropped = 0;
    juce::Component::SafePointer<juce::Component> ownedMenu;
    bool audioPassed = true;
    int audioVerifiedModes = 0, capturedCallbacks = 0;
    auto persist = [&](bool passed)
    {
        auto *out = new juce::DynamicObject;
        out->setProperty("passed", passed);
        out->setProperty("phase", phase);
        out->setProperty("error", failure);
        out->setProperty("provider", binary.getFullPathName());
        out->setProperty("providerName", providerName);
        out->setProperty("providerDescriptor", providerDescriptor);
        out->setProperty("originalSdkFixture", originalFixture);
        out->setProperty("expectedNoEditor", expectNoEditor);
        out->setProperty("nativeEditorCompatibilityPassed", passed && !expectNoEditor);
        out->setProperty("audioPassed", audioPassed && audioVerifiedModes == 2);
        out->setProperty("audioVerifiedModes", audioVerifiedModes);
        out->setProperty("componentInteraction", true);
        out->setProperty("contextMenuInteraction", originalFixture);
        out->setProperty("installedProviderCloseAndControlsEntry",
                         originalFixture ? "notApplicable" : "semanticMcp");
        out->setProperty("nativeOsInput", false);
        out->setProperty("listeningOrHardware", false);
        out->setProperty("callbackHeapInstrumentation", false);
        out->setProperty("outcomes", outcomes);
        return receiptFile.replaceWithText(juce::JSON::toString(juce::var(out), true));
    };
    auto retainCapture = [&]
    {
        if (!capture)
            return;
        capture->stop();
        capturedCallbacks = capture->complete.load(std::memory_order_acquire);
        const bool valid =
            capture->evidence(modeFolder, originalFixture, baselineMissed, baselineDropped, audioSummary);
        audioPassed = audioPassed && valid;
        if (valid)
            ++audioVerifiedModes;
        capture.reset();
    };
    try
    {
        check(editorMode.configured, "Cannot configure own original fixture environment");
        check(binary.exists(), "provider.unavailable: Explicit VST3 bundle does not exist");
        std::vector<juce::PluginDescription> descriptions;
        const auto scanned = PluginCatalog::scanOne(binary, descriptions);
        check(scanned.wasOk(), "provider.scan_failed: " + scanned.getErrorMessage());
        const auto selected = std::find_if(descriptions.begin(), descriptions.end(), [](const auto &d)
                                           { return !d.isInstrument && d.pluginFormatName == "VST3"; });
        check(selected != descriptions.end(), "provider.unsupported: No Windows x64 VST3 effect descriptor");
        providerName = selected->name;
        providerDescriptor = selected->createXml()->toString();
        check(!originalFixture || selected->name == "VibeRa Parameter Fixture",
              "fixture.invalid: Exact original ParameterFixture required for independent DSP oracle");
        const auto sample = folder.getChildFile("original-constant-stereo.wav");
        check(!sample.exists(), "Preserve original input fixture");
        {
            juce::AudioBuffer<float> pcm(2, 4096);
            for (int c = 0; c < 2; ++c)
                std::fill_n(pcm.getWritePointer(c), pcm.getNumSamples(), c == 0 ? .125f : .25f);
            auto stream = sample.createOutputStream();
            check(bool(stream), "Create original sample stream");
            juce::WavAudioFormat format;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                format.createWriterFor(stream.release(), 48000, 2, 32, {}, 0));
            check(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
                  "Write original exact source");
        }
        auto project = Project::empty();
        project.channels.resize(1);
        auto &channel = project.channels.front();
        channel.source = sample.getFullPathName();
        channel.mixer = 1;
        channel.gain = 1;
        channel.pan = 0;
        channel.sampler.loop = true;
        channel.sampler.envelope = true;
        channel.sampler.attack = 0;
        channel.sampler.decay = 0;
        channel.sampler.sustain = 1;
        for (auto &insert : project.mixer)
        {
            insert.gain = 1;
            insert.pan = 0;
            insert.effects.clear();
        }
        const auto pluginId = project.allocate();
        project.mixer[1].effects.push_back(
            {pluginId, selected->name, selected->createXml()->toString(), {}, false, false});
        auto &pattern = project.patterns.front();
        pattern.length = 256;
        pattern.notes.clear();
        pattern.notes.push_back({project.allocate(), channel.id, 0, 256, 60, 1.f, false});
        project.placements.clear();
        project.placements.push_back({project.allocate(), pattern.id, 0, 0, 256, 0});
        check(project.validate().wasOk(), "Original long unity sample and ordered actual effect model");
        controller.replace(project);
        selectedPattern = pattern.id;
        selectedChannel = channel.id;
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        check(device && te::getDefaultPanLaw() == te::PanLawLinear,
              "Exact virtual device and linear-pan prerequisite");
        for (bool usePattern : {true, false})
        {
            phase = usePattern ? "Pattern" : "Song";
            modeFolder = folder.getChildFile(phase);
            check(!modeFolder.exists() && modeFolder.createDirectory().wasOk(), "Fresh per-mode evidence");
            patternMode = usePattern;
            auto ready = juce::Result::fail("pending");
            const auto began = juce::Time::getMillisecondCounter();
            do
            {
                ready = audio.rebuild(controller.project, usePattern, selectedPattern);
                if (ready.failed() && !AudioEngine::preparationPending(ready))
                    break;
                if (ready.failed())
                    dispatchMessages(5);
            } while (ready.failed() && juce::Time::getMillisecondCounter() - began < 30000);
            check(ready.wasOk(), "Actual prepared effect projection: " + ready.getErrorMessage());
            needsRebuild = false;
            projectionError.clear();
            engineRevision = controller.revision;
            check(selectMixerInsert(1).wasOk(), "Select actual populated Mixer insert");
            if (maximisedEditor != 3)
                maximiseEditor(3);
            resized();
            auto *editor = dynamic_cast<Editor *>(windows[3]->getContentComponent());
            check(editor && !editor->effectSlotsLayout().name(0).isEmpty(),
                  "Real visible effect-name target");
            activeHost = audio.preparedPluginHost(controller.project, pluginId);
            check(activeHost && activeHost->alive(), "Actual effect worker must be ready");
            const auto instance = activeHost->runtimeIdentity();
            const auto pid = activeHost->processId();
            const auto nativeState = [&]
            {
                // Setup/after-capture only: never pause a worker to sample state
                // inside the measured live opening interval.
                unsigned serial = 0;
                const auto began = juce::Time::getMillisecondCounter();
                while (!(serial = activeHost->requestState()) && activeHost->alive() &&
                       juce::Time::getMillisecondCounter() - began < 2000)
                    dispatchMessages(2);
                check(serial != 0, "Actual native state capture must be admitted");
                juce::String bytes;
                bool done = false;
                while (!(done = activeHost->readState(serial, bytes)) && activeHost->alive() &&
                       juce::Time::getMillisecondCounter() - began < 2000)
                    dispatchMessages(2);
                check(done, "Actual native provider state capture completed (empty state may be valid)");
                check(!originalFixture || bytes.isNotEmpty(),
                      "Original SDK provider has a real nonempty state");
                return bytes;
            };
            const auto stateBefore = nativeState();
            check(pid != 0 && instance.isNotEmpty(), "Owned worker PID and instance identity required");
            audio.seekBeat(0);
            audio.playStop();
            juce::AudioBuffer<float> warm(2, 256);
            // Explicit setup warmup only: fill the worker pipeline and ordinary
            // gain smoothers BEFORE testing any editor interaction. No later exclusions.
            for (int i = 0; i < 32; ++i)
            {
                device->process(warm);
                dispatchMessages(6);
            }
            const auto before = audio.projectionDiagnostics();
            const auto sourceState = controller.project.state().toXmlString();
            const auto identity = controller.projectIdentity;
            const auto revision = controller.revision;
            const bool dirty = controller.dirty;
            const auto undo = controller.undo.getUndoDescriptions(),
                       redo = controller.undo.getRedoDescriptions();
            const int units = controller.undo.getNumberOfUnitsTakenUpByStoredCommands();
            const int starts = device->starts, stops = device->stops;
            const auto stats = activeHost->statistics();
            baselineMissed = stats.missed;
            baselineDropped = stats.dropped;
            capture = std::make_unique<Capture>(*device, activeHost);
            capture->start();
            auto click = [&](juce::Point<float> point, bool right)
            {
                auto now = juce::Time::getCurrentTime();
                juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), point,
                                       juce::ModifierKeys(right ? juce::ModifierKeys::rightButtonModifier
                                                                : juce::ModifierKeys::leftButtonModifier),
                                       1, 0, 0, 0, 0, editor, editor, now, point, now, 1, false);
                editor->mouseDown(event);
                editor->mouseUp(event);
            };
            auto waitJob = [&](std::size_t first, const char *action) -> PluginEditorResult
            {
                const auto started = juce::Time::getMillisecondCounter();
                for (;;)
                {
                    dispatchMessages(2);
                    pollPluginOperations();
                    for (std::size_t i = first; i < pluginOperations.size(); ++i)
                    {
                        const auto &job = pluginOperations[i];
                        if (job.kind != "editor" || job.plugin != pluginId || job.action != action)
                            continue;
                        check(job.project == identity && job.instance == instance && job.serial != 0,
                              "Actual native request ownership matches name target");
                        if (job.state == "pending")
                            break;
                        check(bool(job.editorResult),
                              "Terminal application editor job must retain native result");
                        auto *row = new juce::DynamicObject;
                        row->setProperty("mode", phase);
                        row->setProperty("action", action);
                        row->setProperty("state", job.state);
                        row->setProperty("outcome", pluginEditorOutcomeName(job.editorResult->outcome));
                        row->setProperty("completionKnown", job.editorResult->completionKnown);
                        row->setProperty("serial", juce::int64(job.serial));
                        row->setProperty("instance", instance);
                        outcomes.add(juce::var(row));
                        check(persist(false), "Retain native outcome before assertion");
                        // UI-created operations are discoverable through the same real MCP job service.
                        auto *query = new juce::DynamicObject;
                        query->setProperty("action", "job");
                        query->setProperty("operation", job.id);
                        const auto semantic = mcpCall("vibera_plugin", juce::var(query))["structuredContent"];
                        check(
                            semantic["completionState"].toString() == job.state &&
                                bool(semantic["nativeCompletionKnown"]) == job.editorResult->completionKnown,
                            "Semantic job reports actual UI request outcome, never acceptance as completion");
                        return *job.editorResult;
                    }
                    check(activeHost->alive() && !capture->exhausted && !capture->threw,
                          "Worker/callback driver must stay alive while opening editor");
                    check(juce::Time::getMillisecondCounter() - started <= pluginEditorTimeoutMs + 2000,
                          "Native editor receipt deadline elapsed; acceptance is not completion");
                }
            };
            const auto openAt = pluginOperations.size();
            capture->phase = 1;
            click(editor->effectSlotsLayout().name(0).getCentre().toFloat(), false);
            auto opened = waitJob(openAt, "open");
            HWND firstNativeWindow = nullptr;
            if (expectNoEditor)
            {
                check(opened.outcome == PluginEditorOutcome::unsupported && opened.completionKnown &&
                          opened.window.known && !opened.window.exists &&
                          opened.window.availability == PluginEditorAvailability::unsupported &&
                          ownedWindows(pid, true).count == 0 && !ownedWindows(pid, true).originalChild,
                      "Original no-editor provider returns truthful unsupported outcome and no wrapper");
            }
            else
            {
                check(opened.succeeded() && opened.completionKnown && opened.window.visible,
                      "provider.editor_unavailable: " +
                          juce::String(pluginEditorOutcomeName(opened.outcome)));
                const auto first = ownedWindows(pid, originalFixture);
                check(first.count == 1 && !first.overflow && (!originalFixture || first.originalChild),
                      "Actual owned native editor exists, including original SDK child when applicable");
                firstNativeWindow = first.peers[0];
                capture->phase = 2;
                const auto repeatAt = pluginOperations.size();
                click(editor->effectSlotsLayout().name(0).getCentre().toFloat(), false);
                const auto repeated = waitJob(repeatAt, "open");
                const auto repeatedWindows = ownedWindows(pid, originalFixture);
                check(repeated.outcome == PluginEditorOutcome::broughtToFront && repeated.completionKnown &&
                          repeatedWindows.count == 1 && repeatedWindows.peers[0] == first.peers[0] &&
                          repeatedWindows.originalChild == first.originalChild,
                      "Second slot-name click reuses acknowledged native window and worker instance");
            }
            auto menuAction = [&](const juce::String &label)
            {
                check(!juce::Component::getCurrentlyModalComponent(), "Own fresh effect menu only");
                DWORD foregroundOwner = 0;
                GetWindowThreadProcessId(GetForegroundWindow(), &foregroundOwner);
                if (foregroundOwner == pid)
                {
                    // The real native editor legitimately owns foreground.
                    // Return only from this exact owned worker, without global
                    // pointer/key injection or stealing an unrelated app.
                    getTopLevelComponent()->toFront(true);
                    editor->grabKeyboardFocus();
                    dispatchMessages(5);
                }
                const auto menuPoint = editor->effectSlotsLayout().menu(0).getCentre();
                const auto *menuInsert = mixerInsert(controller.project, selectedInsert);
                const auto hit = editor->effectSlotsLayout().hit(
                    menuPoint, editor->effectSlotsOffset(), menuInsert ? int(menuInsert->effects.size()) : 0);
                click(menuPoint.toFloat(), false);
                // JUCE enters this actual popup's modal state synchronously.
                // Observe/select before pumping unrelated native focus/timer events;
                // this is component dispatch, not an OS-input/foreground claim.
                auto *menu = juce::Component::getCurrentlyModalComponent();
                ownedMenu = menu;
                DWORD observedForeground = 0;
                GetWindowThreadProcessId(GetForegroundWindow(), &observedForeground);
                auto *observation = new juce::DynamicObject;
                observation->setProperty("mode", phase);
                observation->setProperty("contextItem", label);
                observation->setProperty("componentDispatch", true);
                observation->setProperty("hitAction", int(hit.action));
                observation->setProperty("hitSlot", hit.slot);
                observation->setProperty("immediateModal", menu != nullptr);
                observation->setProperty("immediateModalName", menu ? menu->getName() : juce::String{});
                observation->setProperty("foregroundPid", juce::int64(observedForeground));
                outcomes.add(juce::var(observation));
                check(hit.action == MixerEffectSlots::Action::menu && hit.slot == 0,
                      "Actual slot menu hit resolves exact first effect");
                check(menu && menu->getName() == "menu", "Actual slot context menu is modal and owned");
                check(pressMenuItem(*menu, label), "Actual context item missing: " + label);
                dispatchMessages(2);
                dispatchMessages(5);
                ownedMenu = nullptr;
            };
            auto sharedSemanticAction = [&](const char *action)
            {
                auto arguments = std::make_unique<juce::DynamicObject>();
                arguments->setProperty("action", action);
                arguments->setProperty("plugin", juce::int64(pluginId));
                arguments->setProperty("instance", instance);
                // Actual semantic application service; no synthetic native
                // widget access or claim that this tests the installed menu.
                (void)mcpCall("vibera_plugin", juce::var(arguments.release()));
            };
            if (!expectNoEditor)
            {
                capture->phase = 3;
                const auto closeAt = pluginOperations.size();
                if (originalFixture)
                    menuAction("Close native editor");
                else
                    sharedSemanticAction("closeEditor");
                const auto closed = waitJob(closeAt, "close");
                check(closed.outcome == PluginEditorOutcome::closed && closed.completionKnown &&
                          !closed.window.exists && ownedWindows(pid, originalFixture).count == 0 &&
                          !IsWindow(firstNativeWindow) &&
                          (!originalFixture || !ownedWindows(pid, true).originalChild),
                      "Separate slot context Close retires native peer without removing effect");
            }
            capture->phase = 4;
            const auto editorJobs = pluginOperations.size();
            if (originalFixture)
            {
                menuAction("Plugin controls...");
                const auto openedControls = waitJob(editorJobs, "parameters");
                const auto controlsAt = juce::Time::getMillisecondCounter();
                // The fixture stops Studio's timer while its independent audio
                // driver runs. Pump the actual CONTROL publication explicitly.
                while (!bool(pluginEditorState(pluginId)["wrapperReady"]) &&
                       juce::Time::getMillisecondCounter() - controlsAt < 2000)
                {
                    servicePluginWrapperRequests();
                    dispatchMessages(2);
                }
                const auto controls = pluginEditorState(pluginId);
                check(openedControls.succeeded() && openedControls.window.visible &&
                          controls["wrapperView"].toString() == "parameters" &&
                          bool(controls["wrapperReady"]) && !parameterWindow &&
                          controls["availability"].toString() ==
                              (expectNoEditor ? "controls_only" : "available") &&
                          bool(controls["nativeInterfaceAvailable"]) == !expectNoEditor,
                      "Ordinary slot Controls opens its actual worker Parameters wrapper, including "
                      "honest editorless fallback; no duplicate inspector");
                const auto closeControlsAt = pluginOperations.size();
                sharedSemanticAction("closeEditor");
                check(waitJob(closeControlsAt, "close").succeeded() &&
                          !activeHost->editorWindowState().exists,
                      "Wrapper Controls closes through the canonical acknowledged editor service");
            }
            else
            {
                sharedSemanticAction("controls");
                check(parameterWindow && parameterWindow->isVisible(),
                      "Separate functional Controls inspector opens");
                const auto controlsWait = juce::Time::getMillisecondCounter();
                while (!bool(pluginControlSnapshot()["ready"]) &&
                       juce::Time::getMillisecondCounter() - controlsWait < 2000)
                    dispatchMessages(5);
                const auto control = pluginControlSnapshot();
                check(bool(control["ready"]) && juce::int64(control["plugin"]) == pluginId &&
                          control["instance"].toString() == instance &&
                          !activeHost->editorWindowState().exists && pluginOperations.size() == editorJobs,
                      "Controls selection targets effect without impersonating native editor action");
                parameterWindow->closeButtonPressed();
                dispatchMessages(10);
            }
            const auto waitAt = juce::Time::getMillisecondCounter();
            while (capture->complete.load(std::memory_order_acquire) < 16 &&
                   juce::Time::getMillisecondCounter() - waitAt < 1000)
                dispatchMessages(2);
            retainCapture(); // Join before PCM/model/native lifecycle readback.
            const auto after = audio.projectionDiagnostics();
            check(audioPassed,
                  "Independent native effect playback PCM/misses/deadlines failed; inspect raw/CSV");
            check(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                      before.replacements == after.replacements &&
                      before.samplerCreations == after.samplerCreations &&
                      before.midiUpdates == after.midiUpdates && after.transportPlaying &&
                      after.headPlaying &&
                      std::llround(after.headSeconds * 48000.) - std::llround(before.headSeconds * 48000.) ==
                          capturedCallbacks * 256 &&
                      starts == device->starts && stops == device->stops &&
                      audio.preparedPluginHost(controller.project, pluginId) == activeHost &&
                      activeHost->alive() && activeHost->runtimeIdentity() == instance &&
                      activeHost->processId() == pid,
                  "Source/Edit/worker/transport/device survive all effect name and context interactions");
            check(controller.project.state().toXmlString() == sourceState &&
                      controller.projectIdentity == identity && controller.revision == revision &&
                      controller.dirty == dirty && controller.undo.getUndoDescriptions() == undo &&
                      controller.undo.getRedoDescriptions() == redo &&
                      controller.undo.getNumberOfUnitsTakenUpByStoredCommands() == units,
                  "Opening editors does not change source/effect state, bypass, order, dirty or Undo/Redo");
            auto stream = modeFolder.getChildFile("populated-mixer-component.png").createOutputStream();
            check(bool(stream), "Fresh component screenshot stream");
            stream->setPosition(0);
            stream->truncate();
            check(juce::PNGImageFormat().writeImageToStream(
                      editor->createComponentSnapshot(editor->getLocalBounds()), *stream),
                  "Capture actual populated Mixer component; not a desktop/OS-input claim");
            stream.reset();
            const auto stateAfter = nativeState();
            auto *stateRow = new juce::DynamicObject;
            stateRow->setProperty("mode", phase);
            stateRow->setProperty("nativeStateEqual", stateBefore == stateAfter);
            stateRow->setProperty("stateCapturedOutsideMeasuredPlayback", true);
            stateRow->setProperty("stateComparisonRequired", originalFixture);
            outcomes.add(juce::var(stateRow));
            check(!originalFixture || stateBefore == stateAfter,
                  "Original provider native instrument/effect state remains byte-identical after window "
                  "operations");
            audio.stop();
        }
        phase = "completed";
        check(persist(true), "Persist complete native effect opening result");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        failure = error.what();
        retainCapture();
        if (ownedMenu)
            ownedMenu->exitModalState(0);
        audio.stop();
        (void)persist(false);
        return juce::Result::fail("Effect slot opening: " + phase + ": " + failure);
    }
    catch (...)
    {
        failure = "Unknown actual provider/UI failure";
        retainCapture();
        if (ownedMenu)
            ownedMenu->exitModalState(0);
        audio.stop();
        (void)persist(false);
        return juce::Result::fail(failure);
    }
}
