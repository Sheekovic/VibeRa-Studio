// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PatternExtent.h"
#include "MidiScoreApplication.h"
#include <algorithm>
#include <limits>
#include <set>

static void patternExtentTests(const juce::File &folder)
{
    auto model = Project::empty();
    auto &extent = model.patterns.front();
    const auto owner = model.channels.front().id;
    require(extent.autoLength && extent.numerator == 0 && extent.denominator == 0 &&
                patternNumerator(model, extent) == 4 && patternDenominator(model, extent) == 4 &&
                patternBarBeats(model, extent) == 4,
            "New Pattern defaults to Auto with inherited quarter-note bar units");
    extent.notes = {{model.allocate(), owner, 0, 4, 60, .7f},
                    {model.allocate(), owner, 7, 1, 64, .7f, false, true},
                    {model.allocate(), owner, 8, 1, 67, 0}};
    refreshAutomaticPatternLength(model, extent);
    require(extent.length == 12, "Auto includes muted and zero-velocity stored source ends");
    extent.notes.pop_back();
    refreshAutomaticPatternLength(model, extent);
    require(extent.length == 8, "Exact source boundary does not allocate an extra empty bar");
    extent.channelLoops = {{owner, ChannelLoopMode::fixed, 18.25}};
    for (int refresh = 0; refresh < 8; ++refresh)
    {
        refreshAutomaticPatternLength(model, extent);
        require(extent.length == 20,
                "Auto includes fixed period but never feeds generated repeated long-note ends into extent");
    }
    extent.numerator = 7;
    extent.denominator = 8;
    refreshAutomaticPatternLength(model, extent);
    require(patternBarBeats(model, extent) == 3.5 && extent.length == 21,
            "Pattern 7/8 override rounds source/fixed-period maximum in quarter-note beats");
    extent.channelLoops.clear();
    extent.notes.clear();
    refreshAutomaticPatternLength(model, extent);
    require(extent.length == 3.5, "Empty Auto Pattern retains one effective bar");
    extent.autoLength = false;
    extent.length = .125;
    model.numerator = 3;
    model.denominator = 8;
    extent.numerator = extent.denominator = 0;
    refreshAutomaticPatternLength(model, extent);
    require(extent.length == .125 && patternBarBeats(model, extent) == 1.5,
            "Manual window never changes when inherited document meter changes");

    Controller c;
    const auto pattern = c.project.patterns.front().id;
    const auto channel = c.project.channels[0].id, peer = c.project.channels[1].id;
    const auto first = c.project.allocate(), second = c.project.allocate();
    c.project.patterns.front().notes = {{first, channel, .25, .5, 60, .8f}, {second, peer, .5, .25, 67, .6f}};
    c.project.placements = {{c.project.allocate(), pattern, 0, 2, .5, .25},
                            {c.project.allocate(), pattern, 1, 8, 4, 0}};
    require(c.project.validate().wasOk(),
            "Original extent fixture has genuine source IDs and linked windows");
    const auto placements = c.project.state().getChildWithName("PLACEMENTS");
    auto windowsPreserved = [&]
    {
        require(c.project.state().getChildWithName("PLACEMENTS").isEquivalentTo(placements),
                "Pattern extent/meter edits preserve exact Playlist trims, offsets, lengths and identities");
    };
    auto noOp = [&](const char *command, const CommandArgs &args)
    {
        const auto state = c.project.state();
        const auto revision = c.revision;
        const auto dirty = c.dirty;
        const auto undo = c.undo.getUndoDescriptions(), redo = c.undo.getRedoDescriptions();
        require(c.execute(command, args).wasOk() && c.project.state().isEquivalentTo(state) &&
                    c.revision == revision && c.dirty == dirty && c.undo.getUndoDescriptions() == undo &&
                    c.undo.getRedoDescriptions() == redo,
                "Repeated extent/meter setting is an exact no-op including actual history and dirty state");
    };
    CommandArgs args;
    args.pattern = pattern;
    args.text = "auto";
    noOp("pattern.extent", args);
    args.enabled = false;
    noOp("pattern.meter", args);
    args.text = "manual";
    args.length = 1.25;
    const auto beforeManual = c.project.state();
    require(c.execute("pattern.extent", args).wasOk() && !c.project.pattern(pattern)->autoLength &&
                c.project.pattern(pattern)->length == 1.25,
            "Canonical manual extent is an explicit bounded quarter-beat window");
    const auto manual = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(beforeManual) &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(manual),
            "Extent mode/length is one exact Undo/Redo");
    noOp("pattern.extent", args);
    windowsPreserved();

    CommandArgs note;
    note.pattern = pattern;
    note.channel = channel;
    note.beat = 4;
    note.length = .5;
    note.pitch = 72;
    note.value = .7;
    require(c.execute("notes.add", note).wasOk() && c.project.pattern(pattern)->length == 1.25,
            "Manual Pattern does not regrow when an outside note is edited into the source");
    MidiScore imported;
    imported.notes = {{0, 0, 6, 1, 74, .5f}};
    require(applyMidiScore(c, pattern, channel, imported, true).wasOk() &&
                c.project.pattern(pattern)->length == 1.25,
            "Real canonical MIDI import retains manual source window and dormant imported note");
    CommandArgs take;
    take.pattern = pattern;
    take.enabled = false;
    take.recordedNotes = {{0, channel, 8, .5, 76, .9f}};
    const auto beforeRecording = c.project.state();
    require(c.execute("notes.record", take).wasOk() && c.project.pattern(pattern)->length == 1.25 &&
                c.project.pattern(pattern)->notes.size() == 5,
            "Canonical note-take commit stores outside notes without growing manual window");
    const auto recorded = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(beforeRecording) &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(recorded),
            "Manual-window recording preserves exact one-action history and note IDs");
    require(sequence(c.project, true, pattern).size() == 2,
            "Outside manual-window notes remain stored and dormant instead of extending playback");
    require(c.project.pattern(pattern)->notes[0].id == first &&
                c.project.pattern(pattern)->notes[1].id == second,
            "Extent, import and recording preserve existing stable source-note identities");
    windowsPreserved();
    args.text = "auto";
    require(c.execute("pattern.extent", args).wasOk() && c.project.pattern(pattern)->autoLength &&
                c.project.pattern(pattern)->length == 12 && sequence(c.project, true, pattern).size() == 5,
            "Explicit Auto rounds actual longest stored source end and reveals retained dormant notes");
    args.enabled = true;
    args.numerator = 7;
    args.denominator = 8;
    require(c.execute("pattern.meter", args).wasOk() && c.project.pattern(pattern)->length == 10.5 &&
                patternBarBeats(c.project, *c.project.pattern(pattern)) == 3.5,
            "Canonical Pattern meter recomputes Auto in its own musical units");
    noOp("pattern.meter", args);
    const auto ownMeter = c.project.state();
    CommandArgs documentMeter;
    documentMeter.numerator = 3;
    documentMeter.denominator = 4;
    require(c.execute("project.meter", documentMeter).wasOk() &&
                patternNumerator(c.project, *c.project.pattern(pattern)) == 7 &&
                c.project.pattern(pattern)->length == 10.5,
            "Explicit Pattern meter remains independent of document meter");
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(ownMeter) &&
                c.redoEditChecked().wasOk(),
            "Document meter has exact history without rewriting Pattern override");
    args.enabled = false;
    require(c.execute("pattern.meter", args).wasOk() && c.project.pattern(pattern)->numerator == 0 &&
                c.project.pattern(pattern)->denominator == 0 && c.project.pattern(pattern)->length == 9,
            "Reset meter restores inheritance and recomputes Auto using document three-quarter bar");
    documentMeter.numerator = 5;
    require(c.execute("project.meter", documentMeter).wasOk() && c.project.pattern(pattern)->length == 10 &&
                patternBarBeats(c.project, *c.project.pattern(pattern)) == 5,
            "Changing document meter recomputes inherited Auto extent without rewriting stored notes");
    windowsPreserved();
    args.length = .125;
    require(c.execute("pattern.length", args).wasOk() && !c.project.pattern(pattern)->autoLength &&
                c.project.pattern(pattern)->length == .125,
            "Legacy numeric Pattern length keeps positive sub-quarter compatibility and explicitly selects "
            "Manual");
    noOp("pattern.length", args);
    windowsPreserved();
    args.length = 1.25;
    args.text = "manual";
    require(c.execute("pattern.extent", args).wasOk(), "Restore practical manual window for clone/split");
    args.enabled = true;
    args.numerator = 5;
    args.denominator = 16;
    require(c.execute("pattern.meter", args).wasOk() && c.project.pattern(pattern)->length == 1.25,
            "Manual meter override changes musical grid without resizing manual window");
    const auto cloneBefore = c.project.state();
    require(c.execute("pattern.clone", args).wasOk(), "Clone explicit manual Pattern");
    const auto copyId = c.project.patterns[1].id;
    const auto &copy = *c.project.pattern(copyId);
    require(!copy.autoLength && copy.numerator == 5 && copy.denominator == 16 && copy.length == 1.25 &&
                copy.notes.size() == 5,
            "Clone inherits mode/meter and current extent, including dormant source material");
    std::set<Id> cloneIds;
    for (const auto &n : copy.notes)
    {
        cloneIds.insert(n.id);
        require(std::none_of(c.project.pattern(pattern)->notes.begin(),
                             c.project.pattern(pattern)->notes.end(),
                             [&](const auto &old) { return old.id == n.id; }),
                "Cloned source notes have independent stable IDs");
    }
    const auto cloned = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(cloneBefore) &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(cloned),
            "Clone mode/meter/IDs survive exact Undo/Redo");
    args.pattern = copyId;
    require(c.execute("pattern.splitChannels", args).wasOk() && c.project.patterns.size() == 3,
            "Split cloned manual Pattern into actual Channel parts");
    std::set<Id> splitIds;
    for (std::size_t i = 1; i < c.project.patterns.size(); ++i)
    {
        const auto &part = c.project.patterns[i];
        require(!part.autoLength && part.length == 1.25 && part.numerator == 5 && part.denominator == 16,
                "Every manual split part inherits exact extent and local meter");
        for (const auto &n : part.notes)
            require(splitIds.insert(n.id).second, "Split preserves unique original clone-note ownership");
    }
    require(splitIds == cloneIds,
            "Split partitions every dormant and audible clone note without recycling IDs");
    const auto split = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(cloned) &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(split),
            "Split mode/meter/source identity has exact one-action Undo/Redo");
    windowsPreserved();
    const auto file = folder.getChildFile("pattern-extent-roundtrip.vibra");
    Project restored;
    require(!file.exists() && saveProject(c.project, file).wasOk() && loadProject(file, restored).wasOk() &&
                restored.state().isEquivalentTo(split),
            "Schema30 Auto/manual/meter, dormant source IDs and linked windows round trip exactly");

    auto refused = [&](const char *command, const CommandArgs &bad)
    {
        const auto state = c.project.state();
        const auto revision = c.revision;
        const auto dirty = c.dirty;
        const auto undo = c.undo.getUndoDescriptions(), redo = c.undo.getRedoDescriptions();
        require(c.execute(command, bad).failed() && c.project.state().isEquivalentTo(state) &&
                    c.revision == revision && c.dirty == dirty && c.undo.getUndoDescriptions() == undo &&
                    c.undo.getRedoDescriptions() == redo,
                "Invalid Pattern extent/meter is atomic including allocator and both real history branches");
    };
    args.pattern = pattern;
    args.text = "manual";
    for (double length : {0., -.25, .125, .3, 1000000.25, std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity()})
    {
        auto bad = args;
        bad.length = length;
        refused("pattern.extent", bad);
    }
    auto bad = args;
    bad.text = "automatic";
    refused("pattern.extent", bad);
    bad = args;
    bad.pattern = 999999999;
    refused("pattern.extent", bad);
    bad.enabled = true;
    refused("pattern.meter", bad);
    for (const auto [numerator, denominator] : {std::pair{0, 4}, std::pair{33, 4}, std::pair{4, 0},
                                                std::pair{4, 1}, std::pair{4, 3}, std::pair{4, 32}})
    {
        bad = args;
        bad.enabled = true;
        bad.numerator = numerator;
        bad.denominator = denominator;
        refused("pattern.meter", bad);
    }

    Controller automatic;
    auto &autoPart = automatic.project.patterns.front();
    const auto autoId = autoPart.id;
    autoPart.numerator = 3;
    autoPart.denominator = 8;
    autoPart.notes = {{automatic.project.allocate(), automatic.project.channels[0].id, 0, .5, 60, .8f},
                      {automatic.project.allocate(), automatic.project.channels[1].id, 4, .5, 64, .6f}};
    refreshAutomaticPatternLength(automatic.project, autoPart);
    require(autoPart.length == 4.5, "Auto split source has exact three-eighth-bar boundary");
    CommandArgs divide;
    divide.pattern = autoId;
    require(automatic.execute("pattern.clone", divide).wasOk() && automatic.project.patterns[1].autoLength &&
                automatic.project.patterns[1].length == 4.5,
            "Auto clone retains current extent and mode before independent edits");
    require(automatic.execute("pattern.splitChannels", divide).wasOk() &&
                automatic.project.patterns[0].autoLength && automatic.project.patterns[1].autoLength &&
                automatic.project.patterns[0].length == 1.5 && automatic.project.patterns[1].length == 4.5 &&
                automatic.project.patterns[0].numerator == 3 &&
                automatic.project.patterns[1].denominator == 8,
            "Auto split parts recompute independently from their own score while inheriting local meter");
    const auto autoFile = folder.getChildFile("pattern-extent-auto-roundtrip.vibra");
    Project autoRead;
    require(!autoFile.exists() && saveProject(automatic.project, autoFile).wasOk() &&
                loadProject(autoFile, autoRead).wasOk() &&
                autoRead.state().isEquivalentTo(automatic.project.state()),
            "Automatic split and cloned extents retain mode, independent lengths and local meter on disk");

    auto legacy = split.createCopy();
    legacy.setProperty("version", 29, nullptr);
    stripSamplerFilterForLegacy(legacy);
    const auto intact = restored.state();
    require(Project::parse(legacy, restored).failed() && restored.state().isEquivalentTo(intact),
            "Old schema29 cannot smuggle new Pattern extent/meter fields, destination unchanged");
    for (auto part : legacy.getChildWithName("PATTERNS"))
        for (const auto *field : {"autoLength", "numerator", "denominator"})
            part.removeProperty(field, nullptr);
    require(Project::parse(legacy, restored).wasOk() && restored.version == Project::currentVersion,
            "Genuine schema29 without new fields migrates");
    for (const auto &part : restored.patterns)
        require(!part.autoLength && part.numerator == 0 && part.denominator == 0 && part.length == 1.25,
                "Old Pattern migrates to manual/inherited meter with its exact audible window");
    auto expectedLegacy = c.project;
    for (auto &part : expectedLegacy.patterns)
    {
        part.autoLength = false;
        part.numerator = part.denominator = 0;
    }
    require(
        restored.state().isEquivalentTo(expectedLegacy.state()),
        "Migration changes only explicit new defaults, retaining source IDs, notes, windows and allocator");
    auto malformed = c.project.state();
    malformed.getChildWithName("PATTERNS").getChild(0).removeProperty("denominator", nullptr);
    const auto destination = restored.state();
    require(Project::parse(malformed, restored).failed() && restored.state().isEquivalentTo(destination),
            "Partial current-schema meter refuses atomically");
    {
        auto stale = c.project.state();
        auto part = stale.getChildWithName("PATTERNS").getChild(0);
        part.setProperty("autoLength", true, nullptr);
        part.setProperty("length", .25, nullptr);
        Project reopened;
        require(Project::parse(stale, reopened).wasOk(), "Current Auto file with stale cached extent loads");
        auto expected = c.project;
        auto &automatic = expected.patterns.front();
        automatic.autoLength = true;
        refreshAutomaticPatternLength(expected, automatic);
        require(reopened.state().isEquivalentTo(expected.state()),
                "Admitted Auto load recomputes only the derived window; all stored score and placement IDs "
                "survive");
    }
    {
        Controller paste;
        auto &target = paste.project.patterns.front();
        const auto targetId = target.id, channelId = paste.project.channels.front().id;
        const auto sourceId = paste.project.allocate();
        const auto sourceNote = paste.project.allocate();
        paste.project.patterns.push_back(
            {sourceId, "Paste source", 12, {{sourceNote, channelId, 8, .5, 60, .8f}}});
        CommandArgs args;
        args.pattern = targetId;
        args.channel = channelId;
        args.target = sourceId;
        args.value = double(channelId);
        const auto baseline = paste.project.state();
        require(paste.execute("channel.paste", args).wasOk() && paste.project.pattern(targetId)->length == 12,
                "Pasting a long Channel score grows the Auto target");
        const auto longScore = paste.project.state();
        require(paste.undoEditChecked().wasOk() && paste.project.state().isEquivalentTo(baseline) &&
                    paste.redoEditChecked().wasOk() && paste.project.state().isEquivalentTo(longScore),
                "Channel paste Auto growth preserves exact source IDs and one-action history");
        CommandArgs shorten;
        shorten.pattern = sourceId;
        shorten.ids = {sourceNote};
        shorten.beat = -8;
        shorten.length = -.25;
        require(paste.execute("notes.move", shorten).wasOk(),
                "Source paste variation uses canonical note movement");
        require(paste.execute("channel.paste", args).wasOk() && paste.project.pattern(targetId)->length == 4,
                "Replacing a long Channel score with a short paste shrinks Auto");
    }
    {
        Controller protectedWindow;
        const auto target = protectedWindow.project.patterns.front().id;
        const auto baseline = protectedWindow.project.state();
        protectedWindow.applicationChangeAdmission = [&](const Project &next)
        {
            return next.pattern(target) && next.pattern(target)->length == 4
                       ? juce::Result::ok()
                       : juce::Result::fail("recording.patternExtentBusy: Protected test window");
        };
        CommandArgs add;
        add.pattern = target;
        add.channel = protectedWindow.project.channels.front().id;
        add.beat = 8;
        add.length = 1;
        require(protectedWindow.execute("notes.add", add).failed() &&
                    protectedWindow.project.state().isEquivalentTo(baseline) &&
                    protectedWindow.revision == 0 && !protectedWindow.dirty &&
                    !protectedWindow.undo.canUndo(),
                "Final application recording admission refuses Auto window change before any "
                "canonical/history mutation");
        add.beat = 0;
        require(protectedWindow.execute("notes.add", add).wasOk(),
                "The same protected window still admits ordinary in-range note edits");
    }
    {
        auto clockProject = Project::empty();
        auto &part = clockProject.patterns.front();
        part.numerator = 7;
        part.denominator = 8;
        part.channelLoops = {{clockProject.channels.front().id, ChannelLoopMode::beat, 1}};
        part.notes = {{clockProject.allocate(), clockProject.channels.front().id, 0, .4, 60, .8f}};
        refreshAutomaticPatternLength(clockProject, part);
        const auto patternClock = compileLiveDocument(clockProject, true, part.id, 1);
        const auto songClock = compileLiveDocument(clockProject, false, part.id, 2);
        require(
            patternClock && songClock && patternClock->numerator == 7 && patternClock->denominator == 8 &&
                songClock->numerator == 4 && songClock->denominator == 4 &&
                channelLoopLength(clockProject, part, clockProject.channels.front().id) == .5,
            "Prepared Pattern guide/plugin clock and Beat loops use local meter; Song clock stays global");
        juce::MemoryBlock bytes;
        require(writeMidiScore(part, clockProject.channels.front().id, 120, 4, 4, bytes).wasOk(),
                "MIDI score export accepts independent Pattern meter");
        juce::MidiFile midi;
        juce::MemoryInputStream input(bytes, false);
        require(midi.readFrom(input) && midi.getNumTracks() == 1, "Actual Pattern MIDI bytes decode");
        bool found = false;
        for (int i = 0; i < midi.getTrack(0)->getNumEvents(); ++i)
        {
            const auto &message = midi.getTrack(0)->getEventPointer(i)->message;
            if (message.isTimeSignatureMetaEvent())
            {
                int numerator = 0, denominator = 0;
                message.getTimeSignatureInfo(numerator, denominator);
                require(numerator == 7 && denominator == 8,
                        "Actual MIDI time signature is the source Pattern's7/8");
                found = true;
            }
        }
        require(found, "Export contains actual local time-signature metadata");
    }
    {
        Controller generated;
        generated.replace(Project::empty());
        const auto before = generated.project.state();
        CommandArgs accepted;
        accepted.channel = generated.project.channels.front().id;
        accepted.text = "Explicit region with trailing rests";
        accepted.length = 8;
        accepted.enabled = true;
        accepted.recordedNotes = {{0, 0, 0, .25, 60, .7f, false}};
        require(generated.execute("coproducer.accept", accepted).wasOk(),
                "Short generated score accepts into its explicit eight-beat region");
        require(!generated.project.patterns.back().autoLength &&
                    generated.project.patterns.back().length == 8 &&
                    generated.project.placements.back().length == 8,
                "Explicit accepted rests remain Manual rather than shrinking to occupied bars");
        const auto result = generated.project.state();
        const auto file = folder.getChildFile("accepted-region-with-rests.vibra");
        require(saveProject(generated.project, file).wasOk(), "Actual accepted-region project saves");
        Project reopened;
        require(loadProject(file, reopened).wasOk() && reopened.state().isEquivalentTo(result),
                "Accepted region, notes, allocator and links survive actual save/reopen exactly");
        require(generated.undoEditChecked().wasOk() && generated.project.state().isEquivalentTo(before),
                "Accepted explicit-region creation has exact single Undo");
        require(generated.redoEditChecked().wasOk() && generated.project.state().isEquivalentTo(result),
                "Accepted explicit-region creation has exact Redo including trailing silence");
    }
}
