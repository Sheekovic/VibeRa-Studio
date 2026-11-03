// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/LiveMidi.h"
#include "../src/MidiNoteOwners.h"
#include "../src/PluginHost.h"
#include <array>
#include <cmath>

namespace note_release_worker_acceptance
{
using namespace vibera;
constexpr int quantum = 256, frames = quantum * 44;
constexpr double rate = 48000;
// Independent literal source-frame oracles: five ordinary gates, then six
// 1/64-triplet Repeat gates at120 BPM. Include off offsets0/1/64/255.
constexpr std::array<int, 11> offFrames{64, 767, 1280, 2047, 2049, 5096, 6096, 7096, 8096, 9096, 10096};
inline void writePcm(const juce::File &file, const juce::AudioBuffer<float> &audio)
{
    auto stream = file.createOutputStream();
    require(bool(stream), "Release worker owned PCM stream opens");
    stream->setPosition(0);
    stream->truncate();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.release(), rate, 2, 24, {}, 0));
    require(writer && writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()),
            "Release worker actual PCM evidence writes");
}
inline juce::AudioBuffer<float> render(const juce::File &folder, const PluginSpec &fixture, int releaseByte)
{
    // This entire function runs on the test CONTROL thread with no device/audio
    // callback. Offline Host waits and the final bounded drain are deliberate.
    auto project = Project::empty();
    project.tempo = 120;
    auto &part = project.patterns.front();
    part.autoLength = false;
    part.length = 1;
    const auto channel = project.channels.front().id;
    constexpr std::array<int, 5> starts{0, 512, 1024, 1536, 2048};
    for (size_t i = 0; i < starts.size(); ++i)
    {
        Note note;
        note.id = project.allocate();
        note.channel = channel;
        note.beat = starts[i] / 24000.;
        note.length = (offFrames[i] - starts[i]) / 24000.;
        note.pitch = 60 + int(i);
        note.velocity = 1;
        note.releaseVelocity = releaseByte / 127.;
        part.notes.push_back(note);
    }
    Note repeated;
    repeated.id = project.allocate();
    repeated.channel = channel;
    repeated.beat = 4096. / 24000.;
    repeated.length = .25;
    repeated.pitch = 65;
    repeated.velocity = 1;
    repeated.repeatMode = 14;
    repeated.releaseVelocity = releaseByte / 127.;
    part.notes.push_back(repeated);
    require(project.validate().wasOk(), "Release worker original score is valid");
    const auto source = project.state();
    const auto document = compileLiveDocument(project, true, part.id, 1);
    const auto *lane = document->find(channel);
    require(lane && lane->notes.size() == offFrames.size(),
            "Release worker prepared score has literal five ordinary and six Repeat occurrences");
    PluginHost host;
    const auto started = host.start(fixture, rate, quantum, true);
    require(started.wasOk(), "Release actual VST3 worker starts: " + started.getErrorMessage().toStdString());
    require(host.transportLatencySamples() == quantum && host.latencySamples() == quantum,
            "Release fixture retains default1Q transport and zero native latency");
    LiveMidi midi;
    MidiNoteOwners owners;
    juce::AudioBuffer<float> audio(2, quantum), capture(2, frames);
    capture.clear();
    std::array<int, offFrames.size()> actualOffs{}, actualOffBytes{};
    size_t offCount = 0;
    bool offOverflow = false;
    juce::String rawTrace = "frame,status,pitch,velocity\n";
    for (int begin = 0; begin < frames; begin += quantum)
    {
        audio.clear();
        std::array<ipc::MidiEvent, ipc::events> routed{};
        int routedCount = 0;
        bool routedOverflow = false;
        const int count = midi.process(*document, channel, begin / rate, quantum, rate, true);
        for (int index = 0; index < count; ++index)
        {
            const auto &raw = midi.events[size_t(index)];
            // CONTROL/offline diagnostics only, outside the noexcept owner
            // callback. Do not add text work to a device callback.
            rawTrace += juce::String(begin + raw.frame) + "," + juce::String(int(raw.bytes[0])) + "," +
                        juce::String(int(raw.bytes[1])) + "," + juce::String(int(raw.bytes[2])) + "\n";
            owners.process(0, midi.events[size_t(index)],
                           [&](const ipc::MidiEvent &event) noexcept
                           {
                               if (routedCount >= int(routed.size()))
                               {
                                   routedOverflow = true;
                                   return;
                               }
                               routed[size_t(routedCount++)] = event;
                               if ((event.bytes[0] & 0xf0) == 0x80)
                               {
                                   if (offCount == actualOffs.size())
                                   {
                                       offOverflow = true;
                                       return;
                                   }
                                   actualOffs[offCount] = begin + event.frame;
                                   actualOffBytes[offCount++] = event.bytes[2];
                               }
                           });
        }
        require(!routedOverflow, "Release worker routed MIDI fits");
        host.process(audio, 0, quantum, routed.data(), routedCount, begin / rate, 120, true, false);
        for (int side = 0; side < 2; ++side)
            capture.copyFrom(side, begin, audio, side, 0, quantum);
    }
    // CONTROL/offline completion observation only; no realtime timing claim.
    const auto submitted = host.statistics().submitted;
    const auto deadline = juce::Time::getMillisecondCounter() + 5000;
    while (host.statistics().processed < submitted && host.alive() &&
           juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(1);
    const auto statistics = host.statistics();
    writePcm(folder.getChildFile("release-" + juce::String(releaseByte) + ".wav"), capture);
    std::array<int, offFrames.size()> expectedOffBytes{};
    expectedOffBytes.fill(releaseByte);
    juce::String routedTrace = "index,expectedFrame,actualFrame,actualByte\n";
    for (size_t i = 0; i < offFrames.size(); ++i)
        routedTrace += juce::String(int(i)) + "," + juce::String(offFrames[i]) + "," +
                       (i < offCount ? juce::String(actualOffs[i]) : "missing") + "," +
                       (i < offCount ? juce::String(actualOffBytes[i]) : "missing") + "\n";
    routedTrace +=
        "count," + juce::String(int(offCount)) + ",overflow," + juce::String(int(offOverflow)) + "\n";
    juce::String preparedTrace = "on,sourceId,ordinal,timeSeconds,timeFrames\n";
    for (const auto &event : lane->events)
    {
        const auto &note = lane->notes[event.index];
        preparedTrace += juce::String(int(event.on)) + "," + juce::String(note.note) + "," +
                         juce::String(int(note.repeatOrdinal)) + "," + juce::String(event.time, 17) + "," +
                         juce::String(event.time * rate, 17) + "\n";
    }
    const auto suffix = "-" + juce::String(releaseByte) + ".csv";
    require(folder.getChildFile("raw-midi" + suffix).replaceWithText(rawTrace) &&
                folder.getChildFile("routed-off" + suffix).replaceWithText(routedTrace) &&
                folder.getChildFile("prepared-midi" + suffix).replaceWithText(preparedTrace),
            "Retain actual raw/prepared/routed MIDI observations before literal verdict");
    require(!offOverflow && offCount == offFrames.size() && actualOffs == offFrames &&
                actualOffBytes == expectedOffBytes,
            "Prepared score/LiveMidi/owner path emits exact literal off positions and actual release byte");
    require(midi.overflow == 0 && host.alive() && !host.deliveryFailed() && statistics.missed == 0 &&
                statistics.dropped == 0 && statistics.midiOverflow == 0 && statistics.processed == submitted,
            "Release actual offline worker completes every submitted packet without MIDI/PCM loss");
    require(project.state().isEquivalentTo(source), "Worker rendering leaves raw source/IDs untouched");
    return capture;
}
inline void tests(const juce::File &folder, const PluginSpec &fixture)
{
    require(fixture.instrument && fixture.name == "VibeRa Note Release Fixture",
            "Release proof requires the distinct original VST3 fixture, not the legacy or installed synth");
    const auto neutral = render(folder, fixture, 0);
    double neutralPeak = 0;
    for (int side = 0; side < 2; ++side)
        neutralPeak = std::max(neutralPeak, double(neutral.getMagnitude(side, 0, frames)));
    require(neutralPeak > .01, "Neutral release fixture has genuine note-on/gate PCM");
    juce::String receipt = "releaseByte,sourceOffFrame,outputFrame,expectedL,actualL,expectedR,actualR\n";
    for (int releaseByte : {64, 127})
    {
        const auto actual = render(folder, fixture, releaseByte);
        double maximumError = 0;
        bool finite = true;
        for (int frame = 0; frame < frames; ++frame)
        {
            // Expected impulse positions come from literal musical/source
            // times above plus declared unchanged1Q transport, not LiveMidi or
            // a proprietary algorithm. Every other sample must equal neutral.
            const bool release =
                std::find(offFrames.begin(), offFrames.end(), frame - quantum) != offFrames.end();
            const double expectedL = release ? releaseByte / 127. / 4 : 0;
            const double expectedR = release ? -releaseByte / 127. / 8 : 0;
            const double left = actual.getSample(0, frame) - neutral.getSample(0, frame);
            const double right = actual.getSample(1, frame) - neutral.getSample(1, frame);
            finite = finite && std::isfinite(left) && std::isfinite(right);
            maximumError = std::max({maximumError, std::abs(left - expectedL), std::abs(right - expectedR)});
            if (release)
                receipt += juce::String(releaseByte) + "," + juce::String(frame - quantum) + "," +
                           juce::String(frame) + "," + juce::String(expectedL, 10) + "," +
                           juce::String(left, 10) + "," + juce::String(expectedR, 10) + "," +
                           juce::String(right, 10) + "\n";
        }
        require(folder.getChildFile("release-impulses.csv").replaceWithText(receipt),
                "Retain actual release impulse comparisons before verdict");
        require(finite && maximumError <= 2.e-6, "Real VST3 NoteOffEvent release64/127 drives exact "
                                                 "one-frame signed stereo PCM only at natural offs");
        for (size_t i = 0; i < 5; ++i)
            require(std::abs(neutral.getSample(0, offFrames[i] + quantum)) < 1.e-7 &&
                        std::abs(neutral.getSample(1, offFrames[i] + quantum)) < 1.e-7,
                    "Zero release leaves standalone gates silent after their note-off");
    }
}
} // namespace note_release_worker_acceptance

inline void noteReleaseWorkerTests(const juce::File &folder, const vibera::PluginSpec &fixture)
{
    note_release_worker_acceptance::tests(folder, fixture);
}
