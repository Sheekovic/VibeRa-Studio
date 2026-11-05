// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Project.h"
#include "../src/PatternChannelLoops.h"
#include "../src/PatternAutomation.h"
#include "../src/Commands.h"
#include "../src/PatternManagement.h"

// A Pattern clip longer than its Pattern repeats the Pattern (familiar Playlist behavior).
namespace clip_repeat_acceptance
{
using namespace vibera;
inline void tests()
{
    auto p = Project::empty();
    auto &part = p.patterns.front();
    part.notes.clear();
    part.length = 4;
    part.autoLength = false;
    const Id channel = p.channels.front().id;
    Note note;
    note.id = p.allocate();
    note.channel = channel;
    note.beat = 0;
    note.length = .5;
    note.pitch = 60;
    note.velocity = .8;
    part.notes.push_back(note);
    Note late = note;
    late.id = p.allocate();
    late.beat = 3.5;
    late.length = 1; // Crosses the Pattern end: cut there on every repeat.
    part.notes.push_back(late);
    p.placements.clear();
    const Id clip = p.allocate();
    p.placements.push_back({clip, part.id, 0, 8, 12, 0});
    require(p.validate().wasOk(), "A clip longer than its Pattern is valid");

    auto events = sequence(p, false, 0);
    std::vector<double> starts;
    std::set<std::uint32_t> ordinals;
    for (const auto &e : events)
        if (e.note == note.id)
        {
            starts.push_back(e.beat);
            ordinals.insert(e.repeatOrdinal);
        }
    require(starts == std::vector<double>{8, 12, 16}, "A 12-beat clip plays a 4-beat Pattern three times");
    require(ordinals.size() == 3, "Each repeat is a distinct note occurrence");
    for (const auto &e : events)
        if (e.note == late.id)
            require(std::abs(e.length - .5) < 1e-9, "A note crossing the Pattern end is cut at each repeat");

    // An offset clip starts inside the Pattern, then repeats from its start.
    p.placements.front() = {clip, part.id, 0, 8, 6, 2};
    events = sequence(p, false, 0);
    starts.clear();
    for (const auto &e : events)
        if (e.note == note.id)
            starts.push_back(e.beat);
    require(starts == std::vector<double>{10}, "Offset 2 over 6 beats repeats once from the Pattern start");

    // A clip inside its Pattern behaves exactly as before.
    p.placements.front() = {clip, part.id, 0, 8, 3, 0};
    events = sequence(p, false, 0);
    int inside = 0;
    for (const auto &e : events)
        inside += e.note == note.id ? 1 : 0;
    require(inside == 1, "A clip shorter than its Pattern plays its window once");

    // The repeat helper: segment positions, lengths and offsets.
    std::vector<std::tuple<double, double, double>> segments;
    forEachClipRepeat(10, 1, 4, [&](double position, double length, double offset, std::uint32_t)
                      { segments.emplace_back(position, length, offset); });
    require(segments == std::vector<std::tuple<double, double, double>>{{0, 3, 1}, {3, 4, 0}, {7, 3, 0}},
            "Clip repeat segments follow the Pattern window");

    // Pattern automation repeats with the Pattern.
    AutomationSource source;
    source.id = p.allocate();
    source.name = "Gain";
    source.length = 4;
    AutomationPoint first, second;
    first.id = p.allocate();
    first.value = .2f;
    second.id = p.allocate();
    second.beat = 4;
    second.value = .8f;
    source.points = {first, second};
    p.automation.sources.push_back(source);
    part.automation.push_back({0, source.id, 0, 4, 0, 0, false});
    p.placements.front() = {clip, part.id, 0, 8, 12, 0};
    const auto audible = audibleAutomation(p, false, 0);
    int windows = 0;
    for (const auto &placed : audible.document.placements)
        windows += placed.source == source.id ? 1 : 0;
    require(windows == 3, "Pattern automation repeats with a longer clip");

    // Split by channel and merge, through the canonical commands (one Undo each).
    {
        Controller controller;
        auto q = Project::empty();
        auto &pattern = q.patterns.front();
        pattern.notes.clear();
        pattern.length = 4;
        pattern.autoLength = false;
        const Id kick = q.channels[0].id, snare = q.channels[1].id;
        for (const auto &[channelId, beat] : std::vector<std::pair<Id, double>>{{kick, 0}, {snare, 1}, {kick, 2}})
        {
            Note n;
            n.id = q.allocate();
            n.channel = channelId;
            n.beat = beat;
            n.length = .25;
            n.pitch = 60;
            n.velocity = .8;
            pattern.notes.push_back(n);
        }
        q.placements.clear();
        const Id first = q.allocate(), second = q.allocate();
        q.placements.push_back({first, pattern.id, 2, 0, 4, 0});
        q.placements.push_back({second, pattern.id, 2, 8, 8, 0}); // Plays the Pattern twice.
        controller.replace(q);
        const auto patternsBefore = controller.project.patterns.size();
        CommandArgs split;
        split.target = first;
        require(controller.execute("clip.splitChannels", split).wasOk(), "Split a Pattern clip by channel");
        const auto &after = controller.project;
        std::vector<int> lanes;
        for (const auto &clip : after.placements)
            if (clip.id != second)
                lanes.push_back(clip.lane);
        std::sort(lanes.begin(), lanes.end());
        require(after.patterns.size() == patternsBefore + 2 && lanes == std::vector<int>{2, 3},
                "Two Channels give two clips on the clip's track and the one below");
        require(after.pattern(pattern.id) && after.pattern(pattern.id)->notes.size() == 3,
                "The source Pattern is unchanged for its other clips");
        controller.undoEdit();
        require(controller.project.placements.size() == 2, "Split is one Undo step");

        CommandArgs merge;
        merge.ids = {first, second};
        require(controller.execute("clip.merge", merge).wasOk(), "Merge two Pattern clips");
        const auto &merged = controller.project;
        require(merged.placements.size() == 1 && merged.placements.front().beat == 0 &&
                    std::abs(merged.placements.front().length - 16) < 1e-9,
                "Merged clips become one clip spanning them");
        const auto *one = merged.pattern(merged.placements.front().pattern);
        require(one != nullptr, "The merged Pattern exists");
        std::vector<double> kicks;
        for (const auto &n : one->notes)
            if (n.channel == kick)
                kicks.push_back(n.beat);
        std::sort(kicks.begin(), kicks.end());
        require(kicks == std::vector<double>{0, 2, 8, 10, 12, 14},
                "Merged notes keep their places, including the repeated clip");
        controller.undoEdit();
        require(controller.project.placements.size() == 2, "Merge is one Undo step");

        // Playlist editing: paste, insert/delete space, chop and quick quantize.
        auto clipAt = [&](double beat) -> const Placement *
        {
            for (const auto &c : controller.project.placements)
                if (std::abs(c.beat - beat) < 1e-9)
                    return &c;
            return nullptr;
        };
        auto withMarker = controller.project;
        withMarker.markers.push_back({withMarker.allocate(), 10, "Drop"});
        controller.replace(withMarker);
        CommandArgs paste;
        paste.ids = {first};
        paste.beat = 16;
        require(controller.execute("clip.paste", paste).wasOk(), "Paste clips by id");
        require(controller.project.placements.size() == 3 && clipAt(16) && clipAt(16)->length == 4 &&
                    clipAt(16)->lane == 2 && clipAt(16)->id != first,
                "A pasted clip is a new clip at the paste beat on the same track");
        controller.undoEdit();

        CommandArgs insert;
        insert.beat = 4;
        insert.length = 2;
        insert.enabled = false;
        require(controller.execute("time.insert", insert).wasOk(), "Insert space");
        require(clipAt(0) && clipAt(10) && controller.project.markers.front().beat == 12,
                "Insert space moves later clips and markers by the inserted length");
        controller.undoEdit();

        CommandArgs remove;
        remove.beat = 9;
        remove.length = 2;
        require(controller.execute("time.delete", remove).wasOk(), "Delete space");
        const auto *left = clipAt(8), *right = clipAt(9);
        require(controller.project.placements.size() == 3 && left && right && std::abs(left->length - 1) < 1e-9 &&
                    std::abs(right->length - 5) < 1e-9 && std::abs(right->offset - 3) < 1e-9,
                "Delete space cuts the crossing clip, removes the inside and closes the gap");
        require(controller.project.markers.empty(), "A marker inside the deleted space is removed");
        controller.undoEdit();
        require(controller.project.placements.size() == 2 && controller.project.markers.size() == 1,
                "Delete space is one Undo step");

        CommandArgs chop;
        chop.ids = {first};
        chop.length = 1;
        require(controller.execute("clip.chop", chop).wasOk(), "Chop a clip into beats");
        std::vector<double> offsets;
        for (const auto &c : controller.project.placements)
            if (c.beat < 4)
                offsets.push_back(c.offset);
        std::sort(offsets.begin(), offsets.end());
        require(offsets == std::vector<double>{0, 1, 2, 3}, "A 4-beat clip chops into four 1-beat pieces");
        controller.undoEdit();

        CommandArgs quantize;
        quantize.ids = {second};
        quantize.length = 3;
        require(controller.execute("clip.quantize", quantize).wasOk(), "Quick quantize clip starts");
        require(clipAt(9) && clipAt(9)->id == second && clipAt(9)->length == 8,
                "Quick quantize snaps the start and keeps the length");
        controller.undoEdit();

        CommandArgs trim;
        trim.target = second;
        trim.beat = 10;
        require(controller.execute("clip.trimStart", trim).wasOk(), "Trim a Pattern clip start");
        require(clipAt(10) && std::abs(clipAt(10)->offset - 2) < 1e-9 && std::abs(clipAt(10)->length - 6) < 1e-9,
                "Trimming the start moves the window and keeps the end");
        trim.beat = 8;
        require(controller.execute("clip.trimStart", trim).wasOk() && clipAt(8) && clipAt(8)->offset == 0,
                "Dragging the start back restores the window");
        controller.undoEdit();
        controller.undoEdit();
        require(clipAt(8) && clipAt(8)->id == second && clipAt(8)->length == 8, "Trim start is one Undo step per edit");
        trim.beat = 7;
        require(controller.execute("clip.trimStart", trim).failed(),
                "A Pattern clip cannot start before its Pattern start");
        {
            // Render selected clips: only those clips, moved to start at 0, unmuted.
            auto song = controller.project;
            song.tempoChanges.push_back({song.allocate(), 4, 90, false});
            Project only;
            double start = 0;
            require(pattern_management::renderClipsProject(song, {second}, only, start).wasOk() && start == 8,
                    "Render clips starts at the earliest selected clip");
            require(only.placements.size() == 1 && only.placements.front().beat == 0 && only.markers.empty(),
                    "Only the selected clip is rendered, from beat 0");
            require(std::abs(only.tempo - 90) < 1e-9 && only.tempoChanges.empty(),
                    "The tempo in force at the clip start becomes the render tempo");
            require(pattern_management::renderClipsProject(song, {}, only, start).failed(),
                    "Rendering nothing fails");
        }
        {
            // Track clone / auto name / reset.
            CommandArgs clone;
            clone.lane = 2;
            require(controller.execute("track.clone", clone).wasOk(), "Clone a track");
            int onClone = 0;
            for (const auto &c : controller.project.placements)
                onClone += c.lane == 3 && c.id != first && c.id != second;
            require(onClone == 2, "The cloned track holds new copies of both clips, right below");
            controller.undoEdit();
            require(controller.project.placements.size() == 2, "Clone is one Undo step");
            CommandArgs name;
            name.lane = 2;
            require(controller.execute("track.autoName", name).wasOk() && controller.project.track(2) &&
                        controller.project.track(2)->name == controller.project.pattern(pattern.id)->name,
                    "Auto name uses the first Pattern clip's name");
            require(controller.execute("track.reset", name).wasOk() && !controller.project.track(2),
                    "Reset clears the name and colour (default track)");
            name.lane = 40;
            require(controller.execute("track.autoName", name).failed(), "An empty track has nothing to name it after");
        }
        {
            // Piano roll: paste notes onto another Channel at a beat, flip in time and pitch.
            const auto *source = controller.project.pattern(pattern.id);
            std::vector<Id> kicks;
            for (const auto &n : source->notes)
                if (n.channel == kick)
                    kicks.push_back(n.id);
            CommandArgs paste;
            paste.pattern = pattern.id;
            paste.channel = snare;
            paste.ids = kicks;
            paste.beat = 8;
            require(controller.execute("notes.paste", paste).wasOk(), "Paste notes");
            std::vector<double> pasted;
            for (const auto &n : controller.project.pattern(pattern.id)->notes)
                if (n.channel == snare && n.beat >= 8)
                    pasted.push_back(n.beat);
            std::sort(pasted.begin(), pasted.end());
            require(pasted == std::vector<double>{8, 10}, "Pasted notes keep spacing from the paste beat, on the target Channel");
            controller.undoEdit();
            CommandArgs flip;
            flip.pattern = pattern.id;
            flip.channel = kick;
            flip.text = "horizontal";
            require(controller.execute("notes.flip", flip).wasOk(), "Flip the Channel part in time");
            std::vector<double> flipped;
            for (const auto &n : controller.project.pattern(pattern.id)->notes)
                if (n.channel == kick)
                    flipped.push_back(n.beat);
            std::sort(flipped.begin(), flipped.end());
            require(flipped == std::vector<double>{0, 2}, "Kicks at 0 and 2 (length 1/4) mirror within 0..2.25");
            controller.undoEdit();
            auto kickBeats = [&]
            {
                std::vector<double> beats;
                for (const auto &n : controller.project.pattern(pattern.id)->notes)
                    if (n.channel == kick)
                        beats.push_back(n.beat);
                std::sort(beats.begin(), beats.end());
                return beats;
            };
            CommandArgs tool;
            tool.pattern = pattern.id;
            tool.channel = kick;
            tool.beat = -.25;
            require(controller.execute("notes.rotate", tool).wasOk() && kickBeats() == std::vector<double>{1.75, 3.75},
                    "Rotating left by a step wraps the note at 0 to the bar end");
            controller.undoEdit();
            tool.length = 1;
            require(controller.execute("notes.discardLengths", tool).wasOk(), "Discard lengths");
            for (const auto &n : controller.project.pattern(pattern.id)->notes)
                if (n.channel == kick)
                    require(n.length == 1, "Discard lengths sets every note to the given length");
            controller.undoEdit();
            tool.pitch = 72;
            tool.value = 95;
            require(controller.execute("notes.limit", tool).wasOk(), "Limit to a range");
            for (const auto &n : controller.project.pattern(pattern.id)->notes)
                if (n.channel == kick)
                    require(n.pitch == 72, "Limit folds C5 up an octave into C6..B7");
            controller.undoEdit();
            tool.length = 1. / 32;
            tool.value = .5;
            require(controller.execute("notes.flam", tool).wasOk() && kickBeats().size() == 4,
                    "Flam adds a grace note before each note");
            controller.undoEdit();
            CommandArgs space;
            space.pattern = pattern.id;
            space.channel = kick;
            space.beat = 1;
            space.length = 2;
            space.enabled = false;
            require(controller.execute("notes.insertSpace", space).wasOk() && kickBeats() == std::vector<double>{0, 4},
                    "Insert space moves the Channel's later notes by the space");
            controller.undoEdit();
            require(controller.execute("notes.deleteSpace", space).wasOk() && kickBeats() == std::vector<double>{0},
                    "Delete space removes the note inside the range");
            controller.undoEdit();
            CommandArgs randomize;
            randomize.pattern = pattern.id;
            randomize.channel = snare;
            randomize.text = "major";
            randomize.pitch = 60;
            randomize.length = .25;
            randomize.value = .7;
            randomize.seed = 42;
            auto snareNotes = [&]
            {
                std::vector<std::pair<double, int>> list;
                for (const auto &n : controller.project.pattern(pattern.id)->notes)
                    if (n.channel == snare)
                        list.push_back({n.beat, n.pitch});
                std::sort(list.begin(), list.end());
                return list;
            };
            require(controller.execute("notes.randomize", randomize).wasOk(), "Randomize the snare part");
            const auto first = snareNotes();
            bool inScale = !first.empty();
            for (const auto &[beat, pitchValue] : first)
                inScale = inScale && std::set<int>{0, 2, 4, 5, 7, 9, 11}.contains(pitchValue % 12) && pitchValue >= 60 &&
                          pitchValue <= 72;
            require(inScale, "Random notes stay in C major from C5 to C6");
            controller.undoEdit();
            require(controller.execute("notes.randomize", randomize).wasOk() && snareNotes() == first,
                    "The same seed gives the same notes");
            controller.undoEdit();
            randomize.text = "nonsense";
            require(controller.execute("notes.randomize", randomize).failed(), "An unknown scale is refused");
            flip.text = "sideways";
            require(controller.execute("notes.flip", flip).failed(), "Unknown flip direction fails");
        }
        {
            // Mixer integrated EQ: DSP gain, command + Undo, save/load.
            MixerEqSettings boost;
            boost.gain[0] = 12;
            MixerEqProcessor eq;
            eq.prepare(48000, boost);
            std::vector<float> wave(48000);
            for (size_t i = 0; i < wave.size(); ++i)
                wave[i] = .1f * float(std::sin(2 * 3.14159265358979 * 20 * double(i) / 48000));
            float *channels[]{wave.data()};
            eq.process(channels, 1, int(wave.size()));
            float peak = 0;
            for (size_t i = 24000; i < wave.size(); ++i)
                peak = std::max(peak, std::abs(wave[i]));
            require(peak > .35f && peak < .45f, "A +12 dB low shelf raises 20 Hz about four times");
            require(std::abs(MixerEqProcessor::responseDb(boost, 10000) ) < .5f, "The low shelf leaves 10 kHz flat");
            CommandArgs band;
            band.mixer = 0;
            band.target = 1;
            band.value = -6;
            band.offset = 2000;
            require(controller.execute("mixer.eq", band).wasOk(), "Set the Master mid band");
            const auto &master = controller.project.mixer.front();
            require(master.eq.gain[1] == -6 && master.eq.freq[1] == 2000, "The mid band reads back");
            Project reloaded;
            require(Project::parse(controller.project.state(), reloaded).wasOk() &&
                        reloaded.mixer.front().eq == controller.project.mixer.front().eq,
                    "Mixer EQ survives save and load");
            band.value = 30;
            require(controller.execute("mixer.eq", band).failed(), "EQ gain beyond 18 dB is refused");
            controller.undoEdit();
            require(controller.project.mixer.front().eq == MixerEqSettings{}, "EQ edit is one Undo step");
        }
        CommandArgs empty;
        empty.beat = 4;
        require(controller.execute("clip.paste", empty).failed(), "Paste with nothing copied fails");
    }
}
} // namespace clip_repeat_acceptance
