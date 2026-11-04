// SPDX-License-Identifier: AGPL-3.0-or-later
#include "NoInline.h"
// Include inside namespace vibera in StudioMcp.cpp, after McpSchema.h is available.
// Original SDK/application/component dispatch, actual no-device workers and file
// jobs and genuine nonzero offline WAV. No OS input, physical device, live
// callback/independent waveform or listening qualification.
VIBERA_NOINLINE juce::Result Studio::verifyPluginIo(const juce::File &folder, const juce::File &binary)
{
    if (!controllerTestMode || !pluginTestSettings || !workflowTestSettings || !folder.isDirectory() ||
        folder.getChildFile("plugin-io.json").exists() || afterPluginState || mixerChainBusy() ||
        pendingExport || audio.rendering() || recordArmed || midiTaking || hasPendingMidiTake() ||
        audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        return juce::Result::fail("Plugin I/O acceptance requires a fresh owned idle no-device Studio");
    stopTimer();
    juce::String stage = "setup";
    bool offlineWavExported = false;
    juce::Array<juce::var> outcomes;
    const auto object = [] { return juce::var(new juce::DynamicObject); };
    const auto put = [](juce::var &v, const char *key, juce::var value)
    { v.getDynamicObject()->setProperty(key, std::move(value)); };
    const auto check = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    const auto retain = [&](bool passed, const juce::String &error)
    {
        auto result = object();
        put(result, "passed", passed);
        put(result, "stage", stage);
        put(result, "error", error);
        put(result, "provider", binary.getFullPathName());
        put(result, "outcomes", outcomes);
        put(result, "componentMenuAction", true);
        put(result, "semanticMcp", true);
        put(result, "actualNativeWorkers", true);
        put(result, "hardware", false);
        put(result, "nativeOsInput", false);
        put(result, "pcmQualified", false);
        put(result, "offlineWavExported", offlineWavExported);
        put(result, "scope",
            "Original SDK no-device main bus transaction/history/file persistence and nonzero offline WAV");
        return folder.getChildFile("plugin-io.json").replaceWithText(juce::JSON::toString(result, true));
    };
    const auto call = [&](const char *name, const juce::var &args)
    {
        auto result = mcpCall(name, args)["structuredContent"];
        auto row = object();
        put(row, "tool", name);
        put(row, "args", args);
        put(row, "result", result);
        outcomes.add(row);
        check(retain(false, "Actual result retained before assertion"),
              "Persist actual I/O operation result");
        return result;
    };
    const auto service = [&]
    {
        timerCallback();
        serviceMixerChainApply();
        audio.reclaimPluginHosts();
        dispatchMessages(2);
    };
    const auto finish = [&](const char *tool, juce::var current, const char *expected)
    {
        const auto operation = current["operation"].toString();
        check(operation.isNotEmpty(), "Actual operation token, never model-only inferred completion");
        const auto began = juce::Time::getMillisecondCounter();
        while (current["completionState"].toString() == "pending" &&
               juce::uint32(juce::Time::getMillisecondCounter() - began) < 15000)
        {
            service();
            auto query = object();
            put(query, "action", "job");
            put(query, "operation", operation);
            current = call(tool, query);
        }
        check(current["completionState"].toString() == expected,
              "Actual I/O/file job reaches required terminal state inside existing15s observation");
        return current;
    };
    const auto ioArgs = [&](Id id, const PluginMainIo &io)
    {
        auto choice = object();
        put(choice, "inputBus", io.inputBus);
        put(choice, "outputBus", io.outputBus);
        put(choice, "inputChannels", io.inputChannels);
        put(choice, "monoSource", io.monoSource);
        auto values = object();
        put(values, "target", juce::int64(id));
        put(values, "pluginIo", choice);
        auto args = object();
        put(args, "command", "plugin.io");
        put(args, "args", values);
        put(args, "revision", juce::int64(controller.revision));
        return args;
    };
    const auto buses = [&](Id id, const PluginMainIo &expected)
    {
        const auto host = audio.preparedPluginHost(controller.project, id);
        check(host && host->alive(), "Current target is a real healthy worker");
        auto args = object();
        put(args, "action", "buses");
        put(args, "plugin", juce::int64(id));
        put(args, "instance", host->runtimeIdentity());
        const auto result = call("vibera_plugin", args);
        const auto state = result["audioIo"];
        check(juce::int64(result["plugin"]) == juce::int64(id) &&
                  result["instance"].toString() == host->runtimeIdentity() && bool(state["catalogueKnown"]) &&
                  bool(state["available"]) && state["inputs"].size() == 3 && state["outputs"].size() == 3,
              "MCP exposes actual original three-input/three-output native catalogue");
        for (const char *field : {"requested", "negotiated"})
            check(int(state[field]["inputBus"]) == expected.inputBus &&
                      int(state[field]["outputBus"]) == expected.outputBus &&
                      int(state[field]["inputChannels"]) == expected.inputChannels &&
                      int(state[field]["monoSource"]) == expected.monoSource,
                  "Requested canonical and actual negotiated native selections agree exactly");
        for (int index = 0; index < 3; ++index)
        {
            const auto input = state["inputs"][index], output = state["outputs"][index];
            check(int(input["index"]) == index && input["name"].toString().isNotEmpty() &&
                      int(output["index"]) == index && output["name"].toString().isNotEmpty() &&
                      int(input["actualChannels"]) ==
                          (index == expected.inputBus ? expected.inputChannels : 0) &&
                      int(output["actualChannels"]) == (index == expected.outputBus ? 2 : 0),
                  "Real catalogue preserves native indices and only selected buses enabled");
        }
        return host;
    };
    const auto pending = [&](const juce::var &result, std::uint64_t revision)
    {
        check(result["completionState"].toString() == "pending" &&
                  result["operation"].toString().isNotEmpty() && !bool(result["documentChanged"]) &&
                  controller.revision == revision,
              "Accepted I/O request is pending before native preparation/canonical commit");
    };
    try
    {
        std::vector<juce::PluginDescription> types;
        check(PluginCatalog::scanOne(binary, types).wasOk() && types.size() == 1 &&
                  types.front().name == "VibeRa Native Bus Choice Fixture" && !types.front().isInstrument,
              "Exact scanned original SDK bus-choice fixture, no mock descriptor");
        auto project = Project::empty();
        project.channels.resize(1);
        project.channels.front().source = "builtin:bass";
        project.channels.front().mixer = 1;
        for (auto &pattern : project.patterns)
            pattern.notes.clear();
        project.channels.front().gain = .02f; // Literal14x selected fixture output remains below clipping.
        project.patterns.front().notes = {{project.allocate(), project.channels.front().id, 0, 1, 60, .8f}};
        project.placements = {{project.allocate(), project.patterns.front().id, 0, 0, 4, 0}};
        const auto id = project.allocate();
        project.mixer[1].effects.push_back(
            {id, types.front().name, types.front().createXml()->toString(), {}, false, false});
        controller.replace(std::move(project));
        selectedPattern = controller.project.patterns.front().id;
        selectedChannel = controller.project.channels.front().id;
        selectedInsert = 1;
        patternMode = false;
        auto ready = audio.rebuild(controller.project, patternMode, selectedPattern);
        const auto start = juce::Time::getMillisecondCounter();
        while (AudioEngine::preparationPending(ready) &&
               juce::uint32(juce::Time::getMillisecondCounter() - start) < 15000)
        {
            audio.reclaimPluginHosts();
            dispatchMessages(2);
            ready = audio.rebuild(controller.project, patternMode, selectedPattern);
        }
        check(ready.wasOk(), "Actual initial native fixture projection prepares");
        const auto defer = juce::Time::getMillisecondCounter();
        while (!audio.canDeferChannelRouting() &&
               juce::uint32(juce::Time::getMillisecondCounter() - defer) < 3000)
        {
            audio.reclaimPluginHosts();
            dispatchMessages(2);
        }
        CommandArgs route;
        route.channel = selectedChannel;
        route.mixer = 2;
        const auto routed = routingCoordinator.assign(route, {patternMode, selectedPattern}, false);
        check(routed.accepted() && audio.documentProjectionDeferred() && !audio.projectionDiagnostics().edit,
              "Canonical route enters actual no-device native-control projection");
        const auto drain = juce::Time::getMillisecondCounter();
        while (!audio.deferredWorkerPreparationIdle() &&
               juce::uint32(juce::Time::getMillisecondCounter() - drain) < 5000)
        {
            audio.reclaimPluginHosts();
            dispatchMessages(2);
        }
        check(audio.deferredWorkerPreparationIdle(), "Initial source preparation actually drained");
        route.mixer = 1;
        const auto returned = routingCoordinator.assign(route, {patternMode, selectedPattern}, false);
        check(returned.accepted() && audio.documentProjectionDeferred() &&
                  !audio.projectionDiagnostics().edit &&
                  controller.project.channel(selectedChannel)->mixer == 1,
              "Canonical deferred return routes authored bass THROUGH selected effect insert before "
              "controls/export");
        needsRebuild = false;
        engineRevision = controller.revision;
        const auto initial = buses(id, {});
        const auto tools = mcpTools();
        bool discovered = false;
        if (const auto *list = tools.getArray())
            for (const auto &tool : *list)
                if (tool["name"].toString() == "vibera_plugin")
                    if (const auto *actions = tool["inputSchema"]["properties"]["action"]["enum"].getArray())
                        for (const auto &action : *actions)
                            discovered |= action.toString() == "buses";
        check(discovered, "Actual MCP discovery advertises the native bus readback action");

        stage = "actual-component-menu-main-input";
        juce::PopupMenu menu;
        int nextItem = 1;
        addPluginIoMenu(menu, id, nextItem);
        std::function<void()> action;
        for (juce::PopupMenu::MenuItemIterator item(menu, true); item.next();)
            if (item.getItem().text.startsWith("Input one [1]"))
            {
                check(item.getItem().isEnabled && bool(item.getItem().action),
                      "Actual enabled bus menu action");
                action = item.getItem().action;
            }
        check(bool(action), "Actual native bus label exposes practical component menu choice");
        const auto firstRevision = controller.revision;
        lastStructuralOperation.clear();
        action();
        const auto firstOperation = lastStructuralOperation;
        pending(mixerChainReadback(firstOperation), firstRevision);
        const auto applied = finish("vibera_mixerchain", mixerChainReadback(firstOperation), "completed");
        check(bool(applied["documentChanged"]) && bool(applied["undoable"]) &&
                  applied["audioState"].toString() == "deferredNoDevice" &&
                  !bool(applied["nativeRecoveryRequired"]) && controller.revision == firstRevision + 1,
              "Native prepared menu change commits exactly one undoable model transition");
        const auto first = buses(id, {1, 0, 2, 0});
        check(first != initial && first->runtimeIdentity() != initial->runtimeIdentity(),
              "I/O Apply owns a new real native worker");
        const auto captured = controller.project.plugin(id)->state;
        check(captured.isNotEmpty(), "Actual acknowledged opaque state retained in canonical I/O history");

        stage = "actual-UI-Undo-MCP-Redo";
        lastMixerChainHistoryOperation.clear();
        undoButton.onClick();
        finish("vibera_mixerchain", mixerChainReadback(lastMixerChainHistoryOperation), "completed");
        const auto undone = buses(id, {});
        check(undone != first && undone->recoveryState() == captured &&
                  controller.project.plugin(id)->state == captured,
              "UI Undo restores exact IO and fresh captured native state");
        auto history = object();
        put(history, "command", "edit.redo");
        const auto redone = call("vibera_command", history);
        finish("vibera_mixerchain", redone, "completed");
        const auto redoHost = buses(id, {1, 0, 2, 0});
        check(redoHost != undone && redoHost->recoveryState() == captured &&
                  controller.project.plugin(id)->state == captured,
              "MCP Redo uses exact real history identity and native state");

        stage = "equivalent-MCP-nonzero-output-mono-mapping";
        const PluginMainIo chosen{2, 1, 1, 3};
        const auto revision = controller.revision;
        const auto request = call("vibera_command", ioArgs(id, chosen));
        pending(request, revision);
        finish("vibera_mixerchain", request, "completed");
        buses(id, chosen);

        stage = "armed-recording-refusal";
        const auto baseline = controller.project.state().toXmlString();
        const auto baselineRevision = controller.revision;
        const auto baselineUndo = controller.undo.getUndoDescriptions();
        const auto baselineRedo = controller.undo.getRedoDescriptions();
        const auto current = audio.parameterHost(id);
        record.onClick();
        check(recordArmed && !midiTaking, "Actual UI Record arms without fake take state");
        bool refused = false;
        try
        {
            const auto result = call("vibera_command", ioArgs(id, {}));
            refused = result["completionState"].toString() == "failed" &&
                      result["code"].toString() == "effectChain.controlBusy";
        }
        catch (const McpFailure &error)
        {
            refused = error.code == "effectChain.controlBusy";
            auto row = object();
            put(row, "recordingRefusal", error.code);
            outcomes.add(row);
        }
        record.onClick();
        check(refused && !recordArmed && controller.project.state().toXmlString() == baseline &&
                  controller.revision == baselineRevision && audio.parameterHost(id) == current &&
                  controller.undo.getUndoDescriptions() == baselineUndo &&
                  controller.undo.getRedoDescriptions() == baselineRedo,
              "Armed recording refuses before model/history/native worker mutation");

        stage = "cancel-before-commit";
        const auto cancellation = call("vibera_command", ioArgs(id, {}));
        pending(cancellation, baselineRevision);
        auto cancel = object();
        put(cancel, "action", "cancel");
        put(cancel, "operation", cancellation["operation"]);
        finish("vibera_mixerchain", call("vibera_mixerchain", cancel), "cancelled");
        check(controller.project.state().toXmlString() == baseline &&
                  controller.revision == baselineRevision && audio.parameterHost(id) == current &&
                  current->alive() && controller.undo.getUndoDescriptions() == baselineUndo &&
                  controller.undo.getRedoDescriptions() == baselineRedo,
              "Actual accepted cancellation drains native preparation and preserves source/history");

        stage = "actual-unsupported-surround-output-failure";
        const auto invalid = call("vibera_command", ioArgs(id, {2, 2, 1, 3}));
        pending(invalid, baselineRevision);
        const auto failed = finish("vibera_mixerchain", invalid, "failed");
        check(
            (failed["code"].toString() + " " + failed["message"].toString()).contains("pluginIo.") &&
                !bool(failed["documentChanged"]) && !bool(failed["nativeRecoveryRequired"]) &&
                controller.project.state().toXmlString() == baseline &&
                controller.revision == baselineRevision && audio.parameterHost(id) == current &&
                current->alive() && controller.undo.getUndoDescriptions() == baselineUndo &&
                controller.undo.getRedoDescriptions() == baselineRedo,
            "Actual SDK rejects stereo request on surround-only output before canonical/native publication");
        buses(id, chosen);

        stage = "actual-MCP-save-open-native-persistence";
        const auto saved = folder.getChildFile("native-main-io.vibra");
        check(!saved.exists(), "Fresh owned project output; no overwrite of prior evidence");
        auto save = object();
        put(save, "action", "save");
        put(save, "path", saved.getFullPathName());
        put(save, "overwrite", false);
        finish("vibera_project", call("vibera_project", save), "completed");
        const auto savedState = controller.project.state();
        const auto oldIdentity = controller.projectIdentity;
        auto open = object();
        put(open, "action", "open");
        put(open, "path", saved.getFullPathName());
        put(open, "discardChanges", true);
        finish("vibera_project", call("vibera_project", open), "completed");
        const auto opened = juce::Time::getMillisecondCounter();
        while ((needsRebuild || !audio.preparedPluginHost(controller.project, id)) &&
               juce::uint32(juce::Time::getMillisecondCounter() - opened) < 15000)
            service();
        check(saved.existsAsFile() && controller.projectIdentity != oldIdentity &&
                  controller.project.state().isEquivalentTo(savedState) && !needsRebuild,
              "Actual Save/Open retains exact state and I/O in a new document session");
        const auto reopened = buses(id, chosen);
        check(reopened != current && reopened->recoveryState() == controller.project.plugin(id)->state,
              "Reopen actually restores saved native state and negotiated bus selection");
        stage = "actual-immediate-reopen-export-live-native-recovery";
        const auto exportSession = controller.projectIdentity;
        // Open completion is document-only. Do NOT service or wait for its
        // new live Hosts before export acquires actual capture/offline ownership.
        const auto reopenedAgain = call("vibera_project", open);
        check(reopenedAgain["completionState"].toString() == "completed" &&
                  controller.projectIdentity != exportSession && needsRebuild &&
                  !audio.preparedPluginHost(controller.project, id),
              "Fresh Open deliberately has real pending native access before immediate export");
        const auto immediateSession = controller.projectIdentity;
        const auto wav = folder.getChildFile("native-main-io-immediate-open.wav");
        check(!wav.exists(), "Fresh owned WAV output");
        auto exportRequest = object();
        put(exportRequest, "action", "export");
        put(exportRequest, "path", wav.getFullPathName());
        put(exportRequest, "overwrite", false);
        const auto exported = call("vibera_project", exportRequest);
        check(exported["completionState"].toString() == "pending" &&
                  exported["operation"].toString().isNotEmpty(),
              "Immediate canonical export is a real pending file job");
        finish("vibera_project", exported, "completed");
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));
        check(reader && reader->sampleRate == 48000 && reader->numChannels == 2 &&
                  reader->lengthInSamples == 96000,
              "Actual immediate export writes exact four-beat stereo Song WAV");
        juce::AudioBuffer<float> pcm(2, 96000);
        check(reader->read(&pcm, 0, pcm.getNumSamples(), 0, true, true) &&
                  pcm.getMagnitude(0, pcm.getNumSamples()) > .001f,
              "Original authored builtin bass produces genuine nonzero offline PCM");
        offlineWavExported = true;
        const auto liveStarted = juce::Time::getMillisecondCounter();
        while ((needsRebuild || !audio.preparedPluginHost(controller.project, id)) &&
               juce::uint32(juce::Time::getMillisecondCounter() - liveStarted) < 15000)
            service();
        check(!needsRebuild && !renderWasActive && !afterPluginState && !pendingExport &&
                  !audio.rendering() && controller.projectIdentity == immediateSession &&
                  controller.project.state().isEquivalentTo(savedState),
              "Export teardown preserves pending live preparation until exact current native access returns");
        const auto afterExport = buses(id, chosen);
        check(afterExport != reopened && afterExport->alive() &&
                  afterExport->recoveryState() == controller.project.plugin(id)->state,
              "Reopened live Host is genuinely current with exact chosen buses/state after actual export");

        stage = "actual-Settings-audioIo-parent";
        auto settingsRequest = object();
        put(settingsRequest, "action", "wrapper");
        put(settingsRequest, "plugin", juce::int64(id));
        put(settingsRequest, "instance", afterExport->runtimeIdentity());
        put(settingsRequest, "view", "settings");
        const auto settings = finish("vibera_plugin", call("vibera_plugin", settingsRequest), "completed");
        const auto window = settings["editorState"];
        check(
            settings["kind"].toString() == "editor" && bool(settings["nativeCompletionKnown"]) &&
                bool(window["known"]) && bool(window["exists"]) && bool(window["visible"]) &&
                window["availability"].toString() == "controls_only" &&
                window["nativeInterfaceAvailable"].isBool() && !bool(window["nativeInterfaceAvailable"]) &&
                window["wrapperView"].toString() == "settings" && bool(window["wrapperReady"]),
            "Editorless SDK fixture has a real acknowledged Settings peer, never a native attachment claim");
        const auto audioIoRequest = [&](const PluginMainIo &io, const juce::String &instance)
        {
            auto args = object();
            put(args, "action", "audioIo");
            put(args, "plugin", juce::int64(id));
            put(args, "instance", instance);
            put(args, "pluginIo", ioArgs(id, io)["args"]["pluginIo"]);
            put(args, "projectIdentity", controller.projectIdentity);
            put(args, "revision", juce::int64(controller.revision));
            return args;
        };
        const auto parentRevision = controller.revision;
        const auto parentProject = controller.projectIdentity;
        const auto parent = call("vibera_plugin", audioIoRequest({}, afterExport->runtimeIdentity()));
        check(parent["completionState"].toString() == "pending" && parent["kind"].toString() == "audioIo" &&
                  parent["operation"].toString().isNotEmpty(),
              "Settings audioIo is an actual pending parent operation, not completed admission");
        const auto completed = finish("vibera_plugin", parent, "completed");
        const auto child = completed["structuralOperation"];
        check(completed["operation"].toString() == parent["operation"].toString() &&
                  completed["requestedInstance"].toString() == afterExport->runtimeIdentity() &&
                  completed["sourceProjectIdentity"].toString() == parentProject &&
                  int(completed["requestedIo"]["inputBus"]) == 0 &&
                  int(completed["requestedIo"]["outputBus"]) == 0 &&
                  int(completed["requestedIo"]["inputChannels"]) == 2 &&
                  int(completed["requestedIo"]["monoSource"]) == 0 && bool(completed["historyCommitted"]) &&
                  bool(completed["documentChanged"]) && bool(completed["undoable"]) &&
                  completed["canonicalOperation"].toString().isNotEmpty() &&
                  child["operation"].toString() == completed["canonicalOperation"].toString() &&
                  child["completionState"].toString() == "completed" &&
                  child["audioState"].toString() == "deferredNoDevice" &&
                  !bool(child["nativeRecoveryRequired"]) && controller.revision == parentRevision + 1,
              "Durable Settings parent observes exactly one committed canonical child across replacement");
        int matchingClosures = 0;
        if (const auto *closures = child["editorClosures"].getArray())
            for (const auto &closure : *closures)
                if (juce::int64(closure["plugin"]) == juce::int64(id) &&
                    closure["instance"].toString() == afterExport->runtimeIdentity())
                {
                    ++matchingClosures;
                    check(juce::int64(closure["serial"]) > 0 && closure["outcome"].toString() == "closed" &&
                              bool(closure["nativeCompletionKnown"]) && !bool(closure["windowExists"]),
                          "Exact old Settings window has an actual owned terminal close acknowledgement");
                }
        check(matchingClosures == 1, "Exactly one old-wrapper close receipt precedes canonical replacement");
        const auto parentHost = buses(id, {});
        check(parentHost != afterExport && parentHost->runtimeIdentity() != afterExport->runtimeIdentity() &&
                  parentHost->recoveryState() == controller.project.plugin(id)->state,
              "Settings I/O replaces the real worker while preserving original slot and fresh native state");

        stage = "Settings-audioIo-history";
        lastMixerChainHistoryOperation.clear();
        undoButton.onClick();
        finish("vibera_mixerchain", mixerChainReadback(lastMixerChainHistoryOperation), "completed");
        const auto parentUndo = buses(id, chosen);
        check(parentUndo != parentHost,
              "Settings I/O UI Undo genuinely restores the previous negotiated layout");
        finish("vibera_mixerchain", call("vibera_command", history), "completed");
        const auto parentRedo = buses(id, {});
        check(parentRedo != parentUndo,
              "Settings I/O MCP Redo genuinely restores the committed default layout");
        auto parentQuery = object();
        put(parentQuery, "action", "job");
        put(parentQuery, "operation", parent["operation"]);
        const auto durable = call("vibera_plugin", parentQuery);
        check(durable["completionState"].toString() == "completed" && bool(durable["historyCommitted"]) &&
                  durable["requestedInstance"].toString() == afterExport->runtimeIdentity() &&
                  durable["canonicalOperation"].toString() == completed["canonicalOperation"].toString(),
              "Original Settings parent remains observable after both subsequent worker replacements");

        stage = "Settings-audioIo-noop-and-refusals";
        const auto noopState = controller.project.state().toXmlString();
        const auto noopRevision = controller.revision;
        const auto noopUndo = controller.undo.getUndoDescriptions();
        const auto noopRedo = controller.undo.getRedoDescriptions();
        const auto noop =
            finish("vibera_plugin", call("vibera_plugin", audioIoRequest({}, parentRedo->runtimeIdentity())),
                   "completed");
        check(!bool(noop["historyCommitted"]) && !bool(noop["documentChanged"]) && !bool(noop["undoable"]) &&
                  noop["canonicalOperation"].toString().isEmpty() && audio.parameterHost(id) == parentRedo &&
                  controller.project.state().toXmlString() == noopState &&
                  controller.revision == noopRevision && controller.undo.getUndoDescriptions() == noopUndo &&
                  controller.undo.getRedoDescriptions() == noopRedo,
              "Current-instance Settings no-op leaves exact model, history cursor and real worker unchanged");
        for (int invalidCase = 0; invalidCase < 2; ++invalidCase)
        {
            bool rejected = false;
            try
            {
                (void)call("vibera_plugin",
                           audioIoRequest(invalidCase == 0 ? PluginMainIo{} : PluginMainIo{31, 0, 2, 0},
                                          invalidCase == 0 ? afterExport->runtimeIdentity()
                                                           : parentRedo->runtimeIdentity()));
            }
            catch (const McpFailure &error)
            {
                rejected = error.code == (invalidCase == 0 ? "stale_instance" : "pluginIo.invalidArguments");
                auto evidence = object();
                put(evidence, "audioIoRefusal", error.code);
                outcomes.add(evidence);
            }
            check(rejected && controller.project.state().toXmlString() == noopState &&
                      controller.revision == noopRevision && audio.parameterHost(id) == parentRedo &&
                      parentRedo->alive() && controller.undo.getUndoDescriptions() == noopUndo &&
                      controller.undo.getRedoDescriptions() == noopRedo,
                  "Stale Settings instance/nonexistent native bus refuse before parent mutation or worker "
                  "replacement");
        }
        check(retain(true, {}), "Retain complete actual application I/O acceptance");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        const auto failure = juce::String(error.what());
        (void)retain(false, failure);
        // Preserve operation ownership through the normal cancellation/drain
        // service; asynchronous callbacks never capture this stack.
        const auto began = juce::Time::getMillisecondCounter();
        while ((mixerChainBusy() || afterPluginState) &&
               juce::uint32(juce::Time::getMillisecondCounter() - began) < 15000)
        {
            try
            {
                auto cancel = object();
                put(cancel, "action", "cancel");
                put(cancel, "operation", mixerChainReadback()["state"]["activeOperation"]);
                if (mixerChainBusy())
                    (void)mixerChainOperation(cancel);
                service();
            }
            catch (...)
            {
                break;
            }
        }
        return juce::Result::fail(stage + ": " + failure);
    }
}
