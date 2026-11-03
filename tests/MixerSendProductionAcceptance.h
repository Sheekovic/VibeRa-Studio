// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/MixerSendAutomation.h"
// Actual AudioEngine-built AuxSend/MasterGate, independent literal gain/PCM
// oracle. Existing private borrowed-key/native-clock experiment remains intact.
namespace vibera::test::mixer_send_production
{
using namespace mixer_stereo_native;
inline void leaseTests()
{
    auto project = Project::empty();
    project.mixer[1].routes = {{2, .25f}};
    AutomationSource source;
    source.id = project.allocate();
    source.length = 1;
    source.points = {{project.allocate(), 0, .125f}, {project.allocate(), 1, .125f}};
    source.links = {{{AutomationParameter::mixerSend, 1, 2}, 0, 2}};
    project.automation.sources.push_back(source);
    project.automation.placements.push_back({project.allocate(), source.id, 0, 1, 0, 0, false});
    auto timeline = std::make_shared<LiveTimeline>();
    std::uint64_t generation = 0;
    const auto publish = [&]
    {
        auto next = compileLiveDocument(project, false, project.patterns.front().id, ++generation);
        require(next && timeline->publish(next),
                "Inactive send shells never exhaust the three document slots");
    };
    publish();
    std::array<MixerSendAutomation, 3> inactive;
    for (auto &shell : inactive)
    {
        shell.prepare(timeline, 1, 2, 48000);
        publish(); // Each unprocessed shell would have pinned a different old slot.
    }
    const auto held = inactive.front().read(0, 1. / 48000, 256, true);
    require(held.valid && held.curve && held.curve->valueAt(0) == .25f,
            "Actual processing Block pins the original immutable curve");
    for (int i = 0; i < 32; ++i)
    {
        const float value = float(i + 1) / 64;
        for (auto &point : project.automation.sources.front().points)
            point.value = value;
        publish();
        require(held.curve->valueAt(0) == .25f,
                "Publishing never frees a curve still borrowed by a processing Block");
        bool clean = false;
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            const auto current = inactive.back().read(0, 1. / 48000, 256, true);
            clean = current.valid && current.curve && current.curve->valueAt(0) == 2 * value;
        }
        require(
            clean && callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
            "Block acquires/releases only counters and reads current values without DSP heap/destruction");
    }
}
__declspec(noinline) inline void mode(const juce::File &output, const juce::File &sample, bool patternMode,
                                      int destination)
{
    constexpr int loopFrames = 1920;
    Id endpoint = 0, source = 0;
    auto fixture = std::make_unique<routing_lifetime::Fixture>(
        output, false, patternMode,
        [&](Project &p)
        {
            p.tempo = 375;
            p.patterns.front().length = p.placements.front().length = .25;
            auto &channel = p.channels.front();
            channel.source = sample.getFullPathName();
            channel.sampler = {};
            channel.sampler.root = 60;
            channel.sampler.loop = true;
            channel.sampler.envelope = channel.sampler.normalize = false;
            p.mixer[1].routes = {{destination, .25f}};
            AutomationSource lane;
            source = lane.id = p.allocate();
            lane.length = .25;
            endpoint = p.allocate();
            lane.points = {{p.allocate(), 0, 0}, {endpoint, .25, 1}};
            const AutomationKey key{AutomationParameter::mixerSend, 1, std::uint32_t(destination)};
            lane.links = {{key, 0, 2}};
            p.automation.initialValues.push_back({key, 0});
            p.automation.placements.push_back({p.allocate(), source, 0, .25, 0, 0, false});
            p.automation.sources.push_back(std::move(lane));
        },
        false, 256);
    auto &f = *fixture;
    auto &project = f.controller.project;
    f.audio->setLooping(true);
    auto before = f.audio->projectionDiagnostics();
    const auto initialisations = f.audio->mixerGraphInitialisations();
    const int starts = f.device->starts, stops = f.device->stops;
    juce::AudioBuffer<float> pcm(2, quantum), actual(2, capacity), expected(2, capacity);
    actual.clear();
    expected.clear();
    VoiceOracle voice;
    int frames = 0;
    double top = 1, manual = .25, manualStart = .25, manualGoal = .25;
    int ramp = 240;
    bool linked = true, initialized = true;
    double maximumError = 0, maximumMs = 0;
    juce::String trace = "phase,frame,error,ms,new,delete\n";
    const auto drive = [&](const char *phase)
    {
        require(frames + quantum <= capacity, "Bounded actual send PCM storage");
        const auto added = callbackAllocations.load(), deleted = callbackDestructions.load();
        const auto start = juce::Time::getHighResolutionTicks();
        {
            const juce::ScopedValueSetter<bool> probe(callbackAllocationProbe, true);
            f.device->process(pcm);
        }
        const auto ms = SyntheticCallbackTiming::milliseconds(start, juce::Time::getHighResolutionTicks());
        double error = 0;
        for (int n = 0; n < quantum; ++n)
        {
            if (ramp < 240)
            {
                ++ramp;
                manual = ramp == 240 ? manualGoal : manualStart + (manualGoal - manualStart) * ramp / 240.;
            }
            const double fraction = double((frames + n) % loopFrames) / loopFrames;
            const double gain = patternMode || (!linked && !initialized) ? manual
                                : linked                                 ? 2 * top * fraction
                                                                         : 0;
            for (int channel = 0; channel < 2; ++channel)
            {
                const double wanted = bed[size_t(channel)] * voice.envelope(frames + n) * gain;
                const float value = pcm.getSample(channel, n);
                actual.setSample(channel, frames + n, value);
                expected.setSample(channel, frames + n, float(wanted));
                error = std::isfinite(value) ? std::max(error, std::abs(value - wanted)) : INFINITY;
            }
        }
        const auto now = f.audio->projectionDiagnostics();
        const auto allocations = callbackAllocations.load() - added,
                   destructions = callbackDestructions.load() - deleted;
        trace += juce::String(phase) + "," + juce::String(frames) + "," + juce::String(error, 12) + "," +
                 juce::String(ms, 6) + "," + juce::String(juce::uint64(allocations)) + "," +
                 juce::String(juce::uint64(destructions)) + "\n";
        frames += quantum;
        require(output.getChildFile("callbacks.csv").replaceWithText(trace),
                "Retain actual send callbacks before assertions");
        writeRaw(output.getChildFile("actual.raw"), actual, frames);
        writeRaw(output.getChildFile("expected.raw"), expected, frames);
        require(error < 2e-6 && ms <= 1000. * quantum / rate && allocations == 0 && destructions == 0 &&
                    now.edit == before.edit && now.playbackContext == before.playbackContext &&
                    now.replacements == before.replacements &&
                    now.channels.front().sample == before.channels.front().sample &&
                    now.channels.front().graphInitialisations ==
                        before.channels.front().graphInitialisations &&
                    f.audio->mixerGraphInitialisations() == initialisations && f.device->starts == starts &&
                    f.device->stops == stops && f.audio->playing(),
                "Actual send complete signed PCM, native loop clock, stable graph/device and unchanged "
                "callback budget");
        maximumError = std::max(maximumError, error);
        maximumMs = std::max(maximumMs, ms);
        dispatchMessages(3);
    };
    const auto publish = [&]
    {
        require(awaitProjection(*f.audio, project, patternMode, f.pattern).wasOk(),
                "Publish actual send lane edit");
    };
    for (int n = 0; n < 4; ++n)
        drive("initial-silence");
    voice.on = frames;
    f.audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
    for (int n = 0; n < 8; ++n)
        drive("held-across-loop");
    CommandArgs args;
    args.target = endpoint;
    args.beat = .25;
    args.value = .375;
    require(f.controller.execute("automation.point.move", args).wasOk(), "Canonical send endpoint edit");
    top = .375;
    publish();
    drive("endpoint-edit");
    require(f.controller.undoEditChecked().wasOk(), "Actual send endpoint Undo");
    top = 1;
    publish();
    drive("undo");
    require(f.controller.redoEditChecked().wasOk(), "Actual send endpoint Redo");
    top = .375;
    publish();
    drive("redo");
    CommandArgs level;
    level.mixer = 1;
    level.target = destination;
    level.value = .75;
    require(f.controller.execute("mixer.send", level).wasOk(),
            "Canonical same-edge manual change under lane");
    manualStart = manual;
    manualGoal = .75;
    ramp = 0;
    publish();
    for (int n = 0; n < 3; ++n)
        drive("manual-advances-under-lane");
    CommandArgs remove;
    remove.target = source;
    require(f.controller.execute("automation.source.delete", remove).wasOk(),
            "Delete source retains initialized send");
    linked = false;
    publish();
    drive("initialized-fallback");
    remove = {};
    remove.mixer = 1;
    remove.automationControl = MixerAutomationControl::send;
    remove.automationSendTarget = destination;
    require(f.controller.execute("automation.init.remove", remove).wasOk(),
            "Remove exact send initialization");
    initialized = false;
    publish();
    drive("manual-continuation");
    require(f.controller.undoEditChecked().wasOk() && f.controller.undoEditChecked().wasOk(),
            "Restore source/init through real history");
    linked = initialized = true;
    publish();
    for (int n = 0; n < 5; ++n)
        drive("restored-across-loop");
    voice.off = frames;
    f.audio->midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
    for (int n = 0; n < 4; ++n)
        drive("held-release");
    require(pcm.getMagnitude(0, 0, quantum) == 0 && pcm.getMagnitude(1, 0, quantum) == 0 &&
                f.audio->projectionDiagnostics().channels.front().heldInputKeys == 0,
            "Exact silence and no stuck note after send edits");
    require(output.getChildFile("passed.txt")
                .replaceWithText("PASS actual production send destination=" + juce::String(destination) +
                                 " pattern=" + juce::String(int(patternMode)) + " maxError=" +
                                 juce::String(maximumError, 12) + " maxMs=" + juce::String(maximumMs, 6) +
                                 "; no OS input/listening/hardware or native-plugin PDC claim\n"),
            "Retain actual send terminal result");
}
__declspec(noinline) inline void offline(const juce::File &folder, const juce::File &sample, int destination)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh actual send export scenario");
    auto project = Project::empty();
    project.tempo = 375;
    project.channels.resize(1);
    auto &channel = project.channels.front();
    channel.source = sample.getFullPathName();
    channel.mixer = 1;
    channel.gain = 1;
    channel.pan = 0;
    channel.sampler = {};
    channel.sampler.root = 60;
    channel.sampler.loop = true;
    for (auto &insert : project.mixer)
    {
        insert.gain = 1;
        insert.pan = 0;
    }
    project.mixer[1].routes = {{destination, .25f}};
    auto &pattern = project.patterns.front();
    pattern.length = .25;
    pattern.autoLength = false; // Literal 1920-frame offline send oracle.
    pattern.notes = {{project.allocate(), channel.id, 0, .25, 60, 1}};
    project.placements = {{project.allocate(), pattern.id, 0, 0, .25, 0}};
    AutomationSource source;
    source.id = project.allocate();
    source.length = .25;
    source.points = {{project.allocate(), 0, 0}, {project.allocate(), .25, 1}};
    const AutomationKey key{AutomationParameter::mixerSend, 1, std::uint32_t(destination)};
    source.links = {{key, 0, 2}};
    project.automation.sources.push_back(source);
    project.automation.initialValues.push_back({key, 0});
    project.automation.placements.push_back({project.allocate(), source.id, 0, .25, 0, 0, false});
    require(project.validate().wasOk(), "Actual export fixture uses a valid public send target");
    const auto file = folder.getChildFile("send-project.vibra");
    Project restored;
    require(saveProject(project, file).wasOk() && loadProject(file, restored).wasOk(),
            "Real disk save/reopen completes for public send model");
    auto collectedExpected = project;
    const auto collected = juce::File(restored.channels.front().source);
    require(collected.isAChildOf(folder) && collected.existsAsFile() &&
                juce::SHA256(collected) == juce::SHA256(sample),
            "Actual portable sample collection preserves exact original WAV bytes");
    collectedExpected.channels.front().source = collected.getFullPathName();
    require(restored.state().isEquivalentTo(collectedExpected.state()),
            "Collected-path normalization leaves every other project field and exact send state unchanged");
    // Unity AUTOMATION is the independent same-path source reference; a manual
    // AuxSend has a legitimate startup ramp, so it is not a unity PCM oracle.
    auto unity = restored;
    for (auto &point : unity.automation.sources.front().points)
        point.value = .5f;
    unity.automation.initialValues.front().value = 1;
    auto audio = std::make_unique<AudioEngine>(false, folder.getChildFile("guide.json"), 256);
    const auto actualFile = folder.getChildFile("send-ramp.wav"),
               unityFile = folder.getChildFile("send-unity.wav");
    require(audio->renderSync(restored, actualFile, rate, 24, quantum).wasOk() &&
                audio->renderSync(unity, unityFile, rate, 24, quantum).wasOk(),
            "Actual hosted-independent Song send exports complete");
    double actualRate = 0, unityRate = 0;
    const auto actual = readWav(actualFile, actualRate), reference = readWav(unityFile, unityRate);
    require(actualRate == rate && unityRate == rate && actual.getNumChannels() == 2 &&
                actual.getNumSamples() == reference.getNumSamples() && actual.getNumSamples() == 1920 &&
                actual.getMagnitude(0, actual.getNumSamples()) > .0001f,
            "Nonempty finite-shape stereo WAV exports");
    double error = 0;
    for (int channelIndex = 0; channelIndex < 2; ++channelIndex)
        for (int frame = 0; frame < actual.getNumSamples(); ++frame)
        {
            // Literal tempo375, quarter beat1920frames, source end holds2.
            const auto gain = std::min(2., 2. * frame / 1920.);
            const auto value = actual.getSample(channelIndex, frame);
            error = std::isfinite(value)
                        ? std::max(error, std::abs(value - reference.getSample(channelIndex, frame) * gain))
                        : INFINITY;
        }
    require(folder.getChildFile("export-result.txt")
                .replaceWithText("maxError=" + juce::String(error, 12) +
                                 " frames=" + juce::String(actual.getNumSamples()) +
                                 " actualSHA=" + juce::SHA256(actualFile).toHexString() +
                                 " unitySHA=" + juce::SHA256(unityFile).toHexString() + "\n"),
            "Retain exact export PCM comparison before assertion");
    require(error < 2e-6, "Every actual send export frame follows the independent literal musical-time ramp");
}
inline void tests(const juce::File &output)
{
    require(!output.exists() && output.createDirectory().wasOk(), "Fresh actual send automation attempt");
    leaseTests();
    const auto sample = writeSource(output);
    routing_lifetime::Storage storage(output.getChildFile("settings"));
    juce::String failures;
    for (int destination : {0, 2})
        for (bool pattern : {true, false})
            try
            {
                mode(output.getChildFile(juce::String(destination) + (pattern ? "-pattern" : "-song")),
                     sample, pattern, destination);
            }
            catch (const std::exception &error)
            {
                failures +=
                    juce::String(destination) + (pattern ? " Pattern: " : " Song: ") + error.what() + "\n";
            }
    for (int destination : {0, 2})
        try
        {
            offline(output.getChildFile(juce::String(destination) + "-offline"), sample, destination);
        }
        catch (const std::exception &error)
        {
            failures += juce::String(destination) + " offline: " + error.what() + "\n";
        }
    require(failures.isEmpty(), failures.toStdString());
}
} // namespace vibera::test::mixer_send_production
