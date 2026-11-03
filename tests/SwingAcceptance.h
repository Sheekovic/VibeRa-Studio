// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "Swing.h"
#include "SwingCommands.h"
static void swingTests(const juce::File &folder)
{
    Controller c;
    const auto channel = c.project.channels[0].id, other = c.project.channels[1].id;
    const auto pattern = c.project.patterns[0].id;
    for (int i = 0; i < 4; ++i)
        c.project.patterns[0].notes.push_back({c.project.allocate(), channel, i * .25, .25, 60, .8f, i < 2});
    c.project.patterns[0].notes.push_back({c.project.allocate(), other, .25, .25, 72, .6f, false});
    c.project.channels[1].swingMix = 0;
    c.project.placements = {{c.project.allocate(), pattern, 0, 0, 4, 0},
                            {c.project.allocate(), pattern, 1, 4, 4, 0}};
    const auto initial = c.project.state();
    const auto score = initial.getChildWithName("PATTERNS");
    CommandArgs args;
    args.value = 1;
    require(c.execute("project.swing", args).wasOk(), "Canonical global Swing");
    auto played = sequence(c.project, true, pattern);
    const std::vector<double> expected{0, .25, .375, .5, .875};
    require(played.size() == expected.size(), "Swing retains all score events");
    for (size_t i = 0; i < expected.size(); ++i)
        require(played[i].beat == expected[i],
                "Even sixteenths fixed, odd delayed half-step, unswung Channel exact");
    require(c.project.state().getChildWithName("PATTERNS").isEquivalentTo(score),
            "Swing never rewrites source steps/melody/IDs/lengths");
    const auto swung = c.project.state();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(initial), "Global Swing one exact Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(swung), "Global Swing exact Redo");
    args.channel = channel;
    args.value = .5;
    require(c.execute("channel.swingMix", args).wasOk(), "Independent Channel multiplier");
    played = sequence(c.project, false, pattern);
    auto found = [&](Id note, Id placement) -> ScheduledNote
    {
        const auto all = sequence(c.project, false, pattern);
        const auto n = std::find_if(all.begin(), all.end(), [&](const auto &n)
                                    { return n.note == note && n.placement == placement; });
        require(n != all.end(), "Stable scheduled target exists");
        return *n;
    };
    const auto second = c.project.patterns[0].notes[1].id;
    auto a = found(second, c.project.placements[0].id), b = found(second, c.project.placements[1].id);
    require(a.beat == .3125 && b.beat == 4.3125 && a.length == .25,
            "Linked Song placements share timing, default duration preserved");
    args.enabled = true;
    require(c.execute("channel.truncateSwing", args).wasOk(), "Truncation canonical option");
    a = found(second, c.project.placements[0].id);
    require(a.beat == .3125 && a.length == .1875 && a.beat + a.length == .5,
            "Truncation preserves original note end without changing source");
    require(c.project.state().getChildWithName("PATTERNS").isEquivalentTo(score),
            "Truncate is non-destructive");
    args.ids = {channel, other};
    args.value = .75;
    const auto beforeBulk = c.project.state();
    require(c.execute("rack.swingMix", args).wasOk() && c.project.channels[0].swingMix == .75 &&
                c.project.channels[1].swingMix == .75,
            "Bulk Swing Mix includes stable selected targets");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(beforeBulk), "Bulk is one exact Undo");
    args.enabled = false;
    require(c.execute("rack.truncateSwing", args).wasOk() && !c.project.channels[0].truncateSwing,
            "Bulk truncation can turn off");
    const auto beforeInvalid = c.project.state();
    for (auto value : {-1., 1.001, std::numeric_limits<double>::quiet_NaN()})
    {
        args.value = value;
        require(c.execute("project.swing", args).failed() && c.project.state().isEquivalentTo(beforeInvalid),
                "Invalid Swing is atomic");
    }
    args.value = .5;
    for (auto ids : {std::vector<Id>{}, std::vector<Id>{channel, channel}, std::vector<Id>{channel, 999999}})
    {
        args.ids = ids;
        require(c.execute("rack.swingMix", args).failed() && c.project.state().isEquivalentTo(beforeInvalid),
                "Empty/duplicate/stale Swing targets are atomic");
    }
    Project restored;
    const auto path = folder.getChildFile("swing-roundtrip.vibra");
    require(saveProject(c.project, path).wasOk() && loadProject(path, restored).wasOk() &&
                restored.state().isEquivalentTo(c.project.state()),
            "Schema13 Swing exact project round trip");
    auto legacy = initial.createCopy();
    legacy.setProperty("version", 12, nullptr);
    stripSamplerVoiceFieldsForLegacy(legacy);
    legacy.removeProperty("swing", nullptr);
    for (auto channelTree : legacy.getChildWithName("CHANNELS"))
    {
        channelTree.removeProperty("swingMix", nullptr);
        channelTree.removeProperty("truncateSwing", nullptr);
    }
    require(Project::parse(legacy, restored).wasOk() && restored.swing == 0 &&
                restored.channels[0].swingMix == 1 && !restored.channels[0].truncateSwing,
            "Old projects migrate to audibly unchanged Swing defaults");
    auto malformed = c.project.state();
    malformed.removeProperty("swing", nullptr);
    const auto retained = restored.state();
    require(Project::parse(malformed, restored).failed() && restored.state().isEquivalentTo(retained),
            "Incomplete new schema refuses without modifying destination");
    Channel channelSettings;
    require(swingDelay(.125, 1, channelSettings) == .0625 && swingDelay(.375, 1, channelSettings) == .0625 &&
                swingDelay(.5, 1, channelSettings) == 0,
            "Explicit independent off-grid timing policy is continuous across pair boundaries");
    double previous = -1;
    for (int i = 0; i <= 1000; ++i)
    {
        const auto beat = i / 1000.;
        const auto current = beat + swingDelay(beat, 1, channelSettings);
        require(current > previous, "Off-grid Swing retains strict onset order at maximum amount");
        previous = current;
    }
    auto window = c.project;
    window.swing = 1;
    window.channels[0].swingMix = 1;
    window.placements = {{window.allocate(), pattern, 0, 2, .25, .25}};
    const auto cropped = sequence(window, false, pattern);
    require(cropped.size() == 2 && cropped[0].beat == 2 && cropped[1].beat == 2.125 &&
                cropped[1].length == .125,
            "Swing uses source-Pattern phase before cropped Playlist offsets");
    auto timeline = compileLiveDocument(c.project, false, pattern, 42);
    require(timeline && timeline->find(channel) && timeline->generation == 42,
            "Swung sequence compiles through the existing immutable live timeline");
    Customization profile = Customization::familiarDefaults();
    Macro macro{"swing.current", "Swing current", {{"channel.swingMix", args, true}}};
    macro.steps[0].parameters.value = .25;
    profile.macros.push_back(macro);
    MacroContext context{pattern, channel, 0, {}, {}, {channel, other}};
    require(profile.runMacro(macro.id, c, context).wasOk() && c.project.channel(channel)->swingMix == .25,
            "Parameterized Swing macro uses canonical current instrument");
}
