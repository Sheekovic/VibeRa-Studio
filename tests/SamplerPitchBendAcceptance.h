// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AudioEngine.h"

// The local sampler follows the Channel's pitch bend events (+/-2 semitones): an offline
// render with full upward bend plays 2^(2/12) higher than the same render without it.
namespace sampler_pitch_bend_acceptance
{
using namespace vibera;

inline double crossingsPerSecond(const juce::File &file)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(), true));
    require(reader != nullptr, "Read the render");
    const int from = int(reader->sampleRate * .5), frames = int(reader->sampleRate * 1.);
    juce::AudioBuffer<float> pcm(1, frames);
    reader->read(&pcm, 0, frames, from, true, false);
    int crossings = 0;
    for (int i = 1; i < frames; ++i)
        crossings += (pcm.getSample(0, i - 1) < 0) != (pcm.getSample(0, i) < 0);
    return crossings / (frames / reader->sampleRate);
}

inline void tests(const juce::File &folder)
{
    auto project = Project::empty();
    const auto bass = project.channels[3].id;
    auto &part = project.patterns[0];
    part.length = 4;
    part.autoLength = false;
    Note note;
    note.id = project.allocate();
    note.channel = bass;
    note.beat = 0;
    note.length = 4;
    note.pitch = 45;
    note.velocity = .9f;
    part.notes.push_back(note);
    project.placements.push_back({project.allocate(), part.id, 0, 0, 4, 0});
    AudioEngine audio(false);
    const auto plain = folder.getChildFile("bend-off.wav"), bent = folder.getChildFile("bend-up.wav");
    require(audio.renderSync(project, plain).wasOk(), "Render without bend");
    ControllerEvent bend;
    bend.id = project.allocate();
    bend.channel = bass;
    bend.beat = 0;
    bend.type = 1;
    bend.value = 1;
    project.patterns[0].controllers.push_back(bend);
    require(audio.renderSync(project, bent).wasOk(), "Render with full upward bend");
    const double off = crossingsPerSecond(plain), up = crossingsPerSecond(bent);
    require(off > 20, "The bass note sounds");
    const double ratio = up / off, expected = std::pow(2., 2. / 12.);
    folder.getChildFile("bend.txt").replaceWithText("off " + juce::String(off) + " up " + juce::String(up) + " ratio " +
                                                     juce::String(ratio, 4) + "\n");
    require(std::abs(ratio - expected) < .03, "Full bend raises the sampler 2 semitones");
}
} // namespace sampler_pitch_bend_acceptance
