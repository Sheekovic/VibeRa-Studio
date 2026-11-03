// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"

namespace vibera
{
// Drops into the real Rack and Playlist editors through the DragAndDropTarget entry points a
// mouse drag reaches (Browser file rows, Recent rows and built-ins): below the Channels with
// the velocity graph shown, onto a Channel name (replace), a built-in sound, and a Playlist
// drop above the grid. Mouse movement and drag-image travel are not exercised here.
juce::Result Studio::verifyDropTargets(const juce::File &folder)
{
    auto fail = [](const juce::String &message) { return juce::Result::fail("Drop targets: " + message); };
    folder.createDirectory();
    const auto sample = folder.getChildFile("drop-kick.wav");
    {
        juce::AudioBuffer<float> pcm(1, 4800);
        for (int i = 0; i < 4800; ++i)
            pcm.setSample(0, i, .5f * std::exp(-i / 600.f) * std::sin(i * .05f));
        sample.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(sample.createOutputStream().release(), 48000., 1, 24, {}, 0));
        if (!writer || !writer->writeFromAudioSampleBuffer(pcm, 0, 4800))
            return fail("cannot write the test sample");
    }
    for (int i = 0; i < 4; ++i)
        if (!windows[size_t(i)]->isVisible())
            toggleEditor(i);
    resized();
    auto *rack = dynamic_cast<juce::DragAndDropTarget *>(windows[0]->getContentComponent());
    auto *playlist = dynamic_cast<juce::DragAndDropTarget *>(windows[2]->getContentComponent());
    auto *rackComponent = windows[0]->getContentComponent();
    auto *playlistComponent = windows[2]->getContentComponent();
    if (!rack || !playlist || rackComponent->getHeight() < 120 || playlistComponent->getHeight() < 80)
        return fail("editors are not laid out");
    const auto drop = [&](juce::DragAndDropTarget *target, juce::Component *component, const juce::String &source,
                          juce::Point<int> at)
    {
        juce::DragAndDropTarget::SourceDetails details(source, browser.get(), at);
        if (!target->isInterestedInDragSource(details))
            return false;
        target->itemDragEnter(details);
        target->itemDropped(details);
        juce::ignoreUnused(component);
        return true;
    };
    // 1. Below the Channels while the velocity graph is shown: a new Channel, never lost.
    rackGraphVisible = true;
    rackComponent->resized();
    auto channels = controller.project.channels.size();
    if (!drop(rack, rackComponent, sample.getFullPathName(), {rackComponent->getWidth() / 2, rackComponent->getHeight() - 30}))
        return fail("the Rack refused a sample");
    if (controller.project.channels.size() != channels + 1 || controller.project.channels.back().source != sample.getFullPathName())
        return fail("a drop over the velocity graph did not add the Channel");
    rackGraphVisible = false;
    rackComponent->resized();
    // 2. A built-in sound (Recent row) on empty Rack space adds a Channel too.
    channels = controller.project.channels.size();
    if (!drop(rack, rackComponent, "builtin:snare", {rackComponent->getWidth() / 2, rackComponent->getHeight() - 30}) ||
        controller.project.channels.size() != channels + 1 || controller.project.channels.back().source != "builtin:snare")
        return fail("a built-in sound did not add a Channel");
    // 3. Onto the first Channel's name: its source is replaced, no Channel added.
    channels = controller.project.channels.size();
    const auto first = controller.project.channels.front().id;
    if (!drop(rack, rackComponent, sample.getFullPathName(), {60, 52}))
        return fail("the Rack refused a drop on a Channel name");
    if (controller.project.channels.size() != channels ||
        controller.project.channel(first)->source != sample.getFullPathName())
        return fail("a drop on a Channel name did not replace its sound");
    // 4. Playlist: samples are accepted, built-ins are not, and a drop above the grid queues
    // the clip import (recorded as a recent sound) instead of being ignored.
    juce::DragAndDropTarget::SourceDetails builtin("builtin:kick", browser.get(), {200, 10});
    if (playlist->isInterestedInDragSource(builtin))
        return fail("the Playlist accepted a built-in sound");
    status = "Ready";
    if (!drop(playlist, playlistComponent, sample.getFullPathName(), {400, 4}))
        return fail("the Playlist refused a sample");
    const auto recent = recentSources.favourites(20);
    if (status.startsWith("Choose WAV") ||
        std::none_of(recent.begin(), recent.end(), [&](const auto &e) { return e.source == sample.getFullPathName(); }))
        return fail("a Playlist drop above the grid was not imported (status: " + status + ")");
    // 5. Audio Clip tempo detection: short loops by whole-beat length, longer audio by rhythm.
    const auto writeClicks = [&](const juce::String &name, double seconds, double interval)
    {
        const auto file = folder.getChildFile(name);
        const int frames = int(seconds * 48000);
        juce::AudioBuffer<float> pcm(1, frames);
        pcm.clear();
        for (double t = 0; t < seconds; t += interval)
            for (int i = 0; i < 2400 && int(t * 48000) + i < frames; ++i)
                pcm.setSample(0, int(t * 48000) + i, .8f * std::exp(-i / 300.f) * std::sin(i * .2f));
        file.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(file.createOutputStream().release(), 48000., 1, 24, {}, 0));
        writer->writeFromAudioSampleBuffer(pcm, 0, frames);
        return file;
    };
    controller.project.tempo = 120;
    juce::String how;
    const double twoSeconds = detectFileTempo(writeClicks("loop-2s.wav", 2., .5), how);
    if (std::abs(twoSeconds - 120) > .5 || !how.startsWith("length"))
        return fail("a 2 s loop did not read as 120 BPM by length (" + juce::String(twoSeconds) + ", " + how + ")");
    const double short150 = detectFileTempo(writeClicks("loop-1.6s.wav", 1.6, .4), how);
    if (std::abs(short150 - 150) > .5)
        return fail("a 1.6 s loop did not read as 150 BPM (" + juce::String(short150) + ")");
    const double long120 = detectFileTempo(writeClicks("groove-10s.wav", 10., .5), how);
    if (std::abs(long120 - 120) > 3 && std::abs(long120 - 60) > 2 && std::abs(long120 - 240) > 5)
        return fail("a 10 s groove at 120 BPM was not detected (" + juce::String(long120) + ", " + how + ")");
    return juce::Result::ok();
}
} // namespace vibera
