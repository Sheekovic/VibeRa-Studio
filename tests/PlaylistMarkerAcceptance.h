// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"

// Playlist markers: canonical add/rename/move/delete with exact Undo, persistence
// (project version 44) and refusals.
namespace playlist_marker_acceptance
{
using namespace vibera;

inline void tests(const juce::File &)
{
    Controller controller;
    controller.replace(Project::empty());
    const auto empty = controller.project.state();
    CommandArgs add;
    add.beat = 16;
    require(controller.execute("marker.add", add).wasOk(), "Marker added at beat 16");
    require(controller.project.markers.size() == 1 && controller.project.markers[0].name == "Marker 1" &&
                controller.project.markers[0].beat == 16,
            "An unnamed marker is numbered");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(empty), "Adding a marker is one exact Undo step");
    controller.redoEdit();
    const Id id = controller.project.markers[0].id;
    add.beat = 4;
    add.text = "Verse";
    require(controller.execute("marker.add", add).wasOk(), "Named marker added");
    CommandArgs edit;
    edit.target = id;
    edit.text = "Chorus";
    require(controller.execute("marker.rename", edit).wasOk() && controller.project.markers[0].name == "Chorus",
            "Marker renamed");
    edit.beat = 32;
    require(controller.execute("marker.move", edit).wasOk() && controller.project.markers[0].beat == 32,
            "Marker moved");
    {
        Project reopened;
        require(Project::parse(controller.project.state(), reopened).wasOk() && reopened.markers.size() == 2 &&
                    reopened.markers[0].name == "Chorus" && reopened.markers[1].beat == 4,
                "Markers survive save and reopen");
        auto old = controller.project.state().createCopy();
        old.setProperty("version", 43, nullptr);
        require(Project::parse(old, reopened).failed(), "A version-43 file cannot carry markers");
    }
    edit.beat = -1;
    require(controller.execute("marker.move", edit).failed(), "Negative marker beat refused");
    edit.text = juce::String::repeatedString("x", 129);
    require(controller.execute("marker.rename", edit).failed(), "Names over 128 characters refused");
    edit.target = 999999;
    require(controller.execute("marker.delete", edit).failed(), "Deleting a missing marker refused");
    edit.target = id;
    require(controller.execute("marker.delete", edit).wasOk() && controller.project.markers.size() == 1,
            "Marker deleted");
    controller.undoEdit();
    require(controller.project.markers.size() == 2, "Undo restores a deleted marker");
}
} // namespace playlist_marker_acceptance
