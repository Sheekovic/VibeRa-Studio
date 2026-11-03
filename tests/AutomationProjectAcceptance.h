// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "AutomationSourceCloneAcceptance.h"
#include "SamplerModulationAutomationModelAcceptance.h"
static void automationProjectTests(const juce::File &folder)
{
    automationSourceCloneTests(folder);
    samplerModulationAutomationModelTests(folder);
    Controller c;
    CommandArgs a;
    a.mixer = 17;
    a.beat = 2;
    a.length = 4;
    a.lane = 5;
    const auto before = c.project.state();
    require(c.execute("automation.create", a).wasOk(), "create real Mixer automation document");
    const auto source = c.project.automation.sources.front().id;
    const auto placement = c.project.automation.placements.front().id;
    const auto endpoint = c.project.automation.sources.front().points.back().id;
    require(c.project.endBeat() == 6 && c.project.automation.sources.front().links.front().key.owner == 17,
            "automation contributes song extent and preserves target identity");
    const auto created = c.project.state();
    c.undoEdit();
    require(before.isEquivalentTo(c.project.state()), "creation one exact Undo");
    c.redoEdit();
    require(created.isEquivalentTo(c.project.state()), "creation exact Redo");
    a.target = source;
    a.beat = 1.2345678901234567;
    a.value = .23;
    require(c.execute("automation.point.add", a).wasOk(), "insert normalized point");
    const auto point = c.project.automation.sources.front().points[1].id;
    const auto preGesture = c.project.state();
    c.beginGesture();
    a.target = point;
    for (int i = 0; i < 12; ++i)
    {
        a.beat = 1.5 + i / 100.;
        a.value = i / 20.;
        require(c.execute("automation.point.move", a).wasOk(), "automation gesture samples");
    }
    c.endGesture("Move automation point");
    const auto gesture = c.project.state();
    c.undoEdit();
    require(preGesture.isEquivalentTo(c.project.state()), "whole point drag one Undo");
    c.redoEdit();
    require(gesture.isEquivalentTo(c.project.state()), "whole point drag one Redo");
    c.beginGesture();
    a.beat = 1.9;
    require(c.execute("automation.point.move", a).wasOk(), "cancellable drag");
    c.cancelGesture();
    require(gesture.isEquivalentTo(c.project.state()), "cancel preserves exact source");
    a.target = placement;
    require(c.execute("automation.duplicate", a).wasOk(), "duplicate linked automation placement");
    const auto duplicate = c.project.automation.placements.back().id;
    require(c.project.automation.placements.back().source == source, "duplicates share source");
    a.target = duplicate;
    require(c.execute("automation.unique", a).wasOk(), "unique automation source");
    require(c.project.automation.sources.size() == 2 && c.project.automation.sources.back().links.empty(),
            "unique source has fresh IDs and independent target links");
    a.target = c.project.automation.sources.back().id;
    require(c.execute("automation.link", a).wasOk(), "explicitly relink unique source");
    a.beat = 1.8;
    a.length = .2;
    require(c.execute("automation.range", a).wasOk(), "inverted target range");
    a.target = endpoint;
    a.beat = 5;
    a.value = .9;
    require(c.execute("automation.point.move", a).wasOk() &&
                c.project.automation.placements.front().length == 5,
            "endpoint extends matching source window");
    a.target = point;
    a.text = "smooth";
    a.value = -.3;
    require(c.execute("automation.point.shape", a).wasOk(), "persist curve shape and tension");
    a.target = source;
    a.text = "Original fade";
    require(c.execute("automation.source.rename", a).wasOk(), "rename source");
    const auto file = folder.getChildFile("automation-roundtrip.vibra");
    require(saveProject(c.project, file).wasOk(), "automation file saved");
    Project restored;
    require(loadProject(file, restored).wasOk() && restored.state().isEquivalentTo(c.project.state()),
            "automation exact disk round trip including double precision and global IDs");
    const auto intact = restored.state();
    auto rejected = [&](juce::ValueTree state)
    {
        require(Project::parse(state, restored).failed() && intact.isEquivalentTo(restored.state()),
                "malformed automation file preserves destination");
    };
    auto malformed = intact.createCopy();
    malformed.removeChild(malformed.getChildWithName("AUTOMATION"), nullptr);
    rejected(malformed);
    malformed = intact.createCopy();
    malformed.getChildWithName("AUTOMATION")
        .getChildWithName("SOURCES")
        .getChild(0)
        .getChildWithName("LINKS")
        .getChild(0)
        .setProperty("owner", 900, nullptr);
    rejected(malformed);
    malformed = intact.createCopy();
    malformed.getChildWithName("AUTOMATION")
        .getChildWithName("SOURCES")
        .getChild(0)
        .getChildWithName("POINTS")
        .getChild(0)
        .setProperty("id", juce::int64(c.project.channels[0].id), nullptr);
    rejected(malformed);
    malformed = intact.createCopy();
    malformed.getChildWithName("AUTOMATION")
        .getChildWithName("PLACEMENTS")
        .getChild(0)
        .setProperty("muted", "false-ish", nullptr);
    rejected(malformed);
    malformed = intact.createCopy();
    malformed.setProperty("version", "8junk", nullptr);
    rejected(malformed);
    malformed = intact.createCopy();
    malformed.getChildWithName("AUTOMATION")
        .getChildWithName("SOURCES")
        .getChild(0)
        .getChildWithName("POINTS")
        .getChild(0)
        .setProperty("value", "nan", nullptr);
    rejected(malformed);
    malformed = intact.createCopy();
    malformed.appendChild(malformed.getChildWithName("AUTOMATION").createCopy(), nullptr);
    rejected(malformed);
    for (int v = 1; v <= 7; ++v)
    {
        auto legacy = Project::empty().state();
        legacy.setProperty("version", v, nullptr);
        stripSamplerVoiceFieldsForLegacy(legacy);
        legacy.removeChild(legacy.getChildWithName("AUTOMATION"), nullptr);
        for (auto channel : legacy.getChildWithName("CHANNELS"))
        {
            auto sampler = channel.getChildWithName("SAMPLER");
            if (v < 5)
                channel.removeChild(sampler, nullptr);
            else
            {
                if (v < 6)
                    for (const auto *field : {"sourceTempo", "fitTempo"})
                        sampler.removeProperty(field, nullptr);
                if (v < 7)
                    for (const auto *field : {"loop", "pingPong", "loopStart", "loopEnd"})
                        sampler.removeProperty(field, nullptr);
            }
        }
        require(Project::parse(legacy, restored).wasOk() && restored.version == Project::currentVersion &&
                    restored.automation.sources.empty(),
                "legacy schema migrates with no automation");
    }
    const auto current = c.project.state();
    a.target = source;
    a.beat = 0;
    a.value = .8;
    require(c.execute("automation.point.add", a).failed() && current.isEquivalentTo(c.project.state()),
            "duplicate boundary command rejected atomically");
    a.mixer = 500;
    require(c.execute("automation.link", a).failed() && current.isEquivalentTo(c.project.state()),
            "unavailable command target rejected atomically");
    a.target = source;
    require(c.execute("automation.source.delete", a).wasOk() && c.project.automation.sources.size() == 1 &&
                c.project.automation.placements.size() == 1 && !c.project.automation.initialValues.empty(),
            "source deletion removes linked placements and retains initialized controls");
    c.undoEdit();
    require(current.isEquivalentTo(c.project.state()), "source deletion exact Undo");

    Controller playlist;
    a = {};
    a.mixer = 0;
    a.beat = 2;
    a.length = 4;
    require(playlist.execute("automation.create", a).wasOk(), "Playlist automation fixture");
    playlist.project.automation.sources[0].points[0].value = .1f;
    playlist.project.automation.sources[0].points[1].value = .9f;
    AutomationProgram unsliced;
    std::string error;
    require(AutomationProgram::compile(playlist.project.automation, true, unsliced, error),
            "unsliced preparation");
    const auto leftId = playlist.project.automation.placements.front().id;
    a.ids = {leftId};
    a.beat = 4;
    require(playlist.execute("clip.slice", a).wasOk(), "common Playlist slice supports automation");
    AutomationProgram sliced;
    require(AutomationProgram::compile(playlist.project.automation, true, sliced, error),
            "sliced preparation");
    for (double beat : {1., 2., 3.9, 4., 4.1, 5.9, 6., 10.})
        require(std::abs(*sliced.valueAt({AutomationParameter::mixerGain, 0, 0}, beat) -
                         *unsliced.valueAt({AutomationParameter::mixerGain, 0, 0}, beat)) < 1.e-6f,
                "slice preserves actual evaluated curve before/across/after split");
    a.ids = {playlist.project.automation.placements.back().id};
    require(playlist.execute("clip.delete", a).wasOk(), "common Playlist delete supports automation");
    a.ids = {leftId};
    a.beat = 1;
    require(playlist.execute("clip.slip", a).wasOk() && playlist.project.automation.placements[0].offset == 1,
            "common Playlist slip changes source window");
    a.length = -1;
    a.lane = 2;
    require(playlist.execute("clip.move", a).wasOk() && playlist.project.automation.placements[0].beat == 3 &&
                playlist.project.automation.placements[0].length == 1 &&
                playlist.project.automation.placements[0].lane == 2,
            "common Playlist drag and resize supports automation");
    require(playlist.execute("clip.duplicate", a).wasOk() &&
                playlist.project.automation.placements.size() == 2 &&
                playlist.project.automation.placements[0].source ==
                    playlist.project.automation.placements[1].source,
            "common Playlist duplication remains linked");
    require(playlist.execute("clip.mute", a).wasOk() && playlist.project.automation.placements[0].muted,
            "common Playlist mute supports automation");
}
