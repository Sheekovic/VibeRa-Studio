// SPDX-License-Identifier: AGPL-3.0-or-later
// Included by Studio.cpp inside namespace vibera. Actual component/virtual-device
// acceptance; no OS input, physical hardware, listening or FL-runtime claim.
juce::Result Studio::verifyPianoLoopPreview(const juce::File &folder, const Project *pluginFixture)
{
    auto fail = [this](const juce::String &message)
    {
        audio.stop();
        return juce::Result::fail("Piano loop preview: " + message);
    };
    try
    {
        stopTimer();
        audio.stop();
        releaseTypingNotes();
        if (midiTaking || hasPendingMidiTake() || afterPluginState)
            return fail("fixture requires completed prior take/state ownership");
        recordArmed = false;
        auto seed = Project::empty();
        seed.channels.resize(2);
        seed.channels.front().source = "builtin:bass";
        const auto channel = seed.channels.front().id, other = seed.channels.back().id;
        bool hosted = false;
        if (pluginFixture)
        {
            const auto found = std::find_if(pluginFixture->channels.begin(), pluginFixture->channels.end(),
                                            [](const auto &entry) { return entry.instrument.has_value(); });
            if (found == pluginFixture->channels.end())
                return fail("actual provider fixture contains no instrument");
            seed.channels.front().instrument = found->instrument;
            seed.channels.front().instrument->id = seed.allocate();
            hosted = true;
        }
        auto &part = seed.patterns.front();
        const auto patternId = part.id;
        part.autoLength = false;
        part.length = 8;
        const auto sourceId = seed.allocate(), secondId = seed.allocate(), dormantId = seed.allocate(),
                   outsideId = seed.allocate(), otherId = seed.allocate();
        part.notes = {{sourceId, channel, .5, .25, 60, .7f, false},
                      {secondId, channel, 0, .125, 67, .5f, false},
                      {dormantId, channel, 1.5, .5, 64, .4f, false},
                      {outsideId, channel, 9, .25, 69, .3f, false},
                      {otherId, other, .25, .25, 72, .3f, false}};
        part.channelLoops = {{channel, ChannelLoopMode::fixed, .75}};
        seed.placements = {{seed.allocate(), patternId, 0, 0, 8, 0}};
        if (auto valid = seed.validate(); valid.failed())
            return fail("invalid controlled source: " + valid.getErrorMessage());
        controller.replace(seed);
        selectedPattern = patternId;
        selectedChannel = channel;
        selectedChannels = {channel};
        selectedNotes.clear();
        selectedClips.clear();
        patternMode = true;
        focus = Scope::piano;
        snap = .25;
        audio.invalidateProjection();
        // Same real preparation bound as the enclosing navigation fixture.
        const auto began = juce::Time::getMillisecondCounter();
        auto prepared = juce::Result::fail("Preparing");
        do
        {
            prepared = audio.rebuild(controller.project, true, patternId);
            if (prepared.wasOk())
                break;
            dispatchMessages(20);
        } while (prepared.getErrorMessage().contains("Preparing") &&
                 juce::Time::getMillisecondCounter() - began < 15000);
        if (prepared.failed())
            return fail("real projection preparation: " + prepared.getErrorMessage());
        needsRebuild = false;
        engineRevision = controller.revision;
        projectionError.clear();
        audio.setMidiTarget(channel);
        audio.setMetronome(false);
        audio.setCountInBars(0);
        auto *device = dynamic_cast<SyntheticDevice *>(
            audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice());
        if (!device || device->getCurrentBufferSizeSamples() != 256 ||
            device->getCurrentSampleRate() != 48000.)
            return fail("requires the existing owned256/48kHz synthetic device");
        juce::AudioBuffer<float> block(2, 256);
        double maximumCallbackMs = 0;
        int measuredCallbacks = 0, overruns = 0;
        auto tick = [&](int count)
        {
            float peak = 0;
            for (int i = 0; i < count; ++i)
            {
                block.clear();
                const auto beganCallback = juce::Time::getMillisecondCounterHiRes();
                device->process(block);
                const auto elapsed = juce::Time::getMillisecondCounterHiRes() - beganCallback;
                maximumCallbackMs = std::max(maximumCallbackMs, elapsed);
                ++measuredCallbacks;
                overruns += elapsed > 1000. * 256 / 48000. ? 1 : 0;
                peak = std::max(peak, block.getMagnitude(0, 256));
                dispatchMessages(hosted ? 6 : 1);
            }
            return peak;
        };
        tick(hosted ? 32 : 8);
        maximiseEditor(1);
        refresh();
        auto *editor = static_cast<Editor *>(windows[1]->getContentComponent());
        editor->setPaintMode(false);
        editor->viewAction("piano.fit");
        auto source = [&]() -> const Note *
        {
            const auto *current = controller.project.pattern(patternId);
            if (current)
                for (const auto &note : current->notes)
                    if (note.id == sourceId)
                        return &note;
            return nullptr;
        };
        auto displayBounds = [&](double beat, int pitch = 60)
        {
            auto note = seed.pattern(patternId)->notes.front();
            note.beat = beat;
            note.pitch = pitch;
            return editor->noteBounds(note);
        };
        const auto original = controller.project.state();
        const auto originalRevision = controller.revision;
        const auto originalNext = controller.project.nextId;
        const auto lifecycle = audio.projectionDiagnostics();
        const int starts = device->starts, stops = device->stops;
        editor->viewAction("piano.zoomIn");
        editor->viewAction("piano.zoomOut");
        auto origin = displayBounds(.5).getCentre();
        auto wheelEvent = juce::MouseEvent(
            juce::Desktop::getInstance().getMainMouseSource(), origin, juce::ModifierKeys{}, 1, 0, 0, 0, 0,
            editor, editor, juce::Time::getCurrentTime(), origin, juce::Time::getCurrentTime(), 1, false);
        juce::MouseWheelDetails wheel{};
        wheel.deltaX = .1f;
        editor->mouseWheelMove(wheelEvent, wheel);
        editor->viewAction("piano.fit");
        if (!original.isEquivalentTo(controller.project.state()) || controller.revision != originalRevision ||
            controller.project.nextId != originalNext || controller.undo.canUndo() ||
            controller.undo.canRedo() || audio.playing())
            return fail("view/scroll allocated notes, changed source/history or started playback");

        auto request = juce::JSON::parse(R"({"entity":"noteOccurrences","offset":0,"limit":512})");
        request.getDynamicObject()->setProperty("pattern", juce::int64(patternId));
        const auto inspection = mcpCall("vibera_inspect", request)["structuredContent"];
        const auto *rows = inspection["items"].getArray();
        if (!rows || rows->size() != 24 || int(inspection["total"]) != 24 ||
            juce::int64(inspection["pattern"]) != patternId ||
            inspection["projectIdentity"].toString() != controller.projectIdentity ||
            juce::int64(inspection["revision"]) != juce::int64(controller.revision))
            return fail("actual occurrence readback lacks the literal24 rows or current source authority");
        bool repeatedSource = false, dormantSource = false, outsideSource = false;
        for (const auto &row : *rows)
        {
            const auto id = juce::int64(row["sourceId"]);
            if (juce::int64(row["id"]) != id ||
                (id != sourceId && id != secondId && id != dormantId && id != outsideId && id != otherId))
                return fail("view fabricated an editable note identity");
            if (id == sourceId && int(row["iteration"]) == 1)
                repeatedSource = double(row["beat"]) == 1.25 && double(row["sourceBeat"]) == .5 &&
                                 double(row["sourceLength"]) == .25 && bool(row["repeated"]);
            if (id == dormantId)
                dormantSource = double(row["beat"]) == 1.5 && bool(row["dormant"]) && !bool(row["repeated"]);
            if (id == outsideId)
                outsideSource = double(row["beat"]) == 9 && bool(row["dormant"]) && !bool(row["repeated"]);
        }
        if (!repeatedSource || !dormantSource || !outsideSource)
            return fail("repeat/source relationship or dormant source preservation is missing");
        auto event = [&](juce::Point<float> point, juce::Point<float> start, int flags)
        {
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                    juce::ModifierKeys(flags), 1, 0, 0, 0, 0, editor, editor,
                                    juce::Time::getCurrentTime(), start, juce::Time::getCurrentTime(), 1,
                                    point != start);
        };
        const int left = juce::ModifierKeys::leftButtonModifier;
        auto drag = [&](juce::Point<float> from, juce::Point<float> to, bool cancel)
        {
            editor->mouseDown(event(from, from, left));
            editor->mouseDrag(event(to, from, left));
            if (cancel)
                editor->cancelPointerGesture();
            else
                editor->mouseUp(event(to, from, left));
        };
        auto repeat = displayBounds(1.25).getCentre();
        if (editor->noteAt(repeat) != sourceId ||
            editor->noteAt(displayBounds(1.5, 64).getCentre()) != dormantId)
            return fail("real repeat/dormant hit does not resolve its stable raw source");
        editor->mouseDown(event(repeat, repeat, left));
        editor->mouseUp(event(repeat, repeat, left));
        if (selectedNotes != std::vector<Id>{sourceId} ||
            !original.isEquivalentTo(controller.project.state()) ||
            controller.project.nextId != originalNext || controller.undo.canUndo() ||
            controller.undo.canRedo())
            return fail("clicking a repeat inserted a note or selected an occurrence identity");
        const float quarter = displayBounds(.75).getX() - displayBounds(.5).getX();
        drag(repeat, repeat.translated(-quarter, 0), false);
        auto moved = seed;
        moved.pattern(patternId)->notes.front().beat = .25;
        if (!source() || source()->beat != .25 || !moved.state().isEquivalentTo(controller.project.state()) ||
            controller.undoEditChecked().failed() || !original.isEquivalentTo(controller.project.state()) ||
            controller.redoEditChecked().failed() ||
            !moved.state().isEquivalentTo(controller.project.state()) ||
            controller.undoEditChecked().failed() || !original.isEquivalentTo(controller.project.state()))
            return fail("repeat drag did not move raw source once with exact one-step Undo/Redo");
        const bool cancelUndo = controller.undo.canUndo(), cancelRedo = controller.undo.canRedo();
        drag(repeat, repeat.translated(-quarter, 0), true);
        if (controller.hasGesture() || !original.isEquivalentTo(controller.project.state()) ||
            controller.project.nextId != originalNext || controller.undo.canUndo() != cancelUndo ||
            controller.undo.canRedo() != cancelRedo)
            return fail("repeat drag Cancel lost exact source/allocator or retained ownership");
        auto edge = displayBounds(1.25);
        auto edgePoint = juce::Point<float>{edge.getRight() - 1, edge.getCentreY()};
        drag(edgePoint, edgePoint.translated(quarter, 0), false);
        auto resizedProject = seed;
        resizedProject.pattern(patternId)->notes.front().length = .5;
        if (!source() || source()->length != .5 ||
            !resizedProject.state().isEquivalentTo(controller.project.state()) ||
            controller.undoEditChecked().failed() || !original.isEquivalentTo(controller.project.state()))
            return fail("unclipped repeat resize used displayed absolute time or changed another source");

        auto row = displayBounds(1.25);
        auto first = juce::Point<float>{displayBounds(1).getX(), row.getY() - 2};
        auto last = juce::Point<float>{displayBounds(2.4).getX(), row.getBottom() + 2};
        const int marquee = left | juce::ModifierKeys::ctrlModifier;
        editor->mouseDown(event(first, first, marquee));
        editor->mouseDrag(event(last, first, marquee));
        editor->mouseUp(event(last, first, marquee));
        if (selectedNotes != std::vector<Id>{sourceId} ||
            !original.isEquivalentTo(controller.project.state()))
            return fail("marquee across multiple repeats did not deduplicate real source IDs");
        auto range = juce::JSON::parse(
            R"({"action":"range","target":"notes","start":1,"end":2.4,"low":60,"high":60,"criterion":"overlaps","loopPreview":true})");
        const auto selected = mcpCall("vibera_selection", range)["structuredContent"]["ids"];
        if (!selected.isArray() || selected.size() != 1 || juce::int64(selected[0]) != sourceId ||
            selectedNotes != std::vector<Id>{sourceId} ||
            !original.isEquivalentTo(controller.project.state()))
            return fail("shared semantic occurrence-range selection differs from actual marquee");

        editor->setNoteMuteTool();
        editor->mouseDown(event(repeat, repeat, left));
        editor->mouseUp(event(repeat, repeat, left));
        auto muted = seed;
        muted.pattern(patternId)->notes.front().muted = true;
        if (!muted.state().isEquivalentTo(controller.project.state()) ||
            controller.undoEditChecked().failed() || !original.isEquivalentTo(controller.project.state()))
            return fail("repeat mute touched more than its one real source or lost exact Undo");
        editor->setPaintMode(false);
        const int right = juce::ModifierKeys::rightButtonModifier;
        editor->mouseDown(event(repeat, repeat, right));
        editor->mouseUp(event(repeat, repeat, right));
        auto erased = seed;
        std::erase_if(erased.pattern(patternId)->notes,
                      [&](const auto &note) { return note.id == sourceId; });
        if (!erased.state().isEquivalentTo(controller.project.state()) ||
            controller.undoEditChecked().failed() || !original.isEquivalentTo(controller.project.state()))
            return fail("repeat erase deleted a dormant/foreign note or lost source Undo");

        // An actual raw note overlays the same-pitch repeat and must win the hit.
        const auto overlayId = controller.project.nextId;
        CommandArgs overlay;
        overlay.pattern = patternId;
        overlay.channel = channel;
        overlay.beat = 1.25;
        overlay.length = .25;
        overlay.pitch = 60;
        overlay.value = .2;
        command("notes.add", overlay);
        if (editor->noteAt(repeat) != overlayId || controller.undoEditChecked().failed() ||
            !original.isEquivalentTo(controller.project.state()))
            return fail("raw-first overlap hit or exact source restoration failed");
        float peak = 0;
        audio.setLooping(true);
        for (bool usePattern : {true, false})
        {
            patternMode = usePattern;
            if (auto projection = audio.rebuild(controller.project, usePattern, patternId);
                projection.failed())
                return fail("mode preparation: " + projection.getErrorMessage());
            needsRebuild = false;
            engineRevision = controller.revision;
            audio.seekBeat(0);
            audio.playStop();
            float modePeak = 0;
            for (int edit = 0; edit < 8; ++edit)
            {
                const auto playhead = audio.positionBeat();
                repeat = displayBounds(1.25).getCentre();
                drag(repeat, repeat.translated(-quarter, 0), false);
                if (!source() || source()->beat != .25 || !audio.playing() ||
                    audio.positionBeat() != playhead || controller.undoEditChecked().failed() ||
                    !original.isEquivalentTo(controller.project.state()) || !audio.playing() ||
                    audio.positionBeat() != playhead)
                    return fail("live repeated source drag/Undo restarted or sought transport");
                modePeak = std::max(modePeak, tick(16));
            }
            // Cross the eight-beat outer transport loop as well as Channel cycles.
            modePeak = std::max(modePeak, tick(672));
            peak = std::max(peak, modePeak);
            if (!audio.playing() || modePeak <= .001f || !original.isEquivalentTo(controller.project.state()))
                return fail("real Pattern/Song loop playback lost PCM or altered the source");
            audio.stop();
            tick(hosted ? 32 : 8);
        }
        if (overruns)
            return fail("outer256/48kHz callback overrun during repeated-note workflow: " +
                        juce::String(maximumCallbackMs, 7) + "ms");
        const auto after = audio.projectionDiagnostics();
        if (lifecycle.edit != after.edit || lifecycle.channels.size() != after.channels.size() ||
            starts != device->starts || stops != device->stops || controller.project.nextId != originalNext)
            return fail("Piano preview/editing replaced graph/device or consumed source identities");
        for (size_t i = 0; i < lifecycle.channels.size(); ++i)
            if (lifecycle.channels[i].graphInitialisations != after.channels[i].graphInitialisations)
                return fail("Piano occurrence workflow reinitialised a retained source graph");
        auto png = folder.getChildFile(hosted ? "piano-loop-preview-provider.png" : "piano-loop-preview.png")
                       .createOutputStream();
        if (!png)
            return fail("cannot open fresh populated Piano capture");
        png->setPosition(0);
        png->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(
                editor->createComponentSnapshot(editor->getLocalBounds()), *png))
            return fail("cannot encode populated Piano capture");
        png.reset();
        if (!folder.getChildFile(hosted ? "piano-loop-preview-provider.txt" : "piano-loop-preview.txt")
                 .replaceWithText(
                     "PASS actual component repeat hit/drag/resize/marquee/mute/erase/raw-overlap, source-ID "
                     "MCP readback/range, exact Undo/Redo/Cancel, dormant notes and allocator retained. "
                     "Literal24 occurrence rows; live repeated drag/Undo in Pattern and Song across "
                     "transport/Channel loop boundaries; actual256/48kHz instrument PCM peak=" +
                     juce::String(peak, 7) + "; measured callbacks=" + juce::String(measuredCallbacks) +
                     "; maximumCallbackMs=" + juce::String(maximumCallbackMs, 7) +
                     "; overruns=" + juce::String(overruns) +
                     ". Existing enclosing navigation/audio assertions and deadlines retained. "
                     "No OS input, physical MIDI/hardware, listening or FL-runtime claim.\n"))
            return fail("cannot retain acceptance scope receipt");
        needsRebuild = false;
        engineRevision = controller.revision;
        projectionError.clear();
        maximisedEditor = -1;
        resized();
        startTimerHz(20);
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return fail("actual handler/service exception: " + juce::String(error.what()));
    }
}
