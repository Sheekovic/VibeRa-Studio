// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepRelease.h"
#include "Customization.h"
#include "PatternChannelLoops.h"
#include <limits>

// Original model/profile/prepared-score checks, not native audio qualification.
static void stepReleaseTests(const juce::File &folder)
{
    using namespace vibera;
    const auto close = [](double a, double b) { return std::abs(a - b) < 1.e-12; };
    const auto *info = stepReleaseInfo();
    require(juce::String(info->id) == "release" && juce::String(info->command) == "steps.release" &&
                juce::String(info->field) == "releaseVelocity" && juce::String(info->units) == "normalized" &&
                info->minimum == 0 && info->maximum == 1 && info->defaultValue == 0,
            "Authoritative Release velocity metadata is normalized0..1 with explicit legacy0 default");
    require(
        noteReleaseMidiVelocity(0) == 0 && noteReleaseMidiVelocity(.25) == 32 &&
            noteReleaseMidiVelocity(.5) == 64 && noteReleaseMidiVelocity(.75) == 95 &&
            noteReleaseMidiVelocity(1) == 127 && noteReleaseMidiVelocity(-1) == 0 &&
            noteReleaseMidiVelocity(2) == 127 &&
            noteReleaseMidiVelocity(std::numeric_limits<double>::quiet_NaN()) == 0 &&
            noteReleaseMidiVelocity(std::numeric_limits<double>::infinity()) == 0,
        "Independent literal note-off byte conversion preserves zero and defensively bounds invalid inputs");
    const Note oldNote{1, 2, 0, .25, 60, .8f, true, false, 0, 0, 0, 0};
    const ScheduledNote oldScheduled{2, 0, .25, 60, .8f, 1, 0, true, 0, 0, 0};
    require(oldNote.releaseVelocity == 0 && oldScheduled.releaseVelocity == 0,
            "Existing Note/ScheduledNote aggregates preserve zero release data without source rewrites");
    auto source = Project::empty();
    auto &part = source.patterns.front();
    part.autoLength = false;
    part.length = 4;
    const auto pattern = part.id, channel = source.channels.front().id, other = source.channels[1].id;
    const double beats[] = {0, .25, .25, .5, 3.75}, values[] = {.25, .5, 1, 0, .75};
    for (size_t i = 0; i < 5; ++i)
        part.notes.push_back({source.allocate(), channel, beats[i], .5, 60 + int(i), i == 2 ? 0.f : .7f, true,
                              i == 1, -.25, 25, 50, i == 0 ? 10 : 0, values[i]});
    part.notes.push_back({source.allocate(), channel, .25, 1, 70, .4f, false, false, .5, -25, 100, 0, .9});
    part.notes.push_back({source.allocate(), channel, 4, .5, 71, .5f, true, true, .2, 10, 0, 0, .8});
    part.notes.push_back({source.allocate(), other, 0, .5, 72, .6f, true, false, -.2, -10, 0, 0, .7});
    require(source.validate().wasOk(), "Original Release chord/inactive/dormant source validates");
    const auto before = source.state();
    const auto literal = [&](const Project &project, std::initializer_list<double> expected)
    {
        size_t index = 0;
        for (auto value : expected)
            require(close(project.pattern(pattern)->notes[index++].releaseVelocity, value),
                    "Release source values equal independent literals including inactive chord members");
        auto normalized = project;
        for (size_t i = 0; i < 5; ++i)
            normalized.pattern(pattern)->notes[i].releaseVelocity = values[i];
        require(
            normalized.state().isEquivalentTo(before),
            "Only Release changes; exact source IDs/timing/length/mute/other expression/allocator preserved");
    };
    Controller c;
    c.replace(source);
    CommandArgs args;
    args.pattern = pattern;
    args.channel = channel;
    args.value = .5;
    require(c.execute("steps.release", args).wasOk(), "Canonical Release set uses real Controller");
    literal(c.project, {.5, .5, .5, .5, .5});
    const auto set = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before) && !c.undo.canUndo() &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(set),
            "One exact Release set UndoRedo");
    c.replace(source);
    const juce::String owner("original-release-graph");
    require(c.beginGesture(owner), "Own immutable Release baseline");
    args.text = "scale";
    args.target = part.notes[0].id;
    args.value = .5;
    require(c.execute("steps.release", args, owner).wasOk(),
            "Positive Release anchor uses proportional scale");
    literal(c.project, {.5, 1, 1, 0, 1});
    require(c.execute("steps.release", args, owner).wasOk(), "Repeated Release scale never compounds");
    literal(c.project, {.5, 1, 1, 0, 1});
    args.target = part.notes[3].id;
    args.value = .25;
    require(c.execute("steps.release", args, owner).wasOk(), "Zero Release anchor applies additive lift");
    literal(c.project, {.5, .75, 1, .25, 1});
    args.text = "ramp";
    args.beat = .5;
    args.length = -.5;
    args.value = 1;
    args.offset = 0;
    require(c.execute("steps.release", args, owner).wasOk(), "Reverse Release ramp");
    literal(c.project, {0, .5, .5, 1, .75});
    args.beat = .25;
    args.length = 0;
    args.offset = .75;
    require(c.execute("steps.release", args, owner).wasOk(), "Zero-span Release ramp chooses endpoint");
    literal(c.project, {.25, .75, .75, 0, .75});
    require(c.cancelGestureChecked(owner).wasOk() && c.project.state().isEquivalentTo(before) &&
                !c.undo.canUndo(),
            "Cancel restores exact Release source/history without committing");
    args.text = "reset";
    require(c.beginGesture(owner) && c.execute("steps.release", args, owner).wasOk() &&
                c.endGestureChecked("Release velocity", owner).wasOk(),
            "Accept one canonical Release gesture");
    literal(c.project, {0, 0, 0, 0, 0});
    const auto reset = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before) && !c.undo.canUndo() &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(reset),
            "Exact accepted Release UndoRedo");
    const auto refuse = [&](CommandArgs invalid, const Project &baseline, Project candidate)
    {
        const auto snapshot = candidate.state();
        require(applyStepRelease(candidate, baseline, invalid).failed() &&
                    candidate.state().isEquivalentTo(snapshot),
                "Direct Release validation completes before any mutation");
    };
    args = {};
    args.pattern = pattern;
    args.channel = channel;
    for (double invalid :
         {-.01, 1.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        auto bad = args;
        bad.value = invalid;
        refuse(bad, source, source);
        bad.value = 0;
        bad.text = "ramp";
        bad.offset = invalid;
        refuse(bad, source, source);
    }
    for (auto ids :
         {std::vector<Id>{part.notes[0].id, part.notes[0].id},
          std::vector<Id>{part.notes[0].id, part.notes[5].id},
          std::vector<Id>{part.notes[0].id, part.notes[6].id},
          std::vector<Id>{part.notes[0].id, part.notes[7].id}, std::vector<Id>{part.notes[0].id, 999999999}})
    {
        auto bad = args;
        bad.ids = ids;
        refuse(bad, source, source);
    }
    auto bad = args;
    bad.text = "scale";
    bad.target = part.notes[5].id;
    refuse(bad, source, source);
    auto stale = source;
    stale.pattern(pattern)->notes[0].beat = .5;
    bad.target = part.notes[0].id;
    refuse(bad, stale, source);
    bad = args;
    bad.text = "ramp";
    bad.beat = .1;
    refuse(bad, source, source);
    bad.beat = 0;
    bad.length = 4;
    refuse(bad, source, source);
    bad = args;
    bad.text = "unknown";
    refuse(bad, source, source);
    bad = args;
    bad.pattern = 999999999;
    refuse(bad, source, source);
    auto invalidClip = source;
    invalidClip.channel(channel)->audioClip = true;
    refuse(args, invalidClip, invalidClip);
    auto alias = source;
    args.text = "scale";
    args.target = part.notes[0].id;
    args.value = .5;
    require(applyStepRelease(alias, alias, args).wasOk(),
            "Aliased direct Release scale snapshots original anchor values");
    literal(alias, {.5, 1, 1, 0, 1});

    require(juce::String(noteReleaseAudioResponse(source.channels.front())) == "ignored" &&
                juce::String(noteReleaseAudioResponse(*invalidClip.channel(channel))) == "unavailable",
            "Sampler audio ignores Release velocity; Audio Clip has no score-release capability");
    auto instrument = source;
    auto native = synth::nativeDescription();
    instrument.channel(channel)->instrument =
        PluginSpec{instrument.allocate(), native.name, native.createXml()->toString(), {}, true};
    c.replace(instrument);
    args = {};
    args.pattern = pattern;
    args.channel = channel;
    args.value = .5;
    require(instrument.validate().wasOk() && c.execute("steps.release", args).wasOk() &&
                juce::String(noteReleaseAudioResponse(*instrument.channel(channel))) == "ignored",
            "Exact native Synth is editable MIDI data but truthfully reports ignored audio response");
    native.uniqueId ^= 1;
    auto spoof = *instrument.channel(channel);
    spoof.instrument->descriptor = native.createXml()->toString();
    require(juce::String(noteReleaseAudioResponse(spoof)) == "instrument_defined",
            "Near-native spoof is never labelled the known ignored Synth implementation");
    juce::PluginDescription external;
    external.name = "Original absent release instrument";
    external.pluginFormatName = "VST3";
    external.fileOrIdentifier = "original-release-model.vst3";
    external.isInstrument = true;
    external.numOutputChannels = 2;
    instrument.channel(channel)->instrument->descriptor = external.createXml()->toString();
    require(juce::String(noteReleaseAudioResponse(*instrument.channel(channel))) == "instrument_defined",
            "External instrument audio response remains implementation-defined, never universal ADSR");

    auto musical = Project::empty();
    auto &score = musical.patterns.front();
    score.autoLength = false;
    score.length = 2;
    const auto scorePattern = score.id, scoreChannel = musical.channels.front().id,
               sourceId = musical.allocate();
    score.notes = {{sourceId, scoreChannel, 0, .5, 60, .8f, true, false, -.25, 25, 50, 10, .75}};
    score.channelLoops = {{scoreChannel, ChannelLoopMode::fixed, .75}};
    const auto musicalBefore = musical.state();
    const auto allocation = musical.nextId;
    const auto scheduled = sequence(musical, true, scorePattern);
    require(scheduled.size() == 12 && musical.nextId == allocation &&
                musical.state().isEquivalentTo(musicalBefore),
            "Repeat/Channel-loop scheduling never consumes source IDs or mutates raw score");
    for (size_t i = 0; i < scheduled.size(); ++i)
        require(scheduled[i].note == sourceId && scheduled[i].repeatOrdinal == i % 4 &&
                    scheduled[i].placement == (i < 4   ? 0
                                               : i < 8 ? -1
                                                       : -2) &&
                    scheduled[i].releaseVelocity == .75,
                "Every prepared Repeat and loop hit carries exact Release with distinct occurrence identity");
    musical.placements = {{musical.allocate(), scorePattern, 0, 8, 1, .25}};
    const auto placed = sequence(musical, false, scorePattern);
    require(placed.size() == 6, "Independent Clip source window preserves six visible repeated releases");
    for (const auto &note : placed)
        require(note.releaseVelocity == .75, "Song source offset/crop never drops Release velocity");
    c.replace(musical);
    CommandArgs burn;
    burn.pattern = scorePattern;
    burn.channel = scoreChannel;
    const auto beforeBurn = c.project.state();
    require(c.execute("pattern.burnChannelLoop", burn).wasOk() &&
                c.project.pattern(scorePattern)->notes.size() == 3,
            "Raw Channel-loop Burn retains Repeat and Release metadata");
    for (const auto &note : c.project.pattern(scorePattern)->notes)
        require(note.releaseVelocity == .75, "Burn source copies retain Release exactly");
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(beforeBurn),
            "Release-bearing Burn has exact Undo");
    auto chopped = source;
    c.replace(chopped);
    CommandArgs chop;
    chop.pattern = pattern;
    chop.channel = channel;
    chop.ids = {part.notes[0].id};
    chop.length = .25;
    require(c.execute("notes.chop", chop).wasOk(), "Actual Chop source transform");
    int pieces = 0;
    for (const auto &note : c.project.pattern(pattern)->notes)
        if (note.pitch == 60 && note.channel == channel)
        {
            ++pieces;
            require(note.releaseVelocity == .25, "Chop copies retain Release value");
        }
    require(pieces == 2 && c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before),
            "Chop Release/source history exact");
    auto glue = Project::empty();
    auto &g = glue.patterns.front();
    g.autoLength = false;
    g.length = 2;
    const auto gluePattern = g.id, glueChannel = glue.channels.front().id;
    g.notes = {{glue.allocate(), glueChannel, 0, .5, 60, .8f, false, false, 0, 0, 0, 0, .25},
               {glue.allocate(), glueChannel, .5, .5, 60, .8f, false, false, 0, 0, 0, 0, .75}};
    CommandArgs join;
    join.pattern = gluePattern;
    join.channel = glueChannel;
    join.ids = {g.notes[0].id, g.notes[1].id};
    c.replace(glue);
    const auto unequal = c.project.state();
    require(c.execute("notes.glue", join).wasOk() && c.project.state().isEquivalentTo(unequal) &&
                !c.undo.canUndo(),
            "Unequal Release values cannot disappear at a Glue boundary");
    g.notes[1].releaseVelocity = .25;
    c.replace(glue);
    const auto equal = c.project.state();
    require(c.execute("notes.glue", join).wasOk() && c.project.pattern(gluePattern)->notes.size() == 1 &&
                c.project.pattern(gluePattern)->notes.front().releaseVelocity == .25 &&
                c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(equal),
            "Equal-release Glue keeps value and exact Undo");

    Project loaded;
    const auto projectFile = folder.getChildFile("step-release.vibra");
    require(saveProject(source, projectFile).wasOk() && loadProject(projectFile, loaded).wasOk() &&
                loaded.state().isEquivalentTo(before),
            "Sparse schema34 exact release source file roundtrip");
    const auto firstNote = [](juce::ValueTree tree)
    {
        for (auto row : tree.getChildWithName("PATTERNS").getChild(0))
            if (row.hasType("NOTE"))
                return row;
        require(false, "Locate actual NOTE child");
        return juce::ValueTree{};
    };
    auto legacy = before.createCopy();
    legacy.setProperty("version", 33, nullptr);
    stripSamplerFilterForLegacy(legacy);
    for (auto pat : legacy.getChildWithName("PATTERNS"))
        for (auto note : pat)
            if (note.hasType("NOTE"))
                note.removeProperty("releaseVelocity", nullptr);
    auto neutral = source;
    for (auto &note : neutral.patterns.front().notes)
        note.releaseVelocity = 0;
    require(Project::parse(legacy, loaded).wasOk() && loaded.state().isEquivalentTo(neutral.state()),
            "Genuine previous33 absence migrates to exact neutral release0");
    const auto priorLoad = loaded.state();
    auto oldExplicit = legacy.createCopy();
    firstNote(oldExplicit).setProperty("releaseVelocity", 0, nullptr);
    require(Project::parse(oldExplicit, loaded).failed() && loaded.state().isEquivalentTo(priorLoad),
            "Old schema cannot smuggle even0 Release");
    for (const auto invalid : {juce::var(true), juce::var("garbage"), juce::var(-.01), juce::var(1.01),
                               juce::var(std::numeric_limits<double>::quiet_NaN()),
                               juce::var(std::numeric_limits<double>::infinity())})
    {
        auto state = before.createCopy();
        firstNote(state).setProperty("releaseVelocity", invalid, nullptr);
        require(Project::parse(state, loaded).failed() && loaded.state().isEquivalentTo(priorLoad),
                "Invalid release parse cannot replace destination");
    }
    for (auto pat : neutral.state().getChildWithName("PATTERNS"))
        for (auto note : pat)
            if (note.hasType("NOTE"))
                require(!note.hasProperty("releaseVelocity"),
                        "Neutral Release omits the new source attribute");

    Customization settings = Customization::familiarDefaults();
    args = {};
    args.value = .5;
    args.ids = {999999999};
    Macro macro{"macro.original-release", "Release current Channel", {{"steps.release", args, true}}};
    MacroContext context;
    context.pattern = pattern;
    context.channel = channel;
    context.notes = {part.notes[5].id};
    require(settings.addMacro(macro).wasOk(), "Release numeric command needs no new profile by itself");
    c.replace(source);
    require(settings.runMacro(macro.id, c, context).wasOk(),
            "Release macro uses current source steps, not Piano melodic selection");
    literal(c.project, {.5, .5, .5, .5, .5});
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before),
            "Release macro one exact Undo");
    CommandArgs imported;
    imported.pattern = pattern;
    imported.channel = channel;
    imported.recordedNotes = {part.notes[0], part.notes[0]};
    imported.recordedNotes[0].releaseVelocity = .25;
    imported.recordedNotes[1].releaseVelocity = .75;
    macro = {"macro.original-release-import", "Import release metadata", {{"notes.import", imported, false}}};
    settings = Customization::familiarDefaults();
    require(settings.addMacro(macro).wasOk(), "Full release-bearing source payload admitted");
    const auto encoded = settings.state();
    const auto clone = [](const juce::var &v) { return juce::JSON::parse(juce::JSON::toString(v)); };
    const auto same = [](const juce::var &a, const juce::var &b)
    { return juce::JSON::toString(a) == juce::JSON::toString(b); };
    const auto rows = [&](juce::var &profile) -> juce::Array<juce::var> *
    {
        for (auto &row : *profile["macros"].getArray())
            if (row["id"].toString() == macro.id)
                return row["steps"].getArray()->getReference(0)["parameters"]["recordedNotes"].getArray();
        require(false, "Find actual Release import macro");
        return nullptr;
    };
    auto inspect = clone(encoded);
    require(int(encoded["version"]) == 15 &&
                rows(inspect)->getReference(0).getDynamicObject()->getProperties().size() == 13,
            "Nonneutral Release payload requires profile15 and preserves all13 source fields");
    Customization restored;
    const auto profileFile = folder.getChildFile("release-import-profile.json");
    require(settings.save(profileFile).wasOk() && restored.load(profileFile).wasOk() &&
                same(encoded, restored.state()),
            "Actual Release macro profile file roundtrip is exact");
    c.replace(source);
    const auto nextId = c.project.nextId;
    require(restored.runMacro(macro.id, c, context).wasOk() &&
                c.project.pattern(pattern)->notes[8].id == nextId &&
                c.project.pattern(pattern)->notes[9].id == nextId + 1 &&
                c.project.pattern(pattern)->notes[8].releaseVelocity == .25 &&
                c.project.pattern(pattern)->notes[9].releaseVelocity == .75 &&
                !c.project.pattern(pattern)->notes[8].step,
            "Import keeps Release through melodic conversion and allocates distinct fresh IDs for repeated "
            "metadata");
    const auto importedState = c.project.state();
    require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before) &&
                c.redoEditChecked().wasOk() && c.project.state().isEquivalentTo(importedState),
            "Release import one exact UndoRedo");
    for (int version = 1; version <= 14; ++version)
    {
        auto old = clone(encoded);
        old.getDynamicObject()->setProperty("version", version);
        const auto prior = restored.state();
        require(restored.importState(old).failed() && same(prior, restored.state()),
                "Older profile rejects imported Release presence");
    }
    for (const auto invalid : {juce::var(true), juce::var("0.5"), juce::var(-.01), juce::var(1.01),
                               juce::var(std::numeric_limits<double>::quiet_NaN())})
    {
        auto malformed = clone(encoded);
        rows(malformed)->getReference(0).getDynamicObject()->setProperty("releaseVelocity", invalid);
        const auto prior = restored.state();
        require(restored.importState(malformed).failed() && same(prior, restored.state()),
                "Release macro rejects typed string/bool/nonfinite/range atomically");
    }
    auto compatible = clone(encoded);
    compatible.getDynamicObject()->setProperty("version", 14);
    for (auto &row : *rows(compatible))
        row.getDynamicObject()->removeProperty("releaseVelocity");
    require(restored.importState(compatible).wasOk() && int(restored.state()["version"]) == 14,
            "Absent Release with Repeat stays14");
    auto explicitZero = clone(compatible);
    rows(explicitZero)->getReference(0).getDynamicObject()->setProperty("releaseVelocity", 0);
    const auto prior = restored.state();
    require(restored.importState(explicitZero).failed() && same(prior, restored.state()),
            "Even0 Release presence requires15");
    for (auto &row : *rows(compatible))
        row.getDynamicObject()->removeProperty("repeatMode");
    compatible.getDynamicObject()->setProperty("version", 13);
    require(restored.importState(compatible).wasOk() && int(restored.state()["version"]) == 13,
            "Absent Release/Repeat with Shift stays13");
    for (auto &row : *rows(compatible))
        row.getDynamicObject()->removeProperty("shiftPercent");
    compatible.getDynamicObject()->setProperty("version", 12);
    require(restored.importState(compatible).wasOk() && int(restored.state()["version"]) == 12,
            "Neutral complete ten-field import remains12");
    auto unsafe = macro;
    unsafe.steps.front().parameters.recordedNotes.front().releaseVelocity = 1.01;
    require(restored.addMacro(unsafe).failed(), "Malformed release source rejected at macro add");
    restored.macros = {unsafe};
    c.replace(source);
    juce::MemoryBlock saved;
    require(profileFile.loadFileAsData(saved) && restored.runMacro(unsafe.id, c, context).failed() &&
                c.project.state().isEquivalentTo(before) && !c.undo.canUndo() &&
                restored.save(profileFile).failed(),
            "Unsafe public Release storage cannot run or overwrite real settings");
    juce::MemoryBlock afterFile;
    require(profileFile.loadFileAsData(afterFile) && afterFile == saved,
            "Failed Release save preserves exact file bytes");
}
