// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PatternSplitAcceptance.h"
#include "PatternTransposeAcceptance.h"
#include "ClipPickerAcceptance.h"

static void patternManagementTests(const juce::File &folder)
{
    patternSplitTests(folder);
    patternTransposeTests(folder);
    clipPickerTests(folder);
    Controller controller;
    controller.replace(Project::demo());
    for (int i = 0; i < 2; ++i)
        controller.project.patterns.push_back(
            {controller.project.allocate(), "Unused " + juce::String(i + 1), 4, {}});
    const auto original = controller.project;
    const auto source = original.patterns.front().id;
    CommandArgs args;
    args.pattern = source;
    args.text = "  Verse bass  ";
    args.value = double(0xff5c93d1u);
    require(controller.execute("pattern.properties", args).wasOk(), "Pattern properties command");
    require(controller.project.pattern(source)->name == "Verse bass" &&
                controller.project.pattern(source)->colour == 0xff5c93d1u,
            "Pattern name/colour apply atomically");
    const auto coloured = controller.project.state();
    controller.undoEdit();
    require(original.state().isEquivalentTo(controller.project.state()), "Pattern properties one Undo");
    controller.redoEdit();
    require(coloured.isEquivalentTo(controller.project.state()), "Pattern properties exact Redo");
    args.text = "invalid partial update";
    args.value = 123;
    require(controller.execute("pattern.properties", args).failed() &&
                coloured.isEquivalentTo(controller.project.state()),
            "Invalid colour cannot partially rename");
    for (auto invalid :
         {juce::String("  "), juce::String("line\nbreak"), juce::String::repeatedString("x", 257)})
    {
        args.text = invalid;
        require(controller.execute("pattern.rename", args).failed() &&
                    coloured.isEquivalentTo(controller.project.state()),
                "Invalid Pattern rename is inert");
    }
    args.lane = int(controller.project.patterns.size()) - 1;
    require(controller.execute("pattern.move", args).wasOk() &&
                controller.project.patterns.back().id == source,
            "Pattern moves by order, not identity");
    const auto moved = controller.project.state();
    args.lane = int(controller.project.patterns.size());
    require(controller.execute("pattern.move", args).failed() &&
                moved.isEquivalentTo(controller.project.state()),
            "Out-of-range reorder preserves document");
    controller.undoEdit();
    require(coloured.isEquivalentTo(controller.project.state()), "Pattern move exact Undo");
    controller.redoEdit();
    require(moved.isEquivalentTo(controller.project.state()), "Pattern move exact Redo");
    // Name, colour and selector order must not alter the audible arrangement.
    AudioEngine engine(false);
    const auto before = folder.getChildFile("pattern-order-before.wav");
    const auto after = folder.getChildFile("pattern-order-after.wav");
    require(engine.renderSync(original, before, 48000, 24, 256).wasOk() &&
                engine.renderSync(controller.project, after, 48000, 24, 256).wasOk(),
            "Pattern metadata/order render");
    double beforeRate = 0, afterRate = 0;
    auto beforePcm = readWav(before, beforeRate), afterPcm = readWav(after, afterRate);
    require(beforeRate == afterRate && beforePcm.getNumSamples() == afterPcm.getNumSamples() &&
                beforePcm.getNumChannels() == afterPcm.getNumChannels(),
            "Pattern metadata/order preserves WAV format/duration");
    for (int channel = 0; channel < beforePcm.getNumChannels(); ++channel)
        require(std::equal(beforePcm.getReadPointer(channel),
                           beforePcm.getReadPointer(channel) + beforePcm.getNumSamples(),
                           afterPcm.getReadPointer(channel)),
                "Pattern metadata/reorder preserves exact PCM");
    const auto file = folder.getChildFile("pattern-management.vibra");
    require(saveProject(controller.project, file).wasOk(), "Pattern properties/order save");
    Project loaded;
    require(loadProject(file, loaded).wasOk() && moved.isEquivalentTo(loaded.state()),
            "Pattern properties/order disk round trip");
    auto malformed = loaded.state();
    malformed.getChildWithName("PATTERNS").getChild(0).setProperty("colour", "broken", nullptr);
    require(Project::parse(malformed, loaded).failed() && moved.isEquivalentTo(loaded.state()),
            "Malformed colour preserves loaded project");
    controller.replace(original);
    args.pattern = source;
    require(controller.execute("pattern.clone", args).wasOk(), "Clone selected Pattern");
    const auto &copy = controller.project.patterns[1];
    require(copy.id != source && copy.notes.size() == original.patterns[0].notes.size(),
            "Clone follows source in selector");
    for (size_t i = 0; i < copy.notes.size(); ++i)
        require(copy.notes[i].id != original.patterns[0].notes[i].id &&
                    copy.notes[i].channel == original.patterns[0].notes[i].channel,
                "Clone owns note IDs, reuses instruments");
    const auto copied = controller.project.state();
    require(controller.execute("pattern.insert", args).wasOk() &&
                controller.project.patterns[0].notes.empty() && controller.project.patterns[1].id == source,
            "Insert empty Pattern before source");
    controller.undoEdit();
    require(copied.isEquivalentTo(controller.project.state()), "Insert exact Undo");
    args.pattern = source;
    const auto beforeDelete = controller.project.state();
    const auto channels = controller.project.channels.size(),
               audioClips = controller.project.audioPlacements.size();
    require(controller.execute("pattern.delete", args).wasOk() && !controller.project.pattern(source),
            "Delete source Pattern");
    require(std::none_of(controller.project.placements.begin(), controller.project.placements.end(),
                         [source](const auto &p) { return p.pattern == source; }) &&
                controller.project.channels.size() == channels &&
                controller.project.audioPlacements.size() == audioClips,
            "Delete removes linked instances without deleting instruments/audio clips");
    auto deleted = controller.project.state();
    controller.undoEdit();
    require(beforeDelete.isEquivalentTo(controller.project.state()), "Delete and dependent clips one Undo");
    controller.redoEdit();
    require(deleted.isEquivalentTo(controller.project.state()), "Pattern delete exact Redo");
    controller.replace(Project::empty());
    auto single = controller.project.state();
    args.pattern = controller.project.patterns.front().id;
    require(controller.execute("pattern.delete", args).wasOk() && controller.project.patterns.size() == 1 &&
                controller.project.patterns[0].id != args.pattern && controller.project.validate().wasOk(),
            "Deleting final Pattern preserves usable empty project");
    controller.undoEdit();
    require(single.isEquivalentTo(controller.project.state()), "Final Pattern deletion exact Undo");
    auto defaults = Customization::familiarDefaults();
    auto old = defaults.state();
    old.getDynamicObject()->removeProperty("knownScopes");
    auto *bindings = old["bindings"].getArray();
    for (int i = bindings->size(); --i >= 0;)
    {
        auto name = (*bindings)[i]["action"].toString();
        if (int((*bindings)[i]["scope"]) == int(Scope::patterns) ||
            (name.startsWith("pattern.") && name != "pattern.add" && name != "pattern.clone" &&
             name != "pattern.length"))
            bindings->remove(i);
    }
    auto *known = old["knownActions"].getArray();
    for (int i = known->size(); --i >= 0;)
    {
        auto name = (*known)[i].toString();
        if (name.startsWith("pattern.") && name != "pattern.add" && name != "pattern.clone" &&
            name != "pattern.length")
            known->remove(i);
    }
    Customization migrated;
    require(migrated.importState(old).wasOk(), "Old profile migrates new Pattern scope");
    const juce::KeyPress del(juce::KeyPress::deleteKey);
    require(migrated.resolve(del, Scope::patterns) == "pattern.delete" &&
                migrated.resolve(del, Scope::piano) == "notes.delete" &&
                migrated.resolve(del, Scope::playlist) == "clip.delete",
            "Pattern Delete preserves existing note/clip Delete contexts");
    migrated.unbind("pattern.delete", Scope::patterns);
    const auto profile = folder.getChildFile("pattern-shortcuts.json");
    require(migrated.save(profile).wasOk(), "Pattern scope profile save");
    Customization roundTrip;
    require(roundTrip.load(profile).wasOk() && roundTrip.resolve(del, Scope::patterns).isEmpty(),
            "Deliberate Pattern unbinding survives reload");
    auto malformedScopes = roundTrip.state();
    malformedScopes.getDynamicObject()->setProperty("knownScopes",
                                                    juce::Array<juce::var>{juce::String("bad")});
    require(roundTrip.importState(malformedScopes).failed() &&
                roundTrip.resolve(del, Scope::patterns).isEmpty(),
            "Invalid scope inventory cannot partially import");
    folder.getChildFile("pattern-management-verification.txt")
        .replaceWithText("PASS: atomic properties, invalid-input preservation, stable-ID reorder, exact PCM "
                         "equivalence, disk round trip, "
                         "clone/insert ordering, dependent-placement deletion, last-pattern invariant, exact "
                         "undo/redo. No FL runtime parity/listening claim.\n");
}
