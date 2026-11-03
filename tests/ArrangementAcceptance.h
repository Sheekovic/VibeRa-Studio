// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Arrangements.h"
#include "Commands.h"

// Multiple arrangements: add/clone/select/rename/delete swap the Playlist contents as
// single Undo steps, keep IDs unique, persist (version 48) and never leave a stored clip
// pointing at a deleted Pattern.
namespace arrangement_acceptance
{
using namespace vibera;

inline void tests(const juce::File &)
{
    Controller controller;
    auto project = Project::empty();
    const auto pattern = project.patterns[0].id;
    project.placements.push_back({project.allocate(), pattern, 0, 0, 4, 0});
    project.placements.push_back({project.allocate(), pattern, 1, 4, 4, 0});
    project.markers.push_back({project.allocate(), 0, "Intro"});
    controller.replace(project);
    const auto original = controller.project.placements;
    const auto empty = controller.project.state();
    CommandArgs add;
    add.text = "Remix";
    require(controller.execute("arrangement.add", add).wasOk(), "New arrangement");
    require(controller.project.placements.empty() && controller.project.markers.empty() &&
                controller.project.arrangementName == "Remix" && controller.project.arrangements.size() == 1 &&
                controller.project.arrangements[0].placements.size() == 2 &&
                controller.project.arrangements[0].name == "Arrangement 1",
            "A new arrangement starts empty and stores the first one");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(empty), "Adding is one exact Undo step");
    controller.redoEdit();
    CommandArgs place;
    place.pattern = pattern;
    place.beat = 8;
    place.length = 4;
    require(controller.execute("clip.add", place).wasOk(), "Edit the new arrangement");
    const auto first = controller.project.arrangements[0].id;
    CommandArgs select;
    select.target = first;
    require(controller.execute("arrangement.select", select).wasOk(), "Switch back");
    require(controller.project.placements.size() == 2 && controller.project.placements[0].id == original[0].id &&
                controller.project.markers.size() == 1 && controller.project.arrangements[0].name == "Remix" &&
                controller.project.arrangements[0].placements.size() == 1,
            "Switching restores the first arrangement and stores the remix with its clip");
    CommandArgs clone;
    require(controller.execute("arrangement.clone", clone).wasOk(), "Clone the current arrangement");
    require(controller.project.placements.size() == 2 && controller.project.placements[0].id != original[0].id &&
                controller.project.arrangementName == "Arrangement 1 copy" && controller.project.validate().wasOk(),
            "A clone copies the clips with fresh IDs");
    CommandArgs rename;
    rename.text = "Radio edit";
    require(controller.execute("arrangement.rename", rename).wasOk() &&
                controller.project.arrangementName == "Radio edit",
            "Rename the current arrangement");
    {
        Project reopened;
        const auto parsed = Project::parse(controller.project.state(), reopened);
        require(parsed.wasOk(), ("Reopen: " + parsed.getErrorMessage()).toStdString());
        require(reopened.arrangements.size() == 2 &&
                    reopened.arrangementName == "Radio edit" && reopened.arrangements[0].placements.size() == 1 &&
                    reopened.arrangements[1].placements.size() == 2,
                "Arrangements survive save and reopen");
        auto old = controller.project.state().createCopy();
        old.setProperty("version", 47, nullptr);
        require(Project::parse(old, reopened).failed(), "A version-47 file cannot carry arrangements");
    }
    // Deleting the Pattern removes its clips from every arrangement.
    CommandArgs remove;
    remove.target = 0;
    require(controller.execute("arrangement.delete", remove).wasOk() && controller.project.arrangements.size() == 1,
            "Deleting the current arrangement opens another one");
    remove.target = controller.project.arrangements[0].id;
    require(controller.execute("arrangement.delete", remove).wasOk() && controller.project.arrangements.empty(),
            "Deleting a stored arrangement");
    require(controller.execute("arrangement.delete", remove).failed(), "The last arrangement stays");
    {
        Controller pruning;
        auto withTwo = Project::empty();
        const auto keep = withTwo.patterns[0].id;
        const auto doomed = withTwo.allocate();
        withTwo.patterns.push_back({doomed, "Doomed", 4, {}});
        withTwo.placements.push_back({withTwo.allocate(), doomed, 0, 0, 4, 0});
        pruning.replace(withTwo);
        CommandArgs another;
        require(pruning.execute("arrangement.add", another).wasOk(), "Second arrangement");
        CommandArgs drop;
        drop.pattern = doomed;
        const auto dropped = pruning.execute("pattern.delete", drop);
        require(dropped.wasOk(), ("Pattern delete: " + dropped.getErrorMessage()).toStdString());
        require(pruning.project.arrangements[0].placements.empty() && pruning.project.validate().wasOk() &&
                    pruning.project.pattern(keep) != nullptr,
                "Deleting a Pattern removes its clips from stored arrangements");
    }
}
} // namespace arrangement_acceptance
