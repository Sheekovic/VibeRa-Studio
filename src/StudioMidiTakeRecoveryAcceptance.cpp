// SPDX-License-Identifier: AGPL-3.0-or-later
// Original acceptance fixture. Compile only after the retained-take application
// and semantic adapters are integrated. No native OS input or physical device.
#include "Studio.h"
#include "Editors.h"
#include "McpSchema.h"
#include "../tests/SyntheticDevice.h"
#include <stdexcept>
#include <functional>
#include <algorithm>
#include <cmath>

namespace vibera
{
namespace
{
struct OriginalTakeBlocker final : juce::UndoableAction
{
    int &calls;
    explicit OriginalTakeBlocker(int &counter) : calls(counter) {}
    bool perform() override
    {
        ++calls;
        return true;
    }
    bool undo() override
    {
        ++calls;
        return false;
    }
    int getSizeInUnits() override
    {
        return 1;
    }
};
juce::var request(const char *action)
{
    auto *object = new juce::DynamicObject;
    object->setProperty("action", action);
    return object;
}
void property(juce::var &value, const char *key, juce::var item)
{
    value.getDynamicObject()->setProperty(key, item);
}
bool writeOriginalWav(const juce::File &file)
{
    if (file.exists())
        return false;
    auto stream = file.createOutputStream();
    if (!stream)
        return false;
    stream->setPosition(0);
    stream->truncate();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::AudioFormatWriter> writer(
        format.createWriterFor(stream.release(), 48000, 1, 16, {}, 0));
    if (!writer)
        return false;
    juce::AudioBuffer<float> pcm(1, 480);
    for (int i = 0; i < 480; ++i)
        pcm.setSample(0, i, .1f * std::sin(float(i) * .07f));
    return writer->writeFromAudioSampleBuffer(pcm, 0, 480);
}
} // namespace
juce::Result Studio::verifyMidiTakeRecovery(const juce::File &folder)
{
    if (!controllerTestMode || !bool(getProperties()["viberaMidiTakeRecoveryFixture"]) ||
        getProperties()["viberaMidiTakeRecoveryOutput"].toString() != folder.getFullPathName())
        return juce::Result::fail("MIDI recovery acceptance requires an explicitly owned disposable fixture");
    if (auto result = folder.createDirectory(); result.failed())
        return result;
    const bool wasTimerRunning = isTimerRunning();
    stopTimer();
    const auto normalObserver = controller.changed;
    struct Cleanup
    {
        std::function<void()> fn;
        ~Cleanup()
        {
            try
            {
                fn();
            }
            catch (...)
            {
            }
        }
    } cleanup{[&]
              {
                  controller.changed = normalObserver;
                  cancelPendingMidiTake();
                  audio.stop();
                  releaseTypingNotes();
                  if (wasTimerRunning)
                      startTimerHz(20);
              }};
    juce::StringArray passed;
    juce::String trace = "mode,onSeconds,offSeconds,originSeconds,expectedBeat,expectedLength,serial\n";
    auto check = [](bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    };
    auto completeCase = [&](const char *name) { passed.add(name); };
    auto receipt = [&](bool success, const juce::String &failure)
    {
        auto *object = new juce::DynamicObject;
        object->setProperty("passed", success);
        object->setProperty("failure", failure);
        object->setProperty("evidenceKind",
                            "Actual Studio/Controller/MCP + synthetic device and injected MIDI");
        object->setProperty("nativeOsInput", false);
        object->setProperty("physicalMidiOrAudio", false);
        object->setProperty("listening", false);
        juce::Array<juce::var> cases;
        for (const auto &name : passed)
            cases.add(name);
        object->setProperty("passedCases", cases);
        object->setProperty("take", midiTakeState());
        object->setProperty("appAllocationFaultInjection",
                            "NOT PERFORMED; core/primitive probes are separate");
        return folder.getChildFile("midi-take-studio-verification.json")
                   .replaceWithText(juce::JSON::toString(juce::var(object), true)) &&
               folder.getChildFile("midi-take-clock-trace.csv").replaceWithText(trace);
    };
    try
    {
        auto &manager = audio.engine.getDeviceManager();
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        dispatchMessages(20);
        auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
        check(device && device->getCurrentSampleRate() == 48000 &&
                  device->getCurrentBufferSizeSamples() == 256,
              "Fixture must use the original synthetic 48k/256 device");
        juce::AudioBuffer<float> block(2, 256);
        auto process = [&](int count)
        {
            float peak = 0;
            for (int i = 0; i < count; ++i)
            {
                device->process(block);
                peak = std::max(peak, block.getMagnitude(0, block.getNumSamples()));
                dispatchMessages(1);
            }
            return peak;
        };
        auto invoke = [&](const char *name, const juce::var &args) { return mcpCall(name, args); };
        auto data = [&](const char *name, const juce::var &args)
        {
            auto response = invoke(name, args);
            check(!bool(response["isError"]), "Unexpected semantic operation failure");
            return response["structuredContent"];
        };
        auto recording = [&](const char *action, std::uint64_t serial = 0)
        {
            auto args = request(action);
            if (serial)
                property(args, "serial", juce::int64(serial));
            return invoke("vibera_recording", args);
        };
        auto expectRecordingFailure = [&](const char *action, std::uint64_t serial, const char *code)
        {
            const auto response = recording(action, serial);
            check(bool(response["isError"]) &&
                      response["structuredContent"]["completionState"].toString() == "failed" &&
                      response["structuredContent"]["error"]["code"].toString() == code,
                  "Recording refusal must expose typed failure and actual retained state");
        };
        auto prepare = [&](bool song, bool chainEffects = false)
        {
            check(cancelPendingMidiTake().wasOk(), "Previous owned fixture take cancels");
            check(audioImports.idle() && !audioImportAfterTake,
                  "No unfinished import may be discarded by fixture reset");
            audio.stop();
            releaseTypingNotes();
            clearScoreLog();
            auto project = Project::empty();
            project.name = song ? "Original recovery Song fixture" : "Original recovery Pattern fixture";
            project.channels.front().source = "builtin:bass";
            project.channels.front().mixer = 1;
            const auto pattern = project.patterns.front().id;
            project.patterns.front().length = 4;
            project.patterns.push_back({project.allocate(), "Unselected original part", 4, {}});
            if (song)
                project.placements.push_back({project.allocate(), pattern, 0, 0, 4, 0});
            if (chainEffects)
            {
                // Original valid missing-plugin descriptors/state, NOT a hosting
                // fixture. Runtime plugin support is tested by other branches.
                juce::PluginDescription missing;
                missing.pluginFormatName = "VST3";
                missing.numInputChannels = missing.numOutputChannels = 2;
                for (int slot = 0; slot < 3; ++slot)
                {
                    const auto absent =
                        folder.getChildFile("original-take-chain-absent-" + juce::String(slot) + ".vst3");
                    check(!absent.exists(), "Take-chain fixture must be genuinely missing");
                    missing.name = "Original take placeholder " + juce::String(slot);
                    missing.fileOrIdentifier = absent.getFullPathName();
                    project.mixer[slot == 2 ? 2 : 1].effects.push_back(
                        {project.allocate(), missing.name, missing.createXml()->toString(),
                         slot % 2 ? "1.A." : "1...", false, slot == 1});
                }
                check(project.validate().wasOk(), "Genuine missing-effect project validates");
            }
            controller.replace(project);
            projectFile = juce::File{};
            selectedPattern = pattern;
            selectedChannel = project.channels.front().id;
            patternMode = !song;
            mode.setButtonText(song ? "Song" : "Pattern");
            quantizeRecordingStart = quantizeRecordingEnd = false;
            refresh();
            audio.invalidateProjection();
            auto prepared = audio.rebuild(controller.project, patternMode, selectedPattern);
            if (chainEffects)
            {
                // This optional fixture is the first acquisition with plugin
                // specs. Use real asynchronous completion, never treat admission
                // as a missing-plugin failure or retry an actual terminal error.
                const auto deadline = juce::Time::getMillisecondCounterHiRes() + 30000.;
                while (AudioEngine::preparationPending(prepared) &&
                       juce::Time::getMillisecondCounterHiRes() < deadline)
                {
                    dispatchMessages(5);
                    prepared = audio.rebuild(controller.project, patternMode, selectedPattern);
                }
                check(controller.project.state().isEquivalentTo(project.state()),
                      "Missing effect preparation preserves original canonical state");
                for (const auto &insert : project.mixer)
                    for (const auto &effect : insert.effects)
                        check(audio.pluginProjectionReady(controller.project, effect.id) &&
                                  !audio.parameterHost(effect.id) &&
                                  audio.pluginStatus(effect.id).isNotEmpty(),
                              "Real missing-effect projection retains recoverable placeholders and errors");
            }
            check(prepared.wasOk(), "Original take projection prepares");
            needsRebuild = false;
            projectionError.clear();
            engineRevision = controller.revision;
            audio.setMidiTarget(selectedChannel);
            audio.setMetronome(false);
            audio.setCountInBars(0);
            audio.seekBeat(0);
            recordArmed = false;
            process(8);
            return controller.project;
        };
        struct MusicalOracle
        {
            double beat, length;
            Id channel, pattern;
            int pitch;
            float velocity;
            double onSeconds, originSeconds;
            double releaseVelocity = 0;
        };
        auto captureClosedNote = [&](bool song, bool leaveHeld = false,
                                     const std::function<void()> &duringHeldNote = {}, int releaseByte = 0)
        {
            MidiClock::Position origin;
            check(audio.midiInput.recordingPosition(origin) && !origin.playing,
                  "Stopped target clock is available before actual arm/Play handlers");
            record.onClick();
            play.onClick();
            check(midiTaking && recordArmed && pendingMidiTake,
                  "Actual Record/Play must start owned capture");
            process(12);
            MidiClock::Position ready;
            for (int i = 0; i < 128 && (!audio.midiInput.recordingPosition(ready) || !ready.playing); ++i)
                process(1);
            check(audio.midiInput.recordingPosition(ready) && ready.playing,
                  "Playback capture clock genuinely running before note input");
            auto captured = [&](const juce::MidiMessage &message)
            {
                const auto before = audio.midiInput.capture.write.load();
                audio.midiInput.route(message, true, true);
                const auto after = audio.midiInput.capture.write.load();
                check(after == before + 1, "Real MidiInputRouter route publishes exactly one capture event");
                const auto event =
                    audio.midiInput.capture.queue[before % audio.midiInput.capture.queue.size()];
                drainMidiTake();
                return event;
            };
            const auto on = captured(juce::MidiMessage::noteOn(1, 66, juce::uint8(96)));
            if (duringHeldNote)
            {
                check(process(4) > 1.e-6f,
                      "Actual sampler consumes the original held NoteOn before chain-control interaction");
                duringHeldNote();
            }
            const auto heldPeak = process(24);
            auto off = on;
            if (!leaveHeld)
                off = captured(juce::MidiMessage::noteOff(1, 66, juce::uint8(releaseByte)));
            check(on.channel == selectedChannel && off.channel == on.channel && heldPeak > 1.e-6f &&
                      (leaveHeld || off.seconds > on.seconds) && !audio.midiInput.capture.failed.load(),
                  "Original real-route event provenance and actual nonzero synthetic PCM");
            // Independent oracle uses source event clocks, never captured/output
            // note values or root PCM phase calibration. No loop boundary here.
            const double beat = (on.seconds - origin.seconds) * 2;
            const double length = leaveHeld ? -1 : (off.seconds - on.seconds) * 2;
            check(beat >= 0 && beat < 4 && (leaveHeld || (beat + length < 4 && length > 1. / 960)),
                  "Fixture is a genuine nonwrapping note; held duration is determined only at actual Stop");
            trace += juce::String(song ? "Song" : "Pattern") + "," + juce::String(on.seconds, 15) + "," +
                     juce::String(off.seconds, 15) + "," + juce::String(origin.seconds, 15) + "," +
                     juce::String(beat, 15) + "," + juce::String(length, 15) + "," +
                     juce::String(juce::int64(midiTakeSerial())) + "\n";
            return MusicalOracle{beat,
                                 length,
                                 on.channel,
                                 selectedPattern,
                                 66,
                                 96.f / 127,
                                 on.seconds,
                                 origin.seconds,
                                 leaveHeld ? 0 : releaseByte / 127.};
        };
        auto checkNote = [&](const Note &note, const MusicalOracle &expected, bool allocated)
        {
            check((allocated ? note.id > 0 : note.id == 0) && note.channel == expected.channel &&
                      note.pitch == expected.pitch && std::abs(note.beat - expected.beat) < 1.e-9 &&
                      std::abs(note.length - expected.length) < 1.e-9 &&
                      std::abs(note.velocity - expected.velocity) < 1.e-6 &&
                      std::abs(note.releaseVelocity - expected.releaseVelocity) < 1.e-12 && !note.step &&
                      !note.muted,
                  "Recorded note must match independent source-clock/channel/velocity oracle exactly");
        };
        auto addBlocker = [&](int &calls)
        {
            controller.undo.beginNewTransaction("Original foreign capture-admission fixture");
            check(controller.undo.perform(new OriginalTakeBlocker(calls)) && calls == 1,
                  "Genuine actual-manager foreign action must perform once");
        };
        auto projectFailure = [&](const juce::var &args)
        {
            bool refused = false;
            try
            {
                const auto result = invoke("vibera_project", args);
                refused = bool(result["isError"]) &&
                          result["structuredContent"]["error"]["code"].toString() == "busy";
            }
            catch (const McpFailure &error)
            {
                refused = error.code == "busy";
            }
            check(refused, "Project operation must refuse a retained take before any file/model mutation");
        };
        auto job = [&](juce::var started)
        {
            auto current = started;
            const auto id = started["operation"].toString();
            const auto deadline = juce::Time::getMillisecondCounter() + 5000;
            while (current["completionState"].toString() == "pending" &&
                   juce::Time::getMillisecondCounter() < deadline)
            {
                timerCallback();
                process(1);
                auto query = request("job");
                property(query, "operation", id);
                current = data("vibera_project", query);
            }
            check(current["completionState"].toString() == "completed",
                  "Actual asynchronous project job completes");
            return current;
        };

        const auto wav = folder.getChildFile("original-recovery-import.wav");
        check(writeOriginalWav(wav), "Original mono PCM WAV fixture must be written to fresh destination");
        juce::MemoryBlock originalWavBytes;
        check(wav.loadFileAsData(originalWavBytes),
              "Original WAV byte identity retained before blocked export");
        {
            juce::WavAudioFormat format;
            auto input = wav.createInputStream();
            check(input != nullptr, "Original WAV input stream");
            std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(input.release(), true));
            check(reader && reader->lengthInSamples == 480 && reader->sampleRate == 48000 &&
                      reader->numChannels == 1,
                  "Import/export guard fixture must be genuine valid original mono PCM WAV");
        }
        // Armed-but-not-capturing Stop regression: no capture clock/take exists.
        prepare(false);
        record.onClick();
        check(recordArmed && !midiTaking, "Record arms while stopped");
        stop.onClick();
        check(!recordArmed && !midiTaking && !hasPendingMidiTake(), "Stop disarms idle armed recording");
        completeCase("actual-arm-stop-disarm");

        for (const bool song : {false, true})
        {
            const auto original = prepare(song);
            const auto inputFile =
                folder.getChildFile(song ? "guard-song-input.vibra" : "guard-pattern-input.vibra");
            check(!inputFile.exists() && saveProject(original, inputFile).wasOk(),
                  "Guard input is a genuine fresh Project");
            const auto originalBytes = inputFile.loadFileAsString();
            auto expected =
                captureClosedNote(song, song, {}, song ? 0 : 64); // Physical64 versus synthetic Stop0.
            const auto serial = midiTakeSerial();
            int foreignCalls = 0;
            addBlocker(foreignCalls);
            const auto starts = device->starts, stops = device->stops;
            stop.onClick();
            check(!audio.playing() && !recordArmed && !midiTaking && hasPendingMidiTake() &&
                      pendingMidiTake->phase == PendingMidiTake::Phase::ready &&
                      pendingMidiTake->args.recordedNotes.size() == 1 && foreignCalls == 1 &&
                      original.state().isEquivalentTo(controller.project.state()) &&
                      starts == device->starts && stops == device->stops,
                  "Stop must stop/disarm, retain exact take on genuine history refusal and preserve device");
            if (song)
            {
                expected.length = (pendingMidiTake->stopSeconds - expected.onSeconds) * 2;
                check(expected.length > 1. / 960 && expected.beat + expected.length < 4,
                      "Actual post-quiescence Stop cutoff forms a genuine nonwrapping held-note oracle");
                trace += "SongStop," + juce::String(expected.onSeconds, 15) + "," +
                         juce::String(pendingMidiTake->stopSeconds, 15) + "," +
                         juce::String(expected.originSeconds, 15) + "," + juce::String(expected.beat, 15) +
                         "," + juce::String(expected.length, 15) + "," + juce::String(juce::int64(serial)) +
                         "\n";
            }
            if (song)
            {
                // Release the software-injected physical lane after sealing;
                // monitoring remains distinct from an already stopped capture.
                audio.midiInput.route(juce::MidiMessage::noteOff(1, 66), true, true);
                process(2);
            }
            checkNote(pendingMidiTake->args.recordedNotes.front(), expected, false);
            const auto read = data("vibera_recording", request("state"));
            check(bool(read["recording"]["pending"]) && read["recording"]["phase"].toString() == "ready" &&
                      juce::int64(read["recording"]["serial"]) == juce::int64(serial) &&
                      int(read["notesTotal"]) == 1 && read["notes"][0].hasProperty("releaseVelocity") &&
                      read["notes"][0].hasProperty("releaseMidiVelocity") &&
                      std::abs(double(read["notes"][0]["releaseVelocity"]) - expected.releaseVelocity) <
                          1.e-12 &&
                      int(read["notes"][0]["releaseMidiVelocity"]) == (song ? 0 : 64),
                  "Actual semantic discovery returns retained source-target/token and complete notes");
            const auto failed = recording("retry", serial);
            check(bool(failed["isError"]) &&
                      failed["structuredContent"]["completionState"].toString() == "failed" &&
                      hasPendingMidiTake() && foreignCalls == 1,
                  "MCP retry must honestly fail without losing or executing foreign history");
            expectRecordingFailure("retry", serial + 1, "take.stale");
            expectRecordingFailure("cancel", serial + 1, "take.stale");
            // Actual UI handlers must share the retained-take transport refusal
            // already required by the semantic adapters. No OS input is sent.
            const auto uiProject = controller.project.state();
            const auto uiIdentity = controller.projectIdentity;
            const auto uiRevision = controller.revision;
            const auto uiPattern = selectedPattern;
            const auto uiChannel = selectedChannel;
            const auto uiUndo = controller.undo.getUndoDescriptions().joinIntoString("|");
            const auto uiRedo = controller.undo.getRedoDescriptions().joinIntoString("|");
            const auto uiStopSeconds = pendingMidiTake->stopSeconds;
            const auto uiPosition = audio.positionBeat();
            const auto uiLoop = audio.looping();
            check(!needsRebuild && audio.projectionDiagnostics().edit &&
                      controller.project.patterns.size() > 1 &&
                      uiPattern != controller.project.patterns.back().id,
                  "Retained UI refusal fixture requires a valid prepared graph and distinct last Pattern");
            auto unchangedUiTake = [&]
            {
                check(!audio.playing() && audio.positionBeat() == uiPosition && audio.looping() == uiLoop &&
                          uiIdentity == controller.projectIdentity && uiRevision == controller.revision &&
                          selectedPattern == uiPattern && selectedChannel == uiChannel &&
                          uiProject.isEquivalentTo(controller.project.state()) && hasPendingMidiTake() &&
                          pendingMidiTake->serial == serial &&
                          pendingMidiTake->stopSeconds == uiStopSeconds &&
                          pendingMidiTake->args.recordedNotes.size() == 1 && foreignCalls == 1 &&
                          uiUndo == controller.undo.getUndoDescriptions().joinIntoString("|") &&
                          uiRedo == controller.undo.getRedoDescriptions().joinIntoString("|") &&
                          starts == device->starts && stops == device->stops,
                      "Refused UI handler must preserve exact take/project/target/history/transport/device "
                      "state");
                checkNote(pendingMidiTake->args.recordedNotes.front(), expected, false);
            };
            play.onClick();
            check(status.containsIgnoreCase("retained MIDI take"),
                  "Actual Play button must explain its retained-take refusal");
            unchangedUiTake();
            check(!performAction("transport.loop"),
                  "Actual Loop action must refuse a retained take independently of graph readiness");
            unchangedUiTake();
            newPattern.onClick();
            check(status.startsWith("history.unsupportedTransaction:"),
                  "Actual new Pattern button must stop after failed take admission");
            unchangedUiTake();
            auto *piano = static_cast<Editor *>(windows[1]->getContentComponent());
            const auto viewport = piano->pianoNoteViewport();
            const juce::Point<float> ruler{float(viewport.getX() + std::min(200, viewport.getWidth() / 2)),
                                           float(viewport.getY() - 10)};
            check(viewport.getWidth() > 100 && ruler.x > 78 && ruler.y >= 28 &&
                      piano->beatUnder(ruler.x) != uiPosition,
                  "Ruler fixture must genuinely target a different beat outside the snap-menu region");
            const auto now = juce::Time::getCurrentTime();
            piano->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), ruler,
                                              juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1,
                                              0, 0, 0, 0, piano, piano, now, ruler, now, 1, false));
            check(status.startsWith("history.unsupportedTransaction:"),
                  "Actual ruler seek must report failed take admission before moving transport");
            unchangedUiTake();
            completeCase(song ? "actual-retained-Song-Play-Loop-newPattern-ruler-refusals"
                              : "actual-retained-Pattern-Play-Loop-newPattern-ruler-refusals");
            for (const char *action : {"save", "open", "export", "new"})
            {
                auto args = request(action);
                if (juce::String(action) == "new" || juce::String(action) == "open")
                    property(args, "discardChanges", true);
                if (juce::String(action) != "new")
                    property(args, "path",
                             juce::String(action) == "export" ? wav.getFullPathName()
                                                              : inputFile.getFullPathName());
                if (juce::String(action) == "save" || juce::String(action) == "export")
                    property(args, "overwrite", true);
                projectFailure(args);
            }
            bool saveCallback = false;
            saveTo(inputFile, [&](juce::Result result) { saveCallback = result.failed(); });
            check(saveCallback && !afterPluginState && openProjectFile(inputFile).failed(),
                  "Actual UI save/open preserve pending take");
            const auto identity = controller.projectIdentity;
            exportTo(wav);
            newProject();
            bool closed = false;
            requestClose([&] { closed = true; });
            juce::MemoryBlock currentWavBytes;
            check(!closed && identity == controller.projectIdentity && hasPendingMidiTake() &&
                      inputFile.loadFileAsString() == originalBytes && wav.loadFileAsData(currentWavBytes) &&
                      currentWavBytes == originalWavBytes &&
                      original.state().isEquivalentTo(controller.project.state()),
                  "Pending take is unsaved work even when Project is clean; no close/new/overwrite");
            // EXPLICIT TEST-ONLY blocker removal, not an application recovery strategy.
            controller.undo.clearUndoHistory();
            const auto applied = recording("retry", serial);
            check(!bool(applied["isError"]) &&
                      applied["structuredContent"]["completionState"].toString() == "completed" &&
                      applied["structuredContent"]["recording"]["phase"].toString() == "completed" &&
                      !hasPendingMidiTake(),
                  "Actual MCP retry completes only after the exact canonical commit");
            const auto *pattern = controller.project.pattern(expected.pattern);
            check(pattern && pattern->notes.size() == 1,
                  "Retry creates exactly one note in original Pattern");
            checkNote(pattern->notes.front(), expected, true);
            const auto recorded = controller.project.state();
            auto undoArgs = request("unused");
            undoArgs.getDynamicObject()->removeProperty("action");
            property(undoArgs, "command", "edit.undo");
            data("vibera_command", undoArgs);
            check(original.state().isEquivalentTo(controller.project.state()) && !controller.undo.canUndo(),
                  "One semantic Undo restores exact pre-take Project/allocator/placement");
            property(undoArgs, "command", "edit.redo");
            data("vibera_command", undoArgs);
            check(recorded.isEquivalentTo(controller.project.state()),
                  "One semantic Redo restores exact recorded destination");
            expectRecordingFailure("retry", serial, "take.stale");
            const auto savedFile =
                folder.getChildFile(song ? "recorded-song.vibra" : "recorded-pattern.vibra");
            check(!savedFile.exists(), "Disposable save destination must be fresh");
            auto save = request("save");
            property(save, "path", savedFile.getFullPathName());
            job(data("vibera_project", save));
            Project disk;
            check(loadProject(savedFile, disk).wasOk() && recorded.isEquivalentTo(disk.state()),
                  "Actual saved Project includes recovered notes");
            auto open = request("open");
            property(open, "path", savedFile.getFullPathName());
            property(open, "discardChanges", true);
            job(data("vibera_project", open));
            check(recorded.isEquivalentTo(controller.project.state()),
                  "Actual open job restores recovered notes/IDs");
            completeCase(song ? "actual-song-refusal-retry-history-save-open"
                              : "actual-pattern-refusal-retry-history-save-open");
        }

        // Actual active capture and genuine failed-take integration with the new
        // chain controls. Existing acquisition/recovery oracles above are kept.
        // Missing effects below prove model/UI/MCP ownership, not plugin hosting
        // or audible wet/dry DSP. Synthetic callbacks are explicitly controlled.
        for (const bool song : {false, true})
        {
            const auto original = prepare(song, true);
            setSize(1920, 1044);
            check(selectMixerInsert(1).wasOk(), "Select original take-chain fixture insert");
            if (maximisedEditor != 3)
                maximiseEditor(3);
            auto *mixer = static_cast<Editor *>(windows[3]->getContentComponent());
            check(mixer->setEffectSlotsOffset(0).wasOk(), "Take-chain original first slot visible");
            const auto layout = mixer->effectSlotsLayout();
            const auto knob = layout.wet(0).getCentre().toFloat();
            const auto first = original.mixer[1].effects[0].id;
            const auto foreign = original.mixer[2].effects[0].id;
            check(layout.wet(0).getWidth() >= 24 && layout.chainBypass.getWidth() >= 24,
                  "Take-chain fixture requires practical actual layout targets");
            auto event = [&](juce::Point<float> point)
            {
                const auto now = juce::Time::getCurrentTime();
                return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                        juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0,
                                        0, 0, mixer, mixer, now, knob, now, 1, true);
            };
            auto clickHeader = [&]
            {
                const auto centre = layout.chainBypass.getCentre().toFloat();
                mixer->mouseDown(event(centre));
                mixer->mouseUp(event(centre));
            };
            auto sameNotes = [](const std::vector<Note> &a, const std::vector<Note> &b)
            {
                if (a.size() != b.size())
                    return false;
                for (size_t i = 0; i < a.size(); ++i)
                    if (a[i].id != b[i].id || a[i].channel != b[i].channel || a[i].beat != b[i].beat ||
                        a[i].length != b[i].length || a[i].pitch != b[i].pitch ||
                        a[i].velocity != b[i].velocity || a[i].step != b[i].step || a[i].muted != b[i].muted)
                        return false;
                return true;
            };
            auto exerciseOwnedCancellation = [&]
            {
                check(pendingMidiTake != nullptr, "Actual owned take required before chain edits");
                const auto *owner = pendingMidiTake.get();
                const auto take = *pendingMidiTake;
                const auto finished = midiTake.finishedNotes();
                const auto held = midiTake.heldCount();
                const auto inputPending = midiTake.hasPendingEvent();
                const auto failure = midiTake.failureCode();
                const auto before = controller.project.state();
                const auto dirty = controller.dirty;
                const auto undo = controller.undo.getUndoDescriptions().joinIntoString("|");
                const auto redo = controller.undo.getRedoDescriptions().joinIntoString("|");
                const auto wasTaking = midiTaking, wasArmed = recordArmed, wasPlaying = audio.playing();
                const auto position = audio.positionBeat();
                const auto looping = audio.looping();
                const auto pattern = selectedPattern, channel = selectedChannel;
                const auto starts = device->starts, stops = device->stops;
                const auto inputRead = audio.midiInput.capture.read.load();
                const auto inputWrite = audio.midiInput.capture.write.load();
                const auto session = audio.midiInput.capture.session.load();
                const auto active = audio.midiInput.capture.active.load();
                const auto inputFailed = audio.midiInput.capture.failed.load();
                MidiClock::Position clock;
                check(audio.midiInput.recordingPosition(clock), "Take-chain actual capture clock exists");
                auto unchangedTake = [&]
                {
                    MidiClock::Position after;
                    check(
                        pendingMidiTake.get() == owner && owner->serial == take.serial &&
                            owner->identity == take.identity && owner->phase == take.phase &&
                            owner->error == take.error && owner->args.pattern == take.args.pattern &&
                            owner->args.target == take.args.target &&
                            owner->args.enabled == take.args.enabled && owner->args.beat == take.args.beat &&
                            owner->placementBeat == take.placementBeat &&
                            owner->placementOffset == take.placementOffset &&
                            owner->stopSeconds == take.stopSeconds &&
                            sameNotes(owner->args.recordedNotes, take.args.recordedNotes) &&
                            sameNotes(midiTake.finishedNotes(), finished) && midiTake.heldCount() == held &&
                            midiTake.hasPendingEvent() == inputPending && midiTake.failureCode() == failure &&
                            midiTaking == wasTaking && recordArmed == wasArmed &&
                            audio.playing() == wasPlaying && audio.positionBeat() == position &&
                            audio.looping() == looping && selectedPattern == pattern &&
                            selectedChannel == channel && selectedInsert == 1 && starts == device->starts &&
                            stops == device->stops && inputRead == audio.midiInput.capture.read.load() &&
                            inputWrite == audio.midiInput.capture.write.load() &&
                            session == audio.midiInput.capture.session.load() &&
                            active == audio.midiInput.capture.active.load() &&
                            inputFailed == audio.midiInput.capture.failed.load() &&
                            audio.midiInput.recordingPosition(after) && after.seconds == clock.seconds &&
                            after.playing == clock.playing,
                        "Chain preview/refusal/cancel must preserve exact take owner, notes, placement, "
                        "armed/capture/queue/clock/transport/device state");
                };
                auto cancelledBaseline = [&]
                {
                    unchangedTake();
                    check(!controller.hasGesture() && before.isEquivalentTo(controller.project.state()) &&
                              controller.dirty == dirty &&
                              controller.undo.getUndoDescriptions().joinIntoString("|") == undo &&
                              controller.undo.getRedoDescriptions().joinIntoString("|") == redo,
                          "Cancelled chain edit restores exact canonical controls/dirty/Undo/Redo baseline");
                };
                mixer->mouseDown(event(knob));
                mixer->mouseDrag(event(knob.translated(0, 60)));
                const auto preview = mixer->effectSlotsState()["wetGesture"];
                check(preview["completionState"].toString() == "pending" && bool(preview["valid"]) &&
                          juce::int64(preview["plugin"]) == first &&
                          controller.project.plugin(first)->wet == .5f,
                      "Actual held/retained UI wet drag owns a real canonical preview");
                unchangedTake();
                // Escape first cancels the control lease, not the captured take.
                check(performAction("selection.cancel"), "Actual Escape cancels only the local wet lease");
                check(mixer->effectSlotsState()["wetGesture"].isVoid(), "UI lease is terminal after cancel");
                cancelledBaseline();

                const auto lease =
                    data("vibera_gesture",
                         juce::JSON::parse(R"({"action":"begin","domain":"mixerEffects","sourceInsert":1})"));
                check(lease["completionState"].toString() == "pending" && int(lease["sourceInsert"]) == 1 &&
                          lease["domain"].toString() == "mixerEffects",
                      "Actual capture/retained semantic lease returns its captured chain target");
                auto update = juce::JSON::parse(
                    R"({"action":"update","command":"plugin.effect.mix","args":{"value":0.375}})");
                property(update, "operation", lease["operation"]);
                update["args"].getDynamicObject()->setProperty("target", juce::int64(first));
                data("vibera_gesture", update);
                auto bypass = juce::JSON::parse(
                    R"({"action":"update","command":"mixer.effects.bypass","args":{"mixer":1,"enabled":true}})");
                property(bypass, "operation", lease["operation"]);
                data("vibera_gesture", bypass);
                check(controller.project.plugin(first)->wet == .375f &&
                          controller.project.mixer[1].effectsBypassed,
                      "Semantic held/retained wet and chain bypass update the canonical model");
                unchangedTake();
                const auto validPreview = controller.project.state();
                for (const bool wrongEffect : {false, true})
                {
                    auto wrong = juce::JSON::parse(
                        wrongEffect
                            ? R"({"action":"update","command":"plugin.effect.mix","args":{"value":0.25}})"
                            : R"({"action":"update","command":"mixer.effects.bypass","args":{"mixer":2,"enabled":true}})");
                    property(wrong, "operation", lease["operation"]);
                    if (wrongEffect)
                        wrong["args"].getDynamicObject()->setProperty("target", juce::int64(foreign));
                    bool refused = false;
                    try
                    {
                        invoke("vibera_gesture", wrong);
                    }
                    catch (const McpFailure &error)
                    {
                        refused = error.code == "gesture_domain";
                    }
                    check(refused && validPreview.isEquivalentTo(controller.project.state()),
                          "Foreign-chain effect and header errors preserve the valid preview");
                    unchangedTake();
                }
                auto cancel = request("cancel");
                property(cancel, "operation", lease["operation"]);
                check(data("vibera_gesture", cancel)["completionState"].toString() == "cancelled",
                      "Semantic cancel reports actual restored baseline");
                cancelledBaseline();
                if (wasTaking)
                {
                    clickHeader();
                    check(controller.project.mixer[1].effectsBypassed,
                          "Actual header commits while the original software-injected note is held");
                    unchangedTake();
                    check(controller.undoEditChecked().wasOk() &&
                              before.isEquivalentTo(controller.project.state()),
                          "One checked control Undo during capture restores the exact model");
                    unchangedTake();
                    const auto secondLease = data(
                        "vibera_gesture",
                        juce::JSON::parse(R"({"action":"begin","domain":"mixerEffects","sourceInsert":1})"));
                    property(update, "operation", secondLease["operation"]);
                    property(bypass, "operation", secondLease["operation"]);
                    data("vibera_gesture", update);
                    data("vibera_gesture", bypass);
                    auto commit = request("commit");
                    property(commit, "operation", secondLease["operation"]);
                    check(data("vibera_gesture", commit)["completionState"].toString() == "completed" &&
                              controller.project.plugin(first)->wet == .375f &&
                              controller.project.mixer[1].effectsBypassed,
                          "Actual capture MCP chain preview commits one canonical transaction");
                    unchangedTake();
                    check(controller.undoEditChecked().wasOk() &&
                              before.isEquivalentTo(controller.project.state()),
                          "One control Undo restores both semantic mix and chain override during capture");
                    unchangedTake();
                    const auto order = controller.project.state();
                    CommandArgs move;
                    move.mixer = 1;
                    command("mixer.moveRight", move);
                    check(status == "Ready" && controller.project.mixer[2].index == 1,
                          "Actual UI order command commits while original captured input is held");
                    unchangedTake();
                    check(controller.undoEditChecked().wasOk() &&
                              order.isEquivalentTo(controller.project.state()),
                          "One canonical order Undo restores complete state during actual capture");
                    unchangedTake();
                    const auto moved =
                        data("vibera_command",
                             juce::JSON::parse(R"({"command":"mixer.moveRight","args":{"mixer":1}})"));
                    check(moved["completionState"].toString() == "completed" && bool(moved["changed"]) &&
                              controller.project.mixer[2].index == 1,
                          "Actual capture semantic order command has truthful canonical completion");
                    unchangedTake();
                    check(controller.undoEditChecked().wasOk() &&
                              order.isEquivalentTo(controller.project.state()),
                          "Semantic capture reorder is exactly one Undo");
                    unchangedTake();
                    completeCase(song ? "actual-Song-capture-insert-order-UI-MCP-history"
                                      : "actual-Pattern-capture-insert-order-UI-MCP-history");
                }
                else
                {
                    bool refused = false;
                    try
                    {
                        const auto result =
                            invoke("vibera_command",
                                   juce::JSON::parse(R"({"command":"mixer.moveRight","args":{"mixer":1}})"));
                        refused = bool(result["isError"]);
                    }
                    catch (const McpFailure &)
                    {
                        refused = true;
                    }
                    check(refused && before.isEquivalentTo(controller.project.state()),
                          "Retained foreign history refuses order without consuming the take or executing "
                          "foreign actions");
                    unchangedTake();
                }
            };
            auto expected = captureClosedNote(song, song, exerciseOwnedCancellation);
            check(original.state().isEquivalentTo(controller.project.state()),
                  "Control cancellation during actual held capture preserves the complete original project");
            completeCase(song ? "actual-Song-capture-UI-and-MCP-chain-cancel-foreign-errors"
                              : "actual-Pattern-capture-UI-and-MCP-chain-cancel-foreign-errors");
            const auto serial = midiTakeSerial();
            int foreignCalls = 0;
            addBlocker(foreignCalls);
            stop.onClick();
            check(!audio.playing() && !midiTaking && !recordArmed && pendingMidiTake &&
                      pendingMidiTake->phase == PendingMidiTake::Phase::ready && foreignCalls == 1 &&
                      pendingMidiTake->args.recordedNotes.size() == 1,
                  "Genuine foreign-history Stop retains the actual chain-control acquisition");
            if (song)
            {
                expected.length = (pendingMidiTake->stopSeconds - expected.onSeconds) * 2;
                check(expected.length > 1. / 960 && expected.beat + expected.length < 4,
                      "Chain Song take uses the actual quiesced Stop cutoff");
                audio.midiInput.route(juce::MidiMessage::noteOff(1, 66), true, true);
                process(2);
            }
            checkNote(pendingMidiTake->args.recordedNotes.front(), expected, false);
            exerciseOwnedCancellation();
            check(foreignCalls == 1 && midiTakeSerial() == serial &&
                      original.state().isEquivalentTo(controller.project.state()),
                  "Retained control cancellation never executes/discards foreign history or take");
            completeCase(song ? "actual-Song-retained-UI-and-MCP-chain-cancel-foreign-errors"
                              : "actual-Pattern-retained-UI-and-MCP-chain-cancel-foreign-errors");

            // Explicit test-only recovery removes the known foreign action;
            // product recovery never clears user history to admit a take.
            controller.undo.clearUndoHistory();
            {
                const auto retained = *pendingMidiTake;
                const auto *owner = pendingMidiTake.get();
                const auto order = controller.project.state();
                const auto source = selectedChannel, part = selectedPattern;
                const auto starts = device->starts, stops = device->stops;
                const auto position = audio.positionBeat();
                const auto moved =
                    data("vibera_command",
                         juce::JSON::parse(R"({"command":"mixer.moveRight","args":{"mixer":1}})"));
                check(moved["completionState"].toString() == "completed" && bool(moved["changed"]) &&
                          controller.project.mixer[2].index == 1,
                      "Stopped retained take admits canonical order without applying recording");
                CommandArgs move;
                move.mixer = 1;
                command("mixer.moveLeft", move);
                check(status == "Ready" && order.isEquivalentTo(controller.project.state()) &&
                          pendingMidiTake.get() == owner && owner->serial == retained.serial &&
                          owner->identity == retained.identity && owner->phase == retained.phase &&
                          owner->args.pattern == retained.args.pattern &&
                          owner->args.target == retained.args.target &&
                          owner->args.beat == retained.args.beat &&
                          owner->placementBeat == retained.placementBeat &&
                          owner->placementOffset == retained.placementOffset &&
                          owner->stopSeconds == retained.stopSeconds &&
                          sameNotes(owner->args.recordedNotes, retained.args.recordedNotes) &&
                          selectedChannel == source && selectedPattern == part && selectedInsert == 1 &&
                          !recordArmed && !midiTaking && !audio.playing() &&
                          audio.positionBeat() == position && starts == device->starts &&
                          stops == device->stops,
                      "Retained order pair preserves exact take owner/notes/placement/source/clock/device "
                      "and full original model");
                checkNote(owner->args.recordedNotes.front(), expected, false);
                completeCase(song ? "actual-Song-retained-insert-order-UI-MCP"
                                  : "actual-Pattern-retained-insert-order-UI-MCP");
            }
            clickHeader();
            check(controller.project.mixer[1].effectsBypassed && pendingMidiTake &&
                      midiTakeSerial() == serial && !recordArmed && !audio.playing(),
                  "Actual header commits a usable control while the stopped take remains retained");
            checkNote(pendingMidiTake->args.recordedNotes.front(), expected, false);
            juce::MouseWheelDetails wheel{};
            wheel.deltaY = -1;
            mixer->mouseWheelMove(event(knob), wheel);
            check(std::abs(controller.project.plugin(first)->wet - .9f) < 1.e-6 && pendingMidiTake &&
                      midiTakeSerial() == serial && !recordArmed && !midiTaking,
                  "Actual wet wheel commits without consuming the retained take");
            checkNote(pendingMidiTake->args.recordedNotes.front(), expected, false);
            const auto beforeRetry = controller.project.state();
            const auto applied = recording("retry", serial);
            check(!bool(applied["isError"]) && !hasPendingMidiTake() &&
                      applied["structuredContent"]["completionState"].toString() == "completed" &&
                      controller.project.mixer[1].effectsBypassed &&
                      std::abs(controller.project.plugin(first)->wet - .9f) < 1.e-6,
                  "Canonical retry commits once and retains intervening chain controls");
            const auto *notes = controller.project.pattern(expected.pattern);
            check(notes && notes->notes.size() == 1,
                  "Chain recovery creates exactly one original-source note");
            checkNote(notes->notes.front(), expected, true);
            const auto recorded = controller.project.state();
            expectRecordingFailure("retry", serial, "take.stale");
            check(recorded.isEquivalentTo(controller.project.state()) &&
                      controller.undoEditChecked().wasOk() &&
                      beforeRetry.isEquivalentTo(controller.project.state()) &&
                      controller.redoEditChecked().wasOk() &&
                      recorded.isEquivalentTo(controller.project.state()),
                  "Consumed-token refusal and one exact retry Undo/Redo preserve controls, allocator and "
                  "placement");
            completeCase(song ? "actual-Song-retained-chain-commit-exact-once-retry-history"
                              : "actual-Pattern-retained-chain-commit-exact-once-retry-history");
        }

        // Separate stereo admission evidence using GENUINE acquired held takes,
        // the existing actual source-clock oracle and the same owned device.
        // No synthetic PendingMidiTake seed, semantic batch, or fake held flag.
        juce::String stereoTakeTrace;
        for (const bool song : {false, true})
        {
            prepare(song);
            setSize(1920, 1044);
            check(selectMixerInsert(1).wasOk(), "Select stable stereo recipient before capture");
            if (maximisedEditor != 3)
                maximiseEditor(3);
            auto *mixer = static_cast<Editor *>(windows[3]->getContentComponent());
            juce::ValueTree beforeStereoCommit;
            auto expected = captureClosedNote(
                song, false,
                [&]
                {
                    check(pendingMidiTake && midiTaking && recordArmed && audio.playing() &&
                              midiTake.heldCount() == 1 && midiTake.finishedNotes().empty() &&
                              audio.projectionDiagnostics().channels.front().heldInputKeys == 1,
                          "Actual event-created take and consumed held note precede stereo controls");
                    const auto *owner = pendingMidiTake.get();
                    const auto takeIdentity = owner->identity;
                    const auto serial = owner->serial;
                    const auto part = owner->args.pattern, placement = owner->args.target;
                    const auto placementBeat = owner->placementBeat, placementOffset = owner->placementOffset;
                    const auto takeStartBeat = owner->args.beat;
                    const bool placementMode = owner->args.enabled;
                    const auto source = selectedChannel;
                    const auto session = audio.midiInput.capture.session.load();
                    const auto inputRead = audio.midiInput.capture.read.load(),
                               inputWrite = audio.midiInput.capture.write.load();
                    const auto baseline = audio.projectionDiagnostics();
                    const auto gains = audio.mixerGraphInitialisations();
                    const auto starts = device->starts, stops = device->stops;
                    auto retain = [&](const char *phase)
                    {
                        const auto now = audio.projectionDiagnostics();
                        const auto *strip = mixerInsert(controller.project, 1);
                        const int inputHeld = now.channels.empty() ? -1 : now.channels.front().heldInputKeys;
                        stereoTakeTrace += juce::String(song ? "Song" : "Pattern") + "," + phase +
                                           ",serial=" + juce::String(juce::int64(midiTakeSerial())) +
                                           ",capture=" + juce::String(int(midiTaking)) +
                                           ",armed=" + juce::String(int(recordArmed)) +
                                           ",playing=" + juce::String(int(audio.playing())) +
                                           ",held=" + juce::String(int(midiTake.heldCount())) +
                                           ",inputHeld=" + juce::String(inputHeld) +
                                           ",separation=" + juce::String(strip->stereoSeparation, 6) +
                                           ",invert=" + juce::String(int(strip->phaseInverted)) +
                                           ",swap=" + juce::String(int(strip->stereoSwapped)) +
                                           ",deviceStarts=" + juce::String(device->starts) +
                                           ",deviceStops=" + juce::String(device->stops) + "\n";
                        check(folder.getChildFile("mixer-stereo-held-take.txt")
                                  .replaceWithText(stereoTakeTrace),
                              "Retain actual stereo take/owner/control outcomes before assertions");
                        bool channelsStable = now.channels.size() == baseline.channels.size();
                        if (channelsStable)
                            for (std::size_t i = 0; i < now.channels.size(); ++i)
                                channelsStable &= now.channels[i].channel == baseline.channels[i].channel &&
                                                  now.channels[i].track == baseline.channels[i].track &&
                                                  now.channels[i].sampler == baseline.channels[i].sampler &&
                                                  now.channels[i].sample == baseline.channels[i].sample &&
                                                  now.channels[i].graphInitialisations ==
                                                      baseline.channels[i].graphInitialisations;
                        check(pendingMidiTake.get() == owner && owner->identity == takeIdentity &&
                                  owner->serial == serial &&
                                  owner->phase == PendingMidiTake::Phase::capturing &&
                                  owner->args.pattern == part && owner->args.target == placement &&
                                  owner->args.beat == takeStartBeat && owner->args.enabled == placementMode &&
                                  owner->placementBeat == placementBeat &&
                                  owner->placementOffset == placementOffset &&
                                  owner->args.recordedNotes.empty() && midiTake.heldCount() == 1 &&
                                  midiTake.finishedNotes().empty() && !midiTake.hasPendingEvent() &&
                                  midiTake.failureCode() == MidiTake::Failure::none && midiTaking &&
                                  recordArmed && audio.playing() && selectedChannel == source &&
                                  selectedPattern == part && selectedInsert == 1 &&
                                  audio.midiInput.capture.active.load() &&
                                  !audio.midiInput.capture.failed.load() &&
                                  audio.midiInput.capture.session.load() == session &&
                                  audio.midiInput.capture.read.load() == inputRead &&
                                  audio.midiInput.capture.write.load() == inputWrite &&
                                  now.edit == baseline.edit &&
                                  now.playbackContext == baseline.playbackContext &&
                                  now.replacements == baseline.replacements &&
                                  now.samplerCreations == baseline.samplerCreations &&
                                  now.cache.preparations == baseline.cache.preparations && channelsStable &&
                                  audio.mixerGraphInitialisations() == gains && inputHeld == 1 &&
                                  device->starts == starts && device->stops == stops,
                              "Stereo UI/MCP never takes over, closes, retargets, drops capture or replaces "
                              "held sample/context/device");
                    };
                    const auto publishAndProcess = [&](const char *phase)
                    {
                        check(audio.rebuild(controller.project, patternMode, selectedPattern).wasOk(),
                              "Actual live projection of held-take stereo target");
                        needsRebuild = false;
                        engineRevision = controller.revision;
                        const auto peak = process(1);
                        bool finite = true;
                        for (int channel = 0; channel < block.getNumChannels(); ++channel)
                            for (int frame = 0; frame < block.getNumSamples(); ++frame)
                                finite &= std::isfinite(block.getSample(channel, frame));
                        retain(phase);
                        check(finite && peak > 1.e-6f,
                              "Actual finite nonzero held sampler PCM after stereo interaction");
                    };
                    const auto clickFlag = [&](juce::Rectangle<int> bounds)
                    {
                        check(!bounds.isEmpty(), "Actual stereo flag target visible in owned Mixer");
                        const auto point = bounds.getCentre().toFloat();
                        const auto now = juce::Time::getCurrentTime();
                        const juce::MouseEvent event(
                            juce::Desktop::getInstance().getMainMouseSource(), point,
                            juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, mixer,
                            mixer, now, point, now, 1, true);
                        mixer->mouseDown(event);
                        mixer->mouseUp(event);
                    };
                    clickFlag(mixer->mixerPhaseInvertBounds(1));
                    check(mixerInsert(controller.project, 1)->phaseInverted,
                          "Actual polarity click commits while note is held");
                    publishAndProcess("actual_polarity_click");
                    clickFlag(mixer->mixerStereoSwapBounds(1));
                    check(mixerInsert(controller.project, 1)->stereoSwapped,
                          "Actual swap click commits while note is held");
                    publishAndProcess("actual_swap_click");
                    for (const auto *command : {"mixer.phaseInvert", "mixer.stereoSwap"})
                    {
                        auto args = juce::JSON::parse(R"({"args":{"mixer":1,"enabled":false}})");
                        property(args, "command", command);
                        const auto changed = data("vibera_command", args);
                        check(changed["completionScope"].toString() == "canonicalMixerStereoTarget" &&
                                  !bool(changed["dspSettlingAcknowledged"]),
                              "Discrete semantic completion honestly acknowledges only saved target");
                        publishAndProcess(command);
                    }
                    check(!mixerInsert(controller.project, 1)->phaseInverted &&
                              !mixerInsert(controller.project, 1)->stereoSwapped,
                          "Both semantic flags retain actual capture and restore default targets");
                    const auto beforeCancel = controller.project.state();
                    const auto cancelDirty = controller.dirty;
                    const auto undo = controller.undo.getUndoDescriptions(),
                               redo = controller.undo.getRedoDescriptions();
                    auto begin = request("begin");
                    property(begin, "domain", "mixerControl");
                    property(begin, "sourceInsert", 1);
                    auto lease = data("vibera_gesture", begin);
                    auto update = request("update");
                    property(update, "operation", lease["operation"]);
                    property(update, "command", "mixer.stereoSeparation");
                    property(update, "args", juce::JSON::parse(R"({"mixer":1,"value":0.625})"));
                    check(data("vibera_gesture", update)["completionState"].toString() == "pending" &&
                              mixerInsert(controller.project, 1)->stereoSeparation == .625f,
                          "Actual mixerControl lease admits separation during genuine held capture");
                    publishAndProcess("actual_separation_lease_preview");
                    auto cancel = request("cancel");
                    property(cancel, "operation", lease["operation"]);
                    check(data("vibera_gesture", cancel)["completionState"].toString() == "cancelled" &&
                              beforeCancel.isEquivalentTo(controller.project.state()) &&
                              controller.dirty == cancelDirty &&
                              controller.undo.getUndoDescriptions() == undo &&
                              controller.undo.getRedoDescriptions() == redo,
                          "Actual leased stereo cancel restores exact musical/dirty/history baseline without "
                          "closing take");
                    publishAndProcess("actual_separation_lease_cancel");
                    beforeStereoCommit = controller.project.state();
                    lease = data("vibera_gesture", begin);
                    property(update, "operation", lease["operation"]);
                    property(update, "args", juce::JSON::parse(R"({"mixer":1,"value":-0.25})"));
                    check(data("vibera_gesture", update)["completionState"].toString() == "pending",
                          "Actual second separation preview");
                    auto commit = request("commit");
                    property(commit, "operation", lease["operation"]);
                    check(data("vibera_gesture", commit)["completionState"].toString() == "completed" &&
                              mixerInsert(controller.project, 1)->stereoSeparation == -.25f &&
                              controller.undo.getUndoDescriptions().size() == undo.size() + 1 &&
                              !controller.hasGesture(),
                          "Actual held-take separation commits exactly one independent real Undo");
                    publishAndProcess("actual_separation_lease_commit");
                });
            const auto beforeStop = controller.project.state();
            stop.onClick();
            check(!midiTaking && !recordArmed && !audio.playing() && !pendingMidiTake &&
                      controller.project.pattern(expected.pattern)->notes.size() == 1,
                  "Actual Stop completes one original-source note independently of stereo control history");
            checkNote(controller.project.pattern(expected.pattern)->notes.front(), expected, true);
            const auto afterStop = controller.project.state();
            check(controller.undoEditChecked().wasOk() &&
                      beforeStop.isEquivalentTo(controller.project.state()) &&
                      controller.undoEditChecked().wasOk() &&
                      beforeStereoCommit.isEquivalentTo(controller.project.state()) &&
                      controller.redoEditChecked().wasOk() &&
                      beforeStop.isEquivalentTo(controller.project.state()) &&
                      controller.redoEditChecked().wasOk() &&
                      afterStop.isEquivalentTo(controller.project.state()),
                  "Recording Stop and stereo lease are separate exact real Undo/Redo transactions");
            checkNote(controller.project.pattern(expected.pattern)->notes.front(), expected, true);
            completeCase(
                song ? "actual-Song-held-take-stereo-UI-flags-MCP-lease-cancel-commit-exact-recording-history"
                     : "actual-Pattern-held-take-stereo-UI-flags-MCP-lease-cancel-commit-exact-recording-"
                       "history");
        }

        // Genuine armed/captured/retained-take automation integration. Never seed
        // PendingMidiTake or alter acquisition metadata. Synthetic source events
        // and the existing exact source-clock note oracle remain authoritative.
        juce::String automationTakeTrace;
        for (const bool song : {false, true})
        {
            prepare(song);
            setSize(1920, 1044);
            check(selectMixerInsert(1).wasOk(), "Stable automation capture source selection");
            const auto sameNotes = [](const std::vector<Note> &a, const std::vector<Note> &b)
            {
                if (a.size() != b.size())
                    return false;
                for (std::size_t i = 0; i < a.size(); ++i)
                    if (a[i].id != b[i].id || a[i].channel != b[i].channel || a[i].beat != b[i].beat ||
                        a[i].length != b[i].length || a[i].pitch != b[i].pitch ||
                        a[i].velocity != b[i].velocity || a[i].step != b[i].step || a[i].muted != b[i].muted)
                        return false;
                return true;
            };
            const auto semantic = [&](const char *id, juce::var args)
            {
                auto call = request("");
                call.getDynamicObject()->removeProperty("action");
                property(call, "command", id);
                property(call, "args", args);
                return data("vibera_command", call);
            };
            const auto exercise = [&](const char *phase, int insert)
            {
                const auto *owner = pendingMidiTake.get();
                const auto retained = owner ? std::make_unique<PendingMidiTake>(*owner) : nullptr;
                const auto notes = midiTake.finishedNotes();
                const auto held = midiTake.heldCount();
                const auto failure = midiTake.failureCode();
                const auto serial = midiTakeSerial();
                const auto wasArmed = recordArmed, wasTaking = midiTaking, wasPlaying = audio.playing();
                const auto source = selectedChannel, part = selectedPattern;
                const auto session = audio.midiInput.capture.session.load();
                const auto active = audio.midiInput.capture.active.load();
                const auto read = audio.midiInput.capture.read.load(),
                           write = audio.midiInput.capture.write.load();
                const auto projection = audio.projectionDiagnostics();
                const auto channelCount = controller.project.channels.size();
                check(
                    channelCount > 0 && projection.channels.size() == channelCount &&
                        projection.channels.front().channel == selectedChannel,
                    "All original default-project sources and exact selected recording source are projected");
                const auto stages = audio.mixerGraphInitialisations();
                auto position = audio.positionBeat();
                const auto looping = audio.looping();
                const int starts = device->starts, stops = device->stops;
                MidiClock::Position clock;
                check(audio.midiInput.recordingPosition(clock), "Actual automation take source clock");
                const auto unchanged = [&](bool exactClock)
                {
                    MidiClock::Position after;
                    const auto now = audio.projectionDiagnostics();
                    bool owners = now.channels.size() == projection.channels.size();
                    if (owners)
                        for (std::size_t i = 0; i < now.channels.size(); ++i)
                            owners &= now.channels[i].channel == projection.channels[i].channel &&
                                      now.channels[i].track == projection.channels[i].track &&
                                      now.channels[i].sampler == projection.channels[i].sampler &&
                                      now.channels[i].sample == projection.channels[i].sample &&
                                      now.channels[i].graphInitialisations ==
                                          projection.channels[i].graphInitialisations;
                    check(pendingMidiTake.get() == owner && midiTakeSerial() == serial &&
                              recordArmed == wasArmed && midiTaking == wasTaking &&
                              audio.playing() == wasPlaying && selectedChannel == source &&
                              selectedPattern == part && selectedInsert == 1 &&
                              sameNotes(midiTake.finishedNotes(), notes) && midiTake.heldCount() == held &&
                              midiTake.failureCode() == failure && !midiTake.hasPendingEvent() &&
                              audio.midiInput.capture.session.load() == session &&
                              audio.midiInput.capture.active.load() == active &&
                              audio.midiInput.capture.read.load() == read &&
                              audio.midiInput.capture.write.load() == write &&
                              !audio.midiInput.capture.failed.load() && device->starts == starts &&
                              device->stops == stops && now.edit == projection.edit &&
                              now.playbackContext == projection.playbackContext &&
                              now.replacements == projection.replacements &&
                              now.samplerCreations == projection.samplerCreations &&
                              now.cache.preparations == projection.cache.preparations &&
                              now.midiUpdates == projection.midiUpdates && owners &&
                              now.channels.front().heldInputKeys ==
                                  projection.channels.front().heldInputKeys &&
                              audio.positionBeat() == position && audio.looping() == looping &&
                              audio.mixerGraphInitialisations() == stages &&
                              audio.midiInput.recordingPosition(after) && after.playing == clock.playing &&
                              after.block == clock.block && (!exactClock || after.seconds == clock.seconds),
                          "Automation preserves actual take/source/queue/clock/context/sample/device owners");
                    if (owner)
                        check(owner->identity == retained->identity && owner->serial == retained->serial &&
                                  owner->phase == retained->phase && owner->error == retained->error &&
                                  owner->args.pattern == retained->args.pattern &&
                                  owner->args.target == retained->args.target &&
                                  owner->args.channel == retained->args.channel &&
                                  owner->args.beat == retained->args.beat &&
                                  owner->args.enabled == retained->args.enabled &&
                                  owner->placementBeat == retained->placementBeat &&
                                  owner->placementOffset == retained->placementOffset &&
                                  owner->stopSeconds == retained->stopSeconds &&
                                  sameNotes(owner->args.recordedNotes, retained->args.recordedNotes),
                              "Automation never changes the genuine captured take payload or placement");
                };
                const auto publish = [&](const char *step)
                {
                    unchanged(true);
                    check(audio.rebuild(controller.project, patternMode, selectedPattern).wasOk(),
                          "Actual live automation document publication during take");
                    needsRebuild = false;
                    engineRevision = controller.revision;
                    const auto before = audio.projectionDiagnostics();
                    const auto peak = process(1);
                    bool finite = true;
                    for (int channel = 0; channel < block.getNumChannels(); ++channel)
                        for (int frame = 0; frame < block.getNumSamples(); ++frame)
                            finite &= std::isfinite(block.getSample(channel, frame));
                    MidiClock::Position after;
                    check(audio.midiInput.recordingPosition(after),
                          "Published automation source clock remains readable");
                    const auto now = audio.projectionDiagnostics();
                    check(now.channels.size() == channelCount && before.channels.size() == channelCount,
                          "All original sources remain available for complete callback evidence");
                    automationTakeTrace += juce::String(song ? "Song" : "Pattern") + "," + phase + "," +
                                           step + ",serial=" + juce::String(juce::int64(serial)) +
                                           ",secondsBefore=" + juce::String(clock.seconds, 15) +
                                           ",secondsAfter=" + juce::String(after.seconds, 15) +
                                           ",peak=" + juce::String(peak, 9) + ",sourceCalls=" +
                                           juce::String(juce::int64(now.channels.front().renderCalls -
                                                                    before.channels.front().renderCalls)) +
                                           ",armed=" + juce::String(int(recordArmed)) +
                                           ",taking=" + juce::String(int(midiTaking)) + "\n";
                    check(folder.getChildFile("mixer-automation-held-take.txt")
                              .replaceWithText(automationTakeTrace),
                          "Retain each actual automation/take PCM and source-clock step before assertion");
                    check(finite && (!wasTaking || peak > 1.e-6f) && now.channels.size() == channelCount &&
                              before.channels.size() == channelCount &&
                              now.channels.front().renderCalls == before.channels.front().renderCalls + 1 &&
                              std::abs(after.seconds - clock.seconds - (wasPlaying ? 256. / 48000. : 0.)) <
                                  1.e-12,
                          "Every unchanged-speed nonwrapping callback has finite PCM, one source call and "
                          "exact source-clock advance");
                    clock = after;
                    position = audio.positionBeat();
                    unchanged(true);
                };
                const auto intent = [&](int target, MixerAutomationControl control)
                {
                    return MixerAutomationUiIntent{controller.projectIdentity, controller.revision, target,
                                                   control};
                };
                check(performMixerAutomationIntent(intent(insert, MixerAutomationControl::pan),
                                                   MixerAutomationUiAction::create)
                          .wasOk(),
                      "Actual UI pan Create during armed/capturing/retained take");
                const Id motion = controller.project.automation.sources.back().id;
                const Id point = controller.project.automation.sources.back().points.front().id;
                check(
                    performMixerAutomationIntent(intent(insert + 1, MixerAutomationControl::stereoSeparation),
                                                 MixerAutomationUiAction::create)
                        .wasOk(),
                    "Actual UI separation Create without touching MIDI capture");
                check(performMixerAutomationIntent(intent(insert, MixerAutomationControl::stereoSeparation),
                                                   MixerAutomationUiAction::link, motion)
                          .wasOk(),
                      "Actual UI shared-source separation link preserves capture");
                auto targetArgs =
                    juce::JSON::parse(R"({"automationControl":"stereoSeparation","value":0.125})");
                property(targetArgs, "mixer", insert);
                check(semantic("automation.init", targetArgs)["completionScope"].toString() ==
                          "canonicalAutomationTarget",
                      "Semantic initialization completion describes only canonical target");
                check(performMixerAutomationIntent(intent(insert, MixerAutomationControl::stereoSeparation),
                                                   MixerAutomationUiAction::removeInitialization)
                          .wasOk(),
                      "Actual UI removes exact initialized separation during take");
                auto range =
                    juce::JSON::parse(R"({"automationControl":"stereoSeparation","beat":0,"length":0.5})");
                property(range, "target", juce::int64(motion));
                property(range, "mixer", insert);
                const auto changed = semantic("automation.range", range);
                check(changed["completionState"].toString() == "completed" &&
                          !bool(changed["dspSettlingAcknowledged"]),
                      "Semantic range acknowledgement never pretends DSP settling");
                unchanged(true);
                publish("shared_create_init_link_range");
                const auto baseline = controller.project.state();
                const auto dirty = controller.dirty;
                const auto undo = controller.undo.getUndoDescriptions(),
                           redo = controller.undo.getRedoDescriptions();
                automationSourceDialog(motion);
                auto *dialog =
                    dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
                check(dialog != nullptr, "Actual source properties opens during take");
                dialog->getTextEditor("name")->setText("Cancelled take edit");
                dialog->exitModalState(0);
                dispatchMessages(20);
                check(
                    baseline.isEquivalentTo(controller.project.state()) && controller.dirty == dirty &&
                        controller.undo.getUndoDescriptions() == undo &&
                        controller.undo.getRedoDescriptions() == redo,
                    "Actual properties Cancel preserves exact project and history while take remains owned");
                unchanged(true);
                auto move = juce::JSON::parse(R"({"beat":0,"value":0.625})");
                property(move, "target", juce::int64(point));
                semantic("automation.point.move", move);
                const auto edited = controller.project.state();
                check(!baseline.isEquivalentTo(edited) && controller.undoEditChecked().wasOk() &&
                          baseline.isEquivalentTo(controller.project.state()) &&
                          controller.redoEditChecked().wasOk() &&
                          edited.isEquivalentTo(controller.project.state()),
                      "Semantic shared point edit has exact single Undo/Redo during genuine take");
                unchanged(true);
                publish("semantic_shared_point_undo_redo");
                auto unlink = juce::JSON::parse(R"({"automationControl":"stereoSeparation"})");
                property(unlink, "mixer", insert);
                property(unlink, "target", juce::int64(motion));
                semantic("automation.unlink", unlink);
                check(controller.project.automationSource(motion)->links.size() == 1,
                      "Semantic unlink removes only separation, preserving shared pan link");
                auto link = unlink;
                semantic("automation.link", link);
                semantic("automation.range", range);
                unchanged(true);
                publish("semantic_unlink_relink_exact_kind");
                // Document-domain remote previews remain intentionally unavailable
                // for recording. Do not borrow mixerControl as an automation lease.
                bool refused = false;
                try
                {
                    const auto response = invoke(
                        "vibera_gesture", juce::JSON::parse(R"({"action":"begin","domain":"document"})"));
                    refused = bool(response["isError"]) &&
                              response["structuredContent"]["error"]["code"].toString() == "busy";
                }
                catch (const McpFailure &error)
                {
                    refused = error.code == "busy";
                }
                check(refused && !controller.hasGesture(),
                      "Existing document lease refuses capture/retention without stealing take");
                unchanged(true);
            };
            record.onClick();
            check(recordArmed && !midiTaking && !pendingMidiTake, "Actual stopped armed automation state");
            exercise("armed", 3);
            stop.onClick();
            check(!recordArmed && !midiTaking && !pendingMidiTake,
                  "Actual Stop disarms without invented recording");
            auto expected = captureClosedNote(song, false, [&] { exercise("held_capture", 1); });
            const auto serial = midiTakeSerial();
            int foreignCalls = 0;
            addBlocker(foreignCalls);
            stop.onClick();
            check(pendingMidiTake && pendingMidiTake->phase == PendingMidiTake::Phase::ready &&
                      pendingMidiTake->args.recordedNotes.size() == 1 && !recordArmed && !midiTaking &&
                      !audio.playing() && midiTakeSerial() == serial && foreignCalls == 1,
                  "Actual foreign-history failure retains one genuine acquired automation take");
            checkNote(pendingMidiTake->args.recordedNotes.front(), expected, false);
            // Explicit fixture repair removes only its own deliberately foreign
            // action. Production recovery never clears user history.
            controller.undo.clearUndoHistory();
            process(4); // Consume the genuinely routed NoteOff; never synthesize another NoteOn.
            exercise("retained_after_failed_stop", 5);
            checkNote(pendingMidiTake->args.recordedNotes.front(), expected, false);
            const auto beforeRetry = controller.project.state();
            const auto applied = recording("retry", serial);
            check(!bool(applied["isError"]) &&
                      applied["structuredContent"]["completionState"].toString() == "completed" &&
                      !hasPendingMidiTake() && !recordArmed && !midiTaking,
                  "Actual retained automation take retry completes once");
            const auto *notes = controller.project.pattern(expected.pattern);
            check(notes && notes->notes.size() == 1,
                  "Exactly one original-source note after automation recovery");
            checkNote(notes->notes.front(), expected, true);
            const auto recorded = controller.project.state();
            expectRecordingFailure("retry", serial, "take.stale");
            check(recorded.isEquivalentTo(controller.project.state()) &&
                      controller.undoEditChecked().wasOk() &&
                      beforeRetry.isEquivalentTo(controller.project.state()) &&
                      controller.redoEditChecked().wasOk() &&
                      recorded.isEquivalentTo(controller.project.state()),
                  "Retry one Undo/Redo retains all intervening automation sources, points and links");
            checkNote(controller.project.pattern(expected.pattern)->notes.front(), expected, true);
            completeCase(song ? "actual-Song-armed-held-retained-pan-separation-automation-source-point-"
                                "cancel-history-retry"
                              : "actual-Pattern-armed-held-retained-pan-separation-automation-source-point-"
                                "cancel-history-retry");
        }

        // Additive genuine group/take scenarios. Existing24 cases stay unchanged.
        // Reuse real Record/Play/Stop + source event clock, never seed a take.
        juce::String groupTakeTrace;
        for (const bool song : {false, true})
        {
            prepare(song);
            setSize(1920, 1044);
            check(selectMixerInsert(1).wasOk(), "Stable group capture source before arming");
            if (maximisedEditor != 3)
                maximiseEditor(3);
            const auto semanticGroup = [&](const char *id, juce::var args)
            {
                auto call = request("");
                call.getDynamicObject()->removeProperty("action");
                property(call, "command", id);
                property(call, "args", args);
                return data("vibera_command", call);
            };
            const auto expectGroupFailure = [&](const char *id, juce::var args, const char *code)
            {
                auto call = request("");
                call.getDynamicObject()->removeProperty("action");
                property(call, "command", id);
                property(call, "args", args);
                bool refused = false;
                try
                {
                    const auto response = invoke("vibera_command", call);
                    refused = bool(response["isError"]) &&
                              response["structuredContent"]["completionState"].toString() == "failed" &&
                              response["structuredContent"]["error"]["code"].toString() == code;
                }
                catch (const McpFailure &error)
                {
                    refused = error.code == code;
                }
                check(refused, "Actual shared group rejection exposes its expected typed code");
            };
            semanticGroup("mixer.mute", juce::JSON::parse(R"({"mixer":7,"enabled":true})"));
            check(audio.rebuild(controller.project, patternMode, selectedPattern).wasOk(),
                  "Pre-existing actual mute projection");
            needsRebuild = false;
            engineRevision = controller.revision;
            const auto sameGroupNotes = [](const std::vector<Note> &a, const std::vector<Note> &b)
            {
                if (a.size() != b.size())
                    return false;
                for (std::size_t i = 0; i < a.size(); ++i)
                    if (a[i].id != b[i].id || a[i].channel != b[i].channel || a[i].beat != b[i].beat ||
                        a[i].length != b[i].length || a[i].pitch != b[i].pitch ||
                        a[i].velocity != b[i].velocity || a[i].step != b[i].step || a[i].muted != b[i].muted)
                        return false;
                return true;
            };
            const auto exerciseGroups = [&](const char *phase)
            {
                const auto *owner = pendingMidiTake.get();
                const auto savedOwner = owner ? std::make_unique<PendingMidiTake>(*owner) : nullptr;
                const auto finished = midiTake.finishedNotes();
                const auto held = midiTake.heldCount();
                const auto failure = midiTake.failureCode();
                const auto serial = midiTakeSerial();
                const bool armed = recordArmed, taking = midiTaking, playing = audio.playing();
                const auto source = selectedChannel, part = selectedPattern;
                const auto session = audio.midiInput.capture.session.load();
                const auto captureActive = audio.midiInput.capture.active.load();
                const auto read = audio.midiInput.capture.read.load(),
                           write = audio.midiInput.capture.write.load();
                const auto projection = audio.projectionDiagnostics();
                const auto stages = audio.mixerGraphInitialisations();
                const int starts = device->starts, stops = device->stops;
                const auto looping = audio.looping();
                auto position = audio.positionBeat();
                MidiClock::Position clock;
                check(audio.midiInput.recordingPosition(clock), "Actual group-take source clock readable");
                check(!projection.channels.empty(), "Actual sampler source exists before group interaction");
                // The transport position is an observer the engine refreshes on a timer: while playing it can
                // catch up with blocks this fixture already pumped (seen as exactly 15 blocks, 0.16 beat).
                // Playing: it may only move forward, and the exact audio source clock compared below carries
                // the "nothing moved" claim. Stopped: it must not change at all.
                const auto positionHeld = [&]
                {
                    return playing ? audio.positionBeat() >= position : audio.positionBeat() == position;
                };
                const auto unchangedTake = [&]
                {
                    const auto now = audio.projectionDiagnostics();
                    MidiClock::Position after;
                    bool channels = now.channels.size() == projection.channels.size();
                    if (channels)
                        for (std::size_t i = 0; i < now.channels.size(); ++i)
                            channels &= now.channels[i].channel == projection.channels[i].channel &&
                                        now.channels[i].track == projection.channels[i].track &&
                                        now.channels[i].sampler == projection.channels[i].sampler &&
                                        now.channels[i].sample == projection.channels[i].sample &&
                                        now.channels[i].graphInitialisations ==
                                            projection.channels[i].graphInitialisations;
                    {
                        // Name the terms that changed, so a failure says what moved.
                        juce::StringArray moved;
                        const auto term = [&](bool same, const char *name)
                        {
                            if (!same)
                                moved.add(name);
                        };
                        term(pendingMidiTake.get() == owner && midiTakeSerial() == serial, "take owner/serial");
                        term(recordArmed == armed, "recordArmed");
                        term(midiTaking == taking, "midiTaking");
                        term(audio.playing() == playing, "playing");
                        term(selectedChannel == source && selectedPattern == part && selectedInsert == 1, "selection");
                        term(sameGroupNotes(midiTake.finishedNotes(), finished) && midiTake.heldCount() == held, "take notes");
                        term(audio.midiInput.capture.session.load() == session &&
                                 audio.midiInput.capture.active.load() == captureActive, "capture session");
                        term(audio.midiInput.capture.read.load() == read && audio.midiInput.capture.write.load() == write,
                             "capture read/write");
                        term(device->starts == starts && device->stops == stops, "device starts/stops");
                        term(now.edit == projection.edit && now.playbackContext == projection.playbackContext &&
                                 now.replacements == projection.replacements, "projection edit/context");
                        term(now.samplerCreations == projection.samplerCreations &&
                                 now.cache.preparations == projection.cache.preparations, "sampler/cache preparations");
                        term(now.midiUpdates == projection.midiUpdates, "midiUpdates");
                        term(channels, "projection channels");
                        term(audio.mixerGraphInitialisations() == stages, "mixer graph initialisations");
                        if (!positionHeld()) // Values help tell a jump from a clock drift.
                            moved.add("position " + juce::String(position, 9) + " -> " + juce::String(audio.positionBeat(), 9) +
                                      (playing ? " (playing)" : " (stopped)"));
                        term(audio.looping() == looping, "looping");
                        {
                            MidiClock::Position source;
                            term(audio.midiInput.recordingPosition(source) && source.seconds == clock.seconds &&
                                     source.playing == clock.playing && source.block == clock.block,
                                 "audio source clock");
                        }
                        if (!moved.isEmpty())
                            throw std::runtime_error(("Group operation preserves actual take/session/clock/sample/native/device "
                                                      "ownership; changed: " + moved.joinIntoString(", ")).toStdString());
                    }
                    check(
                        pendingMidiTake.get() == owner && midiTakeSerial() == serial &&
                            recordArmed == armed && midiTaking == taking && audio.playing() == playing &&
                            selectedChannel == source && selectedPattern == part && selectedInsert == 1 &&
                            sameGroupNotes(midiTake.finishedNotes(), finished) &&
                            midiTake.heldCount() == held && midiTake.failureCode() == failure &&
                            !midiTake.hasPendingEvent() &&
                            audio.midiInput.capture.session.load() == session &&
                            audio.midiInput.capture.active.load() == captureActive &&
                            audio.midiInput.capture.read.load() == read &&
                            audio.midiInput.capture.write.load() == write &&
                            !audio.midiInput.capture.failed.load() && device->starts == starts &&
                            device->stops == stops && now.edit == projection.edit &&
                            now.playbackContext == projection.playbackContext &&
                            now.replacements == projection.replacements &&
                            now.samplerCreations == projection.samplerCreations &&
                            now.cache.preparations == projection.cache.preparations &&
                            now.midiUpdates == projection.midiUpdates && channels &&
                            now.channels.front().heldInputKeys == projection.channels.front().heldInputKeys &&
                            audio.mixerGraphInitialisations() == stages && positionHeld() &&
                            audio.looping() == looping && audio.midiInput.recordingPosition(after) &&
                            after.seconds == clock.seconds && after.playing == clock.playing &&
                            after.block == clock.block,
                        "Group operation preserves actual take/session/clock/sample/native/device ownership");
                    if (owner)
                        check(owner->identity == savedOwner->identity &&
                                  owner->serial == savedOwner->serial && owner->phase == savedOwner->phase &&
                                  owner->error == savedOwner->error &&
                                  owner->args.pattern == savedOwner->args.pattern &&
                                  owner->args.channel == savedOwner->args.channel &&
                                  owner->args.target == savedOwner->args.target &&
                                  owner->args.beat == savedOwner->args.beat &&
                                  owner->args.enabled == savedOwner->args.enabled &&
                                  owner->placementBeat == savedOwner->placementBeat &&
                                  owner->placementOffset == savedOwner->placementOffset &&
                                  owner->stopSeconds == savedOwner->stopSeconds &&
                                  sameGroupNotes(owner->args.recordedNotes, savedOwner->args.recordedNotes),
                              "Group operation preserves the genuine pending take payload/placement/error");
                };
                const auto publish = [&](const char *step, bool requireAudible)
                {
                    unchangedTake();
                    check(audio.rebuild(controller.project, patternMode, selectedPattern).wasOk(),
                          "Actual group live-document projection");
                    needsRebuild = false;
                    engineRevision = controller.revision;
                    const auto before = audio.projectionDiagnostics();
                    const auto beforeSeconds = clock.seconds;
                    const auto peak = process(1);
                    bool finite = true;
                    for (int channel = 0; channel < block.getNumChannels(); ++channel)
                        for (int frame = 0; frame < block.getNumSamples(); ++frame)
                            finite &= std::isfinite(block.getSample(channel, frame));
                    MidiClock::Position after;
                    check(audio.midiInput.recordingPosition(after),
                          "Group callback source clock remains readable");
                    const auto now = audio.projectionDiagnostics();
                    groupTakeTrace += juce::String(song ? "Song" : "Pattern") + "," + phase + "," + step +
                                      ",serial=" + juce::String(juce::int64(serial)) +
                                      ",session=" + juce::String(juce::int64(session)) +
                                      ",armed=" + juce::String(int(recordArmed)) +
                                      ",taking=" + juce::String(int(midiTaking)) +
                                      ",held=" + juce::String(int(midiTake.heldCount())) +
                                      ",secondsBefore=" + juce::String(beforeSeconds, 15) +
                                      ",secondsAfter=" + juce::String(after.seconds, 15) +
                                      ",peak=" + juce::String(peak, 9) + ",sourceCalls=" +
                                      juce::String(juce::int64(
                                          now.channels.empty() ? 0
                                                               : now.channels.front().renderCalls -
                                                                     before.channels.front().renderCalls)) +
                                      "\n";
                    check(folder.getChildFile("mixer-groups-held-take.txt").replaceWithText(groupTakeTrace),
                          "Retain each real group/take callback result before assertion");
                    check(finite && (!taking || !requireAudible || peak > 1.e-6f) &&
                              now.channels.size() == before.channels.size() && !now.channels.empty() &&
                              now.channels.front().renderCalls == before.channels.front().renderCalls + 1 &&
                              std::abs(after.seconds - beforeSeconds - (playing ? 256. / 48000. : 0.)) <
                                  1.e-12,
                          "Actual group callback finite PCM/one source call/exact source-clock advance; mute "
                          "never resets voices");
                    clock = after;
                    position = audio.positionBeat();
                    unchangedTake();
                };
                const auto select = request("selectMany");
                auto selectArgs = select;
                property(selectArgs, "ids", juce::Array<juce::var>{1, 3});
                property(selectArgs, "active", 1);
                const auto selected = data("vibera_mixer", selectArgs);
                const auto *selectedIds = selected["organisation"]["selected"].getArray();
                check(selectedIds && selectedIds->size() == 2 && int((*selectedIds)[0]) == 1 &&
                          int((*selectedIds)[1]) == 3 && int(selected["organisation"]["active"]) == 1,
                      "Actual shared multi-selection keeps recording source independent");
                unchangedTake();
                const auto beforeCancel = controller.project.state();
                const auto dirty = controller.dirty;
                const auto undo = controller.undo.getUndoDescriptions(),
                           redo = controller.undo.getRedoDescriptions();
                performMixerControlAction("mixer.group.create");
                auto *dialog =
                    dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
                check(dialog && dialog->getTextEditor("name"),
                      "Actual UI group interface available while take is owned");
                dialog->getTextEditor("name")->setText("Cancelled genuine take group");
                dialog->exitModalState(0);
                dispatchMessages(20);
                check(beforeCancel.isEquivalentTo(controller.project.state()) && controller.dirty == dirty &&
                          controller.undo.getUndoDescriptions() == undo &&
                          controller.undo.getRedoDescriptions() == redo,
                      "Actual group dialog Cancel preserves exact project/dirty/history during take");
                unchangedTake();
                performMixerControlAction("mixer.group.create");
                dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
                check(dialog && dialog->getTextEditor("name"), "Actual group create reopens after Cancel");
                dialog->getTextEditor("name")->setText(juce::String(song ? "Song " : "Pattern ") + phase);
                dialog->exitModalState(1);
                dispatchMessages(20);
                check(controller.project.mixerGroups.size() > 0 &&
                          controller.undo.getUndoDescriptions().size() == undo.size() + 1,
                      "Actual UI group creation commits one history edit without finishing take");
                const auto group = controller.project.mixerGroups.back().id;
                check(controller.project.mixerGroups.back().members == std::vector<int>({1, 3}),
                      "Actual UI group retains exact stable members");
                const auto groupArgs = [&]
                {
                    auto args = request("");
                    args.getDynamicObject()->removeProperty("action");
                    property(args, "mixerGroup", group);
                    return args;
                };
                auto rename = groupArgs();
                property(rename, "text", juce::String(song ? "Song family " : "Pattern family ") + phase);
                const auto renamed = semanticGroup("mixer.group.rename", rename);
                check(renamed["completionScope"].toString() == "canonicalMixerOrganisation" &&
                          !bool(renamed["dspSettlingAcknowledged"]),
                      "Shared semantic group acknowledgement is canonical state, not heard or settled audio");
                publish("actual_ui_create_semantic_rename", true);
                const auto failedBaseline = controller.project.state();
                const auto failedRevision = controller.revision;
                const auto failedUndo = controller.undo.getUndoDescriptions(),
                           failedRedo = controller.undo.getRedoDescriptions();
                const auto failedDirty = controller.dirty;
                expectGroupFailure("mixer.group.create",
                                   juce::JSON::parse(R"({"ids":[0,1],"text":"Invalid Master group"})"),
                                   "invalid_arguments");
                check(failedBaseline.isEquivalentTo(controller.project.state()) &&
                          controller.revision == failedRevision && controller.dirty == failedDirty &&
                          controller.undo.getUndoDescriptions() == failedUndo &&
                          controller.undo.getRedoDescriptions() == failedRedo,
                      "Typed Master membership refusal preserves actual take and canonical state");
                unchangedTake();
                auto mute = groupArgs();
                property(mute, "enabled", true);
                semanticGroup("mixer.group.mute", mute);
                check(mixerInsert(controller.project, 1)->muted &&
                          mixerInsert(controller.project, 3)->muted &&
                          mixerInsert(controller.project, 7)->muted,
                      "Actual group mute preserves pre-existing unrelated mute");
                publish("semantic_group_mute",
                        false); // Intentional silence/ramp; no nonzero demand or voice reset.
                property(mute, "enabled", false);
                semanticGroup("mixer.group.mute", mute);
                publish("semantic_group_unmute", true);
                const auto beforeSolo = controller.project.state();
                auto solo = groupArgs();
                property(solo, "enabled", true);
                semanticGroup("mixer.group.solo", solo);
                check(controller.project.mixerSolo && controller.project.mixerSolo->group == group,
                      "Actual group solo owns restoration session during take");
                publish("semantic_group_solo", true);
                semanticGroup("mixer.mute", juce::JSON::parse(R"({"mixer":3,"enabled":true})"));
                check(!controller.project.mixerSolo && mixerInsert(controller.project, 3)->muted,
                      "Newer manual mute explicitly ends restore ownership");
                const auto manual = controller.project.state();
                expectGroupFailure("mixer.group.unsolo", groupArgs(), "mixer.group.soloOwner");
                check(manual.isEquivalentTo(controller.project.state()),
                      "Stale group restore never overwrites newer manual mute during take");
                check(controller.undoEditChecked().wasOk() && controller.project.mixerSolo &&
                          controller.project.mixerSolo->group == group,
                      "Actual manual-mute Undo restores its prior group solo owner");
                auto restore = groupArgs();
                property(restore, "restore", true);
                semanticGroup("mixer.group.unsolo", restore);
                check(beforeSolo.isEquivalentTo(controller.project.state()) &&
                          mixerInsert(controller.project, 7)->muted,
                      "Actual shared group restore retains exact pre-existing mute baseline during take");
                publish("manual_mute_undo_exact_restore", true);
                const auto beforeBusy = controller.project.state();
                const auto busyRevision = controller.revision;
                const auto busyDirty = controller.dirty;
                const auto busyUndo = controller.undo.getUndoDescriptions(),
                           busyRedo = controller.undo.getRedoDescriptions();
                bool busy = false;
                try
                {
                    const auto response = invoke(
                        "vibera_gesture", juce::JSON::parse(R"({"action":"begin","domain":"document"})"));
                    busy = bool(response["isError"]) &&
                           response["structuredContent"]["error"]["code"].toString() == "busy";
                }
                catch (const McpFailure &error)
                {
                    busy = error.code == "busy";
                }
                check(busy && !controller.hasGesture() &&
                          beforeBusy.isEquivalentTo(controller.project.state()) &&
                          controller.revision == busyRevision && controller.dirty == busyDirty &&
                          controller.undo.getUndoDescriptions() == busyUndo &&
                          controller.undo.getRedoDescriptions() == busyRedo,
                      "Existing document lease refuses armed/held/retained take without stealing it or "
                      "mutating history");
                unchangedTake();
                const auto beforeFinal = controller.project.state();
                const auto history = controller.undo.getUndoDescriptions().size();
                auto colour = groupArgs();
                property(colour, "value", juce::int64(0xff5ac8d8u));
                semanticGroup("mixer.group.colour", colour);
                check(controller.undo.getUndoDescriptions().size() == history + 1,
                      "Final group marker is exactly one independent history edit");
                publish("semantic_final_group_marker", true);
                return beforeFinal;
            };
            record.onClick();
            check(recordArmed && !midiTaking && !pendingMidiTake, "Genuine stopped armed group workflow");
            exerciseGroups("armed");
            stop.onClick();
            check(!recordArmed && !midiTaking && !pendingMidiTake,
                  "Armed group controls create no fabricated MIDI take");
            const auto expected = captureClosedNote(song, false, [&] { exerciseGroups("held_capture"); });
            const auto serial = midiTakeSerial();
            int foreignCalls = 0;
            addBlocker(foreignCalls);
            stop.onClick();
            check(pendingMidiTake && pendingMidiTake->phase == PendingMidiTake::Phase::ready &&
                      pendingMidiTake->args.recordedNotes.size() == 1 && midiTakeSerial() == serial &&
                      !recordArmed && !midiTaking && !audio.playing() && foreignCalls == 1,
                  "Actual foreign-history Stop failure retains exactly one genuinely acquired group-workflow "
                  "take");
            checkNote(pendingMidiTake->args.recordedNotes.front(), expected, false);
            // Fixture repair only removes its deliberately foreign action; product never clears user history.
            controller.undo.clearUndoHistory();
            process(4); // Consume original NoteOff, never replay NoteOn.
            const auto beforeFinalGroup = exerciseGroups("retained_after_failed_stop");
            checkNote(pendingMidiTake->args.recordedNotes.front(), expected, false);
            const auto beforeRetry = controller.project.state();
            const auto historyBeforeRetry = controller.undo.getUndoDescriptions().size();
            const auto applied = recording("retry", serial);
            check(!bool(applied["isError"]) &&
                      applied["structuredContent"]["completionState"].toString() == "completed" &&
                      !hasPendingMidiTake() && !recordArmed && !midiTaking &&
                      controller.undo.getUndoDescriptions().size() == historyBeforeRetry + 1,
                  "Actual group-workflow retry commits genuine recording exactly once after intervening "
                  "group history");
            const auto *part = controller.project.pattern(expected.pattern);
            check(part && part->notes.size() == 1,
                  "Exactly one original-source note survives all armed/held/retained group operations");
            checkNote(part->notes.front(), expected, true);
            const auto recorded = controller.project.state();
            expectRecordingFailure("retry", serial, "take.stale");
            check(recorded.isEquivalentTo(controller.project.state()) &&
                      controller.undoEditChecked().wasOk() &&
                      beforeRetry.isEquivalentTo(controller.project.state()) &&
                      controller.undoEditChecked().wasOk() &&
                      beforeFinalGroup.isEquivalentTo(controller.project.state()) &&
                      controller.redoEditChecked().wasOk() &&
                      beforeRetry.isEquivalentTo(controller.project.state()) &&
                      controller.redoEditChecked().wasOk() &&
                      recorded.isEquivalentTo(controller.project.state()),
                  "Actual recording and group edits have separate exact Undo/Redo preserving all groups, "
                  "membership and unrelated mutes");
            checkNote(controller.project.pattern(expected.pattern)->notes.front(), expected, true);
            completeCase(
                song ? "actual-Song-armed-held-retained-group-UI-MCP-mute-solo-restore-history-retry"
                     : "actual-Pattern-armed-held-retained-group-UI-MCP-mute-solo-restore-history-retry");
        }

        // Direct synthetic envelope setup exercises admission edges through REAL
        // shared handlers. It is deliberately not labelled new audio acquisition.
        auto seed = [&](bool song)
        {
            const auto serial = ++nextMidiTakeSerial;
            auto take = std::make_unique<PendingMidiTake>();
            take->identity = controller.projectIdentity;
            take->serial = serial;
            take->phase = PendingMidiTake::Phase::ready;
            take->args.pattern = controller.project.patterns.front().id;
            take->args.enabled = song;
            take->args.recordedNotes = {{0, controller.project.channels.front().id, .5, .25, 70, .6f, false}};
            if (song)
            {
                const auto &placement = controller.project.placements.front();
                take->args.target = placement.id;
                take->args.beat = placement.beat;
                take->placementBeat = placement.beat;
                take->placementOffset = placement.offset;
            }
            pendingMidiTake = std::move(take);
            midiTaking = false;
            recordArmed = false;
            return serial;
        };
        prepare(true);
        auto serial = seed(true);
        const auto source = controller.project;
        const auto sourceIdentity = controller.projectIdentity;
        controller.projectIdentity = juce::Uuid().toString();
        expectRecordingFailure("retry", serial, "take.source_changed");
        check(hasPendingMidiTake(), "Another session must not consume original take");
        controller.projectIdentity = sourceIdentity;
        std::erase_if(controller.project.placements,
                      [&](const auto &clip) { return clip.id == pendingMidiTake->args.target; });
        expectRecordingFailure("retry", serial, "take.placement_changed");
        check(controller.project.placements.empty(),
              "Missing original clip must not create replacement during retry");
        controller.project = source;
        const auto target = pendingMidiTake->args.pattern;
        std::erase_if(controller.project.patterns, [&](const auto &p) { return p.id == target; });
        expectRecordingFailure("retry", serial, "take.target_missing");
        controller.project = source;
        const auto originalChannel = pendingMidiTake->args.recordedNotes.front().channel;
        std::erase_if(controller.project.channels,
                      [&](const auto &channel) { return channel.id == originalChannel; });
        expectRecordingFailure("retry", serial, "take.target_missing");
        controller.project = source;
        auto invalid = request("retry");
        property(invalid, "serial", 0);
        bool invalidRefused = false;
        try
        {
            invoke("vibera_recording", invalid);
        }
        catch (const McpFailure &error)
        {
            invalidRefused = error.code == "invalid_arguments";
        }
        check(invalidRefused && midiTakeSerial() == serial && hasPendingMidiTake(),
              "Typed serial validation refuses invalid zero without changing ownership");
        selectedPattern = controller.project.patterns.back().id;
        selectedChannel = controller.project.channels.back().id;
        CommandArgs mix;
        mix.mixer = 1;
        mix.value = .61;
        command("mixer.gain", mix);
        check(hasPendingMidiTake() && controller.project.mixer[1].gain == .61f,
              "Safe Mixer change remains available with retained take");
        const auto immediatelyBeforeRetry = controller.project.state();
        const auto recovered = recording("retry", serial);
        check(!bool(recovered["isError"]) && controller.project.pattern(target)->notes.size() == 1 &&
                  controller.project.pattern(selectedPattern)->notes.empty() &&
                  controller.project.mixer[1].gain == .61f,
              "Retry uses original Pattern/Channel and preserves intervening Mixer edits despite selection "
              "changes");
        check(controller.undoEditChecked().wasOk() &&
                  immediatelyBeforeRetry.isEquivalentTo(controller.project.state()),
              "Take Undo preserves unrelated Mixer edit without blanket revision fence");
        completeCase("synthetic-envelope-source-target-placement-selection-mixer-guards");

        prepare(false);
        serial = seed(false);
        const auto beforeObserver = controller.project.state();
        int observerCalls = 0;
        bool sawApplying = false, retryRefused = false, cancelRefused = false;
        controller.changed = [&]
        {
            ++observerCalls;
            sawApplying = pendingMidiTake && pendingMidiTake->phase == PendingMidiTake::Phase::applying;
            retryRefused = retryMidiTake(serial).failed();
            cancelRefused = cancelPendingMidiTake(serial).failed();
            throw 73; // Heap-free deliberate observer failure; Controller must retain notification.
        };
        const auto observerResult = recording("retry", serial);
        check(!bool(observerResult["isError"]) && observerCalls == 1 && sawApplying && retryRefused &&
                  cancelRefused && !hasPendingMidiTake() &&
                  controller.project.patterns.front().notes.size() == 1 &&
                  controller.hasPendingControllerNotification(),
              "Postcommit observer throw/reentry cannot lose or duplicate recovered take");
        const auto afterObserver = controller.project.state();
        controller.changed = normalObserver;
        check(controller.deliverPendingNotifications() &&
                  afterObserver.isEquivalentTo(controller.project.state()),
              "Observer delivery retry does not replay notes/history");
        check(controller.undoEditChecked().wasOk() &&
                  beforeObserver.isEquivalentTo(controller.project.state()),
              "Observer failure does not prevent exact one-Undo recovery");
        completeCase("actual-controller-observer-throw-retry-and-recording-reentry");

        prepare(false);
        serial = seed(false);
        const auto beforeCancel = controller.project.state();
        const auto beforeDirty = controller.dirty;
        const auto cancelled = recording("cancel", serial);
        check(!bool(cancelled["isError"]) &&
                  cancelled["structuredContent"]["completionState"].toString() == "cancelled" &&
                  !hasPendingMidiTake() && !controller.undo.canUndo() && controller.dirty == beforeDirty &&
                  beforeCancel.isEquivalentTo(controller.project.state()),
              "Actual semantic cancel preserves model/history/dirtiness");
        serial = seed(false);
        pendingMidiTake->phase = PendingMidiTake::Phase::sealing;
        pendingMidiTake->args.recordedNotes.clear();
        midiTake.begin(51, 0, 0, 120, 0);
        midiTake.add({controller.project.channels.front().id, 0, 51, 1, {0x90, 60, 100}});
        midiTake.markInputFailure();
        expectRecordingFailure("retry", serial, "take.invalid_capture");
        check(midiTake.heldCount() == 1 && hasPendingMidiTake() &&
                  pendingMidiTake->phase == PendingMidiTake::Phase::invalidCapture &&
                  beforeCancel.isEquivalentTo(controller.project.state()),
              "Invalid capture keeps held diagnostic owner and never claims a partial committed take");
        check(!bool(recording("cancel", serial)["isError"]), "Invalid retained take explicitly cancels");
        completeCase("synthetic-envelope-cancel-exact-and-invalid-capture-diagnostics");

        // A genuine original WAV and actual metadata worker, not a fake successful
        // import. The source waveform is generated entirely by this fixture.
        prepare(false);
        captureClosedNote(false);
        int importForeign = 0;
        addBlocker(importForeign);
        check(audioImports.request({wav, controller.projectIdentity, 2, 0, 1}),
              "Actual metadata worker accepts original WAV");
        const auto importDeadline = juce::Time::getMillisecondCounter() + 5000;
        while (!audioImportAfterTake && juce::Time::getMillisecondCounter() < importDeadline)
        {
            pollAudioImports();
            process(1);
        }
        check(audioImportAfterTake && hasPendingMidiTake() && audio.playing() &&
                  controller.project.audioPlacements.empty(),
              "Completed import job stays owned behind refused active take");
        const auto *ownedImport = &*audioImportAfterTake;
        pollAudioImports();
        check(audioImportAfterTake && &*audioImportAfterTake == ownedImport && !pollingAudioImports,
              "Pending take and full-method guard cannot move/clear import owner");
        controller.undo.clearUndoHistory();
        check(retryMidiTake(midiTakeSerial()).wasOk(),
              "Import-blocking take retry succeeds after explicit fixture unblock");
        int importReentries = 0;
        controller.changed = [&]
        {
            ++importReentries;
            pollAudioImports();
            if (normalObserver)
                normalObserver();
        };
        pollAudioImports();
        controller.changed = normalObserver;
        check(importReentries == 1 && !audioImportAfterTake && !pollingAudioImports &&
                  controller.project.audioPlacements.size() == 1 &&
                  controller.project.patterns.front().notes.size() == 1,
              "Actual import commits once and nested poll cannot consume/reset same owner");
        pollAudioImports();
        check(controller.project.audioPlacements.size() == 1, "Completed import is never blindly replayed");
        completeCase("actual-import-worker-retained-take-owner-and-command-reentry");

        // Synthetic completed-job envelope, using the SAME genuine original WAV:
        // exercise reentry while outer poll is still finishing the active take,
        // before the import command changes phase to applying.
        prepare(false);
        captureClosedNote(false);
        audioImportAfterTake = AudioClipImporter::Job{wav, controller.projectIdentity, 2, .01, 1};
        audioImportPhase = DeferredAudioImportPhase::pending;
        int finishReentries = 0;
        controller.changed = [&]
        {
            ++finishReentries;
            pollAudioImports();
            if (normalObserver)
                normalObserver();
        };
        pollAudioImports();
        controller.changed = normalObserver;
        check(finishReentries == 2 && !hasPendingMidiTake() && !audioImportAfterTake &&
                  !pollingAudioImports && controller.project.audioPlacements.size() == 1 &&
                  controller.project.patterns.front().notes.size() == 1,
              "Full poll ownership survives take-finalization observer before applying and later import "
              "observer");
        completeCase("synthetic-completed-import-envelope-before-command-reentry");

        check(receipt(true, {}), "Successful Studio verification receipt must be written");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        receipt(false, error.what());
        return juce::Result::fail(error.what());
    }
    catch (...)
    {
        receipt(false, "Unexpected nonstandard fixture failure");
        return juce::Result::fail("Unexpected MIDI recovery fixture failure");
    }
}
} // namespace vibera
