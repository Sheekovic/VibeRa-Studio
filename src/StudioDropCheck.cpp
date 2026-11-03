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
    // 6. Mixer record arm: arming routes the recording input into the insert, again disarms.
    if (toggleInsertRecordArm(3).failed() || !insertRecordArmed(3) || audioInput.insert != 3)
        return fail("arming insert 3 did not route recording into it (" + status + ")");
    if (toggleInsertRecordArm(5).failed() || insertRecordArmed(3) || !insertRecordArmed(5))
        return fail("arming another insert did not move the arm");
    if (toggleInsertRecordArm(5).failed() || insertRecordArmed(5) || audioInput.insert != 0)
        return fail("arming again did not disarm");
    if (toggleInsertRecordArm(0).wasOk())
        return fail("the Master was armed");
    // 7. Step record: a chord commits on release at the cursor, the cursor advances by the
    // snap, a rest skips a step.
    selectedChannel = controller.project.channels.back().id;
    for (auto &channel : controller.project.channels)
        if (!channel.audioClip && !channel.instrument)
            selectedChannel = channel.id;
    snap = .5;
    audio.stop();
    performAction("transport.stepRecord");
    if (!stepRecord || stepCursor != 0)
        return fail("step record did not start at the playhead");
    const auto event = [&](int status, int pitch, int velocity)
    {
        MidiCapture::Event e;
        e.bytes = {std::uint8_t(status), std::uint8_t(pitch), std::uint8_t(velocity)};
        stepRecordEvent(e);
    };
    auto *pattern = controller.project.pattern(selectedPattern);
    const auto notesBefore = pattern ? pattern->notes.size() : 0;
    event(0x90, 60, 100);
    event(0x90, 64, 90);
    event(0x80, 60, 0);
    if (controller.project.pattern(selectedPattern)->notes.size() != notesBefore)
        return fail("a chord was written before every key was released");
    event(0x80, 64, 0);
    event(0x90, 67, 100);
    event(0x90, 67, 0); // Note-on with velocity 0 is a release.
    performAction("transport.stepRest");
    event(0x90, 72, 100);
    event(0x80, 72, 0);
    std::vector<std::pair<double, int>> written;
    for (const auto &n : controller.project.pattern(selectedPattern)->notes)
        if (n.channel == selectedChannel && (n.pitch == 60 || n.pitch == 64 || n.pitch == 67 || n.pitch == 72) && !n.step)
            written.push_back({n.beat, n.pitch});
    std::sort(written.begin(), written.end());
    const std::vector<std::pair<double, int>> expected{{0., 60}, {0., 64}, {.5, 67}, {1.5, 72}};
    if (written != expected || stepCursor != 2.)
        return fail("step record wrote the wrong positions or cursor (" + juce::String(stepCursor) + ")");
    performAction("transport.stepRecord");
    if (stepRecord)
        return fail("step record did not turn off");
    // Current project tree lists the project's samples and Patterns.
    const auto tree = browser->projectTreeForTest(folder.getChildFile("project-tree.png"));
    const auto text = tree.joinIntoString(" || ");
    if (!text.contains("SAMPLES (") || !text.contains("drop-kick.wav") || !text.contains("PATTERNS ("))
        return fail("project tree misses samples or Patterns: " + text);
    folder.getChildFile("project-tree.txt").replaceWithText(text);
    return juce::Result::ok();
}
} // namespace vibera
