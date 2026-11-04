// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"

// Playlist track settings and operations: rename/colour/mute (mute silences the track's
// clips without touching clip mute), insert/delete/move shifting clips and settings, each
// one exact Undo step, persisted in project version 50.
namespace playlist_track_acceptance
{
using namespace vibera;

inline void tests(const juce::File &)
{
    Controller controller;
    auto project = Project::empty();
    const auto pattern = project.patterns[0].id;
    Note note;
    note.id = project.allocate();
    note.channel = project.channels[0].id;
    note.beat = 0;
    note.length = 1;
    project.patterns[0].notes.push_back(note);
    const Id a = project.allocate(), b = project.allocate(), c = project.allocate();
    project.placements.push_back({a, pattern, 0, 0, 4, 0});
    project.placements.push_back({b, pattern, 1, 4, 4, 0});
    project.placements.push_back({c, pattern, 2, 8, 4, 0});
    controller.replace(project);
    auto lane = [&](Id id)
    {
        for (const auto &clip : controller.project.placements)
            if (clip.id == id)
                return clip.lane;
        return -1;
    };
    auto run = [&](const char *id, int target, auto &&fill)
    {
        CommandArgs args;
        args.lane = target;
        fill(args);
        return controller.execute(id, args);
    };
    auto audibleAt = [&](double beat)
    {
        for (const auto &scheduled : sequence(controller.project, false, 0))
            if (std::abs(scheduled.beat - beat) < 1.e-9)
                return true;
        return false;
    };

    const auto initial = controller.project.state();
    require(run("track.rename", 1, [](CommandArgs &x) { x.text = "Drums"; }).wasOk() &&
                controller.project.track(1) && controller.project.track(1)->name == "Drums",
            "Rename a track");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(initial), "Renaming is one exact Undo step");
    controller.redoEdit();
    require(run("track.rename", 1, [](CommandArgs &x) { x.text = "a\nb"; }).failed(), "Names are one line");
    require(run("track.colour", 1, [](CommandArgs &x) { x.value = double(0xff4fb3d9u); }).wasOk() &&
                controller.project.track(1)->colour == 0xff4fb3d9u,
            "Colour a track");
    require(run("track.colour", 1, [](CommandArgs &x) { x.value = double(0x804fb3d9u); }).failed(),
            "Track colours must be opaque");
    require(run("track.rename", 200, [](CommandArgs &x) { x.text = "x"; }).failed(), "Lanes are 0..127");

    require(audibleAt(4), "Track 2 plays before muting");
    require(run("track.mute", 1, [](CommandArgs &x) { x.enabled = true; }).wasOk() &&
                controller.project.laneMuted(1),
            "Mute a track");
    require(!audibleAt(4) && audibleAt(0) && audibleAt(8), "A muted track silences only its own clips");
    for (const auto &clip : controller.project.placements)
        require(!clip.muted, "Track mute leaves clip mute flags untouched");

    Project saved;
    require(Project::parse(controller.project.state(), saved).wasOk() && saved.track(1) &&
                saved.track(1)->name == "Drums" && saved.track(1)->colour == 0xff4fb3d9u && saved.track(1)->muted &&
                saved.version == Project::currentVersion,
            "Track settings survive save/load");
    auto old = controller.project.state();
    old.setProperty("version", 49, nullptr);
    require(Project::parse(old, saved).failed(), "Track settings require project version 50");

    // Solo: only that track plays; soloing it again unmutes every track. One Undo each.
    require(run("track.mute", 1, [](CommandArgs &x) { x.enabled = false; }).wasOk(), "Unmute before solo");
    const auto beforeSolo = controller.project.state();
    require(run("track.solo", 2, [](CommandArgs &) {}).wasOk(), "Solo track 3");
    require(!audibleAt(0) && !audibleAt(4) && audibleAt(8) && controller.project.laneMuted(0) &&
                controller.project.laneMuted(1) && !controller.project.laneMuted(2),
            "A soloed track plays alone");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(beforeSolo), "Solo is one exact Undo step");
    controller.redoEdit();
    require(run("track.solo", 2, [](CommandArgs &) {}).wasOk() && audibleAt(0) && audibleAt(4) && audibleAt(8),
            "Soloing the soloed track again unmutes every track");
    require(controller.project.track(1) && controller.project.track(1)->name == "Drums",
            "Solo keeps track names");
    require(run("track.mute", 1, [](CommandArgs &x) { x.enabled = true; }).wasOk(), "Mute again for the next checks");

    const auto beforeInsert = controller.project.state();
    require(run("track.insert", 1, [](CommandArgs &) {}).wasOk(), "Insert a track");
    require(lane(a) == 0 && lane(b) == 2 && lane(c) == 3 && controller.project.track(2) &&
                controller.project.track(2)->name == "Drums" && !controller.project.track(1),
            "Inserting moves this and later tracks down with their settings");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(beforeInsert), "Inserting is one exact Undo step");

    require(run("track.move", 1, [](CommandArgs &x) { x.value = 1; }).wasOk() && lane(b) == 2 && lane(c) == 1 &&
                controller.project.track(2) && controller.project.track(2)->name == "Drums",
            "Move a track down swaps clips and settings");
    require(run("track.move", 0, [](CommandArgs &x) { x.value = -1; }).failed(), "The first track cannot move up");
    controller.undoEdit();

    const auto beforeDelete = controller.project.state();
    require(run("track.delete", 1, [](CommandArgs &) {}).wasOk(), "Delete a track");
    require(lane(b) == -1 && lane(a) == 0 && lane(c) == 1 && !controller.project.track(1) &&
                controller.project.tracks.empty(),
            "Deleting removes its clips and settings and moves later tracks up");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(beforeDelete), "Deleting is one exact Undo step");

    // Groups: tracks join the group of the track above; collapsing hides them in the Playlist
    // (view only: their clips still play). Saved in project v51.
    const auto beforeGroup = controller.project.state();
    require(run("track.group", 0, [](CommandArgs &x) { x.enabled = true; }).failed(), "The first track cannot join a group");
    require(run("track.group", 1, [](CommandArgs &x) { x.enabled = true; }).wasOk() &&
                run("track.group", 2, [](CommandArgs &x) { x.enabled = true; }).wasOk(),
            "Group tracks 2 and 3 under track 1");
    require(run("track.collapse", 2, [](CommandArgs &x) { x.enabled = true; }).wasOk() &&
                controller.project.track(0) && controller.project.track(0)->collapsed,
            "Collapsing from any member collapses the group's first track");
    const auto shown = controller.project.visiblePlaylistLanes();
    require(shown.size() == 126 && shown[0] == 0 && shown[1] == 3, "A collapsed group hides its other tracks");
    require(audibleAt(8), "Collapsed tracks still play");
    Project grouped;
    require(Project::parse(controller.project.state(), grouped).wasOk() && grouped.track(2) &&
                grouped.track(2)->groupedAbove && grouped.track(0)->collapsed,
            "Groups survive save/load");
    auto older = controller.project.state();
    older.setProperty("version", 50, nullptr);
    require(Project::parse(older, grouped).failed(), "Track groups require project version 51");
    require(run("track.collapse", 5, [](CommandArgs &x) { x.enabled = true; }).failed(),
            "A track outside any group cannot collapse");
    require(run("track.collapse", 0, [](CommandArgs &x) { x.enabled = false; }).wasOk() &&
                controller.project.visiblePlaylistLanes().size() == 128,
            "Expanding shows every track again");
    controller.undoEdit();
    controller.undoEdit();
    controller.undoEdit();
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(beforeGroup), "Each group step is one exact Undo step");

    CommandArgs last;
    last.pattern = pattern;
    last.lane = 127;
    last.beat = 0;
    last.length = 4;
    require(controller.execute("clip.add", last).wasOk(), "Fill the last track");
    require(run("track.insert", 0, [](CommandArgs &) {}).failed(), "Inserting refuses when the last track is used");
    require(run("track.mute", 1, [](CommandArgs &x) { x.enabled = false; }).wasOk(), "Unmute");
    require(controller.project.tracks.size() == 1 && !controller.project.track(1)->muted,
            "Unmuting keeps the name and colour");

    // Locked tracks protect their clips from every clip edit; unlocking restores editing.
    require(run("track.lock", 127, [](CommandArgs &x) { x.enabled = true; }).wasOk() &&
                controller.project.track(127)->locked,
            "A track locks");
    Id lastClip = 0;
    for (const auto &clip : controller.project.placements)
        if (clip.lane == 127)
            lastClip = clip.id;
    CommandArgs move;
    move.ids = {lastClip};
    move.beat = 4;
    require(controller.execute("clip.move", move).failed(), "A clip on a locked track cannot move");
    CommandArgs into = last;
    into.beat = 8;
    require(controller.execute("clip.add", into).failed(), "No clip can be added to a locked track");
    CommandArgs remove;
    remove.ids = {lastClip};
    require(controller.execute("clip.delete", remove).failed(), "A clip on a locked track cannot be deleted");
    Project reloaded;
    require(Project::parse(controller.project.state(), reloaded).wasOk() && reloaded.track(127)->locked,
            "The lock survives save/load");
    require(run("track.lock", 127, [](CommandArgs &x) { x.enabled = false; }).wasOk() &&
                controller.execute("clip.move", move).wasOk(),
            "Unlocking allows editing again");

    // Instrument tracks: a track bound to a Channel makes Patterns for it.
    const auto channel = controller.project.channels[1].id;
    const auto channelName = controller.project.channels[1].name;
    require(run("track.instrumentClip", 20, [](CommandArgs &x) { x.beat = 0; }).failed(),
            "A free track has no instrument clip");
    require(run("track.instrument", 20, [](CommandArgs &x) { x.channel = 999999; }).failed(),
            "A missing Channel cannot own a track");
    require(run("track.instrument", 20, [&](CommandArgs &x) { x.channel = channel; }).wasOk() &&
                controller.project.track(20) && controller.project.track(20)->channel == channel,
            "A track becomes an instrument track");
    const auto patternsBefore = controller.project.patterns.size();
    const auto clipsBefore = controller.project.placements.size();
    const auto beforeClip = controller.project.state();
    require(run("track.instrumentClip", 20, [](CommandArgs &x) { x.beat = 8; }).wasOk(), "Draw on the instrument track");
    require(controller.project.patterns.size() == patternsBefore + 1 &&
                controller.project.placements.size() == clipsBefore + 1 &&
                controller.project.placements.back().lane == 20 && controller.project.placements.back().beat == 8 &&
                controller.project.placements.back().pattern == controller.project.patterns.back().id &&
                controller.project.patterns.back().name == channelName + " 1",
            "It makes a new Pattern named after the Channel and places it on the track");
    run("track.instrumentClip", 20, [](CommandArgs &x) { x.beat = 12; });
    require(controller.project.patterns.back().name == channelName + " 2", "Instrument Patterns are numbered");
    controller.undoEdit();
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(beforeClip), "Each instrument clip is one exact Undo step");
    Project bound;
    require(Project::parse(controller.project.state(), bound).wasOk() && bound.track(20) && bound.track(20)->channel == channel,
            "The instrument track survives save and reload");
    CommandArgs removeChannel;
    removeChannel.channel = channel;
    require(controller.execute("channel.delete", removeChannel).wasOk() && !controller.project.track(20),
            "Deleting the Channel turns the track back into a free track");
}
} // namespace playlist_track_acceptance
