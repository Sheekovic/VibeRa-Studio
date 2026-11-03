// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "Customization.h"
#include "LiveTimeline.h"
#include "NoteSlice.h"
#include "PianoLoopView.h"
#include <cmath>

inline void pianoLoopSliceTests()
{
    NoteSliceLine defaults;
    const auto legacy = juce::JSON::parse(
        R"({"startBeat":1.0,"endBeat":1.0,"startPitch":-0.5,"endPitch":127.5,"grid":0.25,"allChannels":false})");
    require(!defaults.loopPreview && !sliceLineState(defaults).hasProperty("loopPreview") &&
                juce::JSON::toString(sliceLineState(defaults), true) == juce::JSON::toString(legacy, true),
            "Loop Slice defaults preserve the exact legacy serialized line");
    NoteSliceLine parsed;
    require(parseSliceLine(legacy, parsed).wasOk() && !parsed.loopPreview &&
                sliceLineSchema()["properties"]["loopPreview"]["type"].toString() == "boolean",
            "Legacy Slice parses with a typed optional false loop-preview default");
    defaults.loopPreview = true;
    const auto enabled = sliceLineState(defaults);
    require(enabled["loopPreview"].isBool() && bool(enabled["loopPreview"]) &&
                parseSliceLine(enabled, parsed).wasOk() && parsed.loopPreview,
            "True loop-preview Slice survives exact typed state parsing");
    const auto parsedBefore = juce::JSON::toString(sliceLineState(parsed), true);
    for (const auto &bad : {juce::var(1), juce::var("true"), juce::var{}})
    {
        auto malformed = juce::JSON::parse(juce::JSON::toString(enabled));
        malformed.getDynamicObject()->setProperty("loopPreview", bad);
        require(parseSliceLine(malformed, parsed).failed() &&
                    juce::JSON::toString(sliceLineState(parsed), true) == parsedBefore,
                "Malformed loop-preview Boolean refuses without replacing the prior line");
    }

    Controller controller;
    auto &part = controller.project.patterns.front();
    const auto pattern = part.id, channel = controller.project.channels.front().id,
               ghost = controller.project.channels[1].id;
    part.autoLength = false;
    part.length = 2;
    const auto sourceId = controller.project.allocate(), dormantId = controller.project.allocate(),
               ghostId = controller.project.allocate();
    part.notes = {{sourceId, channel, .5, .5, 64, .63f, true},
                  {dormantId, channel, 2.5, .25, 67, .4f, false, true},
                  {ghostId, ghost, 1.25, .5, 72, .3f, false}};
    part.channelLoops = {{channel, ChannelLoopMode::fixed, .75}};
    const auto initial = controller.project.state();
    const auto allocator = controller.project.nextId;
    CommandArgs args;
    args.pattern = pattern;
    args.channel = channel;
    args.slice = NoteSliceLine{1.375, 1.375, 63.5, 64.5, 0, false, true};
    Customization loopMacro;
    require(loopMacro.addMacro({"macro.loop-slice", "Slice displayed repeat", {{"notes.slice", args, true}}})
                    .wasOk() &&
                int(loopMacro.state()["version"]) == 11,
            "True display-space Slice macro selects profile11");
    juce::TemporaryFile macroFile(".json");
    Customization restoredMacro;
    require(loopMacro.save(macroFile.getFile()).wasOk() && restoredMacro.load(macroFile.getFile()).wasOk() &&
                restoredMacro.macros.size() == 1 &&
                restoredMacro.macros.front().steps.front().parameters.slice.has_value() &&
                restoredMacro.macros.front().steps.front().parameters.slice->loopPreview,
            "Actual macro file save/load retains typed display-space Slice geometry");
    const auto restoredProfile = juce::JSON::toString(restoredMacro.state(), true);
    for (int version = 3; version < 11; ++version)
    {
        auto smuggled = juce::JSON::parse(juce::JSON::toString(loopMacro.state()));
        smuggled.getDynamicObject()->setProperty("version", version);
        const auto refused = restoredMacro.importState(smuggled);
        require(refused.failed() && refused.getErrorMessage().startsWith("loopPreview.profile") &&
                    juce::JSON::toString(restoredMacro.state(), true) == restoredProfile,
                "Older Slice profile cannot smuggle true loop-preview semantics or replace live macros");
    }
    auto unsupportedProfile = juce::JSON::parse(juce::JSON::toString(loopMacro.state()));
    // Profile17 adds typed sampler automation while retaining older Slice recipes.
    // Verify the accepted compatibility boundary before checking a future format.
    unsupportedProfile.getDynamicObject()->setProperty("version", 17);
    require(restoredMacro.importState(unsupportedProfile).wasOk() &&
                juce::JSON::toString(restoredMacro.state(), true) == restoredProfile,
            "Profile17 retains an older Slice recipe without rewriting its semantics");
    // Profile18 adds typed Note modes/Properties while retaining older Slice recipes.
    unsupportedProfile.getDynamicObject()->setProperty("version", 18);
    require(restoredMacro.importState(unsupportedProfile).wasOk() &&
                juce::JSON::toString(restoredMacro.state(), true) == restoredProfile,
            "Profile18 retains an older Slice recipe without rewriting its semantics");
    // Future formats still refuse rather than discarding unsupported payloads.
    unsupportedProfile.getDynamicObject()->setProperty("version", 19);
    require(restoredMacro.importState(unsupportedProfile).failed() &&
                juce::JSON::toString(restoredMacro.state(), true) == restoredProfile,
            "Unsupported macro profile refuses atomically");
    Customization rawMacro;
    auto rawRecipe = args;
    rawRecipe.slice->loopPreview = false;
    require(
        rawMacro.addMacro({"macro.raw-slice", "Slice source", {{"notes.slice", rawRecipe, true}}}).wasOk() &&
            int(rawMacro.state()["version"]) == 3 &&
            !rawMacro.state()["macros"][0]["steps"][0]["parameters"]["slice"].hasProperty("loopPreview"),
        "Raw Slice macro retains profile3 and omits the optional false flag");
    Customization restoredRaw;
    require(restoredRaw.importState(rawMacro.state()).wasOk() &&
                !restoredRaw.macros.front().steps.front().parameters.slice->loopPreview,
            "Legacy profile3 raw Slice still imports without changed semantics");
    std::vector<NoteSliceCut> cuts;
    require(planNoteSlices(controller.project, pattern, channel, {}, *args.slice, cuts).wasOk() &&
                cuts.size() == 1 && cuts.front().id == sourceId && cuts.front().index == 0 &&
                cuts.front().beat == .625 && cuts.front().right == 0 &&
                controller.project.state().isEquivalentTo(initial) && controller.project.nextId == allocator,
            "Displayed1.25 occurrence cut1.375 maps to exact original source beat0.625 without mutation");
    auto raw = *args.slice;
    raw.loopPreview = false;
    require(planNoteSlices(controller.project, pattern, channel, {}, raw, cuts).wasOk() && cuts.empty(),
            "Legacy raw Slice ignores a derived repeat outside its source geometry");
    auto snapped = *args.slice;
    snapped.grid = .25;
    require(planNoteSlices(controller.project, pattern, channel, {}, snapped, cuts).wasOk() &&
                cuts.size() == 1 && cuts.front().beat == .75,
            "Displayed intersection snaps before mapping to original source time");
    const auto revision = controller.revision;
    auto edge = args;
    edge.slice->startBeat = edge.slice->endBeat = 1.25;
    require(controller.execute("notes.slice", edge).wasOk() && controller.revision == revision &&
                controller.project.nextId == allocator && controller.project.state().isEquivalentTo(initial),
            "Derived appearance start is a no-op with no allocator or history change");
    require(controller.execute("notes.slice", args).wasOk(), "Canonical source-bound repeated Slice");
    const auto sliced = controller.project.state();
    const auto &notes = controller.project.pattern(pattern)->notes;
    require(notes.size() == 4 && notes[0].id == sourceId && notes[0].beat == .5 && notes[0].length == .125 &&
                notes[3].id == allocator && notes[3].beat == .625 && notes[3].length == .375 &&
                notes[3].channel == channel && notes[3].pitch == 64 && notes[3].velocity == .63f &&
                !notes[0].step && !notes[3].step && !notes[3].muted,
            "Repeat Slice retains left ID and exact source geometry/properties with one real right ID");
    require(notes[1].id == dormantId && notes[1].beat == 2.5 && notes[1].muted && notes[2].id == ghostId &&
                notes[2].beat == 1.25 && notes[2].length == .5 &&
                controller.project.pattern(pattern)->channelLoops.front().length == .75,
            "Dormant source, other Channel and loop configuration remain unchanged");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(initial), "Repeated Slice is one exact Undo");
    controller.redoEdit();
    require(controller.project.state().isEquivalentTo(sliced), "Repeated Slice exact Redo restores IDs");

    // Independent literal post-Slice score, not generated by the loop projection.
    auto literal = controller.project;
    auto &literalPart = literal.patterns.front();
    literalPart.channelLoops.clear();
    literalPart.notes = {{literal.allocate(), channel, .5, .125, 64, .63f},
                         {literal.allocate(), channel, .625, .375, 64, .63f},
                         {literal.allocate(), channel, 1.25, .125, 64, .63f},
                         {literal.allocate(), channel, 1.375, .375, 64, .63f},
                         {ghostId, ghost, 1.25, .5, 72, .3f}};
    Sample sample;
    sample.oneShot = false;
    sample.data.setSize(2, 32768);
    for (int frame = 0; frame < sample.data.getNumSamples(); ++frame)
        for (int side = 0; side < 2; ++side)
            sample.data.setSample(side, frame, float((frame % 29 - 14) * .01) * (side == 0 ? 1.f : -.5f));
    for (bool patternMode : {true, false})
    {
        auto actualProject = controller.project;
        auto expectedProject = literal;
        if (!patternMode)
        {
            actualProject.placements = {{actualProject.allocate(), pattern, 0, 0, 2, 0}};
            expectedProject.placements = {{expectedProject.allocate(), pattern, 0, 0, 2, 0}};
        }
        auto document = compileLiveDocument(actualProject, patternMode, pattern, 1);
        auto reference = compileLiveDocument(expectedProject, patternMode, pattern, 1);
        auto actualOwner = std::make_unique<VoiceBank>(), expectedOwner = std::make_unique<VoiceBank>();
        auto &actual = *actualOwner, &expected = *expectedOwner;
        actual.prepare(sample, 48000);
        expected.prepare(sample, 48000);
        LiveSequencer actualSequence, expectedSequence;
        juce::AudioBuffer<float> actualPcm(2, 256), expectedPcm(2, 256);
        float peak = 0;
        bool equalPcm = true;
        for (int position = 0; position < 48000; position += 256)
        {
            actualPcm.clear();
            expectedPcm.clear();
            actualSequence.process(*document, channel, position / 48000., 256, 48000, true, actual, actualPcm,
                                   0);
            expectedSequence.process(*reference, channel, position / 48000., 256, 48000, true, expected,
                                     expectedPcm, 0);
            for (int side = 0; side < 2; ++side)
                for (int frame = 0; frame < 256; ++frame)
                    equalPcm &= actualPcm.getSample(side, frame) == expectedPcm.getSample(side, frame);
            peak = std::max(peak, actualPcm.getMagnitude(0, 256));
        }
        require(equalPcm && peak > .01f,
                "Audible loop Slice Pattern/Song PCM exactly matches the independent literal split score");
    }

    controller.undoEdit();
    MacroContext macroContext{pattern, channel, 0, {}, {}};
    require(restoredMacro.runMacro("macro.loop-slice", controller, macroContext).wasOk() &&
                controller.project.state().isEquivalentTo(sliced),
            "Reloaded display-space macro performs the same exact canonical source Slice");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(initial),
            "Reloaded loop Slice macro has one exact Undo");
    require(controller.beginGesture("loop-slice-test") &&
                controller.execute("notes.slice", args, "loop-slice-test").wasOk(),
            "Loop Slice can preview through canonical gesture ownership");
    controller.cancelGesture("loop-slice-test");
    require(controller.project.state().isEquivalentTo(initial),
            "Cancelling repeated Slice restores exact source");

    // A source crossing a cycle can overlap its own derived appearance. One
    // displayed cut must not silently choose between two different source cuts.
    controller.project.pattern(pattern)->notes.front().length = 1.25;
    const auto ambiguousBefore = controller.project.state();
    const auto ambiguousNextId = controller.project.nextId;
    const auto ambiguousRevision = controller.revision;
    cuts = {{123, 456, 7, 8}};
    const auto ambiguous = planNoteSlices(controller.project, pattern, channel, {}, *args.slice, cuts);
    require(ambiguous.failed() && ambiguous.getErrorMessage().startsWith("loopPreview.ambiguousSlice") &&
                cuts.size() == 1 && cuts.front().id == 123 && cuts.front().beat == 8,
            "Ambiguous overlapping cuts refuse without replacing the caller's prior plan");
    require(controller.execute("notes.slice", args).failed() &&
                controller.project.state().isEquivalentTo(ambiguousBefore) &&
                controller.project.nextId == ambiguousNextId && controller.revision == ambiguousRevision,
            "Ambiguous repeated Slice is fully atomic with no allocated IDs or phantom history");
    require(planNoteSlices(controller.project, pattern, channel, {}, raw, cuts).wasOk() && cuts.size() == 1 &&
                cuts.front().id == sourceId && cuts.front().beat == 1.375,
            "False loop-preview flag preserves legacy raw slicing even when appearances overlap");
    args.ids = {ghostId};
    args.slice->startPitch = args.slice->endPitch = 72;
    require(planNoteSlices(controller.project, pattern, channel, args.ids, *args.slice, cuts).wasOk() &&
                cuts.size() == 1 && cuts.front().id == ghostId && cuts.front().index == 2 &&
                cuts.front().beat == 1.375,
            "Explicit IDs retain editable-ghost scope and actual source index");
}
