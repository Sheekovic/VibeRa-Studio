// SPDX-License-Identifier: AGPL-3.0-or-later
// Original acceptance fixture: an audio take through the real Studio transport on a
// virtual device whose inputs hear its own outputs after a fixed round trip (a
// loopback cable). A correctly placed, latency-compensated take of the metronome
// lines up with the grid. No physical device, listening or native OS input.
#include "Studio.h"
#include "Editors.h"
#include "../tests/SyntheticDevice.h"
#include <cmath>

namespace vibera
{
namespace
{
class LoopbackDevice final : public juce::AudioIODevice
{
  public:
    static constexpr int block = 256, inLatency = 300, outLatency = 500;
    static constexpr double rate = 48000;
    juce::AudioIODeviceCallback *callback = nullptr;
    juce::BigInteger inputs;
    std::array<std::vector<float>, 2> history{std::vector<float>(1 << 18), std::vector<float>(1 << 18)};
    juce::int64 written = 0;
    LoopbackDevice() : juce::AudioIODevice("Loopback", "VibeRa loopback") {}
    juce::StringArray getOutputChannelNames() override { return {"Out 1", "Out 2"}; }
    juce::StringArray getInputChannelNames() override { return {"In 1", "In 2"}; }
    juce::Array<double> getAvailableSampleRates() override { return {rate}; }
    juce::Array<int> getAvailableBufferSizes() override { return {block}; }
    int getDefaultBufferSize() override { return block; }
    juce::String open(const juce::BigInteger &in, const juce::BigInteger &, double, int) override
    {
        inputs = in;
        inputs.setRange(2, 30, false);
        return {};
    }
    void close() override { stop(); }
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback *next) override
    {
        callback = next;
        if (callback)
            callback->audioDeviceAboutToStart(this);
    }
    void stop() override
    {
        if (auto *previous = std::exchange(callback, nullptr))
            previous->audioDeviceStopped();
    }
    bool isPlaying() override { return callback != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return block; }
    double getCurrentSampleRate() override { return rate; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return inputs; }
    int getOutputLatencyInSamples() override { return outLatency; }
    int getInputLatencyInSamples() override { return inLatency; }
    // One device cycle: inputs carry the output from inLatency + outLatency frames ago.
    void process()
    {
        if (!callback)
            return;
        juce::AudioBuffer<float> in(2, block), out(2, block);
        const auto mask = juce::int64(history[0].size() - 1);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
            {
                const auto source = written + i - (inLatency + outLatency);
                in.setSample(c, i, source >= 0 ? history[size_t(c)][size_t(source & mask)] : 0.f);
            }
        std::array<const float *, 2> active{};
        int count = 0;
        for (int c = 0; c < 2; ++c)
            if (inputs[c])
                active[size_t(count++)] = in.getReadPointer(c);
        out.clear();
        callback->audioDeviceIOCallbackWithContext(active.data(), count, out.getArrayOfWritePointers(), 2, block, {});
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < block; ++i)
                history[size_t(c)][size_t((written + i) & mask)] = out.getSample(c, i);
        written += block;
    }
};
class LoopbackType final : public juce::AudioIODeviceType
{
  public:
    LoopbackType() : juce::AudioIODeviceType("VibeRa loopback") {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return {"Loopback"}; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice *device, bool) const override { return device ? 0 : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice *createDevice(const juce::String &, const juce::String &) override
    {
        return new LoopbackDevice;
    }
};
juce::AudioBuffer<float> readTake(const juce::File &file)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(), true));
    if (!reader)
        return {};
    juce::AudioBuffer<float> pcm(int(reader->numChannels), int(reader->lengthInSamples));
    reader->read(&pcm, 0, int(reader->lengthInSamples), 0, true, true);
    return pcm;
}
// First sample at or after `from` whose magnitude passes the threshold, or -1.
int onset(const juce::AudioBuffer<float> &pcm, int from, int to)
{
    for (int i = juce::jmax(0, from); i < juce::jmin(to, pcm.getNumSamples()); ++i)
        if (std::abs(pcm.getSample(0, i)) > .02f)
            return i;
    return -1;
}
} // namespace

juce::Result Studio::verifyAudioTake(const juce::File &folder)
{
    auto fail = [&](const juce::String &message) { return juce::Result::fail("audioTake: " + message); };
    folder.getChildFile("audio-take-failure.txt").deleteFile(); // Never leave an older attempt's failure.
    auto &manager = audio.engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<LoopbackType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa loopback");
    setup.setAttribute("audioOutputDeviceName", "Loopback");
    setup.setAttribute("audioInputDeviceName", "Loopback");
    setup.setAttribute("audioDeviceRate", LoopbackDevice::rate);
    setup.setAttribute("audioDeviceBufferSize", LoopbackDevice::block);
    audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    // Production registers the capture after Tracktion so it runs first in each cycle.
    manager.deviceManager.addAudioCallback(&audio.inputCapture);
    dispatchMessages(20);
    auto device = [&] { return dynamic_cast<LoopbackDevice *>(manager.deviceManager.getCurrentAudioDevice()); };
    if (!device())
        return fail("virtual loopback device did not open");
    projectFile = folder.getChildFile("audio-take.vibera"); // Takes go to folder/Recorded.
    juce::String report;
    auto run = [&](bool pattern, int countInBars, double seconds) -> juce::Result
    {
        audio.stop();
        auto project = Project::empty();
        project.tempo = 120; // One beat = 24000 frames.
        project.patterns[0].length = 16;
        controller.replace(project);
        selectedPattern = project.patterns[0].id;
        selectedChannel = project.channels[0].id;
        patternMode = pattern;
        mode.setButtonText(pattern ? "Pattern" : "Song");
        refresh();
        if (auto result = audio.rebuild(controller.project, pattern, selectedPattern); result.failed())
            return result;
        needsRebuild = false;
        projectionError.clear();
        audio.setMetronome(true);
        audio.setCountInBars(countInBars);
        audio.seekBeat(0);
        for (int i = 0; i < 8; ++i)
            device()->process();
        dispatchMessages(5);
        recordArmed = true;
        play.onClick();
        if (pattern)
        {
            stop.onClick(); // Also seals the (empty) MIDI take the same Record + Play started.
            return audioTake.code == "audioTake.patternMode" && !audioTaking && !audio.inputCapture.capturing()
                       ? juce::Result::ok()
                       : fail("Pattern mode did not refuse the audio take");
        }
        if (!audioTaking || !audio.inputCapture.capturing())
            return fail("Record + Play did not start an audio take: " + audioTake.code + " " + audioTake.message);
        const int blocks = int(seconds * LoopbackDevice::rate / LoopbackDevice::block);
        for (int i = 0; i < blocks; ++i)
        {
            device()->process();
            if (i % 8 == 0)
                dispatchMessages(1);
        }
        const auto historyBefore = controller.project.audioPlacements.size();
        stop.onClick();
        dispatchMessages(5);
        if (audioTake.phase != "completed" || audioTake.clip == 0)
            return fail("take not placed: " + audioTake.phase + " " + audioTake.code + " " + audioTake.message +
                        " | blockers device=" + juce::String(int(devicePreparationBusy())) +
                        " routing=" + juce::String(int(routingPending())) +
                        " pendingMidi=" + juce::String(int(hasPendingMidiTake())) +
                        " midiTaking=" + juce::String(int(midiTaking)) +
                        " gesture=" + juce::String(int(controller.hasGesture())) +
                        " chain=" + juce::String(int(mixerChainApplying())) +
                        " automation=" + juce::String(int(automationRecordingActive())) + " status=" + status);
        if (controller.project.audioPlacements.size() != historyBefore + 1)
            return fail("expected exactly one new Audio Clip");
        const auto *clip = &controller.project.audioPlacements.back();
        if (std::abs(clip->beat) > 1.e-9 || clip->lane != 0)
            return fail("take should start at beat 0 on Track 1, got beat " + juce::String(clip->beat, 6) +
                        " lane " + juce::String(clip->lane));
        const auto pcm = readTake(audioTake.file);
        if (pcm.getNumChannels() != 1 || pcm.getNumSamples() < int(LoopbackDevice::rate * 2))
            return fail("recorded WAV missing or too short");
        if (std::abs(clip->duration - pcm.getNumSamples() / LoopbackDevice::rate) > 1.e-6)
            return fail("clip duration differs from the file");
        // Every metronome click sits on its grid beat: compensation and placement are exact.
        juce::String onsets;
        for (int beat = 0; beat * 24000 < pcm.getNumSamples() - 24000; ++beat)
        {
            const int expected = beat * 24000;
            const int found = onset(pcm, expected - 4000, expected + 4000);
            onsets << beat << ":" << (found < 0 ? juce::String("none") : juce::String(found - expected)) << " ";
            if (found < 0 || std::abs(found - expected) > 48)
                return fail("click " + juce::String(beat) + " off the grid; onsets (frames from beat) " + onsets);
        }
        report << "countIn=" << countInBars << " seconds=" << juce::String(audioTake.seconds, 4)
               << " latencyMs=" << juce::String(audioTake.latencyMs, 3) << " onsets " << onsets << "\n";
        if (std::abs(audioTake.latencyMs - 1000. * (LoopbackDevice::inLatency + LoopbackDevice::outLatency) /
                                                LoopbackDevice::rate) > 1.e-6)
            return fail("reported latency compensation differs from the device round trip");
        // One Undo step removes the take's clip and Audio Clip Channel.
        controller.undoEdit();
        if (controller.project.audioPlacements.size() != historyBefore)
            return fail("Undo did not remove the placed take");
        const auto state = audioRecordingState();
        if (state["take"]["completionState"].toString() != "completed" || !bool(state["enabled"]))
            return fail("readback does not report the completed take");
        return juce::Result::ok();
    };
    AudioInputSettings input;
    input.enabled = true;
    input.channels = 1;
    if (auto result = applyAudioInput(input); result.failed())
        return result;
    dispatchMessages(10);
    if (audioInputReadiness() != "ready" || !device() || !device()->inputs[0])
        return fail("selected input not opened: " + audioInputReadiness());
    for (auto [pattern, countIn] : {std::pair{true, 0}, std::pair{false, 0}, std::pair{false, 1}})
        if (auto result = run(pattern, countIn, countIn ? 6. : 4.); result.failed())
        {
            folder.getChildFile("audio-take.txt").replaceWithText(report + result.getErrorMessage());
            return result;
        }
    // Loop recording: a one-bar Song loop (2 s) recorded for 5 s gives three takes
    // (2 s, 2 s, 1 s) stacked on free tracks, grouped, newest playing. Every pass holds
    // the metronome on its own grid, so each window of the file is placed correctly.
    {
        auto project = Project::empty();
        project.tempo = 120;
        project.patterns[0].length = 4;
        project.placements.push_back({project.allocate(), project.patterns[0].id, 0, 0., 4., 0.});
        controller.replace(project);
        patternMode = false;
        mode.setButtonText("Song");
        refresh();
        if (auto result = audio.rebuild(controller.project, false, selectedPattern); result.failed())
            return result;
        needsRebuild = false;
        audio.setLooping(true);
        audio.setCountInBars(0);
        audio.seekBeat(0);
        for (int i = 0; i < 8; ++i)
            device()->process();
        dispatchMessages(5);
        recordArmed = true;
        play.onClick();
        if (!audioTaking)
            return fail("loop take did not start: " + audioTake.code + " " + audioTake.message);
        for (int i = 0; i < int(5 * LoopbackDevice::rate / LoopbackDevice::block); ++i)
        {
            device()->process();
            if (i % 8 == 0)
                dispatchMessages(1);
        }
        if (performAction("transport.loop") || !audio.looping())
            return fail("loop mode changed during an audio take");
        stop.onClick();
        dispatchMessages(5);
        audio.setLooping(false);
        if (audioTake.phase != "completed" || audioTake.passes != 3 || audioTake.clips.size() != 3 ||
            audioTake.group == 0)
            return fail("loop take: expected 3 grouped passes, got " + juce::String(audioTake.passes) + " " +
                        audioTake.phase + " " + audioTake.message);
        const auto pcm = readTake(audioTake.file);
        juce::String onsets;
        std::vector<AudioPlacement> takes;
        for (auto id : audioTake.clips)
            for (const auto &clip : controller.project.audioPlacements)
                if (clip.id == id)
                    takes.push_back(clip);
        for (size_t k = 0; k < takes.size(); ++k)
        {
            const auto &take = takes[k];
            if (std::abs(take.beat) > 1.e-9 || take.lane != int(k) + 1 || take.muted != (k + 1 < takes.size()))
                return fail("take " + juce::String(int(k)) + " placement/mute wrong: beat " + juce::String(take.beat) +
                            " lane " + juce::String(take.lane));
            const int base = juce::roundToInt(take.offset * LoopbackDevice::rate);
            for (int beat = 0; (beat + 1) * 24000 <= juce::roundToInt(take.duration * LoopbackDevice::rate); ++beat)
            {
                // Beat 0 of a wrapped pass starts exactly on the source window edge.
                const int expected = base + beat * 24000;
                const int found = onset(pcm, expected - (beat == 0 && k > 0 ? 0 : 4000), expected + 4000);
                onsets << int(k) << "." << beat << ":" << (found < 0 ? juce::String("none") : juce::String(found - expected)) << " ";
                if (found < 0 || std::abs(found - expected) > 48)
                    return fail("loop take click off its grid; onsets " + onsets);
            }
        }
        report << "loop takes=" << int(takes.size()) << " onsets " << onsets << "\n";
        // Comp: the first take plays its whole pass; the others are muted there.
        CommandArgs comp;
        comp.target = takes[0].id;
        command("audio.comp", comp); // The Playlist menu path; audio edits may defer through routing.
        for (int i = 0; i < 400 && (routingPending() || devicePreparationBusy()); ++i)
        {
            device()->process();
            dispatchMessages(2);
        }
        for (const auto &clip : controller.project.audioPlacements)
            if (clip.channel == takes[0].channel && clip.muted != (clip.id != takes[0].id))
                return fail("comp did not leave only the chosen take playing: " + status);
        controller.undoEdit();
        for (const auto &clip : controller.project.audioPlacements)
            if (clip.id == takes[0].id && !clip.muted)
                return fail("Undo did not restore the takes before comping");
    }
    // Punch: with the Playlist time selection at beats 2..4 a take recorded from 0 keeps
    // only that range; its clicks still sit on beats 2 and 3.
    {
        auto project = Project::empty();
        project.tempo = 120;
        project.patterns[0].length = 16;
        controller.replace(project);
        patternMode = false;
        refresh();
        if (auto result = audio.rebuild(controller.project, false, selectedPattern); result.failed())
            return result;
        needsRebuild = false;
        audio.setLooping(false);
        audio.seekBeat(0);
        playlistRange = std::pair{2., 4.};
        recordArmed = true;
        play.onClick();
        for (int i = 0; i < int(4 * LoopbackDevice::rate / LoopbackDevice::block); ++i)
        {
            device()->process();
            if (i % 8 == 0)
                dispatchMessages(1);
        }
        stop.onClick();
        dispatchMessages(5);
        playlistRange.reset();
        if (audioTake.phase != "completed" || audioTake.clips.size() != 1)
            return fail("punch take not placed: " + audioTake.phase + " " + audioTake.message);
        const AudioPlacement *punched = nullptr;
        for (const auto &clip : controller.project.audioPlacements)
            if (clip.id == audioTake.clip)
                punched = &clip;
        if (!punched || std::abs(punched->beat - 2) > 1.e-9 || std::abs(punched->duration - 1) > 1.e-6)
            return fail("punch clip should cover beats 2..4 exactly");
        const auto pcm = readTake(audioTake.file);
        const int base = juce::roundToInt(punched->offset * LoopbackDevice::rate);
        juce::String onsets;
        for (int beat = 0; beat < 2; ++beat)
        {
            const int expected = base + beat * 24000;
            const int found = onset(pcm, expected - (beat == 0 ? 2000 : 4000), expected + 4000);
            onsets << (found < 0 ? juce::String("none") : juce::String(found - expected)) << " ";
            if (found < 0 || std::abs(found - expected) > 48)
                return fail("punched click off its grid: " + onsets + " offset=" + juce::String(punched->offset, 6) +
                            " file=" + juce::String(pcm.getNumSamples()));
        }
        report << "punch beats 2..4 offset=" << juce::String(punched->offset, 4) << " onsets " << onsets << "\n";
        // A punch range the take never reaches keeps nothing and says so.
        controller.undoEdit();
        audio.seekBeat(0);
        playlistRange = std::pair{40., 44.};
        recordArmed = true;
        play.onClick();
        for (int i = 0; i < 100; ++i)
            device()->process();
        stop.onClick();
        playlistRange.reset();
        if (audioTake.code != "audioTake.outsidePunch" || !controller.project.audioPlacements.empty())
            return fail("a take outside the punch range was placed: " + audioTake.code);
    }
    // Crash safety: what a crash leaves mid-take (the WAV as flushed so far plus its
    // sidecar) is listed at the next start and placed where it was recorded.
    {
        audioTakeIndex = folder.getChildFile("active-takes.json");
        audioTakeIndex.deleteFile();
        auto project = Project::empty();
        project.tempo = 120;
        project.patterns[0].length = 16;
        controller.replace(project);
        patternMode = false;
        refresh();
        if (auto result = audio.rebuild(controller.project, false, selectedPattern); result.failed())
            return result;
        needsRebuild = false;
        audio.seekBeat(0);
        recordArmed = true;
        play.onClick();
        for (int i = 0; i < int(2.5 * LoopbackDevice::rate / LoopbackDevice::block); ++i)
            device()->process();
        // Wait (bounded) for the writer thread to drain the ring and flush two seconds of audio.
        for (int wait = 0; wait < 100; ++wait)
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> flushed(
                wav.createReaderFor(audioTake.file.createInputStream().release(), true));
            if (flushed && flushed->lengthInSamples >= juce::int64(1.95 * LoopbackDevice::rate))
                break;
            juce::Thread::sleep(50);
        }
        pollAudioTake();
        const auto live = audioTake.file, sidecar = audioTakeSidecar();
        const auto crashed = folder.getChildFile("crashed-take.wav");
        const auto crashedSidecar = crashed.withFileExtension("take.json");
        if (!audioTakeSidecarMapped || !sidecar.existsAsFile() || !live.copyFileTo(crashed))
            return fail("a capturing take has no mapped sidecar or WAV on disk");
        auto record = juce::JSON::parse(sidecar);
        if (!record.hasProperty("startBeat") || std::abs(double(record["startBeat"])) > 1.e-9)
            return fail("the sidecar should record the take's Song beat (0 here)");
        record.getDynamicObject()->setProperty("file", crashed.getFullPathName());
        crashedSidecar.replaceWithText(juce::JSON::toString(record));
        {
            auto index = juce::JSON::parse(audioTakeIndex);
            if (!index["takes"].getArray() || !index["takes"].getArray()->contains(sidecar.getFullPathName()))
                return fail("the capturing take is not in the active-take index");
            index["takes"].getArray()->add(crashedSidecar.getFullPathName());
            audioTakeIndex.replaceWithText(juce::JSON::toString(index));
        }
        cancelAudioTake();
        stop.onClick();
        if (sidecar.existsAsFile())
            return fail("a cancelled take kept its sidecar");
        scanRecoveredTakes();
        if (recoveredTakes.size() != 1 || recoveredTakes[0].file != crashed || !recoveredTakes[0].mapped ||
            recoveredTakes[0].seconds < 1.9 || std::abs(recoveredTakes[0].beat) > 1.e-9)
            return fail("the interrupted take was not recovered with its flushed audio and position (" +
                        juce::String(int(recoveredTakes.size())) + ")");
        const auto seconds = recoveredTakes[0].seconds;
        if (auto result = placeRecoveredTake(crashed); result.failed())
            return result;
        const auto &placed = controller.project.audioPlacements.back();
        if (std::abs(placed.beat) > 1.e-9 || std::abs(placed.duration - seconds) > 1.e-9 || !recoveredTakes.empty() ||
            crashedSidecar.existsAsFile())
            return fail("recovered take placement or cleanup wrong");
        const auto pcm = readTake(crashed);
        for (int beat = 0; (beat + 1) * 24000 < pcm.getNumSamples(); ++beat)
        {
            const int found = onset(pcm, beat * 24000 - (beat ? 4000 : 0), beat * 24000 + 4000);
            if (found < 0 || std::abs(found - beat * 24000) > 48)
                return fail("recovered take clicks off the grid at beat " + juce::String(beat));
        }
        report << "recovered seconds=" << juce::String(seconds, 3) << "\n";
        // With tempo changes the recorded Song beat wins over seconds at the base tempo
        // (3 s at 120 BPM would be beat 6; the sidecar says 7.5).
        {
            const auto mapped = folder.getChildFile("crashed-mapped.wav");
            const auto mappedSidecar = mapped.withFileExtension("take.json");
            if (!crashed.copyFileTo(mapped))
                return fail("copy the mapped recovery fixture");
            auto row = juce::JSON::parse(R"({"version":1,"tempo":120,"mapped":true,"startSeconds":3,"startBeat":7.5})");
            row.getDynamicObject()->setProperty("file", mapped.getFullPathName());
            mappedSidecar.replaceWithText(juce::JSON::toString(row));
            auto index = juce::JSON::parse(R"({"takes":[]})");
            index["takes"].getArray()->add(mappedSidecar.getFullPathName());
            audioTakeIndex.replaceWithText(juce::JSON::toString(index));
            scanRecoveredTakes();
            if (recoveredTakes.size() != 1 || std::abs(recoveredTakes[0].beat - 7.5) > 1.e-9)
                return fail("a recovered take should use its recorded Song beat");
            if (auto result = placeRecoveredTake(mapped); result.failed())
                return result;
            if (std::abs(controller.project.audioPlacements.back().beat - 7.5) > 1.e-9)
                return fail("the recovered take should be placed at beat 7.5");
        }
        audioTakeIndex = juce::File{};
    }
    // Esc cancels a capturing take and leaves nothing behind.
    {
        // Two inputs at once as two mono tracks: one file, Channel and track each, one Undo step.
        auto both = input;
        both.tracks = 2;
        both.insert = 3; // Tracks feed Mixer inserts 3 and 4.
        if (auto result = applyAudioInput(both); result.failed())
            return fail("two-track input: " + result.getErrorMessage());
        dispatchMessages(10);
        if (audioInputReadiness() != "ready" || !device() || !device()->inputs[0] || !device()->inputs[1])
            return fail("both inputs not opened: " + audioInputReadiness());
        auto project = Project::empty();
        project.tempo = 120;
        project.patterns[0].length = 16;
        controller.replace(project);
        patternMode = false;
        refresh();
        if (auto result = audio.rebuild(controller.project, false, selectedPattern); result.failed())
            return result;
        needsRebuild = false;
        audio.setMetronome(true);
        audio.setCountInBars(0);
        audio.seekBeat(0);
        for (int i = 0; i < 8; ++i)
            device()->process();
        dispatchMessages(5);
        const auto before = controller.project.state();
        recordArmed = true;
        play.onClick();
        for (int i = 0; i < int(3. * LoopbackDevice::rate / LoopbackDevice::block); ++i)
        {
            device()->process();
            if (i % 8 == 0)
                dispatchMessages(1);
        }
        stop.onClick();
        dispatchMessages(5);
        const auto &placed = controller.project.audioPlacements;
        if (audioTake.phase != "completed" || audioTake.files.size() != 2 || placed.size() != 2 ||
            placed[0].lane == placed[1].lane || placed[0].channel == placed[1].channel)
            return fail("two-track take not placed as two clips on two tracks: " + audioTake.phase + " " +
                        audioTake.message);
        for (size_t t = 0; t < 2; ++t)
        {
            const auto *channel = controller.project.channel(placed[t].channel);
            if (!channel || channel->mixer != 3 + int(t))
                return fail("track " + juce::String(int(t) + 1) + " should feed Mixer insert " + juce::String(3 + int(t)));
        }
        for (const auto &file : audioTake.files)
        {
            const auto pcm = readTake(file);
            if (pcm.getNumChannels() != 1 || pcm.getNumSamples() < int(LoopbackDevice::rate * 2))
                return fail("track file missing, not mono or too short: " + file.getFileName());
            const int found = onset(pcm, 24000 - 4000, 24000 + 4000);
            if (found < 0 || std::abs(found - 24000) > 48)
                return fail("track " + file.getFileName() + " click off the grid");
        }
        if (juce::File(audioTake.files[0].getFullPathName().replace("-in1.wav", ".wav")).existsAsFile())
            return fail("the combined several-input file should be replaced by the track files");
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(before))
            return fail("Undo should remove both tracks of the take in one step");
        if (auto result = applyAudioInput(input); result.failed())
            return result;
        dispatchMessages(10);
        report << "two-track take: " << audioTake.files[0].getFileName() << " " << audioTake.files[1].getFileName()
               << "\n";
    }
    {
        auto project = Project::empty();
        project.patterns[0].length = 16;
        controller.replace(project);
        patternMode = false;
        refresh();
        audio.seekBeat(0);
        recordArmed = true;
        play.onClick();
        for (int i = 0; i < 40; ++i)
            device()->process();
        const auto file = audioTake.file;
        cancelAudioTake();
        stop.onClick();
        if (audioTake.phase != "cancelled" || file.existsAsFile() || !controller.project.audioPlacements.empty())
            return fail("cancel kept a file or clip");
    }
    folder.getChildFile("audio-take.txt").replaceWithText(report + "PASS\n");
    return juce::Result::ok();
}
juce::Result Studio::verifyAccessibility(const juce::File &folder)
{
    setSize(1600, 940);
    resized();
    refresh();
    juce::StringArray missing, named, unnamedValues, smallTargets;
    int namedValues = 0;
    std::function<void(juce::Component &)> walk = [&](juce::Component &c)
    {
        if (!c.isVisible())
            return;
        // Sliders, choice boxes and text fields need a spoken name too (title, tooltip or name).
        if (dynamic_cast<juce::Slider *>(&c) || dynamic_cast<juce::ComboBox *>(&c) ||
            dynamic_cast<juce::TextEditor *>(&c))
        {
            auto *handler = c.getAccessibilityHandler();
            auto *client = dynamic_cast<juce::SettableTooltipClient *>(&c);
            const auto label = handler ? handler->getTitle() : juce::String();
            if (label.trim().isEmpty() && (!client || client->getTooltip().trim().isEmpty()) &&
                c.getName().trim().isEmpty() && c.getTitle().trim().isEmpty())
            {
                juce::String where;
                for (auto *p = c.getParentComponent(); p && where.length() < 200; p = p->getParentComponent())
                    where << "<" << juce::String(typeid(*p).name()).fromLastOccurrenceOf("::", false, false);
                unnamedValues.add(juce::String(typeid(c).name()).fromLastOccurrenceOf("::", false, false) + " " +
                                  c.getBounds().toString() + " " + where);
            }
            else
                namedValues++;
        }
        if (auto *button = dynamic_cast<juce::Button *>(&c))
        {
            // Pointer targets: at least 16 x 16 logical pixels (readable, clickable).
            if (c.getWidth() < 16 || c.getHeight() < 16)
            {
                juce::String where;
                for (auto *p = c.getParentComponent(); p && where.length() < 200; p = p->getParentComponent())
                    where << "<" << juce::String(typeid(*p).name()).fromLastOccurrenceOf("::", false, false);
                smallTargets.add(button->getButtonText() + " " + c.getBounds().toString() + " " + where);
            }
            auto *handler = button->getAccessibilityHandler();
            const auto title = handler ? handler->getTitle() : juce::String();
            const auto tooltip = button->getTooltip();
            if (title.trim().isEmpty() && button->getButtonText().trim().isEmpty() && tooltip.trim().isEmpty())
                missing.add(c.getComponentID().isNotEmpty() ? c.getComponentID() : c.getName());
            else
                named.add(title.isNotEmpty() ? title : button->getButtonText());
        }
        for (auto *child : c.getChildren())
            walk(*child);
    };
    walk(*this);
    for (auto &w : windows)
        if (w)
            walk(*w);
    juce::StringArray editors;
    for (auto &w : windows)
        if (w)
            if (auto *editor = dynamic_cast<Editor *>(w->getContentComponent()))
            {
                auto *handler = editor->getAccessibilityHandler();
                if (!handler || handler->getTitle().isEmpty() || editor->getDescription().isEmpty() ||
                    editor->getHelpText().isEmpty())
                    return juce::Result::fail("accessibility: an editor has no title/description/help");
                editors.add(handler->getTitle() + ": " + editor->getDescription());
            }
    folder.getChildFile("accessibility.txt")
        .replaceWithText("editors:\n" + editors.joinIntoString("\n") + "\nnamed controls: " +
                         juce::String(named.size()) + "\nunnamed: " + missing.joinIntoString(", ") +
                         "\nnamed value controls: " + juce::String(namedValues) +
                         "\nunnamed value controls: " + unnamedValues.joinIntoString(", ") +
                         "\nsmall targets: " + smallTargets.joinIntoString(", ") + "\n");
    if (!missing.isEmpty())
        return juce::Result::fail("accessibility: unnamed controls: " + missing.joinIntoString(", "));
    if (!unnamedValues.isEmpty() || namedValues < 10)
        return juce::Result::fail("accessibility: unnamed sliders/choices/text fields: " + unnamedValues.joinIntoString(", "));
    if (!smallTargets.isEmpty())
        return juce::Result::fail("accessibility: buttons under 16 px: " + smallTargets.joinIntoString(", "));
    // Readable contrast (WCAG AA 4.5:1) for the theme's text on its surfaces.
    {
        auto luminance = [](juce::Colour c)
        {
            auto channel = [](float v) { return v <= .03928f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); };
            return .2126f * channel(c.getFloatRed()) + .7152f * channel(c.getFloatGreen()) + .0722f * channel(c.getFloatBlue());
        };
        auto ratio = [&](juce::Colour a, juce::Colour b)
        {
            const auto x = luminance(a), y = luminance(b);
            return (std::max(x, y) + .05f) / (std::min(x, y) + .05f);
        };
        auto &lf = getLookAndFeel();
        const std::vector<std::tuple<const char *, juce::Colour, juce::Colour>> pairs{
            {"label text on window", lf.findColour(juce::Label::textColourId), lf.findColour(juce::ResizableWindow::backgroundColourId)},
            {"button text on button", lf.findColour(juce::TextButton::textColourOffId), lf.findColour(juce::TextButton::buttonColourId)},
            {"text field", lf.findColour(juce::TextEditor::textColourId), lf.findColour(juce::TextEditor::backgroundColourId)},
            {"choice box", lf.findColour(juce::ComboBox::textColourId), lf.findColour(juce::ComboBox::backgroundColourId)},
            {"menu", lf.findColour(juce::PopupMenu::textColourId), lf.findColour(juce::PopupMenu::backgroundColourId)}};
        juce::StringArray low;
        for (const auto &[name, text, surface] : pairs)
            if (ratio(text, surface) < 4.5f)
                low.add(juce::String(name) + " " + juce::String(ratio(text, surface), 2));
        folder.getChildFile("accessibility-contrast.txt").replaceWithText(low.isEmpty() ? "all pairs >= 4.5:1\n" : low.joinIntoString("\n"));
        if (!low.isEmpty())
            return juce::Result::fail("accessibility: low contrast: " + low.joinIntoString(", "));
    }
    // Dialog keyboard use: Tab reaches the field, the choice and both buttons in reading order,
    // and a focused button draws a visible focus ring.
    {
        const auto before = controller.project.state();
        transposePattern();
        juce::Component::SafePointer<juce::AlertWindow> dialog = patternPropertiesWindow;
        if (!dialog)
            return juce::Result::fail("accessibility: Transpose dialog did not open");
        auto traverser = dialog->createKeyboardFocusTraverser();
        const auto order = traverser->getAllComponents(dialog.getComponent());
        juce::StringArray kinds;
        juce::Button *transpose = nullptr;
        for (auto *c : order)
        {
            if (auto *b = dynamic_cast<juce::Button *>(c))
            {
                kinds.add("button:" + b->getButtonText());
                if (b->getButtonText() == "Transpose")
                    transpose = b;
            }
            else
                kinds.add(dynamic_cast<juce::TextEditor *>(c) ? "field" : dynamic_cast<juce::ComboBox *>(c) ? "choice" : "other");
        }
        // Tab cycles; the dialog opens on its text field, so compare the cycle from the field.
        if (const auto start = kinds.indexOf("field"); start > 0)
        {
            juce::StringArray rotated;
            for (int i = 0; i < kinds.size(); ++i)
                rotated.add(kinds[(start + i) % kinds.size()]);
            kinds = rotated;
        }
        const auto expected = juce::StringArray{"field", "choice", "button:Transpose", "button:Cancel"};
        bool ordered = kinds.size() >= 4;
        for (int i = 0; ordered && i < 4; ++i)
            ordered = kinds[i] == expected[i];
        juce::String ringResult = "not checked";
        if (ordered && transpose && transpose->isShowing())
        {
            if (auto *field = dialog->getTextEditor("semitones"))
                field->grabKeyboardFocus(); // The plain image must not already carry the ring.
            const bool focusedBefore = transpose->hasKeyboardFocus(false);
            const auto plain = transpose->createComponentSnapshot(transpose->getLocalBounds());
            if (focusedBefore)
                return juce::Result::fail("accessibility: the button kept focus after Tab moved away");
            transpose->grabKeyboardFocus();
            if (transpose->hasKeyboardFocus(false))
            {
                const auto ring = transpose->createComponentSnapshot(transpose->getLocalBounds());
                int changed = 0;
                for (int y = 0; y < plain.getHeight(); ++y)
                    for (int x = 0; x < plain.getWidth(); ++x)
                        changed += plain.getPixelAt(x, y) != ring.getPixelAt(x, y);
                ringResult = juce::String(changed) + " pixels change with focus";
                if (changed < 20)
                    ordered = false;
            }
            else
                ringResult = "focus not granted (no active window); ring drawing not observed";
        }
        folder.getChildFile("accessibility-dialog.txt")
            .replaceWithText("Transpose dialog Tab order: " + kinds.joinIntoString(", ") + "\nfocus ring: " + ringResult + "\n");
        if (dialog)
            dialog->exitModalState(0);
        dispatchMessages(5);
        if (!ordered)
            return juce::Result::fail("accessibility: dialog Tab order/focus ring: " + kinds.joinIntoString(", ") + " | " + ringResult);
        if (!controller.project.state().isEquivalentTo(before))
            return juce::Result::fail("accessibility: cancelling the dialog changed the project");
    }
    if (editors.size() != 4 || named.size() < 40)
        return juce::Result::fail("accessibility: expected 4 editors and the toolbar controls");

    // Keyboard browsing (Tab / Shift+Tab): one note or clip at a time in time order, announced.
    auto project = Project::empty();
    const auto channel = project.channels[0].id, pattern = project.patterns[0].id;
    const Id late = project.allocate(), low = project.allocate(), high = project.allocate();
    project.patterns[0].notes = {{late, channel, 2, .5, 67, 1.f, false}, {high, channel, 1, .5, 64, .5f, false},
                                 {low, channel, 1, .5, 62, .5f, false}};
    const Id clipA = project.allocate(), clipB = project.allocate();
    project.placements = {{clipB, pattern, 1, 4, 4, 0}, {clipA, pattern, 0, 4, 4, 0}};
    controller.replace(project);
    selectedPattern = pattern;
    selectedChannel = channel;
    focus = Scope::piano;
    selectItems(false, {});
    juce::StringArray order;
    for (int i = 0; i < 3; ++i)
    {
        if (!performAction("selection.next") || selectedNotes.size() != 1)
            return juce::Result::fail("accessibility: Tab should select one note at a time");
        order.add(juce::String(selectedNotes[0]) + ":" + status);
    }
    if (selectedNotes[0] != late || !order[0].startsWith(juce::String(low) + ":D5, bar 1 beat 2") ||
        !order[1].startsWith(juce::String(high) + ":E5") || !status.contains("3 of 3"))
        return juce::Result::fail("accessibility: notes in time then pitch order: " + order.joinIntoString(" | "));
    if (performAction("selection.next") || selectedNotes[0] != late || status != "Last item")
        return juce::Result::fail("accessibility: Tab past the last note should stay and say so");
    if (!performAction("selection.previous") || selectedNotes[0] != high)
        return juce::Result::fail("accessibility: Shift+Tab should go back");
    focus = Scope::playlist;
    selectItems(true, {});
    if (!performAction("selection.next") || selectedClips != std::vector<Id>{clipA} || !status.contains("track 1") ||
        !performAction("selection.next") || selectedClips != std::vector<Id>{clipB} || !status.contains("track 2"))
        return juce::Result::fail("accessibility: Playlist clips by time then track: " + status);
    const auto defaults = Customization::familiarDefaults();
    if (defaults.resolve(juce::KeyPress(juce::KeyPress::tabKey), Scope::piano) != "selection.next" ||
        defaults.resolve(juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0), Scope::playlist) !=
            "selection.previous")
        return juce::Result::fail("accessibility: Tab should be the default for selection.next in the Piano Roll");

    // A 3/4 change at bar 3 (beat 8): the ruler numbers bars 3.. in 3/4 and labels the change.
    {
        CommandArgs meter;
        meter.beat = 8;
        meter.numerator = 3;
        meter.denominator = 4;
        command("meter.change.add", meter);
        if (controller.project.meterChanges.size() != 1)
            return juce::Result::fail("accessibility: meter change was not added");
    }
    // Playlist track group: tracks 2-3 grouped under track 1, shown open, then collapsed.
    for (const auto &[name, lane, enabled] : std::vector<std::tuple<const char *, int, bool>>{
             {"track.group", 1, true}, {"track.group", 2, true}, {"track.rename", 0, true}})
    {
        CommandArgs a;
        a.lane = lane;
        a.enabled = enabled;
        a.text = "Drums group";
        command(name, a);
    }
    showEditor(2);
    auto capture = [&](const char *file)
    {
        if (auto *playlist = windows[2] ? windows[2]->getContentComponent() : nullptr)
        {
            juce::PNGImageFormat png;
            juce::FileOutputStream shot(folder.getChildFile(file));
            if (shot.openedOk())
            {
                shot.setPosition(0);
                shot.truncate();
                png.writeImageToStream(playlist->createComponentSnapshot(playlist->getLocalBounds()), shot);
            }
        }
    };
    capture("playlist-group-open.png");
    CommandArgs collapse;
    collapse.lane = 0;
    collapse.enabled = true;
    command("track.collapse", collapse);
    capture("playlist-group-collapsed.png");
    if (controller.project.visiblePlaylistLanes().size() != 126)
        return juce::Result::fail("accessibility: collapsing the group should hide two tracks");

    // Scale/maqam guide capture (Rast on C) for visual inspection.
    pianoGuide = {"rast", 0, true};
    showEditor(1);
    if (auto *piano = windows[1] ? windows[1]->getContentComponent() : nullptr)
    {
        piano->repaint();
        juce::PNGImageFormat png;
        juce::FileOutputStream shot(folder.getChildFile("piano-maqam-guide.png"));
        if (shot.openedOk())
        {
            shot.setPosition(0);
            shot.truncate();
            png.writeImageToStream(piano->createComponentSnapshot(piano->getLocalBounds()), shot);
        }
    }
    pianoGuide = {};
    return juce::Result::ok();
}
} // namespace vibera
namespace vibera
{
// Performance mode on the virtual device: launches land on bar boundaries, repeat until
// stopped, and stopping the transport ends the performance.
juce::Result Studio::verifyPerformance(const juce::File &folder)
{
    auto fail = [&](const juce::String &message) { return juce::Result::fail("performance: " + message); };
    auto &manager = audio.engine.getDeviceManager();
    manager.deviceManager.addAudioDeviceType(std::make_unique<LoopbackType>());
    juce::XmlElement setup("DEVICESETUP");
    setup.setAttribute("deviceType", "VibeRa loopback");
    setup.setAttribute("audioOutputDeviceName", "Loopback");
    setup.setAttribute("audioInputDeviceName", "Loopback");
    setup.setAttribute("audioDeviceRate", LoopbackDevice::rate);
    setup.setAttribute("audioDeviceBufferSize", LoopbackDevice::block);
    audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
    manager.initialise(0, 2);
    dispatchMessages(20);
    auto *device = dynamic_cast<LoopbackDevice *>(manager.deviceManager.getCurrentAudioDevice());
    if (!device)
        return fail("virtual device did not open");
    auto project = Project::empty();
    project.tempo = 120;
    project.patterns[0].length = 4;
    const auto a = project.patterns[0].id, b = project.allocate();
    project.patterns.push_back({b, "Pattern 2", 4, {}});
    controller.replace(project);
    patternMode = false;
    mode.setButtonText("Song");
    refresh();
    if (auto result = audio.rebuild(controller.project, false, selectedPattern); result.failed())
        return result;
    needsRebuild = false;
    audio.seekBeat(0);
    if (auto r = launchPattern(a, true); r.failed())
        return fail(r.getErrorMessage());
    if (!audio.playing())
        return fail("launching did not start playback");
    auto runUntil = [&](double beat)
    {
        for (int i = 0; i < 5000 && audio.positionBeat() < beat; ++i)
        {
            device->process();
            if (i % 4 == 0)
            {
                dispatchMessages(1);
                pollPerformance();
            }
        }
    };
    runUntil(5.2);
    if (auto r = launchPattern(b, true); r.failed())
        return fail(r.getErrorMessage());
    runUntil(9.5);
    launchPattern(a, false);
    runUntil(13.5);
    std::vector<std::pair<Id, double>> placed;
    for (const auto &clip : controller.project.placements)
        placed.push_back({clip.pattern, clip.beat});
    auto has = [&](Id pattern, double beat)
    { return std::find(placed.begin(), placed.end(), std::pair{pattern, beat}) != placed.end(); };
    juce::String report;
    for (const auto &[pattern, beat] : placed)
        report << (pattern == a ? "A@" : "B@") << beat << " ";
    folder.getChildFile("performance.txt").replaceWithText(report);
    if (!has(a, 0) || !has(a, 4) || !has(a, 8) || has(a, 12))
        return fail("Pattern A should repeat at 0, 4, 8 and stop before 12: " + report);
    if (has(b, 4) || !has(b, 8) || !has(b, 12))
        return fail("Pattern B launched mid-bar should start at the next bar (8) and repeat: " + report);
    for (const auto &clip : controller.project.placements)
        if (clip.lane < performance::firstLane)
            return fail("launched clips must use the performance lanes");
    stop.onClick();
    pollPerformance();
    if (!performancePads.empty())
        return fail("stopping the transport must end the performance");

    // Per-Pattern launch quantum: one Undo step, saved with the project.
    const auto before = controller.project.state();
    CommandArgs quantum;
    quantum.pattern = b;
    quantum.value = 0;
    command("pattern.launchQuantum", quantum);
    if (controller.project.pattern(b)->launchQuantum != 0)
        return fail("pattern.launchQuantum did not set the quantum");
    quantum.value = 100;
    command("pattern.launchQuantum", quantum);
    if (controller.project.pattern(b)->launchQuantum != 0)
        return fail("an out-of-range quantum must be refused");
    Project saved;
    if (auto r = Project::parse(controller.project.state(), saved); r.failed() ||
        saved.pattern(b)->launchQuantum != 0 || saved.pattern(a)->launchQuantum != -1)
        return fail("the launch quantum must survive save/load");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(before))
        return fail("Undo should restore the previous quantum");
    controller.redoEdit();

    // Pad 2 (the MIDI-learnable performance.pad2 action) launches Pattern B immediately.
    CommandArgs remove;
    for (const auto &clip : controller.project.placements)
        remove.ids.push_back(clip.id);
    command("clip.delete", remove);
    audio.seekBeat(0);
    play.onClick();
    runUntil(2.3);
    const double pressed = audio.positionBeat();
    if (!performAction("performance.pad2") || !performancePads.count(b))
        return fail("performance.pad2 did not launch the second Pattern");
    runUntil(3.5);
    const auto immediate = std::find_if(controller.project.placements.begin(), controller.project.placements.end(),
                                        [&](const auto &clip) { return clip.pattern == b; });
    if (immediate == controller.project.placements.end() || std::abs(immediate->beat - pressed) > .05)
        return fail("a quantum of 0 should launch where the pad was pressed (" + juce::String(pressed) + ")");
    if (!performAction("performance.pad2") || performancePads.count(b))
        return fail("pressing pad 2 again should stop the Pattern");
    if (performAction("performance.pad9"))
        return fail("a pad without a Pattern must refuse");
    if (!performAction("performance.pad1") || !performAction("performance.pad2") || performancePads.size() != 2 ||
        !performAction("performance.stopAll") || !performancePads.empty() || !audio.playing())
        return fail("performance.stopAll should stop every launched Pattern and keep playing");
    if (performAction("performance.stopAll"))
        return fail("stopAll with nothing launched should report it");

    // Scenes: save the launched Patterns, launch them together; another scene stops the rest.
    {
        CommandArgs both;
        both.value = 3;
        both.ids = {a, b};
        command("performance.saveScene", both);
        CommandArgs onlyA;
        onlyA.value = 4;
        onlyA.ids = {a};
        command("performance.saveScene", onlyA);
        if (controller.project.performanceScenes[2].size() != 2 || controller.project.performanceScenes[3].size() != 1)
            return fail("performance.saveScene did not store the scenes");
        controller.undoEdit();
        if (!controller.project.performanceScenes[3].empty() || controller.project.performanceScenes[2].size() != 2)
            return fail("saving a scene should be one Undo step");
        controller.redoEdit();
        if (!performAction("performance.scene3") || performancePads.size() != 2)
            return fail("scene 3 should launch both Patterns");
        if (!performAction("performance.scene4") || performancePads.size() != 1 || !performancePads.count(a))
            return fail("scene 4 should keep Pattern A and stop Pattern B");
        if (performAction("performance.scene7"))
            return fail("an empty scene must refuse");
        Project scenesSaved;
        if (Project::parse(controller.project.state(), scenesSaved).failed() ||
            scenesSaved.performanceScenes[2] != std::vector<Id>{a, b})
            return fail("scenes must survive save/load");
        auto older = controller.project.state();
        older.setProperty("version", 51, nullptr);
        if (Project::parse(older, scenesSaved).wasOk())
            return fail("scenes require project version 52");
        performAction("performance.stopAll");
    }
    stop.onClick();
    pollPerformance();

    // The same through MCP: set the quantum, refuse a bad one, read the pad layout back.
    // Refusals throw out of mcpCall (the protocol layer turns them into isError).
    juce::String refusal;
    auto call = [&](const char *tool, const juce::String &json)
    {
        try
        {
            return mcpCall(tool, juce::JSON::parse(json));
        }
        catch (const std::exception &e)
        {
            refusal = e.what();
            return juce::JSON::parse(R"({"isError":true})");
        }
    };
    if (bool(call("vibera_command", R"({"command":"pattern.launchQuantum","args":{"pattern":)" +
                                        juce::String(a) + R"(,"value":1}})")["isError"]) ||
        controller.project.pattern(a)->launchQuantum != 1)
        return fail("MCP pattern.launchQuantum did not set the quantum");
    if (!bool(call("vibera_command", R"({"command":"pattern.launchQuantum","args":{"pattern":)" +
                                         juce::String(a) + R"(,"value":0.01}})")["isError"]))
        return fail("MCP must refuse a quantum below 1/16 beat");
    const auto state = call("vibera_project", R"({"action":"perform"})")["structuredContent"];
    const auto pads = state["pads"];
    if (!pads.isArray() || pads.size() != 2 || juce::int64(pads[0]["pattern"]) != a ||
        double(pads[0]["quantum"]) != 1 || int(pads[1]["pad"]) != 2)
        return fail("perform state should list pad, Pattern and quantum: " + juce::JSON::toString(state, true));

    // Song looping follows the Playlist time selection (MCP vibera_transport loop range).
    if (auto result = audio.rebuild(controller.project, false, selectedPattern); result.failed())
        return result;
    needsRebuild = false;
    call("vibera_transport", R"({"action":"loop","enabled":true,"range":[4,8]})");
    if (!audio.looping() || !playlistRange || playlistRange->first != 4 || playlistRange->second != 8)
        return fail("vibera_transport loop range did not set the time selection: " + refusal);
    const auto loop = audio.loopSeconds();
    if (!loop || std::abs(loop->first - 2) > 1.e-6 || std::abs(loop->second - 4) > 1.e-6)
        return fail("the Song loop should cover the selection (2..4 s at 120 BPM)");
    audio.seekBeat(4);
    play.onClick();
    double lowest = 1.e9, highest = -1;
    for (int i = 0; i < int(6 * LoopbackDevice::rate / LoopbackDevice::block); ++i) // 6 s = 12 beats
    {
        device->process();
        if (i % 4 == 0)
        {
            dispatchMessages(1);
            lowest = std::min(lowest, audio.positionBeat());
            highest = std::max(highest, audio.positionBeat());
        }
    }
    stop.onClick();
    if (lowest < 4 - 1.e-6 || highest > 8 + .05)
        return fail("playback left the looped selection: " + juce::String(lowest) + ".." + juce::String(highest));
    const auto readback = call("vibera_state", "{}")["structuredContent"]["audio"]["loopBounds"]["timeSelection"];
    if (!readback.isArray() || double(readback[0]) != 4 || double(readback[1]) != 8)
        return fail("loopBounds.timeSelection should read [4, 8]");
    call("vibera_transport", R"({"action":"loop","range":null})");
    const auto whole = audio.loopSeconds();
    if (playlistRange || !whole || whole->first != 0 || whole->second <= 0)
        return fail("clearing the selection should loop the whole Song again");
    call("vibera_transport", R"({"action":"loop","enabled":false})");

    // Marker navigation and the section between markers (MCP application actions).
    for (const double beat : {12., 4.})
    {
        CommandArgs marker;
        marker.beat = beat;
        marker.text = "M" + juce::String(beat);
        command("marker.add", marker);
    }
    audio.seekBeat(6);
    call("vibera_command", R"({"command":"marker.next"})");
    if (std::abs(audio.positionBeat() - 12) > 1.e-6)
        return fail("marker.next from beat 6 should jump to 12: " + juce::String(audio.positionBeat()));
    if (!performAction("marker.previous") || std::abs(audio.positionBeat() - 4) > 1.e-6)
        return fail("marker.previous from 12 should jump to 4");
    if (performAction("marker.previous") || std::abs(audio.positionBeat() - 4) > 1.e-6)
        return fail("marker.previous before the first marker must refuse and stay");
    audio.seekBeat(6);
    if (!performAction("marker.selectSection") || !playlistRange || playlistRange->first != 4 ||
        playlistRange->second != 12 || audio.songLoopBeats() != std::optional{std::pair{4., 12.}})
        return fail("selectSection at beat 6 should select and loop 4..12");
    audio.seekBeat(2);
    if (!performAction("marker.selectSection") || playlistRange->first != 0 || playlistRange->second != 4)
        return fail("selectSection before the first marker should select 0..4");
    playlistRange.reset();
    syncSongLoop();

    // Align an Audio Clip's first transient to the grid (MCP alignClip, one clip.move step).
    {
        const auto hitFile = folder.getChildFile("align-hit.wav");
        hitFile.deleteFile();
        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(hitFile.createOutputStream().release(), 48000, 1, 24, {}, 0));
            juce::AudioBuffer<float> pcm(1, 48000);
            pcm.clear();
            for (int i = 0; i < 2000; ++i) // A decaying hit 0.13 s into the file.
                pcm.setSample(0, 6240 + i, .8f * std::exp(-i / 400.f) * (i % 20 < 10 ? 1.f : -1.f));
            if (!writer || !writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()))
                return fail("align fixture");
        }
        CommandArgs place;
        place.text = hitFile.getFullPathName();
        place.length = 1;
        place.beat = 1;
        place.lane = 3;
        command("audio.add", place);
        const auto clip = controller.project.audioPlacements.back().id;
        const auto beforeAlign = controller.project.state();
        auto response = call("vibera_project", R"({"action":"alignClip","clip":)" + juce::String(clip) + "}")["structuredContent"];
        const auto &placed = controller.project.audioPlacements.back();
        // Hit at 1 + 0.13 s * 2 beats/s = 1.26 beats -> nearest 1/16 step 1.25: move -0.01 beats.
        if (!bool(response["changed"]) || std::abs(placed.beat - .99) > .002)
            return fail("alignClip should move the clip by -0.01 beats: beat " + juce::String(placed.beat, 4) + " " +
                        juce::JSON::toString(response) + " " + refusal);
        controller.undoEdit();
        if (!controller.project.state().isEquivalentTo(beforeAlign))
            return fail("alignClip should be one Undo step");
        response = call("vibera_project", R"({"action":"alignClip","clip":)" + juce::String(clip) + R"(,"beats":1})")["structuredContent"];
        if (std::abs(controller.project.audioPlacements.back().beat - .74) > .002)
            return fail("a one-beat grid moves the hit to beat 1: " + juce::String(controller.project.audioPlacements.back().beat, 4));
        if (!bool(call("vibera_project", R"({"action":"alignClip","clip":999999})")["isError"]))
            return fail("alignClip must refuse a missing clip");
    }
    return juce::Result::ok();
}
} // namespace vibera
