// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepShift.h"
#include "Customization.h"
#include "PatternChannelLoops.h"
#include "PatternExtent.h"
#include "StepCompose.h"
#include <limits>

// Original model/score/history/profile checks, not callback/provider qualification.
static void stepShiftTests(const juce::File &folder)
{
    using namespace vibera;
    const auto close = [](double a, double b) { return std::abs(a - b) < 1.e-11; };
    const auto *info = stepShiftInfo();
    require(juce::String(info->id) == "shift" && juce::String(info->command) == "steps.shift" &&
                juce::String(info->field) == "shiftPercent" && juce::String(info->units) == "percent" &&
                info->minimum == 0 && info->maximum == 100 && info->defaultValue == 0 &&
                close(noteShiftDelay(0), 0) && close(noteShiftDelay(50), .12375) &&
                close(noteShiftDelay(100), .2475),
            "Authoritative Shift metadata and independent literal quarter-beat delays");
    const Note oldAggregate{1, 2, 0, .25, 60, .8f, true, false, -.5, 25};
    require(oldAggregate.shiftPercent == 0, "Existing Note aggregates remain neutral/source compatible");

    auto source = Project::empty();
    auto &part = source.patterns.front();
    part.autoLength = false;
    part.length = 4;
    const auto pattern = part.id, channel = source.channels[0].id, other = source.channels[1].id;
    const double beats[] = {0, .25, .25, .5, 3.75};
    const double shifts[] = {25, 50, 100, 0, 75};
    for (size_t i = 0; i < 5; ++i)
        part.notes.push_back({source.allocate(), channel, beats[i], .5 + i * .125, 60 + int(i),
                              i == 2 ? 0.f : .7f, true, i == 1, -.25, 25, shifts[i]});
    part.notes.push_back({source.allocate(), channel, .25, .75, 72, .5f, false, false, .5, -25, 100});
    part.notes.push_back({source.allocate(), channel, 4, .5, 73, .6f, true, true, .1, 10, 50});
    part.notes.push_back({source.allocate(), other, .25, .5, 74, .7f, true, false, -.1, -10, 25});
    require(source.validate().wasOk(), "Original Shift chord/muted/zero/dormant fixture validates");
    const auto baseline = source.state();
    const auto literal = [&](const Project &project, std::initializer_list<double> expected)
    {
        size_t index = 0;
        for (auto value : expected)
            require(close(project.pattern(pattern)->notes[index++].shiftPercent, value),
                    "Shift source values equal independent literals, including retained inactive chords");
        auto unchanged = project;
        for (size_t i = 0; i < 5; ++i)
            unchanged.pattern(pattern)->notes[i].shiftPercent = shifts[i];
        require(unchanged.state().isEquivalentTo(baseline),
                "Shift changes no source positions/lengths/IDs/step/mutes/velocity/pitch/pan/fine or "
                "unrelated notes");
    };
    CommandArgs args;
    args.pattern = pattern;
    args.channel = channel;
    args.value = 50;
    Controller controller;
    controller.replace(source);
    require(controller.execute("steps.shift", args).wasOk(), "Canonical Shift set uses actual Controller");
    literal(controller.project, {50, 50, 50, 50, 50});
    const auto set = controller.project.state();
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline) &&
                !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(set),
            "Shift set is exactly one Undo/Redo");
    controller.replace(source);
    const juce::String owner("original-shift-graph");
    require(controller.beginGesture(owner), "Own actual Shift preview baseline");
    args.text = "scale";
    args.target = part.notes[0].id;
    args.value = 50;
    require(controller.execute("steps.shift", args, owner).wasOk(),
            "Positive Shift anchor proportional scale");
    literal(controller.project, {50, 100, 100, 0, 100});
    require(controller.execute("steps.shift", args, owner).wasOk(),
            "Repeated Shift preview does not compound");
    literal(controller.project, {50, 100, 100, 0, 100});
    args.target = part.notes[3].id;
    args.value = 25;
    require(controller.execute("steps.shift", args, owner).wasOk(), "Zero Shift anchor uses additive lift");
    literal(controller.project, {50, 75, 100, 25, 100});
    args.text = "ramp";
    args.beat = .5;
    args.length = -.5;
    args.value = 100;
    args.offset = 0;
    require(controller.execute("steps.shift", args, owner).wasOk(),
            "Reverse Shift ramp uses raw source beats");
    literal(controller.project, {0, 50, 50, 100, 75});
    args.beat = .25;
    args.length = 0;
    args.offset = 75;
    require(controller.execute("steps.shift", args, owner).wasOk(), "Zero-span Shift ramp chooses endpoint");
    literal(controller.project, {25, 75, 75, 0, 75});
    require(controller.cancelGestureChecked(owner).wasOk() &&
                controller.project.state().isEquivalentTo(baseline) && !controller.undo.canUndo(),
            "Cancelled Shift previews restore exact source/history/allocator");
    args.text = "reset";
    require(controller.beginGesture(owner) && controller.execute("steps.shift", args, owner).wasOk() &&
                controller.endGestureChecked("Shift", owner).wasOk(),
            "Accept a real Shift gesture");
    literal(controller.project, {0, 0, 0, 0, 0});
    const auto reset = controller.project.state();
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline) &&
                !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(reset),
            "Accepted Shift gesture is exactly one action");
    const auto refuse = [&](CommandArgs invalid, const Project &before, Project candidate)
    {
        const auto snapshot = candidate.state();
        require(applyStepShift(candidate, before, invalid).failed() &&
                    candidate.state().isEquivalentTo(snapshot),
                "Direct Shift failure is atomic before writes");
    };
    args = {};
    args.pattern = pattern;
    args.channel = channel;
    for (auto invalid :
         {-1., 100.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
    {
        auto bad = args;
        bad.value = invalid;
        refuse(bad, source, source);
        bad.text = "ramp";
        bad.value = 0;
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
    bad.target = part.notes[0].id;
    auto stale = source;
    stale.pattern(pattern)->notes[0].beat = .5;
    refuse(bad, stale, source);
    bad = args;
    bad.text = "ramp";
    bad.beat = .1;
    refuse(bad, source, source);
    bad.beat = 0;
    bad.length = 4;
    refuse(bad, source, source);
    bad = args;
    bad.text = "invented";
    refuse(bad, source, source);
    bad = args;
    bad.channel = 999999999;
    refuse(bad, source, source);
    bad = args;
    auto invalidClip = source;
    invalidClip.channel(channel)->audioClip = true;
    refuse(bad, invalidClip, invalidClip);
    auto inPlace = source;
    args.text = "scale";
    args.target = part.notes[0].id;
    args.value = 50;
    require(applyStepShift(inPlace, inPlace, args).wasOk(),
            "Aliased direct baseline snapshots all anchor values");
    literal(inPlace, {50, 100, 100, 0, 100});

    auto converted = source;
    StepComposeRecipe recipe;
    recipe.channel = channel;
    recipe.mode = StepComposeMode::manual;
    recipe.manual.assign(16, 0);
    for (int cell : {0, 1, 2, 15})
        recipe.manual[size_t(cell)] = 1;
    recipe.convertMelodic = true;
    recipe.loop = false;
    require(applyStepComposition(converted, pattern, {recipe}).wasOk(),
            "Actual explicit melodic conversion accepts original shifted source notes");
    const auto convertedNote =
        std::find_if(converted.pattern(pattern)->notes.begin(), converted.pattern(pattern)->notes.end(),
                     [&](const Note &note) { return note.id == part.notes[5].id; });
    require(convertedNote != converted.pattern(pattern)->notes.end() && convertedNote->step &&
                convertedNote->beat == .25 && convertedNote->length == .25 &&
                convertedNote->shiftPercent == 100 && convertedNote->pan == .5 &&
                convertedNote->fineCents == -25,
            "Melodic-to-step conversion preserves Shift and other source expression without fake identities");

    // Literal score timing is independent of Burn and of noteShiftDelay as an expected generator.
    auto scoreProject = Project::empty();
    auto &scorePart = scoreProject.patterns.front();
    const auto scorePattern = scorePart.id, scoreChannel = scoreProject.channels.front().id;
    scorePart.autoLength = false;
    scorePart.length = 2;
    scorePart.notes = {{scoreProject.allocate(), scoreChannel, 0, .5, 60, .8f, true, false, 0, 0, 50},
                       {scoreProject.allocate(), scoreChannel, 0, .5, 64, .7f, true, false, 0, 0, 100}};
    auto score = sequence(scoreProject, true, scorePattern);
    require(score.size() == 2 && close(score[0].beat, .12375) && close(score[1].beat, .2475) &&
                close(score[0].length, .5) && close(score[1].length, .5),
            "50/100percent delay raw zero notes to literal .12375/.2475 without shortening source duration");
    scoreProject.swing = .5;
    scorePart.notes.resize(1);
    scorePart.notes.front().beat = .25;
    scorePart.notes.front().length = .8;
    scoreProject.channel(scoreChannel)->truncateSwing = true;
    score = sequence(scoreProject, true, scorePattern);
    require(score.size() == 1 && close(score[0].beat, .43625) && close(score[0].length, .7375),
            "Swing uses raw .25 first; truncateSwing subtracts only literal .0625 Swing, never Shift");
    scoreProject.placements = {{scoreProject.allocate(), scorePattern, 0, 8, .6, .25}};
    score = sequence(scoreProject, false, scorePattern);
    require(score.size() == 1 && close(score[0].beat, 8.18625) && close(score[0].length, .41375),
            "Song placement offset and horizon clip shifted onset exactly without wrapping/seeking");
    scorePart.length = .5;
    scoreProject.channel(scoreChannel)->truncateSwing = false;
    score = sequence(scoreProject, true, scorePattern);
    require(score.size() == 1 && close(score[0].beat, .43625) && close(score[0].length, .06375),
            "Pattern horizon clips draw/play duration after the shifted onset");
    scorePart.length = 2;
    scorePart.channelLoops = {{scoreChannel, ChannelLoopMode::fixed, .75}};
    score = sequence(scoreProject, true, scorePattern);
    require(score.size() == 3 && close(score[0].beat, .43625) && close(score[1].beat, 1.12375) &&
                close(score[2].beat, 1.93625),
            "Each repetition evaluates Swing on its raw occurrence once");
    scoreProject.swing = 0;
    scorePart.length = 1.5;
    scorePart.channelLoops.front().length = .5;
    scorePart.notes = {{scoreProject.allocate(), scoreChannel, 0, .25, 60, .8f, true, false, 0, 0, 50},
                       {scoreProject.allocate(), scoreChannel, .4, .1, 67, .7f, false, false, 0, 0, 100}};
    const auto rawLoop = scoreProject.state();
    score = sequence(scoreProject, true, scorePattern);
    const double onsets[] = {.12375, .62375, .6475, 1.12375, 1.1475};
    require(score.size() == 5, "Shift crossing the last loop boundary is clipped, never wrapped");
    for (size_t i = 0; i < score.size(); ++i)
        require(close(score[i].beat, onsets[i]), "Fixed loop delayed onsets match literal independent score");
    require(scoreProject.state().isEquivalentTo(rawLoop) &&
                close(channelLoopLength(scoreProject, scorePart, scoreChannel), .5),
            "Scheduling never changes source score, raw loop period or allocator");
    for (auto mode : {ChannelLoopMode::step, ChannelLoopMode::beat, ChannelLoopMode::bar})
    {
        scorePart.channelLoops.front() = {scoreChannel, mode, 1};
        const auto period = channelLoopLength(scoreProject, scorePart, scoreChannel);
        auto neutral = scoreProject;
        for (auto &note : neutral.patterns.front().notes)
            note.shiftPercent = 0;
        require(period == channelLoopLength(neutral, neutral.patterns.front(), scoreChannel),
                "Automatic Step/Beat/Bar periods depend only on raw positive sounding note ends");
    }
    scorePart.channelLoops.front() = {scoreChannel, ChannelLoopMode::fixed, .5};
    controller.replace(scoreProject);
    CommandArgs burn;
    burn.pattern = scorePattern;
    burn.channel = scoreChannel;
    const auto preBurn = controller.project.state();
    require(controller.execute("pattern.burnChannelLoop", burn).wasOk(), "Burn actual shifted source loop");
    const auto burned = sequence(controller.project, true, scorePattern);
    require(burned.size() == 5 && controller.project.pattern(scorePattern)->channelLoops.empty(),
            "Burn copies Shift metadata into raw materialized notes, not pre-delayed beats");
    for (size_t i = 0; i < burned.size(); ++i)
        require(close(burned[i].beat, onsets[i]) && close(burned[i].length, score[i].length) &&
                    burned[i].pitch == score[i].pitch,
                "Burn retains literal timing and applies Shift once");
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(preBurn) &&
                controller.redoEditChecked().wasOk(),
            "Burn Shift source/history/IDs remain exactly reversible");
    auto automatic = scoreProject;
    automatic.patterns.front().autoLength = true;
    refreshAutomaticPatternLength(automatic, automatic.patterns.front());
    const auto extent = automatic.patterns.front().length;
    for (auto &note : automatic.patterns.front().notes)
        note.shiftPercent = 0;
    refreshAutomaticPatternLength(automatic, automatic.patterns.front());
    require(automatic.patterns.front().length == extent,
            "Auto Pattern extent remains raw note/fixed-loop based, independent of Shift");

    auto glue = source;
    auto &gluePart = glue.patterns.front();
    gluePart.notes = {{glue.allocate(), channel, 0, .5, 60, .8f, false, false, 0, 0, 50},
                      {glue.allocate(), channel, .5, .5, 60, .8f, false, false, 0, 0, 100}};
    controller.replace(glue);
    CommandArgs join;
    join.pattern = pattern;
    join.channel = channel;
    join.ids = {gluePart.notes[0].id, gluePart.notes[1].id};
    const auto unequal = controller.project.state();
    require(controller.execute("notes.glue", join).wasOk() &&
                controller.project.state().isEquivalentTo(unequal) && !controller.undo.canUndo(),
            "Glue cannot discard unequal source Shift at a shared boundary");
    gluePart.notes[1].shiftPercent = 50;
    controller.replace(glue);
    require(controller.execute("notes.glue", join).wasOk() &&
                controller.project.pattern(pattern)->notes.size() == 1 &&
                controller.project.pattern(pattern)->notes.front().shiftPercent == 50 &&
                close(controller.project.pattern(pattern)->notes.front().length, 1),
            "Equal-Shift Glue preserves ordinary interval union semantics");

    Project loaded;
    const auto file = folder.getChildFile("step-shift.vibra");
    require(saveProject(source, file).wasOk() && loadProject(file, loaded).wasOk() &&
                loaded.state().isEquivalentTo(baseline),
            "Sparse schema32 Shift round trips real project bytes");
    const auto firstNote = [](juce::ValueTree tree)
    {
        for (auto row : tree.getChildWithName("PATTERNS").getChild(0))
            if (row.hasType("NOTE"))
                return row;
        require(false, "Find exact NOTE child");
        return juce::ValueTree{};
    };
    auto legacy = baseline.createCopy();
    legacy.setProperty("version", 31, nullptr);
    stripSamplerFilterForLegacy(legacy);
    for (auto pat : legacy.getChildWithName("PATTERNS"))
        for (auto note : pat)
            if (note.hasType("NOTE"))
                note.removeProperty("shiftPercent", nullptr);
    auto neutral = source;
    for (auto &note : neutral.patterns.front().notes)
        note.shiftPercent = 0;
    require(Project::parse(legacy, loaded).wasOk() && loaded.state().isEquivalentTo(neutral.state()),
            "Genuine previous31 schema defaults absent Shift to neutral without other migrations");
    const auto unchangedLoad = loaded.state();
    auto smuggled = legacy.createCopy();
    firstNote(smuggled).setProperty("shiftPercent", 0, nullptr);
    require(Project::parse(smuggled, loaded).failed() && loaded.state().isEquivalentTo(unchangedLoad),
            "Older schema rejects even explicit neutral Shift without replacing destination");
    for (const auto invalid : {juce::var(true), juce::var("bad"), juce::var(-.1), juce::var(100.1),
                               juce::var(std::numeric_limits<double>::quiet_NaN())})
    {
        auto malformed = baseline.createCopy();
        firstNote(malformed).setProperty("shiftPercent", invalid, nullptr);
        require(Project::parse(malformed, loaded).failed() && loaded.state().isEquivalentTo(unchangedLoad),
                "Malformed/nonfinite/out-of-range Shift parsing is atomic");
    }
    for (auto pat : neutral.state().getChildWithName("PATTERNS"))
        for (auto note : pat)
            if (note.hasType("NOTE"))
                require(!note.hasProperty("shiftPercent"), "Neutral Shift is sparse");

    // Shift applies to all score instruments without declaring native per-note expression support.
    juce::PluginDescription descriptor;
    descriptor.name = "Original absent Shift score instrument";
    descriptor.pluginFormatName = "VST3";
    descriptor.fileOrIdentifier = "original-shift-score.vst3";
    descriptor.isInstrument = true;
    descriptor.numOutputChannels = 2;
    auto instrument = source;
    instrument.channel(channel)->instrument =
        PluginSpec{instrument.allocate(), descriptor.name, descriptor.createXml()->toString(), {}, true};
    controller.replace(instrument);
    args = {};
    args.pattern = pattern;
    args.channel = channel;
    args.value = 50;
    require(instrument.validate().wasOk() && controller.execute("steps.shift", args).wasOk(),
            "Shift is score timing for VST3/native instruments too, without a worker/editor claim");
    Customization settings = Customization::familiarDefaults();
    args.pattern = 0;
    args.channel = 0;
    args.ids = {999999999};
    Macro macro{"macro.original-shift", "Shift current Channel", {{"steps.shift", args, true}}};
    MacroContext context;
    context.pattern = pattern;
    context.channel = channel;
    context.notes = {part.notes[5].id};
    require(settings.addMacro(macro).wasOk(),
            "Existing numeric Shift command needs no new profile by itself");
    controller.replace(source);
    require(settings.runMacro(macro.id, controller, context).wasOk(),
            "Current-context Shift uses current Channel raw step targets, never Piano selected melodic IDs");
    literal(controller.project, {50, 50, 50, 50, 50});
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline),
            "Current-context Shift macro remains one exact Undo");
    CommandArgs imported;
    imported.pattern = pattern;
    imported.channel = channel;
    imported.recordedNotes = {part.notes.front(), part.notes.front()};
    imported.recordedNotes[0].shiftPercent = 50;
    imported.recordedNotes[1].shiftPercent = 100;
    macro = {"macro.original-shift-import", "Import shifted score", {{"notes.import", imported, false}}};
    settings = Customization::familiarDefaults();
    require(settings.addMacro(macro).wasOk(), "Actual import macro accepts shifted typed source rows");
    const auto encoded = settings.state();
    const auto same = [](const juce::var &a, const juce::var &b)
    { return juce::JSON::toString(a) == juce::JSON::toString(b); };
    const auto clone = [](const juce::var &value) { return juce::JSON::parse(juce::JSON::toString(value)); };
    const auto rows = [&](juce::var &profile) -> juce::Array<juce::var> *
    {
        for (auto &entry : *profile["macros"].getArray())
            if (entry["id"].toString() == macro.id)
                return entry["steps"].getArray()->getReference(0)["parameters"]["recordedNotes"].getArray();
        require(false, "Locate authored Shift import payload");
        return nullptr;
    };
    auto inspect = clone(encoded);
    require(int(encoded["version"]) == 13 &&
                rows(inspect)->getReference(0).getDynamicObject()->getProperties().size() == 11,
            "Only nonneutral imported Shift requires profile13 and an eleventh note field");
    Customization restored;
    const auto profileFile = folder.getChildFile("shift-import-profile.json");
    require(settings.save(profileFile).wasOk() && restored.load(profileFile).wasOk() &&
                same(restored.state(), encoded),
            "Actual Shift import profile file round trip is exact");
    controller.replace(source);
    const auto allocation = controller.project.nextId;
    require(restored.runMacro(macro.id, controller, context).wasOk() &&
                controller.project.pattern(pattern)->notes[8].id == allocation &&
                controller.project.pattern(pattern)->notes[9].id == allocation + 1 &&
                controller.project.pattern(pattern)->notes[8].shiftPercent == 50 &&
                controller.project.pattern(pattern)->notes[9].shiftPercent == 100 &&
                !controller.project.pattern(pattern)->notes[8].step,
            "Import allocates distinct fresh IDs, converts melodic and preserves Shift timing metadata");
    const auto importedScore = sequence(controller.project, true, pattern);
    require(std::any_of(importedScore.begin(), importedScore.end(), [&](const auto &note)
                        { return note.note == allocation && close(note.beat, .12375); }) &&
                std::any_of(importedScore.begin(), importedScore.end(), [&](const auto &note)
                            { return note.note == allocation + 1 && close(note.beat, .2475); }),
            "Melodic imported Shift retains literal timing without native parameter dispatch");
    const auto importedState = controller.project.state();
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline) &&
                controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(importedState),
            "Shifted import is one exact model/history transaction");
    for (int old = 1; old <= 12; ++old)
    {
        auto badProfile = clone(encoded);
        badProfile.getDynamicObject()->setProperty("version", old);
        const auto beforeProfile = restored.state();
        require(restored.importState(badProfile).failed() && same(restored.state(), beforeProfile),
                "Older profiles cannot silently drop explicit imported Shift");
    }
    for (const auto invalid : {juce::var(true), juce::var("50"), juce::var(-1), juce::var(101),
                               juce::var(std::numeric_limits<double>::quiet_NaN())})
    {
        auto badProfile = clone(encoded);
        rows(badProfile)->getReference(0).getDynamicObject()->setProperty("shiftPercent", invalid);
        const auto beforeProfile = restored.state();
        require(restored.importState(badProfile).failed() && same(restored.state(), beforeProfile),
                "Malformed Shift source metadata cannot replace actual settings");
    }
    auto neutralPayload = clone(encoded);
    neutralPayload.getDynamicObject()->setProperty("version", 12);
    for (auto &row : *rows(neutralPayload))
        row.getDynamicObject()->removeProperty("shiftPercent");
    require(restored.importState(neutralPayload).wasOk() && int(restored.state()["version"]) == 12,
            "Genuine full ten-field neutral import payload remains profile12");
    auto explicitZero = clone(neutralPayload);
    rows(explicitZero)->getReference(0).getDynamicObject()->setProperty("shiftPercent", 0);
    const auto neutralProfile = restored.state();
    require(restored.importState(explicitZero).failed() && same(restored.state(), neutralProfile),
            "Explicit zero Shift under12 is smuggling; absence alone preserves compatibility");
    auto unsafe = macro;
    unsafe.steps.front().parameters.recordedNotes.front().shiftPercent = 101;
    require(restored.addMacro(unsafe).failed(), "Macro add rejects invalid Shift before publication");
    restored.macros = {unsafe};
    controller.replace(source);
    juce::MemoryBlock savedProfile;
    require(profileFile.loadFileAsData(savedProfile), "Retain actual profile bytes before unsafe save");
    require(restored.runMacro(unsafe.id, controller, context).failed() &&
                controller.project.state().isEquivalentTo(baseline) && !controller.undo.canUndo() &&
                restored.save(profileFile).failed(),
            "Unsafe public macro storage refuses run/save atomically");
    juce::MemoryBlock preservedProfile;
    require(profileFile.loadFileAsData(preservedProfile) && preservedProfile == savedProfile,
            "Rejected Shift save preserves exact old profile file bytes");
}
