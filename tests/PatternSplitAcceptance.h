// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <tuple>

static void patternSplitTests(const juce::File &folder)
{
    auto original = Project::demo();
    const auto source = original.patterns.front().id;
    original.patterns.front().colour = 0xff6584a7;
    original.patterns.front().generation = "historical-source-provenance";
    original.channels[1].muted = true;                     // Muted instruments still own musical data.
    original.channels[2].name = original.channels[0].name; // Names are not identity.
    original.channels.push_back({original.allocate(), "Unused", "builtin:hat", 5, .7f, 0, false});
    const auto spare = original.allocate();
    original.patterns.push_back({spare, "Keep me", 4, {}});
    original.placements = {{original.allocate(), source, 2, 0, 4, 1, false},
                           {original.allocate(), source, 5, 4, 6, 3, false},
                           {original.allocate(), source, 1, 8, 3, .5, true},
                           {original.allocate(), spare, 7, 1, 2, 0, false}};
    require(original.validate().wasOk(), "Split fixture is a valid mixed step/Piano arrangement");
    Controller controller;
    controller.replace(original);
    CommandArgs args;
    args.pattern = source;
    require(controller.execute("pattern.splitChannels", args).wasOk(), "Split by channel command");
    const auto &split = controller.project;
    require(split.validate().wasOk() && split.patterns.size() == original.patterns.size() + 3 &&
                split.patterns.front().id == source && split.patterns.back().id == spare,
            "Split emits Rack-ordered adjacent parts, preserving first source and other Pattern IDs");
    require(original.state()
                    .getChildWithName("CHANNELS")
                    .isEquivalentTo(split.state().getChildWithName("CHANNELS")) &&
                original.state().getChildWithName("MIXER").isEquivalentTo(
                    split.state().getChildWithName("MIXER")),
            "Split reuses exact instrument/sample state, mute, Mixer routing and effects");
    for (size_t channel = 0; channel < 4; ++channel)
    {
        const auto &part = split.patterns[channel];
        require(part.name == original.channels[channel].name &&
                    part.length == original.patterns.front().length &&
                    part.colour == original.patterns.front().colour &&
                    part.generation == original.patterns.front().generation && !part.notes.empty(),
                "Parts use Channel names and retain Pattern length/colour/provenance");
        for (const auto &note : part.notes)
        {
            const auto &notes = original.patterns.front().notes;
            const auto found = std::find_if(notes.begin(), notes.end(),
                                            [&](const auto &before) { return before.id == note.id; });
            require(
                found != notes.end() && note.channel == original.channels[channel].id &&
                    found->channel == note.channel && found->beat == note.beat &&
                    found->length == note.length && found->pitch == note.pitch &&
                    found->velocity == note.velocity && found->step == note.step,
                "Split moves original note IDs and exact step/velocity/timing data without copying voices");
        }
    }
    require(split.placements.size() == 13, "Every linked clip expands once per active part");
    for (const auto &before : original.placements)
    {
        const auto found = std::find_if(split.placements.begin(), split.placements.end(),
                                        [&](const auto &clip) { return clip.id == before.id; });
        require(found != split.placements.end() && found->pattern == before.pattern &&
                    found->lane == before.lane && found->beat == before.beat &&
                    found->length == before.length && found->offset == before.offset &&
                    found->muted == before.muted,
                "Original placement identity/window/mute survives");
        if (before.pattern != source)
            continue;
        for (size_t i = 1; i < 4; ++i)
            require(std::count_if(split.placements.begin(), split.placements.end(),
                                  [&](const auto &clip)
                                  {
                                      return clip.pattern == split.patterns[i].id &&
                                             clip.lane == before.lane && clip.beat == before.beat &&
                                             clip.length == before.length && clip.offset == before.offset &&
                                             clip.muted == before.muted;
                                  }) == 1,
                    "Linked parts retain exact offsets, loop lengths, lanes and instance mute");
    }
    auto musical = [](const Project &project)
    {
        std::vector<std::tuple<Id, double, double, int, float>> result;
        for (const auto &note : sequence(project, false, 0))
            result.emplace_back(note.channel, note.beat, note.length, note.pitch, note.velocity);
        std::sort(result.begin(), result.end());
        return result;
    };
    require(musical(original) == musical(split), "Splitting preserves the complete Song event multiset");
    const auto splitState = split.state();
    const auto path = folder.getChildFile("split-channel-patterns.vibra");
    Project loaded;
    require(saveProject(split, path).wasOk() && loadProject(path, loaded).wasOk() &&
                splitState.isEquivalentTo(loaded.state()),
            "Split sources and linked arrangement round trip exactly");
    AudioEngine engine(false);
    const auto beforeFile = folder.getChildFile("split-before.wav");
    const auto afterFile = folder.getChildFile("split-after.wav");
    require(engine.renderSync(original, beforeFile, 48000, 24, 256).wasOk() &&
                engine.renderSync(split, afterFile, 48000, 24, 256).wasOk(),
            "Original and split Song render");
    double beforeRate = 0, afterRate = 0;
    const auto beforePcm = readWav(beforeFile, beforeRate), afterPcm = readWav(afterFile, afterRate);
    require(beforeRate == afterRate && beforePcm.getNumSamples() == afterPcm.getNumSamples() &&
                beforePcm.getNumChannels() == afterPcm.getNumChannels(),
            "Split preserves render format and duration");
    for (int channel = 0; channel < beforePcm.getNumChannels(); ++channel)
        require(std::equal(beforePcm.getReadPointer(channel),
                           beforePcm.getReadPointer(channel) + beforePcm.getNumSamples(),
                           afterPcm.getReadPointer(channel)),
                "Split preserves every rendered PCM sample");
    controller.undoEdit();
    require(original.state().isEquivalentTo(controller.project.state()), "Whole split is one exact Undo");
    controller.redoEdit();
    require(splitState.isEquivalentTo(controller.project.state()), "Whole split is one exact Redo");
    args.pattern = spare;
    require(controller.execute("pattern.splitChannels", args).failed() &&
                splitState.isEquivalentTo(controller.project.state()),
            "Empty Pattern cannot partially split or allocate IDs");
    args.pattern = -1;
    require(controller.execute("pattern.splitChannels", args).failed() &&
                splitState.isEquivalentTo(controller.project.state()),
            "Missing Pattern cannot split a different source");
    auto full = original;
    while (full.patterns.size() < 1024)
        full.patterns.push_back({full.allocate(), "Existing", 4, {}});
    controller.replace(full);
    args.pattern = source;
    require(controller.execute("pattern.splitChannels", args).failed() &&
                full.state().isEquivalentTo(controller.project.state()),
            "Pattern capacity failure preserves all existing content and next ID");
    controller.replace(original);
    Customization customization = Customization::familiarDefaults();
    Macro macro{"macro.split-test", "Split then rename", {{"pattern.splitChannels", {}, true}}};
    CommandArgs rename;
    rename.text = "Kick variation";
    macro.steps.push_back({"pattern.rename", rename, true});
    require(customization.addMacro(macro).wasOk(), "Split is available to user macros");
    MacroContext context;
    context.pattern = source;
    require(customization.runMacro(macro.id, controller, context).wasOk() && context.pattern == source &&
                controller.project.pattern(source)->name == rename.text,
            "Macro retains the first part as its Pattern context");
    controller.undoEdit();
    require(original.state().isEquivalentTo(controller.project.state()), "Split/rename macro is one Undo");
    folder.getChildFile("pattern-split-verification.txt")
        .replaceWithText("PASS: channel partition, stable note/instrument IDs, linked trimmed/looped/muted "
                         "placements, complete Song events, exact PCM, persistence, Undo/Redo, capacity and "
                         "macro context. Automated model/render evidence; no FL runtime/listening claim.\n");
}
