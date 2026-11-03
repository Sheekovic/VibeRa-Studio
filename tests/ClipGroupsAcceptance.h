// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/ClipGroups.h"
static Project groupedClipFixture(const juce::File &folder)
{
    const auto source = folder.getChildFile("original-group-audio.wav");
    juce::AudioBuffer<float> pcm(2, 48000);
    for (int i = 0; i < 48000; ++i)
        for (int ch = 0; ch < 2; ++ch)
            pcm.setSample(ch, i, float(.15 * std::sin(i * .043)));
    auto stream = source.createOutputStream();
    require(bool(stream), "Group audio fixture stream");
    stream->setPosition(0);
    stream->truncate();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.release(), 48000., 2, 16, {}, 0));
    require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, 48000), "Actual original group WAV");
    writer.reset();
    auto p = Project::empty();
    auto &pattern = p.patterns.front();
    pattern.length = 8;
    pattern.autoLength = false; // Group actions retain exact linked source windows.
    pattern.notes = {{p.allocate(), p.channels.front().id, 0, 1, 60, .7f, false}};
    p.placements.push_back({p.allocate(), pattern.id, 1, 1, 2, 0});
    p.placements.push_back({p.allocate(), pattern.id, 7, 20, 2, 0});
    Channel audio;
    audio.id = p.allocate();
    audio.name = "GROUP AUDIO";
    audio.source = source.getFullPathName();
    audio.audioClip = true;
    audio.audioDuration = 1;
    p.channels.push_back(audio);
    p.audioPlacements.push_back({p.allocate(), audio.id, 2, 2, 0, 1});
    AutomationSource automation;
    automation.id = p.allocate();
    automation.length = 8;
    automation.name = "Group gain";
    automation.points = {{p.allocate(), 0, .75f}, {p.allocate(), 8, .75f}};
    automation.links = {{{AutomationParameter::mixerGain, 0, 0}, 0, 1}};
    p.automation.sources.push_back(automation);
    p.automation.placements.push_back({p.allocate(), automation.id, 3, 2, 0, 3});
    require(p.validate().wasOk(), "Heterogeneous group project validates");
    return p;
}
static void clipGroupTests(const juce::File &folder)
{
    Controller c;
    c.replace(groupedClipFixture(folder));
    const auto pattern = c.project.placements.front().id, audio = c.project.audioPlacements.front().id,
               automation = c.project.automation.placements.front().id;
    const auto initial = c.project.state();
    CommandArgs args;
    args.ids = {pattern, audio, automation};
    require(c.execute("clip.group", args).wasOk() && c.project.clipGroups.size() == 1 &&
                c.project.clipGroups.front().members.size() == 3,
            "Canonical heterogeneous Clip grouping");
    const auto grouped = c.project.state();
    const auto groupId = c.project.clipGroups.front().id;
    const auto revision = c.revision;
    require(c.execute("clip.group", args).wasOk() && c.revision == revision,
            "Grouping same membership is idempotent");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(initial), "Group one exact Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(grouped), "Group exact Redo/stable identity");
    require(expandedClipSelection(c.project, {audio}).size() == 3, "Member expands across all Clip types");
    args.ids = {pattern};
    args.beat = 1;
    args.lane = 2;
    args.length = 0;
    require(c.execute("clip.move", args).wasOk() && c.project.placements[0].beat == 2 &&
                c.project.audioPlacements[0].beat == 3 && c.project.automation.placements[0].beat == 4 &&
                c.project.placements[0].lane == 3 && c.project.audioPlacements[0].lane == 4 &&
                c.project.automation.placements[0].lane == 5 && c.project.placements[1].beat == 20,
            "One member moves entire heterogeneous group with exact spacing/nonmembers");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(grouped), "Group move exact Undo");
    args.beat = -100;
    args.lane = -127;
    require(c.execute("clip.move", args).wasOk() && c.project.placements[0].beat == 0 &&
                c.project.audioPlacements[0].beat == 1 && c.project.automation.placements[0].beat == 2 &&
                c.project.placements[0].lane == 0 && c.project.audioPlacements[0].lane == 1,
            "Common boundary clamp preserves group geometry");
    c.undoEdit();
    c.clipGroupingEnabled = false;
    args.beat = 1;
    args.lane = 0;
    require(c.execute("clip.move", args).wasOk() && c.project.placements[0].beat == 2 &&
                c.project.audioPlacements[0].beat == 2 && c.project.clipGroups.front().id == groupId,
            "Disabling grouping edits one member without losing membership");
    c.undoEdit();
    c.clipGroupingEnabled = true;
    args.grouping = false;
    require(c.execute("clip.move", args).wasOk() && c.project.audioPlacements[0].beat == 2,
            "Explicit command bypass uses same model behavior");
    c.undoEdit();
    args.grouping.reset();
    args.beat = 8;
    require(c.execute("clip.duplicate", args).wasOk() && c.project.clipGroups.size() == 2 &&
                c.project.placements.size() == 3 && c.project.audioPlacements.size() == 2 &&
                c.project.automation.placements.size() == 2,
            "Group duplicate copies every type");
    require(c.project.clipGroups.back().id != groupId &&
                c.project.placements.back().pattern == c.project.placements.front().pattern &&
                c.project.audioPlacements.back().channel == c.project.audioPlacements.front().channel &&
                c.project.automation.placements.back().source ==
                    c.project.automation.placements.front().source,
            "Copies form independent group while all musical sources stay linked");
    const auto duplicated = c.project.state();
    args.ids = {c.project.audioPlacements.back().id};
    require(c.execute("clip.delete", args).wasOk() && c.project.clipGroups.size() == 1 &&
                c.project.clipGroups.front().id == groupId && c.project.audioPlacements.size() == 1,
            "Deleting copied member deletes copied group only");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(duplicated), "Delete restores exact copy IDs/group");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(grouped), "Duplicate exact Undo");
    args.ids = {pattern};
    args.beat = 2.5;
    require(c.execute("clip.slice", args).wasOk() && c.project.clipGroups.front().members.size() == 5,
            "Slice halves retain group membership and unsliced member");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(grouped), "Slice group/source windows exact Undo");
    args.ids = {audio};
    require(c.execute("clip.ungroup", args).wasOk() && c.project.clipGroups.empty(),
            "Ungroup from one member dissolves its group");
    c.undoEdit();
    args.ids = {pattern, 999999};
    const auto before = c.project.state();
    require(c.execute("clip.move", args).failed() && c.project.state().isEquivalentTo(before),
            "Unknown mixed target fails atomically");
    args.ids = {pattern};
    args.grouping = false;
    require(c.execute("clip.delete", args).wasOk() && c.project.clipGroups.front().members.size() == 2,
            "Individual delete prunes only removed member");
    args.ids = {audio};
    require(c.execute("clip.delete", args).wasOk() && c.project.clipGroups.empty(),
            "Singleton groups dissolve after deletion");
    c.undoEdit();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(grouped), "Pruning Undo restores all IDs and group");
    Project restored;
    require(Project::parse(grouped, restored).wasOk() && restored.state().isEquivalentTo(grouped),
            "Schema16 exact group round trip");
    auto legacy = initial.createCopy();
    legacy.setProperty("version", 15, nullptr);
    stripNoteMuteForLegacy(legacy);
    legacy.removeChild(legacy.getChildWithName("CLIP_GROUPS"), nullptr);
    require(Project::parse(legacy, restored).wasOk() && restored.clipGroups.empty(),
            "Genuine schema15 migrates without groups");
    const auto unchanged = restored.state();
    for (int bad = 0; bad < 5; ++bad)
    {
        auto tree = grouped.createCopy();
        auto groups = tree.getChildWithName("CLIP_GROUPS");
        if (bad == 0)
            tree.removeChild(groups, nullptr);
        if (bad == 1)
            groups.getChild(0).getChild(0).setProperty("clip", 999999, nullptr);
        if (bad == 2)
            groups.getChild(0).getChild(1).setProperty("clip", pattern, nullptr);
        if (bad == 3)
            groups.appendChild(groups.getChild(0).createCopy(), nullptr);
        if (bad == 4)
            tree.setProperty("version", 15, nullptr);
        require(Project::parse(tree, restored).failed() && restored.state().isEquivalentTo(unchanged),
                "Malformed/masquerading group data preserves destination");
    }
    AudioEngine engine(false);
    const auto baseline = folder.getChildFile("groups-before.wav"),
               after = folder.getChildFile("groups-after.wav");
    auto ungrouped = c.project;
    ungrouped.clipGroups.clear();
    require(engine.renderSync(ungrouped, baseline).wasOk() && engine.renderSync(c.project, after).wasOk() &&
                true,
            "Both group comparison WAVs render");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> aReader(formats.createReaderFor(baseline)),
        bReader(formats.createReaderFor(after));
    require(aReader && bReader && aReader->lengthInSamples == bReader->lengthInSamples,
            "Group WAV shapes match");
    juce::AudioBuffer<float> aPcm(2, int(aReader->lengthInSamples)), bPcm(2, int(bReader->lengthInSamples));
    require(aReader->read(&aPcm, 0, aPcm.getNumSamples(), 0, true, true) &&
                bReader->read(&bPcm, 0, bPcm.getNumSamples(), 0, true, true),
            "Actual group comparison WAV PCM");
    double maximumDifference = 0;
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < aPcm.getNumSamples(); ++frame)
            maximumDifference = std::max(maximumDifference, std::abs(double(aPcm.getSample(channel, frame)) -
                                                                     bPcm.getSample(channel, frame)));
    require(maximumDifference == 0, "Membership-only grouping leaves actual rendered PCM unchanged");
    const auto file = folder.getChildFile("clip-groups.vibra");
    require(saveProject(c.project, file).wasOk() && loadProject(file, restored).wasOk() &&
                restored.clipGroups.size() == 1 &&
                restored.clipGroups.front().members == c.project.clipGroups.front().members,
            "Real collected-media save/reopen preserves group membership");
    args = {};
    args.ids = {pattern};
    args.length = -.5;
    require(c.execute("clip.move", args).wasOk() && c.project.placements.front().length == 1.5 &&
                c.project.audioPlacements.front().duration == .75 &&
                c.project.automation.placements.front().length == 1.5,
            "Grouped common resize respects mixed source units");
    c.undoEdit();
    args.length = 100;
    require(c.execute("clip.move", args).wasOk() && c.project.state().isEquivalentTo(grouped),
            "Full-source Audio Clip clamps common group extension without changing siblings");
    args.length = 0;
    args.enabled = true;
    require(c.execute("clip.mute", args).wasOk() && c.project.placements.front().muted &&
                c.project.audioPlacements.front().muted && c.project.automation.placements.front().muted &&
                !c.project.placements.back().muted,
            "Grouped mute affects members only");
    c.undoEdit();
    Controller policy;
    Project sourceProject;
    require(Project::parse(initial, sourceProject).wasOk(), "Regroup source fixture");
    policy.replace(sourceProject);
    args = {};
    args.ids = {pattern, audio};
    require(policy.execute("clip.group", args).wasOk(), "Initial subgroup");
    args.ids = {audio, automation};
    require(policy.execute("clip.group", args).wasOk() && policy.project.clipGroups.size() == 1 &&
                policy.project.clipGroups.front().members.size() == 3,
            "Enabled regroup unions intersected memberships");
    const auto unionGroup = policy.project.state();
    policy.clipGroupingEnabled = false;
    require(policy.execute("clip.group", args).wasOk() && policy.project.clipGroups.size() == 1 &&
                policy.project.clipGroups.front().members.size() == 2,
            "Bypassed regroup moves explicit members and dissolves singleton");
    policy.undoEdit();
    require(policy.project.state().isEquivalentTo(unionGroup), "Regroup exact Undo");
    policy.replace(sourceProject);
    policy.clipGroupingEnabled = true;
    Customization customization;
    MacroContext context{policy.project.patterns.front().id,
                         policy.project.channels.front().id,
                         0,
                         {},
                         {pattern, audio, automation}};
    CommandArgs grouping, move;
    grouping.grouping = false;
    move.beat = 1;
    move.length = 0;
    Macro macro{"macro.group-move",
                "Group and move section",
                {{"clip.group", grouping, true}, {"clip.move", move, true}}};
    const auto beforeMacro = policy.project.state();
    require(customization.addMacro(macro).wasOk() &&
                customization.runMacro(macro.id, policy, context).wasOk() &&
                policy.project.clipGroups.size() == 1 && policy.project.placements.front().beat == 2,
            "Real multi-step user macro uses canonical group/move/context");
    policy.undoEdit();
    require(policy.project.state().isEquivalentTo(beforeMacro), "Group macro has one exact Undo");
    Customization imported;
    require(imported.importState(customization.state()).wasOk() &&
                imported.macros.front().steps.front().parameters.grouping == std::optional<bool>{false},
            "Macro grouping override round trips as typed false");
    folder.getChildFile("clip-groups-core.txt")
        .replaceWithText("PASS mixed group/rigid move/bypass/duplicate/delete/slice/Undo/schema16/real "
                         "WAV/save; reference runtime/listening separate.\n");
}
