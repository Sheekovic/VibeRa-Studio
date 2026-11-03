// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual Studio/Controller/MCP/native callback acceptance. No OS input,
// physical device, installed provider, or listening claim. Included in Studio.cpp.
juce::Result Studio::verifyStudioChannelRouting(const juce::File &folder, const PluginSpec *originalEffect,
                                                const juce::File &detectorFixture)
{
    if (!controllerTestMode || !pluginTestSettings || !workflowTestSettings ||
        !bool(getProperties()["viberaStudioRoutingFixture"]) ||
        getProperties()["viberaStudioRoutingOutput"].toString() != folder.getFullPathName() ||
        audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        return juce::Result::fail("Routing acceptance requires owned isolated initial no-device Studio");
    const auto receipt = folder.getChildFile("studio-routing-verification.json");
    if (receipt.exists()) return juce::Result::fail("Retain prior routing receipt; use a fresh folder");
    if (auto created = folder.createDirectory(); created.failed()) return created;
    const bool timerWasRunning = isTimerRunning();
    stopTimer();
    juce::String stage = "initial", failure;
    juce::String callbackTrace = "callback,phase,pattern,diagnostic,outputPeak,bus1Peak,bus2Peak,bus3Peak,submitted,processed,missed,dropped\n";
    std::vector<float> callbackPcm;
    callbackPcm.reserve(256 * 2 * 8192);
    int callbackCount = 0;
    bool diagnosticCallbacks = false;
    juce::StringArray passed;
    auto object = [] { return juce::var(new juce::DynamicObject); };
    auto put = [](juce::var& value, const char* key, const juce::var& data)
    { value.getDynamicObject()->setProperty(key, data); };
    auto check = [](bool value, const char* why)
    { if (!value) throw std::runtime_error(why); };
    auto invoke = [&](const char* name, const juce::var& args)
    { return mcpCall(name, args)["structuredContent"]; };
    auto route = [&](int mixer)
    {
        auto args = object(), values = object();
        put(args, "command", "channel.mixer");
        put(values, "channel", juce::int64(selectedChannel)); put(values, "mixer", mixer);
        put(args, "args", values); return invoke("vibera_command", args);
    };
    auto history = [&](const char* command)
    { auto args = object(); put(args, "command", command); return invoke("vibera_command", args); };
    auto read = [&](std::uint64_t id)
    { auto args = object(); put(args, "action", "routingJob"); put(args, "operation", juce::int64(id));
      return invoke("vibera_mixer", args)["routing"]; };
    auto expectDocument = [&](const juce::var& job, int mixer)
    {
        check(job["completionState"].toString() == "completed" && bool(job["documentCommitted"]) &&
              job["audioDisposition"].toString() == "deferredNoDevice" &&
              !bool(job["nativeCompletionKnown"]) && !bool(job["audioPosted"]) &&
              job["projectIdentity"].toString() == controller.projectIdentity &&
              juce::int64(job["destinationRevision"]) == juce::int64(controller.revision) &&
              controller.project.channel(selectedChannel)->mixer == mixer &&
              !audio.projectionDiagnostics().edit && !audio.projectionDiagnostics().playbackContext,
              "Offline route is one truthful document transaction, with no native Edit/context");
    };
    try
    {
        stage = "offline-shared-model-history";
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels.front().source = "builtin:bass";
        project.channels.front().mixer = 1;
        project.channels.front().gain = 1;
        project.channels.front().pan = 0;
        const auto defaultMixerCount = project.mixer.size();
        const int replacedDormantIndex = project.mixer.back().index;
        check(defaultMixerCount > 4 && replacedDormantIndex != 0 &&
              std::none_of(project.channels.begin(), project.channels.end(),
                  [&](const auto& channel) { return channel.mixer == replacedDormantIndex; }),
              "Sparse fixture changes one dormant index only; no source ownership or Master replacement");
        project.mixer.back().index = 128;
        check(project.mixer.size() == defaultMixerCount && Project::empty().mixer.size() == defaultMixerCount,
              "Disposable sparse-index fixture does not change the application's default Mixer count");
        for (auto& insert : project.mixer) { insert.gain = 1; insert.muted = false; }
        project.patterns.front().notes.clear(); project.placements.clear();
        Id fixtureId = 0;
        if (originalEffect)
        {
            check(originalEffect->name == "VibeRa Parameter Fixture" && !originalEffect->instrument,
                  "Exact original SDK effect branch; arbitrary installed-provider controls are not assumed");
            auto effect = *originalEffect; effect.id = project.allocate(); fixtureId = effect.id;
            project.mixer[1].effects.push_back(effect);
        }
        controller.replace(project);
        selectedChannel = project.channels.front().id; selectedPattern = project.patterns.front().id;
        patternMode = true;
        auto preparation = audio.rebuild(controller.project, true, selectedPattern);
        const auto preparationDeadline = juce::Time::getMillisecondCounter() + 15000;
        while (preparation.failed() && AudioEngine::preparationPending(preparation) &&
               juce::Time::getMillisecondCounter() < preparationDeadline)
        { audio.reclaimPluginHosts(); dispatchMessages(5); preparation = audio.rebuild(controller.project, true, selectedPattern); }
        check(preparation.wasOk(), "Initial stopped native projection");
        const auto initialSample = audio.channelSample(selectedChannel);
        check(bool(initialSample), "Actual initial sampler loaded PCM readback");
        auto fixtureHost = fixtureId ? audio.preparedPluginHost(controller.project, fixtureId) : nullptr;
        juce::String expectedNativeState;
        if (originalEffect)
        {
            check(bool(fixtureHost), "Actual original worker prepared without physical device");
            PluginParameterCatalogue catalogue;
            check(fixtureHost->requestParameterCatalogue() != 0, "Original fixture metadata request");
            const auto catalogueDeadline = juce::Time::getMillisecondCounter() + 5000;
            while (!fixtureHost->readParameterCatalogue(catalogue) && juce::Time::getMillisecondCounter() < catalogueDeadline)
            { fixtureHost->service(); dispatchMessages(2); }
            check(catalogue.generation && !catalogue.error, "Actual original worker parameter metadata");
            const auto request = fixtureHost->setParameter(0, .625f, catalogue.generation);
            PluginParameterResult response; bool applied = false;
            const auto controlDeadline = juce::Time::getMillisecondCounter() + 5000;
            while (!(applied = fixtureHost->readParameterResult(request, response)) &&
                   juce::Time::getMillisecondCounter() < controlDeadline)
            { fixtureHost->service(); dispatchMessages(2); }
            check(request && applied && response.result == 0 && response.value == .625f,
                  "Actual opaque worker state changed before offline route");
            const auto stateRequest = fixtureHost->requestState(); bool captured = false;
            const auto stateDeadline = juce::Time::getMillisecondCounter() + 5000;
            while (!(captured = fixtureHost->readState(stateRequest, expectedNativeState)) &&
                   juce::Time::getMillisecondCounter() < stateDeadline)
            { fixtureHost->service(); dispatchMessages(2); }
            check(stateRequest && captured && expectedNativeState.isNotEmpty(), "Original native state oracle before route");
        }
        const auto ownerDeadline = juce::Time::getMillisecondCounter() + 2000;
        while (!audio.canDeferChannelRouting() && juce::Time::getMillisecondCounter() < ownerDeadline)
        { audio.reclaimPluginHosts(); dispatchMessages(1); }
        check(audio.canDeferChannelRouting(), "Actual no-device/native-owner admission");
        audio.seekBeat(3.25); audio.setLooping(false);
        const auto revision = controller.revision;
        expectDocument(route(2), 2);
        check(audio.channelSample(selectedChannel) == initialSample,
              "Offline sampler readback retains exact source PCM without dangling native PluginPtrs");
        if (fixtureId)
            check(audio.preparedPluginHost(controller.project, fixtureId) == fixtureHost &&
                  audio.pluginProjectionReady(controller.project, fixtureId),
                  "Spec-qualified native Controls/editor worker remains usable without a DSP graph");
        check(controller.revision == revision + 1 && audio.positionBeat() == 3.25,
              "Offline route retains cursor and commits one real revision");
        expectDocument(history("edit.undo"), 1);
        expectDocument(history("edit.redo"), 2);
        passed.add(stage);

        stage = "routing-discovery-and-sparse-index-command-batch";
        auto discoveryArgs = object(); put(discoveryArgs, "command", "channel.mixer");
        put(discoveryArgs, "includeSchemas", true);
        const auto contracts = invoke("vibera_commands", discoveryArgs)["operations"];
        check(contracts.isArray() && contracts.size() == 1, "Exact authoritative Channel routing discovery");
        const auto contract = contracts[0];
        check(contract["arguments"]["properties"].isObject() &&
                  contract["arguments"]["properties"].getDynamicObject()->getProperties().size() == 2 &&
                  int(contract["arguments"]["properties"]["mixer"]["maximum"]) == 2147483647 &&
                  int(contract["arguments"]["properties"]["mixer"]["default"]) == 0 &&
                  int(contract["arguments"]["properties"]["channel"]["minimum"]) == 1 &&
                  bool(contract["asynchronous"]) && contract["prerequisitesDetail"].toString().isNotEmpty() &&
                  contract["stateReadback"].toString().isNotEmpty() &&
                  contract["resultPolling"]["tool"].toString() == "vibera_mixer" &&
                  contract["resultPolling"]["action"].toString() == "routingJob" &&
                  contract["cancellation"]["errorCode"].toString() == "routing.tooLate" &&
                  contract["resultSchema"]["anyOf"].isArray() &&
                  contract["resultSchema"]["anyOf"].size() == 3 &&
                  juce::JSON::toString(contract["resultSchema"]["anyOf"][0]) ==
                      juce::JSON::toString(channelRoutingResultSchema()) &&
                  juce::JSON::toString(contract["resultSchema"]["anyOf"][1]) ==
                      juce::JSON::toString(mixerChainWorkflowResultSchema()),
              "Registry discovers exact typed args and one shared receipt/poll/cancel contract");
        const auto toolDefinitions = mcpTools();
        check(toolDefinitions.isArray(), "Actual component tools discovery is an array");
        int matchingToolSchemas = 0;
        for (const auto& definition : *toolDefinitions.getArray())
            if (definition["name"].toString() == "vibera_mixer" ||
                definition["name"].toString() == "vibera_state")
            {
                ++matchingToolSchemas;
                const auto receiptSchema = definition["name"].toString() == "vibera_mixer"
                                               ? definition["outputSchema"]["properties"]["routing"]
                                               : definition["outputSchema"]["properties"]["channelRouting"];
                check(juce::JSON::toString(receiptSchema) ==
                          juce::JSON::toString(contract["resultSchema"]["anyOf"][0]),
                      "Numeric state/readback preserves the authoritative numeric result branch");
            }
        check(matchingToolSchemas == 2, "Both canonical state/readback tool schemas discovered");
        expectDocument(route(128), 128);
        expectDocument(history("edit.undo"), 2);
        expectDocument(history("edit.redo"), 128);
        expectDocument(history("edit.undo"), 2);
        auto defaults = object();
        put(defaults, "command", "channel.mixer");
        expectDocument(invoke("vibera_command", defaults), 0);
        expectDocument(history("edit.undo"), 2);
        const auto noOpRevision = controller.revision;
        const auto unchangedAssignment = route(2);
        check(bool(unchangedAssignment["noOp"]) && unchangedAssignment["completionScope"].toString() == "noChange" &&
              controller.revision == noOpRevision, "Current-index assignment is a truthful no-op, not new history");
        auto sparseBatch = object(); juce::Array<juce::var> sparseSteps;
        for (const int index : {128, 3})
        {
            auto step = object(), values = object(); put(step, "command", "channel.mixer");
            put(values, "channel", juce::int64(selectedChannel)); put(values, "mixer", index);
            put(step, "args", values); sparseSteps.add(step);
        }
        put(sparseBatch, "commands", juce::var(sparseSteps));
        expectDocument(invoke("vibera_batch", sparseBatch), 3);
        expectDocument(history("edit.undo"), 2); expectDocument(history("edit.redo"), 3);
        expectDocument(history("edit.undo"), 2);
        const auto sparseState = controller.project.state().toXmlString();
        const auto sparseRevision = controller.revision;
        const auto undoName = controller.undo.getUndoDescription(), redoName = controller.undo.getRedoDescription();
        const auto unavailableSparse = route(129);
        check(unavailableSparse["completionState"].toString() == "failed" &&
              unavailableSparse["error"]["code"].toString() == "routing.modelRefused",
              "Unknown positive insert is a structured canonical failed operation, not clamped");
        for (const auto bad : {juce::var(-1), juce::var(.5), juce::var(juce::int64(2147483648LL))})
        {
            auto arguments = object(), values = object(); put(arguments, "command", "channel.mixer");
            put(values, "channel", juce::int64(selectedChannel)); put(values, "mixer", bad); put(arguments, "args", values);
            bool invalid = false;
            try { invoke("vibera_command", arguments); }
            catch (const McpFailure& error) { invalid = error.code == "invalid_arguments"; }
            check(invalid, "Out-of-range/fractional Mixer index has typed pre-mutation refusal");
        }
        check(controller.project.state().toXmlString() == sparseState && controller.revision == sparseRevision &&
              controller.undo.getUndoDescription() == undoName && controller.undo.getRedoDescription() == redoName &&
              audio.channelSample(selectedChannel) == initialSample,
              "Invalid sparse routing preserves real project/history/source PCM");
        passed.add(stage);

        stage = "offline-route-only-macro-and-batch";
        CommandArgs first, second; first.channel = second.channel = selectedChannel;
        first.mixer = 3; second.mixer = 4;
        Macro macro{"fixture.route-only", "Original route-only aggregate",
                    {{"channel.mixer", first, false}, {"channel.mixer", second, false}}};
        customization.macros.push_back(macro);
        const auto beforeMacro = controller.revision;
        runMacro(macro.id);
        expectDocument(channelRoutingState(lastRoutingDispatchOperation), 4);
        check(controller.revision == beforeMacro + 1, "Macro has one canonical history transaction");
        expectDocument(history("edit.undo"), 2); expectDocument(history("edit.redo"), 4);
        auto batch = object(); juce::Array<juce::var> steps;
        for (const auto mixer : {2, 3})
        { auto step = object(), values = object(); put(step, "command", "channel.mixer");
          put(values, "channel", juce::int64(selectedChannel)); put(values, "mixer", mixer);
          put(step, "args", values); steps.add(step); }
        put(batch, "commands", juce::var(steps));
        expectDocument(invoke("vibera_batch", batch), 3);
        expectDocument(history("edit.undo"), 4); expectDocument(history("edit.redo"), 3);
        const auto beforeInvalid = controller.project.state().toXmlString();
        const auto invalidRevision = controller.revision;
        check(route(127)["completionState"].toString() == "failed" &&
              controller.project.state().toXmlString() == beforeInvalid && controller.revision == invalidRevision,
              "Invalid route is a real failed job without mutation");
        auto mixed = macro; mixed.id = "fixture.mixed-route";
        mixed.steps.push_back({"channel.gain", first, false}); customization.macros.push_back(mixed);
        runMacro(mixed.id);
        check(status.startsWith("routing.unsupportedMixedSequence") && controller.revision == invalidRevision,
              "Mixed route/content macro refuses whole before first mutation");
        passed.add(stage);

        stage = "offline-sampler-readback-source-identity";
        CommandArgs replaceSource; replaceSource.channel = selectedChannel; replaceSource.text = "builtin:kick";
        auto sourceCommand = [&]
        {
            const auto revisionBefore = controller.revision;
            const auto deadline = juce::Time::getMillisecondCounter() + 15000;
            command("channel.source", replaceSource);
            while (AudioEngine::preparationPending(juce::Result::fail(status)) &&
                   juce::Time::getMillisecondCounter() < deadline)
            {
                check(controller.revision == revisionBefore, "Structural preparation refusal has no partial mutation");
                audio.reclaimPluginHosts(); dispatchMessages(5); command("channel.source", replaceSource);
            }
            timerCallback();
        };
        sourceCommand();
        check(status == "Ready" && !audio.documentProjectionDeferred() &&
              controller.project.channel(selectedChannel)->source == "builtin:kick" &&
              audio.channelSample(selectedChannel) && audio.channelSample(selectedChannel) != initialSample,
              "Actual offline source edit restores ordinary projection and never shows retired bass PCM");
        replaceSource.text = "builtin:bass"; sourceCommand();
        check(audio.channelSample(selectedChannel) == initialSample,
              "Canonical source restoration reuses exact prepared immutable bass PCM");
        expectDocument(route(2), 2); expectDocument(route(3), 3);
        passed.add(stage);

        stage = "automatic-no-file-device-preparation";
        const bool sameConfigCase = bool(getProperties()["viberaStudioRoutingSameConfigFixture"]);
        juce::String originalEditorOpen;
        if (fixtureId)
        {
            juce::String error;
            check(requestPluginEditor(fixtureId, true, fixtureHost->runtimeIdentity(), originalEditorOpen, error).wasOk(),
                  "Original actual native editor open admitted before device appearance");
            const auto deadline = juce::Time::getMillisecondCounter() + 5000;
            while (fixtureHost->editorBusy() && juce::Time::getMillisecondCounter() < deadline)
            { pollPluginOperations(); fixtureHost->service(); dispatchMessages(2); }
            pollPluginOperations();
            check(fixtureHost->editorWindowState().known && fixtureHost->editorWindowState().exists &&
                  fixtureHost->editorWindowState().visible, "Actual native editor exists before capture/reuse");
        }
        const auto projectPathBefore = projectFile;
        const auto startupRevision = controller.revision;
        const auto startupPosition = audio.positionBeat();
        auto& manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", sameConfigCase ? 44100. : 48000.); setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2); dispatchMessages(20);
        auto* device = dynamic_cast<SyntheticDevice*>(manager.deviceManager.getCurrentAudioDevice());
        check(device != nullptr && !audio.projectionDiagnostics().edit,
              "Device appearance cannot resurrect an obsolete routing Edit");
        const auto beforeDevice = controller.project.state().toXmlString();
        check(beginDevicePreparation(false).wasOk() && devicePreparationBusy(),
              "Persistent device preparation admitted without a file Save");
        const auto cancelledToken = devicePreparation->operation;
        auto cancelArgs = object(); put(cancelArgs, "action", "cancelDevicePreparation");
        put(cancelArgs, "operation", juce::int64(cancelledToken));
        check(invoke("vibera_mixer", cancelArgs)["completionState"].toString() == "pending",
              "Cancel admission remains pending until real owned work drains");
        timerCallback();
        check(devicePreparationState(cancelledToken)["completionState"].toString() == "cancelled" &&
              controller.project.state().toXmlString() == beforeDevice && !audio.projectionDiagnostics().edit,
              "Pre-dispatch cancellation drains without source/history or native projection changes");
        auto retryFromToken = cancelledToken;
        if (fixtureId && !sameConfigCase)
        {
            // Actual original-worker snapshot succeeds, but its app callback
            // intentionally throws BEFORE delegating to the ordinary job
            // receiver. No production failpoint/global allocation hook exists.
            check(retryDevicePreparation(cancelledToken).wasOk(), "Owned callback-fault preparation admission");
            const auto faultOperation = devicePreparation->operation;
            const auto closeDeadline = juce::Time::getMillisecondCounter() + 5000;
            while (devicePreparation->phase == DeferredDevicePreparation::Phase::closing &&
                   juce::Time::getMillisecondCounter() < closeDeadline)
            { serviceDevicePreparation(); dispatchMessages(2); }
            check(devicePreparation->phase == DeferredDevicePreparation::Phase::capturing &&
                  afterPluginState && deviceCaptureCallbackOperation == faultOperation,
                  "Actual native close completed and exact shared capture callback is installed");
            auto originalReceiver = std::move(afterPluginState);
            afterPluginState = [receiver=std::move(originalReceiver)](juce::Result, Project) mutable
            {
                (void)receiver; // Retained for proper CONTROL destruction; never falsely invoked.
                throw std::runtime_error("Original owned-device snapshot callback failure before receiver");
            };
            const auto failedDeadline = juce::Time::getMillisecondCounter() + 12000;
            while (devicePreparationBusy() && juce::Time::getMillisecondCounter() < failedDeadline)
            { timerCallback(); dispatchMessages(2); }
            const auto faultState = devicePreparationState(faultOperation);
            check(faultState["completionState"].toString() == "failed" &&
                  faultState["error"]["code"].toString() == "device.capturePollException" &&
                  !afterPluginState && deviceCaptureCallbackOperation == 0 && !audio.pluginStateCapturePending() &&
                  controller.project.state().toXmlString() == beforeDevice &&
                  controller.revision == startupRevision && !audio.projectionDiagnostics().edit &&
                  audio.preparedPluginHost(controller.project, fixtureId) == fixtureHost &&
                  fixtureHost->alive() && fixtureHost->recoveryState() == expectedNativeState,
                  "Real pre-receiver callback failure drains without fake metadata, lost source or false completion");
            // An ordinary generic file-state callback has no device stamp and
            // must not be swallowed by the new narrow device-owner catch.
            auto foreignReached = std::make_shared<bool>(false);
            bool foreignEscaped = false;
            withPluginState([foreignReached](juce::Result, Project)
            {
                *foreignReached = true;
                throw std::runtime_error("Original foreign snapshot callback exception");
            });
            const auto foreignDeadline = juce::Time::getMillisecondCounter() + 12000;
            while (!*foreignReached && juce::Time::getMillisecondCounter() < foreignDeadline)
            {
                try { timerCallback(); }
                catch (const std::runtime_error& error)
                {
                    if (juce::String(error.what()) != "Original foreign snapshot callback exception") throw;
                    foreignEscaped = true;
                }
                dispatchMessages(2);
            }
            check(*foreignReached && foreignEscaped && !afterPluginState && deviceCaptureCallbackOperation == 0 &&
                  !audio.pluginStateCapturePending() && controller.project.state().toXmlString() == beforeDevice,
                  "Ordinary file-state exception behavior is preserved; foreign callback never stolen");
            retryFromToken = faultOperation;
        }
        auto retryArgs = object(); put(retryArgs, "action", "retryDevicePreparation");
        put(retryArgs, "operation", juce::int64(retryFromToken));
        const auto retried = invoke("vibera_mixer", retryArgs);
        check(retried["completionState"].toString() == "pending" && devicePreparationBusy() &&
              devicePreparation->operation != cancelledToken,
              "Semantic retry admits a new exact preparation token, never immediate completion");
        auto expired = object(); put(expired, "action", "devicePreparationState");
        put(expired, "operation", juce::int64(cancelledToken));
        check(invoke("vibera_mixer", expired)["completionState"].toString() == "failed",
              "Expired token cannot claim another preparation outcome");
        bool refusedEdit = false;
        try { route(4); } catch (const McpFailure& error) { refusedEdit = error.code == "device.busy"; }
        check(refusedEdit && controller.revision == startupRevision &&
              controller.project.state().toXmlString() == beforeDevice,
              "Shared semantic route admission preserves source while native state is owned");
        const auto startupDeadline = juce::Time::getMillisecondCounter() + 15000;
        while (devicePreparationBusy() && juce::Time::getMillisecondCounter() < startupDeadline)
        { timerCallback(); dispatchMessages(2); }
        const auto preparedState = devicePreparationState();
        auto readPreparation = object(); put(readPreparation, "action", "devicePreparationState");
        const auto semanticPreparation = invoke("vibera_mixer", readPreparation)["devicePreparation"];
        // Retain actual terminal job/error readback before the compound oracle,
        // including a failed async replacement; acceptance alone is not success.
        check(folder.getChildFile("device-preparation-readback.json")
                  .replaceWithText(juce::JSON::toString(semanticPreparation, false)),
              "Retain exact semantic preparation outcome before completion assertions");
        check(preparedState["completionState"].toString() == "completed" &&
                  semanticPreparation["completionState"].toString() == "completed" &&
                  bool(preparedState["nativePrepared"]) && !bool(preparedState["nativeCompletionKnown"]) &&
                  !audio.documentProjectionDeferred() && !needsRebuild &&
                  audio.projectionDiagnostics().edit && projectionError.isEmpty() &&
                  controller.revision == startupRevision && audio.positionBeat() == startupPosition &&
                  projectFile == projectPathBefore && controller.dirty &&
                  folder.findChildFiles(juce::File::findFiles, false, "*.vibra").isEmpty() &&
                  folder.findChildFiles(juce::File::findFiles, false, "*.wav").isEmpty(),
              "Actual preparation completes without save/path/dirty/revision/cursor changes or a false DSP "
              "claim");
        if (fixtureId)
        {
            const auto host = audio.preparedPluginHost(controller.project, fixtureId);
            check(host && host->alive(), "New/reused native worker actually prepared");
            if (sameConfigCase)
                check(bool(preparedState["workersReusedOnly"]) && !bool(preparedState["stateCaptured"]) &&
                      host == fixtureHost && host->editorWindowState().exists &&
                      host->editorWindowState().visible,
                      "Exact same-config worker/editor reused with no capture or unnecessary native close");
            else
                check(!bool(preparedState["workersReusedOnly"]) && bool(preparedState["stateCaptured"]) &&
                      controller.project.plugin(fixtureId)->state == expectedNativeState &&
                      host != fixtureHost && host->recoveryState() == expectedNativeState &&
                      fixtureHost->editorWindowState().known && !fixtureHost->editorWindowState().exists,
                      "Changed-rate worker comes from genuine acknowledged close/capture without saving first");
            // Same-config state is intentionally still native/volatile. Only a
            // changed-rate reconstruction requires an opaque refresh here.
        }
        passed.add(stage);
        stage = "later-explicit-save-roundtrip";
        const auto file = folder.getChildFile("offline-route.vibra");
        bool saved = false;
        auto saveResult = juce::Result::fail("Not completed");
        saveTo(file, [&](juce::Result result) { saveResult = result; saved = true; });
        const auto saveDeadline = juce::Time::getMillisecondCounter() + 12000;
        while (!saved && juce::Time::getMillisecondCounter() < saveDeadline)
        { timerCallback(); dispatchMessages(2); }
        check(saved && saveResult.wasOk(), "Actual shared asynchronous save completes offline");
        Project restored;
        check(loadProject(file, restored).wasOk() && restored.state().isEquivalentTo(controller.project.state()),
              "Exact offline route project roundtrip");
        if (fixtureId)
            check(restored.plugin(fixtureId) && restored.plugin(fixtureId)->state == expectedNativeState &&
                  controller.project.plugin(fixtureId)->state == expectedNativeState,
                  "Native state changed before route is captured by real save and survives project roundtrip");
        refresh(); audio.setMidiTarget(selectedChannel); audio.setMetronome(false); audio.setCountInBars(0);
        juce::AudioBuffer<float> block(2, 256);
        auto tick = [&]
        {
            block.clear(); device->process(block);
            // CONTROL captures AFTER the complete native callback. These are
            // fixture diagnostics, never evidence I/O or allocation inside DSP.
            check(callbackCount < 8192, "Bounded routing and structural-chain recovery PCM trace");
            for (int c = 0; c < 2; ++c)
                callbackPcm.insert(callbackPcm.end(), block.getReadPointer(c), block.getReadPointer(c) + 256);
            auto host = fixtureId ? audio.preparedPluginHost(controller.project, fixtureId) : nullptr;
            const auto stats = host ? host->statistics() : PluginHost::Statistics{};
            callbackTrace += juce::String(callbackCount++) + "," + stage + "," + juce::String(int(patternMode)) +
                "," + juce::String(int(diagnosticCallbacks)) + "," + juce::String(block.getMagnitude(0, 256), 9) +
                "," + juce::String(audio.mixerMeter(1).peak[0], 9) + "," + juce::String(audio.mixerMeter(2).peak[0], 9) +
                "," + juce::String(audio.mixerMeter(3).peak[0], 9) + "," + juce::String(juce::int64(stats.submitted)) +
                "," + juce::String(juce::int64(stats.processed)) + "," + juce::String(juce::int64(stats.missed)) +
                "," + juce::String(juce::int64(stats.dropped)) + "\n";
            routingCoordinator.poll({patternMode, selectedPattern});
        };
        audio.typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
        for (int i = 0; i < 8; ++i) { tick(); dispatchMessages(1); }
        check(block.getMagnitude(0, 256) > .0001f && audio.mixerMeter(3).peak[0] > .0001f &&
              audio.mixerMeter(1).peak[0] == 0 && audio.mixerMeter(2).peak[0] == 0,
              "Real native startup/held-input PCM uses canonical offline destination3 only");
        passed.add(stage);

        stage = "earlier-routing-history-across-native-capture";
        if (fixtureId)
        {
            auto unacknowledged = controller.project;
            unacknowledged.plugin(fixtureId)->state = juce::MemoryBlock("unacknowledged-state", 20).toBase64Encoding();
            check(audio.ensureChannelRoutingContext(unacknowledged, patternMode, selectedPattern).status ==
                      AudioEngine::ChannelRoutingStatus::staleProjection,
                  "Unacknowledged opaque bytes cannot bypass exact native projection admission");
            auto changedMusic = controller.project;
            changedMusic.channels.front().gain *= .5f;
            check(audio.ensureChannelRoutingContext(changedMusic, patternMode, selectedPattern).status ==
                      AudioEngine::ChannelRoutingStatus::staleProjection,
                  "A genuine native capture cannot excuse unrelated musical source drift");
        }
        const auto capturedRoute = controller.project.channel(selectedChannel)->mixer;
        const auto historicalUndo = history("edit.undo");
        auto historyTrace = object();
        put(historyTrace, "dispatch", historicalUndo);
        put(historyTrace, "routing", channelRoutingState());
        put(historyTrace, "status", status);
        put(historyTrace, "canonicalMixer", controller.project.channel(selectedChannel)->mixer);
        check(folder.getChildFile("historical-route-dispatch.json").replaceWithText(juce::JSON::toString(historyTrace)),
              "Retain actual captured-state history admission before its assertion");
        check(historicalUndo["completionState"].toString() == "pending" &&
              controller.project.channel(selectedChannel)->mixer == 2,
              "EARLIER routing history admits across actual native capture/save (not a new post-save action)");
        tick();
        check(channelRoutingState()["completionState"].toString() == "completed",
              "Earlier routing Undo receives actual native selected completion");
        if (fixtureId) check(controller.project.plugin(fixtureId)->state == expectedNativeState,
              "Earlier routing Undo preserves the actual latest opaque native capture");
        check(history("edit.redo")["completionState"].toString() == "pending",
              "Earlier routing Redo uses the actual existing transaction");
        tick();
        check(channelRoutingState()["completionState"].toString() == "completed" &&
              controller.project.channel(selectedChannel)->mixer == capturedRoute,
              "Earlier routing Redo receives actual selected completion and restores route");
        if (fixtureId) check(controller.project.plugin(fixtureId)->state == expectedNativeState,
              "Earlier routing Redo preserves the actual latest opaque native capture");
        passed.add(stage);

        stage = "actual-ui-mcp-history-selected-completion";
        const auto source = audio.projectionDiagnostics();
        const auto starts = device->starts, stops = device->stops;
        focus = Scope::mixer; selectedInsert = 2;
        check(performAction("channel.mixer"), "Actual contextual UI assignment entry");
        const auto operation = lastRoutingDispatchOperation;
        check(operation && channelRoutingState(operation)["completionState"].toString() == "pending" &&
              !needsRebuild && engineRevision == controller.revision,
              "UI route publishes truthful pending native operation without legacy rebuild");
        check(read(operation)["completionState"].toString() == "pending", "No callback cannot complete publication");
        tick();
        check(read(operation)["completionState"].toString() == "completed" &&
              bool(read(operation)["nativeCompletionKnown"]) &&
              audio.mixerMeter(2).peak[0] > .0001f && audio.mixerMeter(3).peak[0] == 0,
              "Actual selected graph retains held source at new destination2");
        undoButton.onClick(); check(routingPending(), "Actual Undo button route waits for its callback");
        tick(); check(controller.project.channel(selectedChannel)->mixer == 3 && !routingPending(), "Actual Undo route3");
        redoButton.onClick(); check(routingPending(), "Actual Redo button route waits for its callback");
        tick(); check(controller.project.channel(selectedChannel)->mixer == 2 && !routingPending(), "Actual Redo route2");
        const auto after = audio.projectionDiagnostics();
        check(after.edit == source.edit && after.playbackContext == source.playbackContext &&
              after.channels.front().sample == source.channels.front().sample &&
              audio.positionBeat() == 3.25 && !audio.playing() &&
              device->starts == starts && device->stops == stops,
              "Actual UI/MCP/history reuse native context/PCM and retain stopped cursor/device lifecycle");
        audio.typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60)); tick();
        passed.add(stage);

        stage = "actual-pattern-song-live-route-history";
        for (const bool song : {false, true})
        {
            if (patternMode == song)
            {
                const auto deadline = juce::Time::getMillisecondCounter() + 15000;
                mode.onClick();
                while (patternMode == song && AudioEngine::preparationPending(juce::Result::fail(status)) &&
                       juce::Time::getMillisecondCounter() < deadline)
                { audio.reclaimPluginHosts(); dispatchMessages(5); mode.onClick(); }
                timerCallback();
                check(patternMode != song && projectionError.isEmpty(), "Actual mode change full canonical handoff");
            }
            audio.setMidiTarget(selectedChannel);
            audio.typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
            play.onClick();
            check(audio.playing(), "Actual Play control starts selected Pattern/Song mode");
            for (int i = 0; i < 8; ++i) { tick(); dispatchMessages(1); }
            const auto live = audio.projectionDiagnostics();
            const auto liveStarts = device->starts, liveStops = device->stops;
            const auto liveBeat = audio.positionBeat();
            const auto target = controller.project.channel(selectedChannel)->mixer == 2 ? 3 : 2;
            const auto posted = route(target);
            check(posted["completionState"].toString() == "pending", "Live route returns pending before actual callback");
            tick();
            check(channelRoutingState()["completionState"].toString() == "completed" && audio.playing(),
                  "Actual live route completes only after DSP selects its graph");
            check(history("edit.undo")["completionState"].toString() == "pending", "Live canonical Undo pending");
            tick();
            check(history("edit.redo")["completionState"].toString() == "pending", "Live canonical Redo pending");
            tick();
            const auto continued = audio.projectionDiagnostics();
            // Capture one actual post-callback observation before diagnostic
            // file I/O. This manual device cannot produce another callback
            // while CONTROL writes receipts; meters correctly expire after
            // 250ms. Re-reading after a slow disk flush tested receipt I/O,
            // rather than the live routing continuity recorded in this file.
            const auto continuedMeter = audio.mixerMeter(target);
            const auto continuedBeat = audio.positionBeat();
            const auto continuedPlaying = audio.playing();
            const auto continuedPeak = block.getMagnitude(0, 256);
            const bool continuous =
                continued.edit == live.edit && continued.playbackContext == live.playbackContext &&
                continued.channels.front().sample == live.channels.front().sample &&
                device->starts == liveStarts && device->stops == liveStops && continuedPlaying &&
                continuedBeat >= liveBeat && continuedPeak > .0001f && continuedMeter.peak[0] > .0001f;
            auto continuity = object();
            put(continuity, "song", song);
            put(continuity, "sameEdit", continued.edit == live.edit);
            put(continuity, "sameContext", continued.playbackContext == live.playbackContext);
            put(continuity, "sameSample", continued.channels.front().sample == live.channels.front().sample);
            put(continuity, "sameDeviceStarts", device->starts == liveStarts);
            put(continuity, "sameDeviceStops", device->stops == liveStops);
            put(continuity, "playing", continuedPlaying);
            put(continuity, "sourceBeat", liveBeat);
            put(continuity, "currentBeat", continuedBeat);
            put(continuity, "target", target);
            put(continuity, "outputPeak", continuedPeak);
            put(continuity, "targetPeak", continuedMeter.peak[0]);
            put(continuity, "meterAgeMs", juce::int64(continuedMeter.ageMs));
            put(continuity, "meterStale", continuedMeter.stale);
            put(continuity, "continuous", continuous);
            put(continuity, "projectionError", projectionError);
            put(continuity, "routing", channelRoutingState());
            check(folder.getChildFile(song ? "song-continuity.json" : "pattern-continuity.json")
                      .replaceWithText(juce::JSON::toString(continuity)),
                  "Retain discriminating continuity evidence");
            auto finalPcm = folder
                                .getChildFile(song ? "song-final-planar-float32le.raw"
                                                   : "pattern-final-planar-float32le.raw")
                                .createOutputStream();
            check(bool(finalPcm), "Retain final failed callback PCM");
            for (int c = 0; c < 2; ++c)
                check(finalPcm->write(block.getReadPointer(c), 256 * sizeof(float)), "Full failed callback PCM");
            finalPcm.reset();
            if (!continuous)
            {
                // Preserve the failure. Extra callbacks distinguish a cold
                // delay from lost source ownership; they cannot turn it green.
                diagnosticCallbacks = true;
                for (int i = 0; i < 8; ++i) { tick(); dispatchMessages(1); }
                diagnosticCallbacks = false;
            }
            check(continuous,
                  "Pattern/Song UI/MCP/history preserve live native owners, held sound and transport/device lifecycle");
            const auto sparseLive = audio.projectionDiagnostics();
            const auto sparsePosted = route(128);
            check(sparsePosted["completionState"].toString() == "pending",
                  "Actual Pattern/Song sparse-index route is pending before callback selection");
            tick();
            const auto sparseContinued = audio.projectionDiagnostics();
            check(channelRoutingState()["completionState"].toString() == "completed" &&
                  controller.project.channel(selectedChannel)->mixer == 128 && audio.playing() &&
                  sparseContinued.edit == sparseLive.edit && sparseContinued.playbackContext == sparseLive.playbackContext &&
                  sparseContinued.channels.front().sample == sparseLive.channels.front().sample &&
                  sparseContinued.channels.front().heldInputKeys == sparseLive.channels.front().heldInputKeys &&
                  sparseContinued.channels.front().heldInputKeys > 0 &&
                  audio.mixerMeter(128).peak[0] > .0001f && block.getMagnitude(0, 256) > .0001f &&
                  device->starts == liveStarts && device->stops == liveStops,
                  "Sparse insert128 keeps actual source/held key/native owners and sound in Pattern/Song");
            check(history("edit.undo")["completionState"].toString() == "pending", "Sparse live checked Undo pending");
            tick();
            check(controller.project.channel(selectedChannel)->mixer == target && !routingPending(),
                  "Sparse live Undo restores prior stable index through actual selected callback");
            audio.typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
            stop.onClick(); tick();
        }
        passed.add(stage);

        if (originalEffect)
        {
            stage = "actual-stopped-effect-ui-mcp-transaction-history";
            const auto incumbent = audio.projectionDiagnostics();
            const auto effectStarts = device->starts, effectStops = device->stops;
            const auto initialRevision = controller.revision;
            const auto initialState = controller.project.state().toXmlString();
            const int insert = controller.project.channel(selectedChannel)->mixer;
            const auto effectCommand = [&](const char *id, const juce::var &values)
            {
                auto args = object();
                put(args, "command", id);
                put(args, "args", values);
                put(args, "projectIdentity", controller.projectIdentity);
                put(args, "revision", juce::int64(controller.revision));
                return invoke("vibera_command", args);
            };
            auto values = object();
            put(values, "mixer", insert);
            put(values, "text", originalEffect->descriptor);
            juce::Array<juce::var> effectReceipts;
            const auto finishEffect = [&](const juce::var &admitted, const char *expected)
            {
                const auto operation = std::uint64_t(juce::int64(admitted["operation"]));
                check(operation && admitted["completionState"].toString() == "pending" &&
                          !bool(admitted["documentCommitted"]),
                      "Effect admission is pending before canonical/native commit");
                const auto deadline = juce::Time::getMillisecondCounter() + 15000;
                auto receipt = read(operation);
                while (receipt["completionState"].toString() == "pending" &&
                       juce::Time::getMillisecondCounter() < deadline)
                {
                    tick();
                    timerCallback();
                    dispatchMessages(5);
                    receipt = read(operation);
                }
                effectReceipts.add(receipt);
                check(folder.getChildFile("effect-transactions.json")
                          .replaceWithText(juce::JSON::toString(juce::var(effectReceipts))),
                      "Retain actual effect receipts before assertion");
                check(receipt["completionState"].toString() == expected,
                      "Effect operation reaches expected real terminal outcome");
                if (juce::String(expected) == "completed")
                    check(bool(receipt["documentCommitted"]) && bool(receipt["nativeCompletionKnown"]) &&
                              juce::int64(receipt["postedSerial"]) ==
                                  juce::int64(receipt["selectedSerial"]) &&
                              !needsRebuild && engineRevision == controller.revision,
                          "Actual application effect waits for native selection/final bank, with one "
                          "projection owner");
                return receipt;
            };
            // The preceding same-config device test deliberately preserves its
            // existing native editor. Structural effects explicitly require a
            // closed native baseline; assert refusal, then close THIS test-owned
            // window through the real shared editor service before proceeding.
            const auto incumbentHost = audio.parameterHost(fixtureId);
            if (incumbentHost && incumbentHost->editorWindowState().exists)
            {
                const auto refusedEditor =
                    finishEffect(effectCommand("plugin.effect.insert", values), "failed");
                check(refusedEditor["errorCode"].toString() == "effects.captureRequired" &&
                          !bool(refusedEditor["documentCommitted"]) && controller.revision == initialRevision,
                      "Open native editor is a truthful pre-publication refusal, not successful effect "
                      "insertion");
                juce::String closeOperation, closeCode;
                check(requestPluginEditor(fixtureId, false, incumbentHost->runtimeIdentity(), closeOperation,
                                          closeCode)
                          .wasOk(),
                      "Explicitly close only the fixture-owned native editor");
                const auto closeDeadline = juce::Time::getMillisecondCounter() + 5000;
                while (incumbentHost->editorBusy() && juce::Time::getMillisecondCounter() < closeDeadline)
                {
                    incumbentHost->service();
                    pollPluginOperations();
                    dispatchMessages(2);
                }
                pollPluginOperations();
                check(incumbentHost->editorWindowState().known && !incumbentHost->editorWindowState().exists,
                      "Actual native editor close acknowledged before structural-effect fixture");
            }
            const auto cancelAdmission = effectCommand("plugin.effect.insert", values);
            if (incumbentHost)
            {
                juce::String competingOperation, competingCode;
                check(requestPluginEditor(fixtureId, true, incumbentHost->runtimeIdentity(),
                                          competingOperation, competingCode)
                              .failed() &&
                          competingCode == "routing.busy" &&
                          recoverPlugin(fixtureId).getErrorMessage().startsWith("routing.busy:"),
                      "UI/MCP shared native editor/recovery admission cannot mutate an owned effect "
                      "transaction");
            }
            auto cancel = object();
            put(cancel, "action", "cancelRouting");
            put(cancel, "operation", cancelAdmission["operation"]);
            const auto cancellation = invoke("vibera_mixer", cancel);
            check(cancellation["completionState"].toString() == "pending",
                  "Capture cancellation remains pending until actual owned capture drains");
            finishEffect(cancelAdmission, "cancelled");
            check(controller.revision == initialRevision &&
                      controller.project.state().toXmlString() == initialState,
                  "Cancelled pre-publication effect preserves exact document/history source");
            check(
                audio.ensureChannelRoutingContext(controller.project, patternMode, selectedPattern).status ==
                    AudioEngine::ChannelRoutingStatus::ready,
                "Cancellation leaves incumbent projected metadata consistent with unchanged canonical "
                "source");

            CommandArgs proofArgs;
            proofArgs.mixer = insert;
            proofArgs.text = originalEffect->descriptor;
            std::unique_ptr<PreparedControllerChange> proof;
            check(
                controller
                    .prepareStoppedEffectCommand("plugin.effect.insert", proofArgs, controller.project, proof)
                    .wasOk(),
                "Prepare actual same-owner command witness for invalid-admission checks");
            std::unique_ptr<AudioEngine::PreparedChannelRouting> refusedNative;
            auto foreignCapture = controller.project;
            foreignCapture.tempo += 1;
            check(audio.prepareStoppedEffectChange(controller, *proof, foreignCapture, insert, {},
                                                   refusedNative)
                              .status == AudioEngine::ChannelRoutingStatus::unsupportedDelta &&
                      !refusedNative &&
                      audio.prepareStoppedEffectChange(controller, *proof, controller.project, 0, {},
                                                       refusedNative)
                              .status == AudioEngine::ChannelRoutingStatus::unsupportedDelta &&
                      !refusedNative,
                  "Mismatched captured source or insert cannot borrow the same real history witness");
            Controller foreignOwner;
            foreignOwner.replace(controller.project);
            std::unique_ptr<PreparedControllerChange> foreignProof;
            check(
                foreignOwner
                        .prepareStoppedEffectCommand("plugin.effect.insert", proofArgs, foreignOwner.project,
                                                     foreignProof)
                        .wasOk() &&
                    audio.prepareStoppedEffectChange(foreignOwner, *foreignProof, foreignOwner.project,
                                                     insert, {}, refusedNative)
                            .status == AudioEngine::ChannelRoutingStatus::unsupportedDelta &&
                    !refusedNative && controller.revision == initialRevision &&
                    controller.project.state().toXmlString() == initialState,
                "Equivalent foreign Controller/history ticket cannot gain native authority or mutate source");
            proof.reset();
            foreignProof.reset();

            auto invalid = object();
            put(invalid, "mixer", insert);
            put(invalid, "text", "not a VST3 descriptor");
            finishEffect(effectCommand("plugin.effect.insert", invalid), "failed");
            check(controller.revision == initialRevision &&
                      controller.project.state().toXmlString() == initialState,
                  "Actual asynchronous invalid effect refuses without document mutation");

            const auto faultAdmission = effectCommand("plugin.effect.insert", values);
            check(afterPluginState && effectCaptureCallbackOperation == lastRoutingDispatchOperation,
                  "Actual effect capture owns its exact installed callback slot");
            auto originalEffectReceiver = std::move(afterPluginState);
            afterPluginState = [receiver = std::move(originalEffectReceiver)](juce::Result, Project) mutable
            {
                (void)receiver;
                throw std::runtime_error("Original owned effect capture callback fault before receiver");
            };
            const auto captureFailure = finishEffect(faultAdmission, "failed");
            check(captureFailure["errorCode"].toString() == "effects.capturePollFailed" &&
                      !afterPluginState && !effectCaptureCallbackOperation &&
                      !audio.pluginStateCapturePending() && controller.revision == initialRevision &&
                      controller.project.state().toXmlString() == initialState,
                  "Owned capture exception drains and fails truthfully, preserving project/history and "
                  "future admission");

            audio.setMidiTarget(selectedChannel);
            audio.typingInput.handleIncomingMidiMessage(nullptr,
                                                        juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
            for (int n = 0; n < 8; ++n)
            {
                tick();
                dispatchMessages(5);
            }
            finishEffect(effectCommand("plugin.effect.insert", values), "completed");
            const auto *chain = mixerInsert(controller.project, insert);
            check(chain && chain->effects.size() == 1,
                  "Canonical Mixer actually contains the new native effect");
            const auto effectId = chain->effects.front().id;
            const auto firstHost = audio.parameterHost(effectId);
            check(firstHost && firstHost->alive(), "Actual restored native worker belongs to the new slot");
            const auto firstInstance = firstHost->runtimeIdentity();
            undoButton.onClick();
            finishEffect(channelRoutingState(lastRoutingDispatchOperation), "completed");
            check(mixerInsert(controller.project, insert)->effects.empty(),
                  "Actual UI Undo removes effect in one history step");
            finishEffect(history("edit.redo"), "completed");
            const auto restored = audio.parameterHost(effectId);
            check(restored && restored->alive() && restored->runtimeIdentity() != firstInstance &&
                      controller.project.plugin(effectId),
                  "MCP Redo restores the same canonical effect using a fresh worker");
            auto remove = object();
            put(remove, "target", juce::int64(effectId));
            finishEffect(effectCommand("plugin.remove", remove), "completed");
            check(!controller.project.plugin(effectId), "MCP removal changes actual canonical Mixer state");
            undoButton.onClick();
            finishEffect(channelRoutingState(lastRoutingDispatchOperation), "completed");
            check(controller.project.plugin(effectId), "Actual UI Undo of removal restores the effect");
            const auto tooLate = invoke("vibera_mixer",
                                        [&]
                                        {
                                            auto args = object();
                                            put(args, "action", "cancelRouting");
                                            put(args, "operation", juce::int64(lastRoutingOperation));
                                            return args;
                                        }());
            check(tooLate["completionState"].toString() == "failed" &&
                      tooLate["errorCode"].toString() == "routing.tooLate" &&
                      tooLate["routing"]["completionState"].toString() == "completed",
                  "Too-late cancellation fails only that request and retains original successful receipt");
            struct EffectSaveOutcome
            {
                bool completed = false;
                juce::Result result = juce::Result::fail("pending");
            };
            auto savedEffect = std::make_shared<EffectSaveOutcome>();
            const auto effectFile = folder.getChildFile("stopped-effect-workflow.vibra");
            saveTo(effectFile,
                   [savedEffect](juce::Result result)
                   {
                       savedEffect->result = result;
                       savedEffect->completed = true;
                   });
            const auto effectSaveDeadline = juce::Time::getMillisecondCounter() + 12000;
            while (!savedEffect->completed && juce::Time::getMillisecondCounter() < effectSaveDeadline)
            {
                tick();
                timerCallback();
                dispatchMessages(5);
            }
            Project savedMix;
            check(savedEffect->completed && savedEffect->result.wasOk() &&
                      loadProject(effectFile, savedMix).wasOk() &&
                      savedMix.state().isEquivalentTo(controller.project.state()) &&
                      savedMix.plugin(effectId) && savedMix.plugin(effectId)->state.isNotEmpty(),
                  "Actual asynchronous project save/reopen retains canonical restored effect ID/order/native "
                  "state");
            const auto continued = audio.projectionDiagnostics();
            check(continued.edit == incumbent.edit &&
                      continued.playbackContext == incumbent.playbackContext &&
                      continued.channels.front().sample == incumbent.channels.front().sample &&
                      !audio.playing() && device->starts == effectStarts && device->stops == effectStops,
                  "Actual effect workflow preserves native context/sample/device without transport restart");
            audio.typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
            tick();
            passed.add(stage);
        }

        if (originalEffect)
        {
#include "StudioMixerChainApplyAcceptance.inc"
        }

        stage = "device-loss-retained-context-refusal";
        // Deliberate actual loss of this test-owned device; never a user device.
        // Existing effective context must not be reclassified as initial offline.
        manager.deviceManager.closeAudioDevice();
        const auto lostRevision = controller.revision;
        const auto lostState = controller.project.state().toXmlString();
        check(!audio.canDeferChannelRouting(), "Prior native device lifecycle cannot regain initial offline permission");
        juce::var refused;
        try
        {
            refused = route(4);
        }
        catch (const McpFailure &error)
        {
            // This component calls the dispatcher directly; the real transport
            // wraps a typed admission refusal into its failed response object.
            // Keep the exact refusal, rather than mistaking it for a crash or
            // retrying the mutation. Malformed fixture arguments still fail.
            check(error.code != "invalid_arguments" && error.code.isNotEmpty(),
                  "Device-loss request is valid and refused by application admission");
            refused = object();
            put(refused, "completionState", "failed");
            put(refused, "errorCode", error.code);
            put(refused, "message", juce::String(error.what()));
        }
        check(folder.getChildFile("device-loss-refusal.json").replaceWithText(juce::JSON::toString(refused)),
              "Retain actual device-loss refusal before asserting model integrity");
        check(refused["completionState"].toString() == "failed" && controller.revision == lostRevision &&
              controller.project.state().toXmlString() == lostState,
              "Device loss retains canonical route and refuses new document-only mutation");
        passed.add(stage);
    }
    catch (const std::exception& e) { failure = e.what(); }
    catch (...) { failure = "Unknown original Studio routing acceptance failure"; }
    audio.stop();
    if (!folder.getChildFile("native-callbacks.csv").replaceWithText(callbackTrace))
        return juce::Result::fail("Cannot retain native callback diagnostics");
    auto traceOutput = folder.getChildFile("native-callbacks-planar-float32le.raw").createOutputStream();
    if (!traceOutput || !traceOutput->write(callbackPcm.data(), callbackPcm.size() * sizeof(float)))
        return juce::Result::fail("Cannot retain actual native routing PCM");
    traceOutput.reset();
    if (timerWasRunning) startTimerHz(20);
    auto report = object(); put(report, "passed", failure.isEmpty()); put(report, "stage", stage);
    put(report, "failure", failure); put(report, "checks", juce::var(passed));
    put(report, "originalProviderBranch", originalEffect != nullptr);
    put(report, "scope", "Actual Studio component/MCP/model/history plus in-memory native callback; no OS input/hardware/listening");
    if (!receipt.replaceWithText(juce::JSON::toString(report)))
        return juce::Result::fail("Cannot preserve Studio routing evidence");
    return failure.isEmpty() ? juce::Result::ok() : juce::Result::fail(stage + ": " + failure);
}
