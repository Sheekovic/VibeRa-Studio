// SPDX-License-Identifier: AGPL-3.0-or-later
#include "FlpImport.h"
#include "Studio.h"
#include "MonitorVolume.h"
#include "PolyphonyMeter.h"
#include "SliceDetect.h"
#include "AudioLogger.h"
#include "VideoPlayer.h"
#include "StudioDevicePreparation.h"
#include "EffectChainPresetFiles.h"
#include "ChannelPreset.h"
#include "PatternManagement.h"
#include "MixerChainWorkflowContracts.h"
#include "MixerInsertLifecycle.h"
#include "MixerOrganisationReferences.h"
#include <limits>
#include <stdexcept>
#include "McpSchema.h"
#include "StepComposeContracts.h"
#include "RecordingContracts.h"
#include "ChannelRoutingContracts.h"
#include "ClipGroups.h"
#include "SwingCommands.h"
#include "Quantizer.h"
#include "Editors.h"
#include "MixerEqEditor.h"
#include "MidiClipboard.h"
#include "ScoreSheet.h"
#include "ChannelColourPicker.h"
#include "DetachedWindow.h"
#include "Settings.h"
#include "RepeatMidiBudget.h"
#include "effects/NativeEffectsRegistry.h"
#include "NativeInstruments.h"
#include "InstrumentWindow.h"
#include "WorkspaceTiling.h"
#include "CoProducerDock.h"
#include "CoProducerPanel.h"
#include "../tests/SyntheticDevice.h"
#include <tracktion_graph/tracktion_graph.h>
#include <BinaryData.h>
#include <juce_cryptography/juce_cryptography.h>
#include <set>
#include <type_traits>

namespace vibera
{
static_assert(std::is_nothrow_move_assignable_v<Project>,
              "Project replacement identity must witness a nonthrowing document move");
namespace
{
juce::Result cancelRemotePreview(Controller &controller)
{
    // Failed local completion retains a baseline: never snapshot or discard it.
    if (controller.hasGesture() &&
        (controller.gestureOwner().isEmpty() || controller.gestureOwner().startsWith("quantizer-")))
        return juce::Result::fail(
            "history.pendingGesture: Finish or cancel the local edit before file operations");
    if (controller.gestureOwner().isNotEmpty())
        return controller.cancelGestureChecked();
    return juce::Result::ok();
}
} // namespace
#include "StudioMixerChains.inc"
#include "StudioPluginIo.inc"
Studio::Studio(bool enableDevice, int explicitProviderFrames)
    : audio(enableDevice, juce::File{}, explicitProviderFrames),
      pluginTestSettings(enableDevice ? nullptr : std::make_unique<juce::TemporaryFile>()),
      plugins(enableDevice ? juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                 .getChildFile("VibeRa Studio/plugins.xml")
                           : pluginTestSettings->getFile()),
      midiController(audio.midiInput.controls, enableDevice)
{
    if (!enableDevice)
        workflowTestSettings = std::make_unique<juce::TemporaryFile>();
    midiController.invoke = [this](const ControllerInvocation &value) { invokeController(value); };
    controllerTestMode = !enableDevice;
    if (controllerTestMode)
        getProperties().set("viberaNativeMixerMeterGuardsVersion", 1);
    midiController.inputAllowed = [this](const juce::String &id)
    { return id != audio.midiInput.identifier(); };
    if (enableDevice &&
        (midiController.inputIdentifier().isNotEmpty() || midiController.outputIdentifier().isNotEmpty()))
        midiController.setPorts(midiController.inputIdentifier(), midiController.outputIdentifier());
    if (enableDevice && themeFile().existsAsFile())
        loadTheme(themeFile());
    look.setColour(juce::ResizableWindow::backgroundColourId, panel);
    look.setColour(juce::TextButton::buttonColourId, grid);
    look.setColour(juce::TextButton::textColourOffId, ink);
    look.setColour(juce::ComboBox::backgroundColourId, grid);
    look.setColour(juce::Slider::thumbColourId, accent);
    setLookAndFeel(&look);
    // One desktop-level tooltip window serves the main window, dialogs and detached editors;
    // without it no setTooltip() text is ever shown.
    tooltips = std::make_unique<juce::TooltipWindow>(nullptr, 600);
    tooltips->setLookAndFeel(&look);
    setWantsKeyboardFocus(true);
    setSize(1600, 940);
    brand =
        juce::Drawable::createFromImageData(BinaryData::viberasymbol_svg, BinaryData::viberasymbol_svgSize);
    look.setColour(juce::TextButton::textColourOnId, ink);
    look.setColour(juce::ComboBox::textColourId, ink);
    look.setColour(juce::Slider::textBoxTextColourId, ink);
    look.setColour(juce::Slider::textBoxBackgroundColourId, bg);
    look.setColour(juce::Slider::textBoxOutlineColourId, grid);
    play.setIcon(ToolbarButton::Icon::play);
    stop.setIcon(ToolbarButton::Icon::stop);
    record.setIcon(ToolbarButton::Icon::record);
    record.setTooltip("Record (arm, then Play). Records MIDI notes, and audio input when the microphone button is on.");
    audioInputButton.setIcon(ToolbarButton::Icon::mic);
    audioInputButton.setClickingTogglesState(false);
    audioInputButton.setTooltip("Record audio input (microphone/instrument). Right-click to choose the input, "
                                "mono/stereo and monitoring.");
    audioInputButton.onClick = [this]
    {
        auto next = audioInput;
        next.enabled = !next.enabled;
        if (auto result = applyAudioInput(next); result.failed())
            status = result.getErrorMessage().fromFirstOccurrenceOf(": ", false, false);
        else
            status = audioInput.enabled ? "Audio input on: arm Record and press Play in Song mode"
                                        : "Audio input off";
        repaint();
    };
    audioInputButton.menu = [this] { audioInputMenu(); };
    loopButton.setIcon(ToolbarButton::Icon::loop);
    loopButton.setComponentID("transport.loop");
    loopButton.setTooltip(
        "Loop current Pattern / entire Song. Changing loop mode during a MIDI take is unavailable.");
    loopButton.onClick = [this] { performAction("transport.loop"); };
    undoButton.setIcon(ToolbarButton::Icon::undo);
    redoButton.setIcon(ToolbarButton::Icon::redo);
    open.setIcon(ToolbarButton::Icon::folder);
    save.setIcon(ToolbarButton::Icon::save);
    exportWav.setIcon(ToolbarButton::Icon::exportFile);
    devices.setIcon(ToolbarButton::Icon::settings);
    clonePattern.setIcon(ToolbarButton::Icon::clone);
    browserButton.setIcon(ToolbarButton::Icon::browser);
    click.setIcon(ToolbarButton::Icon::metronome);
    typingButton.setIcon(ToolbarButton::Icon::keyboard);
    toolsButton.setIcon(ToolbarButton::Icon::tools);
    play.setTooltip("Play / pause (Space; configurable in Settings > Shortcuts)");
    stop.setTooltip("Stop and return to start");
    undoButton.setTooltip("Undo (Ctrl+Z)");
    redoButton.setTooltip("Redo (Ctrl+Shift+Z)");
    open.setTooltip("Open project (Ctrl+O)");
    save.setTooltip("Save project (Ctrl+S)");
    exportWav.setTooltip("Export: Song as WAV, FLAC or OGG, or stems (one file per Mixer insert)");
    clonePattern.setTooltip("Clone selected pattern");
    browserButton.setTooltip("Show / hide Browser");
    toolsButton.setTooltip("Tools, score log and workspace layout");
    selectedPattern = controller.project.patterns.front().id;
    selectedChannel = controller.project.channels.front().id;
    quantizeRecordingStart = audio.midiInput.recordingQuantizeStart();
    quantizeRecordingEnd = audio.midiInput.recordingQuantizeEnd();
    aiSettings = ai::AiSettings::load(aiSettingsFile()); // Test instances read their isolated temp file.
    if (enableDevice || juce::SystemStats::getEnvironmentVariable("VIBERA_CODEX_EXECUTABLE", {}).isNotEmpty())
        aiDetectAgents(); // Finds a signed-in Codex CLI without blocking startup.
    juce::String profileError;
    if (enableDevice && profileFile().existsAsFile())
    {
        auto loaded = customization.load(profileFile());
        if (loaded.failed())
            profileError = loaded.getErrorMessage();
    }
    for (auto *c : std::initializer_list<juce::Component *>{&play, &mode, &loopButton, &newPattern,
                                                            &clonePattern, &save, &open, &exportWav, &devices,
                                                            &demo, &patterns, &tempo, &title, &deviceLabel})
        addAndMakeVisible(c);
    title.setText("VibeRa Studio", juce::dontSendNotification);
    title.setFont(juce::Font(vibera::uiFont, 16.f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, ink);
    tempo.setRange(20, 400, 1);
    tempo.setValue(120);
    tempo.setSliderStyle(juce::Slider::IncDecButtons);
    tempo.setTextValueSuffix(" BPM");
    // A 70 px value box leaves the +/- buttons side by side at a comfortable size.
    tempo.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 28);
    tempo.setTitle("Tempo");
    tempo.setTooltip("Project tempo in BPM: type a value, drag, or use + and -");
    patterns.setTitle("Pattern");
    snapChoice.setTitle("Snap");
    tempo.onDragStart = [this] { controller.beginGesture(); };
    tempo.onDragEnd = [this]
    {
        if (auto result = controller.endGestureChecked("Tempo"); result.failed())
        {
            status = result.getErrorMessage();
            repaint();
        }
    };
    tempo.onValueChange = [this]
    {
        CommandArgs a;
        a.value = tempo.getValue();
        command("project.tempo", a);
    };
    tapTempoButton.setTooltip("Tap tempo: click in time with the music (two taps or more)");
    tapTempoButton.onClick = [this] { tapTempo(); };
    addAndMakeVisible(tapTempoButton);
    addChildComponent(outputScope);
    outputScope.onToggle = [this] { performAction("view.outputScopeMode"); };
    play.onClick = [this]
    {
        if (!audio.playing())
            playStartBeat = audio.positionBeat(); // Remember seek time: where this playback began.
        if (automationRecordingActive() && audio.playing())
        {
            stop.onClick();
            return;
        }
        const bool ownedChainRecovery =
            mixerChainRecovering() && devicePreparationServicing && devicePreparation &&
            !devicePreparationBusy() &&
            devicePreparation->operation == mixerChains->active->recoveryOperation;
        if (mixerChainApplying() && !ownedChainRecovery)
        {
            status = "Effect chain Apply pending; wait or cancel its operation";
            return;
        }
        if ((audio.deviceRestorationRequired() || audio.documentProjectionDeferred()) &&
            audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice())
        {
            const auto result = devicePreparation && !devicePreparationBusy()
                ? retryDevicePreparation(devicePreparation->operation, true) : beginDevicePreparation(true);
            if (result.failed())
                status = result.getErrorMessage();
            else
                serviceDevicePreparation();
            return;
        }
        if (!audio.playing() && routingPending())
        {
            status = routingPendingStatus;
            return;
        }
        if (hasPendingMidiTake())
        {
            status = "Retry or cancel the retained MIDI take before Play";
            return;
        }
        if (audio.playing())
        {
            finishAudioTake();
            if (auto result = finishMidiTake(); result.failed())
            {
                audio.stop();
                status = result.getErrorMessage();
                return;
            }
            if (!audio.playing())
                return;
        }
        if (needsRebuild || audio.documentProjectionDeferred() || projectionError.isNotEmpty())
        {
            auto r = audio.rebuild(controller.project, patternMode, selectedPattern);
            if (r.failed())
            {
                projectionError = r.getErrorMessage();
                status = r.getErrorMessage();
                repaint();
                return;
            }
            projectionError.clear();
            needsRebuild = false;
            engineRevision = controller.revision;
        }
        previewNote(-1);
        if (waitingForInput)
        {
            // Play again while waiting cancels the wait (and the empty take).
            armWaitForInput(false);
            if (auto result = finishMidiTake(); result.failed())
                status = result.getErrorMessage();
            repaint();
            return;
        }
        if (waitingForPlay)
        {
            // Play again while waiting cancels the wait.
            armPlayWait(false);
            status = "Wait for input cancelled";
            repaint();
            return;
        }
        if (!audio.playing() && waitForInput && !waitBypass && !(recordArmed && recordNotes) &&
            (recordArmed ? !waitRespectFilter : !waitOnlyWhenRecording))
        {
            armPlayWait(true);
            status = recordArmed ? "Waiting for input: play a note to start recording (Stop cancels)"
                                 : "Waiting for input: play a note to start playback (Stop cancels)";
            repaint();
            return;
        }
        if (!audio.playing() && recordArmed && waitForInput && recordNotes)
        {
            // Wait for input: open the take at the stopped playhead; the first note starts it.
            beginMidiTake(0);
            if (midiTaking)
            {
                armWaitForInput(true);
                status = "Waiting for input: play a note to start recording (Stop cancels)";
            }
            repaint();
            return;
        }
        int bars = !audio.playing() && recordArmed ? audio.countInBars() : 0;
        if (!audio.playing() && recordArmed)
        {
            beginAudioTake(); // Before the count-in moves the transport: it trims to this start.
            if (recordNotes)
                beginMidiTake(audio.countInSeconds(bars));
        }
        audio.playStop(midiTaking || audioTaking ? bars : 0);
    };
    mode.onClick = [this]
    {
        if (automationRecordingActive())
        {
            status = "automationRecording.busy: Finish/cancel before changing mode";
            return;
        }
        if (auto ready = restoreRoutingBeforeStructural(); ready.failed())
        {
            status = ready.getErrorMessage();
            return;
        }
        if (auto result = finishMidiTake(); result.failed())
        {
            status = result.getErrorMessage();
            return;
        }
        audio.stop();
        patternMode = !patternMode;
        needsRebuild = true;
        audio.seekBeat(0);
        mode.setButtonText(patternMode ? "Pattern" : "Song");
    };
    patterns.onChange = [this]
    {
        int i = patterns.getSelectedId() - 1;
        if (i >= 0 && i < int(controller.project.patterns.size()))
        {
            selectPattern(controller.project.patterns[size_t(i)].id);
        }
    };
    patternOptions.setIcon(ToolbarButton::Icon::menu);
    patternOptions.setTooltip("Pattern menu: rename, insert, clone, delete, reorder and find empty");
    patternOptions.onClick = [this] { patternMenu(); };
    patterns.menu = [this] { patternMenu(); };
    patterns.focusSelector = [this] { focus = Scope::patterns; };
    patterns.shortcut = [this](const juce::KeyPress &key)
    {
        focus = Scope::patterns;
        return keyPressed(key);
    };
    newPattern.onClick = [this]
    {
        command("pattern.add");
        if (status == "Ready")
        {
            selectedPattern = controller.project.patterns.back().id;
            refresh();
        }
    };
    clonePattern.onClick = [this]
    {
        CommandArgs a;
        a.pattern = selectedPattern;
        command("pattern.clone", a);
        if (status == "Ready")
        {
            const auto it =
                std::find_if(controller.project.patterns.begin(), controller.project.patterns.end(),
                             [&](const auto &p) { return p.id == a.pattern; });
            if (it != controller.project.patterns.end() && std::next(it) != controller.project.patterns.end())
                selectedPattern = std::next(it)->id;
        }
        refresh();
    };
    save.onClick = [this] { chooseSave(); };
    open.onClick = [this] { chooseOpen(); };
    exportWav.onClick = [this] { exportMenu(); };
    devices.onClick = [this] { audioSettings(); };
    demo.onClick = [this] { requestProjectTransition([this](bool discard) { (void)newProject(true, discard); }); };
    for (auto *c : std::initializer_list<juce::Component *>{&stop, &undoButton, &redoButton, &browserButton,
                                                            &snapChoice, &record, &audioInputButton, &click, &countIn,
                                                            &typingButton, &toolsButton, &patternOptions})
        addAndMakeVisible(c);
    click.setTooltip("Metronome (default Ctrl+M). Right-click for original sounds and volume settings.");
    countIn.setTooltip("Recording count-in (default Ctrl+P). Right-click for one or two bars.");
    click.onClick = [this]
    {
        audio.setMetronome(!audio.metronome());
        refresh();
    };
    countIn.onClick = [this]
    {
        audio.setCountInBars(audio.countInBars() ? 0 : audio.preferredCountInBars());
        refresh();
    };
    click.menu = [this] { guideMenu(false); };
    loadAudioInputSettings(enableDevice);
    persistentTyping = enableDevice;
    if (persistentTyping)
    {
        auto prefs =
            juce::JSON::parse(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                  .getChildFile("VibeRa Studio/typing-keyboard.json"));
        typingKeys.enabled = bool(prefs["enabled"]);
        if (prefs.hasProperty("root"))
            typingKeys.root = juce::jlimit(24, 84, int(prefs["root"]));
        if (prefs.hasProperty("velocity"))
            typingKeys.velocity = juce::jlimit(1, 127, int(prefs["velocity"]));
    }
    performanceInput.emit = [this](int pitch, bool on, int velocity)
    {
        audio.typingInput.route(on ? juce::MidiMessage::noteOn(1, pitch, juce::uint8(velocity))
                                   : juce::MidiMessage::noteOff(1, pitch),
                                true, false);
    };
    typingKeys.emit = [this](int pitch, bool on, int velocity)
    {
        performanceInput.set(0, pitch, on, velocity);
        audio.typingInput.route(on ? juce::MidiMessage::noteOn(1, pitch, juce::uint8(velocity))
                                   : juce::MidiMessage::noteOff(1, pitch),
                                false, true);
    };
    audio.typingInput.captureInterrupted = [this](Id channel, double seconds)
    {
        if (midiTaking)
        {
            drainMidiTake();
            if (midiTaking)
                midiTake.interrupt(channel, seconds, 1);
        }
    };
    typingButton.setTooltip("Typing keyboard to piano (Ctrl+T). Right-click for root and velocity.");
    typingButton.onClick = [this]
    {
        releaseTypingNotes();
        typingKeys.enabled = !typingKeys.enabled;
        saveTypingPreferences();
        refresh();
    };
    toolsButton.onClick = [this] { toolsMenu(); };
    audio.midiInput.historyInterrupted = [this](Id channel, double now)
    {
        drainScoreLog();
        scoreLog.interrupt(channel, 0, now);
    };
    audio.typingInput.historyInterrupted = [this](Id channel, double now)
    {
        drainScoreLog();
        scoreLog.interrupt(channel, 1, now);
    };
    typingButton.menu = [this] { typingMenu(); };
    countIn.menu = [this] { guideMenu(true); };
    record.setTooltip(
        "Record MIDI notes (R). Arm, then Play; Stop commits one undoable take. Options in Settings > MIDI.");
    record.setColour(juce::TextButton::buttonOnColourId, themeColour(0xff943c4b));
    record.onClick = [this]
    {
        if (stepComposePending())
        {
            record.setToggleState(recordArmed, juce::dontSendNotification);
            status = "stepCompose.busy: Accept or Cancel Advanced Fill before recording";
            return;
        }
        if (nativeScalarBusy()) { status = "parameterHistory.busy: Finish native scalar history before Record"; return; }
        if (mixerChainApplying())
        {
            record.setToggleState(recordArmed, juce::dontSendNotification);
            status = "Effect chain Apply pending; recording unchanged";
            return;
        }
        if (devicePreparationBusy())
        {
            status = devicePreparationPendingStatus;
            return;
        }
        if (routingPending())
        {
            status = routingPendingStatus;
            return;
        }
        if (hasPendingMidiTake())
        {
            const auto result = retryMidiTake();
            if (result.failed())
                status = result.getErrorMessage();
            return;
        }
        if (midiTaking || audioTaking)
        {
            const bool midi = midiTaking;
            finishAudioTake();
            if (!midi)
            {
                recordArmed = false; // Audio-only take: Record punches out and disarms.
                midiTakeUiPending = true;
            }
            else if (auto result = finishMidiTake(); result.failed())
                status = result.getErrorMessage();
            return;
        }
        recordArmed = !recordArmed;
        record.setToggleState(recordArmed, juce::dontSendNotification);
        if (recordArmed && audio.playing())
        {
            beginAudioTake(); // Punch in on the fly: the take starts here.
            if (recordNotes)
                beginMidiTake();
        }
        else if (recordArmed && recordStartsPlayback)
            play.onClick(); // Recording starts playback: arming while stopped also presses Play.
    };
    audio.midiInput.captureInterrupted = [this](Id channel, double seconds)
    {
        if (midiTaking)
        {
            drainMidiTake();
            if (midiTaking)
                midiTake.interrupt(channel, seconds);
        }
    };
    stop.onClick = [this]
    {
        const bool keepArmed = !disarmOnStop && recordArmed; // Disarm on stop off: Record stays armed.
        if (waitingForPlay)
        {
            armPlayWait(false);
            status = "Wait for input cancelled";
        }
        if (waitingForInput)
        {
            // Stop while waiting for input: no note arrived, so the empty take ends.
            armWaitForInput(false);
            if (auto result = finishMidiTake(); result.failed())
                status = result.getErrorMessage();
            repaint();
            return;
        }
        if (mixerChainApplying())
            mixerChains->active->playRequested = false;
        if (devicePreparationBusy())
            cancelDevicePreparation(devicePreparation->operation);
        const auto automation = finishAutomationRecording(false);
        finishAudioTake(); // Seal before the transport stops: nothing after Stop is kept.
        // Transport Stop must still happen when canonical take admission refuses.
        stopMidiCaptureForRecovery();
        audio.stop();
        // Remember seek time: the first Stop returns to where Play was pressed; Stop again goes to the start.
        audio.seekBeat(rememberSeekTime && playStartBeat ? *playStartBeat : 0.);
        playStartBeat.reset();
        recordArmed = false;
        midiTakeUiPending = true; // Stop also disarms an idle armed recorder.
        const auto result =
            automation.wasOk() && !automationRecordingActive() ? finishMidiTake() : automation;
        status = result.failed()               ? result.getErrorMessage()
                 : automationRecordingActive() ? "Automation take sealing; inspect recording state"
                                               : "Stopped";
        // The sealed audio take waits for the MIDI take's own history step, then follows it.
        if (audioTakePlacementPending)
            placeAudioTake();
        if (keepArmed && !hasPendingMidiTake())
        {
            recordArmed = true;
            record.setToggleState(true, juce::dontSendNotification);
        }
    };
    undoButton.onClick = [this]
    {
        if (auto take = finishMidiTake(); take.failed())
        {
            status = take.getErrorMessage();
            repaint();
            return;
        }
        if (auto result = controller.undoEditChecked(); result.failed())
        {
            status = result.getErrorMessage();
            repaint();
        }
    };
    redoButton.onClick = [this]
    {
        if (auto take = finishMidiTake(); take.failed())
        {
            status = take.getErrorMessage();
            repaint();
            return;
        }
        if (auto result = controller.redoEditChecked(); result.failed())
        {
            status = result.getErrorMessage();
            repaint();
        }
    };
    browserButton.onClick = [this]
    {
        browserShown = !browserShown;
        if (detachedWindows[4])
            detachedWindows[4]->setVisible(browserShown);
        resized();
    };
    snapChoice.addItem("No snap", 1);
    snapChoice.addItem("1/4 beat", 2);
    snapChoice.addItem("1/2 beat", 3);
    snapChoice.addItem("1 beat", 4);
    snapChoice.setSelectedId(2, juce::dontSendNotification);
    snapChoice.onChange = [this]
    {
        snapLine = snapMarkers = false;
        snap = snapChoice.getSelectedId() == 1   ? 0
               : snapChoice.getSelectedId() == 2 ? .25
               : snapChoice.getSelectedId() == 3 ? .5
                                                 : 1.;
        refresh();
    };
    newPattern.setButtonText("+");
    newPattern.setTooltip("New pattern");
    exportWav.setButtonText("Export");
    devices.setTooltip("Audio devices, editable shortcuts and document macros");
    if (persistentTyping)
        recentSources.load(profileFile().getSiblingFile("recent-sources.json"));
    browser = std::make_unique<Browser>(*this);
    addAndMakeVisible(*browser);
    juce::StringArray names{"Channel Rack", "Piano roll", "Playlist", "Mixer"};
    for (int i = 0; i < 4; ++i)
    {
        windows[size_t(i)] = std::make_unique<EditorWindow>(*this, names[i], i);
        addAndMakeVisible(*windows[size_t(i)]);
        auto &button = editorButtons[size_t(i)];
        addAndMakeVisible(button);
        button.setButtonText(juce::StringArray{"Rack F6", "Piano F7", "Playlist F5", "Mixer F9"}[i]);
        button.setIcon(std::array<ToolbarButton::Icon, 4>{
            ToolbarButton::Icon::rack, ToolbarButton::Icon::piano, ToolbarButton::Icon::playlist,
            ToolbarButton::Icon::mixer}[size_t(i)]);
        button.setTooltip(button.getButtonText() + " - show / hide editor");
        button.onClick = [this, i] { toggleEditor(i); };
    }
    coProducerButton.setIcon(ToolbarButton::Icon::spark);
    coProducerButton.setTooltip("CoProducer - AI producer chat and MIDI ideas (side panel)");
    coProducerButton.onClick = [this]
    {
        if (coProducerShown)
            hideCoProducer();
        else
            showCoProducer();
    };
    addAndMakeVisible(coProducerButton);
    coProducerSplit = std::make_unique<Splitter>(*this, 4);
    addChildComponent(*coProducerSplit);
    browserSplit = std::make_unique<Splitter>(*this, 0);
    arrangementSplit = std::make_unique<Splitter>(*this, 1);
    rackSplit = std::make_unique<Splitter>(*this, 2);
    mixerSplit = std::make_unique<Splitter>(*this, 3);
    for (auto *c : {browserSplit.get(), arrangementSplit.get(), rackSplit.get(), mixerSplit.get()})
        addAndMakeVisible(c);
    controller.applicationCommand = [this](const auto &id, const auto &args, const auto &owner)
    {
        if (stepComposePending() && owner != stepComposeSession->owner)
            return std::optional<juce::Result>{juce::Result::fail("stepCompose.busy: Accept or Cancel Advanced Fill first")};
        if (nativeScalarBusy())
            return std::optional<juce::Result>{
                juce::Result::fail("parameterHistory.busy: Finish the owned native scalar edit")};
        if (id.startsWith("channel.sampler.controller.") && automationRecordingActive())
            return std::optional<juce::Result>{automationRecordingAdmission()};
        if (auto recording = automationRecordingCommand(id, args))
            return recording;
        return admitControllerCommand(id, args, owner);
    };
    controller.applicationChangeAdmission = [this](const Project &next)
    {
        auto *device = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        // Match buildEdit's existing no-device host configuration too: semantic
        // edits must not bypass admission simply because hardware is unopened.
        if (auto admitted = validateRepeatMidiBudget(next, patternMode, selectedPattern,
                                                     device ? device->getCurrentBufferSizeSamples() : 512,
                                                     device ? device->getCurrentSampleRate() : 48000.);
            admitted.failed())
            return admitted;
        if (midiTaking && patternMode)
        {
            const auto *before = controller.project.pattern(takePattern);
            const auto *after = next.pattern(takePattern);
            if (!before || !after || before->length != after->length ||
                playbackMeter(controller.project, true, takePattern) !=
                    playbackMeter(next, true, takePattern))
                return juce::Result::fail(
                    "recording.patternExtentBusy: Stop the Pattern take before "
                    "changing its playback window or meter; in-range edits remain available");
        }
        return juce::Result::ok();
    };
    controller.applicationHistory = [this](auto direction)
    {
        if (stepComposePending())
            return std::optional<juce::Result>{juce::Result::fail("stepCompose.busy: Accept or Cancel Advanced Fill before history")};
        if (automationRecordingActive())
            return std::optional<juce::Result>{automationRecordingAdmission()};
        return admitControllerHistory(direction);
    };
    controller.applicationGestureAdmission = [this]
    {
        if (automationRecordingActive())
            return automationRecordingAdmission();
        if (nativeScalarBusy())
            return juce::Result::fail("parameterHistory.busy: Finish the owned native scalar edit");
        if (mixerChainApplying())
            return juce::Result::fail("effectChain.busy: Chain Apply owns the document");
        if (devicePreparationBusy())
            return juce::Result::fail("device.busy: Native capture/preparation owns the edit");
        routingCoordinator.poll({patternMode, selectedPattern});
        return routingCoordinator.mutationBlocked()
                   ? juce::Result::fail("routing.busy: Await selected audio before a new gesture")
                   : juce::Result::ok();
    };
    controller.changed = [this]
    {
        const bool covered = routingCoordinator.projectionCovers({patternMode, selectedPattern});
        // A cancelled structural preview may already have started an ordinary
        // preparation even though the incumbent graph covers the restored model.
        // Service that owner once; rebuild retires its unused job without replacing
        // the compatible active graph.
        needsRebuild = !covered || !audio.deferredWorkerPreparationIdle();
        if (covered)
            engineRevision = controller.revision;
        refresh();
    };
    status = profileError.isEmpty() ? "Ready" : "Shortcut profile: " + profileError;
    refresh();
    resized();
    applyAppearance();
    startTimerHz(20);
    if (persistentTyping)
    {
        startAutosaveSession();
        scheduleStartupUpdateCheck(); // At most daily; VIBERA_NO_UPDATE_CHECK disables it.
    }
    if (persistentTyping)
    {
        auto safe = juce::Component::SafePointer<Studio>(this);
        juce::MessageManager::callAsync(
            [safe]
            {
                if (safe)
                {
                    const auto file = safe->profileFile().getSiblingFile("workspace.json");
                    if (file.existsAsFile())
                    {
                        auto result = safe->loadWorkspaceLayout(file);
                        if (result.failed())
                        {
                            safe->status = result.getErrorMessage();
                            safe->repaint();
                        }
                    }
                }
            });
    }
}
Studio::~Studio()
{
    endAutosaveSession();
    cancelUpdateWork();
    if (samplerControllerLearn)
        midiController.cancelProjectLearn(samplerControllerLearn->serial);
    if (samplerControllerLearnWindow)
        delete samplerControllerLearnWindow.getComponent();
    if (samplerControllerPropertiesWindow)
        delete samplerControllerPropertiesWindow.getComponent();
    if (stepComposePending() && stepComposeSession->project == controller.projectIdentity &&
        controller.gestureOwner() == stepComposeSession->owner)
        controller.cancelGestureChecked(stepComposeSession->owner);
    if (stepComposeWindow) delete stepComposeWindow.getComponent();
    if (patternMeterWindow)
    {
        patternMeterWindow->setLookAndFeel(nullptr);
        delete patternMeterWindow.getComponent();
    }
    for (const auto &job : samplerTempoJobs)
        job->cancel.store(true, std::memory_order_relaxed);
    audio.midiInput.controls.endAutomationStamp();
    if (quantizerWindow)
        delete quantizerWindow.getComponent();
    if (persistentTyping)
        saveWorkspaceLayout(profileFile().getSiblingFile("workspace.json"));
    releaseTypingNotes();
    audio.midiInput.historyInterrupted = {};
    audio.typingInput.historyInterrupted = {};
    audio.typingInput.stopCapture();
    audio.typingInput.captureInterrupted = {};
    audio.midiInput.stopCapture();
    audio.midiInput.captureInterrupted = {};
    stopTimer();
    if (patternPropertiesWindow)
    {
        patternPropertiesWindow->setLookAndFeel(nullptr);
        delete patternPropertiesWindow.getComponent();
    }
    if (rackGroupWindow)
    {
        rackGroupWindow->setLookAndFeel(nullptr);
        delete rackGroupWindow.getComponent();
    }
    if (channelPropertiesWindow)
    {
        channelPropertiesWindow->setLookAndFeel(nullptr);
        delete channelPropertiesWindow.getComponent();
    }
    if (settingsWindow)
        delete settingsWindow.getComponent();
    coProducerDock.reset(); // Stops any MIDI preview while Studio is still intact.
    if (parameterWindow)
        delete parameterWindow.getComponent();
    controller.changed = {};
    controller.applicationCommand = {};
    controller.applicationHistory = {};
    controller.applicationGestureAdmission = {};
    instrumentWindows.clear();
    for (auto &detached : detachedWindows)
        detached.reset();
    audio.stop();
    for (auto &w : windows)
        w.reset();
    browser.reset();
    setLookAndFeel(nullptr);
}
void Studio::paint(juce::Graphics &g)
{
    g.fillAll(bg);
    g.setColour(panel);
    g.fillRect(0, 0, getWidth(), 80);
    g.setColour(grid);
    g.drawHorizontalLine(32, 0, float(getWidth()));
    g.drawHorizontalLine(79, 0, float(getWidth()));
    const auto clockMeter = playbackMeter(controller.project, patternMode, selectedPattern);
    const double bar = clockMeter.first * 4. / clockMeter.second;
    double beat = audio.positionBeat();
    // Song position display: a recessed panel with bar : beat, the meter and play state,
    // placed after the transport row whenever it fits before the editor buttons.
    const juce::Rectangle<int> display(snapChoice.getRight() + 12, 38, 196, 36);
    if (display.getRight() + 10 < browserButton.getX())
    {
        const auto box = display.toFloat();
        g.setColour(bg.darker(.35f));
        g.fillRoundedRectangle(box, 6.f);
        g.setColour(grid);
        g.drawRoundedRectangle(box.reduced(.5f), 6.f, 1.f);
        const bool running = audio.playing();
        g.setColour(running ? accent : muted.withAlpha(.6f));
        g.fillEllipse(box.getX() + 10.f, box.getCentreY() - 3.5f, 7.f, 7.f);
        const int barNumber = int(beat / bar) + 1;
        const int beatNumber = int(std::fmod(std::max(0., beat), bar) * clockMeter.second / 4.) + 1;
        g.setColour(running ? accent : ink);
        g.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 21.f, juce::Font::bold));
        g.drawText(juce::String(barNumber).paddedLeft('0', 3) + ":" + juce::String(beatNumber).paddedLeft('0', 2),
                   display.withTrimmedLeft(24).withWidth(104), juce::Justification::centredLeft);
        label(g, "BAR : BEAT", {display.getX() + 128, display.getY() + 3, 64, 14}, muted, 9);
        label(g, juce::String(clockMeter.first) + " / " + juce::String(clockMeter.second),
              {display.getX() + 128, display.getY() + 17, 64, 16}, ink, 13);
    }
    g.setColour(panel);
    g.fillRect(0, getHeight() - 25, getWidth(), 25);
    g.setColour(grid);
    g.drawHorizontalLine(getHeight() - 25, 0, float(getWidth()));
    label(g, status, {12, getHeight() - 24, std::max(200, getWidth() / 2), 23},
          status == "Ready" ? muted : orange, 11);
    if (!workspace.isEmpty() &&
        std::none_of(windows.begin(), windows.end(), [](auto &w) { return w && w->isVisible(); }))
        label(g, "Choose an editor above, or press F5 / F6 / F7 / F9", workspace.reduced(24), muted, 16);
}
void Studio::resized()
{
    title.setVisible(false);
    // Top row: icon buttons grouped by purpose (file, history, project, input, tools) with gaps
    // between groups instead of one long run of identical boxes.
    int fx = 12;
    auto top = [&](juce::Component &c, int width, int gapAfter = 4)
    {
        c.setBounds(fx, 3, width, 26);
        fx += width + gapAfter;
    };
    top(open, 40);
    top(save, 40);
    top(exportWav, 40, 14);
    top(undoButton, 40);
    top(redoButton, 40, 14);
    top(demo, 62);
    top(devices, 40, 14);
    top(click, 40);
    top(countIn, 84);
    top(typingButton, 40, 14);
    top(toolsButton, 40);
    top(coProducerButton, 40);
    coProducerButton.setToggleState(coProducerShown, juce::dontSendNotification);
    int x = 12;
    auto place = [&](juce::Component &c, int width)
    {
        c.setBounds(x, 41, width, 30);
        x += width + 6;
    };
    place(play, 44);
    place(stop, 44);
    place(record, 44);
    place(audioInputButton, 40);
    place(mode, 70);
    place(loopButton, 34);
    const bool compact = getWidth() < 1250;
    place(tempo, compact ? 100 : 118);
    place(tapTempoButton, 36);
    place(patterns, compact ? 100 : 146);
    place(patternOptions, 28);
    place(newPattern, 30);
    place(clonePattern, compact ? 44 : 50);
    place(snapChoice, compact ? 80 : 94);
    int right = getWidth() - 12 - 4 * 84 - 78;
    // The output oscilloscope takes the gap between the transport row and the window buttons.
    const int scopeRoom = right - x - 12;
    outputScope.setVisible(scopeRoom >= 84);
    if (scopeRoom >= 84)
        outputScope.setBounds(x + 6, 41, std::min(180, scopeRoom), 30);
    browserButton.setBounds(right, 41, 72, 30);
    right += 78;
    for (auto &b : editorButtons)
    {
        b.setBounds(right, 41, 78, 30);
        right += 84;
    }
    browserButton.setToggleState(browserShown, juce::dontSendNotification);
    deviceLabel.setBounds(getWidth() / 2, getHeight() - 24, getWidth() / 2 - 12, 23);
    deviceLabel.setFont(juce::Font(vibera::uiFont, 11.f, juce::Font::plain));
    deviceLabel.setColour(juce::Label::textColourId, muted);
    deviceLabel.setJustificationType(juce::Justification::centredRight);
    if (!browser || !windows[0])
        return;
    auto body = juce::Rectangle<int>(4, 84, getWidth() - 8, getHeight() - 113);
    // The CoProducer dock takes the right edge before the browser and editors share the rest.
    if (coProducerSplit)
        coProducerSplit->setVisible(coProducerShown && coProducerDock);
    if (coProducerShown && coProducerDock)
    {
        const int width = juce::jlimit(std::min(340, body.getWidth() / 2), std::max(340, body.getWidth() - 480),
                                       int(coProducerWidth));
        coProducerDock->setBounds(body.removeFromRight(width).withTrimmedRight(-4));
        coProducerSplit->setBounds(body.removeFromRight(7));
    }
    browser->setVisible(browserShown);
    browserSplit->setVisible(browserShown && (autoTileMode || !floatingWindows[4]) && !detachedWindows[4]);
    if (browserShown && !detachedWindows[4])
    {
        const int tileLimit = std::max(190, body.getWidth() - 614);
        const auto browserSlot =
            body.removeFromLeft(autoTileMode ? std::min(int(browserWidth), tileLimit) : int(browserWidth));
        if (!floatingWindows[4] || autoTileMode)
            browser->setBounds(browserSlot);
        browserSplit->setBounds(body.removeFromLeft(7));
    }
    workspace = body;
    for (int i = 0; i < 4; ++i)
        editorButtons[size_t(i)].setToggleState((detachedWindows[size_t(i)]
                                                     ? detachedWindows[size_t(i)]->isVisible()
                                                     : windows[size_t(i)]->isVisible()),
                                                juce::dontSendNotification);
    for (auto *split : {arrangementSplit.get(), rackSplit.get(), mixerSplit.get()})
        split->setVisible(false);
    if (maximisedEditor >= 0 && windows[size_t(maximisedEditor)]->isVisible())
    {
        for (int i = 0; i < 4; ++i)
        {
            if (!detachedWindows[size_t(i)])
                windows[size_t(i)]->setBounds(i == maximisedEditor ? body : juce::Rectangle<int>{});
        }
        windows[size_t(maximisedEditor)]->toFront(false);
        return;
    }
    maximisedEditor = -1;
    if (autoTileMode)
    {
        std::array<bool, 5> visible{};
        for (int i = 0; i < 4; ++i)
            visible[size_t(i)] = windows[size_t(i)]->isVisible() && !detachedWindows[size_t(i)];
        const auto tiles = tileWorkspace(body, visible, autoTileMode == 2 ? tilePrimaryEditor : -1);
        for (int i = 0; i < 4; ++i)
            if (visible[size_t(i)])
            {
                windows[size_t(i)]->setBounds(tiles.bounds[size_t(i)]);
                floatingBounds[size_t(i)] = tiles.bounds[size_t(i)];
            }
        if (browserShown && !detachedWindows[4])
            floatingBounds[4] = browser->getBounds();
        repaint();
        return;
    }
    bool mixer = windows[3]->isVisible() && !detachedWindows[3],
         playlist = windows[2]->isVisible() && !detachedWindows[2],
         piano = windows[1]->isVisible() && !detachedWindows[1],
         rack = windows[0]->isVisible() && !detachedWindows[0];
    auto placeWindow = [this](int index, juce::Rectangle<int> bounds)
    {
        if (!floatingWindows[size_t(index)])
            windows[size_t(index)]->setBounds(bounds);
    };
    // Default docking: the Channel Rack over the Piano roll on the left, the Playlist over the
    // Mixer on the right (each column takes the whole height when the other is hidden).
    const bool leftColumn = rack || piano, rightColumn = playlist || mixer;
    auto leftArea = body, rightArea = body;
    if (leftColumn && rightColumn)
    {
        const int w = juce::jlimit(330, std::max(330, int(body.getWidth() * .55f)), int(rackWidth));
        leftArea = body.removeFromLeft(w);
        rackSplit->setBounds(body.removeFromLeft(7));
        rackSplit->setVisible(!floatingWindows[0] && !floatingWindows[1]);
        rightArea = body;
    }
    if (mixer)
    {
        const int h = playlist ? (getHeight() < 800 ? 32 : juce::jlimit(200, int(rightArea.getHeight() * .5f), int(mixerHeight)))
                               : rightArea.getHeight();
        placeWindow(3, rightArea.removeFromBottom(h));
        if (playlist)
        {
            mixerSplit->setBounds(rightArea.removeFromBottom(7));
            mixerSplit->setVisible(!floatingWindows[3]);
        }
    }
    if (playlist)
        placeWindow(2, rightArea);
    if (rack && piano)
    {
        const int h = int((leftArea.getHeight() - 7) * playlistRatio);
        placeWindow(0, leftArea.removeFromTop(std::max(120, h)));
        arrangementSplit->setBounds(leftArea.removeFromTop(7));
        arrangementSplit->setVisible(!floatingWindows[0] && !floatingWindows[1]);
    }
    else if (rack)
        placeWindow(0, leftArea);
    if (piano)
        placeWindow(1, leftArea);
    const auto floatArea = juce::Rectangle<int>(4, 84, getWidth() - 8, getHeight() - 113);
    std::vector<juce::Component *> raised;
    for (int index = 0; index < 5; ++index)
        if (floatingWindows[size_t(index)] && !detachedWindows[size_t(index)])
        {
            auto bounds = floatingBounds[size_t(index)];
            bounds.setSize(juce::jlimit(index == 4 ? 190 : 300, floatArea.getWidth(), bounds.getWidth()),
                           juce::jlimit(160, floatArea.getHeight(), bounds.getHeight()));
            bounds.setPosition(
                juce::jlimit(floatArea.getX(), floatArea.getRight() - bounds.getWidth(), bounds.getX()),
                juce::jlimit(floatArea.getY(), floatArea.getBottom() - bounds.getHeight(), bounds.getY()));
            floatingBounds[size_t(index)] = bounds;
            auto *component = index == 4 ? static_cast<juce::Component *>(browser.get())
                                         : static_cast<juce::Component *>(windows[size_t(index)].get());
            component->setBounds(bounds);
            component->resized();
            if (component->isVisible())
                raised.push_back(component);
        }
    // Floating windows stay above the docked ones in the order the user last raised them:
    // a layout pass (e.g. from the Browser) must not shuffle the Channel Rack under the Playlist.
    std::sort(raised.begin(), raised.end(), [this](auto *a, auto *b)
              { return getIndexOfChildComponent(a) < getIndexOfChildComponent(b); });
    for (auto *component : raised)
        component->toFront(false);
    repaint();
}
std::vector<Id> Studio::lassoNotes(const std::vector<std::pair<double, double>> &path, bool allChannels) const
{
    std::vector<Id> ids;
    const auto *pattern = controller.project.pattern(selectedPattern);
    if (!pattern || path.size() < 3)
        return ids;
    const auto inside = [&](double x, double y)
    {
        bool in = false;
        for (size_t i = 0, j = path.size() - 1; i < path.size(); j = i++)
        {
            const auto [xi, yi] = path[i];
            const auto [xj, yj] = path[j];
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
                in = !in;
        }
        return in;
    };
    for (const auto &note : pattern->notes)
        if ((allChannels || note.channel == selectedChannel) && inside(note.beat, note.pitch + .5))
            ids.push_back(note.id);
    return ids;
}
void Studio::songJump(double target)
{
    target = std::max(0., target);
    if (songJumpQuantize == 0 || !audio.playing() || patternMode)
    {
        pendingSongJump.reset();
        audio.seekBeat(target);
        return;
    }
    const double now = audio.positionBeat();
    double at = now;
    if (songJumpQuantize < 0)
        at = std::floor(now + 1.e-9) + 1;
    else
    {
        // The first bar line of an n-bar group: count bars from the Song start (meter changes respected).
        at = controller.project.nextBarLine(now);
        int bar = controller.project.barPosition(at).bar;
        while (bar % songJumpQuantize != 0 && at < 1.e6)
        {
            at = controller.project.nextBarLine(at);
            bar = controller.project.barPosition(at).bar;
        }
    }
    pendingSongJump = std::pair{target, at};
    status = "Jump to beat " + juce::String(target, 2) + " at beat " + juce::String(at, 2);
}
void Studio::pollMarkerActions()
{
    if (pendingSongJump && (!audio.playing() || patternMode))
        pendingSongJump.reset();
    else if (pendingSongJump && audio.positionBeat() >= pendingSongJump->second - 1.e-6)
    {
        const auto target = pendingSongJump->first;
        pendingSongJump.reset();
        audio.seekBeat(target);
    }
    const bool playing = audio.playing() && !patternMode;
    const double now = audio.positionBeat();
    auto sorted = controller.project.markers;
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto &x, const auto &y) { return x.beat < y.beat; });
    const auto nextAfter = [&](double beat) -> std::optional<double>
    {
        for (const auto &m : sorted)
            if (m.beat > beat + 1.e-9)
                return m.beat;
        return std::nullopt;
    };
    if (playing && !markerWasPlaying && now < 1.e-6)
        for (const auto &m : sorted)
            if (m.action == "start" && m.beat > 0)
            {
                audio.seekBeat(m.beat); // Start marker: Play from the Song start begins here.
                markerLastBeat = m.beat;
                markerWasPlaying = true;
                return;
            }
    std::optional<std::pair<double, double>> loop;
    if (playing)
    {
        for (const auto &m : sorted)
            if (m.action == "loop" && now >= m.beat - 1.e-9)
                if (const auto end = nextAfter(m.beat); end && now < *end)
                    loop = std::pair{m.beat, *end};
        // Crossed during ordinary forward playback (not a seek or loop wrap).
        if (now > markerLastBeat && now - markerLastBeat < 4)
            for (const auto &m : sorted)
                if (m.beat > markerLastBeat && m.beat <= now)
                {
                    if (m.action == "pause")
                    {
                        play.onClick(); // Pause at the marker.
                        audio.seekBeat(m.beat);
                        break;
                    }
                    if (m.action == "skip")
                    {
                        const auto end = nextAfter(m.beat);
                        audio.seekBeat(end ? *end : controller.project.endBeat());
                        break;
                    }
                }
    }
    if (loop != markerLoop)
    {
        markerLoop = loop;
        if (loop && !audio.looping())
        {
            markerLoopEnabledLooping = performAction("transport.loop");
        }
        else if (!loop && markerLoopEnabledLooping)
        {
            markerLoopEnabledLooping = false;
            if (audio.looping())
                performAction("transport.loop");
        }
    }
    markerWasPlaying = playing;
    markerLastBeat = audio.positionBeat();
}
void Studio::updateMidiScaleSnap()
{
    std::uint32_t mask = 0;
    if (snapIncomingMidi && pianoGuide.name.isNotEmpty())
    {
        const auto marks = score::guideMarks(pianoGuide.name, pianoGuide.root, pianoGuide.maqam);
        for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            if (marks[size_t(pitchClass)] & 3)
                mask |= 1u << pitchClass;
        if (mask)
            mask |= 1u << 12;
    }
    audio.midiInput.scaleSnap.store(mask);
}
void Studio::moveWorkspaceWindow(int index, juce::Rectangle<int> bounds)
{
    if (index < 0 || index >= 5)
        return;
    if (detachedWindows[size_t(index)])
        return;
    leaveAutoTiling();
    floatingWindows[size_t(index)] = true;
    floatingBounds[size_t(index)] = bounds.withHeight(std::max(index == 3 ? 220 : 160, bounds.getHeight()));
    if (maximisedEditor == index)
        maximisedEditor = -1;
    resized();
    if (index == 4)
        browser->resized();
    else
        windows[size_t(index)]->resized();
    auto *component = index == 4 ? static_cast<juce::Component *>(browser.get())
                                 : static_cast<juce::Component *>(windows[size_t(index)].get());
    component->toFront(false);
}
void Studio::dockWorkspaceWindow(int index)
{
    if (index < 0 || index >= 5)
        return;
    if (detachedWindows[size_t(index)])
    {
        auto *component = index == 4 ? static_cast<juce::Component *>(browser.get())
                                     : static_cast<juce::Component *>(windows[size_t(index)].get());
        detachedWindows[size_t(index)]->clearContentComponent();
        detachedWindows[size_t(index)].reset();
        if (index == 4)
        {
            browser->setNativeHosted(false);
            browserShown = true;
        }
        else
            windows[size_t(index)]->setNativeHosted(false);
        addAndMakeVisible(component);
    }
    floatingWindows[size_t(index)] = false;
    if (maximisedEditor == index)
        maximisedEditor = -1;
    resized();
    if (index == 4)
        browser->resized();
    else
        windows[size_t(index)]->resized();
}
void Studio::detachWorkspaceWindow(int index)
{
    if (index < 0 || index >= 5)
        return;
    if (auto &window = detachedWindows[size_t(index)])
    {
        window->setVisible(true);
        window->setMinimised(false);
        bringWindowToFront(*window);
        return;
    }
    auto *component = index == 4 ? static_cast<juce::Component *>(browser.get())
                                 : static_cast<juce::Component *>(windows[size_t(index)].get());
    auto bounds = component->getScreenBounds();
    if (maximisedEditor == index)
        maximisedEditor = -1;
    floatingWindows[size_t(index)] = false;
    if (index == 4)
    {
        browser->setNativeHosted(true);
        browserShown = true;
    }
    else
        windows[size_t(index)]->setNativeHosted(true);
    detachedWindows[size_t(index)] = std::make_unique<DetachedWindow>(*this, index, *component, bounds);
    resized();
}
void Studio::restoreDockedLayout()
{
    autoTileMode = 0;
    for (int index = 0; index < 5; ++index)
        if (detachedWindows[size_t(index)])
            dockWorkspaceWindow(index);
    floatingWindows.fill(false);
    maximisedEditor = -1;
    resized();
    browser->resized();
    for (auto &window : windows)
        window->resized();
}
void Studio::leaveAutoTiling()
{
    if (!autoTileMode)
        return;
    if (maximisedEditor >= 0)
    {
        maximisedEditor = -1;
        resized();
    }
    for (int i = 0; i < 5; ++i)
        if (!detachedWindows[size_t(i)])
        {
            auto *component = i == 4 ? static_cast<juce::Component *>(browser.get())
                                     : static_cast<juce::Component *>(windows[size_t(i)].get());
            if (component->isVisible())
            {
                floatingBounds[size_t(i)] = component->getBounds();
                floatingWindows[size_t(i)] = true;
            }
        }
    autoTileMode = 0;
}
void Studio::tileWorkspaceWindows(int mode)
{
    if (mode == 0)
        leaveAutoTiling();
    else
    {
        autoTileMode = mode;
        tilePrimaryEditor = int(focus) >= 1 && int(focus) <= 4 ? int(focus) - 1 : 2;
        maximisedEditor = -1;
    }
    resized();
}
void Studio::tileDetachedWindows()
{
    const auto areas = desktopWorkAreas();
    std::vector<std::array<bool, 5>> groups(areas.size());
    for (int i = 0; i < 5; ++i)
        if (auto *window = detachedWindows[size_t(i)].get(); window && window->isVisible() && !areas.empty())
        {
            size_t best = 0;
            double distance = std::numeric_limits<double>::infinity();
            const auto centre = window->savedBounds().getCentre().toDouble();
            for (size_t display = 0; display < areas.size(); ++display)
            {
                const auto area = areas[display];
                const auto nearest = juce::Point<double>(
                    juce::jlimit(double(area.getX()), double(area.getRight()), centre.x),
                    juce::jlimit(double(area.getY()), double(area.getBottom()), centre.y));
                const double current = centre.getDistanceSquaredFrom(nearest);
                if (current < distance)
                {
                    best = display;
                    distance = current;
                }
            }
            groups[best][size_t(i)] = true;
        }
    for (size_t display = 0; display < areas.size(); ++display)
    {
        const auto tiles = tileWorkspace(areas[display].reduced(8), groups[display], -1, true);
        if (!tiles.fits)
            continue; // Preserve usable native windows if this work area cannot fit their minimum sizes.
        for (int i = 0; i < 5; ++i)
            if (groups[display][size_t(i)])
            {
                auto *window = detachedWindows[size_t(i)].get();
                window->setFullScreen(false);
                window->setMinimised(false);
                window->setBounds(tiles.bounds[size_t(i)]);
            }
    }
}
juce::Result Studio::saveWorkspaceLayout(const juce::File &file) const
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("version", 2);
    object->setProperty("clipGroupingEnabled", controller.clipGroupingEnabled);
    object->setProperty("autoTileMode", autoTileMode);
    object->setProperty("tilePrimaryEditor", tilePrimaryEditor);
    object->setProperty("browserWidth", browserWidth);
    object->setProperty("rackWidth", rackWidth);
    object->setProperty("mixerHeight", mixerHeight);
    object->setProperty("playlistRatio", playlistRatio);
    juce::Array<juce::var> panels;
    for (int index = 0; index < 5; ++index)
    {
        auto item = std::make_unique<juce::DynamicObject>();
        auto *native = detachedWindows[size_t(index)].get();
        auto *component = index == 4 ? static_cast<juce::Component *>(browser.get())
                                     : static_cast<juce::Component *>(windows[size_t(index)].get());
        const auto bounds = native ? native->savedBounds() : component->getBounds();
        item->setProperty("mode", native                           ? "detached"
                                  : floatingWindows[size_t(index)] ? "floating"
                                                                   : "docked");
        item->setProperty("visible", native       ? native->isVisible()
                                     : index == 4 ? browserShown
                                                  : component->isVisible());
        item->setProperty("bounds", juce::Array<juce::var>{bounds.getX(), bounds.getY(), bounds.getWidth(),
                                                           bounds.getHeight()});
        panels.add(juce::var(item.release()));
    }
    object->setProperty("panels", panels);
    juce::TemporaryFile temporary(file);
    if (file.getParentDirectory().createDirectory().failed() ||
        !temporary.getFile().replaceWithText(juce::JSON::toString(juce::var(object.release()))) ||
        !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Cannot save workspace layout");
    return juce::Result::ok();
}
juce::Result Studio::loadWorkspaceLayout(const juce::File &file)
{
    const auto value = juce::JSON::parse(file);
    auto invalid = [] { return juce::Result::fail("Invalid workspace layout; current layout preserved"); };
    if (controller.hasGesture() ||
        (value.hasProperty("clipGroupingEnabled") && !value["clipGroupingEnabled"].isBool()))
        return invalid();
    if (!value.isObject() || !value["version"].isInt() ||
        (int(value["version"]) != 1 && int(value["version"]) != 2) || !value["panels"].isArray() ||
        value["panels"].size() != 5)
        return invalid();
    if (int(value["version"]) == 2 &&
        (!value["autoTileMode"].isInt() || int(value["autoTileMode"]) < 0 || int(value["autoTileMode"]) > 2 ||
         !value["tilePrimaryEditor"].isInt() || int(value["tilePrimaryEditor"]) < 0 ||
         int(value["tilePrimaryEditor"]) > 3))
        return invalid();
    auto numeric = [](const juce::var &v) { return v.isInt() || v.isInt64() || v.isDouble(); };
    for (auto name : {"browserWidth", "rackWidth", "mixerHeight", "playlistRatio"})
        if (!numeric(value[name]) || !std::isfinite(double(value[name])))
            return invalid();
    if (double(value["browserWidth"]) < 190 || double(value["browserWidth"]) > 2000 ||
        double(value["rackWidth"]) < 300 || double(value["rackWidth"]) > 4000 ||
        double(value["mixerHeight"]) < 160 || double(value["mixerHeight"]) > 2000 ||
        double(value["playlistRatio"]) < .1 || double(value["playlistRatio"]) > .9)
        return invalid();
    std::array<juce::Rectangle<int>, 5> bounds;
    for (int index = 0; index < 5; ++index)
    {
        const auto item = value["panels"][index];
        const auto mode = item["mode"].toString();
        const auto rect = item["bounds"];
        if (!item.isObject() || !item["visible"].isBool() ||
            (mode != "docked" && mode != "floating" && mode != "detached") || !rect.isArray() ||
            rect.size() != 4)
            return invalid();
        for (int coordinate = 0; coordinate < 4; ++coordinate)
            if (!rect[coordinate].isInt() || std::abs(double(rect[coordinate])) > 100000)
                return invalid();
        bounds[size_t(index)] = {int(rect[0]), int(rect[1]), int(rect[2]), int(rect[3])};
        if ((mode != "docked" && (int(rect[2]) < (index == 4 ? 190 : 300) || int(rect[3]) < 160)) ||
            int(rect[2]) < 0 || int(rect[3]) < 0 || int(rect[2]) > 7680 || int(rect[3]) > 4320)
            return invalid();
    }
    // Validation finishes before changing any existing panel or native peer.
    restoreDockedLayout();
    browserWidth = float(double(value["browserWidth"]));
    rackWidth = float(double(value["rackWidth"]));
    mixerHeight = float(double(value["mixerHeight"]));
    playlistRatio = float(double(value["playlistRatio"]));
    resized();
    for (int index = 0; index < 5; ++index)
    {
        const auto item = value["panels"][index];
        const auto mode = item["mode"].toString();
        if (mode == "detached")
        {
            detachWorkspaceWindow(index);
            detachedWindows[size_t(index)]->setBounds(
                recoverDesktopWindow(bounds[size_t(index)], desktopWorkAreas()));
            detachedWindows[size_t(index)]->setVisible(bool(item["visible"]));
        }
        else
        {
            if (mode == "floating")
                moveWorkspaceWindow(index, bounds[size_t(index)]);
            if (index != 4)
                windows[size_t(index)]->setVisible(bool(item["visible"]));
        }
        if (index == 4)
            browserShown = bool(item["visible"]);
    }
    controller.clipGroupingEnabled = bool(value.getProperty("clipGroupingEnabled", true));
    autoTileMode = int(value["version"]) == 2 ? int(value["autoTileMode"]) : 0;
    tilePrimaryEditor = int(value["version"]) == 2 ? int(value["tilePrimaryEditor"]) : 2;
    resized();
    return juce::Result::ok();
}
void Studio::toggleEditor(int i)
{
    if (i == 1 && noteSlicePending())
        finishNoteSlice(false, "Piano Roll visibility changed");
    if (i == 1 && noteStretchPending())
        finishNoteStretch(false, "Piano Roll visibility changed");
    if (i == 3 && static_cast<Editor *>(windows[3]->getContentComponent())->mixerSendGestureActive())
    {
        auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
        editor->cancelPointerGesture();
        if (editor->mixerSendGestureActive())
            return; // Checked cancellation failed; retain visible recovery.
    }
    previewNote(-1);
    if (i < 0 || i > 3)
        return;
    if (detachedWindows[size_t(i)])
    {
        auto &window = detachedWindows[size_t(i)];
        window->setVisible(!window->isVisible());
        if (window->isVisible())
        {
            window->setMinimised(false);
            bringWindowToFront(*window);
        }
        resized();
        return;
    }
    if (windows[size_t(i)]->isVisible())
    {
        windows[size_t(i)]->setVisible(false);
        if (maximisedEditor == i)
            maximisedEditor = -1;
    }
    else
        showEditor(i);
    resized();
}
void Studio::maximiseEditor(int i)
{
    if (i < 0 || i >= 4)
        return;
    if (detachedWindows[size_t(i)])
    {
        auto &window = detachedWindows[size_t(i)];
        window->setVisible(true);
        window->setFullScreen(!window->isFullScreen());
        return;
    }
    previewNote(-1);
    maximisedEditor = maximisedEditor == i ? -1 : i;
    windows[size_t(i)]->setVisible(true);
    resized();
    focus = Scope(i + 1);
    windows[size_t(i)]->getContentComponent()->grabKeyboardFocus();
}
juce::Result Studio::finishRackEditsChecked()
{
    if (automationRecordingActive())
    {
        status = "automationRecording.busy: Finish/retry/cancel the automation take";
        return automationRecordingAdmission();
    }
    if (nativeScalarBusy() && !nativeScalarCaptureDispatch)
        return juce::Result::fail("parameterHistory.busy: Native scalar ACK/state capture is still owned");
    auto result = finishRackVelocity();
    if (result.wasOk())
        result = finishRackSwing();
    if (result.failed())
        status = result.getErrorMessage();
    return result;
}
void Studio::command(const juce::String &id, CommandArgs a, const juce::String &owner)
{
    if (id == "automation.source.fromAudio")
    {
        // FL "Analyze audio file": the file's level (RMS per step, normalised to its loudest step)
        // becomes the curve over the source length at the project tempo. One Undo step.
        const auto *source = controller.project.automationSource(Id(a.target));
        const juce::File file(a.text);
        const double step = a.value == 0 ? .25 : a.value;
        auto fail = [this](const juce::String &message)
        {
            status = "automation.source.fromAudio: " + message;
            repaint();
        };
        if (!source)
            return fail("target is an Automation source ID");
        if (!juce::File::isAbsolutePath(a.text) || !file.existsAsFile())
            return fail("text is the absolute path of an audio file");
        if (!std::isfinite(step) || step < 1. / 64 || step > 4)
            return fail("value is the step in beats, 1/64..4");
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader || reader->sampleRate <= 0)
            return fail("cannot read " + file.getFileName());
        const double secondsPerBeat = 60. / controller.project.tempo;
        const int steps = std::clamp(int(std::ceil(source->length / step)), 1, 4095);
        std::vector<double> levels;
        juce::AudioBuffer<float> block(int(reader->numChannels), 1);
        for (int i = 0; i <= steps; ++i)
        {
            const double from = std::min(i * step, source->length) * secondsPerBeat;
            const auto start = juce::int64(from * reader->sampleRate);
            const int count = int(step * secondsPerBeat * reader->sampleRate);
            double sum = 0;
            if (start < reader->lengthInSamples && count > 0)
            {
                const int n = int(std::min<juce::int64>(count, reader->lengthInSamples - start));
                block.setSize(int(reader->numChannels), n, false, false, true);
                reader->read(&block, 0, n, start, true, true);
                for (int c = 0; c < block.getNumChannels(); ++c)
                    sum += std::pow(block.getRMSLevel(c, 0, n), 2.);
                sum = std::sqrt(sum / std::max(1, block.getNumChannels()));
            }
            levels.push_back(sum);
        }
        const double loudest = *std::max_element(levels.begin(), levels.end());
        if (loudest <= 0)
            return fail("the file is silent over the curve");
        juce::Array<juce::var> list;
        for (int i = 0; i <= steps; ++i)
            list.add(juce::Array<juce::var>{std::min(i * step, source->length), levels[size_t(i)] / loudest});
        CommandArgs points = a;
        points.text = juce::JSON::toString(juce::var(list), true);
        command("automation.source.points", points, owner);
        return;
    }
    if (id == "clip.uniqueSample" && a.text.isEmpty())
    {
        // FL "Make unique as sample": the clip gets its own Channel and its own copy of the audio
        // file, next to the original. Undo restores the clip; the copied file stays on disk.
        const auto &placements = controller.project.audioPlacements;
        const auto clip = std::find_if(placements.begin(), placements.end(),
                                       [&](const auto &p) { return p.id == a.target; });
        const auto *channel = clip != placements.end() ? controller.project.channel(clip->channel) : nullptr;
        const juce::File original = channel ? juce::File(channel->source) : juce::File{};
        if (!channel || !juce::File::isAbsolutePath(channel->source) || !original.existsAsFile())
        {
            status = "clip.uniqueSample: select an Audio Clip whose audio file exists";
            repaint();
            return;
        }
        const auto copy = original.getParentDirectory().getNonexistentChildFile(
            original.getFileNameWithoutExtension() + " unique", original.getFileExtension(), false);
        if (!original.copyFileTo(copy))
        {
            status = "clip.uniqueSample: could not copy " + original.getFileName() + " (folder not writable?)";
            repaint();
            return;
        }
        a.text = copy.getFullPathName();
        const auto revision = controller.revision;
        command(id, a, owner);
        if (controller.revision == revision)
            copy.deleteFile(); // The model step refused: no orphan copy.
        return;
    }
    // Multi-output routing restarts the instrument's worker with new buses: capture every plugin's
    // current native state first (as Save does), so the restart keeps it, then apply the edit.
    if (id == "channel.outputRoutes" && !outputRoutesCaptured && audio.hasAudioDevice())
    {
        auto safe = juce::Component::SafePointer<Studio>(this);
        const auto identity = controller.projectIdentity;
        status = "Capturing plugin state for the new outputs...";
        repaint();
        withPluginState(
            [safe, identity, a, owner](juce::Result captured, Project snapshot)
            {
                if (!safe || safe->controller.projectIdentity != identity)
                    return;
                if (captured.failed())
                {
                    safe->status = "channel.outputRoutes.captureFailed: " + captured.getErrorMessage();
                    safe->repaint();
                    return;
                }
                for (auto &channel : safe->controller.project.channels)
                    if (channel.instrument)
                        if (const auto *fresh = snapshot.plugin(channel.instrument->id))
                            channel.instrument->state = fresh->state;
                for (auto &insert : safe->controller.project.mixer)
                    for (auto &effect : insert.effects)
                        if (const auto *fresh = snapshot.plugin(effect.id))
                            effect.state = fresh->state;
                safe->outputRoutesCaptured = true;
                safe->command("channel.outputRoutes", a, owner);
                safe->outputRoutesCaptured = false;
            });
        return;
    }
    if (id == "pattern.burnChannelLoop" && (recordArmed || midiTaking || hasPendingMidiTake()))
    {
        status = "channelLoop.transportBusy: Stop recording before materializing loop identities";
        repaint();
        return;
    }
    lastRoutingDispatchOperation = 0;
    lastStructuralOperation.clear();
    lastMixerEffectReplacementOperation.clear();
    lastMixerEffectSidechainOperation.clear();
    lastLifecycleOperation.clear();
    if (requiresBoundedMixerStructural(id, a) || id == "plugin.effect.replace" ||
        id == "plugin.effect.sidechain" || isMixerInsertLifecycleCommand(id))
    {
        // Replacement must refuse active takes/previews through shared admission,
        // rather than first committing them via the generic structural cleanup.
        const auto result = controller.execute(
            id, a,
            owner.isNotEmpty() ? owner
                               : (mcpGestureDispatch && mcpGesture ? mcpGesture->id : juce::String{}));
        // Canonical plugin.io explicitly admits an unchanged selection without
        // a structural owner or history edge. Do not invent pending work for it.
        status = result.failed() ? result.getErrorMessage()
                 : id == "plugin.io" && lastStructuralOperation.isEmpty()
                     ? "Ready"
                     : "Mixer structural change pending; inspect its operation";
        repaint();
        return;
    }
    if (isMixerInsertOrderCommand(id))
    {
        juce::String code;
        if (auto result = admitMixerInsertOrder(code, owner); result.failed())
        { status = result.getErrorMessage(); repaint(); return; }
    }
    if (id == "plugin.effect.mix" || id == "mixer.effects.bypass" ||
        id == "mixer.stereoSeparation" || id == "mixer.phaseInvert" || id == "mixer.stereoSwap")
    {
        juce::String code;
        if (auto result = admitMixerChainControl(code, owner); result.failed())
        {
            status = result.getErrorMessage();
            repaint();
            return;
        }
    }
    if (id == "plugin.effect.move")
    {
        juce::String code;
        if (auto result = admitMixerEffectMove(code); result.failed())
        {
            status = result.getErrorMessage();
            repaint();
            return;
        }
    }
    if (stepComposePending() && owner != stepComposeSession->owner)
    {
        status = "stepCompose.busy: Accept or Cancel Advanced Fill first";
        return;
    }
    if (noteStretchPending() && !committingNoteStretch)
        finishNoteStretch(false, "Another command superseded stretch preview");
    if (noteSlicePending() && !committingNoteSlice)
        finishNoteSlice(false, "Another command superseded the Slice preview");
    if (id == "notes.quantize" || id == "notes.quickQuantize" || id == "notes.quantizeStarts")
        if (auto admitted = quantizerEditAdmission(recordArmed, midiTaking || hasPendingMidiTake(),
                                                   bool(afterPluginState) || bool(pendingExport),
                                                   audio.rendering(), midiFileBusy);
            admitted.failed())
        {
            status = admitted.getErrorMessage();
            return;
        }
    if (id != "steps.velocity" && id != "steps.pitch" && id != "steps.pan" && id != "steps.fine" &&
        id != "steps.modX" && id != "steps.modY" && id != "steps.shift" && id != "steps.repeat" &&
        id != "steps.release")
    {
        if (auto result = finishRackVelocity(); result.failed())
        {
            status = result.getErrorMessage();
            return;
        }
    }
    else if (auto admitted = stepVelocityAdmission(); admitted.failed())
    {
        status = admitted.getErrorMessage();
        return;
    }
    if (id != "project.swing")
        if (auto result = finishRackSwing(); result.failed())
        {
            status = result.getErrorMessage();
            return;
        }
    if (isSwingCommand(id))
        if (auto admitted = stepVelocityAdmission(); admitted.failed())
        {
            status = admitted.getErrorMessage();
            return;
        }
    if ((id == "rack.swingMix" || id == "rack.truncateSwing") && a.ids.empty())
        a.ids = selectedChannels;
    if (!a.restore)
        a.restore = customization.restoreMuteAfterSolo;
    if (isStepRetentionCommand(id) && !a.retainSteps)
        a.retainSteps = customization.muteRemovedSteps;
    if ((id == "rack.muteSelected" || id == "rack.unmuteSelected" || id == "rack.muteLock") && a.ids.empty())
        a.ids = selectedChannels;
    if (id.startsWith("rack.group.") && windows[0])
        static_cast<Editor *>(windows[0]->getContentComponent())->finishRackGesture();
    if ((id == "channel.add" || id == "plugin.instrument.create" || (id == "audio.add" && a.channel == 0)) &&
        a.target == 0 && rackFilter == "group")
        a.target = rackGroup;
    if (id.startsWith("workspace."))
    {
        if (id == "workspace.tileBalanced")
            tileWorkspaceWindows(1);
        else if (id == "workspace.tileFocused")
            tileWorkspaceWindows(2);
        else if (id == "workspace.free")
            tileWorkspaceWindows(0);
        else if (id == "workspace.tileDetached")
            tileDetachedWindows();
        else if (id == "workspace.docked")
            restoreDockedLayout();
        else
        {
            status = "Unknown workspace command";
            return;
        }
        status = "Ready";
        repaint();
        return;
    }
    // Window/meter commands validate canonically without finishing a take on an
    // invalid/no-op request. Final admission pins an actively captured window.
    // Other structural edits commit the take first; display metadata and selector ordering
    // leave its Pattern/Channel IDs and musical clock intact.
    if (id == "channel.preset")
    {
        Channel preset;
        if (auto decoded = decodeChannelPreset(a.text, preset); decoded.failed())
        {
            status = decoded.getErrorMessage();
            repaint();
            return;
        }
        if (!preset.instrument && !preset.source.startsWith("builtin:"))
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader;
            if (juce::File::isAbsolutePath(preset.source))
                reader.reset(formats.createReaderFor(juce::File(preset.source)));
            if (!reader || reader->lengthInSamples < 2 || reader->lengthInSamples > reader->sampleRate * 30)
            {
                status = "channelPreset.missingSample: Preset sample is missing or unsupported: " + preset.source;
                repaint();
                return;
            }
        }
    }
    if (id == "channel.add" || id == "channel.source")
    {
        if (id == "channel.source")
            if (const auto *target = controller.project.channel(a.channel); target && target->audioClip)
            {
                status = "Drop a new Audio Clip in Playlist; source replacement is not implemented yet";
                repaint();
                return;
            }
        if (!a.text.startsWith("builtin:"))
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader;
            if (juce::File::isAbsolutePath(a.text))
                reader.reset(formats.createReaderFor(juce::File(a.text)));
            if (!reader || reader->lengthInSamples < 2 || reader->lengthInSamples > reader->sampleRate * 30)
            {
                status = "Choose a supported sample up to 30 seconds long";
                repaint();
                return;
            }
        }
    }
    if ((midiTaking || hasPendingMidiTake()) && !isMixerInsertOrderCommand(id) && !isMixerGroupCommand(id) &&
        id != "mixer.rename" && id != "mixer.colour" && id != "mixer.solo" && id != "mixer.soloConnected" &&
        id != "mixer.unsolo" && id != "mixer.unmuteAll" && id != "mixer.route" && id != "mixer.routeOnly" &&
        id != "mixer.send" && !(id.startsWith("notes.") && id != "notes.record" && id != "notes.import") &&
        id != "steps.set" && id != "steps.compose" && id != "pattern.length" && id != "pattern.extent" &&
        id != "pattern.meter" && id != "clip.group" && id != "clip.ungroup" && !isSwingCommand(id) &&
        !id.startsWith("rack.group.") && !id.startsWith("steps.fill") && id != "steps.velocity" &&
        id != "steps.pitch" && id != "steps.pan" && id != "steps.fine" && id != "steps.modX" &&
        id != "steps.modY" && id != "steps.shift" && id != "steps.repeat" && id != "steps.release" &&
        id != "channel.moveUp" && id != "channel.moveDown" && id != "channel.properties" &&
        id != "rack.properties" && id != "rack.sort" && id != "channel.solo" && id != "channel.unsolo" &&
        id != "channel.muteLock" && id != "rack.muteLock" && id != "rack.muteSelected" &&
        id != "rack.unmuteSelected" && id != "pattern.channelLoop" && id != "pattern.group" &&
        id != "pattern.rename" && id != "pattern.properties" && id != "pattern.move" &&
        !id.startsWith("channel.sampler.") && !id.startsWith("automation.") &&
        !id.startsWith("plugin.binding.") && id != "mixer.gain" && id != "mixer.pan" && id != "mixer.mute" &&
        id != "mixer.stereoSeparation" && id != "mixer.phaseInvert" && id != "mixer.stereoSwap" &&
        id != "channel.gain" && id != "channel.pan" && id != "channel.mute" && id != "plugin.effect.mix" &&
        id != "mixer.effects.bypass")
    {
        if (auto result = finishMidiTake(); result.failed())
        {
            status = result.getErrorMessage();
            repaint();
            return;
        }
    }
    const auto previousNextId = controller.project.nextId;
    const auto *pastePattern = id == "channel.paste" ? controller.project.pattern(a.pattern) : nullptr;
    const bool revealPastedPart = pastePattern && a.pattern == selectedPattern &&
                                  a.channel == selectedChannel &&
                                  std::none_of(pastePattern->notes.begin(), pastePattern->notes.end(),
                                               [&](const auto &note) { return note.channel == a.channel; });
    auto result = controller.execute(
        id, a,
        owner.isNotEmpty() ? owner : (mcpGestureDispatch && mcpGesture ? mcpGesture->id : juce::String{}));
    status = result.wasOk() ?
        (lastRoutingDispatchOperation && routingPending() ? routingPendingStatus : routingReadyStatus)
        : result.getErrorMessage();
    if (result.wasOk() && id == "notes.quantize" && controller.hasQuantizerPreview())
    {
        quantizerGuide = a.groove;
        quantizerGuidePattern = a.pattern;
        quantizerGuideSerial = controller.gestureSerial();
    }
    if (result.wasOk() && revealPastedPart && windows[1])
        static_cast<Editor *>(windows[1]->getContentComponent())->revealChannel(true);
    if (result.wasOk() && id == "rack.group.create")
        setRackFilter("group", previousNextId);
    if (result.wasOk() && id == "rack.group.assign" && !a.ids.empty() &&
        (a.target != 0 || a.text.isNotEmpty()))
        if (const auto *channel = controller.project.channel(a.ids.front()))
            setRackFilter("group", channel->group);
    if (result.wasOk() && id == "notes.glue" && a.pattern == selectedPattern && !a.ids.empty())
    {
        std::vector<Id> survivors;
        if (const auto *pattern = controller.project.pattern(selectedPattern))
            for (const auto &note : pattern->notes)
                if (selected(a.ids, note.id))
                    survivors.push_back(note.id);
        selectItems(false, survivors, "replace");
    }
    if (result.wasOk() && id == "notes.chop" && a.pattern == selectedPattern && !a.ids.empty())
    {
        selectedNotes = a.ids;
        if (const auto *pattern = controller.project.pattern(selectedPattern))
            for (const auto &note : pattern->notes)
                if (note.id >= previousNextId)
                    selectedNotes.push_back(note.id);
    }
    if (result.wasOk() && id == "clip.unique")
    {
        for (auto &clip : controller.project.placements)
            if (clip.id == a.target)
                selectedPattern = clip.pattern;
        selectedNotes.clear();
        refresh();
    }
    if (result.wasOk() && recordingNotes && !customization.running &&
        (id == "notes.duplicate" || id == "notes.move" || id == "notes.velocity" || id == "notes.quantize" ||
         id == "notes.quickQuantize" || id == "notes.quantizeStarts" || id == "notes.legato" ||
         id == "notes.chop" || id == "notes.glue" || id == "notes.mute" || id == "notes.unmute" ||
         id == "notes.slice" || id == "notes.stretch" || id == "mixer.route" || id == "mixer.routeOnly" ||
         (id.startsWith("mixer.") && !controller.hasGesture())))
    {
        if (recordedSteps.size() < 64)
            recordedSteps.push_back({id, a, !isMixerGroupCommand(id) &&
                (!isMixerInsertOrderCommand(id) || a.mixer == selectedInsert)});
        else
        {
            recordingNotes = false;
            status = "Recording stopped at 64 actions";
        }
    }
    repaint();
}
void Studio::showEditor(int i)
{
    if (i < 0 || i > 3)
        return;
    if (autoTileMode == 2)
        tilePrimaryEditor = i;
    auto &w = windows[size_t(i)];
    if (detachedWindows[size_t(i)])
    {
        auto &window = detachedWindows[size_t(i)];
        window->setVisible(true);
        window->setMinimised(false);
        bringWindowToFront(*window);
        w->getContentComponent()->grabKeyboardFocus();
        focus = Scope(i + 1);
        resized();
        return;
    }
    w->setVisible(true);
    if (maximisedEditor >= 0)
        maximisedEditor = i;
    resized();
    bringWindowToFront(*w);
    w->getContentComponent()->grabKeyboardFocus();
    focus = Scope(i + 1);
}
void Studio::selectPattern(Id id)
{
    if (mixerChainApplying())
    {
        status = "effectChain.busy: Chain Apply owns the source projection";
        return;
    }
    if (devicePreparationBusy()) { status = devicePreparationPendingStatus; return; }
    if (routingPending()) { status = routingPendingStatus; return; }
    if (!controller.project.pattern(id) || selectedPattern == id)
        return;
    if (finishRackEditsChecked().failed())
        return;
    if (auto result = finishMidiTake(); result.failed())
    {
        status = result.getErrorMessage();
        return;
    }
    selectedPattern = id;
    selectedNotes.clear();
    needsRebuild = true;
    refresh();
}
std::vector<Id> Studio::effectiveClipSelection() const
{
    return controller.clipGroupingEnabled ? expandedClipSelection(controller.project, selectedClips)
                                          : selectedClips;
}
juce::Result Studio::setPlaylistGrouping(bool enabled)
{
    if (controller.clipGroupingEnabled == enabled)
        return juce::Result::ok();
    if (controller.hasGesture())
        return juce::Result::fail("Finish or cancel the active edit before changing grouping behavior");
    controller.clipGroupingEnabled = enabled;
    refresh();
    return juce::Result::ok();
}
juce::Result Studio::selectItems(bool clips, const std::vector<Id> &ids, const juce::String &mode)
{
    if (mode != "replace" && mode != "add" && mode != "remove" && mode != "toggle")
        return juce::Result::fail("Unknown selection mode");
    std::set<Id> valid;
    if (clips)
    {
        for (const auto &clip : controller.project.placements)
            valid.insert(clip.id);
        for (const auto &clip : controller.project.audioPlacements)
            valid.insert(clip.id);
        for (const auto &clip : controller.project.automation.placements)
            valid.insert(clip.id);
    }
    else if (const auto *pattern = controller.project.pattern(selectedPattern))
        for (const auto &note : pattern->notes)
            valid.insert(note.id);
    for (auto id : ids)
        if (!valid.contains(id))
            return juce::Result::fail("Selection target is unavailable in the current project/Pattern");
    auto &selection = clips ? selectedClips : selectedNotes;
    if (clips && controller.clipGroupingEnabled && (mode == "toggle" || mode == "remove"))
    {
        const auto effective = effectiveClipSelection();
        auto next = selection;
        for (auto id : ids)
        {
            const auto members = expandedClipSelection(controller.project, {id});
            if (mode == "remove" || std::find(effective.begin(), effective.end(), id) != effective.end())
                std::erase_if(next, [&](Id member)
                              { return std::find(members.begin(), members.end(), member) != members.end(); });
            else if (std::find(next.begin(), next.end(), id) == next.end())
                next.push_back(id);
        }
        selection = std::move(next);
        refresh();
        return juce::Result::ok();
    }
    auto next = mode == "replace" ? std::vector<Id>{} : selection;
    for (auto id : ids)
    {
        auto found = std::find(next.begin(), next.end(), id);
        if (mode == "remove" || (mode == "toggle" && found != next.end()))
        {
            if (found != next.end())
                next.erase(found);
        }
        else if (found == next.end())
            next.push_back(id);
    }
    selection = std::move(next);
    refresh();
    return juce::Result::ok();
}
void Studio::refresh()
{
    syncSamplerControllerBindings();
    for (auto &w : windows)
        if (w)
            if (auto *editor = dynamic_cast<Editor *>(w->getContentComponent()))
                editor->setDescription(editor->accessibleSummary());
    mode.setButtonText(patternMode ? "Pattern" : "Song");
    loopButton.setToggleState(audio.looping(), juce::dontSendNotification);
    loopButton.setEnabled(!midiTaking && !needsRebuild);
    if (typingTarget != selectedChannel)
    {
        releaseTypingNotes();
        typingTarget = selectedChannel;
    }
    typingButton.setToggleState(typingKeys.enabled, juce::dontSendNotification);
    click.setToggleState(audio.metronome(), juce::dontSendNotification);
    countIn.setToggleState(audio.countInBars() > 0, juce::dontSendNotification);
    countIn.setButtonText(audio.countInBars() ? "Count " + juce::String(audio.countInBars()) + " bar" +
                                                    (audio.countInBars() > 1 ? "s" : "")
                                              : "Count-in");
    if (!controller.project.pattern(selectedPattern))
        selectedPattern = controller.project.patterns.front().id;
    if (!controller.project.channel(selectedChannel))
        selectedChannel = controller.project.channels.empty() ? 0 : controller.project.channels.front().id;
    std::erase_if(instrumentWindows,
                  [&](const auto &entry) { return !controller.project.channel(entry.first); });
    if (pendingInstrument && !pendingInstrument->matches(controller.projectIdentity, controller.project))
        pendingInstrument.reset();
    if (!controller.hasQuantizerPreview() || controller.gestureSerial() != quantizerGuideSerial)
        quantizerGuide.reset();
    reconcileRack();
    reconcileMixerSelection();
    audio.setMidiTarget(selectedChannel);
    if (auto *pattern = controller.project.pattern(selectedPattern);
        pattern && !controller.hasQuantizerPreview())
        std::erase_if(selectedNotes,
                      [&](Id id)
                      {
                          return std::none_of(pattern->notes.begin(), pattern->notes.end(),
                                              [&](auto &n) { return n.id == id; });
                      });
    std::erase_if(selectedClips,
                  [&](Id id)
                  {
                      return std::none_of(controller.project.placements.begin(),
                                          controller.project.placements.end(),
                                          [&](auto &p) { return p.id == id; }) &&
                             std::none_of(controller.project.audioPlacements.begin(),
                                          controller.project.audioPlacements.end(),
                                          [&](auto &p) { return p.id == id; }) &&
                             std::none_of(controller.project.automation.placements.begin(),
                                          controller.project.automation.placements.end(),
                                          [&](auto &p) { return p.id == id; });
                  });
    patterns.clear(juce::dontSendNotification);
    for (size_t i = 0; i < controller.project.patterns.size(); ++i)
    {
        auto &p = controller.project.patterns[i];
        patterns.addItem(p.name, int(i) + 1);
        if (p.id == selectedPattern)
            patterns.setSelectedId(int(i) + 1, juce::dontSendNotification);
    }
    tempo.setValue(controller.project.tempo, juce::dontSendNotification);
    snapChoice.setSelectedId(snap == 0     ? 1
                             : snap == .25 ? 2
                             : snap == .5  ? 3
                                           : 4,
                             juce::dontSendNotification);
    for (auto &w : windows)
        if (w)
        {
            static_cast<Editor *>(w->getContentComponent())->revealChannel();
            static_cast<Editor *>(w->getContentComponent())->syncPicker();
            static_cast<Editor *>(w->getContentComponent())->syncRack();
            w->repaint();
        }
    repaint();
}
void Studio::drainScoreLog()
{
    ScoreQueue *queues[]{&audio.midiInput.history, &audio.typingInput.history};
    if (queues[0]->failed.load() || queues[1]->failed.load())
    {
        clearScoreLog();
        status = "Score history overflow: incomplete history discarded; live input and project retained";
        return;
    }
    for (unsigned source = 0; source < 2; ++source)
    {
        auto &queue = *queues[source];
        if (queue.failed.exchange(false))
        {
            scoreLog.clear();
            queue.clear();
            status = "Score history overflow: incomplete history discarded; live input and project retained";
        }
        MidiCapture::Event event;
        for (int i = 0; i < int(queue.capacity) && queue.pop(event); ++i)
            if (event.session == queue.generation.load())
            {
                event.source = source;
                scoreLog.add(event);
                if (stepRecord && !audio.playing())
                    stepRecordEvent(event);
            }
    }
    scoreLog.prune(juce::Time::getMillisecondCounterHiRes() / 1000.);
}
void Studio::stepRecordEvent(const MidiCapture::Event &event)
{
    const int kind = event.bytes[0] & 0xf0, pitch = event.bytes[1] & 127, velocity = event.bytes[2];
    if (kind == 0x90 && velocity > 0)
    {
        stepHeld[pitch] = velocity / 127.f;
        if (std::none_of(stepChord.begin(), stepChord.end(), [&](const auto &n) { return n.first == pitch; }))
            stepChord.push_back({pitch, velocity / 127.f});
        return;
    }
    if (kind != 0x80 && kind != 0x90)
        return;
    stepHeld.erase(pitch);
    if (!stepHeld.empty() || stepChord.empty())
        return;
    const auto *channel = controller.project.channel(selectedChannel);
    if (!channel || channel->audioClip)
    {
        status = "Step record: select an instrument or sampler Channel";
        stepChord.clear();
        return;
    }
    CommandArgs a;
    a.pattern = selectedPattern;
    a.channel = channel->id;
    a.enabled = true; // Blend: keep the Pattern's other notes.
    for (const auto &[notePitch, noteVelocity] : stepChord)
        a.recordedNotes.push_back({0, channel->id, stepCursor, stepLength(), notePitch, noteVelocity, false});
    stepChord.clear();
    const auto revision = controller.revision;
    command("notes.import", a);
    if (controller.revision != revision)
    {
        stepCursor += stepLength();
        status = "Step record: next step at beat " + juce::String(stepCursor + 1, 2);
    }
    refresh();
}
void Studio::clearScoreLog()
{
    audio.midiInput.history.clear();
    audio.typingInput.history.clear();
    scoreLog.clear();
}
void Studio::dumpScoreLog(double seconds)
{
    if (midiTaking)
    {
        status = "Finish the current take before dumping score history";
        repaint();
        return;
    }
    drainScoreLog();
    auto notes = scoreLog.dump(juce::Time::getMillisecondCounterHiRes() / 1000., seconds,
                               controller.project.tempo, controller.project);
    if (notes.empty())
    {
        status = "No notes in the selected score-log interval";
        repaint();
        return;
    }
    CommandArgs args;
    args.pattern = selectedPattern;
    args.recordedNotes = std::move(notes);
    args.enabled = false;
    auto result = controller.execute("notes.record", args);
    status = result.wasOk()
                 ? "Dumped " + juce::String(int(args.recordedNotes.size())) + " score-log notes (one Undo)" +
                       (scoreLog.truncated ? " | history capacity truncated" : "")
                 : result.getErrorMessage();
    if (result.wasOk())
    {
        showEditor(1);
        static_cast<Editor *>(windows[1]->getContentComponent())->revealChannel(true);
    }
    repaint();
}
// Component fixtures supply synthetic callbacks explicitly. Pumping CONTROL
// alone cannot select a prepared native graph before structural predecessor
// history is acquired. Never manufacture that selection receipt.
static juce::Result selectGuiRoutingPredecessorForTest(Studio &studio, const char *phase)
{
    auto *device = dynamic_cast<SyntheticDevice *>(
        studio.audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice());
    if (!device)
        return juce::Result::fail("GUI routing predecessor requires its original synthetic device");
    if (auto admitted = studio.routingSourceAdmission(); admitted.failed())
        return admitted;
    const auto prepared = studio.audio.ensureChannelRoutingContext(
        studio.controller.project, studio.patternMode, studio.selectedPattern);
    if (prepared.status != AudioEngine::ChannelRoutingStatus::ready)
        return juce::Result::fail(prepared.message);
    juce::AudioBuffer<float> output(2, device->getCurrentBufferSizeSamples());
    std::uint64_t selected = 0, posted = 0;
    for (int n = 0; n <= 64; ++n)
    {
        selected = posted = 0;
        // Re-resolve actual native owners after each CONTROL dispatch. The
        // overlay-only routing readback has no serial for an ordinary graph.
        const auto projection = studio.audio.projectionDiagnostics();
        for (auto *edit : studio.audio.engine.getActiveEdits().getEdits())
            if (reinterpret_cast<std::uintptr_t>(edit) == projection.edit)
                if (auto *context = edit->getTransport().getCurrentPlaybackContext();
                    context && reinterpret_cast<std::uintptr_t>(context) == projection.playbackContext)
                {
                    const auto selection = context->getGraphSelection();
                    selected = selection.selectedSerial;
                    posted = selection.postedSerial;
                }
        if (posted != 0 && selected == posted)
            return juce::Result::ok();
        if (n == 64)
            break; // Observe selection after the final admitted callback too.
        device->process(output);
        dispatchMessages(1); // Complete synthetic callback first, then CONTROL.
    }
    return juce::Result::fail("GUI routing predecessor did not reach actual selection [" +
                              juce::String(phase) + "]: selected=" + juce::String(juce::int64(selected)) +
                              " posted=" + juce::String(juce::int64(posted)));
}
juce::Result Studio::verifyPatternWorkflow(const juce::File &folder, const juce::File &fixture)
{
    folder.createDirectory();
    const juce::ScopedValueSetter<bool> unsnappedStart(quantizeRecordingStart, false);
    const juce::ScopedValueSetter<bool> unsnappedEnd(quantizeRecordingEnd, false);
    customization = Customization::familiarDefaults();
    typingKeys.enabled = false;
    auto &manager = audio.engine.getDeviceManager();
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    if (!device)
    {
        manager.deviceManager.addAudioDeviceType(std::make_unique<SyntheticDeviceType>());
        juce::XmlElement setup("DEVICESETUP");
        setup.setAttribute("deviceType", "VibeRa synthetic");
        setup.setAttribute("audioOutputDeviceName", "In-memory output");
        setup.setAttribute("audioDeviceRate", 48000.);
        setup.setAttribute("audioDeviceBufferSize", 256);
        audio.engine.getPropertyStorage().setXmlProperty(te::SettingID::audio_device_setup, setup);
        manager.initialise(0, 2);
        dispatchMessages(20);
        device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    }
    if (!device)
        return juce::Result::fail("Pattern workflow requires virtual output");
    const auto previous = controller.project;
    const auto previousPattern = selectedPattern, previousChannel = selectedChannel;
    const auto previousMode = patternMode;
    auto project = Project::empty();
    project.channels.resize(1);
    project.channels[0].source = "builtin:bass";
    project.channels[0].name = "BASS";
    project.channels[0].pan = 1;
    project.channels[0].gain = 1;
    project.mixer[0].gain = 1;
    project.patterns[0].length = 4;
    project.patterns[0].notes.push_back({project.allocate(), project.channels[0].id, 0, 4, 48, .8f, false});
    const auto firstChannel = project.channels[0].id;
    const auto secondChannel = project.allocate();
    project.channels.push_back({secondChannel, "Second bass", "builtin:bass", 2, .5f, -1, false});
    project.patterns[0].notes.push_back({project.allocate(), secondChannel, 0, 4, 55, .6f, false});
    if (fixture.existsAsFile())
    {
        Project pluginProject;
        auto loaded = loadProject(fixture, pluginProject);
        if (loaded.failed())
            return loaded;
        auto channel = std::find_if(pluginProject.channels.begin(), pluginProject.channels.end(),
                                    [](const auto &c) { return c.instrument.has_value(); });
        if (channel == pluginProject.channels.end())
            return juce::Result::fail("Pattern test needs actual VST3 instrument");
        project.channels[0].instrument = channel->instrument;
        project.channels[0].instrument->id = project.allocate();
    }
    AutomationSource cloneSource;
    cloneSource.id = project.allocate();
    cloneSource.name = "Unplaced clone playback fixture";
    cloneSource.points = {{project.allocate(), 0, .25f}, {project.allocate(), 4, .75f}};
    cloneSource.links = {{{AutomationParameter::mixerGain, 0, 0}, 0, 2}};
    project.automation.sources.push_back(cloneSource);
    project.patterns.push_back({project.allocate(), "Unused", 4, {}});
    const auto target = project.patterns[0].id, spare = project.patterns[1].id;
    project.placements = {{project.allocate(), target, 0, 0, 4, 0}, {project.allocate(), target, 0, 4, 4, 0}};
    audio.setMetronome(false);
    juce::AudioBuffer<float> block(2, 256);
    juce::String report;
    auto fail = [](const char *text) { return juce::Result::fail(text); };
    for (bool pattern : {true, false})
    {
        audio.stop();
        controller.replace(project);
        selectedPattern = target;
        selectedChannel = project.channels[0].id;
        patternMode = pattern;
        refresh();
        const auto until = juce::Time::getMillisecondCounterHiRes() + 8000;
        juce::Result prepared = juce::Result::ok();
        do
        {
            prepared = audio.rebuild(controller.project, pattern, target);
            if (prepared.wasOk())
                break;
            dispatchMessages(5);
        } while (juce::Time::getMillisecondCounterHiRes() < until);
        if (prepared.failed())
            return prepared;
        needsRebuild = false;
        engineRevision = controller.revision;
        audio.setMidiTarget(selectedChannel);
        audio.setLooping(true); // This acceptance explicitly covers looping Song playback too.
        audio.seekBeat(0);
        audio.playStop();
        for (int i = 0; i < 24; ++i)
        {
            device->process(block);
            dispatchMessages(6);
        }
        const auto before = audio.projectionDiagnostics();
        const int starts = device->starts, stops = device->stops;
        const auto position = audio.positionBeat();
        double maximumMs = 0;
        int callbacks = 0, silent = 0;
        auto process = [&]
        {
            const auto start = juce::Time::getMillisecondCounterHiRes();
            device->process(block);
            maximumMs = std::max(maximumMs, juce::Time::getMillisecondCounterHiRes() - start);
            ++callbacks;
            if (block.getMagnitude(1, 0, block.getNumSamples()) < 1.e-6f)
                ++silent;
            dispatchMessages(6);
        };
        {
            const auto opening = controller.project.state();
            auto begin = juce::JSON::parse(R"({"action":"begin","leaseMs":120000})");
            begin.getDynamicObject()->setProperty("pattern", juce::int64(target));
            begin.getDynamicObject()->setProperty("channels", juce::Array<juce::var>{juce::int64(secondChannel), juce::int64(firstChannel)});
            if (stepComposeAction(begin, false).failed()) return fail("Live Advanced Fill session admission");
            auto request = juce::JSON::parse(R"({"action":"update"})");
            request.getDynamicObject()->setProperty("operation", stepComposeSession->id);
            StepComposeRecipe recipe;
            recipe.channel = secondChannel;
            recipe.sequenceSteps = 3;
            recipe.every = 2;
            recipe.loop = true;
            recipe.convertMelodic = true;
            request.getDynamicObject()->setProperty("recipes", juce::Array<juce::var>{stepComposeRecipeState(recipe)});
            if (stepComposeAction(request, false).failed()) return fail("Live Advanced Fill draft validation");
            request.getDynamicObject()->removeProperty("recipes");
            request.getDynamicObject()->setProperty("action", "preview");
            request.getDynamicObject()->setProperty("enabled", true);
            if (stepComposeAction(request, false).failed()) return fail("Live Advanced Fill preview publication");
            for (int i = 0; i < 144; ++i) process();
            const auto scorePreview = controller.project.state();
            request.getDynamicObject()->removeProperty("enabled");
            request.getDynamicObject()->setProperty("action", "select");
            request.getDynamicObject()->setProperty("channel", juce::int64(firstChannel));
            if (stepComposeAction(request, false).failed()) return fail("Live Advanced Fill audition target");
            request.getDynamicObject()->removeProperty("channel");
            request.getDynamicObject()->setProperty("action", "solo");
            request.getDynamicObject()->setProperty("enabled", true);
            if (stepComposeAction(request, false).failed() || !controller.project.solo ||
                controller.project.solo->channel != firstChannel)
                return fail("Live Advanced Fill Solo publication");
            for (int i = 0; i < 24; ++i) process();
            request.getDynamicObject()->setProperty("enabled", false);
            if (stepComposeAction(request, false).failed() ||
                !scorePreview.isEquivalentTo(controller.project.state()))
                return fail("Live Advanced Fill Solo off exact mute restoration");
            for (int i = 0; i < 24; ++i) process();
            request.getDynamicObject()->setProperty("action", "preview");
            request.getDynamicObject()->setProperty("enabled", false);
            if (stepComposeAction(request, false).failed() || !opening.isEquivalentTo(controller.project.state()))
                return fail("Live Advanced Fill preview-off exact restoration");
            for (int i = 0; i < 24; ++i) process();
            request.getDynamicObject()->removeProperty("enabled");
            request.getDynamicObject()->setProperty("action", "cancel");
            if (stepComposeAction(request, false).failed() || !opening.isEquivalentTo(controller.project.state()))
                return fail("Live Advanced Fill session Cancel");
            report += "Advanced Fill source-cycle session preview/off/Cancel retains actual Pattern/Song playback.\n"
                      "Advanced Fill temporary Solo/off preserves continuous right-route PCM and exact opening mutes.\n";
        }
        record.onClick();
        if (!midiTaking || takePattern != target)
            return fail("Metadata test did not arm active MIDI capture");
        const auto heldStartBeat = audio.positionBeat();
        const auto onWrite = audio.midiInput.capture.write.load();
        audio.midiInput.route(juce::MidiMessage::noteOn(1, 72, juce::uint8(90)), true, true);
        if (audio.midiInput.capture.write.load() != onWrite + 1)
            return fail("Held loop fixture did not capture actual note-on");
        const auto heldOn = audio.midiInput.capture.queue[onWrite % audio.midiInput.capture.queue.size()];
        if (pattern)
        {
            const auto before = controller.project.state();
            const auto modeRevision = controller.revision;
            CommandArgs unchanged;
            unchanged.pattern = target;
            unchanged.text = "auto";
            command("pattern.extent", unchanged);
            if (status != "Ready" || !midiTaking || controller.revision != modeRevision ||
                !before.isEquivalentTo(controller.project.state()))
                return fail("No-op Pattern extent finished or changed the active take");
            unchanged.text = "manual";
            unchanged.length = .3;
            auto invalidRequest =
                juce::JSON::parse(R"({"command":"pattern.extent","args":{"text":"manual","length":0.3}})");
            invalidRequest["args"].getDynamicObject()->setProperty("pattern", juce::int64(target));
            // The internal handler throws; McpServer owns the external error envelope.
            const auto rejectionCode = [&](const juce::var &request)
            {
                try
                {
                    mcpCall("vibera_command", request);
                    return juce::String{};
                }
                catch (const McpFailure &error)
                {
                    return error.code;
                }
            };
            if (rejectionCode(invalidRequest) != "patternExtent.invalidLength" || !midiTaking ||
                controller.revision != modeRevision || !before.isEquivalentTo(controller.project.state()))
                return fail("Invalid Pattern extent finished or changed the active take");
            CommandArgs ordinary;
            ordinary.pattern = target;
            ordinary.channel = selectedChannel;
            ordinary.beat = .5;
            ordinary.length = .25;
            ordinary.pitch = 76;
            command("notes.add", ordinary);
            if (status != "Ready" || !midiTaking || controller.undoEditChecked().failed() || !midiTaking ||
                !before.isEquivalentTo(controller.project.state()))
                return fail(
                    "Ordinary in-range application note edit/history interrupted the active Pattern take");
            const auto revision = controller.revision;
            auto growthRequest =
                juce::JSON::parse(R"({"command":"notes.add","args":{"beat":8,"length":1,"pitch":76}})");
            growthRequest["args"].getDynamicObject()->setProperty("pattern", juce::int64(target));
            growthRequest["args"].getDynamicObject()->setProperty("channel", juce::int64(selectedChannel));
            if (rejectionCode(growthRequest) != "recording.patternExtentBusy" ||
                !midiTaking || controller.revision != revision ||
                !before.isEquivalentTo(controller.project.state()))
                return fail("Auto window growth did not preserve/refuse the active Pattern take atomically");
            report += "Auto window-changing note edit refused atomically during actual Pattern capture; held "
                      "input retained.\n";
        }
        for (int i = 0; i < 4; ++i)
            process();
        // Advanced Fill remains an ordinary prepared score edit, including its
        // direct canonical/history path during actual held Pattern/Song input.
        // Keep the long source note at cell zero unchanged and add short cells.
        for (int every : {3, 2, 4})
        {
            const auto original = controller.project.state();
            StepComposeRecipe recipe;
            recipe.channel = selectedChannel;
            recipe.sequenceSteps = 16;
            recipe.every = every;
            recipe.loop = false;
            recipe.convertMelodic = true;
            CommandArgs compose;
            compose.pattern = target;
            compose.composition = {recipe};
            command("steps.compose", compose);
            if (status != "Ready" || !midiTaking)
                return fail("Advanced Fill lost held take or refused ordinary live score edit");
            const auto composed = controller.project.state();
            for (int i = 0; i < 24; ++i) process();
            if (every == 3)
            {
                CommandArgs pitchEdit;
                pitchEdit.pattern = target;
                pitchEdit.channel = selectedChannel;
                pitchEdit.pitch = 72; // Same pitch as held MIDI: owners must remain distinct.
                command("steps.pitch", pitchEdit);
                if (status != "Ready" || !midiTaking)
                    return fail("Step Pitch edit refused or interrupted held input");
                const auto pitched = controller.project.state();
                for (int i = 0; i < 24; ++i) process();
                if (controller.undoEditChecked().failed() || !composed.isEquivalentTo(controller.project.state()))
                    return fail("Live Step Pitch exact Undo");
                for (int i = 0; i < 12; ++i) process();
                if (controller.redoEditChecked().failed() || !pitched.isEquivalentTo(controller.project.state()))
                    return fail("Live Step Pitch exact Redo");
                for (int i = 0; i < 24; ++i) process();
                if (controller.undoEditChecked().failed() || !midiTaking ||
                    !composed.isEquivalentTo(controller.project.state()))
                    return fail("Step Pitch history lost held owner or source");
                report += "Step Pitch same-pitch held-owner edit/Undo/Redo retains Pattern/Song playback and exact step score.\n";
            }
            if (controller.undoEditChecked().failed() || !original.isEquivalentTo(controller.project.state()))
                return fail("Live Advanced Fill exact Undo");
            for (int i = 0; i < 12; ++i) process();
            if (controller.redoEditChecked().failed() || !composed.isEquivalentTo(controller.project.state()))
                return fail("Live Advanced Fill exact Redo");
            for (int i = 0; i < 24; ++i) process();
            if (controller.undoEditChecked().failed() || !midiTaking ||
                !original.isEquivalentTo(controller.project.state()))
                return fail("Live Advanced Fill did not restore capture/source");
        }
        // Actual canonical loop publication with retained virtual device/graph and
        // optional installed instrument. Long notes cross the shorter part cycles.
        for (double length : {.5, .75, 1.})
        {
            const auto original = controller.project.state();
            CommandArgs loop;
            loop.pattern = target;
            loop.channel = selectedChannel;
            loop.text = "fixed";
            loop.length = length;
            command("pattern.channelLoop", loop);
            if (status != "Ready" || !midiTaking ||
                !channelLoop(*controller.project.pattern(target), selectedChannel))
                return fail("Channel loop setter lost held capture or refused live publication");
            const auto looping = controller.project.state();
            for (int i = 0; i < 48; ++i)
                process();
            const auto beforeBurn = controller.project.state();
            const auto burnRevision = controller.revision;
            command("pattern.burnChannelLoop", loop);
            if (!status.startsWith("channelLoop.transportBusy:") || !midiTaking ||
                burnRevision != controller.revision || !beforeBurn.isEquivalentTo(controller.project.state()))
                return fail("Live Burn refusal changed score/history or interrupted capture");
            if (controller.undoEditChecked().failed() || !original.isEquivalentTo(controller.project.state()))
                return fail("Live Channel loop canonical Undo mismatch");
            for (int i = 0; i < 12; ++i)
                process();
            if (controller.redoEditChecked().failed() || !looping.isEquivalentTo(controller.project.state()))
                return fail("Live Channel loop canonical Redo mismatch");
            for (int i = 0; i < 48; ++i)
                process();
            if (controller.undoEditChecked().failed() ||
                !original.isEquivalentTo(controller.project.state()) || !midiTaking)
                return fail("Live Channel loop left altered source or capture after history");
            for (int i = 0; i < 12; ++i)
                process();
        }
        for (const auto *policy : {"clear", "preserve"})
        {
            const auto original = controller.project.state();
            const auto sourceCount = controller.project.automation.sources.size();
            CommandArgs cloneArgs;
            cloneArgs.target = cloneSource.id;
            cloneArgs.text = policy;
            command("automation.source.clone", cloneArgs);
            if (controller.project.automation.sources.size() != sourceCount + 1)
                return fail("Automation source clone refused during native/built-in held playback");
            const auto cloned = controller.project.state();
            for (int i = 0; i < 12; ++i)
                process();
            if (controller.undoEditChecked().failed() || !original.isEquivalentTo(controller.project.state()))
                return fail("Held native/built-in source clone canonical Undo mismatch");
            for (int i = 0; i < 12; ++i)
                process();
            if (controller.redoEditChecked().failed() || !cloned.isEquivalentTo(controller.project.state()))
                return fail("Held native/built-in source clone canonical Redo mismatch");
            for (int i = 0; i < 12; ++i)
                process();
            if (controller.undoEditChecked().failed() ||
                !original.isEquivalentTo(controller.project.state()) || !midiTaking || takePattern != target)
                return fail("Held native/built-in source clone lost original/take after history");
            for (int i = 0; i < 12; ++i)
                process();
        }
        grabKeyboardFocus();
        focus = Scope::patterns;
        if (!keyPressed(juce::KeyPress(juce::KeyPress::F2Key)))
            return fail("F2 Pattern rename not dispatched");
        auto *dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        if (!dialog || !dialog->getTextEditor("name") || !dialog->getTextEditor("colour"))
            return fail("F2 did not open real Pattern properties dialog");
        dialog->getTextEditor("name")->setText("Verse bass");
        dialog->getTextEditor("colour")->setText("#5c93d1");
        auto image = dialog->createComponentSnapshot(dialog->getLocalBounds());
        auto out =
            folder.getChildFile(pattern ? "pattern-properties-pattern.png" : "pattern-properties-song.png")
                .createOutputStream();
        if (!out)
            return fail("Pattern dialog capture");
        out->setPosition(0);
        out->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(image, *out))
            return fail("Pattern dialog PNG");
        out.reset();
        dialog->exitModalState(1);
        dispatchMessages(10);
        if (controller.project.pattern(target)->name != "Verse bass" ||
            controller.project.pattern(target)->colour != 0xff5c93d1u)
            return fail("Pattern dialog did not commit name and colour");
        if (!midiTaking || takePattern != target)
            return fail("Pattern properties interrupted MIDI capture");
        CommandArgs metadataOrder;
        metadataOrder.pattern = target;
        metadataOrder.lane = 1;
        command("pattern.move", metadataOrder);
        metadataOrder.lane = 0;
        command("pattern.move", metadataOrder);
        if (!midiTaking || takePattern != target)
            return fail("Pattern reorder interrupted MIDI capture");
        CommandArgs grouping;
        grouping.ids = {target, spare};
        grouping.text = "Live group";
        command("pattern.group", grouping);
        if (!midiTaking || takePattern != target)
            return fail("Pattern grouping interrupted MIDI capture");
        const auto beforeChannelDisplay = controller.project.state();
        const auto recipient = selectedChannel;
        const auto heldChannels = selectedChannels;
        focus = Scope::rack;
        if (!keyPressed(juce::KeyPress(juce::KeyPress::F2Key)))
            return fail("Rack F2 Channel properties not dispatched");
        dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        if (!dialog || !dialog->getTextEditor("name") || !dialog->getComboBoxComponent("icon"))
            return fail("Rack F2 did not open Channel properties");
        auto *colours = dynamic_cast<ChannelColourPicker *>(dialog->getCustomComponent(0));
        auto *palette =
            colours ? dynamic_cast<juce::TextButton *>(colours->findChildWithID("channelColourPreset.2"))
                    : nullptr;
        auto *theme = colours
                          ? dynamic_cast<juce::TextButton *>(colours->findChildWithID("channelColourTheme"))
                          : nullptr;
        auto *spectrum =
            colours ? dynamic_cast<juce::ColourSelector *>(colours->findChildWithID("channelColourSpectrum"))
                    : nullptr;
        if (!palette || !theme || !spectrum)
            return fail("Channel colour palette/spectrum/reset missing");
        channelPropertiesDialog({recipient});
        if (channelPropertiesWindow.getComponent() != dialog)
            return fail("Colour dialog duplicated its existing window");
        palette->onClick();
        const auto paletteText = dialog->getTextEditorContents("colour");
        if (!paletteText.startsWithChar('#') || paletteText.length() != 7 ||
            juce::uint32(paletteText.substring(1).getHexValue32()) !=
                (channelAppearancePalette()[2].getARGB() & 0x00ffffffu))
            return fail("Palette did not update the exact HTML colour draft");
        theme->onClick();
        if (!dialog->getTextEditorContents("colour").equalsIgnoreCase("theme") ||
            !beforeChannelDisplay.isEquivalentTo(controller.project.state()))
            return fail("Colour draft/reset prematurely changed the document");
        spectrum->setCurrentColour(juce::Colour(0xffe08030u), juce::sendNotification);
        dialog->getTextEditor("colour")->setText("#112233", false);
        dialog->getTextEditor("colour")->onTextChange();
        dispatchMessages(5);
        if (dialog->getTextEditorContents("colour") != "#112233" ||
            spectrum->getCurrentColour().getARGB() != 0xff112233u)
            return fail("Queued spectrum event overwrote a newer HTML draft");
        dialog->getTextEditor("colour")->setText("invalid", false);
        dialog->getTextEditor("colour")->onTextChange();
        if (dialog->getButton("Apply")->isEnabled())
            return fail("Invalid HTML colour allowed commit");
        palette->onClick();
        dialog->getTextEditor("name")->setText("Live keys");
        spectrum->setCurrentColour(juce::Colour(0xff55b7d9u), juce::sendNotification);
        dialog->getComboBoxComponent("icon")->setSelectedItemIndex(4);
        dispatchMessages(5);
        if (!beforeChannelDisplay.isEquivalentTo(controller.project.state()))
            return fail("Colour spectrum preview committed before Apply");
        {
            const auto picture = dialog->createComponentSnapshot(dialog->getLocalBounds());
            auto stream =
                folder
                    .getChildFile(pattern ? "channel-properties-pattern.png" : "channel-properties-song.png")
                    .createOutputStream();
            if (!stream)
                return fail("Channel properties capture");
            stream->setPosition(0);
            stream->truncate();
            if (!juce::PNGImageFormat{}.writeImageToStream(picture, *stream))
                return fail("Channel properties PNG");
        }
        dialog->exitModalState(1);
        dispatchMessages(10);
        if (controller.project.channel(recipient)->name != "Live keys" ||
            controller.project.channel(recipient)->colour != 0xff55b7d9u ||
            controller.project.channel(recipient)->icon != "keys")
            return juce::Result::fail("Channel properties did not commit all fields atomically: " +
                                      controller.project.channel(recipient)->name + ", colour=" +
                                      juce::String::toHexString(controller.project.channel(recipient)->colour) +
                                      ", icon=" + controller.project.channel(recipient)->icon);
        for (int i = 0; i < 12; ++i)
            process();
        const auto displayState = controller.project.state();
        CommandArgs display;
        display.ids = {controller.project.channels.front().id, controller.project.channels.back().id};
        display.appearance = ChannelAppearancePatch{};
        display.appearance->colour = 0xffeeaa66u;
        display.appearance->icon = "drum";
        command("rack.properties", display);
        for (int i = 0; i < 12; ++i)
            process();
        display.appearance.reset();
        display.channelSort = ChannelSortRequest{ChannelSortRequest::By::name, true};
        command("rack.sort", display);
        for (int i = 0; i < 12; ++i)
            process();
        if (!midiTaking || takePattern != target || selectedChannel != recipient ||
            selectedChannels != heldChannels)
            return fail("Channel display edits changed live take/recipient/selection");
        controller.undoEdit();
        controller.undoEdit();
        if (!displayState.isEquivalentTo(controller.project.state()))
            return fail("Bulk properties/sort exact Undo");
        controller.undoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (!beforeChannelDisplay.isEquivalentTo(controller.project.state()) || !midiTaking)
            return fail("Channel properties one Undo retains take");
        controller.redoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (!displayState.isEquivalentTo(controller.project.state()) || !midiTaking)
            return fail("Channel properties exact Redo retains take");
        controller.undoEdit(); // Keep the original Pattern workflow history exact.
        for (int i = 0; i < 12; ++i)
            process(); // Observe the preceding native history selection before another command.
        CommandArgs scoped;
        scoped.ids = {recipient};
        if (controller.project.channels.front().id != recipient)
            scoped.ids.push_back(controller.project.channels.front().id);
        scoped.text = "Live solo scope";
        command("rack.group.assign", scoped);
        if (status != "Ready" || controller.project.channel(recipient)->group == 0)
            return juce::Result::fail("Held group assignment: " + status);
        scoped.channel = recipient;
        scoped.ids.clear();
        scoped.soloScope = "group";
        command("channel.solo", scoped);
        const auto soloCommitStatus = status;
        for (int i = 0; i < 12; ++i)
            process();
        if (soloCommitStatus != "Ready" || !controller.project.solo || !controller.project.solo->groupScoped)
            return juce::Result::fail(
                "Group solo did not commit during held input: " + soloCommitStatus +
                "; hasSolo=" + juce::String(int(bool(controller.project.solo))) + "; grouped=" +
                juce::String(int(controller.project.solo && controller.project.solo->groupScoped)));
        CommandArgs lock;
        lock.channel = recipient;
        command("channel.muteLock", lock);
        const auto lockCommitStatus = status;
        for (int i = 0; i < 12; ++i)
            process();
        if (lockCommitStatus != "Ready" || controller.project.solo ||
            !controller.project.channel(recipient)->muteLocked || !midiTaking || takePattern != target ||
            selectedChannel != recipient || selectedChannels != heldChannels)
            return fail("Mute lock changed held input or retained stale solo ownership");
        for (int i = 0; i < 3; ++i)
            controller.undoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (!beforeChannelDisplay.isEquivalentTo(controller.project.state()))
            return fail("Scoped solo/group/lock Undo did not restore the held-take document");
        for (int i = 0; i < 12; ++i)
            process();
        const auto offWrite = audio.midiInput.capture.write.load();
        audio.midiInput.route(juce::MidiMessage::noteOff(1, 72), true, true);
        if (audio.midiInput.capture.write.load() != offWrite + 1)
            return fail("Held loop fixture did not capture actual note-off");
        const auto heldOff = audio.midiInput.capture.queue[offWrite % audio.midiInput.capture.queue.size()];
        for (int i = 0; i < 12; ++i)
            process();
        drainMidiTake();
        const double expectedHeldBeats = (heldOff.seconds - heldOn.seconds) * project.tempo / 60;
        double capturedHeldBeats = 0;
        bool segmentsValid = !midiPreview.empty();
        for (size_t i = 0; i < midiPreview.size(); ++i)
        {
            const auto &note = midiPreview[i];
            capturedHeldBeats += note.length;
            segmentsValid = segmentsValid && note.pitch == 72 && note.channel == heldOn.channel &&
                            note.length > 0 && note.beat >= 0;
            if (pattern)
                segmentsValid =
                    segmentsValid && note.beat + note.length <= 4 + 1.e-8 && (i == 0 || note.beat == 0) &&
                    (i + 1 == midiPreview.size() || std::abs(note.beat + note.length - 4) < 1.e-8);
        }
        const auto expectedSegments =
            pattern && !midiPreview.empty()
                ? size_t(std::ceil((midiPreview.front().beat + expectedHeldBeats) / 4))
                : size_t(1);
        if (!midiTaking || !segmentsValid || midiPreview.size() != expectedSegments ||
            std::abs(capturedHeldBeats - expectedHeldBeats) > 1.e-8 ||
            std::abs(midiPreview.front().beat - heldStartBeat) > 4 * 256. / 48000)
            return juce::Result::fail(
                "Held MIDI loop capture mismatch: segments=" + juce::String(int(midiPreview.size())) +
                " expected=" + juce::String(int(expectedSegments)) +
                " capturedBeats=" + juce::String(capturedHeldBeats, 9) +
                " actualEventBeats=" + juce::String(expectedHeldBeats, 9));
        report += juce::String(pattern ? "Pattern" : "Song") +
                  " held capture: " + juce::String(int(midiPreview.size())) + " exact segments, " +
                  juce::String(capturedHeldBeats, 9) +
                  " beats; matches actual note-on/off event duration, no lost/duplicate capture.\n";
        folder.getChildFile("channel-loop-capture.txt").replaceWithText(report);
        finishMidiTake(true);
        controller.undoEdit(); // Remove the live grouping transaction before the existing metadata checks.
        controller.undoEdit();
        controller.undoEdit(); // Remove the two metadata-order transactions.
        const auto named = controller.project.state();
        controller.undoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (!project.state().isEquivalentTo(controller.project.state()))
            return fail("Pattern properties one Undo");
        controller.redoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (!named.isEquivalentTo(controller.project.state()))
            return fail("Pattern properties exact Redo");
        CommandArgs extent;
        extent.pattern = target;
        extent.text = "manual";
        extent.length = 4;
        command("pattern.extent", extent);
        CommandArgs extra;
        extra.pattern = target;
        extra.channel = project.channels[0].id;
        extra.beat = 4;
        extra.length = 4;
        extra.pitch = 60;
        extra.value = .8;
        const auto newNote = controller.project.nextId;
        command("notes.add", extra);
        if (controller.project.pattern(target)->length != 4 || controller.project.pattern(target)->autoLength)
            return fail("Manual window regrew from a dormant note during playback");
        for (int i = 0; i < 8; ++i)
            process();
        extent.text = "auto";
        command("pattern.extent", extent);
        if (!controller.project.pattern(target)->autoLength ||
            controller.project.pattern(target)->length != 8)
            return fail("Auto did not include the newly revealed source bar during playback");
        for (int i = 0; i < 8; ++i)
            process();
        extra.ids = {newNote};
        command("notes.delete", extra);
        if (controller.project.pattern(target)->length != 4)
            return fail("Deleting the last source bar did not shrink Auto during playback");
        for (int i = 0; i < 8; ++i)
            process();
        controller.undoEdit();
        if (controller.project.pattern(target)->length != 8)
            return fail("Live Auto shrink Undo did not restore the source bar");
        for (int i = 0; i < 8; ++i)
            process();
        controller.redoEdit();
        if (controller.project.pattern(target)->length != 4)
            return fail("Live Auto shrink Redo did not restore the shorter window");
        for (int i = 0; i < 8; ++i)
            process();
        for (int i = 0; i < 4; ++i)
            controller.undoEdit(); // Delete, Auto, note addition, Manual.
        if (!named.isEquivalentTo(controller.project.state()))
            return fail("Live Pattern extent history did not restore exact source/allocator/mode");
        report += "Manual dormant add, Auto growth/shrink and exact note/extent Undo/Redo passed during "
                  "playback.\n";
        grabKeyboardFocus();
        focus = Scope::rack;
        if (!keyPressed(juce::KeyPress(juce::KeyPress::downKey,
                                       juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier,
                                       0)) ||
            controller.project.patterns.back().id != target || selectedPattern != target)
            return fail("Ctrl+Shift+Down Pattern reorder identity");
        for (int i = 0; i < 12; ++i)
            process();
        controller.undoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (!named.isEquivalentTo(controller.project.state()))
            return fail("Pattern reorder Undo");
        clonePattern.onClick();
        if (controller.project.patterns[1].id != selectedPattern || selectedPattern == target)
            return fail("Toolbar Clone selects adjacent copy");
        selectedPattern = target;
        refresh();
        for (int i = 0; i < 12; ++i)
            process();
        controller.undoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        CommandArgs args;
        args.pattern = spare;
        command("pattern.delete", args);
        for (int i = 0; i < 12; ++i)
            process();
        controller.undoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        const auto combined = controller.project.state();
        // Approach naturally: seeking into a held note would also test MIDI
        // chase, obscuring whether the edit itself interrupted a sounding voice.
        const auto loopEnd =
            pattern ? controller.project.pattern(target)->length : controller.project.endBeat();
        for (int i = 0; i < 4096 && audio.positionBeat() < loopEnd - .1; ++i)
            process();
        if (audio.positionBeat() < loopEnd - .1)
            return fail("Pattern workflow did not reach its natural loop boundary");
        const auto splitPosition = audio.positionBeat();
        if (!performAction("pattern.splitChannels") || selectedPattern != target ||
            controller.project.patterns.size() != project.patterns.size() + 1 ||
            controller.project.pattern(target)->notes.size() != 1 ||
            std::abs(audio.positionBeat() - splitPosition) > .001)
            return fail("Split by channel action did not select/retain the first part");
        for (int i = 0; i < 12; ++i)
            process();
        const auto split = controller.project.state();
        controller.undoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (!combined.isEquivalentTo(controller.project.state()))
            return fail("Split by channel is not one exact Undo during playback");
        controller.redoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (!split.isEquivalentTo(controller.project.state()))
            return fail("Split by channel exact Redo during playback");
        controller.undoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (audio.positionBeat() >= splitPosition)
            return fail("Split/Undo/Redo playback fixture did not cross its loop boundary");
        // Transpose a separate Channel while the right-panned (optionally hosted)
        // sustained voice remains unaffected. Exercise the actual modal callback.
        for (int i = 0; i < 4096 && audio.positionBeat() < loopEnd - .1; ++i)
            process();
        if (audio.positionBeat() < loopEnd - .1)
            return fail("Transpose workflow did not approach its natural loop boundary");
        selectedChannel = secondChannel;
        if (!performAction("pattern.transpose"))
            return fail("Transpose dialog action unavailable");
        dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        if (!dialog || !dialog->getTextEditor("semitones") || !dialog->getComboBoxComponent("channels") ||
            dialog->getComboBoxComponent("channels")->getSelectedItemIndex() != 0)
            return fail("Transpose dialog must default to the selected Channel");
        dialog->getTextEditor("semitones")->setText("+12");
        image = dialog->createComponentSnapshot(dialog->getLocalBounds());
        out = folder.getChildFile(pattern ? "transpose-pattern.png" : "transpose-song.png")
                  .createOutputStream();
        if (!out)
            return fail("Transpose dialog image stream");
        out->setPosition(0);
        out->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(image, *out))
            return fail("Transpose dialog image encoding");
        out.reset();
        const auto transposePosition = audio.positionBeat();
        dialog->exitModalState(1);
        dispatchMessages(10);
        if (controller.project.pattern(target)->notes[0].pitch != 48 ||
            controller.project.pattern(target)->notes[1].pitch != 67 ||
            std::abs(audio.positionBeat() - transposePosition) > .001)
            return fail("Transpose dialog failed selected-Channel scope or moved transport");
        for (int i = 0; i < 12; ++i)
            process();
        const auto transposed = controller.project.state();
        controller.undoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (!combined.isEquivalentTo(controller.project.state()))
            return fail("Transpose during playback is not one exact Undo");
        controller.redoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (!transposed.isEquivalentTo(controller.project.state()))
            return fail("Transpose during playback exact Redo");
        controller.undoEdit();
        for (int i = 0; i < 12; ++i)
            process();
        if (audio.positionBeat() >= transposePosition)
            return fail("Transpose/Undo/Redo fixture did not cross its loop boundary");
        selectedChannel = project.channels[0].id;
        refresh();
        const auto after = audio.projectionDiagnostics();
        if (!audio.playing() || audio.positionBeat() < position || before.edit != after.edit ||
            before.playbackContext != after.playbackContext || device->starts != starts ||
            device->stops != stops || !named.isEquivalentTo(controller.project.state()) ||
            maximumMs >= 256000. / 48000 || silent != 0)
            return juce::Result::fail(
                juce::String(pattern ? "Pattern" : "Song") + ": Pattern workflow failed: playing=" +
                juce::String(int(audio.playing())) + " initialBeat=" + juce::String(position, 6) +
                " finalBeat=" + juce::String(audio.positionBeat(), 6) +
                " splitBeat=" + juce::String(splitPosition, 6) +
                " retainedEdit=" + juce::String(int(before.edit == after.edit)) +
                " retainedContext=" + juce::String(int(before.playbackContext == after.playbackContext)) +
                " starts=" + juce::String(starts) + "->" + juce::String(device->starts) +
                " stops=" + juce::String(stops) + "->" + juce::String(device->stops) +
                " exactDocument=" + juce::String(int(named.isEquivalentTo(controller.project.state()))) +
                " maxCallbackMs=" + juce::String(maximumMs, 6) + " silent=" + juce::String(silent));
        for (size_t i = 0; i < before.channels.size(); ++i)
            if (before.channels[i].graphInitialisations != after.channels[i].graphInitialisations)
                return fail("Pattern management rebuilt instrument graph");
        report += (pattern ? "Pattern" : "Song") + juce::String(": ") + juce::String(callbacks) +
                  " callbacks, max " + juce::String(maximumMs, 4) +
                  " ms; zero silent right blocks/overruns; retained device/Edit/context/instrument graph; "
                  "exact undo/redo including Channel loops/capture-safe Burn refusal, Split by channel and "
                  "Transpose across natural loops.\n";
        audio.stop();
    }
    selectedPattern = target;
    const auto transposeBaseline = controller.project.state();
    auto applyTransposeDialog = [&](const juce::String &text, int channels, int choice) -> bool
    {
        transposePattern();
        auto *form = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        if (!form || !form->getTextEditor("semitones") || !form->getComboBoxComponent("channels"))
            return false;
        form->getTextEditor("semitones")->setText(text);
        form->getComboBoxComponent("channels")->setSelectedItemIndex(channels, juce::dontSendNotification);
        form->exitModalState(choice);
        dispatchMessages(10);
        return true;
    };
    if (!applyTransposeDialog("-1", 1, 1) || controller.project.pattern(target)->notes[0].pitch != 47 ||
        controller.project.pattern(target)->notes[1].pitch != 54)
        return fail("Transpose all-Channels option or negative semitones failed");
    controller.undoEdit();
    for (auto text : {juce::String("+"), juce::String("--1"), juce::String("+127")})
        if (!applyTransposeDialog(text, 0, 1) ||
            !transposeBaseline.isEquivalentTo(controller.project.state()))
            return fail("Invalid Transpose dialog partially edited the score");
    if (!applyTransposeDialog("12", 0, 0) || !transposeBaseline.isEquivalentTo(controller.project.state()))
        return fail("Transpose dialog cancellation changed the score");
    refresh();
    grabKeyboardFocus();
    keyPressed(juce::KeyPress(juce::KeyPress::F4Key));
    if (selectedPattern != spare)
        return fail("F4 next empty selection");
    auto *dialog = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!dialog)
        return fail("F4 naming dialog");
    dialog->exitModalState(0);
    dispatchMessages(10);
    const auto untouched = controller.project.state();
    findEmptyPattern(false, false);
    if (controller.project.patterns.size() != 3 ||
        !controller.project.pattern(selectedPattern)->notes.empty())
        return fail("Next empty creates usable Pattern when needed");
    controller.undoEdit();
    if (!untouched.isEquivalentTo(controller.project.state()))
        return fail("Created empty Pattern Undo");
    auto routing = Project::empty();
    routing.channels.resize(1);
    const auto routingTarget = routing.channels[0].id;
    for (int i = 1; i <= 8; ++i)
        routing.channels.push_back(
            {routing.allocate(), "Occupied " + juce::String(i), "builtin:kick", i, .7f, 0, false});
    controller.replace(routing);
    selectedChannel = routingTarget;
    selectedPattern = routing.patterns[0].id;
    refresh();
    grabKeyboardFocus();
    focus = Scope::rack;
    if (auto ready = selectGuiRoutingPredecessorForTest(*this, "Pattern free Insert 9"); ready.failed())
        return ready;
    keyPressed(juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 0));
    if (controller.project.channel(routingTarget)->mixer != 9)
        return fail("Ctrl+L missed free Insert 9");
    if (auto adopted = finishRoutingForTest("Pattern Ctrl+L free Insert 9"); adopted.failed())
        return adopted;
    if (auto undone = controller.undoEditChecked(); undone.failed())
        return undone;
    if (auto adopted = finishRoutingForTest("Pattern free Insert 9 Undo"); adopted.failed())
        return adopted;
    if (!routing.state().isEquivalentTo(controller.project.state()))
        return fail("Insert 9 assignment Undo");
    for (int i = 9; i <= 17; ++i)
        routing.channels.push_back(
            {routing.allocate(), "Occupied " + juce::String(i), "builtin:kick", i, .7f, 0, false});
    if (auto restored = restoreRoutingBeforeStructural(); restored.failed())
        return restored;
    controller.replace(routing);
    refresh();
    focus = Scope::rack;
    keyPressed(juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 0));
    if (!routing.state().isEquivalentTo(controller.project.state()) ||
        !status.startsWith("No free Mixer insert"))
        return fail("Full Mixer silently reassigned to occupied insert");
    report += "Ctrl+L: free Insert 9 beyond legacy range, exact Undo and fully occupied Mixer preservation "
              "passed.\n";
    {
        auto loopProject = Project::demo();
        loopProject.patterns.front().length = 8;
        controller.replace(loopProject);
        selectedPattern = loopProject.patterns.front().id;
        selectedChannel = loopProject.channels.front().id;
        CommandArgs loop;
        loop.pattern = selectedPattern;
        loop.channel = selectedChannel;
        loop.text = "fixed";
        loop.length = .75;
        command("pattern.channelLoop", loop);
        if (status != "Ready")
            return fail("Loop component fixture command refused");
        refresh();
        resized();
        showEditor(0);
        auto *rack = static_cast<Editor *>(windows[0]->getContentComponent());
        auto stream = folder.getChildFile("channel-loop-rack.png").createOutputStream();
        if (!stream)
            return fail("Channel loop Rack image stream");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(rack->createComponentSnapshot(rack->getLocalBounds()),
                                                       *stream))
            return fail("Channel loop Rack PNG");
        stream.reset();
        const auto baseline = controller.project.state();
        channelLoopLengthDialog(selectedChannel, selectedPattern);
        auto *form = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        if (!form || !form->getTextEditor("length"))
            return fail("Actual Channel loop fixed-length dialog");
        stream = folder.getChildFile("channel-loop-length.png").createOutputStream();
        if (!stream)
            return fail("Channel loop dialog image stream");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(form->createComponentSnapshot(form->getLocalBounds()),
                                                       *stream))
            return fail("Channel loop dialog PNG");
        stream.reset();
        form->getTextEditor("length")->setText("1.25");
        form->exitModalState(1);
        dispatchMessages(10);
        if (channelLoopLength(controller.project, *controller.project.pattern(selectedPattern),
                              selectedChannel) != 1.25 ||
            controller.undoEditChecked().failed() || !baseline.isEquivalentTo(controller.project.state()))
            return fail("Actual Channel loop dialog Apply/one Undo mismatch");
        channelLoopLengthDialog(selectedChannel, selectedPattern);
        form = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
        if (!form)
            return fail("Channel loop Cancel dialog");
        form->exitModalState(0);
        dispatchMessages(10);
        if (!baseline.isEquivalentTo(controller.project.state()))
            return fail("Channel loop Cancel changed score");
        if (!performAction("pattern.burnChannelLoop") ||
            channelLoop(*controller.project.pattern(selectedPattern), selectedChannel) ||
            controller.undoEditChecked().failed() || !baseline.isEquivalentTo(controller.project.state()))
            return fail("Contextual Burn action/one Undo mismatch");
        report += "Actual fixed-loop dialog Apply/Cancel/history and repeated Rack component PNG passed.\n";
    }
    {
        auto meterProject = controller.project; // Retain the already prepared original built-in Rack fixture.
        meterProject.patterns.front().autoLength = false;
        const auto meterTarget = meterProject.patterns.front().id;
        controller.replace(meterProject);
        selectedPattern = meterTarget;
        selectedChannel = meterProject.channels.front().id;
        patternMode = true;
        refresh();
        const auto baseline = controller.project.state();
        if (!keyPressed(
                juce::KeyPress('P', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0)))
            return fail("Ctrl+Shift+P did not dispatch Pattern meter");
        auto *form = patternMeterWindow.getComponent();
        if (!form)
            return fail("Pattern meter dialog did not open");
        patternMeterDialog(meterTarget);
        if (form != patternMeterWindow.getComponent())
            return fail("Pattern meter dialog duplicated instead of reusing the window");
        form->getComboBoxComponent("mode")->setSelectedItemIndex(1);
        form->getComboBoxComponent("numerator")->setSelectedItemIndex(6);
        form->getComboBoxComponent("denominator")->setSelectedItemIndex(2);
        auto stream = folder.getChildFile("pattern-meter-dialog.png").createOutputStream();
        if (!stream)
            return fail("Pattern meter image stream");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(form->createComponentSnapshot(form->getLocalBounds()),
                                                       *stream))
            return fail("Pattern meter PNG");
        stream.reset();
        form->exitModalState(1);
        dispatchMessages(10);
        const auto *part = controller.project.pattern(meterTarget);
        if (part->numerator != 7 || part->denominator != 8 || part->length != 8 || part->autoLength)
            return fail("Pattern meter Apply changed Manual extent or failed custom7/8");
        auto *rack = static_cast<Editor *>(windows[0]->getContentComponent());
        stream = folder.getChildFile("pattern-extent-rack.png").createOutputStream();
        if (!stream)
            return fail("Pattern extent Rack image stream");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(rack->createComponentSnapshot(rack->getLocalBounds()),
                                                       *stream))
            return fail("Pattern extent Rack PNG");
        stream.reset();
        controller.undoEdit();
        if (!baseline.isEquivalentTo(controller.project.state()))
            return fail("Pattern meter dialog Apply is not one exact Undo");
        patternMeterDialog(meterTarget);
        patternMeterWindow->exitModalState(0);
        dispatchMessages(10);
        if (!baseline.isEquivalentTo(controller.project.state()))
            return fail("Pattern meter Cancel changed the document");
        report +=
            "Actual Pattern meter dialog reuse,7/8 Apply, Undo, Cancel and Rack component images passed.\n";
    }
    controller.replace(previous);
    selectedPattern = previousPattern;
    selectedChannel = previousChannel;
    patternMode = previousMode;
    refresh();
    resized();
    juce::Component::SafePointer<juce::AlertWindow> ownedDialog;
    {
        Studio isolated(false);
        isolated.renamePattern();
        ownedDialog = isolated.patternPropertiesWindow;
        if (!ownedDialog)
            return fail("Pattern dialog lifetime fixture");
    }
    dispatchMessages(10);
    if (ownedDialog || juce::Component::getCurrentlyModalComponent())
        return fail("Pattern dialog outlived its owner");
    {
        Studio isolated(false);
        isolated.patternMeterDialog(isolated.selectedPattern);
        ownedDialog = isolated.patternMeterWindow;
        if (!ownedDialog)
            return fail("Pattern meter dialog lifetime fixture");
    }
    dispatchMessages(10);
    if (ownedDialog || juce::Component::getCurrentlyModalComponent())
        return fail("Pattern meter dialog outlived its owner");
    report += "Held MIDI note-on/off across name/colour/reorder and owner-dialog teardown passed; no "
              "hardware input claim.\n";
    folder.getChildFile("pattern-workflow-verification.txt")
        .replaceWithText(
            "PASS\n" + report +
            "Actual F2/modal callback, rename/recolour, contextual reorder, toolbar clone, F4/empty creation "
            "and undo; virtual audio, not OS gestures/listening/FL runtime parity.\n");
    return juce::Result::ok();
}
void Studio::patternMenu()
{
    focus = Scope::patterns;
    auto &list = controller.project.patterns;
    auto it = std::find_if(list.begin(), list.end(), [&](const auto &p) { return p.id == selectedPattern; });
    if (it == list.end())
        return;
    juce::PopupMenu menu;
    menu.addItem(1, "Find next empty (F4)");
    menu.addItem(2, "Find first empty (Shift+F4)");
    menu.addSeparator();
    menu.addItem(3, "Rename / recolour... (F2)");
    menu.addItem(4, "Insert one before selected");
    menu.addItem(5, "Clone");
    menu.addItem(6, "Delete...");
    menu.addItem(9, "Split by channel", !it->notes.empty());
    menu.addItem(10, "Transpose...", !it->notes.empty());
    menu.addItem(13, "Find next empty (no naming)");
    menu.addItem(11, "Select in playlist");
    menu.addItem(12, "Random colour");
    menu.addSeparator();
    menu.addItem(7, "Move up (Ctrl+Shift+Up)", it != list.begin());
    menu.addItem(8, "Move down (Ctrl+Shift+Down)", std::next(it) != list.end());
    const auto id = selectedPattern;
    const auto identity = controller.projectIdentity;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(patternOptions),
        [safe = juce::Component::SafePointer<Studio>(this), id, identity](int choice)
        {
            if (!safe || !choice || safe->controller.projectIdentity != identity ||
                !safe->controller.project.pattern(id))
                return;
            safe->selectedPattern = id;
            if (choice == 1 || choice == 2)
                safe->findEmptyPattern(choice == 2);
            else if (choice == 3)
                safe->renamePattern();
            else if (choice == 5)
                safe->clonePattern.onClick();
            else if (choice == 6)
                safe->deletePattern();
            else if (choice == 9)
            {
                CommandArgs args;
                args.pattern = id;
                safe->command("pattern.splitChannels", args);
            }
            else if (choice == 10)
                safe->transposePattern();
            else if (choice == 11 || choice == 12 || choice == 13)
                safe->performAction(choice == 11   ? "pattern.selectInPlaylist"
                                    : choice == 12 ? "pattern.randomColour"
                                                   : "pattern.findEmptyNoNaming");
            else
            {
                CommandArgs args;
                args.pattern = id;
                const auto &patterns = safe->controller.project.patterns;
                auto index =
                    std::distance(patterns.begin(), std::find_if(patterns.begin(), patterns.end(),
                                                                 [id](const auto &p) { return p.id == id; }));
                if (choice == 4)
                {
                    safe->command("pattern.insert", args);
                    if (safe->status == "Ready")
                        safe->selectedPattern = safe->controller.project.patterns[size_t(index)].id;
                }
                else if (choice == 7 || choice == 8)
                {
                    args.lane = int(index) + (choice == 7 ? -1 : 1);
                    safe->command("pattern.move", args);
                }
                safe->refresh();
            }
        });
}
void Studio::transposePattern()
{
    if (patternPropertiesWindow && patternPropertiesWindow->isCurrentlyModal())
    {
        bringWindowToFront(*patternPropertiesWindow);
        return;
    }
    const auto *pattern = controller.project.pattern(selectedPattern);
    const auto *channel = controller.project.channel(selectedChannel);
    if (!pattern || !channel)
        return;
    const auto id = pattern->id, channelId = channel->id;
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    auto *dialog = new juce::AlertWindow("Transpose Pattern", "Transpose notes in the current Pattern.",
                                         juce::MessageBoxIconType::NoIcon);
    patternPropertiesWindow = dialog;
    dialog->setLookAndFeel(&look);
    dialog->addTextEditor("semitones", "0", "Semitones (-127 to +127)");
    dialog->getTextEditor("semitones")->setInputRestrictions(4, "+-0123456789");
    dialog->addComboBox("channels", {"Selected Channel: " + channel->name, "All Channels in this Pattern"},
                        "Channels");
    dialog->addButton("Transpose", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<Studio>(this),
             form = juce::Component::SafePointer<juce::AlertWindow>(dialog), id, channelId, identity,
             revision](int choice)
            {
                if (!safe || !form || choice != 1)
                    return;
                if (safe->controller.projectIdentity != identity || safe->controller.revision != revision ||
                    !safe->controller.project.pattern(id) || !safe->controller.project.channel(channelId))
                {
                    safe->status = "Project changed while Transpose was open; please retry";
                    safe->repaint();
                    return;
                }
                const auto text = form->getTextEditorContents("semitones").trim();
                const auto digits =
                    text.startsWithChar('+') || text.startsWithChar('-') ? text.substring(1) : text;
                if (digits.isEmpty() || !digits.containsOnly("0123456789"))
                {
                    safe->status = "Enter a whole number of semitones from -127 to +127";
                    safe->repaint();
                    return;
                }
                CommandArgs args;
                args.pattern = id;
                args.channel =
                    form->getComboBoxComponent("channels")->getSelectedItemIndex() == 1 ? 0 : channelId;
                args.value = digits.getIntValue() * (text.startsWithChar('-') ? -1 : 1);
                safe->command("pattern.transpose", args);
            }),
        true);
}
void Studio::renamePattern()
{
    if (patternPropertiesWindow && patternPropertiesWindow->isCurrentlyModal())
    {
        bringWindowToFront(*patternPropertiesWindow);
        return;
    }
    const auto *pattern = controller.project.pattern(selectedPattern);
    if (!pattern)
        return;
    const auto id = pattern->id;
    const auto identity = controller.projectIdentity, original = pattern->name;
    const auto colour = pattern->colour;
    auto *dialog = new juce::AlertWindow("Rename Pattern", "Name the selected multi-instrument pattern.",
                                         juce::MessageBoxIconType::NoIcon);
    patternPropertiesWindow = dialog;
    dialog->setLookAndFeel(&look);
    dialog->addTextEditor("name", original, "Name");
    dialog->addTextEditor("colour",
                          colour == 0
                              ? juce::String("auto")
                              : "#" + juce::String::toHexString(colour & 0x00ffffff).paddedLeft('0', 6),
                          "Colour: auto or #RRGGBB");
    dialog->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
    dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    dialog->enterModalState(
        true,
        juce::ModalCallbackFunction::create(
            [safe = juce::Component::SafePointer<Studio>(this),
             form = juce::Component::SafePointer<juce::AlertWindow>(dialog), id, identity, original,
             colour](int choice)
            {
                if (!safe || !form || choice != 1)
                    return;
                auto *pattern = safe->controller.project.pattern(id);
                if (safe->controller.projectIdentity != identity || !pattern ||
                    (pattern->name != original || pattern->colour != colour))
                {
                    safe->status = "Pattern changed while rename was open; please retry";
                    safe->repaint();
                    return;
                }
                CommandArgs args;
                args.pattern = id;
                args.text = form->getTextEditorContents("name");
                auto colourText = form->getTextEditorContents("colour").trim().toLowerCase();
                if (colourText == "auto")
                    args.value = 0;
                else
                {
                    if (colourText.startsWithChar('#'))
                        colourText = colourText.substring(1);
                    if (colourText.length() != 6 || !colourText.containsOnly("0123456789abcdef"))
                    {
                        safe->status = "Pattern colour must be auto or #RRGGBB";
                        safe->repaint();
                        return;
                    }
                    args.value = double(juce::uint32(0xff000000u | juce::uint32(colourText.getHexValue32())));
                }
                safe->command("pattern.properties", args);
            }),
        true);
}
void Studio::patternBulkAction(int action, const std::vector<Id> &ids)
{
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    auto safe = juce::Component::SafePointer<Studio>(this);
    auto confirm = [this, safe, identity, revision](const juce::String &title, const juce::String &message,
                                                    std::function<void()> run)
    {
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withTitle(title)
                                         .withMessage(message)
                                         .withButton("Delete")
                                         .withButton("Cancel")
                                         .withAssociatedComponent(this),
                                     [safe, identity, revision, run](int choice)
                                     {
                                         if (!safe || choice != 1)
                                             return;
                                         if (safe->controller.projectIdentity != identity ||
                                             safe->controller.revision != revision)
                                         {
                                             safe->status = "Project changed while the dialog was open; retry";
                                             safe->repaint();
                                             return;
                                         }
                                         run();
                                     });
    };
    CommandArgs args;
    args.ids = ids;
    if (action == 6)
    {
        command("pattern.cloneSelected", args);
        return;
    }
    if (action == 7)
    {
        if (ids.size() == 1)
        {
            selectedPattern = ids.front();
            deletePattern();
            return;
        }
        confirm("Delete Patterns",
                "Delete " + juce::String(int(ids.size())) +
                    " Patterns and all their Playlist placements? Undo restores them.",
                [safe, args] { if (safe) safe->command("pattern.deleteSelected", args); });
        return;
    }
    if (action == 8 && ids.size() == 1)
    {
        const auto *pattern = controller.project.pattern(ids.front());
        if (!pattern)
            return;
        const auto id = pattern->id;
        const auto base = projectFile == juce::File{}
                              ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                              : projectFile.getParentDirectory();
        chooser = std::make_unique<juce::FileChooser>(
            "Export Pattern WAV - 48 kHz, 24-bit",
            base.getChildFile(juce::File::createLegalFileName(pattern->name)).withFileExtension("wav"), "*.wav");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe, id](const juce::FileChooser &fc)
                             {
                                 if (safe && fc.getResult() != juce::File{})
                                     safe->exportTo(fc.getResult().withFileExtension("wav"), id);
                             });
        return;
    }
    if (action == 9)
    {
        command("pattern.deleteEmpty", {});
        return;
    }
    if ((action == 11 || action == 12) && ids.size() == 1)
    {
        if (auto started = renderPatternToClip(ids.front(), action == 12); started.failed())
            status = started.getErrorMessage();
        repaint();
        return;
    }
    if (action == 10)
    {
        const auto count = std::count_if(controller.project.patterns.begin(), controller.project.patterns.end(),
                                         [&](const auto &p)
                                         { return !pattern_management::placed(controller.project, p.id); });
        confirm("Delete unplaced Patterns",
                "Delete " + juce::String(int(count)) +
                    " Patterns that are not used in the Playlist, including their notes? Undo restores them.",
                [safe] { if (safe) safe->command("pattern.deleteUnplaced", {}); });
    }
}
juce::Result Studio::renderPatternToClip(Id patternId, bool replace)
{
    const auto *pattern = controller.project.pattern(patternId);
    if (!pattern)
        return juce::Result::fail("patternRender.missingPattern: Existing Pattern required");
    if (afterPluginState || pendingExport || audio.rendering() || renderClip)
        return juce::Result::fail("Finish the active save/export first");
    const auto folder = (projectFile == juce::File{}
                             ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                   .getChildFile("VibeRa Studio")
                             : projectFile.getParentDirectory())
                            .getChildFile("Rendered");
    if (!folder.createDirectory())
        return juce::Result::fail("patternRender.folder: Cannot create " + folder.getFullPathName());
    const auto file =
        folder.getChildFile(juce::File::createLegalFileName(pattern->name) + ".wav").getNonexistentSibling();
    // Place at the Pattern's first Playlist use (or bar 1) on the first lane below every clip.
    double beat = 0;
    int lane = 0;
    bool found = false;
    for (const auto &clip : controller.project.placements)
    {
        if (clip.pattern == patternId && (!found || clip.beat < beat))
        {
            beat = clip.beat;
            found = true;
        }
        lane = std::max(lane, clip.lane + 1);
    }
    for (const auto &clip : controller.project.audioPlacements)
        lane = std::max(lane, clip.lane + 1);
    renderClip = RenderClipRequest{file, controller.projectIdentity, beat, std::min(lane, 127)};
    if (replace)
    {
        // Render and replace: the audio clip goes wherever the Pattern is placed and those Pattern
        // clips are muted (kept, so the swap can be undone clip by clip).
        for (const auto &clip : controller.project.placements)
            if (clip.pattern == patternId)
            {
                renderClip->muteAfter.push_back(clip.id);
                if (clip.beat != beat)
                    renderClip->alsoAt.push_back(clip.beat);
            }
        if (renderClip->muteAfter.empty() || renderClip->alsoAt.size() > 30)
        {
            const bool none = renderClip->muteAfter.empty();
            renderClip.reset();
            return juce::Result::fail(none ? "patternRender.noClips: The Pattern is not placed in the Playlist"
                                           : "patternRender.tooManyClips: Render and replace handles up to 31 placements");
        }
        std::sort(renderClip->alsoAt.begin(), renderClip->alsoAt.end());
        renderClip->alsoAt.erase(std::unique(renderClip->alsoAt.begin(), renderClip->alsoAt.end()), renderClip->alsoAt.end());
    }
    exportTo(file, patternId);
    return juce::Result::ok();
}
juce::Result Studio::renderClipsToAudio(std::vector<Id> clips)
{
    // Selected Pattern and audio clips render together (windows, repeats, mixer and tempo)
    // into one WAV that is placed at their start on the first free track below the Song;
    // the originals are then muted (Undo restores them).
    std::erase_if(clips, [&](Id id)
                  {
                      const auto &p = controller.project;
                      return std::none_of(p.placements.begin(), p.placements.end(), [&](const auto &c) { return c.id == id; }) &&
                             std::none_of(p.audioPlacements.begin(), p.audioPlacements.end(),
                                          [&](const auto &c) { return c.id == id; });
                  });
    if (clips.empty())
        return juce::Result::fail("clipRender.noClips: Select Pattern or audio clips to render");
    if (afterPluginState || pendingExport || audio.rendering() || renderClip || exportBusy())
        return juce::Result::fail("Finish the active save/export first");
    Project probe;
    double start = 0;
    if (auto r = pattern_management::renderClipsProject(controller.project, clips, probe, start); r.failed())
        return r;
    const auto folder = (projectFile == juce::File{}
                             ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                   .getChildFile("VibeRa Studio")
                             : projectFile.getParentDirectory())
                            .getChildFile("Rendered");
    if (!folder.createDirectory())
        return juce::Result::fail("clipRender.folder: Cannot create " + folder.getFullPathName());
    juce::String name = "Clips";
    for (const auto &c : controller.project.placements)
        if (c.id == clips.front())
            if (const auto *p = controller.project.pattern(c.pattern))
                name = p->name;
    const auto file = folder.getChildFile(juce::File::createLegalFileName(name) + " render.wav").getNonexistentSibling();
    int lane = 0;
    for (const auto &clip : controller.project.placements)
        lane = std::max(lane, clip.lane + 1);
    for (const auto &clip : controller.project.audioPlacements)
        lane = std::max(lane, clip.lane + 1);
    renderClip = RenderClipRequest{file, controller.projectIdentity, start, std::min(lane, 127), clips};
    exportTo(file, 0, false, clips);
    return juce::Result::ok();
}
void Studio::deletePattern()
{
    const auto *pattern = controller.project.pattern(selectedPattern);
    if (!pattern)
        return;
    const auto id = pattern->id;
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    const auto index = pattern - controller.project.patterns.data();
    const auto placements =
        std::count_if(controller.project.placements.begin(), controller.project.placements.end(),
                      [id](const auto &p) { return p.pattern == id; });
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withTitle("Delete Pattern")
            .withMessage("Delete '" + pattern->name + "' and its " + juce::String(int(placements)) +
                         " Playlist placements? Undo restores them.")
            .withButton("Delete")
            .withButton("Cancel")
            .withAssociatedComponent(this),
        [safe = juce::Component::SafePointer<Studio>(this), id, identity, revision, index](int choice)
        {
            if (!safe || choice != 1)
                return;
            if (safe->controller.projectIdentity != identity || safe->controller.revision != revision)
            {
                safe->status = "Project changed while delete was open; please retry";
                safe->repaint();
                return;
            }
            CommandArgs args;
            args.pattern = id;
            safe->command("pattern.delete", args);
            if (safe->status == "Ready")
            {
                safe->selectedPattern =
                    safe->controller.project
                        .patterns[size_t(
                            std::min(index, std::ptrdiff_t(safe->controller.project.patterns.size() - 1)))]
                        .id;
                safe->refresh();
            }
        });
}
void Studio::findEmptyPattern(bool first, bool naming)
{
    auto &list = controller.project.patterns;
    auto current =
        std::find_if(list.begin(), list.end(), [&](const auto &p) { return p.id == selectedPattern; });
    auto begin = first ? list.begin() : current == list.end() ? list.begin() : std::next(current);
    auto found = std::find_if(begin, list.end(), [](const auto &p) { return p.notes.empty(); });
    if (found != list.end())
        selectedPattern = found->id;
    else
    {
        command("pattern.add");
        if (status != "Ready")
            return;
        selectedPattern = controller.project.patterns.back().id;
    }
    selectedNotes.clear();
    needsRebuild = true;
    refresh();
    if (naming)
        renamePattern();
}
void Studio::toolsMenu()
{
    juce::PopupMenu menu, history;
    const auto lastTweak = lastPluginTweak();
    menu.addSubMenu("Last tweaked", lastPluginTweakMenu(lastTweak));
    menu.addSeparator();
    for (int minutes : {2, 3, 5, 10, 20, 30})
        history.addItem(minutes, "Last " + juce::String(minutes) + " minutes");
    history.addSeparator();
    history.addItem(100, "Clear log");
    menu.addSubMenu("Dump score log to selected pattern", history);
    menu.addItem(200, "CoProducer panel", true, coProducerShown);
    menu.addItem(201, "Restore docked workspace layout");
    menu.addItem(202, videoWindow ? "Close video player" : "Video player (follows the transport)...");
    menu.addItem(203, "Performance mode (launch Patterns live)");
    menu.addItem(205, "Drum pads (play and load a kit)");
    menu.addItem(206, "Create a chord progression...");
    menu.addItem(207, "Step record (write played notes at a cursor)", true, stepRecord);
    menu.addItem(209, "MIDI recording replaces notes (off = overdub)", true, midiRecordReplace);
    // IDs 240..251: clear of the Detach (210..214), Arrange windows (220..223) and update (225) items.
    menu.addItem(240, "Master pitch (" + juce::String(controller.project.masterPitch, 0) + " cents)...");
    menu.addItem(241, "Wait for input (recording starts on the first MIDI note)", true, waitForInput);
    menu.addItem(257, "Wait for input: only when recording (off = Play waits too)", waitForInput, waitOnlyWhenRecording);
    menu.addItem(258, "Wait for input: respect recording filter (off = wait even without Notes)", waitForInput, waitRespectFilter);
    juce::PopupMenu tempoMenu;
    tempoMenu.addItem(291, "Half speed", controller.project.tempo / 2 >= 20);
    tempoMenu.addItem(292, "Double speed", controller.project.tempo * 2 <= 400);
    tempoMenu.addItem(293, "Nudge up 1 BPM", controller.project.tempo + 1 <= 400);
    tempoMenu.addItem(294, "Nudge down 1 BPM", controller.project.tempo - 1 >= 20);
    menu.addSubMenu("Tempo (" + juce::String(controller.project.tempo, 2) + " BPM)", tempoMenu);
    menu.addItem(252, "Remember seek time (Stop returns to where Play started)", true, rememberSeekTime);
    menu.addItem(253, "Recording starts playback (Record also presses Play)", true, recordStartsPlayback);
    menu.addItem(254, "Disarm on stop (Stop turns Record off)", true, disarmOnStop);
    menu.addItem(255, "Show time as minutes:seconds", true, timeDisplaySeconds);
    menu.addItem(256, "Recording quantize: leave note duration", true, quantizeKeepDuration);
    menu.addItem(295, "Record to step sequencer (recorded notes become steps)", true, recordToSteps);
    {
        // Monitor volume: what you hear only; exports, meters and the audio logger are unchanged.
        juce::PopupMenu monitor;
        const int current = juce::roundToInt(MonitorVolume::instance().get() * 100);
        for (const int percent : {100, 75, 50, 25, 10})
            monitor.addItem(400 + percent, juce::String(percent) + "%", true, current == percent);
        menu.addSubMenu("Monitor volume (listening only, " + juce::String(current) + "%)", monitor);
    }
    {
        // FL Recording filter: what Record captures. Audio is the audio-input switch; automation
        // recording is armed separately on its control.
        juce::PopupMenu filter;
        filter.addItem(242, "Notes (MIDI take)", true, recordNotes);
        filter.addItem(243, "Audio (audio input)", true, audioInput.enabled);
        filter.addItem(244, "Automation: arm it on a Mixer/plugin control", false, automationRecordingActive());
        menu.addSubMenu("Recording filter", filter);
    }
    {
        juce::PopupMenu logger;
        const bool on = AudioLogger::instance().enabled();
        logger.addItem(245, "Off", true, !on);
        for (const int minutes : {1, 2, 5})
            logger.addItem(245 + minutes, "Keep the last " + juce::String(minutes) + " minute" + (minutes > 1 ? "s" : ""),
                           true, on && std::abs(AudioLogger::instance().capacitySeconds() - minutes * 60.) < 1);
        logger.addSeparator();
        logger.addItem(251, "Save the logged audio...", on);
        menu.addSubMenu("Audio logger (master output)", logger);
    }
    menu.addItem(208, "Step record: insert a rest", stepRecord);
    const auto missingCount = int(missingMedia(controller.project).size());
    menu.addItem(204, "Find missing samples..." +
                          (missingCount ? " (" + juce::String(missingCount) + " missing)" : juce::String()),
                 missingCount > 0);
    juce::PopupMenu templates;
    const auto templateFiles = projectTemplates();
    for (int i = 0; i < std::min(30, templateFiles.size()); ++i)
        templates.addItem(260 + i, templateFiles[i].getFileNameWithoutExtension());
    if (templateFiles.isEmpty())
        templates.addItem(259, "(no templates yet: Save as template...)", false);
    templates.addSeparator();
    templates.addItem(227, "Open templates folder", templatesFolder().isDirectory());
    menu.addSubMenu("New from template", templates);
    menu.addItem(226, "Save as template...");
    // Save keeps five backups per project; autosave covers unexpected closes.
    menu.addItem(224, "Open project backups folder", profileFile().getSiblingFile("backups").isDirectory());
    menu.addItem(228, "Export diagnostics...");
    menu.addItem(229, "Analyze loudness of an audio file...");
    menu.addSeparator();
    menu.addItem(230, "Chord Track: write a chord progression...");
    menu.addItem(231, "Chord Track: selected notes / Channel follow the chords", !controller.project.chords.empty());
    menu.addSeparator();
    menu.addItem(232, "Macro knobs...");
    menu.addItem(225, updatePhase == "available" ? "Update available: " + (availableUpdate ? availableUpdate->name : juce::String()) + "..."
                      : updatePhase == "downloading" ? "Downloading update..."
                      : updatePhase == "ready"       ? "Install downloaded update..."
                                                     : juce::String("Check for updates..."));
    juce::PopupMenu layouts;
    layouts.addItem(220, "Balanced auto tiling", true, autoTileMode == 1);
    layouts.addItem(221, "Focus active editor", true, autoTileMode == 2);
    layouts.addItem(222, "Free window movement", true, autoTileMode == 0);
    const bool hasDetached = std::any_of(detachedWindows.begin(), detachedWindows.end(),
                                         [](const auto &window) { return window && window->isVisible(); });
    layouts.addSeparator();
    layouts.addItem(223, "Tile detached editors on their monitors", hasDetached);
    menu.addSubMenu("Arrange windows", layouts);
    juce::PopupMenu detach;
    const juce::StringArray panels{"Channel Rack", "Piano Roll", "Playlist", "Mixer", "Browser"};
    for (int index = 0; index < 5; ++index)
        detach.addItem(210 + index, panels[index]);
    menu.addSubMenu("Detach to another monitor", detach);
    auto safe = juce::Component::SafePointer<Studio>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(toolsButton),
        [safe, lastTweak](int choice)
        {
            if (!safe)
                return;
            if (choice >= 300 && choice <= 303)
            {
                if (lastTweak)
                {
                    juce::String operation, code;
                    safe->requestLastPluginTweak(
                        *lastTweak,
                        juce::StringArray{"controls", "automation.plugin.create", "automation.plugin.init",
                                          "automation.plugin.init.remove"}[choice - 300],
                        operation, code);
                }
                return;
            }
            if (choice == 240)
                return safe->masterPitchDialog();
            if (choice == 245 || choice == 246 || choice == 247 || choice == 250)
            {
                if (auto r = safe->setAudioLogger(choice != 245, choice - 245); r.failed())
                    safe->status = r.getErrorMessage().fromFirstOccurrenceOf(": ", false, false);
                else
                    safe->status = choice == 245 ? "Audio logger off" : "Audio logger keeps the last " + juce::String(choice - 245) + " min of the master output";
                safe->repaint();
                return;
            }
            if (choice == 251)
            {
                safe->saveAudioLogDialog();
                return;
            }
            if (choice == 242)
            {
                safe->performAction("transport.recordNotes");
                return;
            }
            if (choice == 243)
            {
                safe->audioInputButton.onClick();
                return;
            }
            if (choice >= 410 && choice <= 500) // Monitor volume: 400 + percent.
            {
                MonitorVolume::instance().set(float(choice - 400) / 100.f);
                safe->status = "Monitor volume " + juce::String(choice - 400) + "% (listening only)";
                safe->repaint();
                return;
            }
            if (choice == 295)
            {
                safe->performAction("transport.recordToSteps");
                return;
            }
            if (choice >= 291 && choice <= 294)
            {
                safe->performAction(juce::StringArray{"project.tempoHalf", "project.tempoDouble", "project.tempoNudgeUp",
                                                      "project.tempoNudgeDown"}[choice - 291]);
                return;
            }
            if (choice >= 252 && choice <= 258)
            {
                safe->performAction(juce::StringArray{"transport.rememberSeek", "transport.recordStartsPlayback",
                                                      "transport.disarmOnStop", "view.timeDisplay",
                                                      "transport.quantizeKeepDuration", "transport.waitOnlyWhenRecording",
                                                      "transport.waitRespectFilter"}[choice - 252]);
                return;
            }
            if (choice == 241)
            {
                safe->performAction("transport.waitForInput");
                return;
            }
            if (choice == 226)
                return safe->promptSaveTemplate();
            if (choice == 227)
            {
                safe->performAction("project.openTemplatesFolder");
                return;
            }
            if (choice >= 260 && choice < 290)
            {
                const auto files = safe->projectTemplates();
                if (choice - 260 >= files.size())
                    return;
                const auto name = files[choice - 260].getFileNameWithoutExtension();
                safe->requestProjectTransition(
                    [safe, name](bool discard)
                    {
                        if (!safe)
                            return;
                        if (auto opened = safe->newFromTemplate(name, discard); opened.failed())
                        {
                            safe->status = opened.getErrorMessage();
                            safe->repaint();
                        }
                    });
                return;
            }
            if (choice == 232)
            {
                safe->openMacroKnobs();
                return;
            }
            if (choice == 230)
            {
                auto *box = new juce::AlertWindow("Chord Track", "One chord per bar, e.g.  C | Am | F | G   or  Dm7 G7 Cmaj7",
                                                  juce::MessageBoxIconType::NoIcon);
                juce::StringArray current;
                for (const auto &chord : safe->controller.project.chords)
                    current.add(chord.symbol);
                box->addTextEditor("chords", current.joinIntoString(" | "), "Chords");
                box->addTextEditor("bar", "1", "Start bar");
                box->addButton("Write", 1, juce::KeyPress(juce::KeyPress::returnKey));
                box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                box->enterModalState(true, juce::ModalCallbackFunction::create(
                                               [safe, box](int button)
                                               {
                                                   if (!safe || button != 1)
                                                       return;
                                                   CommandArgs a;
                                                   a.text = box->getTextEditorContents("chords");
                                                   const int bar = std::max(1, box->getTextEditorContents("bar").getIntValue());
                                                   double beat = 0;
                                                   for (int b = 1; b < bar; ++b)
                                                       beat += std::max(.25, safe->controller.project.barPosition(beat).barLength);
                                                   a.beat = beat;
                                                   safe->command("chord.fill", a);
                                               }),
                                     true);
                return;
            }
            if (choice == 231)
            {
                CommandArgs a;
                a.pattern = safe->selectedPattern;
                a.channel = safe->selectedChannel;
                for (const auto id : safe->selectedNotes)
                    a.ids.push_back(id);
                safe->command("notes.followChords", a);
                return;
            }
            if (choice == 229)
            {
                auto chooser = std::make_shared<juce::FileChooser>("Analyze loudness", juce::File(),
                                                                   "*.wav;*.flac;*.aif;*.aiff;*.ogg");
                chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                     [safe, chooser](const juce::FileChooser &picked)
                                     {
                                         const auto file = picked.getResult();
                                         if (!safe || file == juce::File{})
                                             return;
                                         loudness::Result measured;
                                         const auto analysed = analyzeLoudnessFile(file, measured);
                                         safe->status = analysed.wasOk()
                                                            ? file.getFileName() + ": " + describeLoudness(measured)
                                                            : analysed.getErrorMessage();
                                         safe->repaint();
                                     });
                return;
            }
            if (choice == 228)
            {
                auto chooser = std::make_shared<juce::FileChooser>(
                    "Export diagnostics",
                    juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                        .getChildFile("VibeRa diagnostics.txt"),
                    "*.txt");
                chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                         juce::FileBrowserComponent::warnAboutOverwriting,
                                     [safe, chooser](const juce::FileChooser &picked)
                                     {
                                         const auto file = picked.getResult();
                                         if (!safe || file == juce::File{})
                                             return;
                                         const auto result = safe->writeDiagnostics(file, true);
                                         safe->status = result.wasOk() ? "Diagnostics saved to " + file.getFileName()
                                                                       : result.getErrorMessage();
                                         safe->repaint();
                                     });
                return;
            }
            if (choice == 224)
            {
                safe->performAction("project.openBackupsFolder");
                return;
            }
            if (choice == 203)
            {
                safe->performAction("view.performance");
                return;
            }
            if (choice == 205)
                return safe->showDrumPads();
            if (choice == 206)
                return safe->showChordProgression();
            if (choice == 207)
            {
                safe->performAction("transport.stepRecord");
                return;
            }
            if (choice == 209)
            {
                safe->performAction("transport.recordReplace");
                return;
            }
            if (choice == 208)
            {
                safe->performAction("transport.stepRest");
                return;
            }
            if (choice == 225)
                return safe->updateSettingsMenu();
            if (choice == 204)
            {
                safe->chooser = std::make_unique<juce::FileChooser>("Folder to search for the missing samples",
                                                                    safe->projectFile.getParentDirectory());
                safe->chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                               juce::FileBrowserComponent::canSelectDirectories,
                                           [safe](const juce::FileChooser &chooser)
                                           {
                                               if (safe && chooser.getResult().isDirectory())
                                                   safe->relinkMissingMedia(chooser.getResult());
                                           });
                return;
            }
            if (choice == 202)
            {
                if (safe->videoWindow)
                    return safe->closeVideo();
                safe->chooser = std::make_unique<juce::FileChooser>("Open a video", juce::File(),
                                                                    "*.mp4;*.mov;*.wmv;*.avi;*.mkv");
                safe->chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                               juce::FileBrowserComponent::canSelectFiles,
                                           [safe](const juce::FileChooser &fc)
                                           {
                                               if (safe && fc.getResult() != juce::File{})
                                                   if (auto r = safe->openVideo(fc.getResult()); r.failed())
                                                       safe->status = r.getErrorMessage();
                                           });
                return;
            }
            if (choice >= 220 && choice <= 223)
            {
                safe->command(juce::StringArray{"workspace.tileBalanced", "workspace.tileFocused",
                                                "workspace.free", "workspace.tileDetached"}[choice - 220]);
                return;
            }
            if (choice == 200)
            {
                if (safe->coProducerShown)
                    safe->hideCoProducer();
                else
                    safe->showCoProducer();
            }
            else if (choice >= 210 && choice < 215)
                safe->detachWorkspaceWindow(choice - 210);
            else if (choice == 201)
                safe->restoreDockedLayout();
            else if (choice == 100)
            {
                safe->clearScoreLog();
                safe->status = "Score log cleared";
                safe->repaint();
            }
            else if (choice > 0)
                safe->dumpScoreLog(choice * 60.);
        });
}
void Studio::previewNote(int pitch)
{
    if (previewPitch == pitch)
        return;
    if (previewPitch >= 0)
        performanceInput.set(1, previewPitch, false, 0);
    previewPitch = -1;
    if (pitch >= 0 && pitch <= 127 && (!audio.playing() || (previewDuringPlayback && !midiTaking && !recordArmed)))
    {
        previewPitch = pitch;
        performanceInput.set(1, pitch, true, 100);
    }
}
void Studio::releaseTypingNotes()
{
    typingKeys.releaseAll();
    performanceInput.release(1);
    previewPitch = -1;
}
bool Studio::keyStateChanged(bool)
{
    typingKeys.poll([](int key) { return juce::KeyPress::isKeyCurrentlyDown(key); });
    return false;
}
void Studio::saveTypingPreferences()
{
    if (!persistentTyping)
        return;
    auto data = std::make_unique<juce::DynamicObject>();
    data->setProperty("version", 1);
    data->setProperty("enabled", typingKeys.enabled);
    data->setProperty("root", typingKeys.root);
    data->setProperty("velocity", typingKeys.velocity);
    auto file = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                    .getChildFile("VibeRa Studio/typing-keyboard.json");
    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(juce::var(data.release())));
}
void Studio::typingMenu()
{
    juce::PopupMenu menu;
    for (int octave = 2; octave <= 7; ++octave)
        menu.addItem(100 + octave, "Root C" + juce::String(octave), true, typingKeys.root == octave * 12);
    menu.addSeparator();
    for (int value : {64, 100, 127})
        menu.addItem(200 + value, "Velocity " + juce::String(value), true, typingKeys.velocity == value);
    auto safe = juce::Component::SafePointer<Studio>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(typingButton),
                       [safe](int result)
                       {
                           if (!safe || !result)
                               return;
                           safe->releaseTypingNotes();
                           if (result >= 102 && result <= 107)
                               safe->typingKeys.root = (result - 100) * 12;
                           else if (result >= 264)
                               safe->typingKeys.velocity = result - 200;
                           safe->saveTypingPreferences();
                       });
}
bool Studio::keyPressed(const juce::KeyPress &key)
{
    const bool textEditing =
        dynamic_cast<juce::TextEditor *>(juce::Component::getCurrentlyFocusedComponent()) != nullptr;
    const auto action = textEditing ? juce::String{} : customization.resolve(key, focus);
    if (controllerTestMode)
    {
        // Bounded read-only native-test diagnostics. No typed text, normal-user
        // logging, persistence, audio-thread work or alternate command path.
        auto events = getProperties()["testKeyDispatches"];
        if (!events.isArray())
            events = juce::var(juce::Array<juce::var>{});
        auto *entry = new juce::DynamicObject;
        entry->setProperty("keyCode", key.getKeyCode());
        entry->setProperty("modifiers", key.getModifiers().getRawFlags());
        entry->setProperty("scope", int(focus));
        entry->setProperty("textEditing", textEditing);
        entry->setProperty("resolvedCommand", action);
        if (events.size() >= 16)
            events.getArray()->remove(0);
        events.getArray()->add(juce::var(entry));
        getProperties().set("testKeyDispatches", events);
    }
    if (textEditing)
    {
        releaseTypingNotes();
        return false;
    }
    return performAction(action, &key);
}
bool Studio::performAction(const juce::String &action, const juce::KeyPress *typingKey)
{
    if (action == "transport.play" && automationTouch &&
        automationTouch->phase == MixerAutomationTake::Phase::armed && !audio.playing())
    {
        play.onClick();
        return audio.playing() || devicePreparationBusy();
    }
    if (action == "transport.loop" && automationRecordingActive())
    {
        status = "automationRecording.unsupportedMode: Finish/cancel the Song take before changing Loop";
        return false;
    }
    if (action == "automation.record.arm" || action == "automation.record.finish" ||
        action == "automation.record.cancel" || (action == "selection.cancel" && automationRecordingActive()))
    {
        auto result = action == "automation.record.cancel" || action == "selection.cancel"
                          ? finishAutomationRecording(true)
                      : action == "automation.record.arm" ? startAutomationRecording(selectedInsert, 0)
                                                          : finishAutomationRecording(false);
        status = result.wasOk()
                     ? (automationRecordingActive()
                            ? "Song automation recording armed/active; Stop to keep, Escape to cancel"
                            : "Automation take resolved")
                     : result.getErrorMessage();
        repaint();
        return result.wasOk();
    }
    if (mixerChainApplying() && action != "mixer.chain.cancel" && action != "transport.stop" &&
        !action.startsWith("window.") && !action.startsWith("view."))
    {
        status = "Effect chain Apply pending; wait or cancel its exact operation";
        return false;
    }
    if (performMixerChainAction(action))
        return true;
    if (performMixerControlAction(action))
        return true;
    if (performMixerRoutingAction(action))
        return true;
    if (action == "selection.cancel" &&
        static_cast<Editor *>(windows[3]->getContentComponent())->mixerSendGestureActive())
    {
        auto *editor = static_cast<Editor *>(windows[3]->getContentComponent());
        editor->cancelPointerGesture();
        if (editor->mixerSendGestureActive())
            return false; // Preserve actual failure/pending owner.
        status = "Mixer edit cancelled; other selections retained";
        refresh();
        return true;
    }
    if (action == "selection.cancel" && noteStretchPending())
    {
        static_cast<Editor *>(windows[1]->getContentComponent())->cancelPointerGesture();
        status = "Stretch preview cancelled; selection retained";
        refresh();
        return true;
    }

    if (stepComposePending() && action == "selection.cancel")
    {
        auto request = juce::var(new juce::DynamicObject);
        request.getDynamicObject()->setProperty("action", "cancel");
        request.getDynamicObject()->setProperty("operation", stepComposeSession->id);
        const auto result = stepComposeAction(request, false);
        status = result.wasOk() ? "Advanced Fill cancelled" : result.getErrorMessage();
        return result.wasOk();
    }
    if (stepComposePending() && action != "transport.play" && action != "transport.stop" &&
        action != "steps.composeDialog")
    {
        status = "stepCompose.busy: Accept or Cancel Advanced Fill first";
        return false;
    }
    if (action == "steps.composeDialog")
    {
        openStepComposer(selectedPattern, selectedChannels.empty() ? std::vector<Id>{selectedChannel} : selectedChannels);
        return stepComposePending();
    }
    if (noteStretchPending() && !committingNoteStretch && action.isNotEmpty())
        finishNoteStretch(false, "Local action superseded stretch preview");
    if (noteSlicePending() && !committingNoteSlice && action.isNotEmpty())
        finishNoteSlice(false, "Local application action superseded the Slice preview");
    if (action.isNotEmpty() && action != "selection.cancel" && action != "transport.stop" &&
        action != "recording.cancel")
        if (finishRackEditsChecked().failed())
            return false; // Refused, so semantic callers cannot report completion.
    if (action == "project.swing" || action == "channel.swingMix")
    {
        swingDialog(action, {selectedChannel});
        return true;
    }
    if (action == "rack.graph")
    {
        setRackGraphVisible(!rackGraphVisible);
        return true;
    }
    if (action == "pattern.group")
    {
        showEditor(2);
        auto *editor = static_cast<Editor *>(windows[2]->getContentComponent());
        editor->setPickerShown(true);
        editor->clipPicker()->groupSelected();
        return true;
    }
    if (action == "controller.bankPrevious" || action == "controller.bankNext")
    {
        midiController.moveBank(action == "controller.bankNext" ? 1 : -1,
                                controllerMixerCount(controller.project));
        status = controllerMixerBankLabel(controller.project, midiController.mixerBank);
        repaint();
        return true;
    }
    if (action.startsWith("workspace."))
    {
        command(action);
        return true;
    }
    if (action == "pattern.insert")
    {
        auto &list = controller.project.patterns;
        const auto it =
            std::find_if(list.begin(), list.end(), [&](const auto &p) { return p.id == selectedPattern; });
        if (it != list.end())
        {
            const auto index = std::distance(list.begin(), it);
            CommandArgs args;
            args.pattern = selectedPattern;
            command("pattern.insert", args);
            if (status == "Ready")
            {
                selectedPattern = controller.project.patterns[size_t(index)].id;
                refresh();
            }
        }
        return true;
    }
    if (action == "pattern.transpose")
    {
        transposePattern();
        return true;
    }
    if (action == "pattern.splitChannels")
    {
        CommandArgs args;
        args.pattern = selectedPattern;
        command(action, args);
        return true;
    }
    if (action == "pattern.delete")
    {
        deletePattern();
        return true;
    }
    if (action == "channel.properties" || action == "rack.properties")
    {
        channelPropertiesDialog(action == "channel.properties" ? std::vector<Id>{selectedChannel}
                                                               : selectedChannels);
        return true;
    }
    if (action == "rack.sort")
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Name A-Z");
        menu.addItem(2, "Assigned colour (theme first)");
        menu.addItem(3, "Mixer assignment");
        menu.addItem(4, "Instrument / sample / Audio Clip");
        menu.addItem(5, "Name Z-A");
        const auto ids = selectedChannels;
        const auto identity = controller.projectIdentity;
        auto safe = juce::Component::SafePointer<Studio>(this);
        menu.showMenuAsync(
            {},
            [safe, ids, identity](int action)
            {
                if (!safe || action < 1 || action > 5 || safe->controller.projectIdentity != identity)
                    return;
                CommandArgs args;
                args.ids = ids;
                args.channelSort = ChannelSortRequest{action == 2   ? ChannelSortRequest::By::colour
                                                      : action == 3 ? ChannelSortRequest::By::mixer
                                                      : action == 4 ? ChannelSortRequest::By::type
                                                                    : ChannelSortRequest::By::name,
                                                      action == 5};
                safe->command("rack.sort", args);
            });
        return true;
    }
    if (action == "pattern.renameDialog" || action == "pattern.rename" || action == "pattern.properties")
    {
        renamePattern();
        return true;
    }
    if (action == "pattern.findEmpty" || action == "pattern.firstEmpty")
    {
        findEmptyPattern(action == "pattern.firstEmpty");
        return true;
    }
    if (action == "pattern.moveUp" || action == "pattern.moveDown")
    {
        const auto &list = controller.project.patterns;
        auto it =
            std::find_if(list.begin(), list.end(), [&](const auto &p) { return p.id == selectedPattern; });
        if (it != list.end())
        {
            CommandArgs args;
            args.pattern = selectedPattern;
            args.lane = int(std::distance(list.begin(), it)) + (action == "pattern.moveUp" ? -1 : 1);
            if (args.lane >= 0 && args.lane < int(list.size()))
                command("pattern.move", args);
        }
        return true;
    }
    if (action == "score.dump")
    {
        dumpScoreLog();
        return true;
    }
    if (action == "score.clear")
    {
        clearScoreLog();
        return true;
    }
    if (action.startsWith("tools.lastTweaked."))
    {
        const auto target = lastPluginTweak();
        if (!target)
        {
            status = "Finish a native plugin gesture first; no unambiguous target is available";
            repaint();
            return true;
        }
        const auto suffix = action.fromFirstOccurrenceOf("tools.lastTweaked.", false, false);
        if (suffix == "controls" || suffix == "create" || suffix == "init" || suffix == "removeInit")
        {
            juce::String operation, code;
            requestLastPluginTweak(*target,
                                   suffix == "controls" ? suffix
                                   : suffix == "create" ? "automation.plugin.create"
                                   : suffix == "init"   ? "automation.plugin.init"
                                                        : "automation.plugin.init.remove",
                                   operation, code);
        }
        return true;
    }
    if (action == "midi.typing")
    {
        typingButton.onClick();
        return true;
    }
    if (typingKey && typingKeys.enabled && !typingKey->getModifiers().isAnyModifierKeyDown() &&
        typingKeys.press(typingKey->getKeyCode()))
        return true;
    if (action.startsWith("browser."))
        return browser->viewAction(action);
    if (action.startsWith("playlist.") && action != "playlist.grouping" && action != "playlist.follow" &&
        action != "playlist.stretchMode" && !action.startsWith("playlist.select"))
    {
        if (focus != Scope::playlist)
            return false;
        static_cast<Editor *>(windows[2]->getContentComponent())->viewAction(action);
        return true;
    }
    if (action == "piano.insertSlide" || action == "piano.insertPortamento" ||
        action == "piano.changeColour" || action == "piano.selectColour" || action == "piano.noteProperties")
    {
        if (focus != Scope::piano)
            return false;
        auto *editor = static_cast<Editor *>(windows[1]->getContentComponent());
        const auto current = editor->noteModes(juce::JSON::parse("{\"action\":\"state\"}"));
        auto request = juce::var(new juce::DynamicObject);
        auto &args = *request.getDynamicObject();
        args.setProperty("action", action == "piano.changeColour"     ? "changeColour"
                                   : action == "piano.selectColour"   ? "selectColour"
                                   : action == "piano.noteProperties" ? "propertiesOpen"
                                                                      : "configure");
        if (action == "piano.insertSlide")
            args.setProperty("slide", !bool(current["slide"]));
        if (action == "piano.insertPortamento")
            args.setProperty("portamento", !bool(current["portamento"]));
        return pianoNoteModes(request)["completionState"].toString() != "failed";
    }
    if (action.startsWith("piano.") && action != "piano.toolMute" && action != "piano.toolSlice" &&
        action != "piano.exportMidi" && action != "piano.resizeLeft")
    {
        static_cast<Editor *>(windows[1]->getContentComponent())->viewAction(action);
        return true;
    }
    if (action.isEmpty())
        return false;
    if (action == "transport.metronome")
    {
        click.onClick();
        return true;
    }
    if (action == "transport.recordNotes")
    {
        recordNotes = !recordNotes;
        repaint();
        return true;
    }
    if (action == "transport.waitForInput")
    {
        waitForInput = !waitForInput;
        if (!waitForInput)
            armPlayWait(false);
        if (!waitForInput && waitingForInput)
        {
            armWaitForInput(false);
            if (auto result = finishMidiTake(); result.failed())
                status = result.getErrorMessage();
        }
        repaint();
        return true;
    }
    if (action == "transport.countIn")
    {
        countIn.onClick();
        return true;
    }
    if (audioTaking && action == "selection.cancel" && !midiTaking && !hasPendingMidiTake())
    {
        cancelAudioTake();
        return true;
    }
    if ((midiTaking || hasPendingMidiTake()) && action == "selection.cancel")
    {
        const bool editing = std::any_of(
            windows.begin(), windows.end(), [](const auto &window)
            { return static_cast<Editor *>(window->getContentComponent())->pointerGestureActive(); });
        if (!editing)
        {
            cancelAudioTake();
            finishMidiTake(true);
            return true;
        }
    }
    if ((midiTaking || hasPendingMidiTake()) && (action == "edit.undo" || action == "edit.redo"))
        if (auto take = finishMidiTake(); take.failed())
        {
            status = take.getErrorMessage();
            return false;
        }
    if (action.startsWith("macro."))
    {
        runMacro(action);
        return true;
    }
    if (action == "rack.group.dialog")
    {
        rackGroupDialog("assign");
        return true;
    }
    if (action == "rack.group.previous" || action == "rack.group.next")
    {
        std::vector<Id> filters{0, -1}; // All, Unsorted, then stable group IDs.
        for (const auto &group : controller.project.channelGroups)
            filters.push_back(group.id);
        const auto current = rackFilter == "all" ? 0 : rackFilter == "unsorted" ? -1 : rackGroup;
        auto at = std::find(filters.begin(), filters.end(), current);
        auto index = at == filters.end() ? 0 : int(at - filters.begin());
        index = (index + int(filters.size()) + (action.endsWith("next") ? 1 : -1)) % int(filters.size());
        setRackFilter(filters[size_t(index)] == 0    ? "all"
                      : filters[size_t(index)] == -1 ? "unsorted"
                                                     : "group",
                      filters[size_t(index)] > 0 ? filters[size_t(index)] : 0);
        return true;
    }
    if (action == "view.restoreLayout")
    {
        restoreDockedLayout();
        return true;
    }
    if (action == "view.playlist")
    {
        toggleEditor(2);
        return true;
    }
    if (action == "view.rack")
    {
        toggleEditor(0);
        return true;
    }
    if (action == "view.piano")
    {
        toggleEditor(1);
        return true;
    }
    if (action == "marker.next" || action == "marker.previous" || action == "marker.selectSection")
    {
        std::vector<double> beats;
        for (const auto &marker : controller.project.markers)
            beats.push_back(marker.beat);
        std::sort(beats.begin(), beats.end());
        const double here = audio.positionBeat();
        const auto after = std::upper_bound(beats.begin(), beats.end(), here + 1.e-6);
        if (action == "marker.selectSection")
        {
            // From the marker at/before the playhead (or Song start) to the next one (or Song end).
            const double start = after == beats.begin() ? 0. : *std::prev(after);
            const double end = after == beats.end() ? std::max(start + 1., controller.project.endBeat()) : *after;
            if (beats.empty() || end <= start)
            {
                status = "Add Playlist markers to select a section";
                return false;
            }
            playlistRange = std::pair{start, end};
            syncSongLoop();
            repaint();
            status = "Selected beats " + juce::String(start, 2) + " to " + juce::String(end, 2);
            return true;
        }
        const auto before = std::lower_bound(beats.begin(), beats.end(), here - 1.e-6);
        if (action == "marker.next" ? after == beats.end() : before == beats.begin())
        {
            status = action == "marker.next" ? "No marker after the playhead" : "No marker before the playhead";
            return false;
        }
        audio.seekBeat(action == "marker.next" ? *after : *std::prev(before));
        repaint();
        return true;
    }
    if (action.startsWith("performance.scene"))
    {
        // Launch a scene: its Patterns start (each at its quantum), anything else stops.
        const int scene = action.fromFirstOccurrenceOf("performance.scene", false, false).getIntValue();
        if (scene < 1 || scene > 8 || controller.project.performanceScenes[size_t(scene - 1)].empty())
        {
            status = "Performance scene " + juce::String(scene) + " is empty";
            return false;
        }
        const auto wanted = controller.project.performanceScenes[size_t(scene - 1)];
        for (const auto &[id, pad] : std::map<Id, performance::Pad>(performancePads))
            if (std::find(wanted.begin(), wanted.end(), id) == wanted.end())
                launchPattern(id, false);
        int started = 0;
        for (const auto pattern : wanted)
            if (controller.project.pattern(pattern) && !performancePads.count(pattern) &&
                launchPattern(pattern, true).wasOk())
                ++started;
        status = "Scene " + juce::String(scene) + ": " + juce::String(int(wanted.size())) + " Patterns";
        return true;
    }
    if (action == "performance.stopAll")
    {
        // Every launched Pattern stops; clips already placed stay and the transport keeps running.
        const bool any = !performancePads.empty();
        performancePads.clear();
        status = any ? "Stopped all launched Patterns" : "No Pattern is launched";
        return any;
    }
    if (action.startsWith("performance.pad"))
        return togglePerformancePad(action.fromFirstOccurrenceOf("performance.pad", false, false).getIntValue() - 1);
    if (action == "view.mixer")
    {
        toggleEditor(3);
        return true;
    }
    if (action == "transport.stepRecord")
    {
        stepRecord = !stepRecord;
        stepHeld.clear();
        stepChord.clear();
        if (stepRecord)
        {
            const double here = std::max(0., audio.positionBeat());
            stepCursor = std::floor(here / stepLength() + 1.e-9) * stepLength();
        }
        status = stepRecord ? "Step record on: play notes to write them at beat " + juce::String(stepCursor + 1, 2)
                            : juce::String("Step record off");
        repaint();
        return true;
    }
    if (action == "transport.stepRest")
    {
        if (!stepRecord)
            return false;
        stepCursor += stepLength();
        status = "Step record: rest, next step at beat " + juce::String(stepCursor + 1, 2);
        repaint();
        return true;
    }
    if (action == "view.drumPads")
    {
        showDrumPads();
        return true;
    }
    if (action == "view.outputScopeMode")
    {
        auto &scope = OutputScope::instance();
        scope.showSpectrum(!scope.spectrumShown());
        status = scope.spectrumShown() ? "Output meter shows the spectrum" : "Output meter shows the waveform";
        repaint();
        return true;
    }
    if (action == "transport.recordToSteps")
    {
        recordToSteps = !recordToSteps;
        status = recordToSteps ? "Recorded notes become step sequencer steps" : "Recorded notes go to the Piano roll";
        repaint();
        return true;
    }
    if (action == "transport.quantizeKeepDuration")
    {
        quantizeKeepDuration = !quantizeKeepDuration;
        status = quantizeKeepDuration ? "Recording quantize keeps the played note length"
                                      : "Recording quantize: starts and ends as set in MIDI settings";
        repaint();
        return true;
    }
    if (action == "transport.recordStartsPlayback" || action == "transport.disarmOnStop" || action == "view.timeDisplay")
    {
        auto &option = action == "transport.recordStartsPlayback" ? recordStartsPlayback
                       : action == "transport.disarmOnStop"       ? disarmOnStop
                                                                  : timeDisplaySeconds;
        option = !option;
        status = action == "transport.recordStartsPlayback" ? (option ? "Record also starts playback" : "Record only arms")
                 : action == "transport.disarmOnStop"       ? (option ? "Stop turns Record off" : "Record stays armed after Stop")
                                                            : (option ? "Time shown as minutes:seconds" : "Time shown as beats");
        repaint();
        return true;
    }
    if (action == "transport.waitOnlyWhenRecording" || action == "transport.waitRespectFilter")
    {
        auto &option = action == "transport.waitOnlyWhenRecording" ? waitOnlyWhenRecording : waitRespectFilter;
        option = !option;
        armPlayWait(false);
        status = action == "transport.waitOnlyWhenRecording"
                     ? (option ? "Wait for input only when recording" : "Wait for input also before plain playback")
                     : (option ? "Wait for input respects the recording filter"
                               : "Wait for input ignores the recording filter");
        repaint();
        return true;
    }
    if (action == "pattern.findEmptyNoNaming")
    {
        findEmptyPattern(false, false); // Like Find next empty, without opening the name field.
        return true;
    }
    if (action == "transport.rememberSeek")
    {
        rememberSeekTime = !rememberSeekTime;
        status = rememberSeekTime ? "Stop returns to where Play started" : "Stop returns to the start";
        repaint();
        return true;
    }
    if (action == "pattern.selectInPlaylist")
    {
        // Patterns menu > Select in playlist: every Playlist clip of the selected Pattern.
        selectedClips.clear();
        for (const auto &clip : controller.project.placements)
            if (clip.pattern == selectedPattern)
                selectedClips.push_back(clip.id);
        status = juce::String(int(selectedClips.size())) + " clip(s) of this Pattern selected in the Playlist";
        refresh();
        return !selectedClips.empty();
    }
    if (action == "pattern.randomColour")
    {
        // Patterns menu > Random colour: a new opaque colour for the selected Pattern (one Undo step).
        const auto *pattern = controller.project.pattern(selectedPattern);
        if (!pattern)
            return false;
        auto &random = juce::Random::getSystemRandom();
        CommandArgs args;
        args.pattern = selectedPattern;
        args.text = pattern->name;
        args.value = double(juce::Colour::fromHSV(random.nextFloat(), .45f + .3f * random.nextFloat(), .85f, 1.f).getARGB());
        command("pattern.properties", args);
        return true;
    }
    if (action == "view.performance")
    {
        showPerformance();
        return true;
    }
    if (action == "transport.recordReplace")
    {
        // Tools > MIDI recording replaces notes (off = overdub).
        midiRecordReplace = !midiRecordReplace;
        status = midiRecordReplace ? "MIDI recording: replace notes in the recorded span"
                                   : "MIDI recording: overdub (merge with existing notes)";
        repaint();
        return true;
    }
    if (action == "project.openTemplatesFolder" || action == "project.openBackupsFolder")
    {
        // Tools > Open templates / project backups folder. Test instances report the folder
        // without starting the file manager.
        const auto folder = action.endsWith("TemplatesFolder") ? templatesFolder() : projectBackupsFolder();
        if (!folder.isDirectory())
        {
            status = "The folder does not exist yet: " + folder.getFullPathName();
            return false;
        }
        if (!controllerTestMode)
            folder.startAsProcess();
        status = "Opened " + folder.getFullPathName();
        return true;
    }
    if (action == "view.browser")
    {
        browserButton.onClick();
        return true;
    }
    if (action == "view.coProducer")
    {
        coProducerButton.onClick();
        return true;
    }
    if (action == "view.chordProgression")
    {
        showChordProgression();
        return true;
    }
    if (action == "view.settings")
    {
        audioSettings();
        return true;
    }
    if (action == "view.plugins")
    {
        showPlugins();
        return true;
    }
    if (action == "midi.panic")
    {
        audio.midiInput.panic();
        return true;
    }
    if (action == "transport.play")
    {
        play.onClick();
        return true;
    }
    if (action == "transport.loop")
    {
        if (audioTaking)
        {
            status = "Stop the audio take before changing loop mode";
            return false;
        }
        if (midiTaking || hasPendingMidiTake() || needsRebuild || !audio.projectionDiagnostics().edit)
        {
            status = (midiTaking || hasPendingMidiTake())
                         ? "Finish or cancel the MIDI take before changing loop mode"
                         : "Wait for project audio preparation before changing loop mode";
            return false;
        }
        audio.setLooping(!audio.looping());
        loopButton.setToggleState(audio.looping(), juce::dontSendNotification);
        status = "Ready";
        return true;
    }
    if (const auto *takeAction = recordingActionForCommand(action))
    {
        // UI shortcuts act on the current take; MCP still requires an exact
        // positive token through its typed tool, never a caller-selected default.
        const auto token = midiTakeSerial();
        const auto result = juce::StringRef(takeAction) == juce::StringRef("finish") ? finishMidiTake()
                            : juce::StringRef(takeAction) == juce::StringRef("retry")
                                ? retryMidiTake(token)
                                : cancelPendingMidiTake(token);
        if (result.failed())
            status = result.getErrorMessage();
        return result.wasOk();
    }
    if (action == "transport.record")
    {
        record.onClick();
        return true;
    }
    if (action == "transport.stop")
    {
        stop.onClick();
        return true;
    }
    if (action.startsWith("sampler.view."))
        return navigateSamplerView(action);
    if (action == "sampler.showKeyboard")
    {
        // The Channel settings "Keyboard" button: scroll the open window to the root-note keyboard.
        const auto window = instrumentWindows.find(selectedChannel);
        if (window == instrumentWindows.end() || !window->second || !window->second->isVisible())
            return false;
        auto *button = dynamic_cast<juce::Button *>(
            window->second->controlsComponent()->findChildWithID("instrument.sampler.showKeyboard"));
        if (!button || !button->isEnabled() || !button->onClick)
            return false;
        button->onClick();
        return true;
    }
    if (action == "sampler.page.sample" || action == "sampler.page.envelope" || action == "sampler.page.misc")
    {
        // Channel settings window page of the selected sampler Channel (opened if needed).
        if (const auto *c = controller.project.channel(selectedChannel); !c || c->instrument)
            return false;
        if (!instrumentWindows.contains(selectedChannel) || !instrumentWindows[selectedChannel] ||
            !instrumentWindows[selectedChannel]->isVisible())
            openInstrument(selectedChannel);
        const auto window = instrumentWindows.find(selectedChannel);
        if (window == instrumentWindows.end() || !window->second)
            return false;
        window->second->showPage(action.endsWith("sample") ? 0 : action.endsWith("envelope") ? 1 : 2);
        return true;
    }
    if (action.startsWith("sampler.modulation."))
        return selectSamplerModulationTarget(
            selectedChannel,
            samplerModulationTarget(
                action.fromFirstOccurrenceOf("sampler.modulation.", false, false).toStdString()));
    if (action == "channel.openInstrument")
    {
        openInstrument(selectedChannel);
        return true;
    }
    if (action == "channel.pluginControls")
    {
        if (const auto *channel = controller.project.channel(selectedChannel); channel && channel->instrument)
            openPluginControlsUI(channel->instrument->id);
        return true;
    }
    if (action == "channel.pluginRecover")
    {
        if (const auto *channel = controller.project.channel(selectedChannel); channel && channel->instrument)
            recoverPlugin(channel->instrument->id);
        return true;
    }
    if (action == "transport.mode")
    {
        mode.onClick();
        return true;
    }
    if (action == "project.save")
    {
        chooseSave();
        return true;
    }
    if (action == "project.open")
    {
        chooseOpen();
        return true;
    }
    if (action == "piano.exportMidi")
    {
        chooseMidiExport();
        return true;
    }
    if (action == "project.export")
    {
        chooseExport();
        return true;
    }
    if (action == "notes.insertControllerValue")
    {
        // The lane of the Channel's most recently added event in this Pattern (pitch bend when none).
        int type = 1, number = 0;
        Id newest = 0;
        if (const auto *part = controller.project.pattern(selectedPattern))
            for (const auto &c : part->controllers)
                if (c.channel == selectedChannel && c.type <= 2 && c.id > newest)
                {
                    newest = c.id;
                    type = c.type;
                    number = c.type == 0 ? c.number : 0;
                }
        if (auto inserted = insertControllerValue(type, number); inserted.failed())
        {
            status = inserted.getErrorMessage();
            repaint();
            return false;
        }
        return true;
    }
    if (action == "mixer.selectChannelsRouted" || action == "mixer.selectRoutedHere" || action == "mixer.selectRoutedFrom")
    {
        const auto selected = selectRoutedFor(selectedInsert, action == "mixer.selectChannelsRouted" ? 0
                                                              : action == "mixer.selectRoutedHere"   ? 1
                                                                                                     : 2);
        if (selected.failed())
            status = selected.getErrorMessage();
        repaint();
        return selected.wasOk();
    }
    if (action == "notes.scriptAgain")
    {
        if (lastNoteScript.isEmpty())
        {
            status = "Run a script first (Piano roll menu > Scripts)";
            repaint();
            return false;
        }
        if (auto ran = runNoteScript(lastNoteScript, lastNoteScriptName); ran.failed())
        {
            status = ran.getErrorMessage();
            repaint();
            return false;
        }
        return true;
    }
    if (action == "channel.automateVolume" || action == "channel.automatePan")
    {
        const auto made = createChannelAutomation(selectedChannel, action == "channel.automatePan");
        if (made.failed())
            status = made.getErrorMessage();
        repaint();
        return made.wasOk();
    }
    if (action == "mixer.routeSelectedHere" || action == "mixer.routeSelectedHereOnly" || action == "mixer.createSubmix")
    {
        const auto started = action == "mixer.createSubmix"
                                 ? createSubmix()
                                 : routeSelectedTo(selectedInsert, action == "mixer.routeSelectedHereOnly");
        if (started.failed())
            status = started.getErrorMessage();
        repaint();
        return started.wasOk();
    }
    if (action == "mixer.trimUnused")
    {
        trimUnusedInserts();
        return true;
    }
    if (action == "mixer.insert.fromChainPreset")
    {
        addInsertFromChainPreset();
        return true;
    }
    if (action == "project.tempoHalf" || action == "project.tempoDouble" || action == "project.tempoNudgeUp" ||
        action == "project.tempoNudgeDown")
    {
        // FL tempo menu: half / double speed and a one-BPM nudge. Each is one ordinary tempo edit (one Undo step).
        const double now = controller.project.tempo;
        CommandArgs a;
        a.value = action == "project.tempoHalf" ? now / 2 : action == "project.tempoDouble" ? now * 2
                  : action == "project.tempoNudgeUp" ? now + 1 : now - 1;
        if (a.value < 20 || a.value > 400)
        {
            status = "Tempo stays within 20 to 400 BPM";
            repaint();
            return false;
        }
        const auto revision = controller.revision;
        command("project.tempo", a);
        if (controller.revision == revision)
            return false;
        status = "Tempo " + juce::String(a.value, 2) + " BPM";
        repaint();
        return true;
    }
    if (action == "project.tapTempo")
    {
        tapTempo();
        return true;
    }
    if (action == "project.saveTemplate")
    {
        promptSaveTemplate();
        return true;
    }
    if (action == "project.new")
    {
        requestProjectTransition([this](bool discard) { (void)newProject(false, discard); });
        return true;
    }
    if (action == "edit.undo")
    {
        if (auto result = controller.undoEditChecked(); result.failed())
        {
            status = result.getErrorMessage();
            repaint();
        }
        return true;
    }
    if (action == "edit.redo")
    {
        if (auto result = controller.redoEditChecked(); result.failed())
        {
            status = result.getErrorMessage();
            repaint();
        }
        return true;
    }
    if (action == "selection.cancel")
    {
        for (auto &window : windows)
            static_cast<Editor *>(window->getContentComponent())->cancelPointerGesture();
        controller.cancelGesture();
        selectedNotes.clear();
        selectedClips.clear();
        refresh();
        return true;
    }
    if (action == "piano.toolSlice")
    {
        if (focus != Scope::piano)
            return false;
        static_cast<Editor *>(windows[1]->getContentComponent())->setNoteSliceTool();
        status = "Slice notes";
        refresh();
        return true;
    }
    if (action == "piano.toolMute")
    {
        if (focus != Scope::piano)
            return false;
        static_cast<Editor *>(windows[1]->getContentComponent())->setNoteMuteTool();
        status = "Mute notes";
        refresh();
        return true;
    }
    if (action == "tool.slice" || action == "tool.slip" || action == "tool.mute" || action == "tool.select" ||
        action == "tool.delete" || action == "tool.zoom")
    {
        if (focus != Scope::playlist)
            return false;
        static_cast<Editor *>(windows[2]->getContentComponent())
            ->setPlaylistTool(action == "tool.slice"    ? 3
                              : action == "tool.slip"   ? 2
                              : action == "tool.mute"   ? 4
                              : action == "tool.select" ? 5
                              : action == "tool.delete" ? 6
                                                        : 7);
        status = action == "tool.slice"    ? "Slice"
                 : action == "tool.slip"   ? "Slip"
                 : action == "tool.mute"   ? "Mute clips"
                 : action == "tool.select" ? "Select clips"
                 : action == "tool.delete" ? "Delete clips"
                                           : "Zoom region";
        repaint();
        return true;
    }
    if (action == "tool.play")
    {
        if (focus == Scope::playlist)
            static_cast<Editor *>(windows[2]->getContentComponent())->setPlaylistTool(8);
        else if (focus == Scope::piano)
        {
            static_cast<Editor *>(windows[1]->getContentComponent())->setPaintMode(false);
            pianoPlayTool = true;
            pianoDrumPaint = false;
        }
        else
            return false;
        status = focus == Scope::playlist ? "Play selected: hold on the Playlist to play from there"
                                          : "Play selected: hold a note to hear it";
        repaint();
        return true;
    }
    if (action == "tool.draw" || action == "tool.paint" || action == "tool.drumPaint")
    {
        pianoPlayTool = false;
        if (action == "tool.drumPaint")
            focus = Scope::piano;
        int i = int(focus) - 1;
        if (i >= 0 && i < 4)
            dynamic_cast<Editor *>(windows[size_t(i)]->getContentComponent())
                ->setPaintMode(action != "tool.draw");
        if (focus == Scope::piano)
            pianoDrumPaint = action == "tool.drumPaint";
        status = action == "tool.drumPaint" ? "Paint drum sequencer: click notes to mute/unmute"
                 : action == "tool.paint"   ? "Paint"
                                            : "Draw";
        repaint();
        return true;
    }
    CommandArgs a;
    a.pattern = selectedPattern;
    a.channel = selectedChannel;
    a.pitch = 0;
    a.length = 0;
    a.ids = focus == Scope::playlist ? selectedClips : selectedNotes;
    if (action == "playlist.grouping")
    {
        const auto result = setPlaylistGrouping(!controller.clipGroupingEnabled);
        status = result.wasOk() ? "Ready" : result.getErrorMessage();
        return result.wasOk();
    }
    if (action == "clip.pitchDown" || action == "clip.pitchUp" || action == "clip.reverse")
    {
        // The first selected Audio Clip; the offline job runs one clip at a time.
        const auto selection = effectiveClipSelection();
        const auto clip = std::find_if(controller.project.audioPlacements.begin(), controller.project.audioPlacements.end(),
                                       [&](const auto &p)
                                       { return std::find(selection.begin(), selection.end(), p.id) != selection.end(); });
        if (clip == controller.project.audioPlacements.end())
        {
            status = "Select an Audio Clip first";
            repaint();
            return false;
        }
        const auto started = action == "clip.reverse" ? editAudioClip(clip->id, "reverse", 0)
                                                      : editAudioClip(clip->id, "pitch", action == "clip.pitchUp" ? 1. : -1.);
        if (started.failed())
            status = started.getErrorMessage();
        repaint();
        return started.wasOk();
    }
    if (action == "clip.group" || action == "clip.ungroup")
    {
        a.ids = selectedClips;
        command(action, a);
        return status == "Ready";
    }
    if (action.startsWith("channel.multiOutputs"))
    {
        // Multi-output: route the selected instrument's next N output buses (after the main one)
        // to N free Mixer tracks, lowest first; 0 keeps the main output only. One Undo step.
        const int wanted = action.getTrailingIntValue();
        const auto *channel = controller.project.channel(selectedChannel);
        if (!channel || !channel->instrument || channel->audioClip || wanted < 0 ||
            wanted > PluginBusRequest::maximumExtraOutputBuses)
        {
            status = "channel.outputRoutes.invalidTarget: Select a plugin instrument Channel";
            repaint();
            return false;
        }
        if (wanted > 0)
        {
            PluginBusCatalogue catalogue;
            const auto host = audio.preparedPluginHost(controller.project, channel->instrument->id);
            if (host && host->readBusCatalogue(catalogue) && catalogue.outputCount < 1 + wanted)
            {
                status = "channel.outputRoutes.busUnavailable: " + channel->instrument->name + " has " +
                         juce::String(catalogue.outputCount) + " output bus(es)";
                repaint();
                return false;
            }
        }
        std::vector<int> free;
        for (const auto &insert : controller.project.mixer)
        {
            const bool used =
                std::any_of(controller.project.channels.begin(), controller.project.channels.end(),
                            [&](const auto &other)
                            {
                                return other.mixer == insert.index ||
                                       (other.instrument && other.id != channel->id &&
                                        std::find(other.instrument->extraOutputMixers.begin(),
                                                  other.instrument->extraOutputMixers.end(),
                                                  insert.index) != other.instrument->extraOutputMixers.end());
                            });
            if (insert.index > 0 && !used)
                free.push_back(insert.index);
        }
        std::sort(free.begin(), free.end());
        if (int(free.size()) < wanted)
        {
            status = "channel.outputRoutes.insufficient: " + juce::String(int(free.size())) +
                     " free Mixer track(s) for " + juce::String(wanted) + " output(s); add inserts first";
            repaint();
            return false;
        }
        CommandArgs args;
        args.channel = channel->id;
        for (int i = 0; i < wanted; ++i)
            args.ids.push_back(Id(free[size_t(i)]));
        if (audio.hasAudioDevice())
        {
            // Captures plugin state first, then applies the routes (status reports the outcome).
            command("channel.outputRoutes", args);
            return true;
        }
        const auto revision = controller.revision;
        command("channel.outputRoutes", args);
        const bool result = controller.revision != revision;
        if (result)
        {
            juce::StringArray tracks;
            for (const auto id : args.ids)
                tracks.add(juce::String(id));
            status = wanted == 0 ? channel->name + ": main output only"
                                 : channel->name + ": outputs 2-" + juce::String(wanted + 1) + " to Mixer tracks " +
                                       tracks.joinIntoString(", ");
        }
        repaint();
        return result;
    }
    if (action == "rack.assignFreeInserts")
    {
        // Each selected Channel (Rack order) gets its own unused Mixer insert, lowest first,
        // as one routing plan and one Undo step.
        std::vector<Id> targets;
        for (const auto &channel : controller.project.channels)
            if (std::find(selectedChannels.begin(), selectedChannels.end(), channel.id) != selectedChannels.end() &&
                !channel.audioClip)
                targets.push_back(channel.id);
        if (targets.empty() && controller.project.channel(selectedChannel))
            targets.push_back(selectedChannel);
        std::vector<int> free;
        for (const auto &insert : controller.project.mixer)
            if (insert.index > 0 &&
                std::none_of(controller.project.channels.begin(), controller.project.channels.end(),
                             [&](const auto &channel) { return channel.mixer == insert.index; }))
                free.push_back(insert.index);
        std::sort(free.begin(), free.end());
        if (targets.empty() || free.size() < targets.size())
        {
            status = "rack.assignFree.insufficient: " + juce::String(int(free.size())) + " free Mixer insert(s) for " +
                     juce::String(int(targets.size())) + " Channel(s); add inserts in the Mixer first";
            repaint();
            return false;
        }
        std::vector<CommandArgs> assignments;
        for (size_t i = 0; i < targets.size(); ++i)
        {
            CommandArgs assignment;
            assignment.channel = targets[i];
            assignment.mixer = free[i];
            assignments.push_back(assignment);
        }
        const auto result = applyRoutingSequence(assignments, "Assign Channels to free Mixer inserts");
        status = result.wasOk() ? "Assigned " + juce::String(int(targets.size())) + " Channel(s) to free Mixer inserts"
                                : result.getErrorMessage();
        repaint();
        return result.wasOk();
    }
    if (action == "channel.mixer")
    {
        if (focus == Scope::mixer)
            a.mixer = selectedInsert;
        else
        {
            a.mixer = -1;
            for (const auto &insert : controller.project.mixer)
            {
                if (insert.index <= 0)
                    continue;
                const bool used =
                    std::any_of(controller.project.channels.begin(), controller.project.channels.end(),
                                [&](const auto &channel) { return channel.mixer == insert.index; });
                if (!used && (a.mixer < 0 || insert.index < a.mixer))
                    a.mixer = insert.index;
            }
            if (a.mixer < 0)
            {
                status = "No free Mixer insert; choose an insert in the Mixer";
                repaint();
                return true;
            }
        }
        command(action, a);
        return true;
    }
    if (action == "clipboard.copy")
    {
        clipboardPattern = selectedPattern;
        clipboardChannel = selectedChannel;
        return true;
    }
    if (action == "clipboard.paste")
    {
        a.target = clipboardPattern;
        a.value = double(clipboardChannel);
        command("channel.paste", a);
        return true;
    }
    if (action == "selection.all")
    {
        std::vector<Id> list;
        if (focus == Scope::playlist)
        {
            for (auto &c : controller.project.placements)
                list.push_back(c.id);
            for (auto &c : controller.project.audioPlacements)
                list.push_back(c.id);
            for (auto &c : controller.project.automation.placements)
                list.push_back(c.id);
        }
        else if (auto *p = controller.project.pattern(selectedPattern))
            for (auto &n : p->notes)
                if (n.channel == selectedChannel)
                    list.push_back(n.id);
        selectItems(focus == Scope::playlist, list);
        return true;
    }
    if (action == "selection.next" || action == "selection.previous")
    {
        // Keyboard and screen-reader browsing: one item at a time in time order, announced.
        const bool clips = focus == Scope::playlist, forward = action == "selection.next";
        struct Item
        {
            double beat;
            int order; // Pitch for notes, track for clips.
            Id id;
            juce::String text;
        };
        std::vector<Item> items;
        const auto &project = controller.project;
        const double bar = project.numerator * 4. / project.denominator;
        auto where = [&](double beat)
        {
            return "bar " + juce::String(int(std::floor(beat / bar)) + 1) + " beat " +
                   juce::String(std::fmod(beat, bar) + 1, std::fmod(beat, 1) == 0 ? 0 : 2);
        };
        if (clips)
        {
            for (const auto &c : project.placements)
                if (const auto *part = project.pattern(c.pattern))
                    items.push_back({c.beat, c.lane, c.id, part->name + ", track " + juce::String(c.lane + 1) + ", " + where(c.beat)});
            for (const auto &c : project.audioPlacements)
                if (const auto *channel = project.channel(c.channel))
                    items.push_back({c.beat, c.lane, c.id,
                                     channel->name + " audio, track " + juce::String(c.lane + 1) + ", " + where(c.beat)});
            for (const auto &c : project.automation.placements)
                items.push_back({c.beat, c.lane, c.id, "Automation clip, track " + juce::String(c.lane + 1) + ", " + where(c.beat)});
        }
        else if (const auto *part = project.pattern(selectedPattern))
            for (const auto &n : part->notes)
                if (n.channel == selectedChannel)
                    items.push_back({n.beat, n.pitch, n.id,
                                     juce::MidiMessage::getMidiNoteName(n.pitch, true, true, 5) + ", " + where(n.beat) +
                                         ", velocity " + juce::String(juce::roundToInt(n.velocity * 127)) +
                                         (n.muted ? ", muted" : "")});
        std::sort(items.begin(), items.end(),
                  [](const auto &x, const auto &y) { return std::tie(x.beat, x.order, x.id) < std::tie(y.beat, y.order, y.id); });
        const auto &chosen = clips ? selectedClips : selectedNotes;
        auto current = chosen.empty() ? items.end()
                                      : std::find_if(items.begin(), items.end(),
                                                     [&](const auto &item) { return item.id == chosen.back(); });
        auto next = current == items.end() ? (forward ? items.begin() : items.end())
                                           : (forward ? current + 1 : current);
        if (!forward)
            next = next == items.begin() ? items.end() : next - 1;
        juce::String announcement;
        if (items.empty())
            announcement = clips ? "No clips in the Playlist" : "No notes for this Channel";
        else if (next == items.end())
            announcement = forward ? "Last item" : "First item";
        else
        {
            selectItems(clips, {next->id});
            announcement = next->text + " (" + juce::String(int(next - items.begin()) + 1) + " of " +
                           juce::String(int(items.size())) + ")";
        }
        status = announcement;
        juce::AccessibilityHandler::postAnnouncement(announcement,
                                                     juce::AccessibilityHandler::AnnouncementPriority::high);
        repaint();
        return !items.empty() && next != items.end();
    }
    if (action == "selection.none")
    {
        selectItems(focus == Scope::playlist, {});
        return true;
    }
    if (action == "notes.delete" || action == "clip.delete")
    {
        command(action, a);
        return true;
    }
    if (action == "clip.merge" || action == "clip.mergeSimilar")
    {
        a.ids = effectiveClipSelection();
        command(action, a);
        return true;
    }
    if (action == "view.keyLabelsNone" || action == "view.keyLabelsRoot" || action == "view.keyLabelsWhite")
    {
        pianoKeyboardLabels = action == "view.keyLabelsNone" ? 0 : action == "view.keyLabelsRoot" ? 1 : 2;
        status = pianoKeyboardLabels == 0 ? "Keyboard labels off"
                 : pianoKeyboardLabels == 1 ? "Keyboard labels: root notes"
                                            : "Keyboard labels: all white notes";
        refresh();
        return true;
    }
    if (action == "view.centerPlayhead")
    {
        const int editor = focus == Scope::piano ? 1 : 2;
        if (windows[editor])
            if (auto *view = dynamic_cast<Editor *>(windows[editor]->getContentComponent()))
                view->centerOnPlayhead();
        return true;
    }
    if (action == "view.snapIncomingMidi")
    {
        snapIncomingMidi = !snapIncomingMidi;
        updateMidiScaleSnap();
        status = !snapIncomingMidi                ? "Incoming MIDI is not snapped"
                 : pianoGuide.name.isEmpty()      ? "Incoming MIDI will snap once a scale guide is chosen"
                                                  : "Incoming MIDI snaps to " + pianoGuide.name;
        return true;
    }
    if (action == "view.noteGridHighlights")
    {
        pianoGridHighlights = !pianoGridHighlights;
        status = pianoGridHighlights ? "Note grid highlights on" : "Note grid highlights off";
        if (windows[1])
            windows[1]->repaint();
        return true;
    }
    if (action == "mixer.meterWave")
    {
        mixerMeterWave = !mixerMeterWave;
        status = mixerMeterWave ? "Meter-wave view on" : "Meter-wave view off";
        for (auto &w : windows)
            if (w)
                w->repaint();
        repaint();
        return true;
    }
    if (action == "mixer.spareStore" || action == "mixer.spareFlip")
    {
        const auto started = startEffectSpare(selectedInsert, action == "mixer.spareFlip");
        if (started.failed())
            status = started.getErrorMessage();
        repaint();
        return started.wasOk();
    }
    if (action == "channel.spareStore" || action == "channel.spareFlip")
    {
        if (!controller.project.channel(selectedChannel))
            return false;
        if (action == "channel.spareStore")
            storeSpareState(selectedChannel);
        else
            flipSpareState(selectedChannel);
        return true;
    }
    if (action == "track.selectPrevious" || action == "track.selectNext" || action == "track.extendPrevious" ||
        action == "track.extendNext")
    {
        const bool up = action.endsWith("Previous");
        if (!selectedTracks.empty())
            trackCursor = juce::jlimit(0, 127, trackCursor + (up ? -1 : 1));
        if (!action.startsWith("track.extend"))
            selectedTracks.clear();
        selectedTracks.insert(trackCursor);
        status = "Track " + juce::String(trackCursor + 1) + " selected (" + juce::String(int(selectedTracks.size())) + ")";
        if (windows[2])
            windows[2]->repaint();
        return true;
    }
    if (action == "track.cloneSelected")
    {
        if (selectedTracks.empty())
        {
            status = "Select tracks first (Ctrl+Up/Down or click a track name)";
            return false;
        }
        // Bottom-up so the lanes still to clone keep their positions; one Undo step.
        controller.beginGesture();
        for (auto lane = selectedTracks.rbegin(); lane != selectedTracks.rend(); ++lane)
        {
            CommandArgs clone;
            clone.lane = *lane;
            command("track.clone", clone);
        }
        const auto finished = controller.endGestureChecked("Clone selected tracks");
        status = finished.wasOk() ? juce::String(int(selectedTracks.size())) + " track(s) cloned" : finished.getErrorMessage();
        repaint();
        return finished.wasOk();
    }
    if (action == "rack.linkSequential")
    {
        // Each selected Channel (Rack order) goes to the next insert from the selected one.
        std::vector<int> inserts;
        for (const auto &insert : controller.project.mixer)
            if (insert.index > 0 && insert.index >= std::max(1, selectedInsert))
                inserts.push_back(insert.index);
        std::sort(inserts.begin(), inserts.end());
        std::vector<Id> channels;
        for (const auto &channel : controller.project.channels)
            if (selected(selectedChannels, channel.id))
                channels.push_back(channel.id);
        if (channels.empty() || channels.size() > inserts.size())
        {
            status = channels.empty() ? "Select Channels first"
                                      : "Not enough Mixer inserts after the selected one";
            repaint();
            return false;
        }
        std::vector<CommandArgs> assignments;
        for (size_t i = 0; i < channels.size(); ++i)
        {
            CommandArgs link;
            link.channel = channels[i];
            link.mixer = inserts[i];
            assignments.push_back(link);
        }
        const auto result = applyRoutingSequence(assignments, "Link Channels to Mixer inserts");
        status = result.wasOk() ? (routingPending() ? routingPendingStatus : routingReadyStatus) : result.getErrorMessage();
        repaint();
        return result.wasOk();
    }
    if (action == "rack.zip" || action == "rack.unzipAll")
    {
        if (action == "rack.zip")
            for (auto id : selectedChannels)
                zippedChannels.insert(id);
        else
            zippedChannels.clear();
        status = action == "rack.zip" ? juce::String(int(selectedChannels.size())) + " Channel(s) zipped (right-click to unzip)"
                                      : "All Channels unzipped";
        repaint();
        for (auto &w : windows)
            if (w)
                w->repaint();
        return true;
    }
    if (action == "playlist.stretchMode")
    {
        audioStretchMode = !audioStretchMode;
        status = audioStretchMode ? "Stretch on: drag an audio clip's right edge to stretch it (keeps pitch)"
                                  : "Stretch off: the right edge resizes audio clips";
        repaint();
        return true;
    }
    if (action == "view.pianoFollow")
    {
        pianoFollowPlayhead = !pianoFollowPlayhead;
        status = pianoFollowPlayhead ? "Piano roll auto-scroll on" : "Piano roll auto-scroll off";
        repaint();
        return true;
    }
    if (action == "view.pianoBackgroundWaveform")
    {
        pianoBackgroundWaveform = !pianoBackgroundWaveform;
        status = pianoBackgroundWaveform ? "Piano roll background waveform on" : "Piano roll background waveform off";
        refresh();
        return true;
    }
    if (action == "view.previewDuringPlayback")
    {
        previewDuringPlayback = !previewDuringPlayback;
        status = previewDuringPlayback ? "Notes preview during playback" : "No note preview during playback";
        refresh();
        return true;
    }
    if (action == "mixer.linkAll")
    {
        int first = 0;
        const auto linked = linkAllMixerParameters(selectedInsert, first);
        status = linked.failed() ? linked.getErrorMessage()
                                 : "Linked Mixer track " + juce::String(selectedInsert) + ": volume CC" +
                                       juce::String(first) + ", pan CC" + juce::String(first + 1) +
                                       ", stereo separation CC" + juce::String(first + 2) + " (MIDI channel 1)";
        refresh();
        return true;
    }
    if (action == "mixer.renderArm")
    {
        const int insert = selectedInsert;
        if (!renderArmedInserts.erase(insert))
            renderArmedInserts.insert(insert);
        status = juce::String(renderArmedInserts.contains(insert) ? "Armed" : "Disarmed") + " Mixer track " +
                 juce::String(insert) + " for rendering (" + juce::String(int(renderArmedInserts.size())) + " armed)";
        refresh();
        return true;
    }
    if (action == "mixer.renderArmed")
    {
        if (stemTargets(controller.project, {}, true).empty())
        {
            status = "No Mixer track is armed for rendering (Mixer track menu > File > Arm for rendering)";
            refresh();
            return true;
        }
        const auto base = projectFile == juce::File{}
                              ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("VibeRa")
                              : projectFile.withFileExtension("");
        chooser = std::make_unique<juce::FileChooser>("Render armed Mixer tracks into folder", base.getParentDirectory());
        auto safe = juce::Component::SafePointer<Studio>(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [safe](const juce::FileChooser &fc)
                             {
                                 if (!safe || fc.getResult() == juce::File{})
                                     return;
                                 safe->stemsArmedOnly = true;
                                 safe->exportTo(fc.getResult(), 0, true);
                             });
        return true;
    }
    if (action == "view.rackFocusSelected")
    {
        rackFocusSelected = !rackFocusSelected;
        status = rackFocusSelected ? "Channel Rack follows the selected Channel" : "Channel Rack focus off";
        refresh();
        return true;
    }
    if (action == "view.rackScrollToSelected")
    {
        rackScrollRequest = true;
        status = "Scrolled to the selected Channel";
        refresh();
        return true;
    }
    if (action == "view.rackButtonsNarrow" || action == "view.rackButtonsNormal" || action == "view.rackButtonsWide")
    {
        rackButtonWidth = action == "view.rackButtonsNarrow" ? 0 : action == "view.rackButtonsNormal" ? 1 : 2;
        status = rackButtonWidth == 0   ? "Channel buttons: narrow"
                 : rackButtonWidth == 1 ? "Channel buttons: normal"
                                        : "Channel buttons: wide";
        refresh();
        return true;
    }
    if (action == "view.followCenter")
    {
        followCenter = !followCenter;
        status = followCenter ? "Auto-scroll keeps the playhead centred" : "Auto-scroll pages with the playhead";
        repaint();
        return true;
    }
    if (action == "playlist.follow")
    {
        followPlayhead = !followPlayhead;
        status = followPlayhead ? "Playlist auto-scroll on" : "Playlist auto-scroll off";
        repaint();
        return true;
    }
    if (action == "clip.render")
    {
        const auto started = renderClipsToAudio(effectiveClipSelection());
        status = started.wasOk() ? "Rendering clips to audio" : started.getErrorMessage();
        repaint();
        return started.wasOk();
    }
    if (action == "notes.copyMidiClipboard")
    {
        const auto snapshot = midiExportSnapshot();
        if (!snapshot || midiClipboardOperation.isNotEmpty())
        {
            status = "Nothing to copy, or a MIDI clipboard copy is already running";
            repaint();
            return false;
        }
        midiClipboardFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                .getChildFile("VibeRa MIDI clipboard")
                                .getChildFile("VibeRa score.mid");
        midiClipboardFile.getParentDirectory().createDirectory();
        juce::String operation;
        const auto started = startMidiExport(midiClipboardFile, *snapshot, true, operation);
        if (started.failed())
        {
            status = started.getErrorMessage();
            repaint();
            return false;
        }
        midiClipboardOperation = operation;
        status = "Copying the Piano roll to the MIDI clipboard...";
        repaint();
        return true;
    }
    if (action == "notes.exportScore")
    {
        chooser = std::make_unique<juce::FileChooser>("Export as score sheet (PDF)",
                                                      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                                          .getChildFile("score.pdf"),
                                                      "*.pdf");
        auto safe = juce::Component::SafePointer<Studio>(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::warnAboutOverwriting,
                             [safe](const juce::FileChooser &choice)
                             {
                                 if (!safe || choice.getResult() == juce::File{})
                                     return;
                                 if (const auto written = safe->exportScoreSheet(choice.getResult().withFileExtension("pdf"));
                                     written.failed())
                                     safe->status = written.getErrorMessage();
                                 safe->repaint();
                             });
        return true;
    }
    if (action == "notes.pasteMidiClipboard")
    {
        const juce::File file(juce::String(midi_clipboard::firstMidiFile().c_str()));
        if (file == juce::File{} || !file.existsAsFile())
        {
            status = "No MIDI file on the clipboard (copy a .mid file, or use Copy to MIDI clipboard)";
            repaint();
            return false;
        }
        importMidiFile(file, selectedPattern, selectedChannel, false);
        return true;
    }
    if (action == "notes.copy" || action == "notes.cut")
    {
        const auto *part = controller.project.pattern(selectedPattern);
        auto board = std::make_shared<NoteClipboard>();
        board->origin = 1.e18;
        if (part)
            for (const auto &n : part->notes)
                if (selectedNotes.empty() ? n.channel == selectedChannel : selected(selectedNotes, n.id))
                    board->notes.push_back(n), board->origin = std::min(board->origin, n.beat);
        if (board->notes.empty())
        {
            status = "No notes to copy";
            repaint();
            return false;
        }
        noteClipboard = board;
        if (action == "notes.cut")
        {
            CommandArgs cut;
            cut.pattern = selectedPattern;
            cut.channel = selectedChannel;
            for (const auto &n : board->notes)
                cut.ids.push_back(n.id);
            command("notes.delete", cut);
        }
        else
            status = juce::String(int(board->notes.size())) + " note(s) copied";
        repaint();
        return true;
    }
    if (action == "notes.paste")
    {
        if (!noteClipboard || noteClipboard->notes.empty())
        {
            status = "Copy or cut notes first (Ctrl+C / Ctrl+X)";
            repaint();
            return false;
        }
        a.noteClipboard = noteClipboard;
        a.ids.clear();
        // Familiar Ctrl+V: from the start of the leftmost visible bar in the Piano roll.
        a.beat = static_cast<Editor *>(windows[1]->getContentComponent())->pasteBeat();
        const auto firstNew = controller.project.nextId;
        command(action, a);
        if (status != "Ready")
            return false;
        std::vector<Id> pasted;
        if (const auto *part = controller.project.pattern(selectedPattern))
            for (const auto &n : part->notes)
                if (n.id >= firstNew)
                    pasted.push_back(n.id);
        selectItems(false, pasted);
        return true;
    }
    if (action == "notes.insertSpaceAction" || action == "notes.insertSpaceSliced" ||
        action == "notes.deleteSpaceAction" || action == "notes.trimAction")
    {
        const auto range = static_cast<Editor *>(windows[1]->getContentComponent())->timeSelection();
        if (!range)
        {
            status = "Select time on the Piano roll ruler first (Ctrl+drag)";
            repaint();
            return false;
        }
        a.ids.clear();
        a.beat = range->first;
        a.length = range->second - range->first;
        a.enabled = action == "notes.insertSpaceSliced";
        command(action == "notes.deleteSpaceAction" ? "notes.deleteSpace"
                : action == "notes.trimAction"      ? "notes.trim"
                                                    : "notes.insertSpace",
                a);
        return status == "Ready";
    }
    if (action == "steps.rotateLeft" || action == "steps.rotateRight")
    {
        CommandArgs args;
        args.pattern = selectedPattern;
        args.channel = selectedChannel;
        for (const auto id : selectedChannels)
            args.ids.push_back(id);
        args.value = action == "steps.rotateLeft" ? -1 : 1;
        command("steps.rotate", args);
        return true;
    }
    if (action == "notes.rotateLeft" || action == "notes.rotateRight" || action == "notes.discardLengthsAction")
    {
        a.ids = selectedNotes;
        if (action == "notes.discardLengthsAction")
        {
            a.length = snap > 0 ? snap : .25;
            command("notes.discardLengths", a);
        }
        else
        {
            a.beat = (action.endsWith("Left") ? -1 : 1) * (snap > 0 ? snap : .25);
            command("notes.rotate", a);
        }
        return status == "Ready";
    }
    if (action == "selection.random" || action == "selection.odd" || action == "selection.muted" ||
        action == "selection.overlapping" || action == "selection.stacked")
    {
        const auto *part = controller.project.pattern(selectedPattern);
        if (!part)
            return false;
        std::vector<const Note *> mine;
        for (const auto &n : part->notes)
            if (n.channel == selectedChannel)
                mine.push_back(&n);
        std::vector<Id> chosenNotes;
        if (action == "selection.random")
        {
            chosenNotes = selectedNotes;
            std::vector<Id> rest;
            for (const auto *n : mine)
                if (!selected(selectedNotes, n->id))
                    rest.push_back(n->id);
            if (!rest.empty())
                chosenNotes.push_back(rest[size_t(juce::Random::getSystemRandom().nextInt(int(rest.size())))]);
        }
        else
            for (const auto *n : mine)
            {
                bool pick = false;
                if (action == "selection.odd")
                {
                    // Notes on the off (even-numbered) steps, the ones swing moves.
                    const double step = n->beat / .25;
                    pick = std::abs(step - std::round(step)) < 1.e-6 && (long long)(std::llround(step)) % 2 == 1;
                }
                else if (action == "selection.muted")
                    pick = n->muted;
                else
                    for (const auto *other : mine)
                        if (other != n && other->pitch < n->pitch + (action == "selection.stacked" ? 1 : 0) &&
                            (action == "selection.stacked"
                                 ? other->pitch == n->pitch && std::abs(other->beat - n->beat) < 1.e-9 &&
                                       std::abs(other->length - n->length) < 1.e-9 && other->id < n->id
                                 : other->beat < n->beat + n->length - 1.e-9 &&
                                       n->beat < other->beat + other->length - 1.e-9))
                            pick = true; // All but the lowest (or first) of each overlap.
                if (pick)
                    chosenNotes.push_back(n->id);
            }
        selectItems(false, chosenNotes);
        status = juce::String(int(chosenNotes.size())) + " note(s) selected";
        repaint();
        return true;
    }
    if (action == "piano.resizeLeft")
    {
        pianoResizeLeft = !pianoResizeLeft;
        status = pianoResizeLeft ? "Resizing notes from the left: on" : "Resizing notes from the left: off";
        repaint();
        return true;
    }
    if (action == "selection.timePrevious" || action == "selection.timeNext")
    {
        auto *piano = static_cast<Editor *>(windows[1]->getContentComponent());
        const auto range = piano->timeSelection();
        if (!range)
        {
            status = "Select time on the Piano roll ruler first (Ctrl+drag)";
            repaint();
            return false;
        }
        const double width = range->second - range->first;
        const double start = action.endsWith("Next") ? range->first + width : std::max(0., range->first - width);
        piano->setTimeSelection(start, start + width);
        return true;
    }
    if (action == "selection.timeAround")
    {
        const auto *part = controller.project.pattern(selectedPattern);
        double start = 1.e18, end = -1.e18;
        if (part)
            for (const auto &n : part->notes)
                if (selected(selectedNotes, n.id))
                    start = std::min(start, n.beat), end = std::max(end, n.beat + n.length);
        if (!(end > start))
        {
            status = "Select notes first";
            repaint();
            return false;
        }
        static_cast<Editor *>(windows[1]->getContentComponent())->setTimeSelection(start, end);
        return true;
    }
    if (action == "playlist.selectMuted" || action == "playlist.selectOverlapping" || action == "playlist.selectStacked")
    {
        // Playlist menu > Select: muted clips, clips that overlap an earlier clip on their track, or
        // clips stacked exactly on another (same track, start and end). The first of each pile stays out.
        struct Row
        {
            Id id;
            int lane;
            double beat, end;
            bool muted;
        };
        std::vector<Row> rows;
        const auto &p = controller.project;
        for (const auto &c : p.placements)
            rows.push_back({c.id, c.lane, c.beat, c.beat + c.length, c.muted});
        for (const auto &c : p.audioPlacements)
            rows.push_back({c.id, c.lane, c.beat, p.audioEndBeat(c.beat, c.duration), c.muted});
        for (const auto &c : p.automation.placements)
            rows.push_back({Id(c.id), c.lane, c.beat, c.beat + c.length, c.muted});
        std::vector<Id> picked;
        for (const auto &row : rows)
        {
            bool pick = action == "playlist.selectMuted" && row.muted;
            if (action != "playlist.selectMuted")
                for (const auto &other : rows)
                {
                    if (other.id == row.id || other.lane != row.lane)
                        continue;
                    const bool same = std::abs(other.beat - row.beat) < 1.e-9 && std::abs(other.end - row.end) < 1.e-9;
                    const bool earlier = other.beat < row.beat - 1.e-9 || (std::abs(other.beat - row.beat) < 1.e-9 && other.id < row.id);
                    if (action == "playlist.selectStacked" ? same && other.id < row.id
                                                           : earlier && other.beat < row.end - 1.e-9 && row.beat < other.end - 1.e-9)
                        pick = true;
                }
            if (pick)
                picked.push_back(row.id);
        }
        focus = Scope::playlist;
        selectItems(true, picked);
        status = picked.empty() ? juce::String(action == "playlist.selectMuted"   ? "No muted clips"
                                               : action == "playlist.selectStacked" ? "No stacked clips"
                                                                                    : "No overlapping clips")
                                : "Selected " + juce::String(int(picked.size())) + " clips";
        repaint();
        return true;
    }
    if (action == "selection.invert")
    {
        const bool clips = focus == Scope::playlist;
        std::vector<Id> inverted;
        if (clips)
        {
            const auto &p = controller.project;
            for (const auto &c : p.placements)
                if (!selected(selectedClips, c.id))
                    inverted.push_back(c.id);
            for (const auto &c : p.audioPlacements)
                if (!selected(selectedClips, c.id))
                    inverted.push_back(c.id);
            for (const auto &c : p.automation.placements)
                if (!selected(selectedClips, c.id))
                    inverted.push_back(c.id);
        }
        else if (const auto *part = controller.project.pattern(selectedPattern))
            for (const auto &n : part->notes)
                if (n.channel == selectedChannel && !selected(selectedNotes, n.id))
                    inverted.push_back(n.id);
        selectItems(clips, inverted);
        repaint();
        return true;
    }
    if (action == "clip.copy" || action == "clip.cut")
    {
        const auto ids = effectiveClipSelection();
        auto board = std::make_shared<ClipClipboard>();
        board->origin = 1.e18;
        const auto &p = controller.project;
        for (const auto &c : p.placements)
            if (selected(ids, c.id))
                board->patterns.push_back(c), board->origin = std::min(board->origin, c.beat);
        for (const auto &c : p.audioPlacements)
            if (selected(ids, c.id))
                board->audio.push_back(c), board->origin = std::min(board->origin, c.beat);
        for (const auto &c : p.automation.placements)
            if (selected(ids, c.id))
                board->automation.push_back(c), board->origin = std::min(board->origin, c.beat);
        if (board->empty())
        {
            status = "Select clips to copy";
            repaint();
            return false;
        }
        const auto count = board->patterns.size() + board->audio.size() + board->automation.size();
        clipClipboard = board;
        if (action == "clip.cut")
        {
            a.ids = ids;
            command("clip.delete", a);
        }
        else
            status = juce::String(int(count)) + (count == 1 ? " clip copied" : " clips copied");
        repaint();
        return true;
    }
    if (action == "clip.paste")
    {
        if (!clipClipboard || clipClipboard->empty())
        {
            status = "Copy or cut clips first (Ctrl+C / Ctrl+X)";
            repaint();
            return false;
        }
        a.clipboard = clipClipboard;
        a.ids.clear();
        const auto playhead = std::max(0., audio.positionBeat());
        a.beat = snap > 0 ? std::floor(playhead / snap + 1.e-9) * snap : playhead;
        const auto firstNew = controller.project.nextId;
        command(action, a);
        if (status != "Ready")
            return false;
        std::vector<Id> pasted;
        for (const auto &c : controller.project.placements)
            if (c.id >= firstNew)
                pasted.push_back(c.id);
        for (const auto &c : controller.project.audioPlacements)
            if (c.id >= firstNew)
                pasted.push_back(c.id);
        for (const auto &c : controller.project.automation.placements)
            if (c.id >= firstNew)
                pasted.push_back(c.id);
        selectItems(true, pasted);
        return true;
    }
    if (action == "clip.quantize")
    {
        a.ids = effectiveClipSelection();
        if (a.ids.empty())
        {
            for (const auto &c : controller.project.placements)
                a.ids.push_back(c.id);
            for (const auto &c : controller.project.audioPlacements)
                a.ids.push_back(c.id);
            for (const auto &c : controller.project.automation.placements)
                a.ids.push_back(c.id);
        }
        a.length = snap > 0 ? snap : 1.;
        command(action, a);
        return status == "Ready";
    }
    if (action == "time.insert" || action == "time.insertSliced" || action == "time.delete")
    {
        if (!playlistRange || playlistRange->second - playlistRange->first < .0625)
        {
            status = "Select time on the ruler first (Ctrl+drag)";
            repaint();
            return false;
        }
        a.ids.clear();
        a.beat = playlistRange->first;
        a.length = playlistRange->second - playlistRange->first;
        a.enabled = action == "time.insertSliced";
        command(action == "time.delete" ? "time.delete" : "time.insert", a);
        return status == "Ready";
    }
    if (action == "notes.duplicate" || action == "clip.duplicate")
    {
        bool piano = action == "notes.duplicate";
        a.ids = piano ? selectedNotes : effectiveClipSelection();
        if (a.ids.empty())
        {
            if (piano)
            {
                if (auto *p = controller.project.pattern(selectedPattern))
                    for (auto &n : p->notes)
                        if (n.channel == selectedChannel)
                            a.ids.push_back(n.id);
            }
            else
            {
                for (auto &c : controller.project.placements)
                    a.ids.push_back(c.id);
                for (auto &c : controller.project.audioPlacements)
                    a.ids.push_back(c.id);
                for (auto &c : controller.project.automation.placements)
                    a.ids.push_back(c.id);
            }
        }
        double start = 1e9, end = 0;
        if (piano)
        {
            if (auto *p = controller.project.pattern(selectedPattern))
                for (auto &n : p->notes)
                    if (selected(a.ids, n.id))
                    {
                        start = std::min(start, n.beat);
                        end = std::max(end, n.beat + n.length);
                    }
        }
        else
            for (auto &c : controller.project.placements)
                if (selected(a.ids, c.id))
                {
                    start = std::min(start, c.beat);
                    end = std::max(end, c.beat + c.length);
                }
        if (!piano)
            for (auto &c : controller.project.audioPlacements)
                if (selected(a.ids, c.id))
                {
                    start = std::min(start, c.beat);
                    end = std::max(end, controller.project.audioEndBeat(c.beat, c.duration));
                }
        if (!piano)
            for (auto &c : controller.project.automation.placements)
                if (selected(a.ids, c.id))
                {
                    start = std::min(start, c.beat);
                    end = std::max(end, c.beat + c.length);
                }
        if (end > 0)
        {
            double bar = controller.project.numerator * 4. / controller.project.denominator;
            if (piano)
                if (const auto *part = controller.project.pattern(selectedPattern))
                    bar = patternBarBeats(controller.project, *part);
            a.beat = std::ceil((end - start) / bar) * bar;
            if (piano)
            {
                auto interval = static_cast<Editor *>(windows[1]->getContentComponent())->repeatInterval();
                if (interval > 0)
                    a.beat = interval;
            }
            auto revision = controller.revision;
            const auto previousNextId = controller.project.nextId;
            command(action, a);
            if (controller.revision == revision)
                return true;
            auto &selection = piano ? selectedNotes : selectedClips;
            selection.clear();
            if (piano)
            {
                auto *p = controller.project.pattern(selectedPattern);
                for (auto i = p->notes.rbegin(); i != p->notes.rend() && selection.size() < a.ids.size(); ++i)
                    selection.push_back(i->id);
            }
            else
            {
                for (auto &clip : controller.project.placements)
                    if (clip.id >= previousNextId)
                        selection.push_back(clip.id);
                for (auto &clip : controller.project.audioPlacements)
                    if (clip.id >= previousNextId)
                        selection.push_back(clip.id);
                for (auto &clip : controller.project.automation.placements)
                    if (clip.id >= previousNextId)
                        selection.push_back(clip.id);
                std::sort(selection.begin(), selection.end(), std::greater<Id>());
            }
            refresh();
        }
        return true;
    }
    if (action == "notes.quantize")
    {
        openQuantizer();
        return true;
    }
    if (action == "notes.quickQuantize" || action == "notes.quantizeStarts")
    {
        a.length = snap > 0 ? snap : .25;
        command(action, a);
        return true;
    }
    if (action == "notes.mute" || action == "notes.unmute" || action == "notes.legato" ||
        action == "notes.chop" || action == "notes.glue" || action == "notes.group" || action == "notes.ungroup")
    {
        a.length = snap > 0 ? snap : .25;
        command(action, a);
        refresh();
        return true;
    }
    if (action == "notes.nudgeLeft" || action == "notes.nudgeRight" || action == "clip.nudgeLeft" ||
        action == "clip.nudgeRight")
    {
        a.beat = (action.endsWith("Left") ? -1 : 1) * (snap > 0 ? snap : .25);
        command(action.startsWith("notes.") ? "notes.move" : "clip.move", a);
        return true;
    }
    if (action == "clip.trackUp" || action == "clip.trackDown")
    {
        a.ids = effectiveClipSelection();
        if (a.ids.empty())
            return false;
        a.lane = action.endsWith("Up") ? -1 : 1;
        command("clip.move", a);
        return status == "Ready";
    }
    if (action == "marker.addAtPlayhead")
    {
        CommandArgs marker;
        marker.beat = std::max(0., audio.positionBeat());
        marker.text = "Marker";
        command("marker.add", marker);
        return status == "Ready";
    }
    if (action == "transport.home" || action == "transport.end")
    {
        double beat = 0;
        if (action == "transport.end" && !patternMode)
        {
            const auto &p = controller.project;
            for (const auto &c : p.placements)
                beat = std::max(beat, c.beat + c.length);
            for (const auto &c : p.audioPlacements)
                beat = std::max(beat, p.audioEndBeat(c.beat, c.duration));
            for (const auto &c : p.automation.placements)
                beat = std::max(beat, c.beat + c.length);
        }
        else if (action == "transport.end")
            if (const auto *p = controller.project.pattern(selectedPattern))
                beat = p->length;
        audio.seekBeat(beat);
        repaint();
        return true;
    }
    if (action == "notes.transposeUp" || action == "notes.transposeDown" || action == "notes.octaveUp" ||
        action == "notes.octaveDown")
    {
        a.pitch = (action.endsWith("Up") ? 1 : -1) * (action.contains("octave") ? 12 : 1);
        command("notes.move", a);
        return true;
    }
    if (action == "pattern.add")
    {
        newPattern.onClick();
        return true;
    }
    if (action == "pattern.clone")
    {
        clonePattern.onClick();
        return true;
    }
    if (action == "pattern.burnChannelLoop")
    {
        command(action, a);
        return status == "Ready";
    }
    if (action == "clip.unique")
    {
        if (!selectedClips.empty())
        {
            a.target = selectedClips.front();
            command(action, a);
        }
        return true;
    }
    if (action == "channel.clone" || action == "channel.delete")
    {
        juce::String operation;
        const auto result = beginChannelEdit(action, selectedChannels, operation);
        if (result.failed())
            status = result.getErrorMessage();
        repaint();
        return result.wasOk();
    }
    if (action == "channel.moveUp" || action == "channel.moveDown")
    {
        a.ids = selectedChannels;
        command(action, a);
        return status == "Ready";
    }
    if (action == "rack.soloRestore")
    {
        const auto saved = setSoloRestorePreference(!customization.restoreMuteAfterSolo);
        status = saved.wasOk() ? "Solo restore preference saved" : saved.getErrorMessage();
        repaint();
        return saved.wasOk();
    }
    if (action == "channel.solo" || action == "channel.unsolo" || action == "rack.muteSelected" ||
        action == "rack.unmuteSelected")
    {
        a.ids = selectedChannels;
        a.enabled = !channelIsSoloed(controller.project, selectedChannel);
        command(action, a);
        return true;
    }
    if (action == "channel.mute")
    {
        if (auto *c = controller.project.channel(selectedChannel))
        {
            a.enabled = !c->muted;
            command(action, a);
        }
        return true;
    }
    if (action == "channel.muteLock" || action == "rack.muteLock")
    {
        if (const auto *channel = controller.project.channel(selectedChannel))
        {
            a.enabled = !channel->muteLocked;
            if (action == "rack.muteLock")
                a.ids = selectedChannels;
            command(action, a);
        }
        return true;
    }
    if (action == "channel.add" || action == "channel.source")
    {
        chooser = std::make_unique<juce::FileChooser>("Choose sample", juce::File{}, "*.wav;*.aif;*.flac");
        auto safe = juce::Component::SafePointer<Studio>(this);
        bool replace = action == "channel.source";
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [safe, replace](const juce::FileChooser &fc)
                             {
                                 if (safe && fc.getResult().existsAsFile())
                                     safe->loadSample(fc.getResult().getFullPathName(), replace);
                             });
        return true;
    }
    if (action == "notes.add")
    {
        a.beat = std::floor(audio.positionBeat() * 4) / 4.;
        a.pitch = 60;
        a.length = .5;
        a.value = .8;
        static_cast<Editor *>(windows[1]->getContentComponent())->fillNoteInsertion(a);
        command(action, a);
        return true;
    }
    if (action.startsWith("steps.fill"))
    {
        a.ids = selectedChannels;
        a.interval = 4;
        command(action, a);
        return true;
    }
    if (action == "steps.set")
    {
        a.beat = std::floor(audio.positionBeat() * 4) / 4.;
        a.enabled = true;
        if (auto *p = controller.project.pattern(selectedPattern))
        {
            for (auto &n : p->notes)
                if (n.channel == selectedChannel && n.step && !n.muted && std::abs(n.beat - a.beat) < .00001)
                    a.enabled = false;
        }
        command(action, a);
        return true;
    }
    if (action == "clip.add")
    {
        a.beat = std::floor(audio.positionBeat());
        a.length = controller.project.pattern(selectedPattern)->length;
        command(action, a);
        return true;
    }
    if (action == "channel.paste")
    {
        a.target = clipboardPattern;
        a.value = double(clipboardChannel);
        command(action, a);
        return true;
    }
    if (action == "channel.sampler.mono" || action == "channel.sampler.portamento" ||
        action == "channel.sampler.fullPortamento")
    {
        if (const auto *channel = controller.project.channel(selectedChannel))
        {
            const auto &settings = channel->sampler;
            a.enabled = !(action == "channel.sampler.mono"         ? settings.mono
                          : action == "channel.sampler.portamento" ? settings.portamento
                                                                   : settings.fullPortamento);
            command(action, a);
            return status == "Ready";
        }
        return false;
    }
    if (action == "channel.sampler.normalize")
    {
        if (const auto *channel = controller.project.channel(selectedChannel))
        {
            a.enabled = !channel->sampler.normalize;
            command(action, a);
            return status == "Ready";
        }
        return false;
    }
    if (action == "channel.sampler.cutItself")
    {
        command(action, a);
        return status == "Ready";
    }
    juce::String prompt;
    double initial = 0;
    if (action == "channel.sampler.slideTime" || action == "channel.sampler.gateBeats")
    {
        const auto *channel = controller.project.channel(selectedChannel);
        if (!channel || channel->instrument || channel->audioClip)
        {
            status = "samplerVoice.unsupportedTarget: Existing local sampler Channel required";
            return false;
        }
        prompt = action.endsWith("slideTime") ? "Slide time (seconds0..10)"
                                              : "Gate (quarter-note beats0..64;0 Off)";
        initial = action.endsWith("slideTime") ? channel->sampler.slideTime : channel->sampler.gateBeats;
    }
    if (action == "project.tempo")
    {
        prompt = "Tempo (BPM)";
        initial = controller.project.tempo;
    }
    if (action == "channel.gain")
    {
        prompt = "Channel volume (linear gain)";
        if (auto *c = controller.project.channel(selectedChannel))
            initial = c->gain;
    }
    if (action == "mixer.gain")
    {
        prompt = "Mixer volume (linear gain)";
        a.mixer = selectedInsert;
        for (auto &m : controller.project.mixer)
            if (m.index == selectedInsert)
                initial = m.gain;
    }
    if (action == "mixer.pan")
    {
        prompt = "Mixer pan (-1 left / +1 right)";
        a.mixer = selectedInsert;
        for (const auto &insert : controller.project.mixer)
            if (insert.index == selectedInsert)
                initial = insert.pan;
    }
    if (action == "pattern.length")
    {
        prompt = "Pattern length (beats)";
        initial = controller.project.pattern(selectedPattern)->length;
    }
    if (action == "pattern.channelLoop")
    {
        channelLoopLengthDialog(selectedChannel, selectedPattern);
        return true;
    }
    if (action == "pattern.meter")
    {
        patternMeterDialog(selectedPattern);
        return true;
    }
    if (action == "pattern.extent")
    {
        a.text = "auto";
        command(action, a);
        return true;
    }
    if (action == "notes.stretch")
    {
        a.ids = selectedNotes;
        NoteStretchPlan source;
        if (auto r = planNoteStretch(controller.project, a.pattern, a.channel, a.ids, 0, 1, source);
            r.failed())
        {
            status = r.getErrorMessage();
            return false;
        }
        a.beat = 1000000;
        for (const auto &note : source.notes)
            a.beat = std::min(a.beat, note.beat);
        prompt = "Stretch selected notes: ratio (2=double; 0.5=half)";
        initial = 2;
    }
    if (action == "notes.velocity")
    {
        prompt = "Velocity (0-1)";
        initial = .8;
    }
    if (action == "notes.move")
        prompt = "Transpose semitones";
    if (action == "clip.move")
        prompt = "Move clips by beats";
    if (prompt.isNotEmpty())
    {
        auto *dialog = new juce::AlertWindow(prompt, "", juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("amount", juce::String(initial), prompt);
        dialog->addButton("Apply", 1);
        dialog->addButton("Cancel", 0);
        auto safe = juce::Component::SafePointer<Studio>(this);
        auto safeDialog = juce::Component::SafePointer<juce::AlertWindow>(dialog);
        dialog->enterModalState(
            true,
            juce::ModalCallbackFunction::create(
                [safe, safeDialog, action, a, expectedProject = controller.projectIdentity,
                 expectedRevision = controller.revision](int result) mutable
                {
                    if (!safe || !safeDialog || result != 1)
                        return;
                    if (isSamplerVoiceModeCommand(action))
                    {
                        if (safe->controller.projectIdentity != expectedProject ||
                            safe->controller.revision != expectedRevision)
                        {
                            safe->status = "samplerVoice.staleDialog: Source changed; no edit applied";
                            return;
                        }
                        const auto parsed =
                            juce::JSON::parse("[" + safeDialog->getTextEditorContents("amount").trim() + "]");
                        if (!parsed.isArray() || parsed.size() != 1 ||
                            !(parsed[0].isInt() || parsed[0].isInt64() || parsed[0].isDouble()))
                        {
                            safe->status = "samplerVoice.invalidValue: One finite physical number required";
                            return;
                        }
                        a.value = double(parsed[0]);
                        safe->command(action, a);
                        return;
                    }
                    if (action == "notes.stretch")
                    {
                        if (safe->controller.projectIdentity != expectedProject ||
                            safe->controller.revision != expectedRevision)
                        {
                            safe->status = "Stretch source changed; no edit applied";
                            return;
                        }
                        if (auto r =
                                parseMacroStretchRatio(safeDialog->getTextEditorContents("amount"), a.value);
                            r.failed())
                        {
                            safe->status = r.getErrorMessage();
                            return;
                        }
                        safe->command(action, a);
                        return;
                    }
                    double value = safeDialog->getTextEditorContents("amount").getDoubleValue();
                    if (action == "pattern.length")
                        a.length = value;
                    else if (action == "notes.move")
                        a.pitch = int(value);
                    else if (action == "clip.move")
                        a.beat = value;
                    else
                        a.value = value;
                    safe->command(action, a);
                }),
            true);
        return true;
    }
    status = "This action needs parameters; create a macro in Settings";
    repaint();
    return true;
}
void Studio::guideMenu(bool count)
{
    juce::PopupMenu menu;
    if (count)
    {
        menu.addItem(1, "Off", true, audio.countInBars() == 0);
        menu.addItem(2, "1 bar", true, audio.countInBars() == 1);
        menu.addItem(3, "2 bars", true, audio.countInBars() == 2);
    }
    else
    {
        menu.addItem(1, "Original Click", true, audio.guideSound() == 0);
        menu.addItem(2, "Original Wood", true, audio.guideSound() == 1);
        menu.addItem(3, "Original Beep", true, audio.guideSound() == 2);
    }
    menu.addSeparator();
    menu.addItem(4, "Transport settings...");
    auto safe = juce::Component::SafePointer<Studio>(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(count ? &countIn : &click),
                       [safe, count](int choice)
                       {
                           if (!safe || !choice)
                               return;
                           if (choice == 4)
                           {
                               safe->audioSettings();
                               if (safe->settingsWindow)
                                   if (auto *tabs = dynamic_cast<juce::TabbedComponent *>(
                                           safe->settingsWindow->getContentComponent()))
                                       tabs->setCurrentTabIndex(5);
                           }
                           else if (count)
                               safe->audio.setCountInBars(choice - 1);
                           else
                               safe->audio.setGuideSound(choice - 1);
                           safe->refresh();
                       });
}
bool Studio::hasPendingMidiTake() const noexcept
{
    return pendingMidiTake && !midiTaking;
}
std::uint64_t Studio::midiTakeSerial() const noexcept
{
    return pendingMidiTake ? pendingMidiTake->serial : lastMidiTakeSerial;
}
void Studio::stopMidiCaptureForRecovery()
{
    if (!midiTaking || !pendingMidiTake)
        return;
    auto &take = *pendingMidiTake;
    // Refuse reentry first, then disarm and quiesce BOTH existing producers.
    // A valid in-flight event must publish before the cutoff clock is frozen.
    take.phase = PendingMidiTake::Phase::sealing;
    midiTaking = false;
    recordArmed = false;
    midiTakeUiPending = true;
    status = retainedMidiTakeStatus; // Already owned; JUCE String copy assignment is noexcept.
    audio.midiInput.stopCapture();
    audio.typingInput.stopCapture();
    // Freeze ONCE after quiescence, before caller transport Stop/seek, never at
    // retry time. Failed quiescence is an explicit invalid capture below.
    MidiClock::Position clock;
    const bool hasClock = audio.midiInput.recordingPosition(clock);
    const double elapsed =
        hasClock && clock.playing
            ? juce::jlimit(0., clock.block, juce::Time::getMillisecondCounterHiRes() / 1000. - clock.wall)
            : 0;
    take.stopSeconds = hasClock ? clock.seconds + elapsed : 0;
    audio.cancelCountIn();
    if (!hasClock || audio.midiInput.capture.failed.load() || audio.typingInput.capture.failed.load())
        midiTake.markInputFailure();
}
void Studio::beginMidiTake(double leadSeconds)
{
    if (devicePreparationBusy()) { status = devicePreparationPendingStatus; recordArmed = false; return; }
    if (routingPending()) { status = routingPendingStatus; recordArmed = false; return; }
    if (pendingMidiTake)
    {
        status = "A MIDI take is retained; Stop/Record retries, Escape cancels";
        recordArmed = false;
        midiTakeUiPending = true;
        return;
    }
    auto *pattern = controller.project.pattern(selectedPattern);
    MidiClock::Position clock;
    if (!pattern || controller.hasGesture() || audio.rendering() || afterPluginState || pendingExport ||
        midiFileBusy || !audio.midiInput.recordingPosition(clock) || clock.wall <= 0 ||
        nextMidiTakeSerial >= 9007199254740991ULL)
    {
        status = "Recording not started: finish active work and wait for the audio clock";
        recordArmed = false;
        midiTakeUiPending = true;
        return;
    }
    try
    {
        auto prepared = std::make_unique<PendingMidiTake>();
        const double songPosition = audio.positionBeat(); // Before any count-in moves the transport.
        prepared->identity = controller.projectIdentity;
        prepared->serial = nextMidiTakeSerial + 1;
        prepared->args.pattern = selectedPattern;
        prepared->args.enabled = !patternMode;
        prepared->args.beat = audio.positionBeat();
        double beat = patternMode ? prepared->args.beat : 0;
        double sourceOffset = 0;
        if (!patternMode)
            for (const auto &clip : controller.project.placements)
                if (clip.pattern == selectedPattern && clip.beat <= prepared->args.beat + .0001 &&
                    clip.beat + clip.length > prepared->args.beat)
                {
                    prepared->args.target = clip.id;
                    prepared->placementBeat = clip.beat;
                    prepared->placementOffset = clip.offset;
                    sourceOffset = clip.offset;
                    beat = prepared->args.beat - clip.beat + clip.offset;
                    prepared->args.beat = clip.beat;
                    break;
                }
        const double elapsed =
            clock.playing
                ? juce::jlimit(0., clock.block, juce::Time::getMillisecondCounterHiRes() / 1000. - clock.wall)
                : 0;
        // Reserve and prepare before capture.begin clears/replaces any ownership.
        auto &capture = audio.midiInput.capture;
        midiTake.begin(capture.session.load() + 1, clock.seconds + elapsed + leadSeconds, beat,
                       controller.project.tempo, patternMode ? pattern->length : 0,
                       quantizeRecordingStart ? snap : 0, quantizeRecordingEnd ? snap : 0, quantizeKeepDuration);
        if (!patternMode && !controller.project.tempoChanges.empty())
            midiTake.useTempoMap(controller.project.songTempo(), songPosition);
        takePattern = selectedPattern;
        takePatternMode = patternMode;
        takePlacement = prepared->args.target;
        takeSongBeat = prepared->args.beat;
        takeSourceOffset = sourceOffset;
        nextMidiTakeSerial = prepared->serial;
        pendingMidiTake = std::move(prepared);
        capture.begin();
        audio.typingInput.capture.begin();
        midiTaking = true;
        midiPreview.clear();
        midiTakeUiPending = true;
        status = "Recording MIDI notes into " + pattern->name + " | Stop to keep take; Esc cancels";
    }
    catch (...)
    {
        // If display formatting failed AFTER capture publication, the take is
        // still owned/capturing; never clear it or pretend recording was rejected.
        if (!midiTaking)
        {
            recordArmed = false;
            midiTakeUiPending = true;
        }
    }
}
void Studio::drainMidiTake()
{
    if (!pendingMidiTake)
        return;
    auto admitted = [](MidiTake::EventResult result)
    { return result == MidiTake::EventResult::applied || result == MidiTake::EventResult::ignored; };
    if (midiTake.hasPendingEvent() && !admitted(midiTake.retryPendingEvent()))
    {
        stopMidiCaptureForRecovery();
        return;
    }
    MidiCapture::Event event;
    for (int i = 0; i < int(audio.midiInput.capture.queue.size()) && audio.midiInput.capture.pop(event); ++i)
        if (!admitted(midiTake.acceptCapturedEvent(event)))
        {
            stopMidiCaptureForRecovery();
            return;
        }
    for (int i = 0; i < int(audio.typingInput.capture.queue.size()) && audio.typingInput.capture.pop(event);
         ++i)
    {
        event.session = audio.midiInput.capture.session.load();
        event.source = 1;
        if (!admitted(midiTake.acceptCapturedEvent(event)))
        {
            stopMidiCaptureForRecovery();
            return;
        }
    }
    if (midiTaking)
    {
        MidiClock::Position clock;
        if (audio.midiInput.recordingPosition(clock))
        {
            const double elapsed =
                clock.playing ? juce::jlimit(0., clock.block,
                                             juce::Time::getMillisecondCounterHiRes() / 1000. - clock.wall)
                              : 0;
            try
            {
                midiPreview = midiTake.preview(clock.seconds + elapsed);
            }
            catch (...)
            {
            } // Display-only copy failed; canonical builder still owns every note/event.
        }
    }
}
juce::Result Studio::finishMidiTake(bool cancel)
{
    if (automationRecordingActive())
        return automationRecordingAdmission();
    if (cancel)
        return cancelPendingMidiTake();
    if (!pendingMidiTake)
        return juce::Result::ok();
    // Quiesce first even if a separate edit refuses: no capture may keep running
    // after the user's Stop, and the immutable take cutoff remains recoverable.
    stopMidiCaptureForRecovery();
    if (auto result = finishRackEditsChecked(); result.failed())
        return result;
    return retryMidiTake();
}
juce::Result Studio::retryMidiTake(std::uint64_t expectedSerial)
{
    if (!pendingMidiTake || (expectedSerial && expectedSerial != pendingMidiTake->serial))
        return juce::Result::fail("take.stale: No matching retained take");
    auto &take = *pendingMidiTake;
    if (midiTaking || take.phase == PendingMidiTake::Phase::applying)
        return juce::Result::fail("take.busy: Recording or canonical admission is active");
    // Failure code is intrinsic, set before constructing a fallible message.
    auto refused = [&](PendingMidiTake::Error code, const char *message)
    {
        take.error = code;
        return juce::Result::fail(message);
    };
    if (take.identity != controller.projectIdentity)
        return refused(PendingMidiTake::Error::sourceChanged,
                       "take.source_changed: Original project session required");
    if (!controller.project.pattern(take.args.pattern))
        return refused(PendingMidiTake::Error::targetMissing,
                       "take.target_missing: Original Pattern no longer exists");
    if (take.args.enabled && take.args.target)
    {
        const auto &placements = controller.project.placements;
        const auto found = std::find_if(placements.begin(), placements.end(),
                                        [&](const auto &clip) { return clip.id == take.args.target; });
        if (found == placements.end() || found->pattern != take.args.pattern ||
            found->beat != take.placementBeat || found->offset != take.placementOffset)
            return refused(PendingMidiTake::Error::placementChanged,
                           "take.placement_changed: Original Song clip/position required; no replacement "
                           "clip was created");
    }
    if (controller.hasGesture() || customization.running || afterPluginState || pendingExport ||
        audio.rendering() || midiFileBusy)
        return refused(PendingMidiTake::Error::busy,
                       "take.busy: Finish active edit or file operation before retry");
    try
    {
        if (take.phase == PendingMidiTake::Phase::sealing)
        {
            drainMidiTake();
            const auto sealed = midiTake.seal(take.stopSeconds);
            if (sealed == MidiTake::SealResult::invalid)
            {
                take.phase = PendingMidiTake::Phase::invalidCapture;
                return refused(PendingMidiTake::Error::captureInvalid,
                               "take.invalid_capture: Capture failure; retained diagnostic notes require "
                               "explicit cancel");
            }
            if (sealed != MidiTake::SealResult::ready)
                return refused(
                    PendingMidiTake::Error::preparation,
                    "take.preparation_failed: Original notes/event retained; retry after recovery");
            take.args.recordedNotes = midiTake.releaseFinishedNotes();
            take.args.recordedControllers = midiTake.releaseFinishedControllers();
            take.phase = PendingMidiTake::Phase::ready;
        }
        if (take.phase == PendingMidiTake::Phase::invalidCapture)
            return refused(PendingMidiTake::Error::captureInvalid,
                           "take.invalid_capture: Explicit cancel required");
        for (const auto &note : take.args.recordedNotes)
            if (!controller.project.channel(note.channel))
                return refused(PendingMidiTake::Error::targetMissing,
                               "take.target_missing: A recorded Channel no longer exists");
        if (!take.args.recordedNotes.empty() || !take.args.recordedControllers.empty())
        {
            take.phase = PendingMidiTake::Phase::applying;
            if (recordToSteps && !take.stepsApplied)
            {
                const auto *target = controller.project.pattern(take.args.pattern);
                recordedNotesToSteps(take.args.recordedNotes, takePatternMode && target ? target->length : 0);
                take.stepsApplied = true;
            }
            take.args.replaceRecorded = midiRecordReplace && !recordToSteps;
            const auto result = controller.execute("notes.record", take.args); // NEVER move out.
            if (result.failed())
            {
                take.phase = PendingMidiTake::Phase::ready;
                take.error = PendingMidiTake::Error::commandRejected;
                return result;
            }
        }
        // Actual canonical success first; observer errors cannot turn this into a
        // retryable take. Owner retirement is control work after publication.
        lastMidiTakeSerial = take.serial;
        lastMidiTakeCompleted = true;
        lastMidiTakeRevision = controller.revision;
        lastMidiTakeNoteCount = int(take.args.recordedNotes.size());
        pendingMidiTake.reset();
        midiTake.cancel();
        midiPreview.clear();
        midiTakeUiPending = true;
        status = lastMidiTakeNoteCount ? committedMidiTakeStatus : emptyMidiTakeStatus;
        return juce::Result::ok();
    }
    catch (...)
    {
        if (take.phase == PendingMidiTake::Phase::applying)
            take.phase = PendingMidiTake::Phase::ready;
        return refused(PendingMidiTake::Error::preparation,
                       "take.preparation_failed: Take remains owned; Stop/Record retries, Escape cancels");
    }
}
juce::Result Studio::cancelPendingMidiTake(std::uint64_t expectedSerial)
{
    if (!pendingMidiTake)
        return expectedSerial ? juce::Result::fail("take.stale: No matching retained take")
                              : juce::Result::ok();
    if (expectedSerial && expectedSerial != pendingMidiTake->serial)
        return juce::Result::fail("take.stale: A newer take owns the buffer");
    if (pendingMidiTake->phase == PendingMidiTake::Phase::applying)
        return juce::Result::fail("take.busy: Canonical admission is active");
    stopMidiCaptureForRecovery();
    lastMidiTakeSerial = pendingMidiTake->serial;
    lastMidiTakeCompleted = false;
    lastMidiTakeRevision = controller.revision;
    lastMidiTakeNoteCount = 0;
    midiTake.cancel();
    pendingMidiTake.reset();
    midiPreview.clear();
    recordArmed = false;
    midiTaking = false;
    midiTakeUiPending = true;
    status = cancelledMidiTakeStatus; // Preowned display text, no allocation.
    // No status string formatting or UI enqueue here: cancellation remains usable
    // with the next C++ allocation forced to fail. Timer handles the button paint.
    return juce::Result::ok();
}
juce::var Studio::midiTakeState() const
{
    auto state = std::make_unique<juce::DynamicObject>();
    state->setProperty("serial", juce::int64(midiTakeSerial()));
    state->setProperty("pending", hasPendingMidiTake());
    state->setProperty("capturing", midiTaking);
    state->setProperty("uiUpdatePending", midiTakeUiPending);
    if (!pendingMidiTake)
    {
        state->setProperty("phase",
                           lastMidiTakeSerial ? (lastMidiTakeCompleted ? "completed" : "cancelled") : "idle");
        state->setProperty("committedRevision", juce::int64(lastMidiTakeRevision));
        state->setProperty("noteCount", lastMidiTakeNoteCount);
        state->setProperty("canRetry", false);
        state->setProperty("canCancel", false);
        return state.release();
    }
    const auto &take = *pendingMidiTake;
    state->setProperty("phase", midiTaking                                       ? "capturing"
                                : take.phase == PendingMidiTake::Phase::sealing  ? "sealing"
                                : take.phase == PendingMidiTake::Phase::ready    ? "ready"
                                : take.phase == PendingMidiTake::Phase::applying ? "applying"
                                                                                 : "invalidCapture");
    state->setProperty("projectIdentity", take.identity);
    state->setProperty("pattern", juce::int64(take.args.pattern));
    state->setProperty("placement", juce::int64(take.args.target));
    state->setProperty("song", take.args.enabled);
    state->setProperty("songBeat", take.args.beat);
    state->setProperty("noteCount", int((take.phase == PendingMidiTake::Phase::ready ||
                                         take.phase == PendingMidiTake::Phase::applying)
                                            ? take.args.recordedNotes.size()
                                            : midiTake.size()));
    state->setProperty("heldCount", int(midiTake.heldCount()));
    state->setProperty("pendingInput", midiTake.hasPendingEvent());
    const char *failureCode = "none";
    switch (take.error)
    {
    case PendingMidiTake::Error::none:
        break;
    case PendingMidiTake::Error::preparation:
        failureCode = "take.preparation_failed";
        break;
    case PendingMidiTake::Error::captureInvalid:
        failureCode = "take.invalid_capture";
        break;
    case PendingMidiTake::Error::sourceChanged:
        failureCode = "take.source_changed";
        break;
    case PendingMidiTake::Error::targetMissing:
        failureCode = "take.target_missing";
        break;
    case PendingMidiTake::Error::placementChanged:
        failureCode = "take.placement_changed";
        break;
    case PendingMidiTake::Error::busy:
        failureCode = "take.busy";
        break;
    case PendingMidiTake::Error::commandRejected:
        failureCode = "take.command_rejected";
        break;
    }
    state->setProperty("failureCode", failureCode);
    state->setProperty("captureFailure", int(midiTake.failureCode()));
    bool validTarget = controller.project.pattern(take.args.pattern) != nullptr;
    for (const auto &note : take.args.recordedNotes)
        validTarget = validTarget && controller.project.channel(note.channel) != nullptr;
    if (take.args.enabled && take.args.target)
        validTarget =
            validTarget &&
            std::any_of(controller.project.placements.begin(), controller.project.placements.end(),
                        [&](const auto &clip)
                        {
                            return clip.id == take.args.target && clip.pattern == take.args.pattern &&
                                   clip.beat == take.placementBeat && clip.offset == take.placementOffset;
                        });
    state->setProperty("canRetry", !midiTaking && take.phase != PendingMidiTake::Phase::invalidCapture &&
                                       take.phase != PendingMidiTake::Phase::applying &&
                                       midiTake.failureCode() == MidiTake::Failure::none &&
                                       take.identity == controller.projectIdentity && validTarget &&
                                       !controller.hasGesture() && !customization.running &&
                                       !afterPluginState && !pendingExport && !audio.rendering() &&
                                       !midiFileBusy);
    state->setProperty("canCancel", take.phase != PendingMidiTake::Phase::applying);
    return state.release();
}

void Studio::timerCallback()
{
    pollWaitForInput();
    pollAiMusic();
    recordDiagnosticStatus();
    serviceAutosave();
    pollUpdates();
    if (!mixerChainApplying() || mixerChainRecovering())
        serviceDevicePreparation();
    routingCoordinator.poll({patternMode, selectedPattern});
    if (status == routingPendingStatus && !routingPending())
    {
        const auto* job = routingCoordinator.find(lastRoutingOperation);
        status = job && job->phase == ChannelRoutingCoordinator::Phase::completed ? routingReadyStatus
                 : job && job->phase == ChannelRoutingCoordinator::Phase::cancelled
                     ? "Effect change cancelled"
                 : job && job->detail.isNotEmpty() ? job->detail
                                                   : routingInterruptedStatus;
    }
    // Observers are post-commit control work; retry failed requests without
    // replaying the already committed model/history mutation.
    controller.deliverPendingNotifications();
    pollNoteSlice();
    pollNoteStretch();
    pollStepCompose();
    pollMcpGesture();
    midiController.poll();
    serviceAutomationRecording();
    syncSamplerControllerBindings();
    midiController.drain();
    serviceAutomationRecording();
    midiController.feedback(audio.playing(), recordArmed, click.getToggleState());
    pollAudioImports();
    drainScoreLog();
    auto *focused = juce::Component::getCurrentlyFocusedComponent();
    const bool inDetached =
        focused &&
        std::any_of(detachedWindows.begin(), detachedWindows.end(), [focused](auto &window)
                    { return window && (focused == window.get() || window->isParentOf(focused)); });
    const bool inInstrument =
        focused &&
        std::any_of(
            instrumentWindows.begin(), instrumentWindows.end(), [&](const auto &entry)
            { return entry.second && (focused == entry.second.get() || entry.second->isParentOf(focused)); });
    if (!juce::Process::isForegroundProcess() || !focused ||
        (focused != this && !isParentOf(focused) && !inDetached && !inInstrument) ||
        dynamic_cast<juce::TextEditor *>(focused) || juce::Component::getCurrentlyModalComponent())
        releaseTypingNotes();
    else
        typingKeys.poll([](int key) { return juce::KeyPress::isKeyCurrentlyDown(key); });
    if (midiTaking)
    {
        drainMidiTake();
        if (!audio.playing() || audio.midiInput.capture.failed.load())
            finishMidiTake();
    }
    pollAudioTake();
    pollExportEncoding();
    pollStretchJob();
    mtsEsp.sync(controller.project);
    followTempoWarps();
    pollPerformance();
    pollMarkerActions();
    syncSongLoop();
    if (videoWindow)
        videoWindow->follow(controller.project.songSeconds(audio.positionBeat()), audio.playing());
    if (midiTakeUiPending)
    {
        try
        {
            record.setToggleState(recordArmed, juce::dontSendNotification);
            if (!midiTaking)
                midiPreview.clear();
            midiTakeUiPending = false;
        }
        catch (...)
        {
        } // Explicit pending flag remains true for later UI retry.
    }
    serviceMixerChainFiles();
    if (afterPluginState)
    {
        const auto deviceOperation = deviceCaptureCallbackOperation;
        const auto effectOperation = effectCaptureCallbackOperation;
        try
        {
            Project snapshot;
            auto result = juce::Result::ok();
            if (audio.pollPluginState(snapshot, result))
            {
                auto callback = std::move(afterPluginState);
                afterPluginState = {};
                callback(result, std::move(snapshot));
                if (deviceCaptureCallbackOperation == deviceOperation)
                    deviceCaptureCallbackOperation = 0;
                if (effectCaptureCallbackOperation == effectOperation)
                    effectCaptureCallbackOperation = 0;
            }
        }
        catch (...)
        {
            // Generic Save/export callbacks keep their existing semantics.
            // Only the exact installed device slot may hand native ownership
            // to this persistent job's already implemented draining phase.
            if (effectOperation && effectCaptureCallbackOperation == effectOperation &&
                routingCoordinator.awaitingEffectCapture(effectOperation))
            {
                const bool drained = !audio.pluginStateCapturePending();
                routingCoordinator.effectCaptureInterrupted(effectOperation, drained);
                if (drained)
                {
                    afterPluginState = {};
                    effectCaptureCallbackOperation = 0;
                }
            }
            else if (mixerChainCaptureException())
            { /* Owned callback remains installed until actual drain. */
            }
            else if (!deviceOperation || deviceCaptureCallbackOperation != deviceOperation ||
                !devicePreparation || devicePreparation->operation != deviceOperation ||
                !devicePreparationBusy() || !devicePreparation->captureAdmitted)
                throw;
            else
            {
            afterPluginState = {};
            deviceCaptureCallbackOperation = 0;
            auto& job = *devicePreparation;
            job.phase = DeferredDevicePreparation::Phase::draining;
            if (job.code.isEmpty())
            {
                job.code.swapWith(job.capturePollExceptionCode);
                job.message.swapWith(job.capturePollExceptionMessage);
            }
            // Do not reset AudioEngine's actual capture or manufacture metadata.
            // Native pending work is drained by serviceDevicePreparation; its
            // own catch keeps later allocation failures visible and pending.
            }
        }
    }
    const auto previewResult = audio.pollAudition();
    if (previewResult.failed())
        status = previewResult.getErrorMessage();
    audio.reclaimPluginHosts();
    lastPluginTweak(); // Observe even when Tools and all plugin inspectors are closed.
    audio.midiInput.poll();
    if (!mixerChainApplying() && audio.consumePluginChanges())
        controller.markExternalChange();
    if (audio.deviceRestorationRequired())
        needsRebuild = false; // Shared capture/restoration owner runs before any replacement graph.
    else if (audio.pluginDeviceConfigurationChanged() ||
             (audio.documentProjectionDeferred() &&
              audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice()))
        needsRebuild = true;
    if (pendingExport && exportCancelRequested)
    {
        // Cancelled while plugin state was captured: never start the render.
        pendingExport.reset();
        exportCancelRequested = false;
        exportQueue.clear();
        exportCurrent.reset();
        status = "Export cancelled";
        completeMcpOperation(mcpExportOperation, juce::Result::fail("export.cancelled: Export cancelled"));
        mcpExportOperation.clear();
    }
    if (pendingExport)
    {
        auto result = audio.startRender(*pendingExport, pendingExportFile, exportRate, exportBits, exportTail);
        if (result.wasOk())
        {
            renderWasActive = true;
            pendingExport.reset();
        }
        else if (!AudioEngine::preparationPending(result))
        {
            status = result.getErrorMessage();
            completeMcpOperation(mcpExportOperation, result);
            mcpExportOperation.clear();
            pendingExport.reset();
        }
    }
    // Capture/export can temporarily exclude device-free native preparation.
    // Keep the pending rebuild until those ACTUAL owners drain. In particular,
    // rendering()==false at worker.done still precedes finishRender's teardown.
    if (needsRebuild && !afterPluginState && !pendingExport && !renderWasActive && !mixerChainApplying() &&
        !audio.rendering() && !devicePreparationBusy() &&
        !(audio.documentProjectionDeferred() &&
          audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice()))
    {
        auto r = audio.rebuild(controller.project, patternMode, selectedPattern);
        if (r.failed())
        {
            projectionError = r.getErrorMessage();
            status = r.getErrorMessage();
        }
        else
        {
            if (projectionError.isNotEmpty() && status == projectionError &&
                AudioEngine::preparationPending(juce::Result::fail(projectionError)))
                status = "Ready";
            projectionError.clear();
            engineRevision = controller.revision;
        }
        needsRebuild = r.failed() && (r.getErrorMessage() == "Audio edit queue busy; retrying" ||
                                      AudioEngine::preparationPending(r));
    }
    if (pluginRecovery && !needsRebuild)
    {
        const auto recovery = *pluginRecovery;
        pluginRecovery.reset();
        if (recovery.project == controller.projectIdentity && controller.project.plugin(recovery.id))
        {
            const auto host = audio.parameterHost(recovery.id);
            if (projectionError.isEmpty() && host && host->alive())
            {
                status = "Plugin recovered from last captured state; uncaptured changes may be lost";
                if (recovery.reopenControls)
                {
                    // Preserve an explicitly opened compatibility inspector's
                    // recovery ownership. Ordinary entry points use the wrapper.
                    if (parameterWindow &&
                        juce::int64(pluginControlSnapshot()["plugin"]) == recovery.id)
                        showPluginParameters(recovery.id);
                    else
                        openPluginControlsUI(recovery.id);
                }
            }
            else if (projectionError.isEmpty())
                status = audio.pluginStatus(recovery.id);
        }
    }
    if (pendingInstrument)
    {
        if (!pendingInstrument->matches(controller.projectIdentity, controller.project))
            pendingInstrument.reset();
        else if (audio.pluginProjectionReady(controller.project, pendingInstrument->plugin))
            openInstrument(pendingInstrument->channel);
    }
    pollPluginOperations();
    servicePluginWrapperRequests();
    audio.setMidiTarget(selectedChannel);
    click.setToggleState(audio.metronome(), juce::dontSendNotification);
    countIn.setToggleState(audio.countInBars() > 0, juce::dontSendNotification);
    for (int w : {1, 2})
        if (windows[w])
            if (auto *editor = dynamic_cast<Editor *>(windows[w]->getContentComponent()))
                editor->followPlayhead();
    serviceEffectSpare();
    serviceMidiClipboard();
    play.setButtonText(audio.playing() ? "Pause" : "Play");
    play.setToggleState(audio.playing(), juce::dontSendNotification);
    loopButton.setToggleState(audio.looping(), juce::dontSendNotification);
    loopButton.setEnabled(!midiTaking && !needsRebuild);
    double remaining = audio.countInRemaining();
    if (midiTaking && remaining > 0)
        status =
            "Count-in: " +
            juce::String(int(std::ceil(
                remaining * playbackMeter(controller.project, patternMode, selectedPattern).second / 4.))) +
            " | MIDI input monitored, recording starts at the cursor";
    else if (midiTaking)
        status = "Recording notes into " + controller.project.pattern(takePattern)->name +
                 " | Stop keeps take; Esc cancels";
    deviceLabel.setText(audio.deviceDescription() + "    " + (patternMode ? "Pattern" : "Song") +
                            (remaining > 0 ? " | Count-in " + juce::String(remaining, 1)
                             : timeDisplaySeconds
                                 ? " | " + juce::String(int(positionSeconds()) / 60) + ":" +
                                       juce::String(std::fmod(positionSeconds(), 60.), 2).paddedLeft('0', 5)
                                 : " | Beat " + juce::String(audio.positionBeat() + 1, 1)) +
                            (audio.cpuLoad() > 0 ? " | CPU " + juce::String(juce::roundToInt(audio.cpuLoad() * 100)) + "%" : juce::String()) +
                            (processMemoryMb() > 0 ? " | RAM " + juce::String(juce::roundToInt(processMemoryMb())) + " MB" : juce::String()) +
                            (audio.playing() && PolyphonyMeter::instance().voices() > 0
                                 ? " | Voices " + juce::String(PolyphonyMeter::instance().voices())
                                 : juce::String()) +
                            (detectedChord().isNotEmpty() ? " | Chord: " + detectedChord() : juce::String()),
                        juce::dontSendNotification);
    if (audio.rendering())
    {
        status = "Rendering WAV... " + juce::String(int(audio.renderProgress() * 100)) + "%";
        renderWasActive = true;
    }
    else if (renderWasActive && exportCurrent)
    {
        renderWasActive = false;
        auto r = audio.finishRender();
        if (std::exchange(exportCancelRequested, false))
            r = juce::Result::fail("export.cancelled: Export cancelled; previous output preserved");
        finishExportStep(r); // Encodes, queues the next stem or completes.
    }
    else if (renderWasActive)
    {
        auto r = audio.finishRender();
        if (std::exchange(exportCancelRequested, false))
            r = juce::Result::fail("export.cancelled: Export cancelled; previous output preserved");
        status = r.wasOk() ? "WAV exported" : r.getErrorMessage();
        completeMcpOperation(mcpExportOperation, r);
        mcpExportOperation.clear();
        renderWasActive = false;
        completeRenderClip(r);
    }
    for (auto &w : windows)
        if (w && w->isVisible())
            w->getContentComponent()->repaint();
    repaint();
}
void Studio::completeRenderClip(const juce::Result &r)
{
    {
        if (auto request = std::exchange(renderClip, std::nullopt))
        {
            if (r.wasOk() && request->project == controller.projectIdentity && request->file.existsAsFile())
            {
                importAudioClip(request->file, request->beat, request->lane); // Async import, own Undo step.
                for (const double beat : request->alsoAt)
                    importAudioClip(request->file, beat, request->lane);
                if (!request->muteAfter.empty())
                {
                    CommandArgs mute;
                    mute.ids = request->muteAfter;
                    mute.enabled = true;
                    mute.grouping = false;
                    command("clip.mute", mute);
                    status = "Rendered " + request->file.getFileName() + "; original clips muted";
                }
            }
            else if (r.wasOk())
                status = "Rendered " + request->file.getFileName() + "; project changed, clip not placed";
        }
    }
    repaint();
}
void Studio::importAudioClip(const juce::File &file, double beat, int lane)
{
    if (devicePreparationBusy()) { status = devicePreparationPendingStatus; return; }
    if (routingPending()) { status = routingPendingStatus; return; }
    if (!file.hasFileExtension("wav;aif;aiff;flac") || !std::isfinite(beat) || beat < 0 || lane < 0 ||
        lane > 127)
    {
        status = "Choose WAV, AIFF or FLAC for a Playlist audio clip";
        repaint();
        return;
    }
    recentSources.record(file.getFullPathName());
    if (!audioImports.request({file, controller.projectIdentity, beat, 0, lane}))
        status = "Audio import queue full or unavailable; try again";
    else
        status = "Reading audio clip metadata...";
    repaint();
}
void Studio::pollAudioImports()
{
    if (mixerChainApplying())
        return;
    if (devicePreparationBusy() || routingPending() || pollingAudioImports || hasPendingMidiTake() ||
        audioImportPhase == DeferredAudioImportPhase::applying)
        return;
    // Covers take-finishing observers AND committed UI refresh, not only the
    // command itself. Every return/exception restores the sole control owner.
    const juce::ScopedValueSetter<bool> pollingOwner(pollingAudioImports, true);
    for (int i = 0; i < 4; ++i)
    {
        // Construct the owning slot BEFORE removing a completed job. Never move
        // it local or reset it before any fallible take admission/UI work.
        if (!audioImportAfterTake)
        {
            audioImportAfterTake.emplace();
            if (!audioImports.poll(*audioImportAfterTake))
            {
                audioImportAfterTake.reset();
                break;
            }
            audioImportPhase = DeferredAudioImportPhase::pending;
        }
        auto &job = *audioImportAfterTake;
        if (job.identity != controller.projectIdentity)
        {
            // Explicit source-session cancellation, never apply to another song.
            audioImportAfterTake.reset();
            audioImportPhase = DeferredAudioImportPhase::pending;
            continue;
        }
        if (job.result.failed())
        {
            status = job.result.getErrorMessage();
            audioImportAfterTake.reset();
            audioImportPhase = DeferredAudioImportPhase::pending;
            continue; // Truthful terminal provider failure, no document mutation.
        }
        try
        {
            if (audioImportPhase != DeferredAudioImportPhase::committed)
            {
                if (midiTaking)
                    if (auto result = finishMidiTake(); result.failed())
                    {
                        status = result.getErrorMessage();
                        return;
                    } // Job remains owned.
                CommandArgs args;
                args.text = job.file.getFullPathName();
                args.beat = job.beat;
                args.lane = job.lane;
                args.length = job.duration;
                const auto firstNewId = controller.project.nextId;
                audioImportPhase = DeferredAudioImportPhase::applying;
                const auto result = controller.execute("audio.add", args);
                if (result.failed())
                {
                    audioImportPhase = DeferredAudioImportPhase::pending;
                    status = result.getErrorMessage();
                    audioImportAfterTake.reset(); // Truthful terminal command failure.
                    return;
                }
                // Intrinsic terminal outcome BEFORE selection allocation/status/
                // refresh. A UI exception must NEVER replay audio.add next tick.
                audioImportPhase = DeferredAudioImportPhase::committed;
                audioImportClip = 0;
                audioImportChannel = 0;
                for (const auto &clip : controller.project.audioPlacements)
                    if (clip.id >= firstNewId && clip.beat == job.beat && clip.lane == job.lane)
                    {
                        audioImportClip = clip.id;
                        audioImportChannel = clip.channel;
                        break;
                    }
            }
            if (audioImportClip && controller.project.channel(audioImportChannel))
            {
                selectedClips = {audioImportClip};
                selectedChannel = audioImportChannel;
                focus = Scope::playlist;
            }
            status = committedAudioImportStatus; // Preowned/no-format display text.
            refresh();
            audioImportAfterTake.reset();
            audioImportPhase = DeferredAudioImportPhase::pending;
        }
        catch (...)
        {
            if (audioImportPhase == DeferredAudioImportPhase::applying)
                audioImportPhase = DeferredAudioImportPhase::pending; // Prepared Controller: no commit.
            if (midiTaking)
                stopMidiCaptureForRecovery();
            // Keep BOTH job and phase. Committed retries only UI publication;
            // a refused/throwing take waits for explicit take retry/cancel.
            status = retainedAudioImportStatus;
            return;
        }
    }
}

void Studio::loadSample(const juce::String &source, bool replace, Id channel)
{
    if (source.startsWith("plugin:"))
    {
        if (const auto description = instrumentByIdentifier(source.fromFirstOccurrenceOf(":", false, false)))
            addInstrument(*description, replace ? (channel ? channel : selectedChannel) : 0);
        else
            status = "That instrument is no longer installed; rescan plugins";
        return;
    }
    CommandArgs a;
    a.channel = channel ? channel : selectedChannel;
    a.text = source;
    const auto revisionBefore = controller.revision;
    command(replace ? "channel.source" : "channel.add", a);
    if (controller.revision != revisionBefore)
        recentSources.record(source); // Learn the producer's favourite sounds.
    if (!replace && status == "Ready")
        selectedChannel = controller.project.channels.back().id;
    refresh();
}
void Studio::withPluginState(std::function<void(juce::Result, Project)> callback, const std::vector<Id> *only)
{
    if (mixerChainApplying() && !mixerChainCaptureDispatch && !devicePreparationDispatch)
    {
        callback(juce::Result::fail("effectChain.busy: Chain Apply owns native capture"), {});
        return;
    }
    if (devicePreparationBusy() && !devicePreparationDispatch)
    { callback(juce::Result::fail("device.busy: Finish or cancel native preparation first"), {}); return; }
    if (routingPending() && !effectCaptureDispatch)
    { callback(juce::Result::fail("routing.pending: Wait for audio selection before file/state capture"), {}); return; }
    if (midiTaking || hasPendingMidiTake())
    {
        callback(juce::Result::fail("take.pending: Finish/retry/cancel the MIDI take before state capture"),
                 {});
        return;
    }
    if (auto result = finishRackEditsChecked(); result.failed())
    {
        callback(result, {});
        return;
    }
    if (afterPluginState || pendingExport || audio.rendering())
    {
        callback(juce::Result::fail("Another save/export operation is still running"), {});
        return;
    }
    if (controller.hasGesture())
    {
        callback(juce::Result::fail("history.pendingGesture: Finish or cancel the edit before state capture"),
                 {});
        return;
    }
    // Only the owned stopped effect transaction opts into exact acknowledged
    // unchanged-state reuse. File Save/export retain fresh native capture.
    auto result = audio.requestPluginState(controller.project, only, effectCaptureDispatch);
    if (result.failed())
    {
        status = result.getErrorMessage();
        callback(result, {});
        return;
    }
    afterPluginState = std::move(callback);
    effectCaptureCallbackOperation = effectCaptureDispatch ? lastRoutingDispatchOperation : 0;
    // Stamp only the physically installed slot. A refused or not-yet-installed
    // capture never grants a device job ownership of another file callback.
    deviceCaptureCallbackOperation = devicePreparationDispatch && devicePreparation &&
        devicePreparationBusy() && devicePreparation->captureAdmitted &&
        devicePreparation->phase == DeferredDevicePreparation::Phase::capturing
        ? devicePreparation->operation : 0;
    status = "Capturing plugin state...";
}
void Studio::saveTo(const juce::File &file, std::function<void(juce::Result)> completion)
{
    if (noteSlicePending())
        finishNoteSlice(false, "Saving superseded the cut-line preview");
    if (noteStretchPending())
        finishNoteStretch(false, "Local context or file operation changed");
    if (auto result = finishRackEditsChecked(); result.failed())
    {
        if (completion)
            completion(result);
        return;
    }
    if (auto result = cancelRemotePreview(controller); result.failed())
    {
        status = result.getErrorMessage();
        if (completion)
            completion(result);
        return;
    }
    if (auto result = finishMidiTake(); result.failed())
    {
        status = result.getErrorMessage();
        if (completion)
            completion(result);
        return;
    }
    auto safe = juce::Component::SafePointer<Studio>(this);
    auto revision = controller.revision;
    const auto identity = controller.projectIdentity;
    withPluginState(
        [safe, file, revision, identity, completion](juce::Result result, Project snapshot)
        {
            if (!safe)
                return;
            if (result.wasOk())
            {
                if (safe->autosaveEnabled)
                    safe->rotateBackups(file);
                result = saveProject(snapshot, file);
            }
            safe->status = result.wasOk() ? "Saved " + file.getFileName() : result.getErrorMessage();
            if (const auto missing = missingMedia(snapshot); result.wasOk() && !missing.empty())
                safe->status << " | " << int(missing.size())
                             << " sample(s) missing: Tools > Find missing samples...";
            if (result.wasOk() && safe->controller.projectIdentity == identity)
            {
                safe->projectFile = file;
                if (safe->controller.revision == revision)
                    safe->controller.dirty = false;
                safe->autosaveRevision = revision; // Saved work needs no autosave.
                safe->audio.markCapturedPluginStateSaved();
                for (auto &c : safe->controller.project.channels)
                    if (c.instrument)
                        for (auto &saved : snapshot.channels)
                            if (saved.instrument && saved.instrument->id == c.instrument->id &&
                                saved.instrument->descriptor == c.instrument->descriptor)
                                c.instrument->state = saved.instrument->state;
                for (auto &m : safe->controller.project.mixer)
                    for (auto &fx : m.effects)
                        for (auto &saved : snapshot.mixer)
                            for (auto &s : saved.effects)
                                if (s.id == fx.id && s.descriptor == fx.descriptor)
                                    fx.state = s.state;
            }
            if (completion)
                completion(result);
            safe->repaint();
        });
}
void Studio::chooseSave()
{
    if (projectFile != juce::File{})
    {
        saveTo(projectFile);
        return;
    }
    chooser = std::make_unique<juce::FileChooser>(
        "Save VibeRa project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("project.vibra"),
        "*.vibra");
    auto safe = juce::Component::SafePointer<Studio>(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::warnAboutOverwriting,
                         [safe](const juce::FileChooser &fc)
                         {
                             if (!safe || fc.getResult() == juce::File{})
                                 return;
                             safe->saveTo(fc.getResult().withFileExtension("vibra"));
                         });
}
void Studio::chooseOpen()
{
    if (controllerTestMode && bool(getProperties()["viberaNativeMixerMeterFixture"]))
    {
        status = "Native meter fixture permits only disposable fresh projects";
        return;
    }
    juce::String code;
    if (auto ready = refreshProjectTransitionState(code); ready.failed())
    { status = ready.getErrorMessage(); return; }
    const auto project = controller.projectIdentity;
    chooser = std::make_unique<juce::FileChooser>("Open VibeRa or FL Studio project", juce::File{}, "*.vibra;*.flp");
    auto safe = juce::Component::SafePointer<Studio>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe, project](const juce::FileChooser &fc)
        {
            if (!safe || !fc.getResult().existsAsFile()) return;
            if (safe->controller.projectIdentity != project)
            { safe->status = "stale_project: Project changed while choosing a file"; return; }
            const auto file = fc.getResult();
            // The destructive decision belongs AFTER the asynchronous chooser,
            // so native changes during browsing are freshly observed/promoted.
            safe->requestProjectTransition([safe, file](bool discard)
                { if (safe) (void)safe->openProjectFile(file, discard); });
        });
}

juce::Result Studio::selectRoutedFor(int insert, int kind)
{
    const auto *row = mixerInsert(controller.project, insert);
    if (!row)
        return juce::Result::fail("Select an existing Mixer track");
    if (kind == 0)
    {
        std::vector<Id> channels;
        for (const auto &channel : controller.project.channels)
            if (channel.mixer == insert)
                channels.push_back(channel.id);
        if (channels.empty())
            return juce::Result::fail("No Channel is routed to " + row->name);
        auto selected = selectRackChannels(channels);
        if (selected.wasOk())
            status = juce::String(int(channels.size())) + " Channel(s) routed to " + row->name + " selected";
        return selected;
    }
    std::vector<int> tracks;
    for (const auto &other : controller.project.mixer)
        if (kind == 1 ? std::any_of(other.routes.begin(), other.routes.end(),
                                    [&](const auto &route) { return route.target == insert; }) && other.index != insert
                      : std::any_of(row->routes.begin(), row->routes.end(),
                                    [&](const auto &route) { return route.target == other.index; }))
            tracks.push_back(other.index);
    if (tracks.empty())
        return juce::Result::fail(kind == 1 ? "No Mixer track is routed to " + row->name
                                            : row->name + " is not routed to another track");
    MixerSelection next;
    if (auto prepared = MixerSelection::prepare(controller.project, controller.projectIdentity, mixerSelection, tracks,
                                                "replace", tracks.front(), next);
        prepared.failed())
        return prepared;
    mixerSelection = next;
    status = juce::String(int(tracks.size())) + " Mixer track(s) selected";
    refresh();
    return juce::Result::ok();
}
void Studio::queueSettledSteps(std::vector<std::function<void()>> steps, juce::String finished)
{
    for (auto &step : steps)
        settledSteps.push_back(std::move(step));
    if (finished.isNotEmpty())
        settledFinished = finished;
    if (!settledRunning)
    {
        settledRunning = true;
        runSettledSteps(100);
    }
}
bool Studio::audioWorkPending()
{
    return mixerChainApplying() || mixerChainBusy() || afterPluginState || devicePreparationBusy() || needsRebuild ||
           routingPending() || !audio.deferredWorkerPreparationIdle() ||
           audio.projectionDiagnostics().pluginSourceState == "pending";
}
void Studio::runSettledSteps(int patience)
{
    auto safe = juce::Component::SafePointer<Studio>(this);
    if (audioWorkPending())
    {
        if (patience > 0)
            juce::Timer::callAfterDelay(100, [safe, patience] { if (safe) safe->runSettledSteps(patience - 1); });
        else
        {
            settledSteps.clear();
            settledRunning = false;
            status = "Stopped: the previous Mixer change did not settle";
            repaint();
        }
        return;
    }
    if (settledSteps.empty())
    {
        settledRunning = false;
        if (settledFinished.isNotEmpty() && (status == "Ready" || status.startsWith("Mixer structural")))
            status = settledFinished;
        settledFinished = {};
        repaint();
        return;
    }
    auto step = std::move(settledSteps.front());
    settledSteps.pop_front();
    step();
    juce::Timer::callAfterDelay(100, [safe] { if (safe) safe->runSettledSteps(100); });
}
juce::Result Studio::routeSelectedTo(int target, bool only)
{
    if (!mixerInsert(controller.project, target))
        return juce::Result::fail("Choose an existing Mixer track");
    std::vector<int> sources;
    for (int id : mixerSelection.ids)
        if (id != 0 && id != target && mixerInsert(controller.project, id))
            sources.push_back(id);
    if (sources.empty())
        return juce::Result::fail("Select the Mixer tracks to route first (Ctrl+click their names)");
    std::vector<std::function<void()>> steps;
    for (int source : sources)
        steps.push_back([this, source, target, only]
                        {
                            CommandArgs a;
                            a.mixer = source;
                            a.target = target;
                            a.enabled = true;
                            command(only ? "mixer.routeOnly" : "mixer.route", a);
                        });
    queueSettledSteps(std::move(steps), "Routed " + juce::String(int(sources.size())) + " track(s) to " +
                                            mixerInsert(controller.project, target)->name);
    return juce::Result::ok();
}
juce::Result Studio::createSubmix()
{
    std::vector<int> sources;
    for (int id : mixerSelection.ids)
        if (id != 0 && mixerInsert(controller.project, id))
            sources.push_back(id);
    if (sources.empty())
        return juce::Result::fail("Select the Mixer tracks to group into a submix first");
    std::set<int> before;
    for (const auto &row : controller.project.mixer)
        before.insert(row.index);
    queueSettledSteps({[this] { command("mixer.insert.create", {}); },
                       [this, before, sources]
                       {
                           int created = -1;
                           for (const auto &row : controller.project.mixer)
                               if (!before.contains(row.index))
                                   created = row.index;
                           if (created < 0)
                           {
                               status = "The submix track was not created";
                               return;
                           }
                           CommandArgs name;
                           name.mixer = created;
                           name.text = "Submix";
                           command("mixer.rename", name);
                           std::vector<std::function<void()>> routes;
                           for (int source : sources)
                               routes.push_back([this, source, created]
                                                {
                                                    CommandArgs a;
                                                    a.mixer = source;
                                                    a.target = created;
                                                    a.enabled = true;
                                                    command("mixer.routeOnly", a);
                                                });
                           queueSettledSteps(std::move(routes), "Submix created from " +
                                                                    juce::String(int(sources.size())) + " track(s)");
                       }},
                      {});
    return juce::Result::ok();
}
std::vector<int> Studio::playlistTrackLanes() const
{
    std::set<int> lanes;
    for (const auto &p : controller.project.placements)
        if (p.lane < 64) // Performance lanes (64+) are live launches, not tracks.
            lanes.insert(p.lane);
    for (const auto &p : controller.project.audioPlacements)
        lanes.insert(p.lane);
    return {lanes.begin(), lanes.end()};
}
juce::Result Studio::exportPlaylistTracks(const juce::File &folder, std::function<void(juce::Result)> done)
{
    std::vector<std::pair<juce::String, std::vector<int>>> sets;
    for (const int lane : playlistTrackLanes())
    {
        const auto *track = controller.project.track(lane);
        sets.push_back({track && track->name.isNotEmpty() ? track->name : "Track " + juce::String(lane + 1), {lane}});
    }
    if (sets.empty())
        return juce::Result::fail("No Playlist track has Pattern or Audio clips");
    return exportLaneSets(folder, std::move(sets), std::move(done));
}
juce::Result Studio::exportPlaylistTrackGroups(const juce::File &folder, std::function<void(juce::Result)> done)
{
    const auto used = playlistTrackLanes();
    std::vector<std::pair<juce::String, std::vector<int>>> sets;
    for (int lane = 0; lane < 64; ++lane)
    {
        const auto *head = controller.project.track(lane);
        if (head && head->groupedAbove)
            continue; // Not a group's first track.
        std::vector<int> members{lane};
        for (int next = lane + 1; next < 64; ++next)
            if (const auto *member = controller.project.track(next); member && member->groupedAbove)
                members.push_back(next);
            else
                break;
        const bool hasClips = std::any_of(members.begin(), members.end(), [&](int l)
                                          { return std::find(used.begin(), used.end(), l) != used.end(); });
        if (members.size() >= 2 && hasClips)
            sets.push_back({head && head->name.isNotEmpty() ? head->name : "Group " + juce::String(lane + 1), members});
    }
    if (sets.empty())
        return juce::Result::fail("No Playlist track group (two or more tracks with clips) to export");
    return exportLaneSets(folder, std::move(sets), std::move(done));
}
juce::Result Studio::exportLaneSets(const juce::File &folder, std::vector<std::pair<juce::String, std::vector<int>>> sets,
                                    std::function<void(juce::Result)> done)
{
    if (!folder.isDirectory() && !folder.createDirectory())
        return juce::Result::fail("Cannot create " + folder.getFullPathName());
    if (pendingExport || audio.rendering() || exportBusy())
        return juce::Result::fail("Finish the current export first");
    struct Run
    {
        std::vector<std::pair<juce::String, std::vector<int>>> sets;
        size_t next = 0;
        bool started = false;
        int patience = 0;
    };
    auto run = std::make_shared<Run>();
    run->sets = std::move(sets);
    auto safe = juce::Component::SafePointer<Studio>(this);
    auto step = std::make_shared<std::function<void()>>();
    *step = [safe, run, folder, done, weak = std::weak_ptr<std::function<void()>>(step)]
    {
        if (!safe)
            return;
        const bool busy = safe->pendingExport || safe->audio.rendering() || safe->exportBusy();
        if (run->started)
        {
            // Wait for this export to begin (bounded) and then to finish.
            if (!busy && run->patience-- > 0 && !safe->exportCurrent.has_value())
            {
                if (auto next = weak.lock())
                    juce::Timer::callAfterDelay(100, [next] { (*next)(); });
                return;
            }
            if (busy)
            {
                run->patience = 0;
                if (auto next = weak.lock())
                    juce::Timer::callAfterDelay(100, [next] { (*next)(); });
                return;
            }
            run->started = false;
        }
        if (run->next >= run->sets.size())
            return done(juce::Result::ok());
        const auto &[name, lanes] = run->sets[run->next++];
        const auto file = folder.getChildFile(juce::String(lanes.front() + 1).paddedLeft('0', 2) + " " +
                                              juce::File::createLegalFileName(name) + ".wav");
        safe->status = "Exporting " + juce::String(int(run->next)) + " of " + juce::String(int(run->sets.size()));
        run->started = true;
        run->patience = 30;
        safe->exportTo(file, 0, false, {}, lanes);
        if (auto next = weak.lock())
            juce::Timer::callAfterDelay(200, [next] { (*next)(); });
    };
    (*step)();
    pendingTrackExport = step;
    return juce::Result::ok();
}
juce::Result Studio::runNoteScript(const juce::String &source, const juce::String &name)
{
    CommandArgs a;
    a.pattern = selectedPattern;
    a.channel = selectedChannel;
    a.ids = selectedNotes;
    a.length = snap > 0 ? snap : .25;
    a.text = source;
    command("notes.script", a);
    if (status != "Ready")
        return juce::Result::fail(status);
    lastNoteScript = source;
    lastNoteScriptName = name;
    status = "Script: " + name;
    repaint();
    return juce::Result::ok();
}
void Studio::chooseNoteScriptFile()
{
    const auto folder = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("VibeRa Studio/Scripts");
    chooser = std::make_unique<juce::FileChooser>("Run Piano roll script", folder.isDirectory() ? folder : juce::File{}, "*.js");
    auto safe = juce::Component::SafePointer<Studio>(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safe](const juce::FileChooser &fc)
                         {
                             if (!safe)
                                 return;
                             const auto file = fc.getResult();
                             safe->chooser.reset();
                             if (!file.existsAsFile())
                                 return;
                             if (file.getSize() > 200000)
                             {
                                 safe->status = "Script files are limited to 200 KB";
                                 return safe->repaint();
                             }
                             if (auto ran = safe->runNoteScript(file.loadFileAsString(), file.getFileNameWithoutExtension());
                                 ran.failed())
                             {
                                 safe->status = ran.getErrorMessage();
                                 safe->repaint();
                             }
                         });
}
juce::Result Studio::createChannelAutomation(Id channel, bool pan)
{
    if (!controller.project.channel(channel))
        return juce::Result::fail("Choose an existing Channel");
    int lane = -1;
    double end = 4;
    for (const auto &p : controller.project.placements)
        lane = std::max(lane, p.lane), end = std::max(end, p.beat + p.length);
    for (const auto &p : controller.project.audioPlacements)
        lane = std::max(lane, p.lane);
    for (const auto &p : controller.project.automation.placements)
        lane = std::max(lane, p.lane);
    CommandArgs a;
    a.channel = channel;
    a.automationControl = pan ? MixerAutomationControl::channelPan : MixerAutomationControl::channelGain;
    a.beat = 0;
    a.length = end;
    a.lane = std::min(127, lane + 1);
    command("automation.create", a);
    if (status != "Ready")
        return juce::Result::fail(status);
    status = juce::String(pan ? "Pan" : "Volume") + " automation clip created on Playlist track " + juce::String(a.lane + 1);
    repaint();
    return juce::Result::ok();
}
std::vector<int> Studio::unusedMixerInserts() const
{
    std::vector<int> unused;
    for (const auto &row : controller.project.mixer)
    {
        if (row.index == 0 || !row.effects.empty())
            continue;
        auto candidate = controller.project;
        if (deleteMixerInsert(candidate, row.index, MixerInsertDeletePolicy{}).wasOk())
            unused.push_back(row.index);
    }
    return unused;
}
void Studio::trimUnusedInserts()
{
    const auto targets = unusedMixerInserts();
    if (targets.empty())
    {
        status = "No unused Mixer tracks to trim";
        repaint();
        return;
    }
    auto safe = juce::Component::SafePointer<Studio>(this);
    auto step = std::make_shared<std::function<void(int)>>();
    auto remaining = std::make_shared<std::vector<int>>(targets.rbegin(), targets.rend()); // Highest first.
    auto removed = std::make_shared<int>(0);
    *step = [safe, remaining, removed, weak = std::weak_ptr<std::function<void(int)>>(step)](int patience)
    {
        if (!safe)
            return;
        const bool busy = safe->audioWorkPending();
        if (busy && patience > 0)
        {
            if (auto next = weak.lock())
                juce::Timer::callAfterDelay(100, [next, patience] { (*next)(patience - 1); });
            return;
        }
        // Skip tracks that became used meanwhile; stop on a refusal.
        while (!remaining->empty())
        {
            const int insert = remaining->front();
            remaining->erase(remaining->begin());
            auto candidate = safe->controller.project;
            if (!mixerInsert(candidate, insert) ||
                deleteMixerInsert(candidate, insert, MixerInsertDeletePolicy{}).failed())
                continue;
            CommandArgs args;
            args.mixer = insert;
            safe->command("mixer.insert.delete", args);
            ++*removed;
            if (auto next = weak.lock())
                juce::Timer::callAfterDelay(100, [next] { (*next)(100); });
            return;
        }
        safe->status = "Trimmed " + juce::String(*removed) + " unused Mixer track(s)";
        safe->repaint();
    };
    pendingTrim = step;
    (*step)(100);
}
void Studio::addInsertFromChainPreset()
{
    // Familiar "Add from state": a new insert, then a chain preset loaded onto it.
    if (!mixerChains || !mixerChains->files)
    {
        status = "Choose the chain preset directory first";
        repaint();
        return;
    }
    std::set<int> before;
    for (const auto &row : controller.project.mixer)
        before.insert(row.index);
    command("mixer.insert.create", {});
    // Creating the insert is structural and commits asynchronously: wait (up to about 10 s)
    // for the new insert to exist and the audio work to settle, then open the chooser for it.
    auto safe = juce::Component::SafePointer<Studio>(this);
    auto attempt = std::make_shared<std::function<void(int)>>();
    *attempt = [safe, before, weak = std::weak_ptr<std::function<void(int)>>(attempt)](int left)
    {
        if (!safe)
            return;
        int created = -1;
        for (const auto &row : safe->controller.project.mixer)
            if (!before.contains(row.index))
                created = row.index;
        const bool busy = safe->audioWorkPending();
        if ((created < 0 || busy) && left > 0)
        {
            if (auto next = weak.lock())
                juce::Timer::callAfterDelay(100, [next, left] { (*next)(left - 1); });
            return;
        }
        if (created < 0)
        {
            safe->status = "The new insert was not created; nothing to load the chain onto";
            safe->repaint();
            return;
        }
        safe->selectedInsert = created;
        safe->chooseMixerChainFile("loadApply", created);
        safe->repaint();
    };
    pendingChainChooser = attempt;
    (*attempt)(100);
    repaint();
}
void Studio::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.;
    if (!tapTimes.empty() && now - tapTimes.back() > 2.)
        tapTimes.clear();
    tapTimes.push_back(now);
    if (tapTimes.size() > 9)
        tapTimes.erase(tapTimes.begin());
    if (tapTimes.size() < 2)
    {
        status = "Tap tempo: keep tapping";
        repaint();
        return;
    }
    const double bpm = 60. * double(tapTimes.size() - 1) / (tapTimes.back() - tapTimes.front());
    if (!tapGesture)
        tapGesture = controller.beginGesture();
    CommandArgs a;
    a.value = std::round(juce::jlimit(20., 400., bpm) * 100.) / 100.;
    command("project.tempo", a);
    if (status == "Ready")
        status = "Tap tempo: " + juce::String(a.value, 2) + " BPM";
    repaint();
    // Two seconds after the last tap the session ends (one Undo step).
    auto safe = juce::Component::SafePointer<Studio>(this);
    juce::Timer::callAfterDelay(2100, [safe, now]
                                {
                                    if (!safe || safe->tapTimes.empty() || safe->tapTimes.back() != now)
                                        return;
                                    safe->tapTimes.clear();
                                    if (std::exchange(safe->tapGesture, false))
                                        if (auto ended = safe->controller.endGestureChecked("Tap tempo"); ended.failed())
                                            safe->status = ended.getErrorMessage();
                                });
}

juce::Array<juce::File> Studio::projectTemplates() const
{
    auto files = templatesFolder().findChildFiles(juce::File::findFiles, false, "*.vibra");
    std::sort(files.begin(), files.end(), [](const juce::File &a, const juce::File &b)
              { return a.getFileName().compareNatural(b.getFileName()) < 0; });
    return files;
}

juce::Result Studio::templateFileName(const juce::String &name, juce::String &legal)
{
    legal = name.trim();
    if (legal.isEmpty() || legal.length() > 64 || juce::File::createLegalFileName(legal) != legal)
        return juce::Result::fail("template.invalidName: Use 1..64 characters without \\ / : * ? \" < > |");
    return juce::Result::ok();
}

juce::Result Studio::newFromTemplate(const juce::String &name, bool discardChanges)
{
    juce::String legal;
    if (auto valid = templateFileName(name, legal); valid.failed())
        return valid;
    const auto file = templatesFolder().getChildFile(legal + ".vibra");
    if (!file.existsAsFile())
        return juce::Result::fail("template.missing: No template named " + legal);
    auto result = openProjectFile(file, discardChanges);
    if (result.failed())
        return result;
    projectFile = juce::File{}; // A new, untitled project: Save asks for its own file.
    status = "New project from template " + legal;
    refresh();
    return result;
}

juce::Result Studio::saveAsTemplate(const juce::String &name, std::function<void(juce::Result)> completion)
{
    juce::String legal;
    if (auto valid = templateFileName(name, legal); valid.failed())
        return valid;
    if (auto result = finishRackEditsChecked(); result.failed())
        return result;
    const auto file = templatesFolder().getChildFile(legal + ".vibra");
    auto safe = juce::Component::SafePointer<Studio>(this);
    withPluginState(
        [safe, file, completion](juce::Result result, Project snapshot)
        {
            if (!safe)
                return;
            if (result.wasOk() && !file.getParentDirectory().createDirectory())
                result = juce::Result::fail("Could not create " + file.getParentDirectory().getFullPathName());
            if (result.wasOk())
                result = saveProject(snapshot, file);
            safe->status = result.wasOk() ? "Saved template " + file.getFileNameWithoutExtension()
                                          : result.getErrorMessage();
            safe->repaint();
            if (completion)
                completion(result);
        });
    return juce::Result::ok();
}

void Studio::promptSaveTemplate()
{
    auto *box = new juce::AlertWindow("Save as template", "Template name (File > New from template)",
                                      juce::MessageBoxIconType::NoIcon);
    box->addTextEditor("name", projectFile == juce::File{} ? juce::String("My template")
                                                           : projectFile.getFileNameWithoutExtension());
    box->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    auto safe = juce::Component::SafePointer<Studio>(this);
    box->enterModalState(true, juce::ModalCallbackFunction::create(
                                   [safe, box](int ok)
                                   {
                                       if (!safe || ok != 1)
                                           return;
                                       if (auto started = safe->saveAsTemplate(box->getTextEditorContents("name"));
                                           started.failed())
                                       {
                                           safe->status = started.getErrorMessage();
                                           safe->repaint();
                                       }
                                   }),
                         true);
}

juce::Result Studio::openProjectFile(const juce::File &file, bool discardChanges)
{
    if (devicePreparationBusy()) return juce::Result::fail("device.busy: Finish/cancel preparation first");
    if (routingPending()) return juce::Result::fail("routing.pending: Wait before project replacement");
    if (controllerTestMode && bool(getProperties()["viberaNativeMixerMeterFixture"]))
        return juce::Result::fail("Native meter fixture refuses user project loading");
    if (afterPluginState || pendingExport || audio.rendering())
        return juce::Result::fail("Finish the active save/export before opening a project");
    if (auto result = finishRackEditsChecked(); result.failed())
        return result;
    if (auto result = cancelRemotePreview(controller); result.failed())
    {
        status = result.getErrorMessage();
        return result;
    }
    if (auto take = finishMidiTake(); take.failed())
    {
        status = take.getErrorMessage();
        return take;
    }
    juce::String code;
    if (auto ready = admitProjectTransition(discardChanges, code); ready.failed())
    { status = ready.getErrorMessage(); return ready; }
    const auto identity = controller.projectIdentity;
    Project p;
    // FL Studio projects are converted into a new, unsaved VibeRa project.
    const bool flp = file.hasFileExtension("flp");
    FlpImportReport flpReport;
    auto result = flp ? importFlp(file, p, flpReport) : loadProject(file, p);
    if (result.failed())
    {
        status = result.getErrorMessage();
        repaint();
        return result;
    }
    if (controller.projectIdentity != identity)
        return juce::Result::fail("stale_project: Project changed while reading the replacement");
    if (auto ready = admitProjectTransition(discardChanges, code); ready.failed())
    { status = ready.getErrorMessage(); return ready; }
    std::unique_ptr<AudioEngine::PreparedDeferredRouting> deferredReplacement;
    Project deferredMetadata;
    if (audio.documentProjectionDeferred() && audio.canDeferChannelRouting())
    {
        deferredMetadata = p;
        if (auto ready = audio.prepareDeferredRouting(p, deferredReplacement); ready.failed())
            return ready;
    }
    else if (auto ready = restoreRoutingBeforeStructural(); ready.failed())
        return ready;
    instrumentWindows.clear();
    pendingInstrument.reset();
    audio.stop();
    audio.invalidateProjection();
    clearScoreLog();
    controller.replace(std::move(p), true);
    if (deferredReplacement)
        audio.retireDeferredProjectReplacement(*deferredReplacement, deferredMetadata);
    static_cast<Editor *>(windows[3]->getContentComponent())->resetMixerEffectView();
    projectFile = flp ? juce::File{} : file;
    selectedPattern = controller.project.patterns.front().id;
    if (!controller.project.channels.empty())
        selectedChannel = controller.project.channels.back().id;
    controller.deliverPendingNotifications();
    if (const auto missing = missingMedia(controller.project); !missing.empty())
        status = "Opened " + file.getFileName() + " | " + juce::String(int(missing.size())) +
                 " sample(s) missing: Tools > Find missing samples...";
    if (flp)
        status = flpReport.summary() + " Save it as a VibeRa project.";
    const bool nativeAccess =
        std::any_of(controller.project.channels.begin(), controller.project.channels.end(),
                    [](const auto &channel) { return channel.instrument.has_value(); }) ||
        std::any_of(controller.project.mixer.begin(), controller.project.mixer.end(),
                    [](const auto &insert) { return !insert.effects.empty(); });
    needsRebuild = !deferredReplacement || nativeAccess; // Prepare native access without allocating an Edit.
    if (deferredReplacement)
        engineRevision = controller.revision;
    refresh();
    return juce::Result::ok();
}
juce::Result Studio::alignClipToGrid(Id clip, double grid)
{
    const auto &project = controller.project;
    const auto found = std::find_if(project.audioPlacements.begin(), project.audioPlacements.end(),
                                    [&](const auto &c) { return c.id == clip; });
    if (found == project.audioPlacements.end())
        return juce::Result::fail("Choose an Audio Clip");
    if (!std::isfinite(grid) || grid < 1. / 64 || grid > 16)
        return juce::Result::fail("Grid is 1/64..16 beats");
    const auto *source = project.channel(found->channel);
    const auto transient =
        source ? firstTransientSeconds(juce::File(source->source), found->offset, found->duration) : std::nullopt;
    if (!transient)
        return juce::Result::fail("No transient found in this clip (silent or missing audio)");
    const double hit = project.songBeat(project.songSeconds(found->beat) + *transient);
    const double delta = std::round(hit / grid) * grid - hit;
    if (found->beat + delta < 0)
        return juce::Result::fail("Aligning would move the clip before the Song start");
    if (std::abs(delta) < 1.e-6)
    {
        status = "Already on the grid";
        return juce::Result::ok();
    }
    CommandArgs move;
    move.ids = {clip};
    move.beat = delta;
    command("clip.move", move);
    status = "Aligned the first transient to the grid (moved " + juce::String(delta, 3) + " beats)";
    return juce::Result::ok();
}
bool Studio::cancelExport()
{
    // Encoding (FLAC/OGG/MP3) is short and not interruptible; everything before it is.
    if (exportEncoding || !(audio.rendering() || pendingExport || afterPluginState || exportCurrent))
        return false;
    exportCancelRequested = true;
    exportQueue.clear();
    audio.cancelRender(); // No-op until the render starts; the timer drops a pending one.
    status = "Cancelling export...";
    return true;
}
juce::Result Studio::relinkMissingMedia(const juce::File &folder)
{
    const auto missing = missingMedia(controller.project);
    if (missing.empty())
    {
        status = "No samples are missing";
        return juce::Result::ok();
    }
    CommandArgs args;
    args.relinks = findMissingMedia(controller.project, folder);
    if (args.relinks.empty())
    {
        status = "None of the " + juce::String(int(missing.size())) + " missing samples are in " + folder.getFileName();
        return juce::Result::fail(status);
    }
    command("media.relink", args);
    status = "Found " + juce::String(int(args.relinks.size())) + " of " + juce::String(int(missing.size())) +
             " missing samples";
    needsRebuild = true;
    refresh();
    return juce::Result::ok();
}
void Studio::chooseExport()
{
    if (auto result = finishMidiTake(); result.failed())
    {
        status = result.getErrorMessage();
        return;
    }
    chooser = std::make_unique<juce::FileChooser>(
        "Export Song WAV - 48 kHz, 24-bit, cut tail",
        projectFile == juce::File{}
            ? juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("VibeRa.wav")
            : projectFile.withFileExtension("wav"),
        "*.wav");
    auto safe = juce::Component::SafePointer<Studio>(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::warnAboutOverwriting,
                         [safe](const juce::FileChooser &fc)
                         {
                             if (safe && fc.getResult() != juce::File{})
                             {
                                 safe->exportTo(fc.getResult().withFileExtension("wav"));
                             }
                         });
}
juce::Result Studio::newProject(bool demo, bool discardChanges)
{
    if (automationRecordingActive())
        return automationRecordingAdmission();
    if (devicePreparationBusy()) { status = devicePreparationPendingStatus; return juce::Result::fail(status); }
    if (routingPending()) { status = routingPendingStatus; return juce::Result::fail(status); }
    if (auto result = cancelRemotePreview(controller); result.failed())
    {
        status = result.getErrorMessage();
        return juce::Result::fail(status);
    }
    if (afterPluginState || pendingExport || audio.rendering())
    {
        status = "Finish the active save/export before creating a project";
        return juce::Result::fail(status);
    }
    if (auto result = finishMidiTake(); result.failed())
    {
        status = result.getErrorMessage();
        return juce::Result::fail(status);
    }
    juce::String code;
    if (auto ready = admitProjectTransition(discardChanges, code); ready.failed())
    { status = ready.getErrorMessage(); return ready; }
    // Construct the fallible replacement before stopping/invalidation of source.
    auto replacement = demo ? Project::demo() : Project::empty();
    if (auto ready = admitProjectTransition(discardChanges, code); ready.failed())
    { status = ready.getErrorMessage(); return ready; }
    std::unique_ptr<AudioEngine::PreparedDeferredRouting> deferredReplacement;
    Project deferredMetadata;
    if (audio.documentProjectionDeferred() && audio.canDeferChannelRouting())
    {
        deferredMetadata = replacement;
        if (auto ready = audio.prepareDeferredRouting(replacement, deferredReplacement); ready.failed())
        {
            status = ready.getErrorMessage();
            return ready;
        }
    }
    else if (auto ready = restoreRoutingBeforeStructural(); ready.failed())
    { status = ready.getErrorMessage(); return ready; }
    instrumentWindows.clear();
    pendingInstrument.reset();
    audio.stop();
    audio.invalidateProjection();
    clearScoreLog();
    controller.replace(std::move(replacement), true);
    if (deferredReplacement)
        audio.retireDeferredProjectReplacement(*deferredReplacement, deferredMetadata);
    static_cast<Editor *>(windows[3]->getContentComponent())->resetMixerEffectView();
    if (!demo)
    {
        patternMode = true;
        mode.setButtonText("Pattern");
    }
    selectedPattern = controller.project.patterns.front().id;
    selectedChannel = demo ? controller.project.channels.back().id : controller.project.channels.front().id;
    projectFile = juce::File{};
    controller.deliverPendingNotifications();
    needsRebuild = !deferredReplacement;
    if (deferredReplacement)
        engineRevision = controller.revision;
    refresh();
    return juce::Result::ok();
}
void Studio::exportTo(const juce::File &file, Id patternOnly, bool stems, std::vector<Id> clipsOnly,
                      std::vector<int> lanesOnly)
{
    if (!audio.rendering() && !pendingExport && !exportBusy())
        exportCancelRequested = false; // A cancel never carries over to a new export.
    if (noteSlicePending())
        finishNoteSlice(false, "Export superseded the cut-line preview");
    if (noteStretchPending())
        finishNoteStretch(false, "Local context or file operation changed");
    if (auto result = finishRackEditsChecked(); result.failed())
    {
        completeMcpOperation(mcpExportOperation, result);
        mcpExportOperation.clear();
        return;
    }
    if (auto result = cancelRemotePreview(controller); result.failed())
    {
        status = result.getErrorMessage();
        completeMcpOperation(mcpExportOperation, result);
        mcpExportOperation.clear();
        return;
    }
    if (auto result = finishMidiTake(); result.failed())
    {
        status = result.getErrorMessage();
        completeMcpOperation(mcpExportOperation, result);
        mcpExportOperation.clear();
        return;
    }
    auto safe = juce::Component::SafePointer<Studio>(this);
    withPluginState(
        [safe, file, patternOnly, stems, clipsOnly, lanesOnly](juce::Result result, Project snapshot)
        {
            if (!safe)
                return;
            if (result.wasOk() && !lanesOnly.empty())
            {
                // Chosen Playlist tracks from the Song start: other tracks' Pattern/Audio clips go.
                const auto keep = [&](int lane) { return std::find(lanesOnly.begin(), lanesOnly.end(), lane) != lanesOnly.end(); };
                std::erase_if(snapshot.placements, [&](const auto &p) { return !keep(p.lane); });
                std::erase_if(snapshot.audioPlacements, [&](const auto &p) { return !keep(p.lane); });
            }
            if (result.wasOk() && patternOnly != 0)
            {
                Project single;
                result = pattern_management::renderProject(snapshot, patternOnly, single);
                if (result.wasOk())
                    snapshot = std::move(single);
            }
            if (result.wasOk() && !clipsOnly.empty())
            {
                Project only;
                double start = 0;
                result = pattern_management::renderClipsProject(snapshot, clipsOnly, only, start);
                if (result.wasOk())
                    snapshot = std::move(only);
            }
            if (result.wasOk())
                result = safe->queueExport(snapshot, file, stems);
            if (result.failed())
            {
                safe->completeMcpOperation(safe->mcpExportOperation, result);
                safe->mcpExportOperation.clear();
            }
            safe->status = result.wasOk() ? "Rendering WAV" : result.getErrorMessage();
            safe->repaint();
        });
}
void Studio::audioSettings()
{
    if (mixerChainApplying())
    {
        status = "Finish or cancel chain Apply before device settings";
        return;
    }
    if (controllerTestMode && bool(getProperties()["viberaNativeMixerMeterFixture"]))
    {
        status = "Native meter fixture uses its owned synthetic device; reconfiguration is disabled";
        return;
    }
    if (settingsWindow)
    {
        bringWindowToFront(*settingsWindow);
        return;
    }
    juce::DialogWindow::LaunchOptions o;
    o.dialogTitle = "Settings";
    o.dialogBackgroundColour = panel;
    o.content.setOwned(new SettingsContent(*this));
    settingsWindow = o.launchAsync();
    if (settingsWindow)
        roundNativeWindow(*settingsWindow);
}
void Studio::showPlugins()
{
    audioSettings();
    if (settingsWindow)
        if (auto *tabs = dynamic_cast<SettingsContent *>(settingsWindow->getContentComponent()))
            tabs->setCurrentTabIndex(3);
}
bool Studio::selectSamplerModulationTarget(Id channel, int target)
{
    const auto *source = controller.project.channel(channel);
    if (!source || source->instrument || source->audioClip || target < 0 || target >= 5)
        return false;
    if (!instrumentWindows.contains(channel))
        openInstrument(channel);
    const auto window = instrumentWindows.find(channel);
    return window != instrumentWindows.end() && window->second &&
           window->second->selectModulationTarget(target);
}
void Studio::openInstrument(Id id)
{
    auto *channel = controller.project.channel(id);
    if (!channel)
        return;
    selectedChannel = id;
    audio.setMidiTarget(id);
    if (channel->instrument)
    {
        auto statusText = audio.pluginStatus(channel->instrument->id);
        if (!audio.pluginProjectionReady(controller.project, channel->instrument->id))
        {
            pendingInstrument = PendingInstrumentOpening{
                controller.projectIdentity, channel->instrument->descriptor, id, channel->instrument->id};
            status = "Preparing instrument editor...";
        }
        else if (statusText.startsWith("Isolated VST3"))
        {
            pendingInstrument.reset();
            openPluginEditorUI(channel->instrument->id);
        }
        else
        {
            status = statusText;
            pendingInstrument.reset();
        }
    }
    else
    {
        pendingInstrument.reset();
        auto &window = instrumentWindows[id];
        if (!window)
            window = std::make_unique<InstrumentWindow>(*this, id);
        window->setVisible(true);
        roundNativeWindow(*window);
        bringWindowToFront(*window);
    }
    refresh();
}
void Studio::pluginMenu(Id channelId)
{
    auto *channel = controller.project.channel(channelId);
    if (!channel)
        return;
    selectedChannel = channelId;
    juce::PopupMenu menu;
    menu.addItem(1, "Browse native / VST3 instruments...");
    if (channel->instrument)
    {
        menu.addSectionHeader(channel->instrument->name);
        menu.addItem(2, "Open native editor");
        menu.addItem(3, "Close editor");
        menu.addItem(4, "Bypass", true, channel->instrument->bypass);
        menu.addItem(5, "Remove instrument (restore sample)");
        menu.addItem(6, audio.pluginStatus(channel->instrument->id), false);
        menu.addItem(7, "Plugin controls...");
        int ioItem = 20000;
        addPluginIoMenu(menu, channel->instrument->id, ioItem);
        const auto host = audio.parameterHost(channel->instrument->id);
        menu.addItem(8, "Recover saved state", !host || !host->alive());
    }
    const auto sourceProject = controller.projectIdentity;
    const auto sourceHost = channel->instrument ? audio.parameterHost(channel->instrument->id) : nullptr;
    const auto sourceInstance = sourceHost ? sourceHost->runtimeIdentity() : juce::String{};
    auto safe = juce::Component::SafePointer<Studio>(this);
    menu.showMenuAsync({},
                       [safe, channelId, sourceProject, sourceInstance](int action)
                       {
                           if (!safe)
                               return;
                           if (safe->controller.projectIdentity != sourceProject)
                           {
                               safe->status = "Project changed; reopen the instrument menu";
                               return;
                           }
                           if (action == 1)
                           {
                               safe->showPlugins();
                               return;
                           }
                           auto *c = safe->controller.project.channel(channelId);
                           if (!c || !c->instrument)
                               return;
                           auto id = c->instrument->id;
                           if (action == 7)
                               safe->openPluginControlsUI(id);
                           else if (action == 8)
                               safe->recoverPlugin(id);
                           else if (action == 2 || action == 3)
                           {
                               const auto host = safe->audio.parameterHost(id);
                               if (!host || host->runtimeIdentity() != sourceInstance)
                               {
                                   safe->status = "Plugin runtime changed; reopen the instrument menu";
                                   return;
                               }
                               safe->openPluginEditorUI(id, action == 2);
                           }
                           else if (action == 4 || action == 5)
                           {
                               CommandArgs a;
                               a.target = id;
                               a.enabled = !c->instrument->bypass;
                               safe->command(action == 4 ? "plugin.bypass" : "plugin.remove", a);
                           }
                       });
}
void Studio::confirmMixerInsertDeletion(int index, std::optional<int> replacement)
{
    CommandArgs args;
    args.mixer = index;
    if (replacement)
    {
        MixerInsertDeletePolicy policy;
        policy.replacement = replacement;
        policy.removeEffects = policy.removeAutomation = policy.removeGroupMembership = true;
        args.mixerDelete = policy;
    }
    auto candidate = controller.project;
    if (auto valid =
            deleteMixerInsert(candidate, index, args.mixerDelete.value_or(MixerInsertDeletePolicy{}));
        valid.failed())
    {
        status = valid.getErrorMessage();
        repaint();
        return;
    }
    const auto *row = mixerInsert(controller.project, index);
    const auto refs = mixerDeletionReferences(controller.project, index);
    const auto identity = controller.projectIdentity;
    const auto revision = controller.revision;
    auto message =
        "Delete '" + row->name + "' (stable insert " + juce::String(index) + ") and its outgoing sends?";
    if (replacement)
    {
        const auto *target = mixerInsert(controller.project, *replacement);
        message += "\nReroute " + juce::String(refs["assignedChannels"].size()) + " assigned Channels and " +
                   juce::String(refs["incomingRoutes"].size()) + " incoming sends to '" + target->name +
                   "' (" + juce::String(*replacement) + "), preserving send levels.";
        message += "\nExplicitly remove " + juce::String(int(refs["effects"])) +
                   " captured effects, links from " + juce::String(refs["automationSources"].size()) +
                   " automation sources, " + juce::String(int(refs["initializedTargets"])) +
                   " initialized targets and " + juce::String(refs["groups"].size()) +
                   " display-group memberships. Other members/curves stay.";
    }
    message += "\nUndo restores the insert and its acknowledged native state. Native playback may briefly "
               "reconfigure.";
    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withTitle("Delete Mixer insert")
            .withMessage(message)
            .withButton("Delete")
            .withButton("Cancel")
            .withAssociatedComponent(this),
        [safe = juce::Component::SafePointer<Studio>(this), args, identity, revision](int choice)
        {
            if (!safe || choice != 1)
                return;
            if (safe->controller.projectIdentity != identity || safe->controller.revision != revision)
            {
                safe->status = "Project changed; reopen the insert deletion confirmation";
                return;
            }
            safe->command("mixer.insert.delete", args);
        });
}
juce::Result Studio::startEffectSpare(int insert, bool flip)
{
    if (effectSpareJob || mixerChainBusy())
        return juce::Result::fail("Effect chain work is active; wait for it to finish");
    const juce::File directory(mixerChains && mixerChains->files ? mixerChains->files->configuredDirectory() : juce::String{});
    if (directory == juce::File{} || !directory.isDirectory())
        return juce::Result::fail("Choose a chain preset directory first (insert menu > Chain preset directory...)");
    if (flip && !effectSpares.contains(insert))
        return juce::Result::fail("Store this insert's chain in spare state first");
    EffectSpareJob job;
    job.insert = insert;
    job.stage = flip ? 2 : 1;
    job.spare = directory.getChildFile("vibera-spare-insert-" + juce::String(insert) + ".vibfx");
    job.next = directory.getChildFile("vibera-spare-insert-" + juce::String(insert) + "-next.vibfx");
    auto args = chain_workflow_detail::object();
    chain_workflow_detail::put(args, "action", "save");
    chain_workflow_detail::put(args, "mixer", insert);
    chain_workflow_detail::put(args, "projectIdentity", controller.projectIdentity);
    chain_workflow_detail::put(args, "revision", juce::int64(controller.revision));
    chain_workflow_detail::put(args, "path", (flip ? job.next : job.spare).getFullPathName());
    chain_workflow_detail::put(args, "overwrite", true);
    const auto outcome = mixerChainOperation(args);
    job.operation = outcome["operation"].toString();
    const auto state = outcome["completionState"].toString();
    if (state == "failed" || job.operation.isEmpty())
        return juce::Result::fail(outcome["message"].toString());
    effectSpareJob = job;
    status = flip ? "Flipping effect chain with spare state..." : "Storing effect chain in spare state...";
    serviceEffectSpare();
    return juce::Result::ok();
}
void Studio::serviceEffectSpare()
{
    if (!effectSpareJob)
        return;
    auto &job = *effectSpareJob;
    auto query = chain_workflow_detail::object();
    chain_workflow_detail::put(query, "action", "job");
    chain_workflow_detail::put(query, "operation", job.operation);
    const auto outcome = mixerChainOperation(query);
    const auto state = outcome["completionState"].toString();
    if (state == "pending")
        return;
    if (state != "completed")
    {
        status = "Spare state: " + outcome["message"].toString();
        effectSpareJob.reset();
        repaint();
        return;
    }
    if (job.stage == 1)
    {
        effectSpares.insert(job.insert);
        status = "Effect chain stored in spare state";
        effectSpareJob.reset();
    }
    else if (job.stage == 2)
    {
        // The current chain is saved; now load and apply the spare (one Undo step).
        auto args = chain_workflow_detail::object();
        chain_workflow_detail::put(args, "action", "loadApply");
        chain_workflow_detail::put(args, "mixer", job.insert);
        chain_workflow_detail::put(args, "projectIdentity", controller.projectIdentity);
        chain_workflow_detail::put(args, "revision", juce::int64(controller.revision));
        chain_workflow_detail::put(args, "path", job.spare.getFullPathName());
        const auto applied = mixerChainOperation(args);
        job.operation = applied["operation"].toString();
        if (applied["completionState"].toString() == "failed" || job.operation.isEmpty())
        {
            status = "Spare state: " + applied["message"].toString();
            effectSpareJob.reset();
        }
        else
            job.stage = 3;
    }
    else
    {
        // The spare is applied; the saved previous chain becomes the new spare.
        job.spare.deleteFile();
        job.next.moveFileTo(job.spare);
        status = "Effect chain flipped with spare state";
        effectSpareJob.reset();
    }
    repaint();
}
juce::Result Studio::exportScoreSheet(const juce::File &file)
{
    const auto *part = controller.project.pattern(selectedPattern);
    const auto *channel = controller.project.channel(selectedChannel);
    if (!part || !channel)
        return juce::Result::fail("Select a Pattern and Channel");
    std::vector<score_sheet::NoteIn> notes;
    for (const auto &n : part->notes)
        if (n.channel == selectedChannel && !n.muted)
            notes.push_back({n.beat, n.length, n.pitch});
    if (notes.empty())
        return juce::Result::fail("This Channel has no notes in the Pattern");
    int pages = 0;
    const auto pdf = score_sheet::render(notes, patternNumerator(controller.project, *part),
                                         patternDenominator(controller.project, *part), controller.project.tempo,
                                         part->name + " - " + channel->name, pages);
    if (!file.replaceWithData(pdf.getData(), pdf.getSize()))
        return juce::Result::fail("Could not write " + file.getFullPathName());
    status = "Score sheet exported (" + juce::String(pages) + (pages == 1 ? " page)" : " pages)");
    return juce::Result::ok();
}
juce::Result Studio::insertControllerValue(int type, int number)
{
    const auto *part = controller.project.pattern(selectedPattern);
    const auto *channel = controller.project.channel(selectedChannel);
    if (!part || !channel || channel->audioClip)
        return juce::Result::fail("Select a Pattern and a playable Channel");
    if (type < 0 || type > 2 || number < 0 || number > 127)
        return juce::Result::fail("Controller lane is CC 0..127, pitch bend or pressure");
    const double span = part->length > 0 ? part->length : 4.;
    double beat = std::fmod(std::max(0., audio.positionBeat()), span);
    beat = std::floor(beat * 960. + .5) / 960.;
    double value = type == 1 ? double(channel->pitchBend) : 0.;
    double latest = -1;
    for (const auto &c : part->controllers)
        if (c.channel == channel->id && c.type == type && (type != 0 || c.number == number) && c.beat <= beat + 1.e-9 &&
            c.beat >= latest)
        {
            latest = c.beat;
            value = c.value;
        }
    CommandArgs a;
    a.pattern = part->id;
    a.channel = channel->id;
    a.text = type == 0 ? "cc" : type == 1 ? "pitchBend" : "pressure";
    a.pitch = number;
    a.beat = beat;
    a.value = value;
    command("notes.controller.add", a);
    return status == "Ready" ? juce::Result::ok() : juce::Result::fail(status);
}
void Studio::serviceMidiClipboard()
{
    if (midiClipboardOperation.isEmpty())
        return;
    for (const auto &job : mcpOperations)
        if (job.id == midiClipboardOperation && job.state != "pending")
        {
            const bool ok = job.state == "completed" && midiClipboardFile.existsAsFile() &&
                            midi_clipboard::putFile(midiClipboardFile.getFullPathName().toWideCharPointer());
            status = ok ? "Copied to the MIDI clipboard (paste the .mid into another app)"
                        : "MIDI clipboard copy failed: " + (job.message.isNotEmpty() ? job.message : juce::String("clipboard unavailable"));
            midiClipboardOperation.clear();
            repaint();
            return;
        }
}
void Studio::storeSpareState(Id channel)
{
    auto safe = juce::Component::SafePointer<Studio>(this);
    status = "Capturing spare state...";
    captureChannelPreset(channel,
                         [safe, channel](juce::Result result, juce::String xml)
                         {
                             if (!safe)
                                 return;
                             if (result.wasOk())
                                 safe->spareStates[channel] = xml;
                             safe->status = result.wasOk() ? "Stored in spare state" : result.getErrorMessage();
                             safe->repaint();
                         });
}
void Studio::flipSpareState(Id channel)
{
    const auto spare = spareStates.find(channel);
    if (spare == spareStates.end())
    {
        status = "Store a spare state first";
        repaint();
        return;
    }
    // Capture the current state, apply the spare (one Undo step), keep the captured one as spare.
    auto safe = juce::Component::SafePointer<Studio>(this);
    const auto wanted = spare->second;
    status = "Flipping with spare state...";
    captureChannelPreset(channel,
                         [safe, channel, wanted](juce::Result result, juce::String current)
                         {
                             if (!safe)
                                 return;
                             if (result.failed())
                             {
                                 safe->status = result.getErrorMessage();
                                 return;
                             }
                             const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                   .getChildFile("vibera-spare-" + juce::Uuid().toString() + ".vibch");
                             if (!file.replaceWithText(wanted))
                             {
                                 safe->status = "Spare state could not be staged";
                                 return;
                             }
                             const auto applied = safe->applyChannelPresetFile(channel, file);
                             file.deleteFile();
                             if (applied.wasOk())
                                 safe->spareStates[channel] = current;
                             safe->status = applied.wasOk() ? "Flipped with spare state" : applied.getErrorMessage();
                             safe->repaint();
                         });
}
void Studio::openMixerEq(int index)
{
    const auto *insert = mixerInsert(controller.project, index);
    if (!insert)
        return;
    auto editor = std::make_unique<MixerEqEditor>();
    auto safe = juce::Component::SafePointer<Studio>(this);
    const auto identity = controller.projectIdentity;
    editor->read = [safe, index]
    {
        const auto *row = safe ? mixerInsert(safe->controller.project, index) : nullptr;
        return row ? row->eq : MixerEqSettings{};
    };
    editor->apply = [safe, index, identity](int band, float gainDb, float hz)
    {
        if (!safe || safe->controller.projectIdentity != identity)
            return;
        CommandArgs a;
        a.mixer = index;
        a.target = Id(band);
        a.value = gainDb;
        a.offset = hz;
        safe->command("mixer.eq", a);
    };
    editor->beginGesture = [safe]
    {
        if (safe)
            safe->controller.beginGesture();
    };
    editor->endGesture = [safe]
    {
        if (safe)
            reportHistoryResult(*safe, safe->controller.endGestureChecked("Mixer EQ"));
    };
    editor->reset = [safe, index, identity]
    {
        if (!safe || safe->controller.projectIdentity != identity)
            return;
        CommandArgs a;
        a.mixer = index;
        safe->command("mixer.eqReset", a);
    };
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "EQ - " + (insert->name.isNotEmpty() ? insert->name : "Insert " + juce::String(index));
    options.content.setOwned(editor.release());
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.escapeKeyTriggersCloseButton = true;
    options.dialogBackgroundColour = panel;
    options.launchAsync();
}
void Studio::effectMenu(int index)
{
    auto insert = std::find_if(controller.project.mixer.begin(), controller.project.mixer.end(),
                               [&](auto &m) { return m.index == index; });
    if (insert == controller.project.mixer.end())
        return;
    selectedInsert = index;
    juce::PopupMenu menu;
    menu.addItem(1, "Browse / insert VST3 effect...");
    menu.addItem(10010, "EQ (3-band)...");
    menu.addItem(10011, "Track delay..." + (insert->delayMs > 0 ? " (" + juce::String(insert->delayMs, 1) + " ms)" : juce::String()));
    juce::PopupMenu selectMenu;
    selectMenu.addItem(10020, "Channels routed to this track    Alt+L");
    selectMenu.addItem(10021, "Tracks routed to this one");
    selectMenu.addItem(10022, "Tracks this one is routed to");
    menu.addSubMenu("Select", selectMenu);
    juce::PopupMenu routing;
    const bool others = std::any_of(mixerSelection.ids.begin(), mixerSelection.ids.end(),
                                    [index](int id) { return id != 0 && id != index; });
    routing.addItem(10025, "Route selected tracks to this track", others);
    routing.addItem(10026, "Route selected tracks to this track only", others);
    routing.addItem(10027, "Create submix from selected tracks...",
                    std::any_of(mixerSelection.ids.begin(), mixerSelection.ids.end(), [](int id) { return id != 0; }));
    menu.addSubMenu("Routing", routing);
    menu.addItem(10023, "Reset track (controls, name, colour; keeps effects)");
    juce::PopupMenu trackFile;
    trackFile.addItem(10028, "Save mixer track state...", !mixerChainBusy());
    trackFile.addItem(10029, "Open mixer track state...", !mixerChainBusy());
    trackFile.addSeparator();
    trackFile.addItem(10032, "Link all parameters to MIDI CCs");
    trackFile.addItem(10030, "Arm for rendering", true, renderArmedInserts.contains(index));
    trackFile.addItem(10031, "Render armed tracks to WAV files...", !renderArmedInserts.empty());
    menu.addSubMenu("File", trackFile);
    menu.addItem(10024, "Trim unused tracks (" + juce::String(int(unusedMixerInserts().size())) + ")",
                 !mixerChainBusy() && !unusedMixerInserts().empty());
    juce::PopupMenu dockMenu;
    dockMenu.addItem(10016, "Left", true, insert->dock == -1);
    dockMenu.addItem(10017, "Middle", true, insert->dock == 0);
    dockMenu.addItem(10018, "Right", true, insert->dock == 1);
    menu.addSubMenu("Dock to", dockMenu);
    menu.addItem(10008, "Create insert", !mixerChainBusy());
    menu.addItem(10012, "Add multiple inserts...", !mixerChainBusy());
    {
        // FX container: parallel chains fed from this insert, mixed back where it outputs.
        juce::PopupMenu parallel;
        for (int count = 1; count <= 4; ++count) // IDs 10041..10044: clear of the File items 10028..10032.
            parallel.addItem(10040 + count, juce::String(count) + (count == 1 ? " parallel chain" : " parallel chains"),
                             index != 0 && !mixerChainBusy());
        menu.addSubMenu("Add parallel chains (FX container)", parallel, index != 0);
    }
    menu.addItem(10015, "Add insert from chain preset...", !mixerChainBusy());
    menu.addItem(10013, "Store effect chain in spare state", !mixerChainBusy() && !insert->effects.empty());
    menu.addItem(10014, "Flip effect chain with spare state", !mixerChainBusy() && effectSpares.contains(index));
    juce::PopupMenu deleteMenu;
    deleteMenu.addItem(10009, "Delete unused insert (refuse references)...", index != 0 && !mixerChainBusy());
    std::map<int, int> deleteDestinations;
    int deleteItem = 11000;
    for (const auto &row : controller.project.mixer)
        if (row.index != index)
        {
            deleteDestinations.emplace(deleteItem, row.index);
            deleteMenu.addItem(deleteItem++,
                               "Delete and reroute to " + row.name + " (" + juce::String(row.index) + ")...",
                               index != 0 && !mixerChainBusy());
        }
    menu.addSubMenu("Delete insert...", deleteMenu, index != 0 && !mixerChainBusy());
    const auto chainState = mixerChainReadback()["state"];
    menu.addSeparator();
    menu.addItem(10001, "Copy effect chain", !mixerChainBusy());
    menu.addItem(10002, "Save chain preset...", bool(chainState["available"]) && !mixerChainBusy());
    menu.addItem(10003, "Load chain preset to clipboard...", bool(chainState["available"]) && !mixerChainBusy());
    menu.addItem(10004, "Chain preset directory...", !mixerChainBusy());
    menu.addItem(10005, "Cancel chain operation", mixerChainBusy());
    menu.addItem(10006, "Paste effect chain", bool(chainState["clipboardAvailable"]) && !mixerChainBusy());
    menu.addItem(10007, "Load chain preset...", bool(chainState["available"]) && !mixerChainBusy());
    menu.addSeparator();
    struct Action
    {
        Id id;
        int command;
        int destination;
    };
    std::vector<Action> actions;
    std::map<Id, juce::String> sourceInstances;
    std::vector<std::pair<juce::String, juce::String>> replacementChoices;
    for (const auto &type : plugins.snapshot().plugins)
        if (type.pluginFormatName == "VST3" && !type.isInstrument)
            if (auto xml = type.createXml())
                replacementChoices.emplace_back(type.name + (type.manufacturerName.isEmpty()
                                                                 ? juce::String{}
                                                                 : " (" + type.manufacturerName + ")"),
                                                xml->toString());
    struct DetectorAction
    {
        Id target;
        int source, channels;
        bool custom;
    };
    std::map<int, DetectorAction> detectorActions;
    std::map<int, std::pair<Id, juce::String>> replacementActions;
    int replacementItem = 20000; // Separate from fixed chain entries and original slot actions.
    const auto sourceProject = controller.projectIdentity;
    const auto chainRevision = controller.revision;
    int item = 2;
    for (size_t i = 0; i < insert->effects.size(); ++i)
    {
        auto &fx = insert->effects[i];
        juce::PopupMenu slot;
        auto add =
            [&](juce::String text, int command, int destination = 0, bool enabled = true, bool tick = false)
        {
            actions.push_back({fx.id, command, destination});
            slot.addItem(item++, text, enabled, tick);
        };
        add("Open native editor", 1);
        add("Close editor", 2);
        add("Plugin controls...", 6);
        const auto host = audio.parameterHost(fx.id);
        sourceInstances[fx.id] = host ? host->runtimeIdentity() : juce::String{};
        add("Recover saved state", 7, 0, !host || !host->alive());
        {
            juce::PluginDescription description;
            const auto descriptor = juce::XmlDocument::parse(fx.descriptor);
            if (descriptor && description.loadFromXml(*descriptor) && effects::isNativeDescription(description))
                add("Smart disable", 8, effectSmartDisabled(fx.id) ? 0 : 1, !mixerChainBusy(), effectSmartDisabled(fx.id));
        }
        add("Bypass", 3, fx.bypass ? 0 : 1, true, fx.bypass);
        add("Move up", 4, int(i) - 1, i > 0);
        add("Move down", 4, int(i) + 1, i + 1 < insert->effects.size());
        add("Remove", 5);
        juce::PopupMenu replacementMenu;
        for (const auto &[label, descriptor] : replacementChoices)
        {
            if (replacementItem == std::numeric_limits<int>::max())
                break;
            replacementActions.emplace(replacementItem, std::make_pair(fx.id, descriptor));
            replacementMenu.addItem(replacementItem++, label);
        }
        if (replacementChoices.empty())
            replacementMenu.addItem(19999, "No scanned VST3 effects; browse / scan first", false);
        slot.addSubMenu("Replace...", replacementMenu, !mixerChainBusy() && !replacementChoices.empty());
        juce::PopupMenu detectorMenu;
        const auto addDetector = [&](juce::PopupMenu &to, const juce::String &label, int source, int channels,
                                     bool custom, bool tick = false)
        {
            if (replacementItem == std::numeric_limits<int>::max())
                return;
            detectorActions.emplace(replacementItem, DetectorAction{fx.id, source, channels, custom});
            to.addItem(replacementItem++, label, true, tick);
        };
        addDetector(detectorMenu, "Disconnect", -1, 2, false, !fx.detector);
        for (const auto &source : controller.project.mixer)
            if (source.index > 0 && source.index != index)
            {
                juce::PopupMenu sourceMenu;
                for (const int channels : {2, 1})
                    addDetector(sourceMenu, channels == 2 ? "Stereo / input bus 1" : "Mono / input bus 1",
                                source.index, channels, false,
                                fx.detector &&
                                    *fx.detector == PluginDetectorAssignment{source.index, 1, channels});
                addDetector(sourceMenu, "Other input bus/channels...", source.index, 2, true);
                detectorMenu.addSubMenu(source.name + " [" + juce::String(source.index) + "]", sourceMenu);
            }
        slot.addSubMenu("Sidechain detector (layout request)", detectorMenu, !mixerChainBusy());
        addPluginIoMenu(slot, fx.id, replacementItem);
        slot.addItem(item++, audio.pluginStatus(fx.id), false);
        actions.push_back({fx.id, 0, 0});
        menu.addSubMenu(juce::String(int(i) + 1) + ". " + fx.name, slot);
    }
    auto safe = juce::Component::SafePointer<Studio>(this);
    menu.showMenuAsync({},
                       [safe, actions, replacementActions, detectorActions, deleteDestinations, sourceProject,
                        sourceInstances, index, chainRevision,
                        chainOperation = chainState["activeOperation"].toString()](int selected)
                       {
                           if (!safe)
                               return;
                           if (selected != 10003 && selected != 10004 && selected != 10005 &&
                               safe->controller.projectIdentity != sourceProject)
                           {
                               safe->status = "Project changed; reopen the effect menu";
                               return;
                           }
                           if ((selected == 10006 || selected == 10007 || selected == 10008 ||
                                selected == 10009 || deleteDestinations.contains(selected)) &&
                               safe->controller.revision != chainRevision)
                           {
                               safe->status = "Project changed; reopen the effect menu";
                               return;
                           }
                           if (selected == 10008)
                           {
                               safe->command("mixer.insert.create", {});
                               return;
                           }
                           if (selected > 10040 && selected <= 10044)
                           {
                               CommandArgs a;
                               a.target = index;
                               a.value = selected - 10040;
                               safe->command("mixer.insert.create", a);
                               return;
                           }
                           if (selected == 10010)
                           {
                               safe->openMixerEq(index);
                               return;
                           }
                           if (selected == 10015)
                           {
                               safe->addInsertFromChainPreset();
                               return;
                           }
                           if (selected == 10013 || selected == 10014)
                           {
                               if (const auto started = safe->startEffectSpare(index, selected == 10014); started.failed())
                                   safe->status = started.getErrorMessage();
                               safe->repaint();
                               return;
                           }
                           if (selected == 10012)
                           {
                               auto *box = new juce::AlertWindow("Add multiple inserts", "How many Mixer inserts (1..32)?",
                                                                 juce::MessageBoxIconType::NoIcon);
                               box->addTextEditor("count", "4");
                               box->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey));
                               box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                               box->enterModalState(true, juce::ModalCallbackFunction::create(
                                                              [safe, box](int ok)
                                                              {
                                                                  if (!safe || ok != 1)
                                                                      return;
                                                                  CommandArgs a;
                                                                  a.value = box->getTextEditorContents("count").getIntValue();
                                                                  safe->command("mixer.insert.create", a);
                                                              }),
                                                    true);
                               return;
                           }
                           if (selected >= 10020 && selected <= 10022)
                           {
                               if (auto chosen = safe->selectRoutedFor(index, selected - 10020); chosen.failed())
                                   safe->status = chosen.getErrorMessage();
                               safe->repaint();
                               return;
                           }
                           if (selected >= 10025 && selected <= 10027)
                           {
                               const auto started = selected == 10027 ? safe->createSubmix()
                                                                      : safe->routeSelectedTo(index, selected == 10026);
                               if (started.failed())
                                   safe->status = started.getErrorMessage();
                               safe->repaint();
                               return;
                           }
                           if (selected == 10032)
                           {
                               safe->selectedInsert = index;
                               safe->performAction("mixer.linkAll");
                               return;
                           }
                           if (selected == 10030 || selected == 10031)
                           {
                               if (selected == 10030)
                                   safe->selectedInsert = index;
                               safe->performAction(selected == 10030 ? "mixer.renderArm" : "mixer.renderArmed");
                               return;
                           }
                           if (selected == 10028 || selected == 10029)
                           {
                               safe->chooseTrackStateFile(index, selected == 10028);
                               return;
                           }
                           if (selected == 10024)
                           {
                               safe->trimUnusedInserts();
                               return;
                           }
                           if (selected == 10023)
                           {
                               CommandArgs a;
                               a.mixer = index;
                               safe->command("mixer.reset", a);
                               return;
                           }
                           if (selected >= 10016 && selected <= 10018)
                           {
                               CommandArgs a;
                               a.mixer = index;
                               a.value = selected - 10017;
                               safe->command("mixer.dock", a);
                               return;
                           }
                           if (selected == 10011)
                           {
                               const auto *row = mixerInsert(safe->controller.project, index);
                               auto *box = new juce::AlertWindow("Track delay", "Time offset in milliseconds (0..500)",
                                                                 juce::MessageBoxIconType::NoIcon);
                               box->addTextEditor("ms", juce::String(row ? row->delayMs : 0.f, 2));
                               box->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
                               box->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                               box->enterModalState(true, juce::ModalCallbackFunction::create(
                                                              [safe, box, index](int ok)
                                                              {
                                                                  if (!safe || ok != 1)
                                                                      return;
                                                                  CommandArgs a;
                                                                  a.mixer = index;
                                                                  a.value = box->getTextEditorContents("ms").getDoubleValue();
                                                                  safe->command("mixer.delay", a);
                                                              }),
                                                    true);
                               return;
                           }
                           if (selected == 10009 || deleteDestinations.contains(selected))
                           {
                               const auto destination = deleteDestinations.find(selected);
                               safe->confirmMixerInsertDeletion(
                                   index, destination == deleteDestinations.end()
                                              ? std::optional<int>{}
                                              : std::optional<int>{destination->second});
                               return;
                           }
                           if (selected >= 10001 && selected <= 10007)
                           {
                               if (selected == 10005)
                               {
                                   auto args = chain_workflow_detail::object();
                                   chain_workflow_detail::put(args, "action", "cancel");
                                   chain_workflow_detail::put(args, "operation", chainOperation);
                                   const auto out = safe->mixerChainOperation(args);
                                   safe->status = out["message"].toString();
                                   return;
                               }
                               static constexpr const char *ids[]{
                                   "mixer.chain.copy",      "mixer.chain.save",   "mixer.chain.loadClipboard",
                                   "mixer.chain.directory", "mixer.chain.cancel", "mixer.chain.paste",
                                   "mixer.chain.loadApply"};
                               safe->selectedInsert = index;
                               safe->performMixerChainAction(ids[selected - 10001]);
                               return;
                           }
                           if (selected == 1)
                           {
                               safe->showPlugins();
                               return;
                           }
                           if (const auto detector = detectorActions.find(selected);
                               detector != detectorActions.end())
                           {
                               const auto action = detector->second;
                               const auto *row = mixerInsert(safe->controller.project, index);
                               if (safe->controller.revision != chainRevision || !row ||
                                   std::none_of(row->effects.begin(), row->effects.end(),
                                                [&](const auto &slot) { return slot.id == action.target; }) ||
                                   (action.source > 0 &&
                                    !mixerInsert(safe->controller.project, action.source)))
                               {
                                   safe->status = "Project or detector slot changed; reopen its menu";
                                   return;
                               }
                               if (!action.custom)
                               {
                                   CommandArgs args;
                                   args.target = action.target;
                                   std::optional<PluginDetectorAssignment> assignment;
                                   if (action.source > 0)
                                       assignment =
                                           PluginDetectorAssignment{action.source, 1, action.channels};
                                   args.detector.emplace(assignment);
                                   safe->command("plugin.effect.sidechain", args);
                                   return;
                               }
                               auto *dialog =
                                   new juce::AlertWindow("Effect detector layout",
                                                         "Request a layout; the worker must negotiate it.",
                                                         juce::MessageBoxIconType::NoIcon);
                               dialog->addTextEditor("bus", "1", "Input bus (1-31)");
                               dialog->getTextEditor("bus")->setInputRestrictions(2, "0123456789");
                               dialog->addComboBox("channels", {"Stereo", "Mono"}, "Channels");
                               dialog->addButton("Request", 1, juce::KeyPress(juce::KeyPress::returnKey));
                               dialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
                               dialog->enterModalState(
                                   true,
                                   juce::ModalCallbackFunction::create(
                                       [safe, form = juce::Component::SafePointer<juce::AlertWindow>(dialog),
                                        action, sourceProject, chainRevision, index](int choice)
                                       {
                                           if (!safe || !form || choice != 1)
                                               return;
                                           const auto *row = mixerInsert(safe->controller.project, index);
                                           if (safe->controller.projectIdentity != sourceProject ||
                                               safe->controller.revision != chainRevision || !row ||
                                               std::none_of(row->effects.begin(), row->effects.end(),
                                                            [&](const auto &slot)
                                                            { return slot.id == action.target; }) ||
                                               !mixerInsert(safe->controller.project, action.source))
                                           {
                                               safe->status = "Project changed; reopen the detector menu";
                                               return;
                                           }
                                           const auto bus = form->getTextEditorContents("bus").trim();
                                           if (bus.isEmpty() || !bus.containsOnly("0123456789") ||
                                               bus.getIntValue() < 1 || bus.getIntValue() > 31)
                                           {
                                               safe->status = "Enter an input bus from 1 to 31";
                                               safe->repaint();
                                               return;
                                           }
                                           CommandArgs args;
                                           args.target = action.target;
                                           args.detector.emplace(PluginDetectorAssignment{
                                               action.source, bus.getIntValue(),
                                               form->getComboBoxComponent("channels")
                                                           ->getSelectedItemIndex() == 1
                                                   ? 1
                                                   : 2});
                                           safe->command("plugin.effect.sidechain", args);
                                       }),
                                   true);
                               return;
                           }
                           if (const auto replacement = replacementActions.find(selected);
                               replacement != replacementActions.end())
                           {
                               const auto *row = mixerInsert(safe->controller.project, index);
                               if (safe->controller.revision != chainRevision || !row ||
                                   std::none_of(row->effects.begin(), row->effects.end(),
                                                [&](const auto &slot)
                                                { return slot.id == replacement->second.first; }))
                               {
                                   safe->status = "Project or effect slot changed; reopen the Replace menu";
                                   return;
                               }
                               CommandArgs args;
                               args.target = replacement->second.first;
                               args.text = replacement->second.second;
                               safe->command("plugin.effect.replace", args);
                               return;
                           }
                           if (selected < 2 || selected - 2 >= int(actions.size()))
                               return;
                           auto action = actions[size_t(selected - 2)];
                           if (action.command == 6)
                               safe->openPluginControlsUI(action.id);
                           else if (action.command == 7)
                               safe->recoverPlugin(action.id);
                           else if (action.command == 8)
                           {
                               if (auto started = safe->setEffectSmartDisable(action.id, action.destination == 1,
                                                                              [](juce::Result) {});
                                   started.failed())
                                   safe->status = started.getErrorMessage();
                               safe->repaint();
                           }
                           else if (action.command == 1 || action.command == 2)
                           {
                               const auto host = safe->audio.parameterHost(action.id);
                               const auto prior = sourceInstances.find(action.id);
                               if (!host || prior == sourceInstances.end() ||
                                   host->runtimeIdentity() != prior->second)
                               {
                                   safe->status = "Plugin runtime changed; reopen the effect menu";
                                   return;
                               }
                               safe->openPluginEditorUI(action.id, action.command == 1);
                           }
                           else
                           {
                               CommandArgs a;
                               a.target = action.id;
                               a.enabled = action.destination != 0;
                               a.lane = action.destination;
                               if (action.command)
                                   safe->command(action.command == 3   ? "plugin.bypass"
                                                 : action.command == 4 ? "plugin.effect.move"
                                                                       : "plugin.remove",
                                                 a);
                           }
                       });
}
juce::File Studio::profileFile() const
{
    if (workflowTestSettings)
        return workflowTestSettings->getFile();
    // Owned recovery acceptance (scripts/verify-crash-recovery.py) keeps the shortcut profile,
    // autosaves and session markers in its own folder instead of the user's profile.
    if (const auto folder = juce::SystemStats::getEnvironmentVariable("VIBERA_PROFILE_DIRECTORY", {});
        folder.isNotEmpty() && juce::File::isAbsolutePath(folder) && juce::File(folder).isDirectory())
        return juce::File(folder).getChildFile("shortcuts.json");
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("VibeRa Studio/shortcuts.json");
}
void Studio::saveProfile()
{
    auto result = customization.save(profileFile());
    if (result.failed())
        status = result.getErrorMessage();
}
juce::File Studio::appearanceFile() const
{
    if (workflowTestSettings)
        return workflowTestSettings->getFile().getSiblingFile(
            workflowTestSettings->getFile().getFileNameWithoutExtension() + "-appearance.json");
    return themeFile();
}
void Studio::applyAppearance()
{
    // Fonts made without an explicit name (and every vibera::uiFont use) follow the choice.
    look.setDefaultSansSerifTypefaceName(uiFont);
    juce::LookAndFeel::getDefaultLookAndFeel().setDefaultSansSerifTypefaceName(uiFont);
    look.setColour(juce::ResizableWindow::backgroundColourId, panel);
    look.setColour(juce::TextButton::buttonColourId, grid);
    look.setColour(juce::TextButton::textColourOffId, ink);
    look.setColour(juce::TextButton::textColourOnId, ink);
    look.setColour(juce::ComboBox::backgroundColourId, bg);
    look.setColour(juce::ComboBox::textColourId, ink);
    look.setColour(juce::Label::textColourId, ink);
    look.setColour(juce::Slider::thumbColourId, accent);
    look.setColour(juce::Slider::textBoxTextColourId, ink);
    look.setColour(juce::Slider::textBoxBackgroundColourId, bg);
    look.setColour(juce::Slider::textBoxOutlineColourId, grid);
    look.setColour(juce::TextEditor::backgroundColourId, bg);
    look.setColour(juce::TextEditor::textColourId, ink);
    look.setColour(juce::TextEditor::highlightColourId, accent.darker(.4f));
    look.setColour(juce::TextEditor::outlineColourId, grid);
    look.setColour(juce::TextEditor::focusedOutlineColourId, accent.withAlpha(.8f));
    look.setColour(juce::ToggleButton::textColourId, ink);
    look.setColour(juce::ToggleButton::tickColourId, bg);
    look.setColour(juce::TooltipWindow::backgroundColourId, panel.brighter(.08f));
    look.setColour(juce::TooltipWindow::textColourId, ink);
    look.setColour(juce::TooltipWindow::outlineColourId, grid);
    look.setColour(juce::ListBox::backgroundColourId, bg);
    look.setColour(juce::ScrollBar::thumbColourId, muted);
    look.setColour(juce::PopupMenu::backgroundColourId, panel);
    look.setColour(juce::PopupMenu::textColourId, ink);
    look.setColour(juce::PopupMenu::highlightedBackgroundColourId, grid);
    look.setColour(juce::TabbedComponent::backgroundColourId, panel);
    look.setColour(juce::TabbedButtonBar::tabTextColourId, ink);
    look.setColour(juce::TabbedButtonBar::frontTextColourId, ink);
    title.setColour(juce::Label::textColourId, ink);
    deviceLabel.setColour(juce::Label::textColourId, muted);
    record.setColour(juce::TextButton::buttonOnColourId, themeColour(0xff943c4b));
    std::function<void(juce::Component &)> update = [&](juce::Component &component)
    {
        if (auto *window = dynamic_cast<juce::ResizableWindow *>(&component); window && window->isOnDesktop())
        {
            window->setBackgroundColour(panel);
            roundNativeWindow(*window);
        }
        if (auto *tree = dynamic_cast<juce::TreeView *>(&component))
        {
            tree->setColour(juce::TreeView::backgroundColourId, bg);
            tree->setColour(juce::TreeView::linesColourId, grid);
        }
        component.sendLookAndFeelChange();
        component.repaint();
        for (int i = 0; i < component.getNumChildComponents(); ++i)
            update(*component.getChildComponent(i));
    };
    update(*this);
    for (auto &window : detachedWindows)
        if (window)
            update(*window);
    if (settingsWindow)
        update(*settingsWindow);
    if (parameterWindow)
        update(*parameterWindow);
    for (auto &[id, window] : instrumentWindows)
        if (window)
            update(*window);
    if (auto *top = getTopLevelComponent())
        top->repaint();
}
void Studio::closeSettings()
{
    if (settingsWindow)
        settingsWindow->exitModalState(0);
}
void Studio::runMacro(const juce::String &id)
{
    const auto macro = std::find_if(customization.macros.begin(), customization.macros.end(),
                                    [&](const auto &value) { return value.id == id; });
    if (routingPending()) { status = routingPendingStatus; return; }
    if (macro != customization.macros.end())
    {
        const bool anyRouting = std::any_of(macro->steps.begin(), macro->steps.end(),
            [](const auto& step) { return step.action == "channel.mixer"; });
        if (anyRouting)
        {
            if (!std::all_of(macro->steps.begin(), macro->steps.end(),
                [](const auto& step) { return step.action == "channel.mixer"; }))
            { status = "routing.unsupportedMixedSequence: Route/content macros need one joint plan"; return; }
            std::vector<CommandArgs> assignments;
            for (const auto& step : macro->steps)
            {
                auto args = step.parameters;
                if (step.useCurrentContext) args.channel = selectedChannel;
                assignments.push_back(std::move(args));
            }
            const auto result = applyRoutingSequence(assignments, macro->name);
            status = result.wasOk() ? (routingPending() ? routingPendingStatus : routingReadyStatus)
                                   : result.getErrorMessage();
            return;
        }
        // Handoff current canonical audio once BEFORE a legacy macro gesture;
        // later-step failure cannot leave a partly applied private native graph.
        if ((audio.channelRoutingReadback().overlayActive || audio.documentProjectionDeferred()) &&
            std::any_of(macro->steps.begin(), macro->steps.end(), [&](const auto& step)
            { auto args = step.parameters; if (step.useCurrentContext) args.ids = selectedClips;
              return routingStructuralCommand(step.action, args); }))
            if (auto ready = restoreRoutingBeforeStructural(); ready.failed())
            { status = ready.getErrorMessage(); return; }
    }

    if (macro != customization.macros.end() &&
        std::any_of(macro->steps.begin(), macro->steps.end(),
                    [](const auto &step) { return step.action == "plugin.effect.move"; }))
    {
        juce::String code;
        if (auto result = admitMixerEffectMove(code); result.failed())
        {
            status = result.getErrorMessage();
            return;
        }
    }
    if (macro != customization.macros.end() &&
        std::any_of(macro->steps.begin(), macro->steps.end(), [](const auto &step)
                    { return step.action == "plugin.effect.mix" || step.action == "mixer.effects.bypass" ||
                               step.action == "mixer.stereoSeparation" || step.action == "mixer.phaseInvert" || step.action == "mixer.stereoSwap"; }))
    {
        juce::String code;
        if (auto result = admitMixerChainControl(code); result.failed())
        {
            status = result.getErrorMessage();
            return;
        }
    }
    if (finishRackEditsChecked().failed())
        return;
    const bool usesQuantizer =
        macro != customization.macros.end() && std::any_of(macro->steps.begin(), macro->steps.end(),
                                                           [](const auto &step)
                                                           {
                                                               return step.action == "notes.quantize" ||
                                                                      step.action == "notes.quickQuantize" ||
                                                                      step.action == "notes.quantizeStarts";
                                                           });
    if (usesQuantizer)
        if (auto admitted = quantizerEditAdmission(recordArmed, midiTaking || hasPendingMidiTake(),
                                                   bool(afterPluginState) || bool(pendingExport),
                                                   audio.rendering(), midiFileBusy);
            admitted.failed())
        {
            status = admitted.getErrorMessage();
            return;
        }
    const bool takeSafe =
        macro != customization.macros.end() &&
        std::all_of(macro->steps.begin(), macro->steps.end(),
                    [](const auto &step)
                    {
                        return step.action.startsWith("mixer.") || step.action == "plugin.effect.mix" ||
                               step.action.startsWith("channel.sampler.modulation.") ||
                               isSwingCommand(step.action) || step.action.startsWith("rack.group.") ||
                               step.action == "channel.moveUp" || step.action == "channel.moveDown" ||
                               step.action.startsWith("steps.fill") || step.action == "channel.solo" ||
                               step.action == "channel.unsolo" || step.action == "steps.velocity" ||
                               step.action == "steps.pitch" || step.action == "steps.pan" ||
                               step.action == "steps.fine" || step.action == "steps.modX" ||
                               step.action == "steps.modY" || step.action == "steps.shift" ||
                               step.action == "steps.repeat" || step.action == "steps.release" ||
                               step.action == "channel.mute" || step.action == "channel.muteLock" ||
                               step.action == "rack.muteLock" || step.action == "rack.muteSelected" ||
                               step.action == "rack.unmuteSelected" || step.action == "channel.properties" ||
                               step.action == "rack.properties" || step.action == "rack.sort";
                    });
    if (!takeSafe)
        if (auto result = finishMidiTake(); result.failed())
        {
            status = result.getErrorMessage();
            return;
        }
    MacroContext context{selectedPattern, selectedChannel, selectedInsert,
                         selectedNotes,   selectedClips,   selectedChannels};
    context.mixerSelection = mixerSelection.ids;
    context.mixerGroup = mixerGroupFor(controller.project, selectedInsert);
    auto result = customization.runMacro(id, controller, context);
    if (result.wasOk())
    {
        selectedPattern = context.pattern;
        selectedChannel = context.channel;
        selectedNotes = context.notes;
        selectedClips = context.clips;
        selectedChannels = context.channels;
        status = "Macro complete";
        refresh();
    }
    else
    {
        status = result.getErrorMessage();
        repaint();
    }
}
juce::Result Studio::refreshProjectTransitionState(juce::String &errorCode)
{
    errorCode.clear();
    auto busy = [&](const char *message)
    { errorCode = "busy"; return juce::Result::fail(message); };
    // Refusal never drains, steals, clears or cancels another accepted file job.
    if (mixerChainApplying() || devicePreparationBusy() || routingPending() || afterPluginState ||
        pendingExport || audio.rendering() || midiFileBusy || mcpExportOperation.isNotEmpty() ||
        pluginRecovery || !audioImports.idle() || audioImportAfterTake || pollingAudioImports)
        return busy("busy: Finish or cancel the active project/audio operation before closing or replacing");
    if (controller.hasGesture() || midiTaking || hasPendingMidiTake())
        return busy("busy: Finish the edit or finish/retry/cancel the MIDI take before replacing");
    if (pluginControlsPending())
        return busy("busy: Wait for the queued or unacknowledged plugin Controls edit");
    try
    {
        auto pending = [&](const PluginSpec &plugin)
        {
            const auto host = audio.parameterHost(plugin.id); // CONTROL ownership only.
            return host && (host->pendingParameterRequests() || host->editorBusy());
        };
        for (const auto &channel : controller.project.channels)
            if (channel.instrument && pending(*channel.instrument))
                return busy("busy: Wait for the active native plugin request before closing or replacing");
        for (const auto &insert : controller.project.mixer)
            for (const auto &effect : insert.effects)
                if (pending(effect))
                    return busy("busy: Wait for the active native plugin request before closing or replacing");
        // Observed revisions are not the saved baseline. Preserve captured opaque
        // revisions; this ONLY materializes newer volatile state as existing dirty.
        if (audio.consumePluginChanges())
            controller.markExternalChange();
    }
    catch (...)
    {
        controller.markExternalChange(); // Conservatively retain unsaved ownership.
        errorCode = "preparation_failed";
        return juce::Result::fail("transition.preparationFailed: Cannot establish current native state; work retained");
    }
    return juce::Result::ok();
}
juce::Result Studio::admitProjectTransition(bool discardChanges, juce::String &errorCode)
{
    if (auto ready = refreshProjectTransitionState(errorCode); ready.failed()) return ready;
    if (controller.dirty && !discardChanges)
    {
        errorCode = "unsaved_changes";
        return juce::Result::fail("unsaved_changes: Save first, or explicitly discard current changes");
    }
    return juce::Result::ok();
}
void Studio::requestClose(std::function<void()> proceed)
{
    requestProjectTransition([proceed = std::move(proceed)](bool) { proceed(); });
}
void Studio::requestProjectTransition(std::function<void(bool)> proceed)
{
    if (finishRackEditsChecked().failed()) return;
    if (auto result = cancelRemotePreview(controller); result.failed())
    { status = result.getErrorMessage(); return; }
    if (auto result = finishMidiTake(); result.failed())
    { status = result.getErrorMessage(); return; }
    juce::String code;
    if (auto ready = refreshProjectTransitionState(code); ready.failed())
    { status = ready.getErrorMessage(); return; }
    if (!controller.dirty)
    {
        proceed(false); // No discard authority is manufactured from a clean check.
        return;
    }
    const auto identity = controller.projectIdentity;
    auto safe = juce::Component::SafePointer<Studio>(this);
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                     .withTitle("Unsaved project")
                                     .withMessage("Discard unsaved changes?")
                                     .withButton("Discard")
                                     .withButton("Cancel"),
        [safe, identity, proceed = std::move(proceed)](int result)
        {
            if (!safe || result != 1) return;
            if (safe->controller.projectIdentity != identity)
            { safe->status = "stale_project: Original discard request no longer targets this project"; return; }
            juce::String code;
            if (auto ready = safe->admitProjectTransition(true, code); ready.failed())
            { safe->status = ready.getErrorMessage(); return; }
            proceed(true); // Explicit Discard for this same project; final target revalidates.
        });
}

juce::Result Studio::runInstrumentOpeningTest(const juce::File &projectFile, const juce::File &output)
{
#if !JUCE_WINDOWS
    juce::ignoreUnused(projectFile, output);
    return juce::Result::fail("Native instrument window acceptance requires Windows");
#else

    stopTimer();
    output.createDirectory();
    Project project;
    auto loaded = loadProject(projectFile, project);
    if (loaded.failed())
        return loaded;
    if (project.placements.empty())
        project.placements.push_back(
            {project.allocate(), project.patterns.front().id, 0, 0, project.patterns.front().length, 0});
    // A deferred name-button click is bound to the original project and
    // instrument, not merely a numeric Channel ID that another project may reuse.
    auto deferredProject = project;
    auto &deferredChannel = deferredProject.channels.front();
    PluginSpec deferredSpec;
    deferredSpec.id = deferredProject.allocate();
    deferredSpec.descriptor = "original deferred instrument";
    deferredChannel.instrument = deferredSpec;
    controller.replace(deferredProject);
    pendingInstrument = PendingInstrumentOpening{controller.projectIdentity, deferredSpec.descriptor,
                                                 deferredChannel.id, deferredSpec.id};
    refresh();
    if (!pendingInstrument)
        return juce::Result::fail("Valid deferred instrument target was discarded");
    controller.project.channels.front().instrument->descriptor = "replacement instrument";
    refresh();
    if (pendingInstrument)
        return juce::Result::fail("Deferred opening survived an instrument replacement");
    controller.replace(deferredProject);
    pendingInstrument = PendingInstrumentOpening{controller.projectIdentity, deferredSpec.descriptor,
                                                 deferredChannel.id, deferredSpec.id};
    controller.replace(deferredProject); // Identical IDs/descriptors, different session identity.
    refresh();
    if (pendingInstrument)
        return juce::Result::fail("Deferred opening survived project identity replacement");
    pendingInstrument = PendingInstrumentOpening{controller.projectIdentity, deferredSpec.descriptor,
                                                 deferredChannel.id, deferredSpec.id};
    newProject(false);
    if (pendingInstrument)
        return juce::Result::fail("New project retained a deferred instrument opening");
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
    if (!device)
        return juce::Result::fail("No virtual output");
    clearScoreLog();
    controller.replace(project);
    selectedPattern = project.patterns.front().id;
    auto selected = std::find_if(project.channels.begin(), project.channels.end(),
                                 [](auto &c) { return c.instrument || !c.source.startsWith("builtin:"); });
    if (selected == project.channels.end())
        selected = project.channels.begin();
    selectedChannel = selected->id;
    refresh();
    auto beforeDocument = controller.project.state();
    juce::AudioBuffer<float> block(2, 256);
    juce::String report;
    const auto windowTrace = output.getChildFile("native-window-trace.txt");
    if (!windowTrace.replaceWithText(
            "Channel Rack opening window identities; UI-thread observations only.\n"))
        return juce::Result::fail("Cannot create native-window trace");
    class CallbackCapture final : public juce::Thread
    {
        SyntheticDevice &device;
        juce::AudioBuffer<float> buffer{2, 256};

      public:
        std::atomic<int> blocks{0};
        double maximumMs = 0;
        int silent = 0;
        float peak = 0;
        explicit CallbackCapture(SyntheticDevice &d) : Thread("Rack opening virtual callback"), device(d) {}
        ~CallbackCapture() override
        {
            signalThreadShouldExit();
            waitForThreadToExit(3000);
        }
        void run() override
        {
            while (!threadShouldExit())
            {
                buffer.clear();
                auto begin = juce::Time::getHighResolutionTicks();
                device.process(buffer);
                maximumMs = std::max(maximumMs, 1000. * juce::Time::highResolutionTicksToSeconds(
                                                            juce::Time::getHighResolutionTicks() - begin));
                peak = std::max(peak, buffer.getMagnitude(0, 256));
                if (buffer.getMagnitude(0, 256) == 0)
                    ++silent;
                ++blocks;
                sleep(6);
            }
        }
    };
    for (bool mode : {true, false})
    {
        patternMode = mode;
        auto deadline = juce::Time::getMillisecondCounter() + 30000;
        auto result = audio.rebuild(project, mode, selectedPattern);
        while (result.failed() && result.getErrorMessage() == "Preparing VST3 workers..." &&
               juce::Time::getMillisecondCounter() < deadline)
        {
            dispatchMessages(5);
            result = audio.rebuild(project, mode, selectedPattern);
        }
        if (result.failed())
            return result;
        if (mode && selected->instrument)
        {
            const auto plugin = selected->instrument->id;
            const auto oldHost = audio.preparedPluginHost(controller.project, plugin);
            if (!oldHost)
                return juce::Result::fail("Opening fixture needs a genuine prepared instrument");
            struct CaptureObservation
            {
                bool done = false, succeeded = false;
            };
            const auto captured = std::make_shared<CaptureObservation>();
            withPluginState(
                [captured, plugin](juce::Result stateResult, Project snapshot)
                {
                    captured->done = true;
                    const auto *saved = snapshot.plugin(plugin);
                    captured->succeeded = stateResult.wasOk() && saved && saved->state.isNotEmpty();
                });
            if (!afterPluginState)
                return juce::Result::fail("Native state fixture was not actually admitted");
            openInstrument(selectedChannel);
            if (!pendingInstrument || oldHost->editorWindowState().exists ||
                mcpCall("vibera_state",
                        juce::var(new juce::DynamicObject))["structuredContent"]["instrumentOpening"]["phase"]
                        .toString() != "waiting")
                return juce::Result::fail("Busy native capture discarded a Channel-name opening intent");
            audio.invalidateProjection();
            needsRebuild = false; // Exercise the timer's own binding check, not its rebuild flag.
            openInstrument(selectedChannel);
            timerCallback();
            if (!pendingInstrument || pendingInstrument->plugin != plugin ||
                oldHost->editorWindowState().exists)
                return juce::Result::fail("Stale retained worker consumed a deferred name-button click");
            deadline = juce::Time::getMillisecondCounter() + 30000;
            do
            {
                result = audio.rebuild(controller.project, mode, selectedPattern);
                if (result.failed() && AudioEngine::preparationPending(result))
                    dispatchMessages(5);
            } while (result.failed() && AudioEngine::preparationPending(result) &&
                     juce::Time::getMillisecondCounter() < deadline);
            if (result.failed())
                return result;
            timerCallback(); // Matching prepared projection now consumes exactly the queued target.
            const auto current = audio.preparedPluginHost(controller.project, plugin);
            deadline = juce::Time::getMillisecondCounter() + 10000;
            while (current && (!current->editorWindowState().visible || current->editorBusy()) &&
                   juce::Time::getMillisecondCounter() < deadline)
            {
                dispatchMessages(5);
                timerCallback();
            }
            pollPluginOperations();
            juce::String deferredTrace =
                "deferred status=" + status + " projectionError=" + projectionError +
                " pending=" + juce::String(int(bool(pendingInstrument))) +
                " needsRebuild=" + juce::String(int(needsRebuild)) + " old=" + oldHost->runtimeIdentity() +
                " oldWindow=" + juce::String(juce::uint64(oldHost->editorWindowState().packed())) +
                " current=" + juce::JSON::toString(pluginEditorState(plugin), true) + "\n";
            for (const auto &job : pluginOperations)
                if (job.kind == "editor")
                    deferredTrace += "  action=" + job.action + " instance=" + job.instance +
                                     " state=" + job.state + " code=" + job.code + " message=" + job.message +
                                     "\n";
            if (!windowTrace.appendText(deferredTrace))
                return juce::Result::fail("Cannot retain deferred native admission diagnostics");
            if (!captured->done || !captured->succeeded || pendingInstrument || !current ||
                !current->editorWindowState().visible ||
                current->runtimeIdentity() == oldHost->runtimeIdentity() ||
                oldHost->editorWindowState().exists)
                return juce::Result::fail("Deferred click failed to open only the matching new native owner");
            openPluginEditorUI(plugin, false);
            deadline = juce::Time::getMillisecondCounter() + 10000;
            while ((current->editorWindowState().exists || current->editorBusy()) &&
                   juce::Time::getMillisecondCounter() < deadline)
            {
                dispatchMessages(5);
                pollPluginOperations();
            }
            pollPluginOperations();
            if (current->editorWindowState().exists)
                return juce::Result::fail("Deferred-click fixture failed to close its actual native window");
            report += "Deferred opening: replacement/project/New guards and retained-old-worker timer path "
                      "pass; only matching new native instance opened.\n";
        }
        audio.setMidiTarget(selectedChannel);
        audio.seekBeat(0);
        audio.playStop();
        for (int i = 0; i < 8; ++i)
        {
            device->process(block);
            dispatchMessages(6);
        }
        auto before = audio.projectionDiagnostics();
        int starts = device->starts, stops = device->stops;
        auto clickName = [&]
        {
            auto *editor = dynamic_cast<Editor *>(windows[0]->getContentComponent());
            int row = int(std::distance(project.channels.begin(), selected));
            auto point = juce::Point<float>(55.f, float(44 + row * 38 + 16));
            auto now = juce::Time::getCurrentTime();
            juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), point,
                                   juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                   editor, editor, now, point, now, 1, false);
            editor->mouseDown(event);
            editor->mouseUp(event);
        };
        CallbackCapture capture(*device);
        capture.startThread();
        auto position = audio.positionBeat();
        clickName();
        int count = 0;
        HWND firstWindow = nullptr;
        if (selected->instrument)
        {
            DWORD pid = 0;
            for (auto &d : audio.pluginDiagnostics())
                if (d.id == selected->instrument->id)
                    pid = d.processId;
            struct Probe
            {
                DWORD pid;
                int count = 0;
                HWND first = nullptr;
                juce::String details;
            };
            auto probe = [&]
            {
                Probe p{pid};
                EnumWindows(
                    [](HWND h, LPARAM value) -> BOOL
                    {
                        auto &p = *reinterpret_cast<Probe *>(value);
                        DWORD id = 0;
                        GetWindowThreadProcessId(h, &id);
                        if (id != p.pid)
                            return TRUE;
                        wchar_t name[1024]{}, type[256]{};
                        GetWindowTextW(h, name, 1024);
                        GetClassNameW(h, type, 256);
                        RECT rect{};
                        GetWindowRect(h, &rect);
                        auto handle = [](HWND window)
                        { return juce::String::toHexString(juce::int64(reinterpret_cast<INT_PTR>(window))); };
                        p.details += "  hwnd=" + handle(h) + " visible=" + juce::String(IsWindowVisible(h)) +
                                     " owner=" + handle(GetWindow(h, GW_OWNER)) +
                                     " parent=" + handle(GetParent(h)) + " class=" + juce::String(type) +
                                     " title=" + juce::String(name) + " style=" +
                                     juce::String::toHexString(juce::int64(GetWindowLongPtrW(h, GWL_STYLE))) +
                                     " rect=" + juce::String(rect.left) + "," + juce::String(rect.top) + "," +
                                     juce::String(rect.right) + "," + juce::String(rect.bottom) + "\n";
                        if (IsWindowVisible(h) && GetWindow(h, GW_OWNER) == nullptr)
                        {
                            ++p.count;
                            p.first = h;
                        }
                        return TRUE;
                    },
                    reinterpret_cast<LPARAM>(&p));
                return p;
            };
            auto trace = [&](const char *phase, const Probe &p)
            {
                windowTrace.appendText(
                    (mode ? "Pattern " : "Song ") + juce::String(phase) +
                    " timeMs=" + juce::String(juce::Time::getMillisecondCounter()) +
                    " pid=" + juce::String(p.pid) + " count=" + juce::String(p.count) + " expected=" +
                    juce::String::toHexString(juce::int64(reinterpret_cast<INT_PTR>(firstWindow))) + "\n" +
                    p.details);
                windowTrace.appendText("  status=" + status + "\n");
                for (const auto &job : pluginOperations)
                    if (job.kind == "editor" && job.plugin == selected->instrument->id)
                        windowTrace.appendText("  operation=" + job.id + " action=" + job.action +
                                               " state=" + job.state + " code=" + job.code + "\n");
            };
            auto editorPending = [&]
            {
                // This fixture stops the Studio timer. Pump the same editor
                // housekeeping explicitly; an HWND can appear before its owned
                // open/bring-to-front acknowledgement has been consumed.
                pollPluginOperations();
                if (pendingInstrument &&
                    pendingInstrument->matches(controller.projectIdentity, controller.project) &&
                    audio.pluginProjectionReady(controller.project, pendingInstrument->plugin))
                    openInstrument(pendingInstrument->channel);
                return pendingInstrument.has_value() ||
                       std::any_of(pluginOperations.begin(), pluginOperations.end(),
                                   [&](const auto &job)
                                   {
                                       return job.kind == "editor" &&
                                              job.plugin == selected->instrument->id &&
                                              job.state == "pending";
                                   });
            };
            auto editorCompleted = [&](const char *action)
            {
                const auto latest =
                    std::find_if(pluginOperations.rbegin(), pluginOperations.rend(), [&](const auto &job)
                                 { return job.kind == "editor" && job.plugin == selected->instrument->id; });
                return latest != pluginOperations.rend() && latest->action == action &&
                       latest->state == "completed";
            };
            trace("after-first-click", probe());
            deadline = juce::Time::getMillisecondCounter() + 10000;
            auto opened = probe();
            while ((editorPending() || opened.count == 0) && juce::Time::getMillisecondCounter() < deadline)
            {
                dispatchMessages(6);
                opened = probe();
            }
            count = opened.count;
            firstWindow = opened.first;
            trace("first-visible", opened);
            if (count != 1 || pid == 0 || editorPending() || !editorCompleted("open"))
                return juce::Result::fail("Actual native VST3 editor did not open exactly once");
            clickName();
            // Reuse the first opening's existing10s guard. Do not issue Close
            // while the repeat Open is still pending, and do not enlarge the
            // native request's unchanged5s deadline.
            while (editorPending() && juce::Time::getMillisecondCounter() < deadline)
            {
                dispatchMessages(6);
            }
            auto reopened = probe();
            trace("after-repeat-click", reopened);
            if (reopened.count != 1 || reopened.first != firstWindow || editorPending() ||
                !editorCompleted("open"))
                return juce::Result::fail("Native instrument window duplicated");
            openPluginEditorUI(selected->instrument->id, false);
            trace("close-requested", probe());
            deadline = juce::Time::getMillisecondCounter() + 10000;
            auto closed = probe();
            // HWND destruction can precede the worker's close acknowledgement.
            // Keep observing the actual owned request under the same deadline.
            while ((IsWindow(firstWindow) || closed.count != 0 || editorPending()) &&
                   juce::Time::getMillisecondCounter() < deadline)
            {
                dispatchMessages(6);
                pollPluginOperations();
                closed = probe();
            }
            pollPluginOperations();
            trace("close-completed", closed);
            if (IsWindow(firstWindow) || closed.count != 0 || editorPending() || !editorCompleted("close"))
                return juce::Result::fail("Native instrument editor close did not complete");
        }
        else
        {
            auto *first = instrumentWindows[selectedChannel].get();
            if (!first || !first->isVisible())
                return juce::Result::fail("Sampler window did not open");
            clickName();
            if (instrumentWindows[selectedChannel].get() != first)
                return juce::Result::fail("Sampler window duplicated");
            auto stream = output.getChildFile("sampler-window.png").createOutputStream();
            if (stream)
            {
                stream->setPosition(0);
                stream->truncate();
            }
            if (!stream || !juce::PNGImageFormat().writeImageToStream(
                               first->createComponentSnapshot(first->getLocalBounds()), *stream))
                return juce::Result::fail("Sampler snapshot failed");
            first->setVisible(false);
        }
        while (capture.blocks.load() < 32)
            dispatchMessages(6);
        capture.signalThreadShouldExit();
        capture.waitForThreadToExit(3000);
        double maxMs = capture.maximumMs;
        int silent = capture.silent;
        if (capture.peak < .001f)
            return juce::Result::fail("Opening fixture produced no musical PCM");
        auto after = audio.projectionDiagnostics();
        if (!audio.playing() || audio.positionBeat() < position || before.edit != after.edit ||
            device->starts != starts || device->stops != stops ||
            !beforeDocument.isEquivalentTo(controller.project.state()))
            return juce::Result::fail("Opening instrument changed playback/device/project");
        for (size_t i = 0; i < before.channels.size(); ++i)
            if (before.channels[i].graphInitialisations != after.channels[i].graphInitialisations)
                return juce::Result::fail("Opening instrument rebuilt graph");
        if (maxMs >= 256000. / 48000)
            return juce::Result::fail("Virtual callback overrun while instrument open");
        report += (mode ? "Pattern" : "Song") + juce::String(" playback: name-button handler opens/reuses ") +
                  (selected->instrument ? "actual native VST3 editor" : "sampler/settings window") +
                  "; document, graph and device unchanged; max callback " + juce::String(maxMs, 4) +
                  " ms; silent measurement blocks " + juce::String(silent) + ".\n";
        audio.stop();
    }
    output.getChildFile("rack-opening.txt")
        .replaceWithText(
            "PASS\n" + report +
            "Synthetic JUCE mouse-handler and virtual audio checks; native SDK visibility/identity checked "
            "for VST3. No OS mouse input, physical hardware or listening.\n");
    return juce::Result::ok();
#endif
}
juce::Result Studio::verifyMidiRecording(const juce::File &folder)
{
    customization = Customization::familiarDefaults();
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
    if (!device)
        return juce::Result::fail("GUI recording requires the virtual device");
    juce::AudioBuffer<float> buffer(2, 256);
    for (bool pattern : {true, false})
    {
        audio.stop();
        auto project = Project::empty();
        project.channels[0].source = "builtin:bass";
        project.channels[0].name = "BASS";
        project.patterns[0].length = pattern ? .25 : 4;
        clearScoreLog();
        controller.replace(project);
        selectedPattern = project.patterns[0].id;
        selectedChannel = project.channels[0].id;
        patternMode = pattern;
        mode.setButtonText(pattern ? "Pattern" : "Song");
        refresh();
        auto result = audio.rebuild(project, pattern, selectedPattern);
        if (result.failed())
            return result;
        needsRebuild = false;
        projectionError.clear();
        audio.setMidiTarget(selectedChannel);
        audio.seekBeat(0);
        dispatchMessages(20);
        for (int i = 0; i < 8; ++i)
        {
            device->process(buffer);
            dispatchMessages(1);
        }
        auto before = controller.project.state();
        keyPressed(juce::KeyPress('R'));
        dispatchMessages(20);
        if (!recordArmed || midiTaking)
            return juce::Result::fail("R did not arm recording while stopped");
        play.onClick();
        if (!midiTaking)
            return juce::Result::fail("Play did not begin armed note recording");
        for (int i = 0; i < 72; ++i)
        {
            if (i == 20)
                audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                          juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
            if (i == 45)
                audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
            device->process(buffer);
            dispatchMessages(1);
            drainMidiTake();
            if (i == 35)
            {
                if (midiPreview.empty() || !controller.project.state().isEquivalentTo(before))
                    return juce::Result::fail(
                        "Live MIDI preview must show captured notes without committing the project");
                showEditor(1);
                setSize(1600, 940);
                resized();
                auto output =
                    folder.getChildFile(pattern ? "live-recording-pattern.png" : "live-recording-song.png")
                        .createOutputStream();
                if (output)
                {
                    output->setPosition(0);
                    output->truncate();
                }
                if (!output || !juce::PNGImageFormat().writeImageToStream(
                                   createComponentSnapshot(getLocalBounds()), *output))
                    return juce::Result::fail("Live recording preview screenshot failed");
            }
        }
        stop.onClick();
        auto *p = controller.project.pattern(selectedPattern);
        if (midiTaking || recordArmed || !midiPreview.empty() || !p || p->notes.empty() ||
            (!pattern && controller.project.placements.size() != 1))
            return juce::Result::fail("Stop did not retain MIDI take and Song placement");
        auto retained = controller.project.state();
        keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
        if (!controller.project.state().isEquivalentTo(before))
            return juce::Result::fail("GUI recording undo was not atomic");
        keyPressed(
            juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0));
        if (!controller.project.state().isEquivalentTo(retained))
            return juce::Result::fail("GUI recording redo failed");
        result = audio.rebuild(controller.project, pattern, selectedPattern);
        if (result.failed())
            return result;
        needsRebuild = false;
        for (int i = 0; i < 8; ++i)
        {
            device->process(buffer);
            dispatchMessages(1);
        }
        record.onClick();
        play.onClick();
        device->process(buffer);
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 65, juce::uint8(90)));
        for (int i = 0; i < 4; ++i)
        {
            device->process(buffer);
            dispatchMessages(1);
        }
        keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
        if (midiTaking || !controller.project.state().isEquivalentTo(retained))
            return juce::Result::fail("Esc did not cancel recording without altering project");
        audio.midiInput.panic();
        audio.stop();
        auto saved =
            saveProject(controller.project, folder.getChildFile(pattern ? "gui-recorded-pattern.vibra"
                                                                        : "gui-recorded-song.vibra"));
        if (saved.failed())
            return saved;
    }
    showEditor(1);
    setSize(1600, 940);
    resized();
    auto stream = folder.getChildFile("midi-recorded-workspace.png").createOutputStream();
    if (stream)
    {
        stream->setPosition(0);
        stream->truncate();
    }
    if (!stream ||
        !juce::PNGImageFormat().writeImageToStream(createComponentSnapshot(getLocalBounds()), *stream))
        return juce::Result::fail("Recorded-workspace screenshot failed");
    folder.getChildFile("midi-recording-gui.txt")
        .replaceWithText("PASS: real R shortcut handler arms, Play button records injected MIDI, Stop "
                         "commits Pattern/Song take, Ctrl+Z/Ctrl+Shift+Z undo/redo complete take and "
                         "placement, Esc cancels held-note take.\n"
                         "Evidence: component render and synthetic virtual output. Not native OS input, "
                         "physical MIDI/audio or listening.\n");
    patternMode = true;
    mode.setButtonText("Pattern");
    return juce::Result::ok();
}
juce::Result Studio::verifyPianoNavigation(const juce::File &folder, const Project *fixture)
{
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    clearScoreLog();
    auto project = fixture ? *fixture : Project::empty();
    bool plugin = project.channels.front().instrument.has_value();
    play.setButtonText("Play");
    play.setToggleState(false, juce::dontSendNotification);
    for (auto &m : project.mixer)
        m.effects.clear();
    if (project.channels.size() < 2)
    {
        Channel ghost;
        ghost.id = project.allocate();
        ghost.name = "Ghost";
        ghost.source = "builtin:bass";
        project.channels.push_back(ghost);
    }
    if (!plugin)
        project.channels.front().source = "builtin:bass";
    auto &pat = project.patterns.front();
    pat.length = 8;
    pat.notes = {{project.allocate(), project.channels[0].id, 0, 1, 60, .8f, false},
                 {project.allocate(), project.channels[1].id, 3, 1, 62, .6f, false}};
    pat.autoLength = false; // This Piano Roll navigation/take fixture keeps its eight-beat window.
    selectedPattern = pat.id;
    selectedChannel = project.channels[0].id;
    patternMode = true;
    controller.replace(project);
    audio.invalidateProjection();
    auto deadline = juce::Time::getMillisecondCounter() + 15000;
    auto prepared = juce::Result::fail("Preparing");
    do
    {
        prepared = audio.rebuild(project, true, selectedPattern);
        if (prepared.wasOk())
            break;
        dispatchMessages(20);
    } while (prepared.getErrorMessage().contains("Preparing") &&
             juce::Time::getMillisecondCounter() < deadline);
    if (prepared.failed())
        return prepared;
    needsRebuild = false;
    audio.setMidiTarget(selectedChannel);
    audio.setMetronome(false);
    audio.setCountInBars(0);
    maximiseEditor(1);
    refresh();
    auto *editor = static_cast<Editor *>(windows[1]->getContentComponent());
    editor->viewAction("piano.fit");
    auto *device = dynamic_cast<SyntheticDevice *>(
        audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice());
    if (!device)
        return juce::Result::fail("Piano test requires virtual device");
    juce::AudioBuffer<float> block(2, 256);
    auto tick = [&](int count)
    {
        float peak = 0;
        for (int i = 0; i < count; ++i)
        {
            block.clear();
            device->process(block);
            peak = std::max(peak, block.getMagnitude(0, 256));
            dispatchMessages(plugin ? 6 : 1);
        }
        return peak;
    };
    tick(plugin ? 180 : 8);
    clearScoreLog();
    auto original = controller.project.state();
    auto graph = audio.projectionDiagnostics();
    int starts = device->starts, stops = device->stops;
    auto event = [&](juce::Component *target, juce::Point<float> point, juce::Point<float> start, int flags,
                     int clicks = 1)
    {
        auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(flags), 1, 0, 0, 0, 0, target, target, now, start, now,
                                clicks, point != start);
    };
    auto key = juce::Point<float>{20, editor->keyY(60)};
    editor->mouseDown(event(editor, key, key, juce::ModifierKeys::leftButtonModifier));
    auto previewPeak = tick(24);
    auto previewDiagnostics = audio.projectionDiagnostics();
    folder.getChildFile("piano-preview-diagnostics.txt")
        .replaceWithText(
            "peak=" + juce::String(previewPeak, 8) + " keyY=" + juce::String(key.y) +
            " previewPitch=" + juce::String(previewPitch) + " input=" + audio.typingInput.status() +
            " transport=" + juce::String(int(audio.playing())) +
            " pluginStatus=" + (plugin ? audio.pluginStatus(project.channels[0].instrument->id) : "builtin") +
            " renderCalls=" + juce::String(juce::int64(previewDiagnostics.channels[0].renderCalls)) + "\n");
    if (previewPeak <= .001f || audio.playing() || !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Preview must sound actual instrument without transport/document edits");
    drainScoreLog();
    if (!scoreLog.dump(juce::Time::getMillisecondCounterHiRes() / 1000., 180, 120, project).empty())
        return juce::Result::fail("Preview polluted score history");
    typingKeys.enabled = true;
    keyPressed(juce::KeyPress('Z'));
    tick(8);
    editor->mouseUp(event(editor, key, key, juce::ModifierKeys::leftButtonModifier));
    if (tick(24) <= .001f)
        return juce::Result::fail("Preview release cut held typing note");
    releaseTypingNotes();
    typingKeys.enabled = false;
    tick(plugin ? 180 : 12);
    if (!plugin && tick(4) != 0)
        return juce::Result::fail("Preview release left stuck note");
    focus = Scope::piano;
    auto beforeZoom = editor->visibleBeatCount();
    keyPressed(juce::KeyPress(juce::KeyPress::pageUpKey));
    if (editor->visibleBeatCount() >= beforeZoom || !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Page Up view-only zoom");
    editor->viewAction("piano.fit");
    auto wheelEvent = event(editor, {300, 100}, {300, 100}, juce::ModifierKeys::ctrlModifier);
    juce::MouseWheelDetails wheel{};
    wheel.deltaY = 1;
    auto beat = editor->beatUnder(300);
    editor->mouseWheelMove(wheelEvent, wheel);
    if (std::abs(editor->beatUnder(300) - beat) > 1.e-8)
        return juce::Result::fail("Wheel zoom lost cursor beat");
    editor->viewAction("piano.fit");
    for (int i = 0; i < editor->getNumChildComponents(); ++i)
        if (auto *bar = dynamic_cast<ViewportBar *>(editor->getChildComponent(i));
            bar && bar->getWidth() > bar->getHeight())
        {
            auto range = bar->read();
            float edge = float(bar->getWidth() * range[1] / range[2] - 2);
            auto start = juce::Point<float>{edge, 8};
            bar->mouseDown(event(bar, start, start, juce::ModifierKeys::leftButtonModifier));
            bar->mouseDrag(event(bar, {edge - 30, 8}, start, juce::ModifierKeys::leftButtonModifier));
            if (editor->visibleBeatCount() >= range[1])
                return juce::Result::fail("Thumb edge zoom");
        }
    editor->viewAction("piano.fit");
    auto ghostId = controller.project.pattern(selectedPattern)->notes.back().id;
    auto pitchStart = juce::Point<float>{20, editor->keyY(60)};
    auto pitchEnd = juce::Point<float>{20, editor->keyY(62)};
    auto pitchModifiers = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::leftButtonModifier;
    auto pitchInput = audio.typingInput.status();
    editor->mouseDown(event(editor, pitchStart, pitchStart, pitchModifiers));
    editor->mouseDrag(event(editor, pitchEnd, pitchStart, pitchModifiers));
    editor->mouseUp(event(editor, pitchEnd, pitchStart, pitchModifiers));
    if (selectedNotes != std::vector<Id>{controller.project.pattern(selectedPattern)->notes.front().id} ||
        audio.typingInput.status() != pitchInput || !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Pitch selection auditioned or selected readonly ghost notes");
    auto additivePitch = juce::Point<float>{20, editor->keyY(61)};
    auto addModifiers = pitchModifiers | juce::ModifierKeys::shiftModifier;
    editor->mouseDown(event(editor, additivePitch, additivePitch, addModifiers));
    editor->mouseDrag(event(editor, pitchEnd, additivePitch, addModifiers));
    editor->mouseUp(event(editor, pitchEnd, additivePitch, addModifiers));
    if (selectedNotes.size() != 1 || audio.typingInput.status() != pitchInput)
        return juce::Result::fail("Additive pitch selection lost existing selection or auditioned");
    editor->viewAction("piano.editGhosts");
    editor->mouseDown(event(editor, additivePitch, additivePitch, pitchModifiers));
    editor->mouseDrag(event(editor, pitchEnd, additivePitch, pitchModifiers));
    editor->mouseUp(event(editor, pitchEnd, additivePitch, pitchModifiers));
    if (selectedNotes != std::vector<Id>{ghostId} || selectedChannel != project.channels[0].id)
        return juce::Result::fail("Editable ghost pitch range selected the wrong Channel");
    editor->viewAction("piano.editGhosts");
    auto ownRect = editor->noteBounds(controller.project.pattern(selectedPattern)->notes.front());
    auto timeFirst = juce::Point<float>{ownRect.getX(), 38};
    auto timeLast = juce::Point<float>{ownRect.getRight() + ownRect.getWidth() * .5f, 38};
    auto timeModifiers = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::leftButtonModifier;
    auto cursor = audio.positionBeat();
    editor->mouseDown(event(editor, timeFirst, timeFirst, juce::ModifierKeys::leftButtonModifier, 2));
    editor->mouseDoubleClick(event(editor, timeFirst, timeFirst, juce::ModifierKeys::leftButtonModifier, 2));
    editor->mouseDrag(event(editor, timeLast, timeFirst, juce::ModifierKeys::leftButtonModifier, 2));
    editor->mouseUp(event(editor, timeLast, timeFirst, juce::ModifierKeys::leftButtonModifier, 2));
    if (editor->repeatInterval() != 1.5 || audio.positionBeat() != cursor ||
        !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Double-click ruler did not start a view-only time selection");
    editor->mouseDown(event(editor, timeFirst, timeFirst, timeModifiers));
    editor->mouseDrag(event(editor, timeLast, timeFirst, timeModifiers));
    editor->mouseUp(event(editor, timeLast, timeFirst, timeModifiers));
    if (selectedNotes != std::vector<Id>{controller.project.pattern(selectedPattern)->notes.front().id} ||
        audio.positionBeat() != cursor || !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Timeline selection selected wrong notes or changed cursor/document");
    auto sourceNote = controller.project.pattern(selectedPattern)->notes.front();
    keyPressed(juce::KeyPress('B', juce::ModifierKeys::ctrlModifier, 0));
    auto firstRepeat = controller.project.state();
    auto &firstCopy = controller.project.pattern(selectedPattern)->notes.back();
    if (controller.project.pattern(selectedPattern)->notes.size() != 3 || firstCopy.beat != 1.5 ||
        firstCopy.channel != sourceNote.channel || firstCopy.pitch != sourceNote.pitch ||
        firstCopy.length != sourceNote.length || firstCopy.velocity != sourceNote.velocity ||
        selectedNotes != std::vector<Id>{firstCopy.id})
        return juce::Result::fail("Ctrl+B ignored timeline interval or copied wrong notes");
    keyPressed(juce::KeyPress('B', juce::ModifierKeys::ctrlModifier, 0));
    auto secondRepeat = controller.project.state();
    if (controller.project.pattern(selectedPattern)->notes.size() != 4 ||
        controller.project.pattern(selectedPattern)->notes.back().beat != 3.)
        return juce::Result::fail("Repeated Ctrl+B used an incorrect interval");
    controller.undoEdit();
    if (!firstRepeat.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Timeline repeat was not one undo");
    controller.redoEdit();
    if (!secondRepeat.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Timeline repeat redo changed the score");
    controller.undoEdit();
    controller.undoEdit();
    if (!original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Timeline repeats did not restore original score");
    selectedNotes = {sourceNote.id};
    keyPressed(juce::KeyPress('5', juce::ModifierKeys::shiftModifier, 0));
    if (std::abs(editor->visibleBeatCount() - 1.5) > .02 || audio.positionBeat() != cursor ||
        !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Shift+5 did not zoom to actual timeline selection");
    {
        auto timelineImage = folder.getChildFile("piano-timeline-selection.png").createOutputStream();
        if (!timelineImage)
            return juce::Result::fail("Timeline selection capture could not open output");
        timelineImage->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(createComponentSnapshot(getLocalBounds()),
                                                       *timelineImage))
            return juce::Result::fail("Timeline selection capture could not encode output");
    }
    editor->mouseDown(event(editor, timeFirst, timeFirst, timeModifiers));
    editor->mouseUp(event(editor, timeFirst, timeFirst, timeModifiers));
    auto noSelectionRange = editor->visibleBeatCount();
    keyPressed(juce::KeyPress('5', juce::ModifierKeys::shiftModifier, 0));
    if (editor->visibleBeatCount() != noSelectionRange || !selectedNotes.empty())
        return juce::Result::fail("Shift+5 without selection changed view");
    CommandArgs meter;
    meter.numerator = 6;
    meter.denominator = 8;
    if (controller.execute("project.meter", meter).failed())
        return juce::Result::fail("Repeat meter fixture");
    selectedNotes = {sourceNote.id};
    keyPressed(juce::KeyPress('B', juce::ModifierKeys::ctrlModifier, 0));
    if (controller.project.pattern(selectedPattern)->notes.back().beat != 3.)
        return juce::Result::fail("No-range duplication assumed a four-quarter-note bar in 6/8");
    controller.undoEdit();
    controller.undoEdit();
    if (!original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Meter repeat fixture failed to restore original project");
    selectedNotes.clear();
    editor->viewAction("piano.fit");
    auto fullRange = editor->visibleBeatCount();
    auto regionStart = juce::Point<float>{180, 150};
    auto regionEnd = juce::Point<float>{520, 300};
    auto regionModifiers = juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::rightButtonModifier;
    editor->mouseDown(event(editor, regionStart, regionStart, regionModifiers));
    editor->mouseDrag(event(editor, regionEnd, regionStart, regionModifiers));
    editor->mouseUp(event(editor, regionEnd, regionStart, regionModifiers));
    if (editor->visibleBeatCount() >= fullRange || !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Region zoom changed score or did not zoom");
    editor->mouseDown(event(editor, regionStart, regionStart, regionModifiers));
    editor->mouseUp(event(editor, regionStart, regionStart, regionModifiers));
    if (std::abs(editor->visibleBeatCount() - fullRange) > 1.e-8 ||
        !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Region unzoom did not restore view");
    focus = Scope::rack;
    editor->viewAction("piano.fit");
    if (focus != Scope::piano)
        return juce::Result::fail("Piano navigation lost command context");
    audio.playStop();
    tick(4);
    auto liveCursor = audio.positionBeat();
    auto liveInputStatus = audio.typingInput.status();
    auto liveKey = juce::Point<float>{20, editor->keyY(60)};
    editor->mouseDown(event(editor, liveKey, liveKey, juce::ModifierKeys::leftButtonModifier));
    editor->mouseUp(event(editor, liveKey, liveKey, juce::ModifierKeys::leftButtonModifier));
    editor->mouseDown(event(editor, timeFirst, timeFirst, timeModifiers));
    editor->mouseDrag(event(editor, timeLast, timeFirst, timeModifiers));
    editor->mouseUp(event(editor, timeLast, timeFirst, timeModifiers));
    keyPressed(juce::KeyPress('5', juce::ModifierKeys::shiftModifier, 0));
    if (!audio.playing() || audio.positionBeat() != liveCursor || previewPitch >= 0 ||
        audio.typingInput.status() != liveInputStatus || !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Live navigation sought/stopped transport or preview sent MIDI");
    tick(8);
    if (!audio.playing())
        return juce::Result::fail("Live ruler navigation interrupted transport");
    audio.stop();
    editor->mouseDown(event(editor, timeFirst, timeFirst, timeModifiers));
    editor->mouseUp(event(editor, timeFirst, timeFirst, timeModifiers));
    editor->viewAction("piano.fit");
    auto ghostPoint =
        editor->noteBounds(controller.project.pattern(selectedPattern)->notes.back()).getCentre();
    if (editor->noteAt(ghostPoint) != 0)
        return juce::Result::fail("Readonly ghost intercepted edit");
    keyPressed(juce::KeyPress('V', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0));
    if (editor->noteAt(ghostPoint) != ghostId)
        return juce::Result::fail("Editable ghost hit target");
    editor->mouseDown(event(editor, ghostPoint, ghostPoint, juce::ModifierKeys::leftButtonModifier));
    editor->mouseDrag(
        event(editor, ghostPoint.translated(50, 0), ghostPoint, juce::ModifierKeys::leftButtonModifier));
    editor->mouseUp(
        event(editor, ghostPoint.translated(50, 0), ghostPoint, juce::ModifierKeys::leftButtonModifier));
    auto &moved = controller.project.pattern(selectedPattern)->notes.back();
    if (moved.channel != project.channels[1].id || moved.beat == 3)
        return juce::Result::fail("Ghost drag changed wrong Channel or did not move");
    controller.undoEdit();
    if (!original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Ghost one undo");
    editor->viewAction("piano.editGhosts");
    editor->mouseDoubleClick(
        event(editor, ghostPoint, ghostPoint, juce::ModifierKeys::rightButtonModifier, 2));
    if (selectedChannel != project.channels[1].id || !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Ghost double-right target switch");
    auto otherGhost =
        editor->noteBounds(controller.project.pattern(selectedPattern)->notes.front()).getCentre();
    editor->mouseDown(event(editor, otherGhost, otherGhost, juce::ModifierKeys::backButtonModifier));
    editor->mouseUp(event(editor, otherGhost, otherGhost, juce::ModifierKeys::backButtonModifier));
    if (selectedChannel != project.channels[0].id || !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("X1 ghost switch changed document or missed owning Channel");
    editor->mouseDown(event(editor, timeFirst, timeFirst, timeModifiers));
    editor->mouseDrag(event(editor, timeLast, timeFirst, timeModifiers));
    editor->mouseUp(event(editor, timeLast, timeFirst, timeModifiers));
    if (editor->repeatInterval() <= 0)
        return juce::Result::fail("Project replacement view fixture did not select a range");
    controller.replace(project);
    if (editor->repeatInterval() != 0 || !original.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("New project reused stale ruler selection through matching IDs");
    auto after = audio.projectionDiagnostics();
    if (graph.edit != after.edit ||
        graph.channels[0].graphInitialisations != after.channels[0].graphInitialisations ||
        starts != device->starts || stops != device->stops)
        return juce::Result::fail("Piano UI changed graph/device");
    needsRebuild = false;
    projectionError.clear();
    play.setButtonText(audio.playing() ? "Pause" : "Play");
    play.setToggleState(audio.playing(), juce::dontSendNotification);
    mode.setButtonText(patternMode ? "Pattern" : "Song");
    status = "Piano Roll audition/navigation acceptance passed";
    auto stream = folder.getChildFile(plugin ? "piano-vst3-workspace.png" : "piano-navigation-workspace.png")
                      .createOutputStream();
    if (!stream)
        return juce::Result::fail("Piano capture");
    stream->truncate();
    juce::PNGImageFormat{}.writeImageToStream(createComponentSnapshot(getLocalBounds()), *stream);
    stream.reset();
    folder.getChildFile(plugin ? "piano-vst3-gui.txt" : "piano-navigation-gui.txt")
        .replaceWithText(
            "PASS: actual preview/typing ownership and instrument PCM, no audition score "
            "mutation, Page Up/wheel anchor/thumb-edge/region zoom and unzoom, ruler "
            "selection/Shift+5/no-selection no-op/context ownership, editable ghost original-Channel "
            "drag/undo/double-right/X1 switch; live navigation preserves transport and live preview "
            "is disabled; graph/device retained. Virtual device and JUCE "
            "handlers/component render, no OS input/hardware/listening.\n");
    maximisedEditor = -1;
    resized();
    startTimerHz(20);
    return verifyPianoLoopPreview(folder, fixture);
}
#include "PianoLoopViewAcceptance.inc"
juce::Result Studio::runPianoNavigationTest(const juce::File &projectFile, const juce::File &output)
{
    output.createDirectory();
    Project fixture;
    auto loaded = loadProject(projectFile, fixture);
    if (loaded.failed())
        return loaded;
    auto setup = verifyMidiRecording(output);
    if (setup.failed())
        return setup;
    return verifyPianoNavigation(output, &fixture);
}
juce::Result Studio::verifyScoreLog(const juce::File &folder)
{
    stopTimer();
    audio.stop();
    clearScoreLog();
    auto old = controller.project.state();
    auto originalCount = controller.project.pattern(selectedPattern)->notes.size();
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
    dispatchMessages(25);
    audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
    typingKeys.enabled = true;
    keyPressed(juce::KeyPress('Q'));
    dispatchMessages(25);
    releaseTypingNotes();
    typingKeys.enabled = false;
    drainScoreLog();
    if (!old.isEquivalentTo(controller.project.state()) || audio.playing())
        return juce::Result::fail("Unarmed stopped score logging must not edit/start transport");
    customization.bind(
        "score.dump", Scope::global,
        juce::KeyPress('D', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0));
    if (!keyPressed(
            juce::KeyPress('D', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0)))
        return juce::Result::fail("Custom score dump shortcut");
    if (controller.project.pattern(selectedPattern)->notes.size() != originalCount + 2)
        return juce::Result::fail("Stopped physical and typing log dump");
    controller.undoEdit();
    if (!old.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Score dump single undo");
    controller.redoEdit();
    auto saved = saveProject(controller.project, folder.getChildFile("score-history-dumped.vibra"));
    if (saved.failed())
        return saved;
    auto png = folder.getChildFile("score-logger-workspace.png").createOutputStream();
    if (!png)
        return juce::Result::fail("Score snapshot output");
    png->truncate();
    juce::PNGImageFormat{}.writeImageToStream(createComponentSnapshot(getLocalBounds()), *png);
    png.reset();
    clearScoreLog();
    auto committed = controller.project.state();
    dumpScoreLog();
    if (!committed.isEquivalentTo(controller.project.state()))
        return juce::Result::fail("Empty log should preserve project");
    folder.getChildFile("score-logger-gui.txt")
        .replaceWithText("PASS: unarmed stopped physical/typing input to score history, custom dump "
                         "shortcut, canonical one undo/redo, save and empty clear. Actual handlers with "
                         "injected MIDI; component render, no OS input/hardware/listening.\n");
    startTimerHz(20);
    return juce::Result::ok();
}
juce::Result Studio::verifyTypingKeyboard(const juce::File &folder)
{
    stopTimer();
    auto &manager = audio.engine.getDeviceManager();
    auto *device = dynamic_cast<SyntheticDevice *>(manager.deviceManager.getCurrentAudioDevice());
    if (!device)
        return juce::Result::fail("Typing GUI requires the existing virtual device");
    audio.stop();
    audio.setCountInBars(0);
    audio.setMetronome(false);
    clearScoreLog();
    controller.replace(Project::empty());
    auto &p = controller.project;
    p.channels[0].source = "builtin:bass";
    selectedChannel = p.channels[0].id;
    selectedPattern = p.patterns[0].id;
    patternMode = true;
    auto result = audio.rebuild(p, true, selectedPattern);
    if (result.failed())
        return result;
    needsRebuild = false;
    refresh();
    audio.setMidiTarget(selectedChannel);
    juce::AudioBuffer<float> block(2, 256);
    auto tick = [&](int count)
    {
        float peak = 0;
        for (int i = 0; i < count; ++i)
        {
            block.clear();
            device->process(block);
            peak = std::max(peak, block.getMagnitude(0, 256));
            dispatchMessages(1);
        }
        return peak;
    };
    tick(8);
    if (!keyPressed(juce::KeyPress('T', juce::ModifierKeys::ctrlModifier, 0)))
        return juce::Result::fail("Ctrl+T handler");
    dispatchMessages(20);
    if (!typingKeys.enabled)
        return juce::Result::fail("Ctrl+T did not enable typing input");
    auto old = p.state();
    if (!keyPressed(juce::KeyPress('Z')) || tick(8) <= .001f || audio.playing() ||
        !old.isEquivalentTo(p.state()))
        return juce::Result::fail(
            "Typing key should monitor instrument without editing or starting transport");
    keyPressed(juce::KeyPress('Z')); // Repeat.
    releaseTypingNotes();
    tick(12);
    if (tick(4) != 0)
        return juce::Result::fail("Typing focus release left stuck note");
    record.onClick();
    play.onClick();
    tick(8);
    keyPressed(juce::KeyPress('Z'));
    tick(24);
    typingKeys.release('Z');
    tick(8);
    drainMidiTake();
    if (liveTakeNotes().empty() || !old.isEquivalentTo(p.state()))
        return juce::Result::fail("Typing MIDI must preview before canonical commit");
    stop.onClick();
    if (p.patterns[0].notes.size() != 1 || p.patterns[0].notes[0].pitch != 60)
        return juce::Result::fail("Typing recording should commit one note despite key repeat");
    keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier, 0));
    if (!old.isEquivalentTo(p.state()))
        return juce::Result::fail("Typing take undo");
    keyPressed(juce::KeyPress('Z', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0));
    if (p.patterns[0].notes.size() != 1)
        return juce::Result::fail("Typing take redo");
    auto saved = saveProject(p, folder.getChildFile("typing-recorded.vibra"));
    if (saved.failed())
        return saved;
    typingKeys.enabled = false;
    refresh();
    folder.getChildFile("typing-keyboard-gui.txt")
        .replaceWithText("PASS: actual Ctrl+T/Z/undo/redo and Rec/Play/Stop handlers; virtual built-in PCM, "
                         "repeat suppression, focus release, pending preview, one-note take persistence. No "
                         "OS input or listening.\n");
    startTimerHz(20);
    return juce::Result::ok();
}
juce::Result Studio::verifyCountInRecording(const juce::File &folder)
{
    auto *device = dynamic_cast<SyntheticDevice *>(
        audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice());
    if (!device)
        return juce::Result::fail("Count-in GUI test requires virtual output");
    audio.stop();
    audio.setCountInBars(0);
    audio.setMetronome(false);
    audio.setGuideGain(.65f);
    auto project = Project::empty();
    project.channels[0].source = "builtin:bass";
    project.channels[0].name = "BASS";
    project.numerator = 7;
    project.denominator = 8;
    project.patterns[0].length = 3.5;
    clearScoreLog();
    controller.replace(project);
    selectedPattern = project.patterns[0].id;
    selectedChannel = project.channels[0].id;
    patternMode = true;
    mode.setButtonText("Pattern");
    refresh();
    auto prepared = audio.rebuild(project, true, selectedPattern);
    if (prepared.failed())
        return prepared;
    needsRebuild = false;
    projectionError.clear();
    audio.setMidiTarget(selectedChannel);
    audio.seekBeat(0);
    juce::AudioBuffer<float> buffer(2, 256);
    for (int i = 0; i < 8; ++i)
    {
        device->process(buffer);
        dispatchMessages(1);
    }
    keyPressed(juce::KeyPress('P', juce::ModifierKeys::ctrlModifier, 0));
    dispatchMessages(20);
    if (audio.countInBars() != 1)
        return juce::Result::fail("Ctrl+P must enable one-bar countdown");
    keyPressed(juce::KeyPress('M', juce::ModifierKeys::ctrlModifier, 0));
    dispatchMessages(20);
    if (!audio.metronome())
        return juce::Result::fail("Ctrl+M must toggle metronome");
    keyPressed(juce::KeyPress('M', juce::ModifierKeys::ctrlModifier, 0));
    dispatchMessages(20);
    auto original = controller.project.state();
    auto hits = audio.guideHits();
    record.onClick();
    play.onClick();
    for (int i = 0; i < 430; ++i)
    {
        if (i == 40)
            audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                      juce::MidiMessage::noteOn(1, 60, juce::uint8(100)));
        if (i == 60)
            audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 60));
        if (i == 350)
            audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                      juce::MidiMessage::noteOn(1, 65, juce::uint8(90)));
        if (i == 390)
            audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, 65));
        device->process(buffer);
        dispatchMessages(1);
        drainMidiTake();
        if (i == 80)
        {
            if (!midiPreview.empty() || audio.countInRemaining() <= 0 ||
                !controller.project.state().isEquivalentTo(original))
            {
                MidiClock::Position position;
                const bool clockAvailable = audio.midiInput.recordingPosition(position);
                folder.getChildFile("count-in-failure.txt")
                    .replaceWithText("preview=" + juce::String(int(midiPreview.size())) + " remaining=" +
                                     juce::String(audio.countInRemaining(), 9) + " projectUnchanged=" +
                                     juce::String(int(controller.project.state().isEquivalentTo(original))) +
                                     " clockAvailable=" + juce::String(int(clockAvailable)) +
                                     " clock=" + juce::String(position.seconds, 9) + " head=" +
                                     juce::String(audio.positionBeat(), 9) + " status=" + status + "\n");
                return juce::Result::fail("Count-in must monitor without storing early MIDI");
            }
            timerCallback();
            setSize(1600, 940);
            resized();
            auto stream = folder.getChildFile("count-in-workspace.png").createOutputStream();
            if (stream)
            {
                stream->setPosition(0);
                stream->truncate();
            }
            if (!stream || !juce::PNGImageFormat().writeImageToStream(
                               createComponentSnapshot(getLocalBounds()), *stream))
                return juce::Result::fail("Countdown screenshot failed");
        }
    }
    MidiClock::Position observed;
    audio.midiInput.recordingPosition(observed);
    folder.getChildFile("count-in-diagnostic.txt")
        .replaceWithText("hits=" + juce::String(juce::int64(audio.guideHits() - hits)) +
                         " preview=" + juce::String(int(midiPreview.size())) +
                         " remaining=" + juce::String(audio.countInRemaining(), 6) +
                         " clock=" + juce::String(observed.seconds, 6) +
                         " position=" + juce::String(audio.positionBeat(), 6) +
                         " metronome=" + juce::String(int(audio.metronome())) + "\n");
    if (audio.guideHits() - hits != 7 || midiPreview.empty())
        return juce::Result::fail("7/8 lead-in must click seven times and preview post-boundary input");
    stop.onClick();
    auto *pattern = controller.project.pattern(selectedPattern);
    if (!pattern || pattern->notes.size() != 1 || pattern->notes[0].pitch != 65 ||
        pattern->notes[0].beat < .15 || pattern->notes[0].beat > .3)
        return juce::Result::fail("Only post-count note and correct relative timing may be committed");
    auto kept = controller.project.state();
    audio.setCountInBars(2);
    record.onClick();
    play.onClick();
    for (int i = 0; i < 20; ++i)
    {
        device->process(buffer);
        dispatchMessages(1);
    }
    keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    if (audio.playing() || midiTaking || !midiPreview.empty() ||
        !controller.project.state().isEquivalentTo(kept))
        return juce::Result::fail("Esc must cancel countdown without changing the project");
    audio.setCountInBars(0);
    audio.setMetronome(false);
    audio.midiInput.panic();
    refresh();
    folder.getChildFile("count-in-recording-gui.txt")
        .replaceWithText(
            "PASS: Ctrl+M/Ctrl+P handlers, seven eighth-note pulses, early MIDI monitoring without capture, "
            "live post-boundary notes and relative timing, two-bar countdown cancellation. Virtual "
            "audio/synthetic JUCE handlers and component capture; no physical/native OS input/listening.\n");
    return juce::Result::ok();
}
juce::Result Studio::runCoProducerIntegrationTest(const juce::File &folder,
                                                  const juce::File &instrumentFixture, bool anticipation)
{
    folder.createDirectory();
    std::optional<PluginSpec> testInstrument;
    if (instrumentFixture != juce::File{})
    {
        Project fixture;
        auto loaded = loadProject(instrumentFixture, fixture);
        if (loaded.failed())
            return loaded;
        for (const auto &channel : fixture.channels)
            if (channel.instrument && channel.instrument->instrument && !channel.instrument->bypass)
            {
                testInstrument = *channel.instrument;
                break;
            }
        if (!testInstrument)
            return juce::Result::fail("CoProducer instrument fixture has no enabled instrument.");
    }
    auto setup = verifyMidiRecording(folder);
    if (setup.failed())
        return setup;
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    audio.midiInput.panic();
    audio.typingInput.panic();
    controller.replace(Project::empty());
    selectedPattern = controller.project.patterns.front().id;
    selectedChannel = controller.project.channels.front().id;
    selectedNotes.clear();
    selectedClips.clear();
    patternMode = true;
    audio.setMetronome(false);
    audio.setCountInBars(0);
    audio.setLooping(true);
    auto prepared = audio.rebuild(controller.project, true, selectedPattern);
    if (prepared.failed())
        return prepared;
    needsRebuild = false;
    engineRevision = controller.revision;
    refresh();
    resized();
    auto *device = dynamic_cast<SyntheticDevice *>(
        audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice());
    if (!device)
        return juce::Result::fail("CoProducer integration requires the synthetic device.");
    juce::AudioBuffer<float> block(2, 256);
    double maximumCallbackMs = 0;
    int callbacks = 0, overruns = 0;
    auto tick = [&]
    {
        block.clear();
        const auto begin = juce::Time::getMillisecondCounterHiRes();
        device->process(block);
        const auto elapsed = juce::Time::getMillisecondCounterHiRes() - begin;
        maximumCallbackMs = std::max(maximumCallbackMs, elapsed);
        ++callbacks;
        overruns += elapsed > 256.0 / 48000 * 1000 ? 1 : 0;
        dispatchMessages(6);
        return block.getMagnitude(0, 256);
    };
    auto result = juce::Result::ok();
    for (bool usePattern : {true, false})
        for (bool replaceForTest : {false, true})
        {
            // Consume pending undo/redo publications before switching the disposable scenario.
            for (int drain = 0; drain < 12; ++drain)
                tick();
            controller.replace(Project::empty());
            selectedPattern = controller.project.patterns.front().id;
            selectedChannel = controller.project.channels.front().id;
            selectedNotes.clear();
            selectedClips.clear();
            patternMode = usePattern;
            if (testInstrument)
            {
                const auto instrumentId = controller.project.allocate();
                for (auto &channel : controller.project.channels)
                    if (channel.source == "builtin:bass")
                    {
                        channel.instrument = *testInstrument;
                        channel.instrument->id = instrumentId;
                    }
            }
            if (!usePattern)
            {
                controller.project.patterns.front().length = 16;
                controller.project.placements.push_back(
                    {controller.project.allocate(), selectedPattern, 0, 0, 16, 0});
            }
            if (replaceForTest)
            {
                auto &pattern = controller.project.patterns.front();
                pattern.length = 16;
                Id destination = 0;
                for (const auto &channel : controller.project.channels)
                    if (channel.source == "builtin:bass")
                        destination = channel.id;
                pattern.notes.push_back({controller.project.allocate(), destination, usePattern ? 0.0 : 3.0,
                                         usePattern ? 6.0 : 7.0, 60, .65f, false});
                pattern.notes.push_back({controller.project.allocate(),
                                         controller.project.channels.front().id, 1, 1, 60, .4f, false});
                if (!usePattern)
                {
                    selectedClips = {controller.project.placements.front().id};
                    controller.project.placements.push_back(
                        {controller.project.allocate(), selectedPattern, 1, 20, 16, 0});
                }
            }
            if (anticipation)
            {
                // Authored multi-voice MIDI fixture. Declared GM identities are model controls,
                // not a claim that these independent melodic samplers reproduce GM timbres.
                auto &pattern = controller.project.patterns.front();
                pattern.length = 16;
                for (int index = 0; index < 3; ++index)
                    controller.project.channels[size_t(index)].source = "builtin:bass";
                auto note = [&](int channel, double beat, double length, int pitch)
                {
                    pattern.notes.push_back({controller.project.allocate(),
                                             controller.project.channels[size_t(channel)].id, beat, length,
                                             pitch, .8f, false});
                };
                for (int pitch : {60, 64, 67})
                    note(0, 4, 2, pitch);
                for (int pitch : {65, 69, 72})
                    note(0, 6, 2, pitch);
                note(1, 5, .6, 43);
                note(3, 1, 1, 43);
                note(3, 2.5, 1, 48);
                note(2, 10, 1, 67);
            }
            const auto preparationDeadline = juce::Time::getMillisecondCounterHiRes() + 20000;
            result = audio.rebuild(controller.project, usePattern, selectedPattern);
            while (result.failed() && result.getErrorMessage() == "Preparing VST3 workers..." &&
                   juce::Time::getMillisecondCounterHiRes() < preparationDeadline)
            {
                tick();
                result = audio.rebuild(controller.project, usePattern, selectedPattern);
            }
            if (result.failed())
                return result;
            needsRebuild = false;
            engineRevision = controller.revision;
            refresh();
            resized();
            // Settle the stopped scenario switch before measuring live candidate edits.
            for (int settle = 0; settle < 80; ++settle)
                tick();
            const auto scenario = folder.getChildFile(juce::String(usePattern ? "pattern" : "song") +
                                                      (replaceForTest ? "-replace" : ""));
            scenario.createDirectory();
            if (testInstrument && audio.pluginDiagnostics().empty())
                for (const auto &channel : controller.project.channels)
                    if (channel.instrument)
                        return juce::Result::fail("Actual CoProducer instrument unavailable: " +
                                                  audio.pluginStatus(channel.instrument->id));
            const auto deviceStarts = device->starts, deviceStops = device->stops;
            CoProducerPanel panel(*this);
            result = panel.verifyGenerationForTest(scenario, tick, replaceForTest, anticipation);
            juce::String hostReport;
            for (const auto &host : audio.pluginDiagnostics())
                hostReport += "Host " + juce::String(host.id) + ", alive=" + juce::String(int(host.alive)) +
                              ", latency=" + juce::String(host.latency) +
                              ", submitted=" + juce::String(juce::int64(host.packets.submitted)) +
                              ", missed=" + juce::String(juce::int64(host.packets.missed)) +
                              ", dropped=" + juce::String(juce::int64(host.packets.dropped)) +
                              ", midiOverflow=" + juce::String(juce::int64(host.packets.midiOverflow)) + "\n";
            for (const auto &channel : audio.projectionDiagnostics().channels)
                hostReport += "Channel " + juce::String(channel.channel) +
                              ", renders=" + juce::String(juce::int64(channel.renderCalls)) +
                              ", lastPlaying=" + juce::String(int(channel.lastPlaying)) +
                              ", lastTime=" + juce::String(channel.lastTime, 4) + "\n";
            scenario.getChildFile("host-diagnostics.txt").replaceWithText(hostReport);
            if (result.failed())
                return result;
            if (testInstrument)
            {
                const auto hosts = audio.pluginDiagnostics();
                if (hosts.empty() ||
                    !std::all_of(hosts.begin(), hosts.end(), [](const auto &host) { return host.alive; }))
                    return juce::Result::fail("CoProducer actual instrument worker did not remain alive.");
                scenario.getChildFile("actual-instrument.txt")
                    .replaceWithText("Actual hosted instrument: " + testInstrument->name +
                                     "; descriptor SHA256 " +
                                     juce::SHA256(testInstrument->descriptor.toRawUTF8(),
                                                  size_t(testInstrument->descriptor.getNumBytesAsUTF8()))
                                         .toHexString() +
                                     ". Native generation/audition/acceptance handlers and virtual plugin "
                                     "PCM only; no OS input/hardware/listening.\n");
            }
            if (device->starts != deviceStarts || device->stops != deviceStops)
                return juce::Result::fail("CoProducer generation/audition restarted the output device.");
        }
    folder.getChildFile("inference-playback-measurements.txt")
        .replaceWithText("Virtual synchronous callbacks: " + juce::String(callbacks) + ", maximum " +
                         juce::String(maximumCallbackMs, 4) + " ms, " + juce::String(overruns) +
                         " overruns versus 5.3333 ms. Includes actual local inference and audition; not "
                         "hardware latency/listening evidence.\n");
    if (result.wasOk() && overruns != 0)
        return juce::Result::fail(
            "Model integration exceeded a virtual callback deadline; inspect measurements.");
    return result;
}
juce::Result Studio::finishRoutingForTest(const char *phase)
{
    auto *device = dynamic_cast<SyntheticDevice *>(
        audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice());
    if (!device)
    {
        const auto *job = routingCoordinator.find(lastRoutingOperation);
        if (!audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice() && job &&
            job->phase == ChannelRoutingCoordinator::Phase::completed &&
            job->audioDisposition != ChannelRoutingCoordinator::AudioDisposition::liveGraph)
            return juce::Result::ok(); // Actual deferred/no-device completion, not native adoption.
        return juce::Result::fail("GUI route adoption requires its original synthetic device");
    }
    juce::AudioBuffer<float> output(2, device->getCurrentBufferSizeSamples());
    for (int n = 0; n < 64; ++n)
    {
        const bool pending = routingPending();
        const auto native = audio.channelRoutingReadback();
        if (!pending && native.nativePostedSerial != 0 &&
            native.nativePostedSerial == native.selectedSerial)
            break;
        device->process(output); // Genuine native graph selection, never fabricated completion.
        dispatchMessages(1); // CONTROL after the entire callback, no physical input/listening claim.
    }
    const auto *job = routingCoordinator.find(lastRoutingOperation);
    const auto native = audio.channelRoutingReadback();
    if (!job || job->phase != ChannelRoutingCoordinator::Phase::completed ||
        !job->nativeCompletionKnown || job->selectedSerial != job->postedSerial ||
        native.nativePostedSerial == 0 || native.nativePostedSerial != native.selectedSerial)
        return juce::Result::fail("GUI route did not reach exact native selection [" + juce::String(phase) +
                                  "]: " + (job ? job->detail : juce::String("No owned route job")) +
                                  " selected=" + juce::String(juce::int64(job ? job->selectedSerial : 0)) +
                                  " posted=" + juce::String(juce::int64(job ? job->postedSerial : 0)) +
                                  " nativeSelected=" + juce::String(juce::int64(native.selectedSerial)) +
                                  " nativePosted=" + juce::String(juce::int64(native.nativePostedSerial)));
    return juce::Result::ok();
}
juce::Result Studio::runSmokeTest(const juce::File &folder)
{
    stopTimer();
    folder.createDirectory();
    auto recordingResult = verifyMidiRecording(folder);
    if (recordingResult.failed())
        return recordingResult;
    if (auto typingResult = verifyTypingKeyboard(folder); typingResult.failed())
        return typingResult;
    if (auto scoreResult = verifyScoreLog(folder); scoreResult.failed())
        return scoreResult;
    if (auto pianoResult = verifyPianoNavigation(folder); pianoResult.failed())
        return pianoResult;
    if (auto scoreResult = verifyScoreTools(folder); scoreResult.failed())
        return scoreResult;
    if (auto rackResult = verifyRackGroups(folder); rackResult.failed())
        return rackResult;
    if (auto fillResult = verifyStepFill(folder); fillResult.failed())
        return fillResult;
    if (auto soloResult = verifyChannelSolo(folder); soloResult.failed())
        return soloResult;
    if (auto channelsResult = verifyChannelManagement(folder); channelsResult.failed())
        return channelsResult;
    if (auto velocityResult = verifyRackVelocity(folder); velocityResult.failed())
        return velocityResult;
    if (auto swingResult = verifyRackSwing(folder); swingResult.failed())
        return swingResult;
    if (auto quantizerResult = verifyQuantizer(folder); quantizerResult.failed())
        return quantizerResult;
    auto countResult = verifyCountInRecording(folder);
    if (countResult.failed())
        return countResult;
    audio.stop();
    customization = Customization::familiarDefaults();
    clearScoreLog();
    controller.replace(Project::empty());
    selectedPattern = controller.project.patterns.front().id;
    selectedChannel = controller.project.channels.front().id;
    refresh();
    resized();
    {
        CoProducerPanel inspector(*this);
        if (auto result = inspector.verifyForTest(folder); result.failed())
            return result;
    }
    keyPressed(juce::KeyPress(juce::KeyPress::F6Key));
    if (windows[0]->isVisible())
        return juce::Result::fail("F6 did not hide Channel Rack");
    keyPressed(juce::KeyPress(juce::KeyPress::F6Key));
    if (!windows[0]->isVisible())
        return juce::Result::fail("F6 did not restore Channel Rack");
    auto fail = [](const char *message) { return juce::Result::fail(message); };
    auto click = [this](int i, float x, float y, int flags)
    {
        auto *editor = dynamic_cast<Editor *>(windows[size_t(i)]->getContentComponent());
        juce::Point<float> point{x, y};
        auto now = juce::Time::getCurrentTime();
        juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), point,
                               juce::ModifierKeys(flags), 1, 0, 0, 0, 0, editor, editor, now, point, now, 1,
                               false);
        editor->mouseDown(event);
        editor->mouseUp(event);
    };
    CommandArgs a;
    a.pattern = selectedPattern;
    a.length = 8;
    command("pattern.length", a);
    click(0, float(juce::jlimit(230, 250, int(windows[0]->getContentComponent()->getWidth() * .38)) + 18 /* first step, after name/knob column */), 60,
          juce::ModifierKeys::leftButtonModifier);
    if (controller.project.pattern(selectedPattern)->notes.size() != 1)
        return fail("Step activation gesture failed");
    click(0, float(juce::jlimit(230, 250, int(windows[0]->getContentComponent()->getWidth() * .38)) + 18 /* first step, after name/knob column */), 60,
          juce::ModifierKeys::rightButtonModifier);
    if (!controller.project.pattern(selectedPattern)->notes.empty())
        return fail("Step deactivation gesture failed");
    click(0, float(juce::jlimit(230, 250, int(windows[0]->getContentComponent()->getWidth() * .38)) + 18 /* first step, after name/knob column */), 60,
          juce::ModifierKeys::leftButtonModifier);
    selectedChannel = controller.project.channels.back().id;
    const auto noteArea = static_cast<Editor *>(windows[1]->getContentComponent())->pianoNoteViewport();
    if (noteArea.getHeight() < 12 || noteArea.getWidth() < 30)
        return fail("Piano Roll needs a usable note viewport");
    click(1, float(noteArea.getX() + 26), float(noteArea.getCentreY()),
          juce::ModifierKeys::leftButtonModifier);
    if (controller.project.pattern(selectedPattern)->notes.size() != 2)
        return fail("Piano roll drawing failed");
    focus = Scope::piano;
    keyPressed(juce::KeyPress('A', juce::ModifierKeys::ctrlModifier, 0));
    keyPressed(juce::KeyPress('B', juce::ModifierKeys::ctrlModifier, 0));
    if (controller.project.pattern(selectedPattern)->notes.size() != 3)
        return fail("Piano roll Ctrl+B failed");
    const auto initialPlacement =
        static_cast<Editor *>(windows[2]->getContentComponent())->playlistPosition(0, 0);
    click(2, float(initialPlacement.x), float(initialPlacement.y), juce::ModifierKeys::leftButtonModifier);
    focus = Scope::playlist;
    for (int i = 0; i < 3; ++i)
        keyPressed(juce::KeyPress('B', juce::ModifierKeys::ctrlModifier, 0));
    if (controller.project.placements.size() != 4 || controller.project.endBeat() != 32)
        return fail("Repeated Playlist Ctrl+B failed");
    const auto linked = selectedPattern;
    a = {};
    a.target = controller.project.placements[1].id;
    command("clip.unique", a);
    if (controller.project.placements[1].pattern == linked)
        return fail("Make Unique failed");
    focus = Scope::mixer;
    selectedInsert = 4;
    if (auto ready = selectGuiRoutingPredecessorForTest(*this, "initial Ctrl+L"); ready.failed())
        return ready;
    keyPressed(juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 0));
    if (controller.project.channel(selectedChannel)->mixer != 4)
    {
        folder.getChildFile("gui-routing-diagnostic.txt").replaceWithText(
            "status=" + status + "\npending=" + juce::String(int(routingPending())) +
            "\noperation=" + juce::String(juce::int64(lastRoutingDispatchOperation)) +
            "\nactual=" + juce::String(controller.project.channel(selectedChannel)->mixer) +
            "\nproject=" + controller.projectIdentity + "\n");
        return fail("Mixer Ctrl+L assignment failed");
    }
    if (auto adopted = finishRoutingForTest("initial Ctrl+L"); adopted.failed())
        return adopted;
    patternMode = false;
    mode.setButtonText("Song");
    refresh();
    auto directory = folder.createDirectory();
    if (directory.failed())
        return directory;
    auto projectPath = folder.getChildFile("gui-eight-bars.vibra");
    auto result = saveProject(controller.project, projectPath);
    if (result.failed())
        return result;
    Project reopened;
    result = loadProject(projectPath, reopened);
    if (result.failed())
        return result;
    if (!reopened.state().isEquivalentTo(controller.project.state()))
        return fail("GUI-created song round trip failed");
    result = audio.renderSync(reopened, folder.getChildFile("gui-eight-bars.wav"));
    if (result.failed())
        return result;
    focus = Scope::piano;
    auto customKey =
        juce::KeyPress('D', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
    result = customization.bind("notes.duplicate", Scope::piano, customKey);
    if (result.failed())
        return result;
    auto beforeRemap = controller.project.state();
    if (keyPressed(juce::KeyPress('B', juce::ModifierKeys::ctrlModifier, 0)) ||
        !controller.project.state().isEquivalentTo(beforeRemap))
        return fail("Removed binding still active");
    if (!keyPressed(customKey))
        return fail("Custom binding did not dispatch");
    controller.undoEdit();
    selectedNotes = {controller.project.pattern(selectedPattern)->notes.back().id};
    Macro variation{"macro.smoke", "Duplicate and transpose", {}};
    CommandArgs duplicate;
    duplicate.beat = 1;
    variation.steps.push_back({"notes.duplicate", duplicate, true});
    CommandArgs transpose;
    transpose.pitch = 12;
    transpose.length = 0;
    variation.steps.push_back({"notes.move", transpose, true});
    result = customization.addMacro(variation);
    if (result.failed())
        return result;
    auto beforeMacro = controller.project.state();
    runMacro(variation.id);
    if (status != "Macro complete")
        return juce::Result::fail("GUI macro runner failed: " + status);
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(beforeMacro))
        return fail("GUI macro undo failed");
    result = customization.save(folder.getChildFile("shortcuts.json"));
    if (result.failed())
        return result;
    SettingsContent settings(*this);
    settings.setCurrentTabIndex(1);
    juce::PNGImageFormat settingsPng;
    auto settingsOutput = folder.getChildFile("settings-shortcuts.png").createOutputStream();
    if (settingsOutput)
    {
        settingsOutput->setPosition(0);
        settingsOutput->truncate();
    }
    if (!settingsOutput || !settingsPng.writeImageToStream(
                               settings.createComponentSnapshot(settings.getLocalBounds()), *settingsOutput))
        return fail("Settings snapshot failed");
    settingsOutput.reset();
    settings.setCurrentTabIndex(2);
    settingsOutput = folder.getChildFile("settings-macros.png").createOutputStream();
    if (settingsOutput)
    {
        settingsOutput->setPosition(0);
        settingsOutput->truncate();
    }
    if (!settingsOutput || !settingsPng.writeImageToStream(
                               settings.createComponentSnapshot(settings.getLocalBounds()), *settingsOutput))
        return fail("Macro snapshot failed");
    settingsOutput.reset();
    settings.setCurrentTabIndex(4);
    settingsOutput = folder.getChildFile("settings-midi.png").createOutputStream();
    if (settingsOutput)
    {
        settingsOutput->setPosition(0);
        settingsOutput->truncate();
    }
    if (!settingsOutput || !settingsPng.writeImageToStream(
                               settings.createComponentSnapshot(settings.getLocalBounds()), *settingsOutput))
        return fail("MIDI Settings snapshot failed");
    settingsOutput.reset();
    settings.setCurrentTabIndex(5);
    settingsOutput = folder.getChildFile("settings-transport.png").createOutputStream();
    if (settingsOutput)
    {
        settingsOutput->setPosition(0);
        settingsOutput->truncate();
    }
    if (!settingsOutput || !settingsPng.writeImageToStream(
                               settings.createComponentSnapshot(settings.getLocalBounds()), *settingsOutput))
        return fail("Transport Settings snapshot failed");
    settingsOutput.reset();
    auto projected = audio.rebuild(controller.project, patternMode, selectedPattern);
    {
        ColourPage colours(*this);
        if (auto checked = colours.verifyForTest(folder); checked.failed())
            return checked;
        settings.setCurrentTabIndex(6);
        auto output = folder.getChildFile("settings-colours.png").createOutputStream();
        if (!output)
            return fail("Colour settings output");
        output->setPosition(0);
        output->truncate();
        if (!settingsPng.writeImageToStream(settings.createComponentSnapshot(settings.getLocalBounds()),
                                            *output))
            return fail("Colour settings snapshot");
    }
    if (projected.failed())
        return projected;
    auto unchanged = controller.project.state();
    selectedChannel = controller.project.channels.front().id;
    click(0, 55, 60, juce::ModifierKeys::leftButtonModifier);
    auto *existing = instrumentWindows[selectedChannel].get();
    if (!existing || !existing->isVisible() || !unchanged.isEquivalentTo(controller.project.state()))
        return fail("Channel name instrument opening changed project or failed");
    click(0, 55, 60, juce::ModifierKeys::leftButtonModifier);
    if (instrumentWindows[selectedChannel].get() != existing)
        return fail("Instrument window duplicated");
    auto instrumentOutput = folder.getChildFile("channel-instrument.png").createOutputStream();
    if (instrumentOutput)
    {
        instrumentOutput->setPosition(0);
        instrumentOutput->truncate();
    }
    if (!instrumentOutput ||
        !settingsPng.writeImageToStream(existing->createComponentSnapshot(existing->getLocalBounds()),
                                        *instrumentOutput))
        return fail("Instrument snapshot failed");
    auto instrumentChannel = selectedChannel;
    selectedChannel = controller.project.channels.back().id;
    auto otherGain = controller.project.channel(selectedChannel)->gain;
    auto *volume =
        dynamic_cast<juce::Slider *>(existing->controlsComponent()->findChildWithID("instrument.volume"));
    auto *panControl =
        dynamic_cast<juce::Slider *>(existing->controlsComponent()->findChildWithID("instrument.pan"));
    if (!volume || !panControl)
        return fail("Functional instrument controls missing");
    volume->setValue(.42, juce::sendNotificationSync);
    if (std::abs(controller.project.channel(instrumentChannel)->gain - .42f) > .001f ||
        controller.project.channel(selectedChannel)->gain != otherGain)
        return fail("Instrument control targeted wrong Channel");
    controller.undoEdit();
    panControl->setValue(.25, juce::sendNotificationSync);
    if (std::abs(controller.project.channel(instrumentChannel)->pan - .25f) > .001f)
        return fail("Instrument pan control failed");
    controller.undoEdit();
    if (!unchanged.isEquivalentTo(controller.project.state()))
        return fail("Instrument control undo failed");
    auto *pitchControl = dynamic_cast<juce::Slider *>(
        existing->controlsComponent()->findChildWithID("instrument.sampler.cents"));
    auto *rootControl = dynamic_cast<juce::Slider *>(
        existing->controlsComponent()->findChildWithID("instrument.sampler.root"));
    auto *envelopeControl = dynamic_cast<juce::ToggleButton *>(
        existing->controlsComponent()->findChildWithID("instrument.sampler.envelope"));
    if (!pitchControl || !rootControl || !envelopeControl)
        return fail("Functional sampler controls missing");
    pitchControl->setValue(700, juce::sendNotificationSync);
    if (controller.project.channel(instrumentChannel)->sampler.cents != 700 ||
        controller.project.channel(selectedChannel)->sampler.cents != 0)
        return fail("Sampler pitch control targeted wrong Channel");
    controller.undoEdit();
    rootControl->setValue(57, juce::sendNotificationSync);
    if (controller.project.channel(instrumentChannel)->sampler.root != 57)
        return fail("Sampler root control failed");
    controller.undoEdit();
    envelopeControl->triggerClick();
    dispatchMessages(10);
    if (!controller.project.channel(instrumentChannel)->sampler.envelope)
        return fail("Sampler envelope control failed");
    controller.undoEdit();
    if (!unchanged.isEquivalentTo(controller.project.state()))
        return fail("Sampler controls did not undo exactly");
    dispatchMessages(220); // Let the settings view reconcile the preceding Undo before capture.
    for (auto field : {"start", "end", "attack", "decay", "sustain", "release"})
    {
        auto *slider = dynamic_cast<juce::Slider *>(
            existing->controlsComponent()->findChildWithID("instrument.sampler." + juce::String(field)));
        if (!slider)
            return fail("Sampler range/envelope slider missing");
        const auto expected = juce::String(field) == "start" ? .25 : juce::String(field) == "end" ? .75 : .3;
        slider->setValue(expected *
                             ((juce::String(field) == "start" || juce::String(field) == "end") ? 100 : 1),
                         juce::sendNotificationSync);
        const auto &settings = controller.project.channel(instrumentChannel)->sampler;
        const auto actual = juce::String(field) == "start"     ? settings.start
                            : juce::String(field) == "end"     ? settings.end
                            : juce::String(field) == "attack"  ? settings.attack
                            : juce::String(field) == "decay"   ? settings.decay
                            : juce::String(field) == "sustain" ? settings.sustain
                                                               : settings.release;
        if (std::abs(actual - expected) > .00001)
            return fail("Sampler range/envelope slider did not update its Channel");
        controller.undoEdit();
        dispatchMessages(220);
    }
    auto samplerStream = folder.getChildFile("sampler-controls.png").createOutputStream();
    if (!samplerStream)
        return fail("Sampler screenshot stream failed");
    samplerStream->setPosition(0);
    samplerStream->truncate();
    if (!juce::PNGImageFormat().writeImageToStream(
            existing->createComponentSnapshot(existing->getLocalBounds()), *samplerStream))
        return fail("Sampler controls screenshot failed");
    samplerStream.reset();
    auto *samplerViewport =
        dynamic_cast<juce::Viewport *>(existing->getContentComponent()->getChildComponent(0));
    if (!samplerViewport)
        return fail("Sampler viewport missing");
    // A short window must scroll its settings (the default size may already fit them all).
    const auto samplerWindowSize = existing->getBounds();
    existing->setSize(existing->getWidth(), 360);
    samplerViewport->setViewPosition(0, 1000);
    const bool samplerScrolled = samplerViewport->getViewPositionY() > 0;
    existing->setBounds(samplerWindowSize);
    if (!samplerScrolled)
        return fail("Sampler settings overflow did not scroll");
    auto lowerStream = folder.getChildFile("sampler-controls-bottom.png").createOutputStream();
    if (!lowerStream)
        return fail("Sampler lower controls stream failed");
    lowerStream->setPosition(0);
    lowerStream->truncate();
    if (!juce::PNGImageFormat().writeImageToStream(
            existing->createComponentSnapshot(existing->getLocalBounds()), *lowerStream))
        return fail("Sampler lower controls screenshot failed");
    lowerStream.reset();
    samplerViewport->setViewPosition(0, 0);
    if (!unchanged.isEquivalentTo(controller.project.state()))
        return fail("Sampler controls changed unrelated state");
    windows[1]->setVisible(false);
    auto *rack = dynamic_cast<Editor *>(windows[0]->getContentComponent());
    auto point = juce::Point<float>(55, 60);
    auto now = juce::Time::getCurrentTime();
    juce::MouseEvent doubleClick(juce::Desktop::getInstance().getMainMouseSource(), point,
                                 juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                 rack, rack, now, point, now, 2, false);
    rack->mouseDoubleClick(doubleClick);
    if (windows[1]->isVisible())
        return fail("Name double-click still opened Piano Roll");
    click(0, float(juce::jlimit(230, 250, int(rack->getWidth() * .38)) - 55), 60,
          juce::ModifierKeys::leftButtonModifier);
    if (!windows[1]->isVisible())
        return fail("Dedicated Rack Piano Roll button failed");
    existing->setVisible(false);
    status = "GUI interaction smoke passed - listening and user familiarity pending";
    auto image = createComponentSnapshot(getLocalBounds());
    juce::PNGImageFormat png;
    auto output = folder.getChildFile("studio.png").createOutputStream();
    if (output)
    {
        output->setPosition(0);
        output->truncate();
    }
    if (!output || !png.writeImageToStream(image, *output))
        return fail("Snapshot write failed");
    // An original, real .vibra song for populated review, not decorative/fake waveforms.
    auto review = Project::demo();
    review.name = "Midnight Session";
    review.placements.clear();
    auto source = review.patterns.front();
    review.patterns.front().name = "Beat + bass";
    review.placements.push_back({review.allocate(), source.id, 0, 0, 8, 0});
    review.placements.push_back({review.allocate(), source.id, 0, 8, 8, 0});
    for (int section = 0; section < 4; ++section)
    {
        Pattern p{review.allocate(),
                  juce::StringArray{"Drum groove", "Bass phrase", "Half-time", "Bass variation"}[section],
                  8,
                  {}};
        for (auto n : source.notes)
        {
            bool bass = n.channel == review.channels.back().id;
            if ((section % 2 == 1) != bass)
                continue;
            if (section == 2 && n.step && int(n.beat * 4) % 4 != 0)
                continue;
            n.id = review.allocate();
            if (section == 3)
                n.pitch += int(n.beat) % 3 == 0 ? 7 : 0;
            p.notes.push_back(n);
        }
        for (int repeat = 0; repeat < 2; ++repeat)
            review.placements.push_back({review.allocate(), p.id, section < 2 ? section + 1 : section - 1,
                                         (section < 2 ? 16. : 32.) + repeat * 8, 8, 0});
        review.patterns.push_back(std::move(p));
    }
    auto reviewPath = folder.getChildFile("midnight-session.vibra");
    result = saveProject(review, reviewPath);
    if (result.failed())
        return result;
    result = openProjectFile(reviewPath, true);
    if (result.failed())
        return result;
    selectedPattern = controller.project.patterns[2].id;
    refresh();
    auto beforeLayout = controller.project.state();
    {
        auto original = controller.project;
        auto navigationProject = original;
        navigationProject.placements.push_back(
            {navigationProject.allocate(), selectedPattern, 127, 96, 8, 0});
        for (int index = 0; index < 30; ++index)
            navigationProject.channels.push_back({navigationProject.allocate(),
                                                  "SCROLL " + juce::String(index + 1), "builtin:hat", 17, .7f,
                                                  0, false});
        controller.replace(navigationProject);
        refresh();
        const auto beforeNavigation = controller.project.state();
        for (int kind : {0, 2, 3})
        {
            auto *editor = static_cast<Editor *>(windows[size_t(kind)]->getContentComponent());
            int checked = 0;
            std::vector<juce::Rectangle<int>> gutters;
            ViewportBar *rackHorizontal = nullptr, *rackVertical = nullptr;
            for (int child = 0; child < editor->getNumChildComponents(); ++child)
                if (auto *bar = dynamic_cast<ViewportBar *>(editor->getChildComponent(child)))
                {
                    if (kind == 0)
                    {
                        if (bar->getWidth() == scrollBarThickness)
                            rackVertical = bar;
                        else
                            rackHorizontal = bar;
                    }
                    if (!bar->isVisible())
                        continue;
                    if (std::min(bar->getWidth(), bar->getHeight()) != scrollBarThickness ||
                        !editor->getLocalBounds().contains(bar->getBounds()))
                        return fail("Scrollbar must fit its slim reserved gutter");
                    for (const auto &other : gutters)
                        if (other.intersects(bar->getBounds()))
                            return fail("Horizontal and vertical scrollbar gutters overlap");
                    gutters.push_back(bar->getBounds());
                    const auto range = bar->read();
                    if (range[2] > range[1])
                    {
                        auto now = juce::Time::getCurrentTime();
                        const juce::Point<float> point{float(bar->getWidth() - 2),
                                                       float(bar->getHeight() - 2)};
                        juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), point,
                                               juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1,
                                               0, 0, 0, 0, bar, bar, now, point, now, 1, false);
                        bar->mouseDown(event);
                        bar->mouseUp(event);
                        if (bar->read()[0] <= 0)
                            return fail("Editor scrollbar did not move overflowing content");
                        ++checked;
                    }
                }
            if (checked < (kind == 2 ? 2 : 1))
                return fail("Editor overflow scrollbar missing");
            if (kind == 0)
            {
                if (!rackHorizontal || !rackVertical || rackVertical->isVisible())
                    return fail("Channel Rack must hide its vertical scrollbar only");
                rackHorizontal->change(0, rackHorizontal->read()[1]);
                const auto now = juce::Time::getCurrentTime();
                const juce::Point<float> position{80, 80};
                auto wheelEvent = [&](int modifiers)
                {
                    const auto target = (modifiers & juce::ModifierKeys::shiftModifier)
                                            ? juce::Point<float>{float(editor->getWidth() - 30), 80.f}
                                            : position;
                    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), target,
                                            juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, editor, editor, now,
                                            target, now, 1, false);
                };
                juce::MouseWheelDetails wheel{};
                wheel.deltaY = -.1f;
                editor->mouseWheelMove(wheelEvent(0), wheel);
                if (rackVertical->read()[0] != 1 || rackHorizontal->read()[0] != 0)
                    return fail("Plain Rack wheel must scroll Channels without moving steps");
                editor->mouseWheelMove(wheelEvent(juce::ModifierKeys::shiftModifier), wheel);
                if (rackHorizontal->read()[0] <= 0 || rackVertical->read()[0] != 1)
                    return fail("Shift Rack wheel must retain horizontal step scrolling");
                for (int i = 0; i < 128; ++i)
                    editor->mouseWheelMove(wheelEvent(0), wheel);
                const auto range = rackVertical->read();
                if (range[0] != std::max(0., range[2] - range[1]))
                    return fail("Rack wheel cannot reach or clamp at the final Channel");
                wheel.deltaY = .1f;
                editor->mouseWheelMove(wheelEvent(0), wheel);
                if (rackVertical->read()[0] != range[0] - 1)
                    return fail("Rack wheel cannot scroll back from the bottom");
                const auto beforeZero = rackVertical->read()[0];
                wheel.deltaY = 0;
                editor->mouseWheelMove(wheelEvent(0), wheel);
                if (rackVertical->read()[0] != beforeZero)
                    return fail("Zero Rack wheel input must not jump a Channel");
                auto oldSize = editor->getBounds();
                editor->setSize(editor->getWidth(), editor->getHeight() + 40);
                if (rackVertical->isVisible())
                    return fail("Resizing must not expose the Rack vertical scrollbar");
                editor->setBounds(oldSize);
            }
            if (kind == 3)
            {
                const auto beforeRoute = controller.project.state();
                const int stripArea =
                    editor->getWidth() >= 700 ? editor->getWidth() - 240 : editor->getWidth();
                click(3, float(stripArea - 60), 46, juce::ModifierKeys::leftButtonModifier);
                if (selectedInsert != 17)
                    return fail("Scrolled Mixer header cannot select Insert 17");
                if (auto ready = selectGuiRoutingPredecessorForTest(*this, "scrolled Ctrl+L"); ready.failed())
                    return ready;
                keyPressed(juce::KeyPress('L', juce::ModifierKeys::ctrlModifier, 0));
                if (controller.project.channel(selectedChannel)->mixer != 17)
                    return fail("Ctrl+L cannot assign scrolled Insert 17");
                if (auto adopted = finishRoutingForTest("scrolled Ctrl+L"); adopted.failed())
                    return adopted;
                controller.undoEdit();
                if (auto adopted = finishRoutingForTest("scrolled Undo"); adopted.failed())
                    return adopted;
                if (!beforeRoute.isEquivalentTo(controller.project.state()))
                    return fail("Scrolled Insert 17 routing undo");
            }
            auto output = folder
                              .getChildFile(kind == 0   ? "rack-scrolled.png"
                                            : kind == 2 ? "playlist-scrolled.png"
                                                        : "mixer-insert17.png")
                              .createOutputStream();
            if (!output)
                return fail("Scrolled editor output");
            output->setPosition(0);
            output->truncate();
            if (!png.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), *output))
                return fail("Scrolled editor snapshot");
            for (int child = 0; child < editor->getNumChildComponents(); ++child)
                if (auto *bar = dynamic_cast<ViewportBar *>(editor->getChildComponent(child)))
                    bar->change(0, bar->read()[1]);
        }
        if (!beforeNavigation.isEquivalentTo(controller.project.state()))
            return fail("Scrolling mutated musical document");
        // Restore this disposable document through the actual replacement seam;
        // raw Controller::replace leaves the selected routing overlay attached.
        result = openProjectFile(reviewPath, true);
        if (result.failed())
            return result;
        if (!original.state().isEquivalentTo(controller.project.state()))
            return fail("Navigation fixture document restoration changed music");
        selectedPattern = controller.project.patterns[2].id;
        refresh();
        folder.getChildFile("workspace-verification.txt")
            .replaceWithText(
                "PASS: 18 Mixer strips; slim reserved nonoverlapping gutters; hidden Rack vertical bar; "
                "plain wheel reaches/clamps Channels, Shift-wheel steps, reverse/zero wheel and resize; "
                "scrollbar mouse-down handlers move Playlist/Mixer/steps; Playlist H/V; colour "
                "picker/profile "
                "preserve musical state. Component tests, not native musical gestures or listening.\n");
    }
    auto snapshot = [&](const juce::String &name, int w, int h, float scale) -> juce::Result
    {
        setSize(w, h);
        resized();
        for (auto &window : windows)
            if (window->isVisible())
            {
                if (!workspace.contains(window->getBounds()) || window->getWidth() < 300 ||
                    window->getHeight() < 30)
                    return fail("Dock bounds are outside the workspace");
            }
        for (auto &b : editorButtons)
            if (!getLocalBounds().contains(b.getBounds()) || b.getBounds().intersects(snapChoice.getBounds()))
                return fail("Toolbar overlap");
        auto rendered = createComponentSnapshot(getLocalBounds(), true, scale);
        auto stream = folder.getChildFile(name).createOutputStream();
        if (stream)
        {
            stream->setPosition(0);
            stream->truncate();
        }
        if (!stream || !png.writeImageToStream(rendered, *stream))
            return fail("Scaled snapshot failed");
        return juce::Result::ok();
    };
    struct View
    {
        const char *name;
        int w, h;
        float scale;
    };
    const View views[]{{"populated-1080p-100.png", 1920, 1040, 1.f},
                       {"populated-1080p-150.png", 1280, 690, 1.5f},
                       {"populated-4k-100.png", 3840, 2080, 1.f},
                       {"populated-4k-150.png", 2560, 1386, 1.5f},
                       {"populated-4k-200.png", 1920, 1040, 2.f}};
    for (auto &view : views)
    {
        result = snapshot(view.name, view.w, view.h, view.scale);
        if (result.failed())
            return result;
    }
    setSize(1600, 940);
    resized();
    {
        const auto beforeWindows = controller.project.state();
        const bool beforePlaying = audio.playing();
        auto *beforeDevice = audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice();
        auto event = [](juce::Component *target, juce::Point<float> point, juce::Point<float> start,
                        bool dragged = false)
        {
            auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                    juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                    target, target, now, start, now, 1, dragged);
        };
        for (int index = 0; index < 5; ++index)
        {
            auto *panel = index == 4 ? static_cast<juce::Component *>(browser.get())
                                     : static_cast<juce::Component *>(windows[size_t(index)].get());
            const auto before = panel->getBounds();
            const juce::Point<float> start{100, 15},
                end = index == 4 ? juce::Point<float>{150, 65} : juce::Point<float>{50, -35};
            panel->mouseDown(event(panel, start, start));
            panel->mouseDrag(event(panel, end, start, true));
            panel->mouseUp(event(panel, end, start, true));
            if (!floatingWindows[size_t(index)] || panel->getBounds() == before)
                return fail("Header drag did not float/move editor or Browser");
            auto image = panel->createComponentSnapshot(panel->getLocalBounds());
            if (image.getPixelAt(0, 0).getAlpha() != 0 || image.getPixelAt(20, 15).getAlpha() == 0)
                return fail("Rounded frame alpha clipping");
            for (int child = 0; child < panel->getNumChildComponents(); ++child)
                if (auto *border =
                        dynamic_cast<juce::ResizableBorderComponent *>(panel->getChildComponent(child));
                    border && border->isVisible())
                {
                    const auto beforeResize = panel->getBounds();
                    const juce::Point<float> corner{float(border->getWidth() - 2),
                                                    float(border->getHeight() / 2)};
                    auto *input = static_cast<juce::Component *>(border);
                    input->mouseDown(event(border, corner, corner));
                    input->mouseDrag(event(border, corner.translated(30, 0), corner, true));
                    input->mouseUp(event(border, corner.translated(30, 0), corner, true));
                    if (panel->getBounds() == beforeResize)
                        return fail("Floating window edge resize handler");
                }
        }
        const auto savedBounds = floatingBounds;
        maximiseEditor(2);
        maximiseEditor(2);
        for (int index = 0; index < 4; ++index)
            if (windows[size_t(index)]->getBounds() != savedBounds[size_t(index)])
                return fail("Maximize lost floating editor geometry");
        toggleEditor(0);
        toggleEditor(0);
        if (windows[0]->getBounds() != savedBounds[0])
            return fail("Hide/show lost floating geometry");
        auto output = folder.getChildFile("floating-rounded-workspace.png").createOutputStream();
        if (!output)
            return fail("Floating workspace output");
        output->setPosition(0);
        output->truncate();
        if (!png.writeImageToStream(createComponentSnapshot(getLocalBounds()), *output))
            return fail("Floating workspace PNG");
        output.reset();
        setSize(1280, 690);
        const auto area = juce::Rectangle<int>(4, 84, getWidth() - 8, getHeight() - 113);
        for (int index = 0; index < 4; ++index)
            if (!area.contains(windows[size_t(index)]->getBounds()))
                return fail("Floating editor escaped resized workspace");
        if (!area.contains(browser->getBounds()))
            return fail("Floating Browser escaped resized workspace");
        setSize(1600, 940);
        restoreDockedLayout();
        if (beforePlaying != audio.playing() ||
            beforeDevice != audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice() ||
            !beforeWindows.isEquivalentTo(controller.project.state()))
            return fail("Window gestures changed playback/device/document");
        folder.getChildFile("floating-window-verification.txt")
            .replaceWithText("PASS: real frame header/resize handlers for Rack/Piano/Playlist/Mixer/Browser; "
                             "transparent rounded corners; maximize/restore and hide/show geometry; main "
                             "resize confinement; dock restore; retained device/transport/document. "
                             "Component input only, not physical/native musical gestures or listening.\n");
    }
    {
        const std::vector<juce::Rectangle<int>> dual{{-1920, 0, 1920, 1080}, {0, 0, 2560, 1440}};
        const juce::Rectangle<int> left{-1800, 120, 800, 600};
        if (recoverDesktopWindow(left, dual) != left ||
            !dual[1].contains(recoverDesktopWindow(left, {dual[1]})) ||
            !dual[1].contains(recoverDesktopWindow({9000, -4000, 900, 700}, {dual[1]})) ||
            recoverDesktopWindow({0, 0, 500, 400}, {}) != juce::Rectangle<int>(0, 0, 500, 400))
            return fail("Detached desktop topology recovery");
        auto *device = dynamic_cast<SyntheticDevice *>(
            audio.engine.getDeviceManager().deviceManager.getCurrentAudioDevice());
        if (!device)
            return fail("Detached editor test requires virtual audio device");
        juce::AudioBuffer<float> buffer(2, 256);
        const auto document = controller.project.state();
        juce::String evidence;
        for (bool pattern : {true, false})
        {
            auto prepared = audio.rebuild(controller.project, pattern, selectedPattern);
            if (prepared.failed())
                return prepared;
            needsRebuild = false;
            audio.seekBeat(0);
            audio.playStop();
            dispatchMessages(20);
            for (int i = 0; i < 10; ++i)
                device->process(buffer);
            const auto before = audio.projectionDiagnostics();
            const int starts = device->starts, stops = device->stops;
            const auto position = audio.positionBeat();
            double maximumMs = 0;
            for (int index = 0; index < 5; ++index)
            {
                auto *component = index == 4 ? static_cast<juce::Component *>(browser.get())
                                             : static_cast<juce::Component *>(windows[size_t(index)].get());
                auto *content = index == 4 ? component : windows[size_t(index)]->getContentComponent();
                detachWorkspaceWindow(index);
                if (!detachedWindows[size_t(index)] || !detachedWindows[size_t(index)]->getPeer() ||
                    detachedWindows[size_t(index)]->getContentComponent() != component ||
                    (index != 4 && windows[size_t(index)]->getContentComponent() != content))
                    return fail("Detached editor lost component or native peer identity");
                auto *same = detachedWindows[size_t(index)].get();
                detachWorkspaceWindow(index);
                if (detachedWindows[size_t(index)].get() != same)
                    return fail("Duplicate detached window");
                for (int i = 0; i < 16; ++i)
                {
                    const auto start = juce::Time::getMillisecondCounterHiRes();
                    device->process(buffer);
                    maximumMs = std::max(maximumMs, juce::Time::getMillisecondCounterHiRes() - start);
                    dispatchMessages(1);
                }
                if (index < 4)
                {
                    toggleEditor(index);
                    if (same->isVisible())
                        return fail("Detached hide failed");
                    showEditor(index);
                    if (!same->isVisible())
                        return fail("Detached show failed");
                }
                auto output = folder
                                  .getChildFile("detached-" + juce::String(index) +
                                                (pattern ? "-pattern.png" : "-song.png"))
                                  .createOutputStream();
                if (!output)
                    return fail("Detached snapshot output");
                output->setPosition(0);
                output->truncate();
                if (!png.writeImageToStream(same->createComponentSnapshot(same->getLocalBounds()), *output))
                    return fail("Detached snapshot encoding");
                dockWorkspaceWindow(index);
                if (detachedWindows[size_t(index)] || component->getParentComponent() != this)
                    return fail("Detached reattach ownership");
            }
            for (const auto &layoutCommand :
                 {"workspace.tileBalanced", "workspace.tileFocused", "workspace.free",
                  "workspace.tileBalanced", "workspace.tileDetached", "workspace.docked"})
            {
                command(layoutCommand);
                for (int tick = 0; tick < 8; ++tick)
                {
                    const auto start = juce::Time::getMillisecondCounterHiRes();
                    device->process(buffer);
                    maximumMs = std::max(maximumMs, juce::Time::getMillisecondCounterHiRes() - start);
                }
            }
            auto after = audio.projectionDiagnostics();
            if (!audio.playing() || audio.positionBeat() < position || before.edit != after.edit ||
                device->starts != starts || device->stops != stops || maximumMs >= 256000. / 48000 ||
                !document.isEquivalentTo(controller.project.state()))
                return fail("Detaching disturbed playback/device/document");
            for (size_t i = 0; i < before.channels.size(); ++i)
                if (before.channels[i].graphInitialisations != after.channels[i].graphInitialisations)
                    return fail("Detaching rebuilt audio graph");
            evidence += (pattern ? "Pattern" : "Song") +
                        juce::String(": 128 virtual callbacks including six layout commands, max ") +
                        juce::String(maximumMs, 4) +
                        " ms; zero overruns, same device/start/stop/Edit/graph/document; all five native "
                        "peers reuse original content.\n";
            audio.stop();
        }
        moveWorkspaceWindow(0, {400, 200, 500, 300});
        detachWorkspaceWindow(2);
        const auto savedNative = detachedWindows[2]->savedBounds();
        toggleEditor(1);
        const auto layout = folder.getChildFile("workspace-roundtrip.json");
        if (saveWorkspaceLayout(layout).failed())
            return fail("Workspace layout save");
        restoreDockedLayout();
        showEditor(1);
        if (loadWorkspaceLayout(layout).failed() || !floatingWindows[0] || !detachedWindows[2] ||
            detachedWindows[2]->savedBounds() != savedNative || windows[1]->isVisible())
            return fail("Workspace layout round trip");
        auto *native = detachedWindows[2].get();
        auto corrupt = folder.getChildFile("workspace-corrupt.json");
        corrupt.replaceWithText("{\"version\":1,\"panels\":[]}");
        if (loadWorkspaceLayout(corrupt).wasOk() || detachedWindows[2].get() != native || !floatingWindows[0])
            return fail("Invalid layout partially changed workspace");
        restoreDockedLayout();
        showEditor(1);
        if (!document.isEquivalentTo(controller.project.state()))
            return fail("Layout persistence changed project");
        folder.getChildFile("detached-window-verification.txt")
            .replaceWithText(
                "PASS\n" + evidence +
                "Native peers and component handlers; layout atomic round trip and invalid-input "
                "preservation; simulated negative-origin/disconnected monitor recovery. No OS musical input, "
                "second physical monitor, mixed-DPI transition, hardware or listening claim.\n");
    }
    {
        const auto document = controller.project.state();
        const auto revision = controller.revision;
        restoreDockedLayout();
        for (int i = 0; i < 4; ++i)
            showEditor(i);
        command("workspace.tileBalanced");
        for (const auto &size : {juce::Point<int>{1152, 640}, {1920, 1080}, {2560, 1440}})
        {
            result = snapshot("tiling-balanced-" + juce::String(size.x) + ".png", size.x, size.y,
                              size.x == 2560 ? 1.5f : 1.f);
            if (result.failed())
                return result;
            for (int i = 0; i < 4; ++i)
                for (int j = i + 1; j < 4; ++j)
                    if (windows[size_t(i)]->getBounds().intersects(windows[size_t(j)]->getBounds()))
                        return fail("Balanced tiles overlap");
        }
        showEditor(2);
        command("workspace.tileFocused");
        result = snapshot("tiling-focused-playlist.png", 1600, 940, 1.f);
        if (result.failed() || windows[2]->getWidth() <= windows[0]->getWidth())
            return fail("Focused editor not prominent");
        const auto layout = folder.getChildFile("tiling-workspace.json");
        if (saveWorkspaceLayout(layout).failed())
            return fail("Automatic layout save");
        command("workspace.docked");
        if (loadWorkspaceLayout(layout).failed() || autoTileMode != 2 || tilePrimaryEditor != 2)
            return fail("Automatic layout persistence");
        auto invalid = juce::JSON::parse(layout);
        invalid.getDynamicObject()->setProperty("autoTileMode", 99);
        auto corrupt = folder.getChildFile("tiling-invalid.json");
        corrupt.replaceWithText(juce::JSON::toString(invalid));
        const auto before = windows[2]->getBounds();
        if (loadWorkspaceLayout(corrupt).wasOk() || autoTileMode != 2 || windows[2]->getBounds() != before)
            return fail("Invalid automatic layout partially applied");
        invalid.getDynamicObject()->setProperty("version", 1);
        corrupt.replaceWithText(juce::JSON::toString(invalid));
        if (loadWorkspaceLayout(corrupt).failed() || autoTileMode != 0)
            return fail("Version one layout migration");
        command("workspace.tileBalanced");
        toggleEditor(3);
        toggleEditor(3);
        maximiseEditor(1);
        maximiseEditor(1);
        moveWorkspaceWindow(0, windows[0]->getBounds().translated(10, 10));
        if (autoTileMode != 0 || !floatingWindows[0])
            return fail("Manual dragging did not leave automatic tiling");
        const auto savedCustomization = customization;
        const juce::KeyPress tileKey('T', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier,
                                     0);
        if (customization.bind("workspace.tileBalanced", Scope::global, tileKey).failed() ||
            !keyPressed(tileKey) || autoTileMode != 1 || !browserSplit->isVisible())
            return fail("Custom tiling shortcut or Browser splitter failed");
        customization = savedCustomization;
        for (int i = 0; i < 4; ++i)
            detachWorkspaceWindow(i);
        command("workspace.tileDetached");
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                if (detachedWindows[size_t(i)]->getBounds().intersects(
                        detachedWindows[size_t(j)]->getBounds()))
                    return fail("Native tiles overlap");
        restoreDockedLayout();
        if (!document.isEquivalentTo(controller.project.state()) || controller.revision != revision)
            return fail("Tiling edited musical state/history");
        folder.getChildFile("tiling-verification.txt")
            .replaceWithText(
                "PASS: balanced/focused layouts, compact/1080p/4K component captures, visibility/maximize, "
                "manual movement, v2 round trip/v1 migration/invalid preservation, native peer tiling. "
                "Pattern/Song layout lifecycle measurements in detached-window-verification.txt. "
                "No OS input, physical multi-monitor or listening claim.\n");
    }
    int oldWidth = windows[1]->getWidth();
    browserWidth = 300;
    rackWidth = 520;
    playlistRatio = .45f;
    mixerHeight = 220;
    resized();
    if (browser->getWidth() != 300 || windows[1]->getWidth() == oldWidth)
        return fail("Dock resize did not redistribute area");
    auto normalPiano = windows[1]->getBounds();
    maximiseEditor(1);
    if (windows[1]->getBounds() != workspace)
        return fail("Editor maximise failed");
    maximiseEditor(1);
    if (windows[1]->getBounds() != normalPiano)
        return fail("Editor restore failed");
    keyPressed(juce::KeyPress(juce::KeyPress::F9Key));
    if (windows[3]->isVisible())
        return fail("Mixer collapse failed");
    keyPressed(juce::KeyPress(juce::KeyPress::F9Key));
    if (!windows[3]->isVisible())
        return fail("Mixer restore failed");
    if (!controller.project.state().isEquivalentTo(beforeLayout))
        return fail("Layout changed musical data");
    result = snapshot("populated-resized.png", 1600, 940, 1.f);
    if (result.failed())
        return result;
    if (auto pickerResult = verifyClipPicker(folder); pickerResult.failed())
        return pickerResult;
    if (auto patternResult = verifyPatternWorkflow(folder); patternResult.failed())
        return patternResult;
    folder.getChildFile("gui-smoke.txt")
        .replaceWithText("PASS: actual editor mouse handlers (step on/off, draw note, place clip); "
                         "contextual shortcut handlers (select/duplicate notes, repeated clip duplicate, "
                         "Mixer assignment); Make Unique command; GUI-created project round trip; Tracktion "
                         "WAV render; removed/custom shortcut dispatch; user macro with one undo; profile "
                         "save; populated project; hide/maximise/restore/resize without document mutation; "
                         "dock bounds/toolbar overlap at 1080p and 4K logical viewports with 100/150/200 "
                         "percent render scales; application and Settings component snapshots.\nNOT "
                         "VERIFIED: actual Windows DPI transitions, physical input "
                         "delivery, displayed desktop, listening, hardware latency, human familiarity.\n");
    controller.dirty = false;
    return juce::Result::ok();
}
juce::Result PluginPage::verifyDiscoveryControls(const juce::File &output, const juce::File &fixture)
{
    if (!owner || !owner->controllerTestMode || !owner->pluginTestSettings ||
        (!fixture.existsAsFile() && !fixture.isDirectory()) || output.createDirectory().failed())
        return juce::Result::fail(
            "Catalogue component test requires isolated settings and an original SDK fixture");
    if (!output.getChildFile("verification.json")
             .replaceWithText("{\"passed\":false,\"completionState\":\"pending\"}"))
        return juce::Result::fail("Cannot invalidate preceding catalogue test receipt");
    const auto project = owner->controller.project.state();
    const auto revision = owner->controller.revision;
    auto nativeTypes = effects::nativeDescriptions();
    const auto instruments = nativeInstrumentDescriptions();
    nativeTypes.insert(nativeTypes.begin(), instruments.begin(), instruments.end());
    const auto nativeCount = nativeTypes.size();
    const auto pickerMatches = [&](const auto &visible, const PluginCatalog::Snapshot &catalogue)
    {
        if (visible.size() != nativeCount + catalogue.plugins.size())
            return false;
        for (size_t index = 0; index < nativeCount; ++index)
            if (!(index < instruments.size() ? isNativeInstrument(visible[index])
                            : effects::isNativeDescription(visible[index])) ||
                visible[index].createXml()->toString() != nativeTypes[index].createXml()->toString())
                return false;
        for (size_t index = 0; index < catalogue.plugins.size(); ++index)
            if (effects::isNativeDescription(catalogue.plugins[index]) ||
                isNativeInstrument(catalogue.plugins[index]) ||
                visible[nativeCount + index].createXml()->toString() !=
                    catalogue.plugins[index].createXml()->toString())
                return false;
        return true;
    };
    timerCallback();
    if (nativeTypes.empty() || !pickerMatches(types, owner->plugins.snapshot()))
        return juce::Result::fail(
            "Native Synth and effects must precede the external catalogue without a scan");
    browser.selectRow(0);
    if (!create.isEnabled() || !replace.isEnabled() || insert.isEnabled())
        return juce::Result::fail("Built-in Synth selection must offer instrument Channel actions only");
    browser.selectRow(int(instruments.size())); // First built-in effect, after the native instruments.
    if (create.isEnabled() || replace.isEnabled() || !insert.isEnabled())
        return juce::Result::fail("Built-in effect selection must offer Mixer insertion only");
    const auto invalid = output.getChildFile("invalid-bundle");
    if (invalid.createDirectory().failed() || !invalid.getChildFile("Owned invalid component fixture.vst3")
                                                   .replaceWithText("Owned invalid scanner test input"))
        return juce::Result::fail("Cannot prepare catalogue component fault input");
    // The fixture's build directory may contain other original SDK effects.
    // Scan an exact disposable scope so the same-count refresh oracle really
    // compares one class with one class, independently of sibling build targets.
    const auto primaryFolder = output.getChildFile("primary-sdk-scope");
    if (primaryFolder.createDirectory().failed())
        return juce::Result::fail("Cannot prepare isolated catalogue SDK scope");
    juce::TemporaryFile primary(primaryFolder.getChildFile("Original SDK.vst3"));
    if (!fixture.existsAsFile() || !fixture.copyFileTo(primary.getFile()))
        return juce::Result::fail("Exact single-file original SDK fixture required");
    const juce::StringArray paths{primaryFolder.getFullPathName(), invalid.getFullPathName()};
    folders.setText(paths.joinIntoString("\n"), false);
    standardLocations.setToggleState(false, juce::dontSendNotification);
    const auto wait = [&](PluginCatalog::Snapshot &result)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 15000;
        while (owner->plugins.snapshot().scanning && juce::Time::getMillisecondCounter() < deadline)
            dispatchMessages(5);
        result = owner->plugins.snapshot();
        timerCallback();
        return !result.scanning;
    };
    const auto capture = [&](const char *name)
    {
        auto stream = output.getChildFile(name).createOutputStream();
        if (!stream)
            return false;
        stream->setPosition(0);
        stream->truncate();
        auto *root = getTopLevelComponent();
        return juce::PNGImageFormat{}.writeImageToStream(
            root->createComponentSnapshot(root->getLocalBounds()), *stream);
    };
    scan.onClick();
    timerCallback();
    auto accepted = owner->plugins.snapshot();
    if (accepted.operation.isEmpty() || accepted.requestedFolders != paths ||
        accepted.requestedStandardLocations || scan.isEnabled() || retry.isEnabled() || !cancel.isEnabled() ||
        folders.isEnabled() || standardLocations.isEnabled() || !pickerMatches(types, accepted) ||
        !insert.isEnabled())
        return juce::Result::fail("Actual Scan callback/scope/busy-control availability mismatch");
    PluginCatalog::Snapshot completed;
    if (!wait(completed) || completed.completionState != "completed" || !completed.persisted ||
        completed.plugins.size() != 1 || completed.blacklist.size() != 1 || completed.failures.size() != 1 ||
        !pickerMatches(types, completed) ||
        std::none_of(completed.plugins.begin(), completed.plugins.end(), [&](const auto &p)
                     { return p.fileOrIdentifier == primary.getFile().getFullPathName(); }))
        return juce::Result::fail(
            "Actual Scan callback did not persist original SDK class and isolated failure");
    browser.selectRow(int(nativeCount));
    const auto originalClass = types[nativeCount].createIdentifierString();
    scan.onClick();
    if (!wait(completed) || completed.completionState != "completed" || !completed.failures.isEmpty() ||
        browser.getSelectedRow() < 0 || size_t(browser.getSelectedRow()) >= types.size() ||
        types[size_t(browser.getSelectedRow())].createIdentifierString() != originalClass ||
        !pickerMatches(types, completed))
        return juce::Result::fail("Actual Rescan callback did not skip persisted blacklist");
    retry.onClick();
    if (!wait(completed) || completed.completionState != "completed" || completed.failures.size() != 1 ||
        completed.blacklist.size() != 1 || !completed.persisted || !scan.isEnabled() || !retry.isEnabled() ||
        cancel.isEnabled() || !folders.isEnabled() || !standardLocations.isEnabled() ||
        !pickerMatches(types, completed) || !capture("catalogue-controls-completed.png"))
        return juce::Result::fail("Actual Retry callback/result/idle controls/capture mismatch");
    const auto saved = owner->plugins.settingsFile().loadFileAsString();
    const auto previous = completed;
    const auto alternateFolder = output.getChildFile("alternate-sdk-scope");
    if (!fixture.existsAsFile() || alternateFolder.createDirectory().failed())
        return juce::Result::fail("Single-file original SDK fixture required for same-count UI refresh");
    juce::TemporaryFile alternate(alternateFolder.getChildFile("Original SDK.vst3"));
    if (!fixture.copyFileTo(alternate.getFile()))
        return juce::Result::fail("Cannot prepare exact temporary owned SDK class identity");
    folders.setText(alternateFolder.getFullPathName(), false);
    scan.onClick();
    timerCallback();
    if (!cancel.isEnabled())
        return juce::Result::fail("Pending component scan did not enable Cancel");
    cancel.onClick();
    timerCallback();
    if (cancel.isEnabled() || !wait(completed) || completed.completionState != "cancelled" ||
        completed.persisted || completed.folders != previous.folders ||
        completed.blacklist != previous.blacklist ||
        !std::equal(completed.plugins.begin(), completed.plugins.end(), previous.plugins.begin(),
                    previous.plugins.end(), [](const auto &a, const auto &b)
                    { return a.createXml()->toString() == b.createXml()->toString(); }) ||
        owner->plugins.settingsFile().loadFileAsString() != saved ||
        !project.isEquivalentTo(owner->controller.project.state()) ||
        revision != owner->controller.revision || !pickerMatches(types, completed) ||
        !capture("catalogue-controls-cancelled.png"))
        return juce::Result::fail(
            "Actual Cancel callback changed catalogue/settings/document or failed completion");
    {
        PluginPage reopened(*owner.getComponent());
        if (reopened.folders.getText() != previous.folders.joinIntoString("\n") ||
            reopened.standardLocations.getToggleState() != previous.standardLocations ||
            !pickerMatches(reopened.types, owner->plugins.snapshot()))
            return juce::Result::fail(
                "Reopened Settings resurrected a cancelled scope as configured folders");
    }
    // A scan can finish entirely between UI timer polls. Replace the original
    // project-owned SDK fixture with an identical temporary file at a new class
    // path, keeping the class count unchanged. Never copy an installed vendor.
    stopTimer();
    folders.setText(alternateFolder.getFullPathName(), false);
    scan.onClick();
    const auto idleDeadline = juce::Time::getMillisecondCounter() + 15000;
    while (owner->plugins.snapshot().scanning && juce::Time::getMillisecondCounter() < idleDeadline)
        juce::Thread::sleep(1); // Test-only: deliberately no UI polling/message dispatch.
    completed = owner->plugins.snapshot();
    timerCallback();
    auto *trace = new juce::DynamicObject;
    juce::var observation(trace);
    trace->setProperty("scanning", completed.scanning);
    trace->setProperty("scanState", completed.completionState);
    trace->setProperty("previousClassCount", int(previous.plugins.size()));
    trace->setProperty("nativeEffectCount", int(nativeCount));
    trace->setProperty("expectedPath", alternate.getFile().getFullPathName());
    juce::Array<juce::var> visiblePaths, discoveredPaths;
    for (const auto &type : types)
        visiblePaths.add(type.fileOrIdentifier);
    for (const auto &type : completed.plugins)
        discoveredPaths.add(type.fileOrIdentifier);
    trace->setProperty("visibleClassPaths", visiblePaths);
    trace->setProperty("discoveredClassPaths", discoveredPaths);
    if (!output.getChildFile("fast-refresh-observation.json")
             .replaceWithText(juce::JSON::toString(observation)))
        return juce::Result::fail("Cannot retain actual same-count UI/backend observation");
    if (completed.scanning || completed.completionState != "completed" ||
        completed.plugins.size() != previous.plugins.size() || !pickerMatches(types, completed) ||
        types[nativeCount].fileOrIdentifier != alternate.getFile().getFullPathName() ||
        browser.getSelectedRow() != -1)
        return juce::Result::fail(
            "Fast completed same-count scan left a stale class in the actual Settings list");
    if (!capture("catalogue-controls-fast-refresh.png"))
        return juce::Result::fail("Cannot capture refreshed Settings class");
    if (!project.isEquivalentTo(owner->controller.project.state()) || revision != owner->controller.revision)
        return juce::Result::fail("Catalogue identity/selection refresh changed the musical project");
    // Exercise the actual picker button through the canonical Channel command,
    // separately from the project-neutral discovery/scanning assertions above.
    browser.selectRow(0);
    create.onClick();
    const auto createdChannel = owner->controller.project.channels.back().id;
    const auto *created = owner->controller.project.channel(createdChannel);
    if (owner->controller.project.channels.size() !=
            project.getChildWithName("CHANNELS").getNumChildren() + 1 ||
        !created || !created->instrument || owner->selectedChannel != createdChannel)
        return juce::Result::fail("Actual native picker New Channel did not create/select one instrument");
    juce::PluginDescription createdDescription;
    auto createdXml = juce::parseXML(created->instrument->descriptor);
    if (!createdXml || !createdDescription.loadFromXml(*createdXml) ||
        !synth::isNativeDescription(createdDescription))
        return juce::Result::fail("Actual native picker created the wrong descriptor");
    const auto createdPlugin = created->instrument->id;
    const auto audioPending = [&]
    {
        return bool(owner->mcpCall(
            "vibera_state", juce::var(new juce::DynamicObject))["structuredContent"]["pendingAudioUpdate"]);
    };
    const auto readyDeadline = juce::Time::getMillisecondCounter() + 10000;
    while (audioPending() && juce::Time::getMillisecondCounter() < readyDeadline)
        dispatchMessages(5);
    const auto prepared = owner->audio.preparedPluginHost(owner->controller.project, createdPlugin);
    if (audioPending() || !prepared || !prepared->alive())
        return juce::Result::fail("Actual picker-created native Synth did not prepare its owned worker");
    if (!capture("catalogue-native-synth-created.png"))
        return juce::Result::fail("Cannot capture native Synth picker creation");
    if (!owner->performAction("edit.undo"))
        return juce::Result::fail("Native picker Undo admission failed: " + owner->status);
    const auto undoDeadline = juce::Time::getMillisecondCounter() + 10000;
    while ((owner->mixerChainBusy() || audioPending()) &&
           juce::Time::getMillisecondCounter() < undoDeadline)
        dispatchMessages(5);
    if (!project.isEquivalentTo(owner->controller.project.state()))
    {
        output.getChildFile("native-picker-before.xml").replaceWithText(project.toXmlString());
        output.getChildFile("native-picker-after.xml")
            .replaceWithText(owner->controller.project.state().toXmlString());
        return juce::Result::fail(
            "Native picker Channel creation Undo did not restore the exact source project: " + owner->status);
    }
    auto *evidence = new juce::DynamicObject;
    juce::var document(evidence);
    evidence->setProperty("passed", true);
    evidence->setProperty(
        "applicationSha256",
        juce::SHA256(juce::File::getSpecialLocation(juce::File::currentExecutableFile)).toHexString());
    evidence->setProperty(
        "scope",
        "Actual JUCE component callbacks and original SDK scanner, no OS input or third-party qualification");
    juce::Array<juce::var> checks;
    for (const auto *item :
         {"Scan callback passes exact typed scope and persists SDK class/isolated failure",
          "Native instrument/effect prefix is available before scanning, throughout scan/cancel and after "
          "reopening",
          "ui.native-picker.create-synth-channel",
          "Native picker-created Synth owns a prepared worker and exact one-step Undo",
          "Built-in effect selection offers Mixer insertion without instrument actions",
          "Busy and idle control availability matches actual scan state",
          "Rescan skips persisted blacklist; Retry callback reruns failed bundle",
          "Cancel callback reaches actual cancellation and preserves prior XML/catalogue/blacklist",
          "Completed scan between UI polls refreshes an unchanged-size class list with the exact new "
          "identity",
          "Rescan preserves class selection; replacement clears it; reopened Settings exclude cancelled "
          "scope",
          "Scanning preserves project state/revision; native creation/Undo separately restores exact project "
          "state"})
        checks.add(item);
    evidence->setProperty("checks", checks);
    return output.getChildFile("verification.json").replaceWithText(juce::JSON::toString(document, true))
               ? juce::Result::ok()
               : juce::Result::fail("Cannot write catalogue component verification");
}
juce::Result Studio::verifyPluginCatalogueControls(const juce::File &output, const juce::File &fixture)
{
    SettingsContent settings(*this);
    settings.setCurrentTabIndex(3);
    if (auto *holder = settings.getCurrentContentComponent())
        for (int i = 0; i < holder->getNumChildComponents(); ++i)
            if (auto *viewport = dynamic_cast<juce::Viewport *>(holder->getChildComponent(i)))
                if (auto *page = dynamic_cast<PluginPage *>(viewport->getViewedComponent()))
                {
                    const auto result = page->verifyDiscoveryControls(output, fixture);
                    if (result.wasOk() &&
                        !output.getChildFile("alternate-sdk-scope")
                             .findChildFiles(juce::File::findFilesAndDirectories, false, "*.vst3")
                             .isEmpty())
                        return juce::Result::fail("Temporary original SDK fixture was not retired");
                    return result;
                }
    return juce::Result::fail("Actual Settings Plugins component unavailable");
}
#include "StudioAutomationRecording.inc"
#include "StudioAutomationTouchAcceptance.inc"
#include "StudioDevicePreparation.inc"
#include "StudioRoutingCoordinator.inc"
#include "StudioRoutingCoordinatorAcceptance.inc"
#include "StudioMixerTargetAcceptance.inc"
} // namespace vibera

namespace vibera
{
juce::var Studio::pianoNoteModes(const juce::var &args)
{
    auto *editor = static_cast<Editor *>(windows[1]->getContentComponent());
    try
    {
        return editor->noteModes(args);
    }
    catch (const McpFailure &error)
    {
        auto result = juce::var(new juce::DynamicObject), detail = juce::var(new juce::DynamicObject);
        detail.getDynamicObject()->setProperty("code", error.code);
        detail.getDynamicObject()->setProperty("message", juce::String(error.what()));
        result.getDynamicObject()->setProperty("completionState", "failed");
        result.getDynamicObject()->setProperty("error", detail);
        const auto operation = args["operation"].toString();
        if (operation.isNotEmpty() && !editor->ownsNotePropertiesOperation(operation))
            return result;
        status = juce::String(error.what());
        editor->noteModesError(status, operation);
        repaint();
        return result;
    }
}
juce::Result Studio::verifyNoteModesWorkflow(const juce::File &folder)
{
    return static_cast<Editor *>(windows[1]->getContentComponent())->verifyNoteModes(folder);
}
} // namespace vibera
#include "../tests/PianoNoteModesAcceptance.inc"

namespace vibera
{
juce::Result Studio::linkAllMixerParameters(int insert, int &firstCc)
{
    const auto *track = mixerInsert(controller.project, insert);
    if (!track)
        return juce::Result::fail("mixer.linkAll: Choose an existing Mixer track");
    struct Parameter
    {
        const char *action, *name;
        double minimum, maximum;
    };
    static constexpr Parameter parameters[]{{"mixer.gain", "Volume", 0, 1.5},
                                            {"mixer.pan", "Pan", -1, 1},
                                            {"mixer.stereoSeparation", "Stereo separation", -1, 1}};
    const auto binding = [&](const Parameter &parameter, int cc)
    {
        ControllerBinding b;
        b.port = 1;
        b.channel = 1;
        b.kind = 0;
        b.number = cc;
        b.mode = 1; // Absolute.
        b.action = parameter.action;
        b.minimum = parameter.minimum;
        b.maximum = parameter.maximum;
        b.mixer = insert;
        b.label = track->name + " " + parameter.name;
        return b;
    };
    // Three consecutive CCs that no enabled mapping uses yet (20..119, the common free range).
    firstCc = -1;
    for (int cc = 20; cc + 2 <= 119 && firstCc < 0; ++cc)
        if (std::none_of(std::begin(parameters), std::end(parameters),
                         [&](const Parameter &parameter)
                         {
                             const auto candidate = binding(parameter, cc + int(&parameter - parameters));
                             return midiController.conflicts(candidate, std::numeric_limits<size_t>::max());
                         }))
            firstCc = cc;
    if (firstCc < 0)
        return juce::Result::fail("mixer.linkAll: No three free MIDI CCs are left (20..119)");
    for (int i = 0; i < 3; ++i)
    {
        juce::String profile;
        if (auto identity = midiController.bindingProfileIdentity(profile); identity.failed())
            return identity;
        if (auto added = midiController.setBinding(midiController.bindings.size(), binding(parameters[i], firstCc + i),
                                                   profile);
            added.failed())
            return added;
    }
    return juce::Result::ok();
}
} // namespace vibera
