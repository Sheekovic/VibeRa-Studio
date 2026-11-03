// SPDX-License-Identifier: AGPL-3.0-or-later
// Original actual application/SDK detector assignment acceptance. Included in
// verifyStudioChannelRouting after the shared chain/lifecycle helpers and before
// the deliberately missing-callback watchdog. No hardware/listening claim.
if (detectorFixture != juce::File{})
{
    stage = "actual-persisted-native-detector-assignment";
    check(juce::File::isAbsolutePath(detectorFixture.getFullPathName()) && detectorFixture.existsAsFile(),
          "Explicit original detector binary exists");
    std::vector<juce::PluginDescription> detectorDescriptions;
    const auto scanned = PluginCatalog::scanOne(detectorFixture, detectorDescriptions);
    check(scanned.wasOk(), "Isolated actual SDK detector scan completes");
    const auto description = std::find_if(detectorDescriptions.begin(), detectorDescriptions.end(),
                                          [](const auto &entry)
                                          {
                                              return entry.name == "VibeRa Mixer Latency Fixture" &&
                                                     !entry.isInstrument && entry.pluginFormatName == "VST3";
                                          });
    check(description != detectorDescriptions.end(), "Exact independently authored detector effect selected");
    const auto detectorDescriptor = description->createXml()->toString();
    check(controller.project.channels.size() == 1 && controller.project.channel(selectedChannel) &&
              !controller.project.channel(selectedChannel)->instrument &&
              audio.channelSample(selectedChannel),
          "Detector stimulus is the genuine original sampler Channel");
    if (controller.project.channel(selectedChannel)->mixer == 2)
    {
        check(route(3)["completionState"].toString() == "pending", "Prepare distinct actual detector source");
        tick();
        check(!routingPending() && controller.project.channel(selectedChannel)->mixer == 3,
              "Actual callback selects source outside target2");
    }
    const int detectorSource = controller.project.channel(selectedChannel)->mixer;
    check(detectorSource > 0 && detectorSource != 2 && mixerInsert(controller.project, 2),
          "Source and target are distinct existing non-Master stable inserts");
    for (const auto &row : controller.project.mixer)
        for (const auto &send : row.routes)
            check(send.target != 2, "No audible send contributes main audio to detector target2");
    if (mixerInsert(controller.project, 2)->effects.empty())
        finishChain(chainCall(chainRequest("paste", 2)), "completed");
    const auto originalTarget = mixerInsert(controller.project, 2)->effects.front();
    juce::PluginDescription originalDescription;
    const auto originalXml = juce::XmlDocument::parse(originalTarget.descriptor);
    check(originalXml && originalDescription.loadFromXml(*originalXml) &&
              originalDescription.name == "VibeRa Parameter Fixture" && !originalDescription.isInstrument,
          "Replace an actual existing SDK target on2, independently of its user-visible slot label");
    const auto command = [&](const char *id, const juce::var &values)
    {
        auto request = object();
        put(request, "command", id);
        put(request, "args", values);
        put(request, "projectIdentity", controller.projectIdentity);
        put(request, "revision", juce::int64(controller.revision));
        return invoke("vibera_command", request);
    };
    auto replaceValues = object();
    put(replaceValues, "target", juce::int64(originalTarget.id));
    put(replaceValues, "text", detectorDescriptor);
    finishChain(command("plugin.effect.replace", replaceValues), "completed");
    const auto detectorId = mixerInsert(controller.project, 2)->effects.front().id;
    check(detectorId != originalTarget.id && !controller.project.plugin(originalTarget.id) &&
              controller.project.plugin(detectorId)->descriptor == detectorDescriptor &&
              !controller.project.plugin(detectorId)->detector &&
              !controller.project.plugin(detectorId)->bypass,
          "Canonical Replace creates a fresh default-state native detector with stable destination slot");
    const auto readAmount = [&](const std::shared_ptr<PluginHost> &host, bool set)
    {
        check(host && host->alive(), "Actual detector worker is healthy");
        const auto serial = host->requestParameterCatalogue();
        PluginParameterCatalogue catalogue;
        bool acknowledged = false;
        const auto began = juce::Time::getMillisecondCounter();
        while (!(acknowledged = host->readParameterCatalogue(catalogue) && catalogue.serial == serial) &&
               juce::Time::getMillisecondCounter() - began < 5000)
        {
            host->service();
            tick();
            dispatchMessages(2);
        }
        const auto amount = std::find_if(catalogue.parameters.begin(), catalogue.parameters.end(),
                                         [](const auto &entry) { return entry.id == 71; });
        check(serial && acknowledged && !catalogue.error && amount != catalogue.parameters.end(),
              "Real sparse native parameter71 metadata received");
        if (!set)
            return amount->value;
        const auto request = host->setParameter(71, 1.f, catalogue.generation);
        PluginParameterResult response;
        acknowledged = false;
        const auto sent = juce::Time::getMillisecondCounter();
        while (!(acknowledged = host->readParameterResult(request, response)) &&
               juce::Time::getMillisecondCounter() - sent < 5000)
        {
            host->service();
            tick();
            dispatchMessages(2);
        }
        check(request && acknowledged && response.result == 0 && response.value == 1.f,
              "Actual native sidechainAmount1 acknowledgement");
        return response.value;
    };
    auto noAuxHost = audio.parameterHost(detectorId);
    check(readAmount(noAuxHost, false) == 0.f, "New effect genuinely has default native amount0");
    (void)readAmount(noAuxHost, true);
    check(noAuxHost->busLayout().auxiliaryInputBus == -1 && noAuxHost->busLayout().auxiliaryChannels == 0,
          "No detector means actual negotiated auxiliary bus is absent");
    audio.playStop();
    tick();
    juce::Array<juce::var> detectorReceipts;
    const auto probe = [&](const char *name, bool connected)
    {
        check(audio.playing(), "Detector completion recovers the previously playing transport");
        audio.setMidiTarget(selectedChannel);
        audio.typingInput.handleIncomingMidiMessage(nullptr,
                                                    juce::MidiMessage::noteOn(1, 64, juce::uint8(110)));
        float sourcePeak = 0, targetPeak = 0;
        bool sourceFresh = true, targetFresh = true;
        for (int n = 0; n < 32; ++n)
        {
            tick();
            const auto source = audio.mixerMeter(detectorSource), target = audio.mixerMeter(2);
            sourcePeak = std::max(sourcePeak, source.peak[0]);
            targetPeak = std::max(targetPeak, target.peak[0]);
            sourceFresh = sourceFresh && !source.stale;
            targetFresh = targetFresh && !target.stale;
            dispatchMessages(2);
        }
        audio.typingInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 64));
        tick();
        auto receipt = object();
        put(receipt, "phase", name);
        put(receipt, "sourceMixer", detectorSource);
        put(receipt, "effect", juce::int64(detectorId));
        put(receipt, "sourcePeak", sourcePeak);
        put(receipt, "targetPeak", targetPeak);
        put(receipt, "sourceFresh", sourceFresh);
        put(receipt, "targetFresh", targetFresh);
        detectorReceipts.add(receipt);
        check(folder.getChildFile("detector-native-measurements.json")
                  .replaceWithText(juce::JSON::toString(juce::var(detectorReceipts))),
              "Retain actual detector path evidence before assertions");
        // An actual nonproducing target can legitimately have a dormant meter.
        // Connected output must be measured fresh, never an old retained peak.
        check(sourceFresh && std::isfinite(sourcePeak) && sourcePeak > .0001f && std::isfinite(targetPeak) &&
                  (connected ? targetFresh && targetPeak > .0001f : targetPeak == 0.f),
              "Actual post-FX target2 is silent without aux and audible only with assigned detector input");
    };
    const auto assign = [&](int bus, int channels, bool clear = false)
    {
        auto values = object();
        put(values, "target", juce::int64(detectorId));
        juce::var detector;
        if (!clear)
        {
            detector = object();
            put(detector, "sourceMixer", detectorSource);
            put(detector, "inputBus", bus);
            put(detector, "channels", channels);
        }
        put(values, "detector", detector);
        return command("plugin.effect.sidechain", values);
    };
    const auto verifyLayout = [&](int channels)
    {
        auto host = audio.parameterHost(detectorId);
        check(
            host && host->alive() && controller.project.plugin(detectorId) &&
                controller.project.plugin(detectorId)->detector &&
                controller.project.plugin(detectorId)->detector->sourceMixer == detectorSource &&
                controller.project.plugin(detectorId)->detector->inputBus == 1 &&
                controller.project.plugin(detectorId)->detector->channels == channels &&
                host->busLayout().auxiliaryInputBus == 1 && host->busLayout().auxiliaryChannels == channels &&
                readAmount(host, false) == 1.f,
            "Persisted same-ID assignment matches actual negotiated mono/stereo worker and captured amount1");
        return host;
    };
    probe("no-detector", false);
    const auto armedState = controller.project.state().toXmlString();
    const auto armedRevision = controller.revision;
    const auto takeBefore = midiTakeState();
    bool armedRefused = false;
    {
        const juce::ScopedValueSetter<bool> armed(recordArmed, true);
        try
        {
            const auto result = assign(1, 1);
            armedRefused = result["completionState"].toString() == "failed" &&
                           result["code"].toString() == "effectChain.controlBusy";
        }
        catch (const std::exception &error)
        {
            armedRefused = juce::String(error.what()).contains("effectChain.controlBusy");
        }
        check(recordArmed && !midiTaking, "Recording arm remains owned and no take is implicitly started");
    }
    check(armedRefused && controller.revision == armedRevision &&
              controller.project.state().toXmlString() == armedState &&
              juce::JSON::toString(midiTakeState()) == juce::JSON::toString(takeBefore),
          "Armed structural refusal preserves exact document/history and existing take readback");
    finishChain(assign(1, 1), "completed");
    auto monoHost = verifyLayout(1);
    check(monoHost != noAuxHost, "Layout assignment creates a genuinely different worker");
    probe("mono-assigned", true);
    finishChainHistory(true);
    check(!controller.project.plugin(detectorId)->detector, "UI Undo clears exact same effect assignment");
    probe("mono-ui-undo", false);
    finishChainHistory(false);
    (void)verifyLayout(1);
    probe("mono-mcp-redo", true);
    finishChain(assign(1, 2), "completed");
    auto stereoHost = verifyLayout(2);
    check(stereoHost != monoHost, "Mono-to-stereo negotiation creates a genuinely different worker");
    probe("stereo-assigned", true);
    {
        // A detector project must not turn ordinary send-level history into a
        // structural rebuild. Exercise the actual UI/MCP history admission.
        const auto baseline = audio.projectionDiagnostics();
        const auto nativeHost = audio.parameterHost(detectorId);
        const auto *edge = mixerRoute(controller.project, detectorSource, 0);
        check(edge != nullptr, "Detector source retains its existing manual Master send");
        const float beforeLevel = edge->gain;
        auto values = object();
        put(values, "mixer", detectorSource);
        put(values, "target", 0);
        put(values, "value", .5);
        const auto changed = command("mixer.send", values);
        check(changed["completionState"].toString() == "completed" && !mixerChainApplying(),
              "Ordinary same-edge send change is canonical without a structural job");
        tick();
        timerCallback();
        undoButton.onClick();
        tick();
        timerCallback();
        check(!mixerChainApplying() && mixerRoute(controller.project, detectorSource, 0)->gain == beforeLevel,
              "Ordinary send Undo on aux graph never starts structural capture/rebuild");
        const auto redone = history("edit.redo");
        tick();
        timerCallback();
        const auto after = audio.projectionDiagnostics();
        check(redone["completionState"].toString() == "completed" && !mixerChainApplying() &&
                  mixerRoute(controller.project, detectorSource, 0)->gain == .5f &&
                  after.edit == baseline.edit && after.playbackContext == baseline.playbackContext &&
                  after.replacements == baseline.replacements &&
                  audio.parameterHost(detectorId) == nativeHost && audio.playing(),
              "Aux graph send Undo/Redo preserves actual Edit/context/worker and playing transport");
        undoButton.onClick();
        tick();
        timerCallback();
    }
    // General route transactions are selected BEFORE the aux-unsupported
    // retained planner. They may interrupt, but must actually recover with
    // current captured state, one accurate history edge and healthy workers.
    {
        const auto originalRouting = controller.project.channel(selectedChannel)->mixer;
        finishChain(route(4), "completed");
        check(controller.project.channel(selectedChannel)->mixer == 4 &&
                  controller.project.plugin(detectorId)->detector->sourceMixer == detectorSource,
              "Aux project Channel assignment changes only the requested source route");
        verifyRecoveredSound();
        finishChainHistory(true);
        check(controller.project.channel(selectedChannel)->mixer == originalRouting,
              "Bounded routing UI Undo restores the original Channel assignment");
        (void)verifyLayout(2);
        probe("bounded-route-ui-undo", true);
        finishChainHistory(false);
        check(controller.project.channel(selectedChannel)->mixer == 4,
              "Bounded routing MCP Redo restores the exact destination");
        verifyRecoveredSound();
        finishChainHistory(true);
        probe("bounded-route-final-undo", true);
        auto routeValues = object();
        put(routeValues, "mixer", detectorSource);
        put(routeValues, "target", 5);
        put(routeValues, "enabled", true);
        finishChain(command("mixer.route", routeValues), "completed");
        check(mixerRoute(controller.project, detectorSource, 5) != nullptr,
              "Actual aux-source bus send is enabled through the general native graph");
        probe("bounded-bus-route", true);
        finishChainHistory(true);
        check(!mixerRoute(controller.project, detectorSource, 5),
              "One route Undo removes only the added edge");
        probe("bounded-bus-route-undo", true);
        finishChainHistory(false);
        check(mixerRoute(controller.project, detectorSource, 5) != nullptr,
              "One route Redo restores the added edge");
        finishChainHistory(true);
        const auto originalNeighbours = mixerInsert(controller.project, 2)->effects;
        check(!originalNeighbours.empty() && originalNeighbours.front().id == detectorId,
              "Existing detector and every retained neighbouring slot are identified before append");
        const auto retainedPrefix = [&](const std::vector<PluginSpec> &chain, std::size_t start)
        {
            if (chain.size() < start + originalNeighbours.size())
                return false;
            for (std::size_t i = 0; i < originalNeighbours.size(); ++i)
                if (chain[start + i].id != originalNeighbours[i].id ||
                    chain[start + i].descriptor != originalNeighbours[i].descriptor ||
                    chain[start + i].detector != originalNeighbours[i].detector ||
                    chain[start + i].bypass != originalNeighbours[i].bypass ||
                    chain[start + i].wet != originalNeighbours[i].wet)
                    return false;
            return true;
        };
        auto appendValues = object();
        put(appendValues, "mixer", 2);
        put(appendValues, "text", originalTarget.descriptor);
        finishChain(command("plugin.effect.insert", appendValues), "completed");
        check(mixerInsert(controller.project, 2)->effects.size() == originalNeighbours.size() + 1 &&
                  retainedPrefix(mixerInsert(controller.project, 2)->effects, 0),
              "Actual auxiliary chain append preserves first detector identity/layout");
        const auto appended = mixerInsert(controller.project, 2)->effects.back().id;
        probe("aux-append", true);
        auto moveValues = object();
        put(moveValues, "target", juce::int64(appended));
        put(moveValues, "lane", 0);
        finishChain(command("plugin.effect.move", moveValues), "completed");
        check(mixerInsert(controller.project, 2)->effects.front().id == appended &&
                  retainedPrefix(mixerInsert(controller.project, 2)->effects, 1),
              "Actual aux chain reorder retains both native identities");
        probe("aux-reordered", true);
        auto removeValues = object();
        put(removeValues, "target", juce::int64(appended));
        finishChain(command("plugin.remove", removeValues), "completed");
        check(!controller.project.plugin(appended) && controller.project.plugin(detectorId)->detector &&
                  mixerInsert(controller.project, 2)->effects.size() == originalNeighbours.size() &&
                  retainedPrefix(mixerInsert(controller.project, 2)->effects, 0),
              "Actual aux chain removal preserves every surviving neighbour, detector and routing");
        probe("aux-removed-neighbour", true);
        finishChainHistory(true);
        check(controller.project.plugin(appended), "Aux removal Undo restores captured neighbour");
        finishChainHistory(false);
        check(!controller.project.plugin(appended), "Aux removal Redo preserves captured survivor");
        probe("aux-remove-redone", true);
    }
    finishChain(assign(1, 2, true), "completed");
    check(!controller.project.plugin(detectorId)->detector &&
              audio.parameterHost(detectorId)->busLayout().auxiliaryInputBus == -1,
          "Explicit null disconnects actual auxiliary bus without changing canonical effect ID");
    probe("cleared", false);
    finishChainHistory(true);
    (void)verifyLayout(2);
    probe("clear-ui-undo", true);
    finishChainHistory(false);
    probe("clear-mcp-redo", false);
    finishChain(assign(1, 2), "completed");
    stereoHost = verifyLayout(2);
    {
        // Exercise the wrapper's exact-effect parent over the SAME active
        // canonical detector service; restore stereo before all old oracles.
        const auto previousStage = stage;
        stage = "actual-wrapper-sidechain-disconnect-reconnect";
        juce::Array<juce::var> wrapperReceipts;
        const auto finishWrapper = [&](juce::var current)
        {
            const auto operation = current["operation"].toString();
            check(operation.isNotEmpty() && current["completionState"].toString() == "pending",
                  "Wrapper editor/chain admission owns an actual pending parent token");
            const auto began = juce::Time::getMillisecondCounter();
            while (current["completionState"].toString() == "pending" &&
                   juce::uint32(juce::Time::getMillisecondCounter() - began) < 15000)
            {
                tick();
                timerCallback();
                dispatchMessages(5);
                auto query = object();
                put(query, "action", "job");
                put(query, "operation", operation);
                current = invoke("vibera_plugin", query);
            }
            wrapperReceipts.add(current);
            auto evidence = object();
            put(evidence, "operations", juce::var(wrapperReceipts));
            put(evidence, "nativeOsInput", false);
            put(evidence, "hardware", false);
            put(evidence, "listening", false);
            check(folder.getChildFile("detector-wrapper-transactions.json")
                      .replaceWithText(juce::JSON::toString(evidence, true)),
                  "Retain actual wrapper parent/editor/child receipts before terminal assertions");
            check(current["operation"].toString() == operation &&
                      current["completionState"].toString() == "completed",
                  "Exact wrapper parent completes inside the same15s observation bound");
            return current;
        };
        for (bool connected : {false, true})
        {
            const auto beforeHost = audio.parameterHost(detectorId);
            check(beforeHost && beforeHost->alive(), "Actual current detector owner before wrapper request");
            const auto instance = beforeHost->runtimeIdentity();
            auto openSettings = object();
            put(openSettings, "action", "wrapper");
            put(openSettings, "plugin", juce::int64(detectorId));
            put(openSettings, "instance", instance);
            put(openSettings, "view", "settings");
            const auto editor = finishWrapper(invoke("vibera_plugin", openSettings));
            const auto window = editor["editorState"];
            check(editor["kind"].toString() == "editor" && bool(editor["nativeCompletionKnown"]) &&
                      bool(window["known"]) && bool(window["exists"]) && bool(window["visible"]) &&
                      window["availability"].toString() == "controls_only" &&
                      window["nativeInterfaceAvailable"].isBool() &&
                      !bool(window["nativeInterfaceAvailable"]) &&
                      window["wrapperView"].toString() == "settings" && bool(window["wrapperReady"]),
                  "Original editorless detector has an actual acknowledged Settings peer");
            auto request = object();
            put(request, "action", "chain");
            put(request, "chainAction", "sidechain");
            put(request, "plugin", juce::int64(detectorId));
            put(request, "instance", instance);
            put(request, "projectIdentity", controller.projectIdentity);
            put(request, "revision", juce::int64(controller.revision));
            juce::var detector;
            if (connected)
            {
                detector = object();
                put(detector, "sourceMixer", detectorSource);
                put(detector, "inputBus", 1);
                put(detector, "channels", 2);
            }
            put(request, "detector", detector);
            const auto beforeRevision = controller.revision;
            const auto completed = finishWrapper(invoke("vibera_plugin", request));
            const auto child = completed["structuralOperation"];
            check(completed["kind"].toString() == "chain" &&
                      completed["chainAction"].toString() == "sidechain" &&
                      completed["requestedInstance"].toString() == instance && int(completed["mixer"]) == 2 &&
                      completed["canonicalOperation"].toString().isNotEmpty() &&
                      child["operation"].toString() == completed["canonicalOperation"].toString() &&
                      child["completionState"].toString() == "completed" &&
                      child["audioState"].toString() == "active" && !bool(child["nativeRecoveryRequired"]) &&
                      bool(completed["historyCommitted"]) && bool(completed["documentChanged"]) &&
                      bool(completed["undoable"]) && controller.revision == beforeRevision + 1 &&
                      audio.projectionDiagnostics().pluginSourceState == "active" && !needsRebuild &&
                      engineRevision == controller.revision && audio.playing(),
                  "Wrapper detector parent waits for exact one-edge canonical commit and actual source "
                  "recovery");
            int ownCloses = 0;
            if (const auto *closures = completed["editorClosures"].getArray())
                for (const auto &closure : *closures)
                    if (juce::int64(closure["plugin"]) == juce::int64(detectorId) &&
                        closure["instance"].toString() == instance)
                    {
                        ++ownCloses;
                        check(juce::int64(closure["serial"]) > 0 &&
                                  closure["outcome"].toString() == "closed" &&
                                  bool(closure["nativeCompletionKnown"]) && !bool(closure["windowExists"]),
                              "Sidechain parent owns real old Settings-close acknowledgement before "
                              "replacement");
                    }
            check(ownCloses == 1,
                  "Exactly one current detector Settings peer closed through its owned parent");
            const auto afterHost = audio.parameterHost(detectorId);
            check(afterHost && afterHost != beforeHost && afterHost->runtimeIdentity() != instance &&
                      afterHost->alive() && controller.project.plugin(detectorId),
                  "Wrapper detector change replaces the actual worker and retains exact canonical effect ID");
            if (connected)
            {
                stereoHost = verifyLayout(2);
                probe("wrapper-stereo-reconnected", true);
            }
            else
            {
                check(!controller.project.plugin(detectorId)->detector &&
                          afterHost->busLayout().auxiliaryInputBus == -1 &&
                          afterHost->busLayout().auxiliaryChannels == 0 &&
                          readAmount(afterHost, false) == 1.f,
                      "Wrapper null detector disconnects real native aux while preserving captured amount1");
                probe("wrapper-detector-disconnected", false);
            }
        }
        stage = previousStage;
    }
    const auto validState = controller.project.state().toXmlString();
    const auto validRevision = controller.revision;
    const auto invalid = finishChain(assign(31, 2), "failed");
    check(!bool(invalid["documentChanged"]) && controller.revision == validRevision &&
              controller.project.state().toXmlString() == validState &&
              audio.parameterHost(detectorId) == stereoHost && stereoHost->alive(),
          "Unsupported actual bus31 refuses before canonical commit, preserving incumbent worker/model");
    probe("invalid-layout-recovered", true);
    verifyRecoveredSound();

    struct SavedDetector
    {
        bool done = false;
        juce::Result result = juce::Result::fail("Pending");
    };
    const auto saved = std::make_shared<SavedDetector>();
    const auto projectPath = folder.getChildFile("native-detector-roundtrip.vibra");
    saveTo(projectPath,
           [saved](juce::Result result)
           {
               saved->result = result;
               saved->done = true;
           });
    const auto saveStarted = juce::Time::getMillisecondCounter();
    while (!saved->done && juce::Time::getMillisecondCounter() - saveStarted < 12000)
    {
        tick();
        timerCallback();
        dispatchMessages(2);
    }
    Project restored;
    check(saved->done && saved->result.wasOk() && loadProject(projectPath, restored).wasOk() &&
              restored.state().isEquivalentTo(controller.project.state()) &&
              restored.plugin(detectorId)->detector == controller.project.plugin(detectorId)->detector &&
              restored.plugin(detectorId)->state.isNotEmpty(),
          "Real asynchronous save/read/parse retains exact detector and captured opaque native state");
    audio.stop();
    tick();
    // Original offline score fixture derived from the saved project. Only source
    // audible routing is removed, so the sole route to Master is the hosted aux
    // contribution. No private graph injection or fake detector PCM is used.
    auto rendered = restored;
    rendered.placements.clear();
    rendered.audioPlacements.clear();
    for (auto &pattern : rendered.patterns)
        pattern.notes.clear();
    auto &score = rendered.patterns.front();
    score.notes.push_back({rendered.allocate(), selectedChannel, 0, .5, 64, .8f});
    rendered.placements.push_back({rendered.allocate(), score.id, 0, 0, 1, 0});
    auto sourceRow = std::find_if(rendered.mixer.begin(), rendered.mixer.end(),
                                  [&](const auto &row) { return row.index == detectorSource; });
    check(sourceRow != rendered.mixer.end(), "Saved detector source exists for actual offline export");
    sourceRow->routes.clear();
    check(rendered.validate().wasOk(), "Original detector-only Song export model validates");
    const auto wav = folder.getChildFile("native-detector-only.wav");
    check(audio.renderSync(rendered, wav, 48000, 24, 256).wasOk(), "Actual hosted detector Song WAV export");
    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(wav.createInputStream().release(), true));
    check(reader && reader->sampleRate == 48000 && reader->numChannels == 2 && reader->lengthInSamples > 0 &&
              reader->lengthInSamples <= 48000 * 5,
          "Actual detector WAV has bounded stereo frames and requested rate");
    juce::AudioBuffer<float> pcm(2, int(reader->lengthInSamples));
    check(reader->read(&pcm, 0, pcm.getNumSamples(), 0, true, true), "Read all actual detector-only WAV PCM");
    bool finite = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < pcm.getNumSamples(); ++frame)
            finite = finite && std::isfinite(pcm.getSample(channel, frame));
    check(finite && pcm.getMagnitude(0, pcm.getNumSamples()) > .0001f,
          "Every exported sample is finite and the genuine aux-only hosted path is audible");
    passed.add(stage);
}
