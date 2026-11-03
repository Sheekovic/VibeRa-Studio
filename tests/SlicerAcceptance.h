// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "SliceDetect.h"

// Slicex-style slicing: onset detection on a sample file and the one-step command that
// turns a sampler Channel into one Channel per slice plus a Pattern replaying them.
namespace slicer_acceptance
{
using namespace vibera;

inline void tests(const juce::File &folder)
{
    const auto file = folder.getChildFile("four-hits.wav");
    {
        juce::AudioBuffer<float> pcm(1, 96000);
        pcm.clear();
        for (double at : {0., .5, 1., 1.5})
            for (int i = 0; i < 6000; ++i)
                pcm.setSample(0, int(at * 48000) + i,
                              .6f * float(std::exp(-i / 900.) * std::sin(i * 2 * juce::MathConstants<double>::pi * 180 / 48000)));
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(file.createOutputStream().release(), 48000., 1, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, 96000), "Four-hit sample written");
    }
    const auto slices = detectSlices(file, .5);
    require(slices.size() == 4, "Four hits give four slices (got " + std::to_string(slices.size()) + ")");
    for (size_t i = 0; i < slices.size(); ++i)
        require(std::abs(slices[i] - .25 * double(i)) < .01, "Slices start at the hits");

    Controller controller;
    auto project = Project::empty();
    project.tempo = 120;
    project.channels[0].source = file.getFullPathName();
    project.channels[0].name = "Loop";
    const auto loop = project.channels[0].id;
    controller.replace(project);
    const auto before = controller.project.state();
    CommandArgs args;
    args.channel = loop;
    args.slicePoints = slices;
    args.length = 2;
    require(controller.execute("channel.sliceToChannels", args).wasOk(), "Slice to Channels");
    const auto &p = controller.project;
    require(p.channels.size() == project.channels.size() + 4, "One Channel per slice");
    const auto &third = p.channels[project.channels.size() + 2];
    require(third.name == "Loop 3" && std::abs(third.sampler.start - slices[2]) < 1.e-12 &&
                std::abs(third.sampler.end - slices[3]) < 1.e-12,
            "Each slice Channel plays its own window");
    const auto &part = p.patterns.back();
    require(part.name == "Loop slices" && part.notes.size() == 4 && std::abs(part.notes[1].beat - 1) < .03 &&
                std::abs(part.notes[3].beat - 3) < .03 && part.length == 4,
            "The Pattern replays the slices in place (beats 0..3 at 120 BPM)");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(before), "Slicing is one exact Undo step");
    args.slicePoints = {.2, .5};
    require(controller.execute("channel.sliceToChannels", args).failed(), "Slices must start at 0");
    args.slicePoints = {0, .6, .4};
    require(controller.execute("channel.sliceToChannels", args).failed(), "Slices must increase");
}
} // namespace slicer_acceptance
