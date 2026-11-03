// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
static void rackGroupTests(const juce::File &folder)
{
    // Live grouping acceptance exposed a pre-existing recorder precision defect:
    // adding a rounded duration can stop one ULP before its exact endpoint.
    for (double loop : {0., 4.})
    {
        MidiTake take;
        take.begin(1, 0, 0, 60, loop);
        take.add({1, .003, 1, 1, {0x90, 60, 100}});
        take.add({1, .013, 1, 1, {0x80, 60, 0}});
        auto notes = take.finish(.013);
        require(notes.size() == 1 && notes[0].beat == .003 && std::abs(notes[0].length - .01) < 1e-15,
                "Recorder closes decimal-time note once without microscopic residual segment");
    }
    Controller c;
    const auto a = c.project.channels[0].id, b = c.project.channels[2].id;
    const auto original = c.project.state();
    CommandArgs args;
    args.ids = {a, b};
    args.text = "Drums";
    require(c.execute("rack.group.assign", args).wasOk(), "Group Selected creates one group atomically");
    const auto group = c.project.channelGroups.front().id;
    require(c.project.channel(a)->group == group && c.project.channel(b)->group == group &&
                c.project.channels[1].group == 0,
            "Bulk group membership preserves other Channels");
    const auto grouped = c.project.state();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(original), "Group Selected one exact Undo including allocator");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(grouped), "Group Selected exact Redo");
    args.ids = {c.project.channels[1].id};
    args.text = "dRuMs";
    require(c.execute("rack.group.assign", args).wasOk() && c.project.channelGroups.size() == 1 &&
                c.project.channels[1].group == group,
            "Name reuse is case insensitive");
    args = {};
    args.target = group;
    args.text = "Percussion";
    require(c.execute("rack.group.rename", args).wasOk() && c.project.channelGroups[0].id == group,
            "Rename preserves stable group identity");
    args = {};
    args.text = "Empty";
    require(c.execute("rack.group.create", args).wasOk(), "Empty group creation");
    const auto valid = c.project.state();
    for (const auto &name : juce::StringArray{"", " All ", "Unsorted", "percussion", "Dr\nums",
                                              juce::String::repeatedString("x", 129)})
    {
        args.text = name;
        require(c.execute("rack.group.create", args).failed() && c.project.state().isEquivalentTo(valid),
                "Invalid/reserved/duplicate group name rejected atomically");
    }
    args = {};
    args.target = group;
    args.ids = {a, 999999};
    require(c.execute("rack.group.assign", args).failed() && c.project.state().isEquivalentTo(valid),
            "Stale bulk member fails before changing any membership");
    args.ids.clear();
    require(c.execute("rack.group.assign", args).failed(), "Empty bulk command rejected");
    args.ids = {a};
    args.target = 999999;
    require(c.execute("rack.group.assign", args).failed(), "Missing group rejected");
    args = {};
    args.target = group;
    args.text = "builtin:kick";
    require(c.execute("channel.add", args).wasOk() && c.project.channels.back().group == group,
            "Canonical new Channel explicit group membership");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(valid), "Grouped new Channel exact Undo");
    args = {};
    args.target = group;
    require(c.execute("rack.group.delete", args).wasOk() && c.project.channels.size() == 4 &&
                std::all_of(c.project.channels.begin(), c.project.channels.end(),
                            [](const auto &ch) { return ch.group == 0; }),
            "Delete unassigns; preserves every instrument, note, routing and sample");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(valid), "Delete exact Undo including empty group");
    const auto path = folder.getChildFile("rack-groups.vibra");
    Project loaded;
    require(saveProject(c.project, path).wasOk() && loadProject(path, loaded).wasOk() &&
                loaded.state().isEquivalentTo(valid),
            "Schema11 groups exact file round trip");
    auto malformed = valid.createCopy();
    malformed.getChildWithName("CHANNELS").getChild(0).setProperty("group", 999999, nullptr);
    require(Project::parse(malformed, loaded).failed(), "Unknown persisted membership rejected");
    malformed = valid.createCopy();
    malformed.getChildWithName("CHANNEL_GROUPS").getChild(0).setProperty("id", 1.5, nullptr);
    require(Project::parse(malformed, loaded).failed(), "Fractional persisted group identity rejected");
    auto legacy = original.createCopy();
    legacy.setProperty("version", 10, nullptr);
    stripSamplerVoiceFieldsForLegacy(legacy);
    legacy.removeChild(legacy.getChildWithName("CHANNEL_GROUPS"), nullptr);
    auto expectedLegacy = original.createCopy();
    for (auto pattern : expectedLegacy.getChildWithName("PATTERNS"))
        pattern.setProperty("autoLength", false, nullptr);
    require(Project::parse(legacy, loaded).wasOk() && loaded.version == Project::currentVersion &&
                loaded.channelGroups.empty() && loaded.state().isEquivalentTo(expectedLegacy),
            "Legacy schema10 migrates without model loss");
    Customization custom = Customization::familiarDefaults();
    require(custom.resolve(juce::KeyPress('G', juce::ModifierKeys::altModifier, 0), Scope::rack) ==
                    "rack.group.dialog" &&
                custom.resolve(juce::KeyPress(juce::KeyPress::pageDownKey), Scope::rack) == "rack.group.next",
            "Familiar contextual group/filter defaults");
    Macro macro{"macro.rack-test", "Group selected drums", {{"rack.group.assign", {}, true}}};
    macro.steps[0].parameters.text = "Macro";
    require(custom.addMacro(macro).wasOk(), "Group macro registration");
    MacroContext context;
    context.channels = {a, b};
    require(custom.runMacro(macro.id, c, context).wasOk() &&
                c.project.channel(a)->group == c.project.channel(b)->group &&
                c.project.channel(a)->group != group,
            "Macro resolves shared bulk selection");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(valid), "Group macro exact one Undo");
}

static void rackGroupsLiveTests(const juce::File &folder, const PluginSpec *plugin = nullptr,
                                bool fill = false)
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
        require(device != nullptr, "Rack groups virtual device");
        Controller c;
        auto &p = c.project.patterns[0];
        p.length = 1;
        p.autoLength = false; // Fixed one-beat held-input group-edit scenario.
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
                "Rack groups initial projection");
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
        a.ids = fill ? std::vector<Id>{channel} : std::vector<Id>{channel, held};
        a.text = "Live group";
        for (int edit = 0; edit < 40; ++edit)
        {
            const auto head = audio.positionBeat();
            const auto before = c.project.state();
            const auto command = fill ? "steps.fill" + juce::String(edit % 3 == 0   ? 2
                                                                    : edit % 3 == 1 ? 4
                                                                                    : 8)
                                      : juce::String("rack.group.assign");
            require(c.execute(command, a).wasOk() && !c.project.state().isEquivalentTo(before),
                    "live fill/group actual mutation");
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "live group projection");
            require(audio.playing() && std::abs(audio.positionBeat() - head) < .01,
                    "groups preserve playhead");
            for (int i = 0; i < 4; ++i)
            {
                tick();
                silent += block.getMagnitude(1, 0, 256) == 0;
            }
            c.undoEdit();
            require(c.project.state().isEquivalentTo(before), "live group exact Undo");
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "live group Undo projection");
            c.redoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "live group Redo projection");
            c.undoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "live group restore");
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
            report += "hostsBefore=" + juce::String(int(pluginBefore.size())) +
                      " hostsAfter=" + juce::String(int(pluginAfter.size())) +
                      " status=" + audio.pluginStatus(c.project.channels[0].instrument->id) + "\n";
            if (!pluginBefore.empty() && !pluginAfter.empty())
                report += "beforePid=" + juce::String(pluginBefore[0].processId) +
                          " afterPid=" + juce::String(pluginAfter[0].processId) +
                          " beforePackets=" + juce::String(juce::int64(pluginBefore[0].packets.submitted)) +
                          " afterPackets=" + juce::String(juce::int64(pluginAfter[0].packets.submitted)) +
                          "\n";
            folder.getChildFile(fill ? "step-fill-vst3-diagnostics.txt" : "rack-groups-vst3-diagnostics.txt")
                .replaceWithText(report);
            require(pluginBefore.size() == 1 && pluginAfter.size() == 1 && pluginAfter[0].alive &&
                        pluginBefore[0].processId == pluginAfter[0].processId &&
                        pluginAfter[0].packets.submitted > pluginBefore[0].packets.submitted,
                    "Rack grouping retain and actually process the installed instrument worker");
            report += "VST3 worker=" + juce::String(pluginAfter[0].processId) +
                      " packets=" + juce::String(juce::int64(pluginAfter[0].packets.submitted)) +
                      " missed=" + juce::String(juce::int64(pluginAfter[0].packets.missed)) +
                      " dropped=" + juce::String(juce::int64(pluginAfter[0].packets.dropped)) + "\n";
        }
        folder
            .getChildFile(fill ? (plugin ? "step-fill-vst3-live.txt" : "step-fill-live.txt")
                               : (plugin ? "rack-groups-vst3-live.txt" : "rack-groups-live.txt"))
            .replaceWithText(
                report + (plugin ? "Actual installed VST3 plus unaffected built-in part; virtual callbacks, "
                                   "no hardware/listening.\n"
                                 : "Virtual callbacks, built-in samples; no listening/hardware claim.\n"));
        require(overruns == 0 && silent == 0, "Rack groups callback deadline and unaffected sustained PCM");
        require(editedPartPeak > .001f,
                "Affected instrument produces isolated left-channel PCM during score edits");
        require(after.edit == baseline.edit && after.playbackContext == baseline.playbackContext &&
                    after.replacements == baseline.replacements &&
                    after.samplerCreations == baseline.samplerCreations &&
                    after.cache.preparations == baseline.cache.preparations && device->starts == starts &&
                    device->stops == stops,
                "Rack groups retain graph, device and prepared samples");
        auto heldState = std::find_if(after.channels.begin(), after.channels.end(),
                                      [&](const auto &state) { return state.channel == held; });
        require(heldState != after.channels.end() && heldState->midiNotes == 1,
                "Rack groups retain the unrelated scheduled score part");
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 55));
        tick();
        audio.stop();
    }
}
