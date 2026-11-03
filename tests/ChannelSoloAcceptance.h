// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "ChannelSolo.h"
#include "MidiNoteOwners.h"
static void channelSoloTests(const juce::File &folder)
{
    Controller c;
    const auto kick = c.project.channels[0].id, snare = c.project.channels[1].id,
               hat = c.project.channels[2].id;
    c.project.channels[1].muted = true;
    for (const auto &channel : c.project.channels)
        c.project.patterns[0].notes.push_back({c.project.allocate(), channel.id, 0, 2, 60, .7f, false});
    const auto before = c.project.state();
    CommandArgs a;
    a.channel = kick;
    require(c.execute("channel.solo", a).wasOk() && c.project.solo &&
                c.project.solo->mutedBefore == std::vector<Id>{snare},
            "Solo captures original mixed mute mask");
    require(sequence(c.project, true, c.project.patterns[0].id).size() == 1,
            "Score mute compiles only audible Channel; shared live lanes are not part of score");
    const auto soloKick = c.project.state();
    const auto revision = c.revision;
    require(c.execute("channel.solo", a).wasOk() && c.revision == revision, "Explicit solo is idempotent");
    a.channel = hat;
    require(c.execute("channel.solo", a).wasOk() && c.project.solo->mutedBefore == std::vector<Id>{snare} &&
                c.project.channel(kick)->muted && !c.project.channel(hat)->muted,
            "Switching solo preserves original baseline and stable targets");
    const auto soloHat = c.project.state();
    const auto path = folder.getChildFile("active-solo.vibra");
    Project loaded;
    require(saveProject(c.project, path).wasOk() && loadProject(path, loaded).wasOk() &&
                loaded.state().isEquivalentTo(soloHat),
            "Active solo and baseline round trip exactly");
    Controller reopened;
    reopened.replace(loaded);
    require(reopened.execute("channel.unsolo").wasOk() && reopened.project.state().isEquivalentTo(before),
            "Unsolo after reopen restores exact mixed mute state");
    a.enabled = false;
    require(c.execute("channel.solo", a).wasOk() && c.project.state().isEquivalentTo(before),
            "Explicit unsolo restores previous mute states");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(soloHat), "Unsolo one-step Undo restores snapshot");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(before), "Unsolo Redo exact");
    a.enabled = true;
    a.channel = kick;
    require(c.execute("channel.solo", a).wasOk(), "Second solo");
    a.restore = false;
    require(c.execute("channel.unsolo", a).wasOk() && !c.project.solo &&
                std::none_of(c.project.channels.begin(), c.project.channels.end(),
                             [](auto &x) { return x.muted; }),
            "Restore off explicitly unmutes all");
    c.replace(loaded);
    const auto valid = c.project.state();
    for (auto ids : {std::vector<Id>{}, std::vector<Id>{kick, 999999}, std::vector<Id>{kick, kick}})
    {
        a.ids = ids;
        require(c.execute("rack.muteSelected", a).failed() && c.project.state().isEquivalentTo(valid),
                "Empty/stale/duplicate bulk mute atomically refuses");
    }
    a.ids = {kick, hat};
    require(c.execute("rack.unmuteSelected", a).wasOk() && !c.project.solo &&
                !c.project.channel(kick)->muted && !c.project.channel(hat)->muted,
            "Manual bulk edit ends restore session, keeps remaining mask and edits all intended targets");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(valid), "Bulk one Undo restores active solo and baseline");
    a.ids.clear();
    a.channel = 999999;
    require(c.execute("channel.solo", a).failed() && c.project.state().isEquivalentTo(valid),
            "Stale solo atomic");
    a.channel = kick;
    a.enabled = false;
    require(c.execute("channel.solo", a).failed(), "A non-owner cannot unsolo another target");
    a = {};
    a.text = "builtin:hat";
    require(c.execute("channel.add", a).wasOk() && c.project.channels.back().muted,
            "New Channel joins solo mask without sounding unexpectedly");
    const auto newChannel = c.project.channels.back().id;
    require(c.execute("channel.unsolo").wasOk() && !c.project.channel(newChannel)->muted &&
                c.project.channel(snare)->muted,
            "New Channel baseline is restored without losing original mutes");
    auto legacy = Project::empty().state();
    legacy.setProperty("version", 11, nullptr);
    stripSamplerVoiceFieldsForLegacy(legacy);
    legacy.removeChild(legacy.getChildWithName("CHANNEL_SOLO"), nullptr);
    require(Project::parse(legacy, loaded).wasOk() && loaded.version == Project::currentVersion &&
                !loaded.solo,
            "Schema11 migrates without inventing solo or rewriting a file");
    for (int fault = 0; fault < 4; ++fault)
    {
        auto broken = valid.createCopy();
        auto solo = broken.getChildWithName("CHANNEL_SOLO");
        if (fault == 0)
            solo.setProperty("channel", 999999, nullptr);
        if (fault == 1)
            solo.appendChild(solo.getChild(0).createCopy(), nullptr);
        if (fault == 2)
            solo.getChild(0).setProperty("channel", 999999, nullptr);
        if (fault == 3)
            broken.getChildWithName("CHANNELS").getChild(0).setProperty("muted", false, nullptr);
        const auto sentinel = loaded.state();
        require(Project::parse(broken, loaded).failed() && loaded.state().isEquivalentTo(sentinel),
                "Invalid solo data refuses without replacing caller project");
    }
    Customization settings = Customization::familiarDefaults();
    settings.restoreMuteAfterSolo = false;
    require(settings.save(folder.getChildFile("solo-profile.json")).wasOk(), "Solo preference profile save");
    Customization restored;
    require(restored.load(folder.getChildFile("solo-profile.json")).wasOk() && !restored.restoreMuteAfterSolo,
            "Workflow preference round trip");
    auto oldProfile = settings.state();
    oldProfile.getDynamicObject()->removeProperty("restoreMuteAfterSolo");
    require(restored.importState(oldProfile).wasOk() && restored.restoreMuteAfterSolo,
            "Old profile defaults restore on");
    auto invalid = settings.state();
    invalid.getDynamicObject()->setProperty("restoreMuteAfterSolo", "false");
    const auto previous = juce::JSON::toString(restored.state());
    require(restored.importState(invalid).failed() && juce::JSON::toString(restored.state()) == previous,
            "Invalid preference type refuses atomically");
    c.replace(Project::empty());
    for (auto &channel : c.project.channels)
        channel.muted = channel.id != kick;
    a = {};
    a.channel = kick;
    a.enabled = false;
    require(channelIsSoloed(c.project, kick) && c.execute("channel.solo", a).wasOk() &&
                std::none_of(c.project.channels.begin(), c.project.channels.end(),
                             [](auto &x) { return x.muted; }),
            "Manually isolated Channel can unsolo without an unavailable snapshot");
    c.replace(Project::empty());
    c.project.channels[1].muted = true;
    const auto macroBefore = c.project.state();
    settings.addMacro(
        {"macro.solo", "Solo then all", {{"channel.solo", {}, true}, {"channel.unsolo", {}, true}}});
    MacroContext context;
    context.channel = kick;
    require(settings.runMacro("macro.solo", c, context).wasOk() && !c.project.channels[1].muted,
            "Macro uses Workflow preference through canonical solo transitions");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(macroBefore), "Whole solo macro has one exact Undo");
    (void)soloKick;
}
static void midiNoteOwnerTests()
{
    MidiNoteOwners owners;
    std::array<ipc::MidiEvent, 512> events{};
    int count = 0;
    auto emit = [&](const ipc::MidiEvent &e)
    {
        require(count < int(events.size()), "Owned MIDI bounded fixture");
        events[size_t(count++)] = e;
    };
    auto send = [&](int source, int kind, int pitch, int value = 0, int channel = 0)
    {
        owners.process(
            source,
            ipc::MidiEvent{0, {std::uint8_t(kind | channel), std::uint8_t(pitch), std::uint8_t(value)}, 3},
            emit);
    };
    send(0, 0x90, 60, 90);
    send(1, 0x90, 60, 100);
    send(2, 0x90, 60, 80);
    send(0, 0x80, 60);
    send(1, 0x80, 60);
    require(count == 1, "Score mute and keyboard off cannot cancel a typing-held same key");
    send(2, 0x80, 60);
    require(count == 2 && events[1].bytes[0] == 0x80, "Final owner releases shared key once");
    owners.reset();
    count = 0;
    for (int source = 0; source < MidiNoteOwners::sourceCount; ++source)
        send(source, 0x90, 60, 100);
    for (int source = 0; source < MidiNoteOwners::sourceCount - 1; ++source)
        send(source, 0x80, 60);
    require(count == 1, "Score/physical/typing release cannot cancel a preview-held same key");
    send(3, 0x80, 60);
    require(count == 2 && events[1].bytes[0] == 0x80,
            "Fourth preview source releases the shared key only as the final owner");
    owners.reset();
    count = 0;
    for (int source = 0; source < MidiNoteOwners::sourceCount; ++source)
        send(source, 0x90, 60, 100);
    send(3, 0x90, 64, 100);
    send(3, 0xb0, 123);
    require(count == 3 && events[2].bytes[0] == 0x80 && events[2].bytes[1] == 64,
            "Preview panic releases its unique note without cancelling any other held source");
    send(0, 0x80, 60);
    send(1, 0x80, 60);
    require(count == 3, "Preview panic retained physical/typing ownership after score release");
    send(2, 0x80, 60);
    require(count == 4 && events[3].bytes[0] == 0x80 && events[3].bytes[1] == 60,
            "Other sources still pair their final release after a preview panic");
    for (int source = 0; source < MidiNoteOwners::sourceCount - 1; ++source)
    {
        owners.reset();
        count = 0;
        send(source, 0x90, 60, 100);
        send(3, 0x90, 60, 100);
        send(source, 0xb0, 120);
        require(count == 1, "Another lane's panic cannot cancel preview's shared held note");
        send(3, 0x80, 60);
        require(count == 2, "Preview receives its paired release after another lane panics");
    }
    owners.reset();
    count = 0;
    send(0, 0x90, 60, 100);
    auto previewLane = std::make_shared<MidiLane>();
    previewLane->enabled = true;
    MidiMonitor preview;
    preview.bind(previewLane);
    const unsigned char shared[]{0x90, 60, 100}, exclusive[]{0x90, 64, 100};
    previewLane->push(shared, 3);
    previewLane->push(exclusive, 3);
    preview.read();
    for (int i = 0; i < preview.count; ++i)
        owners.process(3, preview.events[std::size_t(i)], emit);
    require(count == 2 && events[1].bytes[1] == 64,
            "Prepared preview lane coalesces a score-held pitch and emits its own separate pitch");
    ++previewLane->epoch; // The same scoped cancellation as AudioEngine::stopChannelPreview.
    preview.read();
    require(preview.panicked && !preview.globalPanic && preview.releaseCount == 2,
            "Preview epoch produces local held-key releases rather than a global panic");
    for (int i = 0; i < preview.count; ++i)
        owners.process(3, preview.events[std::size_t(i)], emit);
    require(count == 3 && events[2].bytes[0] == 0x80 && events[2].bytes[1] == 64,
            "Actual preview epoch cancellation preserves the score's shared held key");
    send(0, 0x80, 60);
    require(count == 4 && events[3].bytes[1] == 60,
            "Score gate still releases normally after actual preview epoch cancellation");
    const auto unchanged = count;
    send(-1, 0x90, 60, 100);
    send(MidiNoteOwners::sourceCount, 0x90, 60, 100);
    owners.process(3, ipc::MidiEvent{0, {0x90, 60, 100}, 2}, emit);
    owners.process(3, ipc::MidiEvent{0, {0x90, 60, 128}, 3}, emit);
    owners.process(3, ipc::MidiEvent{0, {0x90, 60, 100}, 0}, emit);
    require(count == unchanged, "Out-of-bounds sources and malformed short MIDI cannot touch ownership");
    owners.reset();
    count = 0;
    send(0, 0x90, 60, 90);
    send(1, 0x90, 64, 90);
    send(1, 0xb0, 123);
    require(count == 3 && events[2].bytes[0] == 0x80 && events[2].bytes[1] == 64,
            "Lane panic releases its key without all-notes-off over score owner");
    send(0, 0x80, 60);
    require(count == 4, "Score owner still receives paired release after lane panic");
    owners.reset();
    count = 0;
    send(0, 0x90, 60, 90);
    send(0, 0x90, 60, 90);
    send(0, 0x80, 60);
    require(count == 2, "Overlapping score keys do not release before final score owner");
    send(0, 0x90, 60, 0);
    require(count == 3, "Zero-velocity note-on releases final score owner");
    owners.reset();
    count = 0;
    send(0, 0x90, 60, 90);
    send(1, 0x90, 60, 90, 1);
    send(0, 0x80, 60);
    require(count == 3 && events[1].bytes[0] == 0x91, "MIDI channels have independent key ownership");
    const auto allocated = callbackAllocations.load(), destroyed = callbackDestructions.load();
    auto boundedEmit = [&](const ipc::MidiEvent &e)
    {
        if (count < int(events.size()))
            events[size_t(count++)] = e;
    };
    callbackAllocationProbe = true;
    for (int i = 0; i < 200; ++i)
    {
        count = 0;
        owners.reset();
        for (int source = 0; source < MidiNoteOwners::sourceCount; ++source)
            owners.process(source, ipc::MidiEvent{0, {0x90, 60, 100}, 3}, boundedEmit);
        for (int source = 0; source < MidiNoteOwners::sourceCount; ++source)
            owners.process(source, ipc::MidiEvent{0, {0xb0, 123, 0}, 3}, boundedEmit);
        previewLane->push(shared, 3);
        preview.read();
        for (int event = 0; event < preview.count; ++event)
            owners.process(3, preview.events[std::size_t(event)], boundedEmit);
        ++previewLane->epoch;
        preview.read();
        for (int event = 0; event < preview.count; ++event)
            owners.process(3, preview.events[std::size_t(event)], boundedEmit);
        owners.process(-1, ipc::MidiEvent{0, {0x90, 60, 100}, 3}, boundedEmit);
        owners.process(MidiNoteOwners::sourceCount, ipc::MidiEvent{0, {0x90, 60, 100}, 3}, boundedEmit);
    }
    callbackAllocationProbe = false;
    require(callbackAllocations.load() == allocated && callbackDestructions.load() == destroyed,
            "New callback owner merging/reset/panic performs no instrumented allocations or destructions");
}
static void channelSoloLiveTests(const juce::File &folder, const PluginSpec *plugin = nullptr,
                                 bool reorder = false, bool velocity = false, bool swing = false,
                                 bool quantizer = false, bool scoped = false)
{
    juce::String report;
    for (bool patternMode : {true, false})
    {
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
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        require(device != nullptr, "Solo virtual device");
        Controller c;
        auto &p = c.project.patterns[0];
        // Groove end targets can legitimately extend a one-beat Pattern. Keep
        // continuity testing inside a full template cycle, with the untouched
        // score sustaining across that whole loop. Pattern-growth semantics are
        // tested separately; a musical rest is not an audio dropout.
        p.length = quantizer ? 4 : 1;
        p.autoLength = false; // Continuity uses the fixed template cycle described above.
        c.project.tempo = 240;
        const auto pattern = p.id, channel = c.project.channels[0].id, other = c.project.channels[1].id;
        for (int i = 0; i < 2; ++i)
        {
            c.project.channels[size_t(i)].source = "builtin:bass";
            c.project.channels[size_t(i)].sampler.loop = true;
            c.project.channels[size_t(i)].pan = i == 0 ? -1.f : 1.f;
        }
        if (plugin)
        {
            c.project.channels[0].instrument = *plugin;
            c.project.channels[0].instrument->id = c.project.allocate();
        }
        p.notes = {{c.project.allocate(), channel, 0, 1, 60, .7f, false},
                   {c.project.allocate(), other, 0, p.length, 48, .7f, false}};
        if (velocity)
            p.notes[0].step = true;
        if (quantizer)
            p.notes.push_back({c.project.allocate(), channel, .08, .34, 60, .6f, false});
        if (swing)
        {
            c.project.swing = .5;
            p.notes.push_back({c.project.allocate(), channel, .25, .5, 60, .6f, false});
        }
        if (scoped)
        {
            const auto group = c.project.allocate();
            c.project.channelGroups.push_back({group, "Live scoped drums"});
            c.project.channels[0].group = c.project.channels[2].group = group;
            c.project.channels[2].source = "builtin:bass";
            c.project.channels[2].sampler.loop = true;
            p.notes.push_back({c.project.allocate(), c.project.channels[2].id, 0, p.length, 55, .3f, false});
        }
        c.project.placements.push_back({c.project.allocate(), pattern, 0, 0, p.length, 0});
        require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "Solo initial projection");
        audio.setMidiTarget(channel);
        audio.setLooping(true);
        audio.playStop();
        dispatchMessages(20);
        juce::AudioBuffer<float> block(2, 256);
        int callbacks = 0, overruns = 0, silentLive = 0, silentOther = 0, retainedStepEdits = 0;
        double maximum = 0;
        float livePeak = 0;
        int editing = -1;
        const char *phase = "warmup";
        juce::String trace = "callback,edit,phase,left,right,headSeconds,patternBeats,channel,time,playing,"
                             "skip,calls,misses\n";
        auto tick = [&]
        {
            const auto start = juce::Time::getHighResolutionTicks();
            device->process(block);
            const auto ms =
                juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - start) * 1000;
            maximum = std::max(maximum, ms);
            overruns += ms > 256. / 48000. * 1000.;
            ++callbacks;
            const auto diagnostic = audio.projectionDiagnostics(); // Control-thread observation only.
            for (const auto &lane : diagnostic.channels)
                trace += juce::String(callbacks) + "," + juce::String(editing) + "," + phase + "," +
                         juce::String(block.getMagnitude(0, 0, 256), 9) + "," +
                         juce::String(block.getMagnitude(1, 0, 256), 9) + "," +
                         juce::String(diagnostic.headSeconds, 9) + "," +
                         juce::String(c.project.pattern(pattern)->length, 9) + "," +
                         juce::String(lane.channel) + "," + juce::String(lane.lastTime, 9) + "," +
                         juce::String(int(lane.lastPlaying)) + "," + juce::String(lane.lastLeadSkip) + "," +
                         juce::String(juce::int64(lane.renderCalls)) + "," +
                         juce::String(juce::int64(lane.timelineMisses)) + "\n";
            dispatchMessages(plugin ? 6 : 1);
        };
        for (int i = 0; i < 16; ++i)
            tick();
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
        tick();
        const auto before = audio.projectionDiagnostics();
        const auto hostBefore = audio.pluginDiagnostics();
        const int starts = device->starts, stops = device->stops;
        for (int edit = 0; edit < 40; ++edit)
        {
            editing = edit;
            phase = "edit";
            const auto initial = c.project.state();
            const auto head = audio.positionBeat();
            CommandArgs args;
            args.channel = edit % 2 == 0 ? channel : other;
            if (velocity)
            {
                args.channel = channel;
                args.pattern = pattern;
                args.value = edit % 2 == 0 ? .2 : .9;
            }
            if (swing)
            {
                args.channel = channel;
                args.value = .2 + (edit % 5) * .2;
                args.enabled = edit % 2 == 0;
            }
            if (quantizer)
            {
                args.channel = channel;
                args.pattern = pattern;
                args.length = .25;
                args.strength = .25 + (edit % 4) * .25;
                args.text = juce::StringArray{"duration", "end", "leaveDuration", "leaveEnd"}[edit % 4];
                args.groove = factoryGrooves()[1];
                args.velocityStrength = .5;
                if (edit % 4 == 0)
                    args.groove->points[0].velocity = 0;
                args.zeroVelocityDeletes = true;
            }
            juce::String scopedCommand;
            if (scoped)
            {
                args.channel = edit % 3 == 0 ? channel : c.project.channels[2].id;
                if (edit % 3 == 0)
                {
                    scopedCommand = "channel.solo";
                    args.soloScope = "group";
                }
                else if (edit % 3 == 1)
                    scopedCommand = "channel.muteLock";
                else
                {
                    scopedCommand = "rack.muteLock";
                    args.ids = {channel, c.project.channels[2].id};
                }
            }
            require(c.execute(scoped      ? scopedCommand
                              : quantizer ? "notes.quantize"
                              : swing     ? (edit % 3 == 0   ? "project.swing"
                                             : edit % 3 == 1 ? "channel.swingMix"
                                                             : "channel.truncateSwing")
                              : velocity  ? "steps.velocity"
                              : reorder   ? (edit % 2 == 0 ? "channel.moveDown" : "channel.moveUp")
                                          : (edit % 2 == 0 ? "channel.mute" : "channel.solo"),
                              args)
                        .wasOk(),
                    "Live score mute/solo");
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Solo incremental update");
            if (quantizer)
                require(
                    c.project.pattern(pattern)->length == 4 &&
                        std::any_of(c.project.pattern(pattern)->notes.begin(),
                                    c.project.pattern(pattern)->notes.end(), [&](const auto &note)
                                    { return note.channel == other && note.beat == 0 && note.length == 4; }),
                    "Groove continuity fixture preserves full-cycle untouched score and loop length");
            require(audio.playing() && std::abs(audio.positionBeat() - head) < .01,
                    "Solo retains transport/playhead");
            for (int i = 0; i < 4; ++i)
            {
                tick();
                silentLive += block.getMagnitude(0, 0, 256) == 0;
                silentOther += block.getMagnitude(1, 0, 256) == 0;
                livePeak = std::max(livePeak, block.getMagnitude(0, 0, 256));
            }
            phase = "undo";
            c.undoEdit();
            require(c.project.state().isEquivalentTo(initial), "Live mute/solo exact Undo");
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "Solo Undo publication");
            tick();
            tick();
            phase = "redo";
            c.redoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(), "Solo Redo publication");
            tick();
            tick();
            phase = "baseline";
            c.undoEdit();
            require(awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                    "Solo baseline publication");
            tick();
            tick();
            if (velocity && edit % 5 == 0)
            {
                // Keep every original velocity edit. Add activation/history
                // while the same-pitch live key and untouched right lane sound.
                const auto activationBaseline = c.project.state();
                const auto sourceId = c.project.pattern(pattern)->notes.front().id;
                const auto activationHead = audio.positionBeat();
                CommandArgs activation;
                activation.pattern = pattern;
                activation.channel = channel;
                activation.beat = 0;
                activation.enabled = false;
                activation.retainSteps = true;
                require(c.execute("steps.set", activation).wasOk(), "Live retained step removal");
                const auto &source = c.project.pattern(pattern)->notes.front();
                require(c.project.pattern(pattern)->notes.size() == 2 && source.id == sourceId &&
                            source.channel == channel && source.beat == 0 && source.length == 1 &&
                            source.pitch == 60 && source.velocity == .7f && source.step && source.muted,
                        "Live retained source keeps literal identity, pitch, length and velocity");
                const auto activationMuted = c.project.state();
                require(awaitProjection(audio, c.project, patternMode, pattern).wasOk() && audio.playing() &&
                            std::abs(audio.positionBeat() - activationHead) < .01,
                        "Retained activation preserves transport and playhead");
                auto activationTick = [&]
                {
                    tick();
                    silentLive += block.getMagnitude(0, 0, 256) == 0;
                    silentOther += block.getMagnitude(1, 0, 256) == 0;
                    livePeak = std::max(livePeak, block.getMagnitude(0, 0, 256));
                };
                phase = "retained-remove";
                for (int i = 0; i < 4; ++i)
                    activationTick();
                phase = "retained-undo";
                c.undoEdit();
                require(c.project.state().isEquivalentTo(activationBaseline) &&
                            awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                        "Live retained activation exact Undo and publication");
                activationTick();
                activationTick();
                phase = "retained-redo";
                c.redoEdit();
                require(c.project.state().isEquivalentTo(activationMuted) &&
                            awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                        "Live retained activation exact Redo and publication");
                activationTick();
                activationTick();
                phase = "retained-baseline";
                c.undoEdit();
                require(c.project.state().isEquivalentTo(activationBaseline) &&
                            awaitProjection(audio, c.project, patternMode, pattern).wasOk(),
                        "Live retained activation restores original source for subsequent edits");
                activationTick();
                activationTick();
                ++retainedStepEdits;
            }
        }
        require(!velocity || retainedStepEdits == 8, "All added retained-step live cases executed");
        const auto after = audio.projectionDiagnostics();
        folder
            .getChildFile(juce::String(scoped      ? "scoped-solo"
                                       : quantizer ? "quantizer"
                                       : swing     ? "swing"
                                       : velocity  ? "velocity"
                                       : reorder   ? "reorder"
                                                   : "solo") +
                          (patternMode ? "-pattern" : "-song") + "-callbacks.csv")
            .replaceWithText(trace);
        const auto hosts = audio.pluginDiagnostics();
        const auto held = std::find_if(after.channels.begin(), after.channels.end(),
                                       [&](const auto &state) { return state.channel == channel; });
        require(held != after.channels.end() && held->heldInputKeys == 1,
                "Callback readback confirms live input ownership through all score edits");
        report +=
            juce::String(patternMode ? "Pattern" : "Song") +
            " edits=40 callbacks=" + juce::String(callbacks) + " maxMs=" + juce::String(maximum, 4) +
            " overruns=" + juce::String(overruns) + " silentLive=" + juce::String(silentLive) +
            " silentOther=" + juce::String(silentOther) + " liveSamePitchPeak=" + juce::String(livePeak, 6) +
            " graphReplacements=" + juce::String(juce::int64(after.replacements - before.replacements)) +
            " deviceStarts=" + juce::String(device->starts - starts) +
            " deviceStops=" + juce::String(device->stops - stops) +
            " retainedStepEdits=" + juce::String(retainedStepEdits) + "\n";
        if (plugin && !hosts.empty())
            report += "worker=" + juce::String(hosts[0].processId) +
                      " beforeWorker=" + juce::String(hostBefore.empty() ? 0 : hostBefore[0].processId) +
                      " packets=" + juce::String(juce::int64(hosts[0].packets.submitted)) +
                      " missed=" + juce::String(juce::int64(hosts[0].packets.missed)) +
                      " dropped=" + juce::String(juce::int64(hosts[0].packets.dropped)) + "\n";
        folder
            .getChildFile(
                scoped      ? (plugin ? "channel-scoped-solo-vst3-live.txt" : "channel-scoped-solo-live.txt")
                : quantizer ? (plugin ? "quantizer-vst3-live.txt" : "quantizer-live.txt")
                : swing     ? (plugin ? "swing-vst3-live.txt" : "swing-live.txt")
                : velocity  ? (plugin ? "step-velocity-vst3-live.txt" : "step-velocity-live.txt")
                : reorder   ? (plugin ? "channel-reorder-vst3-live.txt" : "channel-reorder-live.txt")
                            : (plugin ? "channel-solo-vst3-live.txt" : "channel-solo-live.txt"))
            .replaceWithText(
                report +
                (quantizer ? "Quantizer four modes with held same-pitch MIDI; virtual "
                 : swing   ? "Global/Channel Swing/truncation with same-pitch held MIDI; virtual "
                 : velocity
                     ? "Step velocity and retained activation/history with same-pitch live input; virtual "
                 : reorder ? "Held live MIDI shares score pitch60 during Channel reordering; virtual "
                           : "Held live MIDI shares score pitch60 on muted instrument; virtual ") +
                "callback measurements only. No physical hardware/listening.\n");
        require(overruns == 0 && silentLive == 0 && silentOther == 0 && livePeak > .001f,
                "Muted score/solo preserves same-pitch held live PCM and unaffected score within callback "
                "deadline");
        require(before.edit == after.edit && before.playbackContext == after.playbackContext &&
                    before.replacements == after.replacements &&
                    before.samplerCreations == after.samplerCreations &&
                    before.cache.preparations == after.cache.preparations && device->starts == starts &&
                    device->stops == stops,
                "Solo retains graph/device/prepared sample identity");
        if (reorder || velocity)
            for (const auto &old : before.channels)
            {
                const auto current =
                    std::find_if(after.channels.begin(), after.channels.end(),
                                 [&](const auto &value) { return value.channel == old.channel; });
                require(current != after.channels.end() && current->track == old.track &&
                            current->sampler == old.sampler && current->sample == old.sample &&
                            current->graphInitialisations == old.graphInitialisations,
                        "Reordering retains every track/sampler/sample and graph initialization identity");
            }
        if (plugin)
            require(hosts.size() == 1 && hostBefore.size() == 1 && hosts[0].alive &&
                        hosts[0].processId == hostBefore[0].processId && hosts[0].packets.missed == 0 &&
                        hosts[0].packets.dropped == 0,
                    "Real provider worker continuity and bounded packets");
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
        tick();
        const auto released = audio.projectionDiagnostics();
        const auto live = std::find_if(released.channels.begin(), released.channels.end(),
                                       [&](auto &s) { return s.channel == channel; });
        require(live != released.channels.end() && live->heldInputKeys == 0,
                "Final live key releases its ownership");
        audio.stop();
    }
}
