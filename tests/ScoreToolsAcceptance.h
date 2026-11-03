// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
static void scoreToolsTests(const juce::File &folder)
{
    Controller c;
    auto &p = c.project.patterns.front();
    const auto pattern = p.id, channel = c.project.channels[0].id, other = c.project.channels[1].id;
    const auto first = c.project.allocate(), chord = c.project.allocate(), last = c.project.allocate();
    p.notes = {{first, channel, .125, .2, 60, .6f, true},
               {chord, channel, .125, .3, 64, .4f, false},
               {last, channel, 1.25, .7, 67, .8f, false},
               {c.project.allocate(), other, .25, 2, 48, .9f, true}};
    const auto original = c.project.state();
    CommandArgs a;
    a.pattern = pattern;
    a.channel = channel;
    require(c.execute("notes.legato", a).wasOk(), "legato current part");
    auto score = c.project.pattern(pattern)->notes;
    require(score[0].length == 1.125 && score[1].length == 1.125 && score[2].length == .7 &&
                score[3].length == 2 && !score[0].step && score[3].step,
            "legato chord groups, final onset and unrelated Channel");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(original), "legato exact one Undo");
    c.redoEdit();
    const auto connected = c.project.state();
    require(c.execute("notes.legato", a).wasOk() && c.project.state().isEquivalentTo(connected),
            "repeated legato is idempotent");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(original), "no-op adds no history");
    a.ids = {first, last};
    require(c.execute("notes.legato", a).wasOk(), "legato selected phrase");
    require(c.project.pattern(pattern)->notes[1].length == .3, "unselected chord member untouched");
    c.undoEdit();
    a.ids = {first, 999999};
    require(c.execute("notes.legato", a).failed() && c.project.state().isEquivalentTo(original),
            "stale selection is atomic");
    a.ids = {last};
    a.length = .25;
    require(c.execute("notes.chop", a).wasOk(), "chop selected held note");
    score = c.project.pattern(pattern)->notes;
    require(score.size() == 6 && score[2].id == last && score[2].length == .25 && score[4].beat == 1.5 &&
                score[5].beat == 1.75 && std::abs(score[5].length - .2) < 1.e-12 &&
                score[5].velocity == .8f && score[5].pitch == 67 && score[3].step && score[0].step,
            "chop exact onset-relative slices/remainder, properties and first identity");
    const auto chopped = c.project.state();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(original), "chop exact one Undo including IDs");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(chopped), "chop exact Redo including IDs");
    const auto path = folder.getChildFile("score-tools.vibra");
    require(saveProject(c.project, path).wasOk(), "save chopped score");
    Project loaded;
    require(loadProject(path, loaded).wasOk() && loaded.state().isEquivalentTo(chopped),
            "score tools project round trip");
    c.undoEdit();
    for (double invalid : {0., -.25, .001, 65., std::numeric_limits<double>::infinity()})
    {
        a.length = invalid;
        require(c.execute("notes.chop", a).failed() && c.project.state().isEquivalentTo(original),
                "invalid chop interval cannot mutate document or allocator");
    }
    a.length = .25;
    a.ids.clear();
    a.channel = 999999;
    require(c.execute("notes.chop", a).failed(), "all-current-part requires valid Channel");
    auto huge = c.project;
    huge.patterns[0].notes[2].length = 200;
    c.replace(huge);
    a.channel = channel;
    a.length = .03125;
    const auto beforeLimit = c.project.state();
    require(c.execute("notes.chop", a).failed() && c.project.state().isEquivalentTo(beforeLimit),
            "bounded chop rejects excessive expansion before partial edits");
    auto defaults = Customization::familiarDefaults();
    c.replace(Project::empty());
    auto &macroPattern = c.project.patterns.front();
    const auto macroChannel = c.project.channels.front().id;
    macroPattern.notes = {{c.project.allocate(), macroChannel, 0, .25, 60, .7f, false},
                          {c.project.allocate(), macroChannel, 1, .5, 62, .8f, false}};
    const auto macroBefore = c.project.state();
    CommandArgs chop;
    chop.length = .25;
    require(defaults
                .addMacro({"macro.phrase",
                           "Connect and repeat",
                           {{"notes.legato", {}, true}, {"notes.chop", chop, true}}})
                .wasOk(),
            "score tools available in user macros");
    MacroContext context{macroPattern.id, macroChannel, 0, {}, {}};
    require(defaults.runMacro("macro.phrase", c, context).wasOk() && c.project.patterns[0].notes.size() == 6,
            "score tool macro performs usable phrase transformation");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(macroBefore), "whole score macro one Undo");
    auto oldProfile = defaults.state();
    oldProfile.getDynamicObject()->removeProperty("knownActions");
    auto *oldBindings = oldProfile["bindings"].getArray();
    for (int i = oldBindings->size(); --i >= 0;)
        if ((*oldBindings)[i]["action"] == juce::var("notes.legato") ||
            (*oldBindings)[i]["action"] == juce::var("notes.chop"))
            oldBindings->remove(i);
    Customization migrated;
    require(migrated.importState(oldProfile).wasOk() &&
                migrated.resolve(juce::KeyPress('U', juce::ModifierKeys::ctrlModifier, 0), Scope::piano) ==
                    "notes.chop",
            "legacy profiles acquire nonconflicting new tool shortcuts");
    defaults.unbind("notes.legato", Scope::piano);
    require(migrated.importState(defaults.state()).wasOk() &&
                migrated.resolve(juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 0), Scope::piano)
                    .isEmpty(),
            "deliberately unbound tool remains unbound");
    defaults = Customization::familiarDefaults();
    const juce::KeyPress key('L', juce::ModifierKeys::ctrlModifier, 0);
    require(defaults.resolve(key, Scope::piano) == "notes.legato" &&
                defaults.resolve(key, Scope::rack) == "channel.mixer" &&
                defaults.resolve(juce::KeyPress('U', juce::ModifierKeys::ctrlModifier, 0), Scope::piano) ==
                    "notes.chop",
            "familiar contextual shortcuts do not steal Rack assignment");
}

static void scoreToolsLiveTests(const juce::File &folder, const PluginSpec *plugin = nullptr)
{
    juce::String report;
    for (bool patternMode : {true, false})
    {
        auto audioOwner = std::make_unique<AudioEngine>(false, juce::File{}, 256);
        auto &audio = *audioOwner;
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
        require(device != nullptr, "score tools virtual device");
        Controller c;
        auto &p = c.project.patterns[0];
        p.length = 1;
        p.autoLength = false; // Score-tool continuity is measured inside this exact loop.
        c.project.tempo = 240;
        const auto pattern = p.id, channel = c.project.channels[0].id, held = c.project.channels[1].id;
        c.project.channels[0].source = c.project.channels[1].source = "builtin:bass";
        c.project.channels[0].pan = -1;
        c.project.channels[1].pan = 1;
        if (plugin)
        {
            c.project.channels[0].instrument = *plugin;
            c.project.channels[0].instrument->id = c.project.allocate();
        }
        p.notes = {{c.project.allocate(), channel, 0, .125, 60, .7f, false},
                   {c.project.allocate(), channel, .5, .125, 67, .6f, false},
                   {c.project.allocate(), channel, .625, .275, 67, .6f, false},
                   {c.project.allocate(), held, 0, 1, 48, .6f, false}};
        c.project.placements.push_back({c.project.allocate(), pattern, 0, 0, 1, 0});
        require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                "score tools initial projection");
        audio.setMidiTarget(held);
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        int callbacks = 0, overruns = 0;
        double maximum = 0;
        float editedPartPeak = 0;
        auto tick = [&]
        {
            const auto start = juce::Time::getHighResolutionTicks();
            device->process(block);
            const auto ms =
                juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) *
                1000.;
            maximum = std::max(maximum, ms);
            overruns += ms > 256. / 48000. * 1000.;
            ++callbacks;
            editedPartPeak = std::max(editedPartPeak, block.getMagnitude(0, 0, 256));
            dispatchMessages(plugin ? 6 : 1);
        };
        for (int i = 0; i < 16; ++i)
            tick();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 55, juce::uint8(100)));
        tick();
        const auto baseline = audio.projectionDiagnostics();
        const auto pluginBefore = audio.pluginDiagnostics();
        const int starts = device->starts, stops = device->stops;
        int silent = 0;
        CommandArgs a;
        a.pattern = pattern;
        a.channel = channel;
        a.length = .125;
        for (int edit = 0; edit < 40; ++edit)
        {
            const auto head = audio.positionBeat();
            const char *tool = edit % 3 == 0 ? "notes.legato" : edit % 3 == 1 ? "notes.chop" : "notes.glue";
            const auto beforeTool = c.project.state();
            require(c.execute(tool, a).wasOk() && !c.project.state().isEquivalentTo(beforeTool),
                    "live score tool makes actual edit");
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "live score projection");
            require(audio.playing() && std::abs(audio.positionBeat() - head) < .01,
                    "score tools retain playhead");
            for (int i = 0; i < 4; ++i)
            {
                tick();
                silent += block.getMagnitude(1, 0, 256) == 0;
            }
            c.undoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "live score Undo");
            c.redoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "live score Redo");
            c.undoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "live score restore");
            for (int i = 0; i < 4; ++i)
                tick();
        }
        const auto after = audio.projectionDiagnostics();
        report +=
            juce::String(patternMode ? "Pattern" : "Song") +
            " edits=40 callbacks=" + juce::String(callbacks) + " maxMs=" + juce::String(maximum, 4) +
            " overruns=" + juce::String(overruns) + " silentEditBlocks=" + juce::String(silent) +
            " graphReplacements=" + juce::String(juce::int64(after.replacements - baseline.replacements)) +
            " deviceStarts=" + juce::String(device->starts - starts) +
            " deviceStops=" + juce::String(device->stops - stops) +
            " editedPartPeak=" + juce::String(editedPartPeak, 6) + "\n";
        const auto pluginAfter = audio.pluginDiagnostics();
        if (plugin)
        {
            require(pluginBefore.size() == 1 && pluginAfter.size() == 1 && pluginAfter[0].alive &&
                        pluginBefore[0].processId == pluginAfter[0].processId &&
                        pluginAfter[0].packets.submitted > pluginBefore[0].packets.submitted,
                    "Glue/chop/legato retain and actually process the installed instrument worker");
            report += "VST3 worker=" + juce::String(pluginAfter[0].processId) +
                      " packets=" + juce::String(juce::int64(pluginAfter[0].packets.submitted)) +
                      " missed=" + juce::String(juce::int64(pluginAfter[0].packets.missed)) +
                      " dropped=" + juce::String(juce::int64(pluginAfter[0].packets.dropped)) + "\n";
        }
        folder.getChildFile(plugin ? "score-tools-vst3-live.txt" : "score-tools-live.txt")
            .replaceWithText(
                report + (plugin ? "Actual installed VST3 plus unaffected built-in part; virtual callbacks, "
                                   "no hardware/listening.\n"
                                 : "Virtual callbacks, built-in samples; no listening/hardware claim.\n"));
        require(overruns == 0 && silent == 0, "score tools callback deadline and unaffected sustained PCM");
        require(editedPartPeak > .001f,
                "Affected instrument produces isolated left-channel PCM during score edits");
        require(after.edit == baseline.edit && after.playbackContext == baseline.playbackContext &&
                    after.replacements == baseline.replacements &&
                    after.samplerCreations == baseline.samplerCreations &&
                    after.cache.preparations == baseline.cache.preparations && device->starts == starts &&
                    device->stops == stops,
                "score tools retain graph, device and prepared samples");
        auto heldState = std::find_if(after.channels.begin(), after.channels.end(),
                                      [&](const auto &state) { return state.channel == held; });
        require(heldState != after.channels.end() && heldState->midiNotes == 1,
                "score tools retain unrelated live held note");
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 55));
        tick();
        audio.stop();
    }
}
