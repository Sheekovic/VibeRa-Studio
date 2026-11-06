// SPDX-License-Identifier: AGPL-3.0-or-later
// Audio-input recording: input selection and monitoring, take lifecycle around the
// transport, and placement of the finished WAV as an undoable Playlist Audio Clip.
#include "Studio.h"
#include "TakeLanes.h"
#include "McpSchema.h"
#include "AudioStretch.h"
#include "AudioFileCheck.h"
#include "SamplerTempo.h"
#include "VideoPlayer.h"

namespace vibera
{
namespace
{
juce::var obj()
{
    return juce::var(new juce::DynamicObject);
}
void set(juce::var &o, const char *k, const juce::var &v)
{
    o.getDynamicObject()->setProperty(k, v);
}
juce::File audioInputSettingsFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("VibeRa Studio/audio-input.json");
}
} // namespace

void Studio::loadAudioInputSettings(bool persistent)
{
    persistentAudioInput = persistent;
    if (!persistentAudioInput)
        return;
    audioTakeIndex = audioInputSettingsFile().getSiblingFile("active-takes.json");
    scanRecoveredTakes();
    const auto prefs = juce::JSON::parse(audioInputSettingsFile());
    if (!prefs.isObject())
        return;
    AudioInputSettings next;
    next.enabled = bool(prefs["enabled"]);
    next.monitor = bool(prefs["monitor"]);
    next.input = juce::jlimit(0, 31, int(prefs["input"]));
    next.channels = int(prefs["channels"]) == 1 ? 1 : 2;
    next.monitorGain = prefs.hasProperty("monitorGain") ? juce::jlimit(0.f, 1.5f, float(prefs["monitorGain"])) : 1.f;
    next.tracks = prefs.hasProperty("tracks") ? juce::jlimit(1, 8, int(prefs["tracks"])) : 1;
    next.insert = juce::jlimit(0, 125, int(prefs["insert"]));
    // A device without the saved inputs keeps the preference; readiness reports why.
    if (applyAudioInput(next).failed())
        audioInput = next;
}

juce::Result Studio::toggleInsertRecordArm(int insert)
{
    if (insert <= 0 || insert > 125)
        return juce::Result::fail("Record arm works on Mixer inserts 1..125");
    auto next = audioInput;
    if (insertRecordArmed(insert))
        next.insert = 0; // Disarm: takes go to Playlist clips again.
    else
    {
        next.enabled = true;
        next.insert = insert;
    }
    auto result = applyAudioInput(next);
    if (result.wasOk())
    {
        saveAudioInputSettings();
        status = next.insert > 0 ? "Insert " + juce::String(insert) + " armed: recording goes into it"
                                 : juce::String("Insert disarmed: recording goes to Playlist clips");
    }
    else
        status = result.getErrorMessage();
    repaint();
    return result;
}

void Studio::saveAudioInputSettings() const
{
    if (!persistentAudioInput)
        return;
    auto value = obj();
    set(value, "version", 1);
    set(value, "enabled", audioInput.enabled);
    set(value, "monitor", audioInput.monitor);
    set(value, "input", audioInput.input);
    set(value, "channels", audioInput.channels);
    set(value, "monitorGain", audioInput.monitorGain);
    set(value, "tracks", audioInput.tracks);
    set(value, "insert", audioInput.insert);
    audioInputSettingsFile().getParentDirectory().createDirectory();
    audioInputSettingsFile().replaceWithText(juce::JSON::toString(value));
}

// Index of the selected device input among the device's active inputs, which is how
// the callback receives them; -1 when any selected input is not active.
int Studio::audioInputCompactIndex() const
{
    auto *device = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    if (!device)
        return -1;
    const auto active = device->getActiveInputChannels();
    for (int c = 0; c < audioInput.width(); ++c)
        if (!active[audioInput.input + c])
            return -1;
    int index = 0;
    for (int bit = 0; bit < audioInput.input; ++bit)
        index += active[bit] ? 1 : 0;
    return index;
}

juce::String Studio::audioInputReadiness() const
{
    if (!audioInput.enabled)
        return "disabled";
    auto *device = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    if (!device || !audio.inputCapture.device().running)
        return "noDevice";
    if (audioInputCompactIndex() < 0)
        return "noInput";
    return "ready";
}

// Applies preferences; opening exactly the selected inputs restarts the device once
// (a bounded interruption), so it is refused during playback and takes.
juce::Result Studio::applyAudioInput(const AudioInputSettings &next)
{
    if (next.channels != 1 && next.channels != 2)
        return juce::Result::fail("audioInput.channels: Choose mono (1) or stereo (2)");
    if (next.input < 0 || next.input > 31 || !std::isfinite(next.monitorGain) || next.monitorGain < 0 ||
        next.monitorGain > 1.5f || next.tracks < 1 || next.tracks > 8 || next.input + next.width() > 32 ||
        next.insert < 0 || next.insert > 125)
        return juce::Result::fail("audioInput.range: input 0..31, tracks 1..8 within 32 inputs, insert 0..125, "
                                  "monitorGain 0..1.5");
    const bool inputsChange = next.enabled && (next.input != audioInput.input || next.channels != audioInput.channels ||
                                               next.tracks != audioInput.tracks || !audioInput.enabled);
    if (inputsChange && (audio.playing() || audioTaking))
        return juce::Result::fail("audioInput.busy: Stop playback before changing the recording input");
    auto &manager = audio.engine.getDeviceManager().deviceManager;
    auto *device = manager.getCurrentAudioDevice();
    if (next.enabled && device)
    {
        const auto names = device->getInputChannelNames();
        if (next.input + next.width() > names.size())
            return juce::Result::fail("audioInput.noInput: " + device->getName() + " has " +
                                      juce::String(names.size()) + " input channel(s)");
        auto setup = manager.getAudioDeviceSetup();
        juce::BigInteger wanted;
        wanted.setRange(next.input, next.width(), true);
        if ((setup.inputChannels & wanted) != wanted || setup.useDefaultInputChannels)
        {
            setup.useDefaultInputChannels = false;
            setup.inputChannels = wanted;
            const auto error = manager.setAudioDeviceSetup(setup, true);
            if (error.isNotEmpty())
                return juce::Result::fail("audioInput.device: " + error);
        }
    }
    audioInput = next;
    audio.inputCapture.monitorChannels = audioInput.channels;
    audio.inputCapture.monitorFirstInput = juce::jmax(0, audioInputCompactIndex());
    audio.inputCapture.monitorGain = audioInput.monitorGain;
    audio.inputCapture.monitor = audioInput.enabled && audioInput.monitor;
    audioInputButton.setToggleState(audioInput.enabled, juce::dontSendNotification);
    audioInputButton.channels = audioInput.channels;
    saveAudioInputSettings();
    return juce::Result::ok();
}

// The sidecar records what a crashed session would otherwise lose: where the take sits.
void Studio::writeAudioTakeSidecar(std::optional<double> startEdit)
{
    auto value = obj();
    set(value, "version", 1);
    set(value, "file", audioTake.file.getFullPathName());
    set(value, "project", projectFile.getFullPathName());
    set(value, "tempo", controller.project.tempo);
    set(value, "mapped", startEdit.has_value());
    set(value, "startSeconds", startEdit.value_or(0.));
    // The Song beat through the tempo map: recovery places the take there even when the
    // Song has tempo changes (older sidecars fall back to the constant tempo).
    set(value, "startBeat", juce::jmax(0., controller.project.songBeat(startEdit.value_or(0.))));
    audioTakeSidecar().replaceWithText(juce::JSON::toString(value));
    if (audioTakeIndex == juce::File{})
        return;
    auto index = juce::JSON::parse(audioTakeIndex);
    juce::Array<juce::var> takes;
    if (const auto *rows = index["takes"].getArray())
        takes = *rows;
    if (!takes.contains(audioTakeSidecar().getFullPathName()))
        takes.add(audioTakeSidecar().getFullPathName());
    auto next = obj();
    set(next, "takes", takes);
    audioTakeIndex.getParentDirectory().createDirectory();
    audioTakeIndex.replaceWithText(juce::JSON::toString(next));
}

void Studio::forgetAudioTakeSidecar(const juce::File &take)
{
    const auto sidecar = take.withFileExtension("take.json");
    sidecar.deleteFile();
    if (audioTakeIndex == juce::File{} || !audioTakeIndex.existsAsFile())
        return;
    auto index = juce::JSON::parse(audioTakeIndex);
    juce::Array<juce::var> takes;
    if (const auto *rows = index["takes"].getArray())
        for (const auto &row : *rows)
            if (row.toString() != sidecar.getFullPathName())
                takes.add(row);
    auto next = obj();
    set(next, "takes", takes);
    audioTakeIndex.replaceWithText(juce::JSON::toString(next));
}

// Index entries whose sidecar and WAV still exist belong to takes a crash interrupted.
void Studio::scanRecoveredTakes()
{
    recoveredTakes.clear();
    if (audioTakeIndex == juce::File{} || !audioTakeIndex.existsAsFile())
        return;
    const auto index = juce::JSON::parse(audioTakeIndex);
    if (const auto *rows = index["takes"].getArray())
        for (const auto &row : *rows)
        {
            const auto sidecar = juce::JSON::parse(juce::File(row.toString()));
            const juce::File file(sidecar["file"].toString());
            if (!sidecar.isObject() || !juce::File::isAbsolutePath(file.getFullPathName()) || !file.existsAsFile())
                continue;
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader(
                wav.createReaderFor(file.createInputStream().release(), true));
            if (!reader || reader->lengthInSamples <= 0 || reader->sampleRate <= 0)
                continue; // Nothing was flushed before the crash: there is nothing to recover.
            RecoveredTake take;
            take.file = file;
            take.seconds = double(reader->lengthInSamples) / reader->sampleRate;
            take.mapped = bool(sidecar["mapped"]);
            const double tempo = double(sidecar["tempo"]) > 0 ? double(sidecar["tempo"]) : 120.;
            take.beat = !take.mapped                    ? 0.
                        : sidecar.hasProperty("startBeat") ? juce::jmax(0., double(sidecar["startBeat"]))
                                                           : juce::jmax(0., double(sidecar["startSeconds"]) * tempo / 60.);
            take.project = sidecar["project"].toString();
            recoveredTakes.push_back(take);
        }
    if (!recoveredTakes.empty())
        status = juce::String(int(recoveredTakes.size())) +
                 " audio take(s) were interrupted; right-click the microphone button to recover them";
}

// Places a recovered take where it was recorded (at the playhead when it never got that
// far) on the first free track, as one Undo step, and forgets its index entry.
juce::Result Studio::placeRecoveredTake(const juce::File &file)
{
    const auto found = std::find_if(recoveredTakes.begin(), recoveredTakes.end(),
                                    [&](const auto &take) { return take.file == file; });
    if (found == recoveredTakes.end())
        return juce::Result::fail("audioTake.recovery: No such recovered take");
    if (audioTaking || devicePreparationBusy() || routingPending() || hasPendingMidiTake() ||
        controller.hasGesture())
        return juce::Result::fail("audioTake.busy: Finish the current take or edit first");
    const auto &project = controller.project;
    const double beat = found->mapped ? found->beat : audio.positionBeat();
    const double to = project.audioEndBeat(beat, found->seconds);
    int lane = 0;
    while (lane < 127 &&
           (std::any_of(project.placements.begin(), project.placements.end(),
                        [&](const auto &p) { return p.lane == lane && p.beat < to && p.beat + p.length > beat; }) ||
            std::any_of(project.audioPlacements.begin(), project.audioPlacements.end(),
                        [&](const auto &p)
                        { return p.lane == lane && p.beat < to && project.audioEndBeat(p.beat, p.duration) > beat; })))
        ++lane;
    CommandArgs args;
    args.text = found->file.getFullPathName();
    args.beat = beat;
    args.lane = lane;
    args.length = found->seconds;
    if (auto result = controller.execute("audio.add", args); result.failed())
        return result;
    forgetAudioTakeSidecar(found->file);
    status = "Recovered " + found->file.getFileName() + " on Track " + juce::String(lane + 1);
    recoveredTakes.erase(found);
    refresh();
    return juce::Result::ok();
}

// MP3 needs the LAME encoder installed by the user (it is not bundled): the
// VIBERA_LAME_EXECUTABLE file, or lame.exe on PATH.
static juce::File findLame()
{
    const auto configured = juce::SystemStats::getEnvironmentVariable("VIBERA_LAME_EXECUTABLE", {});
    if (configured.isNotEmpty())
        return juce::File(configured).existsAsFile() ? juce::File(configured) : juce::File{};
    for (const auto &folder :
         juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("PATH", {}), ";", "\""))
        if (folder.isNotEmpty() && juce::File::isAbsolutePath(folder))
            if (const auto lame = juce::File(folder).getChildFile("lame.exe"); lame.existsAsFile())
                return lame;
    return {};
}

// One stem per Mixer insert a Channel feeds directly, named "NN Insert name.wav".
// With armedOnly, one stem per Mixer track armed for rendering instead (any track, buses too).
std::vector<std::pair<int, juce::File>> Studio::stemTargets(const Project &project, const juce::File &folder,
                                                            bool armedOnly) const
{
    std::vector<std::pair<int, juce::File>> stems;
    for (const auto &insert : project.mixer)
        if (armedOnly ? renderArmedInserts.contains(insert.index)
                      : insert.index > 0 &&
                            std::any_of(project.channels.begin(), project.channels.end(),
                                        [&](const auto &channel) { return channel.mixer == insert.index; }))
            stems.push_back({insert.index, folder.getChildFile(juce::File::createLegalFileName(
                                               juce::String(insert.index).paddedLeft('0', 2) + " " +
                                               insert.name + ".wav"))});
    return stems;
}

juce::Result Studio::queueExport(const Project &snapshot, const juce::File &target, bool stems)
{
    const bool armedOnly = std::exchange(stemsArmedOnly, false);
    if (exportBusy())
        return juce::Result::fail("Finish the running export first");
    exportQueue.clear();
    exportOutputs.clear();
    if (!stems)
    {
        if (target.hasFileExtension("mp3") && findLame() == juce::File{})
            return juce::Result::fail("MP3 export needs the LAME encoder: install lame.exe on PATH or set "
                                      "VIBERA_LAME_EXECUTABLE (it is not bundled with VibeRa)");
        const bool encoded = target.hasFileExtension("flac;ogg;mp3");
        exportQueue.push_back({snapshot, encoded ? target.getSiblingFile(target.getFileNameWithoutExtension() +
                                                                         ".render.wav")
                                                 : target,
                               target});
    }
    else
    {
        if (!target.createDirectory())
            return juce::Result::fail("Cannot create the stems folder " + target.getFullPathName());
        for (const auto &[insert, file] : stemTargets(snapshot, target, armedOnly))
        {
            // The canonical "solo connected" keeps whatever feeds or receives this insert.
            Controller solo;
            solo.replace(snapshot);
            CommandArgs args;
            args.mixer = insert;
            if (auto result = solo.execute("mixer.soloConnected", args); result.failed())
                return result;
            exportQueue.push_back({solo.project, file, file});
        }
        if (exportQueue.empty())
            return juce::Result::fail(armedOnly ? "No Mixer track is armed for rendering"
                                                : "No Mixer insert is fed by a Channel: nothing to export as stems");
    }
    startNextExport();
    return juce::Result::ok();
}

void Studio::startNextExport()
{
    exportCurrent = std::move(exportQueue.front());
    exportQueue.pop_front();
    pendingExport = exportCurrent->snapshot;
    pendingExportFile = exportCurrent->render;
}

void Studio::finishExportStep(juce::Result rendered)
{
    auto fail = [this](const juce::Result &result)
    {
        if (exportCurrent && exportCurrent->render != exportCurrent->target)
            exportCurrent->render.deleteFile();
        exportQueue.clear();
        exportCurrent.reset();
        status = result.getErrorMessage();
        completeMcpOperation(mcpExportOperation, result);
        mcpExportOperation.clear();
        completeRenderClip(result);
    };
    if (rendered.failed())
        return fail(rendered);
    if (exportCurrent->render != exportCurrent->target)
    {
        // Encode on a worker; the timer collects the outcome.
        exportEncoding = std::make_unique<ExportEncoding>();
        auto *job = exportEncoding.get();
        const auto source = exportCurrent->render, target = exportCurrent->target;
        status = "Encoding " + target.getFileName() + "...";
        job->thread = std::thread(
            [job, source, target]
            {
                job->result = [&]
                {
                    juce::WavAudioFormat wav;
                    std::unique_ptr<juce::AudioFormatReader> reader(
                        wav.createReaderFor(source.createInputStream().release(), true));
                    if (!reader)
                        return juce::Result::fail("Cannot read the rendered WAV");
                    const bool flac = target.hasFileExtension("flac"), mp3 = target.hasFileExtension("mp3");
                    std::unique_ptr<juce::AudioFormat> format;
                    if (flac)
                        format = std::make_unique<juce::FlacAudioFormat>();
                    else if (mp3)
                        format = std::make_unique<juce::LAMEEncoderAudioFormat>(findLame());
                    else
                        format = std::make_unique<juce::OggVorbisAudioFormat>();
                    const auto qualities = format->getQualityOptions();
                    int quality = flac ? 0 : juce::jmax(0, qualities.indexOf("256 kbps"));
                    if (mp3)
                        for (int q = 0; q < qualities.size(); ++q)
                            if (qualities[q].contains("320"))
                                quality = q;
                    const auto part = target.getSiblingFile(target.getFileName() + ".part");
                    part.deleteFile();
                    std::unique_ptr<juce::AudioFormatWriter> writer(
                        format->createWriterFor(part.createOutputStream().release(), reader->sampleRate,
                                                reader->numChannels, flac ? 24 : 16, {}, quality));
                    if (!writer)
                        return juce::Result::fail("Cannot open the " + juce::String(flac ? "FLAC" : mp3 ? "MP3" : "OGG") + " encoder");
                    const bool written = writer->writeFromAudioReader(*reader, 0, -1);
                    writer.reset();
                    if (!written || !part.moveFileTo(target))
                    {
                        part.deleteFile();
                        return juce::Result::fail("Writing " + target.getFileName() + " failed");
                    }
                    return juce::Result::ok();
                }();
                job->done = true;
            });
        return;
    }
    exportOutputs.add(exportCurrent->target.getFullPathName());
    exportCurrent.reset();
    if (!exportQueue.empty())
        return startNextExport();
    status = exportOutputs.size() > 1 ? "Exported " + juce::String(exportOutputs.size()) + " stems"
                                      : "Exported " + juce::File(exportOutputs[0]).getFileName();
    completeMcpOperation(mcpExportOperation, juce::Result::ok());
    mcpExportOperation.clear();
    completeRenderClip(juce::Result::ok()); // Render-to-clip requests place their audio clip now.
}

void Studio::pollExportEncoding()
{
    if (!exportEncoding || !exportEncoding->done.load())
        return;
    exportEncoding->thread.join();
    const auto result = exportEncoding->result;
    exportEncoding.reset();
    if (exportCurrent)
        exportCurrent->render.deleteFile(); // The intermediate WAV.
    if (result.failed())
    {
        exportQueue.clear();
        exportCurrent.reset();
        status = result.getErrorMessage();
        completeMcpOperation(mcpExportOperation, result);
        mcpExportOperation.clear();
        return;
    }
    if (exportCurrent)
        exportCurrent->render = exportCurrent->target; // Encoded: the step is complete.
    finishExportStep(juce::Result::ok());
}

void Studio::exportMenu()
{
    if (exportBusy() || pendingExport || audio.rendering())
    {
        // While exporting, the export button offers to cancel the render.
        juce::PopupMenu running;
        running.addItem(1, "Cancel the running export", audio.rendering() || pendingExport.has_value());
        running.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportWav),
                              [this](int chosen)
                              {
                                  if (chosen == 1 && !cancelExport())
                                      status = "The export is encoding and finishes shortly";
                              });
        return;
    }
    juce::PopupMenu menu;
    const auto format = juce::String(exportRate / 1000., 1).replace(".0", "") + " kHz, " +
                        (exportBits == 32 ? juce::String("32-bit float") : juce::String(exportBits) + "-bit");
    menu.addItem(1, "Song as WAV (" + format + ")...");
    menu.addItem(2, "Song as FLAC (lossless, 24-bit)...");
    menu.addItem(3, "Song as OGG Vorbis (256 kbps)...");
    menu.addItem(5, findLame() != juce::File{} ? "Song as MP3 (320 kbps, LAME)..."
                                               : "Song as MP3 (install LAME first)...",
                 findLame() != juce::File{});
    menu.addSeparator();
    menu.addItem(4, "Stems: one WAV per Mixer insert...");
    juce::PopupMenu quality;
    quality.addItem(40, "44.1 kHz", true, exportRate == 44100);
    quality.addItem(41, "48 kHz", true, exportRate == 48000);
    quality.addSeparator();
    quality.addItem(42, "16-bit", true, exportBits == 16);
    quality.addItem(43, "24-bit", true, exportBits == 24);
    quality.addItem(44, "32-bit float", true, exportBits == 32);
    menu.addSubMenu("Sample rate / bit depth (" + format + ")", quality);
    juce::PopupMenu tail;
    tail.addItem(45, "Cut remainder (stop at the Song end)", true, exportTail == 0);
    tail.addItem(46, "Leave remainder (keep effect tails)", true, exportTail == 1);
    tail.addItem(47, "Wrap remainder (tails onto the start, for loops)", true, exportTail == 2);
    menu.addSubMenu("Tail", tail);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&exportWav),
                       [this](int chosen)
                       {
                           if (chosen <= 0)
                               return;
                           if (chosen >= 45 && chosen <= 47)
                           {
                               exportTail = chosen - 45;
                               const char *names[]{"cut", "leave", "wrap"};
                               status = juce::String("Export tail: ") + names[exportTail] + " remainder";
                               return;
                           }
                           if (chosen >= 40 && chosen <= 44)
                           {
                               if (chosen <= 41)
                                   exportRate = chosen == 40 ? 44100 : 48000;
                               else
                                   exportBits = chosen == 42 ? 16 : chosen == 43 ? 24 : 32;
                               status = "Export format: " + juce::String(exportRate / 1000., 1) + " kHz, " +
                                        juce::String(exportBits) + "-bit";
                               return;
                           }
                           const juce::String extension =
                               chosen == 2 ? "flac" : chosen == 3 ? "ogg" : chosen == 5 ? "mp3" : "wav";
                           const auto base = projectFile == juce::File{}
                                                 ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                       .getChildFile("VibeRa")
                                                 : projectFile.withFileExtension("");
                           if (chosen == 4)
                           {
                               chooser = std::make_unique<juce::FileChooser>(
                                   "Export stems into folder", base.getParentDirectory());
                               chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                                        juce::FileBrowserComponent::canSelectDirectories,
                                                    [this](const juce::FileChooser &fc)
                                                    {
                                                        if (fc.getResult() != juce::File{})
                                                            exportTo(fc.getResult(), 0, true);
                                                    });
                               return;
                           }
                           chooser = std::make_unique<juce::FileChooser>(
                               "Export Song " + extension.toUpperCase(), base.withFileExtension(extension),
                               "*." + extension);
                           chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                                    juce::FileBrowserComponent::canSelectFiles |
                                                    juce::FileBrowserComponent::warnAboutOverwriting,
                                                [this, extension](const juce::FileChooser &fc)
                                                {
                                                    if (fc.getResult() != juce::File{})
                                                        exportTo(fc.getResult().withFileExtension(extension));
                                                });
                       });
}

// Stretches a clip's source window by `ratio` (output/input length) into a new file next to
// the project, then points the clip at it as one Undo step. Pitch is preserved.
juce::Result Studio::stretchAudioClip(Id clip, double ratio, const juce::String &operation)
{
    if (stretchJob)
        return juce::Result::fail("A stretch is already running");
    const auto found = std::find_if(controller.project.audioPlacements.begin(), controller.project.audioPlacements.end(),
                                    [&](const auto &p) { return p.id == clip; });
    if (found == controller.project.audioPlacements.end())
        return juce::Result::fail("Select an Audio Clip to stretch");
    const auto *source = controller.project.channel(found->channel);
    if (!source || !(ratio >= .25 && ratio <= 4))
        return juce::Result::fail("Stretch ratio is 0.25..4");
    const auto folder = projectFile != juce::File{}
                            ? projectFile.getParentDirectory().getChildFile("Stretched")
                            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                  .getChildFile("VibeRa Studio/Stretched");
    if (!folder.createDirectory())
        return juce::Result::fail("Cannot create " + folder.getFullPathName());
    auto job = std::make_unique<StretchJob>();
    job->clip = clip;
    job->seconds = found->duration * ratio;
    job->project = controller.projectIdentity;
    job->operation = operation;
    job->target = folder.getChildFile(juce::File(source->source).getFileNameWithoutExtension() + " x" +
                                      juce::String(ratio, 3) + ".wav")
                      .getNonexistentSibling(false);
    auto *raw = job.get();
    const juce::File input(source->source);
    const double offset = found->offset, duration = found->duration;
    job->thread = std::thread([raw, input, offset, duration, ratio]
                              {
                                  raw->result = stretchAudioFile(input, offset, duration, ratio, raw->target);
                                  raw->done = true;
                              });
    stretchJob = std::move(job);
    status = "Stretching " + source->name + " (keeps pitch)...";
    return juce::Result::ok();
}

// Warps a clip recorded at `sourceBpm` onto the Song tempo map: each source beat lands on
// the matching Song beat from the clip start, so the clip follows jumps and ramps. Rendered
// offline (pitch kept) and applied like a stretch, as one Undo step.
juce::Result Studio::warpAudioClip(Id clip, double sourceBpm, const juce::String &operation)
{
    if (stretchJob)
        return juce::Result::fail("An audio clip job is already running");
    const auto found = std::find_if(controller.project.audioPlacements.begin(), controller.project.audioPlacements.end(),
                                    [&](const auto &p) { return p.id == clip; });
    if (found == controller.project.audioPlacements.end())
        return juce::Result::fail("Select an Audio Clip to warp");
    const auto *source = controller.project.channel(found->channel);
    if (!source || !(found->duration > 0))
        return juce::Result::fail("The clip has no source");
    if (!(sourceBpm >= 10 && sourceBpm <= 522))
        return juce::Result::fail("Source tempo is 10..522 BPM");
    // A clip warped before renders again from its original source, never from a warp.
    const auto *original = found->warpBpm > 0 ? controller.project.channel(found->warpChannel) : nullptr;
    if (original && juce::File::isAbsolutePath(original->source))
        source = original;
    const double offset = original ? found->warpOffset : found->offset;
    const double duration = original ? found->warpDuration : found->duration;
    // (source seconds, output seconds) every 1/16 beat of the source.
    const auto &project = controller.project;
    const double beats = duration * sourceBpm / 60., zero = project.songSeconds(found->beat);
    std::vector<std::pair<double, double>> map{{0., 0.}};
    for (double b = .0625; b < beats; b += .0625)
        map.emplace_back(b * 60. / sourceBpm, project.songSeconds(found->beat + b) - zero);
    map.emplace_back(duration, project.songSeconds(found->beat + beats) - zero);
    const auto folder = projectFile != juce::File{}
                            ? projectFile.getParentDirectory().getChildFile("Stretched")
                            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                  .getChildFile("VibeRa Studio/Stretched");
    if (!folder.createDirectory())
        return juce::Result::fail("Cannot create " + folder.getFullPathName());
    auto job = std::make_unique<StretchJob>();
    job->clip = clip;
    job->seconds = map.back().second;
    job->project = controller.projectIdentity;
    job->operation = operation;
    job->warpBpm = sourceBpm;
    job->target = folder.getChildFile(juce::File(source->source).getFileNameWithoutExtension() + " warped.wav")
                      .getNonexistentSibling(false);
    auto *raw = job.get();
    const juce::File input(source->source);
    job->thread = std::thread([raw, input, offset, duration, map]
                              {
                                  raw->result = warpAudioFile(input, offset, duration, map, raw->target);
                                  raw->done = true;
                              });
    stretchJob = std::move(job);
    status = "Warping " + source->name + " to the Song tempo...";
    return juce::Result::ok();
}

// Re-warps clips whose tempo map or position changed since their last warp, one job at a
// time, after edits settle for 0.8 s. Each re-render is its own Undo step; undoing it does
// not re-trigger (the signature it was made for is remembered).
void Studio::followTempoWarps()
{
    if (stretchJob || audioTaking)
        return;
    const auto &project = controller.project;
    juce::String tempo(project.tempo, 6);
    for (const auto &change : project.tempoChanges)
        tempo << ";" << juce::String(change.beat, 6) << "," << juce::String(change.bpm, 6) << (change.ramp ? "r" : "");
    const AudioPlacement *due = nullptr;
    juce::String dueSignature;
    for (const auto &clip : project.audioPlacements)
    {
        if (!(clip.warpBpm > 0) || !project.channel(clip.warpChannel))
            continue;
        const auto signature = tempo + "@" + juce::String(clip.beat, 6) + "/" + juce::String(clip.warpBpm, 6);
        const auto known = warpSignatures.find(clip.id);
        if (known == warpSignatures.end())
        {
            // First sight (e.g. after opening): trust a clip whose length already matches.
            const double beats = clip.warpDuration * clip.warpBpm / 60.;
            const double expected = project.songSeconds(clip.beat + beats) - project.songSeconds(clip.beat);
            if (std::abs(expected - clip.duration) < .002)
            {
                warpSignatures[clip.id] = signature;
                continue;
            }
        }
        else if (known->second == signature)
            continue;
        due = &clip;
        dueSignature = signature;
        break;
    }
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.;
    if (!due)
    {
        rewarpPendingSince = 0;
        return;
    }
    if (rewarpPendingSince == 0)
        rewarpPendingSince = now;
    if (now - rewarpPendingSince < .8)
        return;
    rewarpPendingSince = 0;
    warpSignatures[due->id] = dueSignature; // Also on failure: never retry the same input in a loop.
    if (auto started = warpAudioClip(due->id, due->warpBpm); started.failed())
        status = "Re-warp: " + started.getErrorMessage();
    else
        status = "Re-warping a clip to the new tempo...";
}

double Studio::detectFileTempo(const juce::File &file, juce::String &how) const
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(decodableAudioFile(file) ? formats.createReaderFor(file) : nullptr);
    if (!reader || reader->sampleRate <= 0 || reader->lengthInSamples < 64)
        return 0;
    const double seconds = double(reader->lengthInSamples) / reader->sampleRate;
    if (seconds >= 4.5)
    {
        Sample sample;
        sample.sampleRate = reader->sampleRate;
        const int frames = int(std::min<juce::int64>(reader->lengthInSamples, juce::int64(reader->sampleRate * 120)));
        sample.data.setSize(int(std::min(2u, reader->numChannels)), frames);
        reader->read(&sample.data, 0, frames, 0, true, sample.data.getNumChannels() > 1);
        if (const double bpm = estimateSampleTempo(sample); bpm > 0)
        {
            how = "rhythm analysis";
            return bpm;
        }
    }
    // Loops are a whole number of beats: choose the count whose tempo is plausible and
    // nearest the Song tempo.
    const double song = controller.project.tempo;
    double best = 0, distance = 1e9;
    int bestBeats = 0;
    for (int beats : {1, 2, 4, 8, 16, 32, 64}) // Loops are powers of two in beats.
    {
        const double bpm = beats * 60. / seconds;
        if (bpm < 70 || bpm > 180)
            continue;
        if (std::abs(bpm - song) < distance)
        {
            distance = std::abs(bpm - song);
            best = bpm;
            bestBeats = beats;
        }
    }
    if (best > 0)
        how = "length (" + juce::String(bestBeats) + " beats)";
    return best;
}
juce::Result Studio::fitAudioClipToTempo(Id clip, double sourceBpm)
{
    const auto found = std::find_if(controller.project.audioPlacements.begin(), controller.project.audioPlacements.end(),
                                    [&](const auto &p) { return p.id == clip; });
    if (found == controller.project.audioPlacements.end())
        return juce::Result::fail("Select an Audio Clip");
    const auto *channel = controller.project.channel(found->warpBpm > 0 && controller.project.channel(found->warpChannel)
                                                         ? found->warpChannel
                                                         : found->channel);
    if (!channel)
        return juce::Result::fail("The clip has no source");
    juce::String how = "your setting";
    if (!(sourceBpm > 0))
        sourceBpm = detectFileTempo(juce::File(channel->source), how);
    if (!(sourceBpm > 0))
        return juce::Result::fail("Could not detect the tempo; use Set tempo... with the BPM it was made at");
    if (auto started = warpAudioClip(clip, sourceBpm); started.failed())
        return started;
    status = "Fitting to the Song tempo from " + juce::String(sourceBpm, 1) + " BPM (" + how + "), pitch kept...";
    return juce::Result::ok();
}

juce::Result Studio::editAudioClip(Id clip, const juce::String &edit, double value, const juce::String &operation)
{
    if (stretchJob)
        return juce::Result::fail("An audio clip job is already running");
    const auto found = std::find_if(controller.project.audioPlacements.begin(), controller.project.audioPlacements.end(),
                                    [&](const auto &p) { return p.id == clip; });
    if (found == controller.project.audioPlacements.end())
        return juce::Result::fail("Select an Audio Clip to edit");
    if (edit == "pitch" && (!std::isfinite(value) || value == 0 || std::abs(value) > 24))
        return juce::Result::fail("Pitch is -24..24 semitones (nonzero)");
    const auto *source = controller.project.channel(found->channel);
    if (!source)
        return juce::Result::fail("The clip has no source");
    const auto folder = projectFile != juce::File{}
                            ? projectFile.getParentDirectory().getChildFile("Edited")
                            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                  .getChildFile("VibeRa Studio/Edited");
    if (!folder.createDirectory())
        return juce::Result::fail("Cannot create " + folder.getFullPathName());
    auto job = std::make_unique<StretchJob>();
    job->clip = clip;
    job->seconds = found->duration;
    job->bakedFades = edit == "consolidate";
    job->project = controller.projectIdentity;
    job->operation = operation;
    job->target = folder.getChildFile(juce::File(source->source).getFileNameWithoutExtension() + " " + edit + ".wav")
                      .getNonexistentSibling(false);
    auto *raw = job.get();
    const juce::File input(source->source);
    const double offset = found->offset, duration = found->duration, in = found->fadeIn, out = found->fadeOut;
    job->thread = std::thread([raw, input, offset, duration, edit, value, in, out]
                              {
                                  raw->result = editAudioFile(input, offset, duration, edit, value, in, out, raw->target);
                                  raw->done = true;
                              });
    stretchJob = std::move(job);
    status = "Processing " + source->name + " (" + edit + ")...";
    return juce::Result::ok();
}

// Launch (on) or stop (off) a Pattern. Launching starts Song playback if needed; the clip
// lands on the next bar on its own performance-zone lane and repeats until stopped.
juce::Result Studio::launchPattern(Id pattern, bool on)
{
    if (!on)
    {
        if (performancePads.erase(pattern) == 0)
            return juce::Result::fail("That Pattern is not launched");
        status = "Stopped " + (controller.project.pattern(pattern) ? controller.project.pattern(pattern)->name : juce::String());
        return juce::Result::ok();
    }
    const auto *part = controller.project.pattern(pattern);
    if (!part)
        return juce::Result::fail("No such Pattern");
    if (patternMode)
        return juce::Result::fail("Performance mode launches into the Song: switch to Song mode");
    if (performancePads.count(pattern))
        return juce::Result::ok();
    std::set<int> used;
    for (const auto &[id, pad] : performancePads)
        used.insert(pad.lane);
    int lane = performance::firstLane;
    while (used.count(lane) && lane < 127)
        ++lane;
    const double bar = controller.project.numerator * 4. / controller.project.denominator;
    const double quantum = part->launchQuantum < 0 ? bar : part->launchQuantum;
    performance::Pad pad;
    pad.pattern = pattern;
    pad.lane = lane;
    const double now = std::max(0., audio.positionBeat());
    // "One bar" follows the Song's time signature at that point (including changes).
    pad.nextBeat = !audio.playing()                                         ? now
                   : part->launchQuantum < 0 && !controller.project.meterChanges.empty() ? controller.project.nextBarLine(now)
                                                                          : performance::nextBoundary(now, quantum);
    performancePads[pattern] = pad;
    if (!audio.playing())
        play.onClick();
    pollPerformance();
    status = "Launched " + part->name + " on Track " + juce::String(lane + 1);
    return juce::Result::ok();
}

void Studio::pollPerformance()
{
    if (performancePads.empty())
        return;
    if (!audio.playing() || patternMode)
    {
        performancePads.clear(); // Stopping the transport ends the performance.
        return;
    }
    for (const auto &clip : performance::due(performancePads, controller.project, audio.positionBeat(), 1.))
    {
        CommandArgs args;
        args.pattern = clip.pattern;
        args.lane = clip.lane;
        args.beat = clip.beat;
        args.length = clip.length;
        command("clip.add", args);
    }
}

juce::var Studio::performanceState() const
{
    auto o = obj();
    juce::Array<juce::var> pads;
    for (const auto &[id, pad] : performancePads)
    {
        auto row = obj();
        set(row, "pattern", juce::int64(pad.pattern));
        set(row, "lane", pad.lane);
        set(row, "nextBeat", pad.nextBeat);
        pads.add(row);
    }
    set(o, "launched", pads);
    juce::Array<juce::var> layout; // Pad n = n-th Pattern; quantum -1 is one bar.
    for (size_t i = 0; i < controller.project.patterns.size() && i < 16; ++i)
    {
        auto row = obj();
        set(row, "pad", int(i) + 1);
        set(row, "pattern", juce::int64(controller.project.patterns[i].id));
        set(row, "quantum", controller.project.patterns[i].launchQuantum);
        layout.add(row);
    }
    set(o, "pads", layout);
    set(o, "playing", audio.playing());
    return o;
}

// Pad n (performance.padN, MIDI-learnable on the Controller page) toggles the n-th Pattern.
bool Studio::togglePerformancePad(int index)
{
    const auto id = controller.project.padPattern(index);
    if (id == 0)
    {
        status = "Performance pad " + juce::String(index + 1) + " has no Pattern";
        return false;
    }
    if (auto r = launchPattern(id, !performancePads.count(id)); r.failed())
    {
        status = r.getErrorMessage();
        return false;
    }
    return true;
}

// A pad per Pattern: click to launch at its quantum (one bar by default), click again to
// stop; right-click picks the quantum, saved with the Pattern.
void Studio::showPerformance()
{
    struct Pads final : juce::Component, private juce::Timer
    {
        Studio &s;
        juce::OwnedArray<juce::TextButton> buttons;
        explicit Pads(Studio &owner) : s(owner)
        {
            stopAll.setTooltip("Stop every launched Pattern (performance.stopAll; MIDI-learnable)");
            stopAll.onClick = [this] { s.performAction("performance.stopAll"); };
            addAndMakeVisible(stopAll);
            scenes.setTooltip("Launch a scene (performance.scene1..8, MIDI-learnable) or save the launched Patterns");
            scenes.onClick = [this] { scenesMenu(); };
            addAndMakeVisible(scenes);
            setSize(520, 300);
            startTimerHz(10);
        }
        void rebuild()
        {
            // 16 fixed pads; each launches its assigned Pattern (default: the n-th Pattern).
            buttons.clear();
            for (int index = 0; index < 16; ++index)
            {
                const auto id = s.controller.project.padPattern(index);
                const auto *part = s.controller.project.pattern(id);
                auto *b = buttons.add(new juce::TextButton(part ? part->name : juce::String("Pad ") + juce::String(index + 1)));
                b->setClickingTogglesState(false);
                b->setAlpha(part ? 1.f : .45f);
                b->setTooltip("Pad " + juce::String(index + 1) + (part ? ": launch / stop " + part->name : juce::String(": empty")) +
                              " (right-click: assign a Pattern, launch quantum; bind a MIDI pad on the Controller page)");
                b->onClick = [this, index]
                {
                    if (juce::ModifierKeys::currentModifiers.isPopupMenu())
                        return padMenu(index);
                    s.togglePerformancePad(index);
                    s.repaint();
                };
                addAndMakeVisible(b);
            }
            layoutSignature = signature();
            resized();
        }
        juce::String layoutSignature;
        juce::String signature() const
        {
            juce::String text;
            for (int index = 0; index < 16; ++index)
                if (const auto *part = s.controller.project.pattern(s.controller.project.padPattern(index)))
                    text << part->name << "|";
                else
                    text << "-|";
            return text;
        }
        void padMenu(int index)
        {
            juce::PopupMenu menu, assign;
            const auto &project = s.controller.project;
            assign.addItem(1000, "Default (Pattern " + juce::String(index + 1) + ")", true, project.padPatterns[size_t(index)] == 0);
            for (size_t i = 0; i < project.patterns.size() && i < 500; ++i)
                assign.addItem(1001 + int(i), project.patterns[i].name, true,
                               project.padPatterns[size_t(index)] == project.patterns[i].id);
            menu.addSubMenu("Assign Pattern to pad " + juce::String(index + 1), assign);
            const auto id = project.padPattern(index);
            menu.addItem(2, "Launch quantum...", id != 0);
            menu.showMenuAsync(juce::PopupMenu::Options(),
                               [this, index, id](int chosen)
                               {
                                   if (chosen == 2)
                                       return quantumMenu(id);
                                   if (chosen < 1000)
                                       return;
                                   CommandArgs a;
                                   a.value = index + 1;
                                   a.pattern = chosen == 1000 ? 0 : s.controller.project.patterns[size_t(chosen - 1001)].id;
                                   s.command("performance.assignPad", a);
                               });
        }
        void quantumMenu(Id id)
        {
            const auto *part = s.controller.project.pattern(id);
            if (!part)
                return;
            juce::PopupMenu menu;
            const std::array<std::pair<double, const char *>, 6> choices{
                {{-1, "One bar (default)"}, {0, "Immediately"}, {.5, "1/2 beat"}, {1, "1 beat"}, {8, "2 bars"},
                 {16, "4 bars"}}};
            for (size_t i = 0; i < choices.size(); ++i)
                menu.addItem(int(i) + 1, choices[i].second, true, part->launchQuantum == choices[i].first);
            menu.showMenuAsync(juce::PopupMenu::Options(),
                               [this, id, choices](int chosen)
                               {
                                   if (chosen <= 0)
                                       return;
                                   CommandArgs args;
                                   args.pattern = id;
                                   args.value = choices[size_t(chosen - 1)].first;
                                   s.command("pattern.launchQuantum", args);
                               });
        }
        juce::TextButton stopAll{"Stop all"}, scenes{"Scenes"};
        void scenesMenu()
        {
            juce::PopupMenu menu, save;
            const auto &list = s.controller.project.performanceScenes;
            for (int i = 0; i < 8; ++i)
            {
                const auto count = int(list[size_t(i)].size());
                menu.addItem(1 + i, "Launch scene " + juce::String(i + 1) +
                                        (count ? " (" + juce::String(count) + " Patterns)" : juce::String(" (empty)")),
                             count > 0);
                save.addItem(11 + i, "Scene " + juce::String(i + 1));
            }
            menu.addSubMenu("Save launched Patterns as", save, !s.performancePads.empty());
            menu.showMenuAsync(juce::PopupMenu::Options(),
                               [this](int chosen)
                               {
                                   if (chosen >= 1 && chosen <= 8)
                                       s.performAction("performance.scene" + juce::String(chosen));
                                   else if (chosen >= 11)
                                   {
                                       CommandArgs a;
                                       a.value = chosen - 10;
                                       for (const auto &[id, pad] : s.performancePads)
                                           a.ids.push_back(id);
                                       s.command("performance.saveScene", a);
                                   }
                               });
        }
        void resized() override
        {
            stopAll.setBounds(getWidth() - 98, getHeight() - 38, 90, 30);
            scenes.setBounds(getWidth() - 196, getHeight() - 38, 90, 30);
            const int columns = 4, size = std::max(60, (getWidth() - 10) / columns - 6);
            for (int i = 0; i < buttons.size(); ++i)
                buttons[i]->setBounds(8 + (i % columns) * (size + 6), 8 + (i / columns) * 54, size, 48);
        }
        void timerCallback() override
        {
            if (signature() != layoutSignature)
                rebuild();
            for (int i = 0; i < buttons.size(); ++i)
                buttons[i]->setToggleState(s.performancePads.count(s.controller.project.padPattern(i)) > 0,
                                           juce::dontSendNotification);
        }
    };
    if (!performanceWindow)
    {
        performanceWindow = std::make_unique<juce::DocumentWindow>("Performance", panel,
                                                                   juce::DocumentWindow::closeButton, true);
        performanceWindow->setUsingNativeTitleBar(false);
        performanceWindow->setTitleBarHeight(30);
        performanceWindow->setResizable(true, true);
        auto *pads = new Pads(*this);
        pads->rebuild();
        performanceWindow->setContentOwned(pads, true);
        performanceWindow->centreWithSize(540, 320);
    }
    performanceWindow->setVisible(true);
    performanceWindow->toFront(true);
}

juce::Result Studio::openVideo(const juce::File &file)
{
    if (!file.existsAsFile())
        return juce::Result::fail("Video file not found: " + file.getFullPathName());
    if (!videoWindow)
        videoWindow = std::make_unique<VideoPlayerWindow>(
            [safe = juce::Component::SafePointer<Studio>(this)]
            {
                if (safe)
                    juce::MessageManager::callAsync([safe] { if (safe) safe->closeVideo(); });
            });
    auto result = videoWindow->open(file);
    if (result.failed())
    {
        videoWindow.reset();
        return juce::Result::fail("Cannot play " + file.getFileName() + ": " + result.getErrorMessage());
    }
    status = "Video follows the transport: " + file.getFileName();
    return juce::Result::ok();
}

void Studio::closeVideo() { videoWindow.reset(); }

juce::var Studio::videoState() const
{
    auto o = obj();
    set(o, "open", videoWindow != nullptr && videoWindow->decoder.isOpen());
    if (videoWindow)
    {
        set(o, "file", videoWindow->path.getFullPathName());
        set(o, "duration", videoWindow->decoder.duration());
        set(o, "position", videoWindow->decoder.position());
        set(o, "offset", videoWindow->offset);
    }
    return o;
}

void Studio::pollStretchJob()
{
    if (!stretchJob || !stretchJob->done.load())
        return;
    stretchJob->thread.join();
    auto job = std::move(stretchJob);
    auto result = job->result;
    if (result.wasOk() && job->project != controller.projectIdentity)
        result = juce::Result::fail("The project changed during stretching; nothing was applied");
    if (result.wasOk())
    {
        CommandArgs args;
        args.target = job->clip;
        args.text = job->target.getFullPathName();
        args.length = job->seconds;
        args.step = job->bakedFades;
        args.warpBpm = job->warpBpm;
        result = controller.execute("audio.stretchApply", args);
        if (result.wasOk())
            refresh();
    }
    status = result.wasOk() ? "Stretched clip ready (" + job->target.getFileName() + ")" : result.getErrorMessage();
    if (job->operation.isNotEmpty())
        completeMcpOperation(job->operation, result);
}

juce::File Studio::nextAudioTakeFile() const
{
    const auto folder = projectFile != juce::File{}
                            ? projectFile.getParentDirectory().getChildFile("Recorded")
                            : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                  .getChildFile("VibeRa Studio/Recorded");
    const auto name = "Take " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M%S") + ".wav";
    return folder.getChildFile(name).getNonexistentSibling(false);
}

void Studio::beginAudioTake()
{
    if (!audioInput.enabled || audioTaking)
        return;
    auto refuse = [this](const juce::String &code, const juce::String &message)
    {
        audioTake.phase = "failed";
        audioTake.code = code;
        audioTake.message = message;
        status = message;
    };
    if (patternMode)
        return refuse("audioTake.patternMode", "Audio takes record in Song mode: switch to Song to record audio");
    if (devicePreparationBusy() || routingPending())
        return refuse("audioTake.busy", "Audio take not started: wait for the device/effect change to finish");
    const auto readiness = audioInputReadiness();
    if (readiness != "ready")
        return refuse("audioTake." + readiness, readiness == "noDevice"
                                                    ? "Audio take not started: no running audio device"
                                                    : "Audio take not started: enable the selected input in Settings > Audio");
    AudioInputCapture::TakeRequest request;
    request.file = nextAudioTakeFile();
    request.channels = audioInput.width();
    request.firstInput = audioInputCompactIndex();
    request.startEdit = controller.project.songSeconds(audio.positionBeat());
    if (auto result = audio.inputCapture.begin(request); result.failed())
        return refuse(result.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                      result.getErrorMessage().fromFirstOccurrenceOf(": ", false, false));
    audioTaking = true;
    audioTakeLoop = audio.loopSeconds(); // Loop mode cannot change while the take runs.
    audioTake = {};
    audioTake.serial = ++audioTakeSerial;
    audioTake.phase = "capturing";
    audioTake.file = request.file;
    audioTakeTracks = audioInput.tracks;
    audioTakeSidecarMapped = false;
    writeAudioTakeSidecar(std::nullopt);
    status = "Recording audio input | Stop keeps the take; Esc cancels";
}

juce::Result Studio::splitAudioTake(const juce::File &source, int tracks, std::vector<juce::File> &parts)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(source.createInputStream().release(), true));
    if (!reader || tracks < 2 || int(reader->numChannels) % tracks != 0)
        return juce::Result::fail("Cannot split the recorded inputs into tracks");
    const int width = int(reader->numChannels) / tracks;
    std::vector<std::unique_ptr<juce::AudioFormatWriter>> writers;
    parts.clear();
    for (int t = 0; t < tracks; ++t)
    {
        const auto file = source.getSiblingFile(source.getFileNameWithoutExtension() + "-in" + juce::String(t + 1) + ".wav");
        file.deleteFile();
        auto stream = std::make_unique<juce::FileOutputStream>(file);
        std::unique_ptr<juce::AudioFormatWriter> writer;
        if (stream->openedOk())
            writer.reset(wav.createWriterFor(stream.get(), reader->sampleRate, unsigned(width), 24, {}, 0));
        if (!writer)
        {
            for (const auto &part : parts)
                part.deleteFile();
            return juce::Result::fail("Cannot write " + file.getFullPathName());
        }
        stream.release();
        writers.push_back(std::move(writer));
        parts.push_back(file);
    }
    constexpr int chunk = 65536;
    juce::AudioBuffer<float> block(int(reader->numChannels), chunk);
    bool ok = true;
    for (juce::int64 at = 0; ok && at < reader->lengthInSamples; at += chunk)
    {
        const int size = int(std::min<juce::int64>(chunk, reader->lengthInSamples - at));
        ok = reader->read(&block, 0, size, at, true, true);
        for (int t = 0; ok && t < tracks; ++t)
        {
            juce::AudioBuffer<float> view(block.getArrayOfWritePointers() + t * width, width, size);
            ok = writers[size_t(t)]->writeFromAudioSampleBuffer(view, 0, size);
        }
    }
    writers.clear(); // Flush and close.
    if (!ok)
    {
        for (const auto &part : parts)
            part.deleteFile();
        return juce::Result::fail("Splitting the recorded inputs failed (disk full?)");
    }
    return juce::Result::ok();
}

void Studio::cancelAudioTake()
{
    if (!audioTaking)
        return;
    audioTaking = false;
    audio.inputCapture.cancel();
    forgetAudioTakeSidecar(audioTake.file);
    audioTake.phase = "cancelled";
    audioTake.message = "Audio take cancelled; nothing was placed";
    status = audioTake.message;
}

void Studio::finishAudioTake()
{
    if (!audioTaking)
        return;
    audioTaking = false;
    const auto result = audio.inputCapture.finish();
    forgetAudioTakeSidecar(audioTake.file); // Sealed: the take no longer needs recovery.
    using O = AudioInputCapture::Outcome;
    audioTake.seconds = result.rate > 0 ? double(result.frames) / result.rate : 0;
    audioTake.droppedSeconds = result.rate > 0 ? double(result.dropped) / result.rate : 0;
    audioTake.latencyMs = result.latencySeconds * 1000.;
    audioTake.interrupted = result.interrupted;
    const auto gap = result.dropped > 0 ? juce::String(" (truncated: ") + juce::String(audioTake.droppedSeconds, 2) +
                                              " s not captured" + (result.interrupted ? ", device restarted)" : ")")
                                        : juce::String();
    switch (result.outcome)
    {
    case O::completed:
        audioTake.files = {audioTake.file};
        if (audioTakeTracks > 1)
        {
            std::vector<juce::File> parts;
            if (auto split = splitAudioTake(audioTake.file, audioTakeTracks, parts); split.failed())
            {
                audioTake.phase = "failed";
                audioTake.code = "audioTake.split";
                audioTake.message = split.getErrorMessage() + " (the recording is kept at " +
                                    audioTake.file.getFullPathName() + ")";
                audioTake.file = juce::File{};
                status = audioTake.message;
                return;
            }
            audioTake.file.deleteFile();
            audioTake.files = parts;
            audioTake.file = parts.front();
        }
        audioTake.phase = "placing";
        audioTake.beat = juce::jmax(0., controller.project.songBeat(result.startEdit));
        // Through a looping transport every pass becomes its own take.
        audioTakePasses = loopPasses(juce::jmax(0., result.startEdit), audioTake.seconds,
                                     audioTakeLoop ? audioTakeLoop->first : 0.,
                                     audioTakeLoop ? audioTakeLoop->second : 0.);
        if (playlistRange)
        {
            // Punch: keep only what falls inside the Playlist time selection. The file keeps
            // everything, so trimming the clips later can reveal more of the take.
            const double in = controller.project.songSeconds(playlistRange->first),
                         out = controller.project.songSeconds(playlistRange->second);
            std::vector<TakePass> punched;
            for (auto pass : audioTakePasses)
            {
                const double from = std::max(pass.edit, in), to = std::min(pass.edit + pass.duration, out);
                if (to - from < .01)
                    continue;
                pass.offset += from - pass.edit;
                pass.duration = to - from;
                pass.edit = from;
                punched.push_back(pass);
            }
            audioTakePasses = std::move(punched);
            if (audioTakePasses.empty())
            {
                audioTake.phase = "empty";
                audioTake.code = "audioTake.outsidePunch";
                audioTake.message = "Nothing was recorded inside the punch range (Playlist time selection)" + gap;
                for (const auto &file : audioTake.files)
                    file.deleteFile();
                audioTake.file = juce::File{};
                audioTake.files.clear();
                status = audioTake.message;
                return;
            }
            audioTake.beat = controller.project.songBeat(audioTakePasses.front().edit);
        }
        audioTake.passes = int(audioTakePasses.size());
        audioTake.message = "Recorded " + juce::String(audioTake.seconds, 1) + " s" +
                            (audioTake.passes > 1 ? " in " + juce::String(audioTake.passes) + " loop takes" : "") +
                            gap;
        audioTakePlacementPending = true;
        placeAudioTake();
        return;
    case O::empty:
        audioTake.phase = "empty";
        audioTake.code = "audioTake.empty";
        audioTake.message = "Nothing was recorded after the start position" + gap;
        break;
    case O::unmapped:
        audioTake.phase = "unplaced";
        audioTake.code = "audioTake.unplaced";
        audioTake.message = "The transport never played during the take; nothing was placed" + gap;
        break;
    case O::failed:
        audioTake.phase = "failed";
        audioTake.code = result.error.upToFirstOccurrenceOf(":", false, false);
        audioTake.message = result.error.fromFirstOccurrenceOf(": ", false, false) + gap;
        break;
    }
    audioTake.file = juce::File{};
    status = audioTake.message;
}

// Places a sealed take (one clip per loop pass, stacked on consecutive Playlist tracks
// free over their spans) as one "Place recorded takes" history step. The newest pass
// plays; earlier passes are muted takes to comp from. Retried while edits are busy.
void Studio::placeAudioTake()
{
    if (!audioTakePlacementPending)
        return;
    if (devicePreparationBusy() || routingPending() || hasPendingMidiTake() || midiTaking ||
        controller.hasGesture() || mixerChainApplying() || automationRecordingActive())
        return;
    audioTakePlacementPending = false;
    const auto &project = controller.project;
    // Passes of one input stack on consecutive tracks; each input track gets its own block.
    // Audio tracks are preferred (TakeLanes.h).
    const int count = juce::jmax(1, int(audioTakePasses.size()));
    const int tracks = juce::jmax(1, int(audioTake.files.size()));
    const int lanes = count * tracks;
    std::vector<std::pair<double, double>> spans;
    for (const auto &pass : audioTakePasses)
        spans.push_back({project.songBeat(pass.edit), project.songBeat(pass.edit + pass.duration)});
    const int lane = chooseTakeLane(project, spans, tracks);
    CommandArgs args;
    args.text = audioTake.file.getFullPathName();
    args.length = audioTake.seconds;
    // Into the chosen Mixer insert(s); a missing one falls back to Master and says so.
    juce::String insertNote;
    args.mixer = audioInput.insert;
    for (int t = 0; t < tracks && args.mixer > 0; ++t)
        if (std::none_of(project.mixer.begin(), project.mixer.end(),
                         [&](const auto &insert) { return insert.index == audioInput.insert + t; }))
        {
            insertNote = " | Mixer insert " + juce::String(audioInput.insert + t) + " missing: routed to Master";
            args.mixer = 0;
        }
    if (tracks > 1)
        for (const auto &file : audioTake.files)
            args.takeFiles.push_back(file.getFullPathName());
    for (int t = 0; t < tracks; ++t)
        for (int i = 0; i < count; ++i)
        {
            const auto &pass = audioTakePasses[size_t(i)];
            AudioPlacement window;
            window.channel = Id(t);
            window.beat = juce::jmax(0., project.songBeat(pass.edit));
            window.lane = juce::jmin(127, lane + t * count + i);
            window.offset = pass.offset;
            window.duration = pass.duration;
            window.muted = i + 1 < count;
            args.audioTakes.push_back(window);
        }
    const auto firstNewId = controller.project.nextId;
    if (auto result = controller.execute("audio.takes", args); result.failed())
    {
        audioTake.phase = "failed";
        audioTake.code = "audioTake.place";
        audioTake.message = result.getErrorMessage() + " (the recording is kept at " +
                            audioTake.file.getFullPathName() + ")";
        status = audioTake.message;
        return;
    }
    audioTake.lane = lane;
    for (const auto &clip : controller.project.audioPlacements)
        if (clip.id >= firstNewId)
        {
            audioTake.clips.push_back(clip.id);
            audioTake.clip = clip.id; // The newest pass, which plays.
            audioTake.channel = clip.channel;
        }
    for (const auto &group : controller.project.clipGroups)
        if (group.id >= firstNewId)
            audioTake.group = group.id;
    audioTake.phase = "completed";
    if (audioTake.clip)
    {
        selectedClips = {audioTake.clip};
        selectedChannel = audioTake.channel;
        focus = Scope::playlist;
    }
    audioTake.message += insertNote;
    status = audioTake.message + (lanes > 1 ? " on Tracks " + juce::String(lane + 1) + "-" + juce::String(lane + lanes) +
                                                  (count > 1 ? " | right-click a take: Use this take here" : "")
                                            : " on Track " + juce::String(lane + 1));
    refresh();
}

void Studio::pollAudioTake()
{
    // Any route that stops the transport (button, MCP, end of song) seals the take.
    if (audioTaking && !audioTakeSidecarMapped)
        if (const auto start = audio.inputCapture.mappedStartEdit())
        {
            writeAudioTakeSidecar(start);
            audioTakeSidecarMapped = true;
        }
    if (audioTaking && !audio.playing())
        finishAudioTake();
    placeAudioTake();
    if (audioInput.enabled)
    {
        const auto peaks = audio.inputCapture.takePeaks();
        bool changed = false;
        for (size_t c = 0; c < 2; ++c)
        {
            // Instant attack, ~300 ms release at the timer rate.
            const float next = std::max(peaks[c], audioInputLevel[c] * .82f);
            changed |= std::abs(next - audioInputLevel[c]) > 1.e-4f;
            audioInputLevel[c] = next < 1.e-4f ? 0.f : next;
        }
        if (changed)
        {
            audioInputButton.level = audioInputLevel;
            audioInputButton.repaint();
        }
    }
}

void Studio::audioInputMenu()
{
    juce::PopupMenu menu;
    auto next = audioInput;
    menu.addItem(1, "Record audio input", true, audioInput.enabled);
    menu.addSeparator();
    auto *device = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    const auto names = device ? device->getInputChannelNames() : juce::StringArray{};
    if (names.isEmpty())
        menu.addItem(2, "No audio inputs on the current device", false, false);
    for (int i = 0; i < names.size(); ++i)
        menu.addItem(100 + i, "Mono: " + names[i], true,
                     audioInput.channels == 1 && audioInput.input == i);
    for (int i = 0; i + 1 < names.size(); i += 2)
        menu.addItem(200 + i, "Stereo: " + names[i] + " + " + names[i + 1], true,
                     audioInput.channels == 2 && audioInput.input == i);
    menu.addSeparator();
    juce::PopupMenu tracks;
    for (int count : {1, 2, 4, 8})
        tracks.addItem(600 + count,
                       count == 1 ? juce::String("One track")
                                  : juce::String(count) + " tracks from consecutive inputs (one file each)",
                       audioInput.input + audioInput.channels * count <= names.size(), audioInput.tracks == count);
    menu.addSubMenu("Record several inputs at once", tracks);
    juce::PopupMenu inserts;
    inserts.addItem(700, "Master", true, audioInput.insert == 0);
    for (const auto &insert : controller.project.mixer)
        if (insert.index > 0 && insert.index <= 125)
            inserts.addItem(700 + insert.index,
                            "Insert " + juce::String(insert.index) + (insert.name.isNotEmpty() ? ": " + insert.name : ""),
                            true, audioInput.insert == insert.index);
    menu.addSubMenu("Record into Mixer insert", inserts);
    menu.addItem(3, "Monitor input (hear it while recording)", true, audioInput.monitor);
    juce::PopupMenu level;
    for (int percent : {100, 70, 50, 25})
        level.addItem(300 + percent, juce::String(percent) + "%", true,
                      juce::roundToInt(audioInput.monitorGain * 100) == percent);
    menu.addSubMenu("Monitor level", level);
    if (!recoveredTakes.empty())
    {
        juce::PopupMenu recovered;
        for (size_t i = 0; i < recoveredTakes.size(); ++i)
            recovered.addItem(500 + int(i), "Place " + recoveredTakes[i].file.getFileName() + " (" +
                                                juce::String(recoveredTakes[i].seconds, 1) + " s)");
        recovered.addSeparator();
        recovered.addItem(5, "Forget the list (files stay on disk)");
        menu.addSeparator();
        menu.addSubMenu("Recovered takes (" + juce::String(int(recoveredTakes.size())) + ")", recovered);
    }
    menu.addSeparator();
    menu.addItem(4, "Audio device settings...");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&audioInputButton),
                       [this, next](int chosen) mutable
                       {
                           if (chosen <= 0)
                               return;
                           if (chosen == 4)
                               return audioSettings();
                           if (chosen == 5)
                           {
                               for (const auto &take : recoveredTakes)
                                   forgetAudioTakeSidecar(take.file);
                               recoveredTakes.clear();
                               return;
                           }
                           if (chosen >= 500 && chosen - 500 < int(recoveredTakes.size()))
                           {
                               if (auto result = placeRecoveredTake(recoveredTakes[size_t(chosen - 500)].file);
                                   result.failed())
                                   status = result.getErrorMessage().fromFirstOccurrenceOf(": ", false, false);
                               return;
                           }
                           if (chosen == 1)
                               next.enabled = !next.enabled;
                           else if (chosen == 3)
                               next.monitor = !next.monitor;
                           else if (chosen >= 700)
                               next.insert = chosen - 700;
                           else if (chosen > 600)
                               next.tracks = chosen - 600;
                           else if (chosen >= 300)
                               next.monitorGain = float(chosen - 300) / 100.f;
                           else if (chosen >= 200)
                               next = {true, next.monitor, chosen - 200, 2, next.monitorGain, next.tracks};
                           else if (chosen >= 100)
                               next = {true, next.monitor, chosen - 100, 1, next.monitorGain, next.tracks};
                           if (auto result = applyAudioInput(next); result.failed())
                               status = result.getErrorMessage().fromFirstOccurrenceOf(": ", false, false);
                           repaint();
                       });
}

juce::var Studio::audioRecordingState() const
{
    auto o = obj();
    set(o, "enabled", audioInput.enabled);
    set(o, "input", audioInput.input);
    set(o, "channels", audioInput.channels);
    set(o, "tracks", audioInput.tracks);
    set(o, "insert", audioInput.insert);
    set(o, "monitor", audioInput.monitor);
    set(o, "monitorGain", audioInput.monitorGain);
    set(o, "readiness", audioInputReadiness());
    set(o, "songModeRequired", true);
    set(o, "patternMode", patternMode);
    set(o, "recordArmed", recordArmed);
    set(o, "capturing", audioTaking);
    if (playlistRange)
    {
        juce::Array<juce::var> range{playlistRange->first, playlistRange->second};
        set(o, "punchRange", range);
    }
    else
        set(o, "punchRange", juce::var());
    auto device = obj();
    const auto info = audio.inputCapture.device();
    auto *current = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
    set(device, "name", current ? current->getName() : juce::String());
    set(device, "running", info.running);
    set(device, "sampleRate", info.rate);
    set(device, "blockSize", info.block);
    set(device, "activeInputs", info.inputs);
    set(device, "inputLatencySamples", info.inputLatency);
    set(device, "outputLatencySamples", info.outputLatency);
    juce::Array<juce::var> names;
    if (current)
        for (const auto &name : current->getInputChannelNames())
            names.add(name);
    set(device, "inputNames", names);
    set(o, "device", device);
    juce::Array<juce::var> level{audioInputLevel[0], audioInputLevel[1]};
    set(o, "inputPeak", level);
    auto take = obj();
    set(take, "serial", juce::int64(audioTake.serial));
    set(take, "phase", audioTake.phase);
    set(take, "completionState", audioTake.phase == "capturing" || audioTake.phase == "placing" ? "pending"
                                 : audioTake.phase == "completed"                                ? "completed"
                                 : audioTake.phase == "cancelled"                                ? "cancelled"
                                 : audioTake.phase == "idle"                                     ? "none"
                                                                                                 : "failed");
    set(take, "file", audioTake.file.getFullPathName());
    juce::Array<juce::var> files;
    for (const auto &file : audioTake.files)
        files.add(file.getFullPathName());
    set(take, "files", files);
    set(take, "beat", audioTake.beat);
    set(take, "lane", audioTake.lane);
    set(take, "seconds", audioTaking ? double(audio.inputCapture.capturedFrames()) / juce::jmax(1., info.rate)
                                     : audioTake.seconds);
    set(take, "clip", juce::int64(audioTake.clip));
    set(take, "passes", audioTake.passes);
    set(take, "group", juce::int64(audioTake.group));
    juce::Array<juce::var> clips;
    for (auto clip : audioTake.clips)
        clips.add(juce::int64(clip));
    set(take, "clips", clips);
    set(take, "channel", juce::int64(audioTake.channel));
    set(take, "latencyCompensationMs", audioTake.latencyMs);
    set(take, "droppedSeconds", audioTaking ? double(audio.inputCapture.droppedFrames()) / juce::jmax(1., info.rate)
                                            : audioTake.droppedSeconds);
    set(take, "interrupted", audioTake.interrupted);
    set(take, "code", audioTake.code);
    set(take, "message", audioTake.message);
    set(o, "take", take);
    juce::Array<juce::var> recovered;
    for (const auto &item : recoveredTakes)
    {
        auto row = obj();
        set(row, "file", item.file.getFullPathName());
        set(row, "seconds", item.seconds);
        set(row, "mapped", item.mapped);
        set(row, "beat", item.beat);
        set(row, "project", item.project);
        recovered.add(row);
    }
    set(o, "recovered", recovered);
    return o;
}

juce::var Studio::audioRecordingOperation(const juce::var &args)
{
    const auto action = args["action"].toString();
    if (action == "configure")
    {
        auto next = audioInput;
        if (args.hasProperty("enabled"))
            next.enabled = bool(args["enabled"]);
        if (args.hasProperty("monitor"))
            next.monitor = bool(args["monitor"]);
        if (args.hasProperty("input"))
            next.input = int(args["input"]);
        if (args.hasProperty("channels"))
            next.channels = int(args["channels"]);
        if (args.hasProperty("tracks"))
            next.tracks = int(args["tracks"]);
        if (args.hasProperty("insert"))
            next.insert = int(args["insert"]);
        if (args.hasProperty("monitorGain"))
            next.monitorGain = float(double(args["monitorGain"]));
        if (args.hasProperty("punchRange"))
        {
            const auto range = args["punchRange"];
            if (range.isVoid() || range.isUndefined())
                playlistRange.reset();
            else if (const auto *pair = range.getArray(); pair && pair->size() == 2 &&
                                                          double((*pair)[0]) >= 0 &&
                                                          double((*pair)[1]) > double((*pair)[0]) + 1.e-8)
                playlistRange = std::pair{double((*pair)[0]), double((*pair)[1])};
            else
                throw McpFailure("invalid_arguments", "punchRange is [startBeat, endBeat] with end > start, or null");
        }
        if (auto result = applyAudioInput(next); result.failed())
        {
            const auto message = result.getErrorMessage();
            throw McpFailure(message.upToFirstOccurrenceOf(":", false, false),
                             message.fromFirstOccurrenceOf(": ", false, false));
        }
        repaint();
    }
    else if (action == "placeRecovered")
    {
        if (auto result = placeRecoveredTake(juce::File(args["file"].toString())); result.failed())
            throw McpFailure(result.getErrorMessage().upToFirstOccurrenceOf(":", false, false),
                             result.getErrorMessage().fromFirstOccurrenceOf(": ", false, false));
    }
    else if (action == "forgetRecovered")
    {
        for (const auto &take : recoveredTakes)
            forgetAudioTakeSidecar(take.file);
        recoveredTakes.clear();
    }
    else if (action == "cancel")
    {
        if (!audioTaking)
            throw McpFailure("audioTake.none", "No audio take is capturing");
        cancelAudioTake();
    }
    else if (action != "state")
        throw McpFailure("invalid_arguments",
                         "action must be state, configure, cancel, placeRecovered or forgetRecovered");
    return audioRecordingState();
}

juce::var Studio::audioRecordingToolDefinition()
{
    return juce::JSON::parse(R"JSON({"name":"vibera_audioRecording","description":"Audio-input recording (microphone/instrument through the audio device). configure: enabled, input (first device input channel, 0-based), channels 1 mono|2 stereo, tracks 1..8 (record that many separate input tracks at once from consecutive inputs, each `channels` wide: one WAV, Audio Clip Channel and Playlist track per input, all in one Undo step; take.files lists them), insert 0..125 (Mixer insert the recorded Channels feed, 0 Master; several tracks feed consecutive inserts; a missing insert falls back to Master and the take message says so), monitor (hear the input through the outputs), monitorGain 0..1.5 linear. Enabling opens exactly the selected inputs (one brief device restart; refused while playing). Takes follow the transport like the toolbar: with enabled audio input and Record armed (vibera_command transport.record), Play in Song mode captures; Stop seals the take, removes pre-roll/count-in and the device round-trip latency, writes a 24-bit WAV next to the project (Recorded/) and places it as an Audio Clip on the first free Playlist track (one Undo step). With Loop on, every loop pass becomes its own take: one clip per pass on consecutive tracks, grouped, newest playing and earlier ones muted (take.clips, take.group, take.passes). Comp with vibera_command audio.comp {target: a take clip}: that clip plays over its span and the other takes are cut at its edges and muted there (slice takes first with clip.slice to comp sections). Pattern mode refuses audio takes. state: readiness ready|disabled|noDevice|noInput, device (inputs, latencies), inputPeak, take {serial, phase idle|capturing|placing|completed|empty|unplaced|failed|cancelled, completionState, file, beat, lane, seconds, clip, latencyCompensationMs, droppedSeconds, interrupted, code, message}. A dropped/interrupted take is truncated and reported, never stitched. cancel discards the capturing take. Crash safety: the WAV header is rewritten every second of audio and a sidecar records the take's timeline position, so takes interrupted by a crash appear in state.recovered at the next start; placeRecovered {file} places one where it was recorded (one Undo step), forgetRecovered clears the list (files stay). Punch: the Playlist time selection (Ctrl-drag the ruler, or configure punchRange [startBeat, endBeat]) limits what a take keeps; clips cover only the range while the WAV keeps everything (state.punchRange). State is not listening or hardware evidence.","inputSchema":{"type":"object","additionalProperties":false,"properties":{"action":{"type":"string","enum":["state","configure","cancel","placeRecovered","forgetRecovered"]},"file":{"type":"string","maxLength":4096,"description":"placeRecovered: exact recovered take file from state.recovered."},"enabled":{"type":"boolean"},"input":{"type":"integer","minimum":0,"maximum":31},"channels":{"type":"integer","enum":[1,2]},"tracks":{"type":"integer","minimum":1,"maximum":8},"insert":{"type":"integer","minimum":0,"maximum":125},"monitor":{"type":"boolean"},"monitorGain":{"type":"number","minimum":0,"maximum":1.5},"punchRange":{"type":["array","null"],"items":{"type":"number","minimum":0},"minItems":2,"maxItems":2,"description":"[startBeat, endBeat] Playlist time selection used as the punch range; null clears it."}},"required":["action"]}})JSON");
}
} // namespace vibera
