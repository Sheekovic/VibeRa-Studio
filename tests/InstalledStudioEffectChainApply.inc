// SPDX-License-Identifier: AGPL-3.0-or-later
// Genuine installed provider, actual shared application and native callbacks.
// Included after the existing stricter stopped-effect qualification. No exact
// plugin-response, listening, physical input or hardware compatibility claim.
pinned = false; // Bounded structural interruptions/context replacement are accepted.
stage = "installed-chain-copy-paste";
const auto chainRequest = [&](const char *action, int mixer)
{
    auto value = object();
    auto *a = value.getDynamicObject();
    a->setProperty("action", action);
    a->setProperty("mixer", mixer);
    a->setProperty("projectIdentity", s.controller.projectIdentity);
    a->setProperty("revision", juce::int64(s.controller.revision));
    return value;
};
const auto chainFinish = [&](juce::var current, bool changesDocument)
{
    const auto operation = current["operation"].toString();
    check(current["completionState"].toString() == "pending" && operation.isNotEmpty(),
          "Installed chain operation has an actual pending session receipt");
    auto args = object();
    args.getDynamicObject()->setProperty("action", "job");
    args.getDynamicObject()->setProperty("operation", operation);
    const auto began = juce::Time::getMillisecondCounter();
    while (current["completionState"].toString() == "pending" &&
           juce::Time::getMillisecondCounter() - began < 20000)
    {
        tick();
        current = call("vibera_mixerchain", args);
    }
    events.add(current);
    keep();
    check(current["completionState"].toString() == "completed" &&
              bool(current["documentChanged"]) == changesDocument &&
              bool(current["undoable"]) == changesDocument,
          "Installed chain operation reaches a truthful actual terminal/history result");
    if (changesDocument)
        check(current["audioState"].toString() == "active" && !bool(current["nativeRecoveryRequired"]) &&
                  s.audio.projectionDiagnostics().pluginSourceState == "active" && s.audio.playing(),
              "Installed structural change resumes actual playing native source before completion");
    return current;
};
const auto recoveredPcm = [&]
{
    s.audio.setMidiTarget(c.id);
    s.audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(127)));
    float peak = 0;
    for (int n = 0; n < 64; ++n)
    {
        tick();
        peak = std::max(peak, block.getMagnitude(0, 0, 256));
    }
    s.audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
    tick();
    check(std::isfinite(peak) && peak > .000001f,
          "Actual installed chain recovers finite non-silent held-sample output after structural change");
};
chainFinish(call("vibera_mixerchain", chainRequest("copy", 2)), false);
check(s.performAction("transport.play") && s.audio.playing(),
      "Actual familiar Play action starts the previously stopped provider workflow");
chainFinish(call("vibera_mixerchain", chainRequest("paste", 1)), true);
const auto pasted = mixerInsert(s.controller.project, 1)->effects;
check(pasted.size() == 1 && pasted[0].id != backgroundId && pasted[0].descriptor == base.descriptor &&
          s.audio.parameterHost(pasted[0].id),
      "Installed Paste produces a new target runtime and preserves source insert identity");
effectId = pasted[0].id;
recoveredPcm();
stage = "installed-chain-ui-undo-mcp-redo";
check(s.performAction("edit.undo"), "Actual UI Undo dispatches installed chain history");
chainFinish(s.mixerChainReadback(s.lastMixerChainHistoryOperation), true);
check(mixerInsert(s.controller.project, 1)->effects.empty(), "Installed chain Undo restores empty insert");
recoveredPcm();
auto redoArgs = object();
redoArgs.getDynamicObject()->setProperty("command", "edit.redo");
chainFinish(call("vibera_command", redoArgs), true);
check(mixerInsert(s.controller.project, 1)->effects.front().id == effectId,
      "Actual installed MCP Redo restores historical slot ID");
recoveredPcm();

stage = "installed-single-slot-replace";
auto replaceArgs = object(), replacement = object();
replaceArgs.getDynamicObject()->setProperty("command", "plugin.effect.replace");
replacement.getDynamicObject()->setProperty("target", juce::int64(effectId));
replacement.getDynamicObject()->setProperty("text", base.descriptor);
replaceArgs.getDynamicObject()->setProperty("args", replacement);
chainFinish(call("vibera_command", replaceArgs), true);
const auto replacedId = mixerInsert(s.controller.project, 1)->effects.front().id;
check(replacedId != effectId && !s.controller.project.plugin(effectId) &&
          mixerInsert(s.controller.project, 2)->effects.front().id == backgroundId,
      "Actual installed slot Replace creates fresh native identity and preserves unrelated chain");
effectId = replacedId;
recoveredPcm();

stage = "installed-chain-preset-occupied-apply";
const auto presets = folder.getChildFile("installed-chain-presets");
check(presets.createDirectory().wasOk(), "Task-owned installed preset directory created");
auto configure = object();
configure.getDynamicObject()->setProperty("action", "configureDirectory");
configure.getDynamicObject()->setProperty("directory", presets.getFullPathName());
call("vibera_mixerchain", configure);
const auto preset = presets.getChildFile("native.vibfx");
auto save = chainRequest("save", 1);
save.getDynamicObject()->setProperty("path", preset.getFullPathName());
save.getDynamicObject()->setProperty("overwrite", false);
chainFinish(call("vibera_mixerchain", save), false);
auto apply = chainRequest("loadApply", 1);
apply.getDynamicObject()->setProperty("path", preset.getFullPathName());
chainFinish(call("vibera_mixerchain", apply), true);
check(mixerInsert(s.controller.project, 1)->effects.front().id != replacedId,
      "Installed occupied preset Apply creates exact new canonical chain rather than aliasing old slot");
effectId = mixerInsert(s.controller.project, 1)->effects.front().id;
recoveredPcm();
s.audio.stop();
tick();
stage = "installed-applied-chain-project-persistence";
const auto project = folder.getChildFile("applied-chain.vibra");
projectJob("save", project);
Project saved;
check(loadProject(project, saved).wasOk() && saved.state().isEquivalentTo(s.controller.project.state()) &&
          saved.plugin(effectId) && saved.plugin(effectId)->state.isNotEmpty(),
      "Actual fresh Save retains installed native state and exact applied chain in readable project");
projectJob("open", project);
const auto reopenStarted = juce::Time::getMillisecondCounter();
const auto audioPending = [&] { return bool(call("vibera_state", object())["pendingAudioUpdate"]); };
while ((audioPending() || s.audio.projectionDiagnostics().pluginSourceState != "active") &&
       juce::Time::getMillisecondCounter() - reopenStarted < 20000)
    tick();
check(!audioPending() && s.audio.projectionDiagnostics().pluginSourceState == "active" &&
          s.controller.project.state().isEquivalentTo(saved.state()) && s.audio.parameterHost(effectId) &&
          s.audio.parameterHost(effectId)->alive(),
      "Actual project open recreates the saved installed chain and activates its native projection");
check(s.performAction("transport.play") && s.audio.playing(), "Reopened installed chain can play");
recoveredPcm();
s.audio.stop();
stage = "installed-applied-chain-musical-export";
const auto musicCommand = [&](const char *command, const juce::var &values)
{
    auto args = object();
    args.getDynamicObject()->setProperty("command", command);
    args.getDynamicObject()->setProperty("args", values);
    call("vibera_command", args);
};
auto note = object();
note.getDynamicObject()->setProperty("channel", juce::int64(c.id));
note.getDynamicObject()->setProperty("pattern", juce::int64(s.selectedPattern));
note.getDynamicObject()->setProperty("pitch", 60);
note.getDynamicObject()->setProperty("beat", 0);
note.getDynamicObject()->setProperty("length", 2);
note.getDynamicObject()->setProperty("value", .8);
musicCommand("notes.add", note);
auto placement = object();
placement.getDynamicObject()->setProperty("pattern", juce::int64(s.selectedPattern));
placement.getDynamicObject()->setProperty("beat", 0);
placement.getDynamicObject()->setProperty("length", 4);
placement.getDynamicObject()->setProperty("lane", 0);
musicCommand("clip.add", placement);
const auto musicReady = juce::Time::getMillisecondCounter();
while (audioPending() && juce::Time::getMillisecondCounter() - musicReady < 5000)
    tick();
check(!audioPending() && !s.controller.project.pattern(s.selectedPattern)->notes.empty() &&
          !s.controller.project.placements.empty(),
      "Actual canonical notes and Song placement precede genuine musical export");
const auto wav = folder.getChildFile("installed-applied-chain.wav");
projectJob("export", wav);
juce::AudioFormatManager formats;
formats.registerBasicFormats();
std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(wav));
check(reader && reader->sampleRate == 48000 && reader->numChannels == 2 && reader->bitsPerSample == 24 &&
          reader->lengthInSamples > 0 && reader->lengthInSamples < 480000,
      "Actual installed-chain export is readable stereo48k/24bit with bounded real musical duration");
juce::AudioBuffer<float> rendered(2, int(reader->lengthInSamples));
check(reader->read(&rendered, 0, rendered.getNumSamples(), 0, true, true),
      "Actual exported native-plugin WAV samples decoded");
float exportedPeak = 0;
for (int channel = 0; channel < 2; ++channel)
    for (int frame = 0; frame < rendered.getNumSamples(); ++frame)
    {
        const auto value = rendered.getSample(channel, frame);
        check(std::isfinite(value), "Exported installed-plugin sample remains finite");
        exportedPeak = std::max(exportedPeak, std::abs(value));
    }
check(exportedPeak > .000001f, "Genuine arranged music exports non-silent through installed applied chain");
report.getDynamicObject()->setProperty("installedChainExportPeak", exportedPeak);
report.getDynamicObject()->setProperty("installedChainExportSha256", juce::SHA256(wav).toHexString());
report.getDynamicObject()->setProperty("installedChainApplyVerified", true);
report.getDynamicObject()->setProperty("installedSingleSlotReplaceVerified", true);
