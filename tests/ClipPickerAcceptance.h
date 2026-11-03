// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "PickerAudioFixture.h"
static void clipPickerTests(const juce::File &folder)
{
    Controller c;
    c.replace(Project::demo());
    auto second = c.project.patterns.front();
    second.id = c.project.allocate();
    second.name = "Second Pattern";
    for (auto &note : second.notes)
        note.id = c.project.allocate();
    c.project.patterns.push_back(second);
    CommandArgs a;
    for (const auto &p : c.project.patterns)
        a.ids.push_back(p.id);
    a.text = "  Verse  ";
    const auto original = c.project;
    const auto before = original.state();
    require(c.execute("pattern.group", a).wasOk(), "Group multi-selected source Patterns");
    auto expected = original;
    for (auto &p : expected.patterns)
        p.group = "Verse";
    require(expected.state().isEquivalentTo(c.project.state()), "Grouping changes only filter metadata");
    const auto grouped = c.project.state();
    c.undoEdit();
    require(before.isEquivalentTo(c.project.state()), "Group exact single Undo");
    c.redoEdit();
    require(grouped.isEquivalentTo(c.project.state()), "Group exact Redo");
    auto bad = a;
    bad.ids.push_back(999999);
    require(c.execute("pattern.group", bad).failed() && grouped.isEquivalentTo(c.project.state()),
            "Partially invalid group selection is atomic");
    bad = a;
    bad.ids.push_back(a.ids.front());
    require(c.execute("pattern.group", bad).failed(), "Duplicate group IDs refused");
    bad = a;
    for (const auto &name : {juce::String::repeatedString("x", 129), juce::String("a\nb")})
    {
        bad.text = name;
        require(c.execute("pattern.group", bad).failed() && grouped.isEquivalentTo(c.project.state()),
                "Invalid group name preserves state");
    }
    const auto path = folder.getChildFile("picker-groups.vibra");
    Project loaded;
    require(saveProject(c.project, path).wasOk() && loadProject(path, loaded).wasOk() &&
                loaded.state().isEquivalentTo(grouped),
            "Schema10 groups disk round trip");
    auto legacy = before.createCopy();
    for (int version : {8, 9})
    {
        legacy.setProperty("version", version, nullptr);
        stripSamplerVoiceFieldsForLegacy(legacy);
        require(Project::parse(legacy, loaded).wasOk() && loaded.version == Project::currentVersion &&
                    loaded.patterns.front().group.isEmpty(),
                "Previous schema migrates empty groups");
    }
    legacy = grouped.createCopy();
    legacy.setProperty("version", 9, nullptr);
    stripSamplerVoiceFieldsForLegacy(legacy);
    const auto protectedState = loaded.state();
    require(Project::parse(legacy, loaded).failed() && protectedState.isEquivalentTo(loaded.state()),
            "Mislabelled group data cannot silently disappear");
    a.text.clear();
    require(c.execute("pattern.group", a).wasOk() && before.isEquivalentTo(c.project.state()),
            "Ungroup removes metadata without touching clips");
    c.undoEdit();
    CommandArgs place;
    place.ids = a.ids;
    place.text = "pattern";
    place.beat = 32;
    place.lane = 3;
    const auto clips = c.project.placements.size();
    require(c.execute("picker.place", place).wasOk(), "Multi-Pattern linked placement");
    for (size_t i = 0; i < a.ids.size(); ++i)
    {
        const auto &clip = c.project.placements[clips + i];
        require(clip.pattern == a.ids[i] && clip.beat == 32 && clip.lane == 3 + int(i) && clip.offset == 0 &&
                    clip.length == c.project.pattern(a.ids[i])->length,
                "Picker placement preserves source links, lengths and aligned consecutive lanes");
    }
    const auto placed = c.project.state();
    c.undoEdit();
    require(grouped.isEquivalentTo(c.project.state()), "Multi-placement is one Undo");
    c.redoEdit();
    require(placed.isEquivalentTo(c.project.state()), "Multi-placement exact Redo");
    for (const auto &kind : {"unknown", "audio", "automation"})
    {
        place.text = kind;
        require(c.execute("picker.place", place).failed() && placed.isEquivalentTo(c.project.state()),
                "Incorrect source kinds preserve project");
    }
    place.text = "pattern";
    place.lane = 127;
    require(c.execute("picker.place", place).failed() && placed.isEquivalentTo(c.project.state()),
            "Multi-source placement cannot spill past last lane");
    place.lane = 0;
    place.ids.push_back(999999);
    require(c.execute("picker.place", place).failed() && placed.isEquivalentTo(c.project.state()),
            "Partial placement failure is atomic including ID allocator");
    auto sources = c.project;
    Channel audio;
    audio.id = sources.allocate();
    audio.name = "Audio source";
    const auto audioFile = pickerAudioFixture(folder);
    require(audioFile.existsAsFile(), "Original Picker audio test fixture");
    audio.source = audioFile.getFullPathName();
    audio.audioClip = true;
    audio.audioDuration = 2.5;
    sources.channels.push_back(audio);
    AutomationSource automation;
    automation.id = sources.allocate();
    automation.length = 3;
    automation.points = {{sources.allocate(), 0, .2f}, {sources.allocate(), 3, .8f}};
    automation.links = {{{AutomationParameter::mixerGain, 0, 0}, 0, 1}};
    sources.automation.sources.push_back(automation);
    c.replace(sources);
    place.ids = {audio.id};
    place.text = "audio";
    require(c.execute("picker.place", place).wasOk() &&
                c.project.audioPlacements.back().channel == audio.id &&
                c.project.audioPlacements.back().duration == 2.5,
            "Picker reuses existing Audio source");
    place.ids = {automation.id};
    place.text = "automation";
    require(c.execute("picker.place", place).wasOk() &&
                c.project.automation.placements.back().source == automation.id &&
                c.project.automation.placements.back().length == 3,
            "Picker reuses Automation source");
    require(saveProject(c.project, path).wasOk() && loadProject(path, loaded).wasOk() &&
                loaded.state().isEquivalentTo(c.project.state()),
            "All picker source kinds round trip");
}
