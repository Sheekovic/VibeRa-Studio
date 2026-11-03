// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AudioInputCapture.h"
#include "Commands.h"
#include "AudioEngine.h"

// Loop takes and comping: pass splitting, one-step take placement and the comp edit.
namespace audio_comp_acceptance
{
using namespace vibera;

// Audio placements of the take group, ordered by lane then beat.
inline std::vector<AudioPlacement> lanePieces(const Project &project, int lane)
{
    std::vector<AudioPlacement> pieces;
    for (const auto &placement : project.audioPlacements)
        if (placement.lane == lane)
            pieces.push_back(placement);
    std::sort(pieces.begin(), pieces.end(), [](const auto &a, const auto &b) { return a.beat < b.beat; });
    return pieces;
}

inline void tests(const juce::File &folder)
{
    auto same = [](double a, double b) { return std::abs(a - b) < 1.e-9; };
    {
        // Loop [2, 6) s; recording starts at 3 s and runs 11.5 s: 3..6, 2..6, 2..6, 2..2.5.
        const auto passes = loopPasses(3, 11.5, 2, 6);
        require(passes.size() == 4, "Loop take splits at every wrap");
        require(same(passes[0].edit, 3) && same(passes[0].offset, 0) && same(passes[0].duration, 3) &&
                    same(passes[1].edit, 2) && same(passes[1].offset, 3) && same(passes[1].duration, 4) &&
                    same(passes[2].offset, 7) && same(passes[3].edit, 2) && same(passes[3].offset, 11) &&
                    same(passes[3].duration, .5),
                "Each pass maps its file window back to the loop start");
        require(loopPasses(3, 7.05, 2, 6).size() == 2, "A pass shorter than the minimum after Stop is dropped");
        const auto single = loopPasses(1, 4, 0, 0);
        require(single.size() == 1 && same(single[0].edit, 1) && same(single[0].duration, 4),
                "Without a loop the take is one pass");
        require(loopPasses(7, 3, 2, 6).size() == 1, "Starting after the loop end is one pass");
    }
    Controller controller;
    controller.project.channels.clear();
    controller.project.tempo = 120; // Two beats per second.
    const auto file = folder.getChildFile("loop-take.wav").getFullPathName();
    CommandArgs takes;
    takes.text = file;
    takes.length = 6;
    for (int i = 0; i < 3; ++i)
    {
        AudioPlacement window;
        window.beat = 0;
        window.lane = 2 + i;
        window.offset = 2. * i;
        window.duration = 2;
        window.muted = i < 2;
        takes.audioTakes.push_back(window);
    }
    {
        auto bad = takes;
        bad.audioTakes[1].offset = 5; // 5 + 2 > 6 s source.
        require(controller.execute("audio.takes", bad).failed(), "A window outside the source is refused");
        bad = takes;
        bad.text = "relative.wav";
        require(controller.execute("audio.takes", bad).failed(), "A relative source path is refused");
    }
    const auto empty = controller.project.state();
    require(controller.execute("audio.takes", takes).wasOk(), "Loop takes are placed");
    const auto &project = controller.project;
    require(project.channels.size() == 1 && project.channels[0].audioClip && project.audioPlacements.size() == 3 &&
                project.clipGroups.size() == 1 && project.clipGroups[0].members.size() == 3,
            "One source Channel, one clip per pass and one take group");
    require(project.audioPlacements[0].muted && project.audioPlacements[1].muted && !project.audioPlacements[2].muted,
            "The newest pass plays; earlier passes are muted takes");
    const Id first = project.audioPlacements[0].id;
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(empty), "Placing the takes is one Undo step");
    controller.redoEdit();

    // Comp the middle of take 1: slice it (grouped, so all takes slice) at beats 1 and 3.
    CommandArgs slice;
    slice.ids = {first};
    slice.beat = 1;
    require(controller.execute("clip.slice", slice).wasOk(), "Slice the grouped takes at beat 1");
    Id middle = 0;
    for (const auto &piece : lanePieces(controller.project, 2))
        if (same(piece.beat, 1))
            middle = piece.id;
    require(middle != 0, "Take 1 has a piece starting at beat 1");
    slice.ids = {middle};
    slice.beat = 3;
    require(controller.execute("clip.slice", slice).wasOk(), "Slice the grouped takes at beat 3");
    const auto beforeComp = controller.project.state();
    CommandArgs comp;
    comp.target = middle;
    require(controller.execute("audio.comp", comp).wasOk(), "Comp: use take 1 for beats 1..3");
    {
        const auto take1 = lanePieces(controller.project, 2), take3 = lanePieces(controller.project, 4);
        require(take1.size() == 3 && take3.size() == 3, "Each take is in three pieces");
        require(take1[0].muted && !take1[1].muted && take1[2].muted, "Only the comped section of take 1 plays");
        require(!take3[0].muted && take3[1].muted && !take3[2].muted,
                "Take 3 is muted only where take 1 now plays");
        for (const auto &piece : lanePieces(controller.project, 3))
            require(piece.muted, "Take 2 stays muted everywhere");
        require(same(take1[1].offset, .5 - .005) && same(take1[1].duration, 1 + .01),
                "The comped piece plays its own source window, widened by half a crossfade each side");
    }
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(beforeComp), "Comp is one exact Undo step");
    controller.redoEdit();

    // Flatten: the muted takes go, the playing pieces (take 3, comped take 1, take 3) share the top track.
    {
        const auto beforeFlatten = controller.project.state();
        CommandArgs flatten;
        flatten.target = middle;
        require(controller.execute("audio.flattenComp", flatten).wasOk(), "Flatten the comp");
        const auto &clips = controller.project.audioPlacements;
        require(clips.size() == 3 && std::none_of(clips.begin(), clips.end(), [](const auto &c) { return c.muted; }),
                "Only the three playing pieces remain");
        require(std::all_of(clips.begin(), clips.end(), [](const auto &c) { return c.lane == 2; }),
                "They move onto the take group's top track");
        require(std::any_of(clips.begin(), clips.end(), [&](const auto &c) { return c.id == middle; }),
                "The comped piece keeps its identity");
        controller.undoEdit();
        require(controller.project.state().isEquivalentTo(beforeFlatten), "Flatten is one exact Undo step");
        CommandArgs loose;
        loose.target = 999999;
        require(controller.execute("audio.flattenComp", loose).failed(), "A clip outside a take group is refused");
    }

    // Comp a whole unsliced take against takes that are not yet cut: they are cut at its edges.
    {
        Controller fresh;
        fresh.project.channels.clear();
        fresh.project.tempo = 120;
        auto unequal = takes;
        unequal.audioTakes[0].beat = 1; // Take 1 covers beats 1..5; the others 0..4.
        require(fresh.execute("audio.takes", unequal).wasOk(), "Offset takes are placed");
        const Id early = fresh.project.audioPlacements[0].id;
        CommandArgs whole;
        whole.target = early;
        require(fresh.execute("audio.comp", whole).wasOk(), "Comp a whole take");
        const auto take3 = lanePieces(fresh.project, 4);
        require(take3.size() == 2 && same(take3[0].beat, 0) && !take3[0].muted && same(take3[1].beat, 1) &&
                    take3[1].muted && same(take3[1].offset, 4.5),
                "Other takes are cut at the comped take's start and muted after it");
        const auto group = fresh.project.clipGroups[0].members;
        require(std::find(group.begin(), group.end(), take3[1].id) != group.end(),
                "New pieces stay in the take group");
    }
    // Comp crossfades: the chosen take overlaps its neighbours by 10 ms with fades.
    {
        Controller fresh;
        fresh.project.channels.clear();
        fresh.project.tempo = 120;
        auto wide = takes;
        wide.length = 6;
        for (int i = 0; i < 3; ++i)
        {
            wide.audioTakes[size_t(i)].offset = .5 + 1.5 * i; // Room for the overlap in the source.
            wide.audioTakes[size_t(i)].duration = 1.5;
        }
        require(fresh.execute("audio.takes", wide).wasOk(), "Takes with source headroom are placed");
        CommandArgs cut;
        cut.ids = {fresh.project.audioPlacements[0].id};
        cut.beat = 1;
        require(fresh.execute("clip.slice", cut).wasOk(), "Slice the takes at beat 1");
        cut.ids = {fresh.project.audioPlacements[0].id};
        cut.beat = .5;
        require(fresh.execute("clip.slice", cut).wasOk(), "Slice the takes at beat 0.5");
        Id piece = 0;
        for (const auto &placement : lanePieces(fresh.project, 2))
            if (same(placement.beat, .5))
                piece = placement.id;
        CommandArgs comp;
        comp.target = piece;
        require(fresh.execute("audio.comp", comp).wasOk(), "Comp take 1 for beats 0.5..1");
        const auto take1 = lanePieces(fresh.project, 2), take3 = lanePieces(fresh.project, 4);
        const double half = .005, halfBeats = half * 2;
        require(same(take1[1].beat, .5 - halfBeats) && same(take1[1].fadeIn, .01) && same(take1[1].fadeOut, .01) &&
                    same(take1[1].duration, .25 + .01),
                "The chosen piece starts half a crossfade early and fades at both edges");
        require(same(take3[0].fadeOut, .01) && same(take3[0].duration, .25 + half) && same(take3[2].fadeIn, .01) &&
                    same(take3[2].beat, 1 - halfBeats),
                "The neighbouring take overlaps the comp edges with matching fades");
        require(fresh.project.validate().wasOk(), "Crossfaded comp keeps every window inside its source");
    }
    // Fades: command validation, slicing keeps fades on the outer edges, persistence.
    {
        Controller faded;
        faded.project.channels.clear();
        faded.project.tempo = 120;
        CommandArgs add;
        add.text = file;
        add.length = 2;
        require(faded.execute("audio.add", add).wasOk(), "Clip for fades");
        const Id id = faded.project.audioPlacements[0].id;
        CommandArgs fade;
        fade.ids = {id};
        fade.fadeIn = 1.5;
        fade.fadeOut = 1;
        require(faded.execute("audio.fade", fade).failed(), "Fades longer than the clip are refused");
        fade.fadeIn = .5;
        fade.fadeOut = .25;
        const auto before = faded.project.state();
        require(faded.execute("audio.fade", fade).wasOk() && same(faded.project.audioPlacements[0].fadeIn, .5) &&
                    same(faded.project.audioPlacements[0].fadeOut, .25),
                "Fades are set");
        faded.undoEdit();
        require(faded.project.state().isEquivalentTo(before), "Fade change is one exact Undo step");
        faded.redoEdit();
        Project reopened;
        require(Project::parse(faded.project.state(), reopened).wasOk() &&
                    same(reopened.audioPlacements[0].fadeIn, .5) && same(reopened.audioPlacements[0].fadeOut, .25),
                "Fades survive save and reopen");
        auto old = faded.project.state().createCopy();
        old.setProperty("version", 42, nullptr);
        Project refused;
        require(Project::parse(old, refused).failed(), "A version-42 file cannot carry fades");
        CommandArgs slice;
        slice.ids = {id};
        slice.beat = 2;
        require(faded.execute("clip.slice", slice).wasOk(), "Slice a faded clip");
        const auto pieces = lanePieces(faded.project, 0);
        require(pieces.size() == 2 && same(pieces[0].fadeIn, .5) && same(pieces[0].fadeOut, 0) &&
                    same(pieces[1].fadeIn, 0) && same(pieces[1].fadeOut, .25),
                "Slicing keeps fades on the outer edges, none at the cut");
        CommandArgs trim;
        trim.target = pieces[0].id;
        trim.offset = 0;
        trim.length = .25;
        require(faded.execute("audio.trim", trim).wasOk(), "Trim shorter than its fade");
        for (const auto &placement : faded.project.audioPlacements)
            if (placement.id == pieces[0].id)
                require(placement.fadeIn + placement.fadeOut <= placement.duration + 1.e-9,
                        "Trimming keeps the fades inside the clip");
    }
    // Rendered fades: a constant source fades in from silence and out to silence.
    {
        const auto constant = folder.getChildFile("fade-source.wav");
        {
            juce::AudioBuffer<float> pcm(1, 96000);
            for (int i = 0; i < pcm.getNumSamples(); ++i)
                pcm.setSample(0, i, .5f);
            constant.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(constant.createOutputStream().release(), 48000., 1, 24, {}, 0));
            require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()), "Fade source WAV");
        }
        Controller render;
        render.project.channels.clear();
        render.project.tempo = 120;
        CommandArgs add;
        add.text = constant.getFullPathName();
        add.length = 2;
        require(render.execute("audio.add", add).wasOk(), "Constant clip");
        const auto plain = render.project;
        CommandArgs fade;
        fade.target = render.project.audioPlacements[0].id;
        fade.fadeIn = .5;
        fade.fadeOut = .5;
        require(render.execute("audio.fade", fade).wasOk(), "Fade the constant clip");
        AudioEngine engine(false);
        auto read = [&](const Project &project, const char *name)
        {
            const auto out = folder.getChildFile(name);
            require(engine.renderSync(project, out).wasOk(), "Render the clip");
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader(
                wav.createReaderFor(out.createInputStream().release(), true));
            require(reader != nullptr, "Rendered WAV readable");
            juce::AudioBuffer<float> pcm(int(reader->numChannels), int(reader->lengthInSamples));
            reader->read(&pcm, 0, int(reader->lengthInSamples), 0, true, true);
            return pcm;
        };
        const auto flat = read(plain, "fade-plain.wav"), shaped = read(render.project, "fade-shaped.wav");
        auto ratio = [&](int frame) { return shaped.getSample(0, frame) / flat.getSample(0, frame); };
        require(std::abs(flat.getSample(0, 48000)) > .05f, "Unfaded render carries the source");
        require(std::abs(ratio(48000) - 1.f) < 1.e-3f, "The middle of a faded clip is untouched");
        require(std::abs(ratio(20)) < .05f && ratio(12000) > .05f && ratio(12000) < .95f &&
                    ratio(23000) > ratio(12000),
                "The fade-in rises from silence over its 0.5 s");
        require(ratio(95900) < .05f && ratio(84000) > .05f && ratio(84000) < .95f,
                "The fade-out falls to silence at the clip end");
    }
    // Swipe comp: one command cuts the chosen take at the range edges and comps that part.
    {
        Controller fresh;
        fresh.project.channels.clear();
        fresh.project.tempo = 120;
        require(fresh.execute("audio.takes", takes).wasOk(), "Takes placed for swipe comp");
        const auto before = fresh.project.state();
        CommandArgs swipe;
        swipe.target = fresh.project.audioPlacements[0].id;
        swipe.compRange = std::array<double, 2>{5, 6};
        require(fresh.execute("audio.comp", swipe).failed(), "A swipe range outside the take is refused");
        swipe.compRange = std::array<double, 2>{1, 3};
        require(fresh.execute("audio.comp", swipe).wasOk(), "Swipe comp take 1 over beats 1..3");
        const auto take1 = lanePieces(fresh.project, 2), take3 = lanePieces(fresh.project, 4);
        require(take1.size() == 3 && take3.size() == 3, "Swipe cuts the chosen and the playing take in three");
        require(take1[0].muted && !take1[1].muted && take1[2].muted, "Only the swiped part of take 1 plays");
        require(!take3[0].muted && take3[1].muted && !take3[2].muted, "Take 3 is muted only under the swipe");
        require(same(take1[1].offset, .5 - .005) && same(take1[1].duration, 1 + .01),
                "The swiped piece plays its own window with the comp crossfades");
        const auto group = fresh.project.clipGroups[0].members;
        require(std::all_of(take1.begin(), take1.end(),
                            [&](const auto &piece)
                            { return std::find(group.begin(), group.end(), piece.id) != group.end(); }),
                "Swipe pieces stay in the take group");
        fresh.undoEdit();
        require(fresh.project.state().isEquivalentTo(before), "Swipe comp is one exact Undo step");
    }
    // A clip outside any take group cannot be comped.
    {
        Controller lone;
        lone.project.channels.clear();
        CommandArgs add;
        add.text = file;
        add.length = 2;
        require(lone.execute("audio.add", add).wasOk(), "Lone clip placed");
        CommandArgs comp1;
        comp1.target = lone.project.audioPlacements[0].id;
        require(lone.execute("audio.comp", comp1).failed(), "Comp refuses a clip without other takes");
    }
}
} // namespace audio_comp_acceptance
