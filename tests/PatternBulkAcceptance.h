// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PatternManagement.h"
#include "Commands.h"

// Bulk Pattern management, purge and Pattern-only render documents. Model/history only.
static void patternManagementModelTests(const juce::File &folder)
{
    using namespace vibera;
    namespace pm = pattern_management;
    auto project = Project::empty();
    const auto kick = project.channels[0].id;
    auto &first = project.patterns.front();
    first.name = "Beat";
    first.notes.push_back({project.allocate(), kick, 0, .25, 60, .8f, true});
    const auto beat = first.id;
    const auto firstNote = first.notes[0].id; // `first` dangles after the pushes below.
    project.patterns.push_back({project.allocate(), "Empty", 4, {}});
    const auto emptyId = project.patterns.back().id;
    project.patterns.push_back({project.allocate(), "Idea", 8, {}});
    project.patterns.back().notes.push_back({project.allocate(), kick, 1, .5, 62, .7f, false});
    const auto idea = project.patterns.back().id;
    project.patterns.push_back({project.allocate(), "Placed empty", 4, {}});
    const auto placedEmpty = project.patterns.back().id;
    const auto clipA = project.allocate(), clipB = project.allocate(), clipC = project.allocate();
    project.placements = {{clipA, beat, 0, 0, 4, 0}, {clipB, beat, 0, 4, 4, 0}, {clipC, placedEmpty, 1, 0, 4, 0}};
    project.clipGroups = {{project.allocate(), {clipA, clipC}}};
    require(project.validate().wasOk(), "Pattern management fixture valid");

    // Bulk clone: independent copies inserted after each source, fresh note IDs.
    {
        auto p = project;
        std::vector<Id> created;
        require(pm::clone(p, {beat, idea}, &created).wasOk() && created.size() == 2, "Bulk clone");
        require(p.patterns.size() == 6 && p.patterns[1].id == created[0] && p.patterns[4].id == created[1] &&
                    p.patterns[1].name == "Beat copy" && p.patterns[1].notes.size() == 1 &&
                    p.patterns[1].notes[0].id != firstNote && p.validate().wasOk(),
                "Clones follow sources with independent IDs");
        require(pm::clone(p, {beat, beat}).failed() && pm::clone(p, {999999}).failed() && pm::clone(p, {}).failed(),
                "Duplicate/unknown/empty clone selections refuse");
    }
    // Bulk delete removes placements and repairs clip groups.
    {
        auto p = project;
        require(pm::remove(p, {beat, emptyId}).wasOk() && p.patterns.size() == 2 && p.placements.size() == 1 &&
                    p.clipGroups.empty() && p.validate().wasOk(),
                "Bulk delete removes placements and dissolves undersized clip groups");
        auto all = project;
        require(pm::remove(all, {beat, emptyId, idea, placedEmpty}).wasOk() && all.patterns.size() == 1 &&
                    all.placements.empty() && all.validate().wasOk(),
                "Deleting every Pattern keeps one fresh Pattern");
    }
    // Purge modes.
    {
        auto p = project;
        std::vector<Id> removed;
        require(pm::purge(p, "empty", &removed).wasOk() && removed == std::vector<Id>{emptyId} &&
                    p.pattern(idea) && p.pattern(placedEmpty) && p.validate().wasOk(),
                "Delete empty keeps scored and placed Patterns");
        require(pm::purge(p, "empty").failed(), "Second empty purge reports nothing to do");
        auto q = project;
        removed.clear();
        require(pm::purge(q, "unplaced", &removed).wasOk() && removed.size() == 2 && !q.pattern(idea) &&
                    !q.pattern(emptyId) && q.pattern(beat) && q.pattern(placedEmpty),
                "Delete unplaced removes every Pattern without Playlist use");
        require(pm::purge(q, "bogus").failed(), "Unknown purge mode refuses");
    }
    // Pattern render document.
    {
        Project out;
        require(pm::renderProject(project, idea, out).wasOk() && out.placements.size() == 1 &&
                    out.placements[0].pattern == idea && out.placements[0].beat == 0 &&
                    out.placements[0].length == 8 && out.audioPlacements.empty() && out.clipGroups.empty() &&
                    out.validate().wasOk() && std::abs(out.endBeat() - 8) < 1e-9,
                "Pattern render plays exactly the Pattern once");
        require(pm::renderProject(project, 999999, out).failed(), "Missing render Pattern refuses");
    }
    // Canonical commands share one Undo step each.
    {
        Controller controller;
        controller.replace(project);
        const auto baseline = controller.project.state();
        CommandArgs args;
        args.ids = {beat, idea};
        require(controller.execute("pattern.cloneSelected", args).wasOk() &&
                    controller.project.patterns.size() == 6,
                "pattern.cloneSelected clones the selection");
        require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline),
                "Bulk clone is one Undo step");
        require(controller.execute("pattern.deleteSelected", args).wasOk() && !controller.project.pattern(beat) &&
                    !controller.project.pattern(idea),
                "pattern.deleteSelected deletes the selection");
        require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(baseline),
                "Bulk delete is one Undo step");
        require(controller.execute("pattern.deleteEmpty", {}).wasOk() && !controller.project.pattern(emptyId),
                "pattern.deleteEmpty");
        require(controller.undoEditChecked().wasOk() && controller.execute("pattern.deleteUnplaced", {}).wasOk() &&
                    !controller.project.pattern(idea) && controller.project.pattern(beat),
                "pattern.deleteUnplaced");
        const auto revision = controller.revision;
        require(controller.execute("pattern.deleteUnplaced", {}).failed() && controller.revision == revision,
                "Nothing-to-delete refuses without history");
        // Legacy single-Pattern semantics are unchanged.
        CommandArgs single;
        single.pattern = beat;
        require(controller.execute("pattern.clone", single).wasOk(), "Single clone unchanged");
    }
    folder.getChildFile("pattern-management-verification.txt")
        .replaceWithText("Bulk clone/delete, purge empty/unplaced, Pattern render document and history passed.\n");
}
