// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "AudioStretch.h"
#include "Commands.h"

// Audio Clip stretching: pitch-preserving offline stretch of a source window, and the
// one-step source swap that applies it.
namespace audio_stretch_acceptance
{
using namespace vibera;

inline double zeroCrossingHz(const juce::AudioBuffer<float> &pcm, int from, int to, double rate)
{
    double first = -1, last = -1;
    int count = 0;
    for (int i = std::max(1, from); i < std::min(to, pcm.getNumSamples()); ++i)
    {
        const float a = pcm.getSample(0, i - 1), b = pcm.getSample(0, i);
        if (a < 0 && b >= 0)
        {
            const double t = i - 1 + a / double(a - b);
            if (first < 0)
                first = t;
            last = t;
            ++count;
        }
    }
    return count > 1 ? (count - 1) * rate / (last - first) : 0;
}

inline void tests(const juce::File &folder)
{
    const auto source = folder.getChildFile("stretch-source.wav");
    {
        juce::AudioBuffer<float> pcm(1, 96000);
        for (int i = 0; i < pcm.getNumSamples(); ++i)
            pcm.setSample(0, i, .4f * float(std::sin(i * 2 * juce::MathConstants<double>::pi * 220 / 48000)));
        source.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(source.createOutputStream().release(), 48000., 1, 24, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()), "Stretch source written");
    }
    const auto stretched = folder.getChildFile("stretched.wav");
    require(stretchAudioFile(source, .5, 1.5, 1.5, stretched).wasOk(), "A 1.5 s window stretches by 1.5");
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stretched.createInputStream().release(), true));
    require(reader && std::abs(int(reader->lengthInSamples) - 108000) <= 1, "Output is 1.5 x 1.5 s long");
    juce::AudioBuffer<float> out(1, int(reader->lengthInSamples));
    reader->read(&out, 0, out.getNumSamples(), 0, true, false);
    const double hz = zeroCrossingHz(out, 24000, 84000, 48000);
    require(std::abs(hz - 220) < 2, "Pitch is preserved (got " + juce::String(hz, 2).toStdString() + " Hz)");
    require(stretchAudioFile(source, 0, 1, 8, stretched).failed(), "Ratios beyond 4 are refused");
    require(stretchAudioFile(source, 0, .01, 2, stretched).failed(), "Too-short windows are refused");

    // Sample edits into new files.
    auto load = [&](const juce::File &file)
    {
        std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(file.createInputStream().release(), true));
        require(r != nullptr, "Edited WAV readable");
        juce::AudioBuffer<float> pcm(int(r->numChannels), int(r->lengthInSamples));
        r->read(&pcm, 0, pcm.getNumSamples(), 0, true, false);
        return pcm;
    };
    const auto edited = folder.getChildFile("edited.wav");
    require(editAudioFile(source, 0, 2, "normalize", -6, 0, 0, edited).wasOk(), "Normalize to -6 dBFS");
    require(std::abs(load(edited).getMagnitude(0, 96000) - juce::Decibels::decibelsToGain(-6.f)) < 1.e-3f,
            "Normalized peak is -6 dBFS");
    require(editAudioFile(source, 0, 2, "gain", 6, 0, 0, edited).wasOk(), "Gain +6 dB");
    require(std::abs(load(edited).getMagnitude(0, 96000) - .4f * juce::Decibels::decibelsToGain(6.f)) < 1.e-3f,
            "Gain scales the peak");
    require(editAudioFile(source, .25, 1, "reverse", 0, 0, 0, edited).wasOk(), "Reverse a window");
    {
        juce::AudioBuffer<float> original(1, 96000);
        std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(source.createInputStream().release(), true));
        r->read(&original, 0, 96000, 0, true, false);
        const auto reversed = load(edited);
        require(reversed.getNumSamples() == 48000 &&
                    std::abs(reversed.getSample(0, 0) - original.getSample(0, 12000 + 47999)) < 1.e-5f &&
                    std::abs(reversed.getSample(0, 47999) - original.getSample(0, 12000)) < 1.e-5f,
                "Reverse plays the window backwards");
    }
    require(editAudioFile(source, 0, 2, "consolidate", 0, .5, .5, edited).wasOk(), "Consolidate with fades");
    {
        const auto baked = load(edited);
        require(baked.getMagnitude(0, 0, 200) < .02f && baked.getMagnitude(0, 95800, 200) < .02f &&
                    std::abs(baked.getMagnitude(0, 47000, 2000) - .4f) < 1.e-3f,
                "Consolidate bakes the fades into the file and keeps the middle");
    }
    require(editAudioFile(source, 0, 2, "explode", 0, 0, 0, edited).failed(), "Unknown edits are refused");

    Controller controller;
    controller.project.channels.clear();
    CommandArgs add;
    add.text = source.getFullPathName();
    add.length = 2;
    add.beat = 4;
    require(controller.execute("audio.add", add).wasOk(), "Clip placed");
    const auto clip = controller.project.audioPlacements[0].id;
    CommandArgs fade;
    fade.target = clip;
    fade.fadeIn = .2;
    require(controller.execute("audio.fade", fade).wasOk(), "Clip faded");
    const auto before = controller.project.state();
    CommandArgs apply;
    apply.target = clip;
    apply.text = stretched.getFullPathName();
    apply.length = 3;
    require(controller.execute("audio.stretchApply", apply).wasOk(), "Stretched source applied");
    const auto &placed = controller.project.audioPlacements[0];
    require(placed.beat == 4 && placed.offset == 0 && placed.duration == 3 && std::abs(placed.fadeIn - .3) < 1.e-9,
            "The clip keeps its position, plays the new file from its start and scales its fades");
    require(controller.project.channels.size() == 1 && controller.project.channels[0].source == stretched.getFullPathName(),
            "The unused original source Channel is replaced");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(before), "Stretching is one exact Undo step");
    apply.step = true; // Consolidated: fades are now in the file.
    require(controller.execute("audio.stretchApply", apply).wasOk() &&
                controller.project.audioPlacements[0].fadeIn == 0,
            "Applying a consolidated file clears the clip fades");
    controller.undoEdit();

    // Tempo warp: variable-ratio render follows the map; pitch kept.
    const auto warped = folder.getChildFile("warped.wav");
    require(warpAudioFile(source, 0, 2, {{0., 0.}, {1., 1.}, {2., 3.}}, warped).wasOk(),
            "A map of 1 s as is + 1 s doubled renders");
    {
        const auto pcm = load(warped);
        require(std::abs(pcm.getNumSamples() - 144000) <= 1, "Warped length follows the map end (3 s)");
        const double early = zeroCrossingHz(pcm, 6000, 40000, 48000), late = zeroCrossingHz(pcm, 70000, 140000, 48000);
        require(std::abs(early - 220) < 3 && std::abs(late - 220) < 3,
                "Warped pitch is kept in both spans (" + juce::String(early, 1).toStdString() + ", " +
                    juce::String(late, 1).toStdString() + " Hz)");
        require(pcm.getMagnitude(0, 6000, 130000) > .3f, "Warped audio has no dropout");
    }
    require(warpAudioFile(source, 0, 2, {{0., 0.}, {2., 10.}}, warped).failed(), "Warp spans beyond 4x are refused");

    // A warp apply remembers the original; a re-warp keeps it; a plain stretch ends it.
    const auto originalChannel = controller.project.audioPlacements[0].channel;
    apply.step = false;
    apply.warpBpm = 120;
    apply.text = warped.getFullPathName();
    require(controller.execute("audio.stretchApply", apply).wasOk(), "Warp applied");
    {
        const auto &w = controller.project.audioPlacements[0];
        require(w.warpBpm == 120 && w.warpChannel == originalChannel && w.warpOffset == 0 && w.warpDuration == 2 &&
                    controller.project.channel(originalChannel) != nullptr,
                "The warped clip keeps its original source Channel and window");
    }
    const auto saved = controller.project.state();
    Project reloaded;
    require(Project::parse(saved, reloaded).wasOk() && reloaded.audioPlacements[0].warpBpm == 120 &&
                reloaded.audioPlacements[0].warpChannel == originalChannel,
            "Warp origin survives save/load");
    apply.text = stretched.getFullPathName();
    require(controller.execute("audio.stretchApply", apply).wasOk() &&
                controller.project.audioPlacements[0].warpChannel == originalChannel &&
                controller.project.channel(originalChannel) != nullptr,
            "A re-warp still points at the first original");
    apply.warpBpm = 0;
    require(controller.execute("audio.stretchApply", apply).wasOk() &&
                controller.project.audioPlacements[0].warpBpm == 0 &&
                controller.project.channel(originalChannel) == nullptr,
            "A plain stretch ends the warp and drops the unused original");
}
} // namespace audio_stretch_acceptance
