// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "../tests/SyntheticDevice.h"

namespace vibera
{
juce::Result Studio::verifyAutomation(const juce::File &folder)
{
    auto fail = [](const char *message) { return juce::Result::fail(message); };
    if (folder.createDirectory().failed())
        return fail("Automation GUI output directory");
    auto fixture = Project::empty();
    fixture.channels.resize(1);
    fixture.channels[0].source = "builtin:bass";
    fixture.patterns[0].notes.push_back({fixture.allocate(), fixture.channels[0].id, 0, 4, 48, .8f, false});
    fixture.placements.push_back({fixture.allocate(), fixture.patterns[0].id, 0, 0, 4, 0});
    controller.replace(fixture);
    selectedPattern = fixture.patterns[0].id;
    selectedChannel = fixture.channels[0].id;
    patternMode = false;
    mode.setButtonText("Song");
    setSize(1600, 940);
    resized();
    showEditor(2);
    CommandArgs a;
    a.mixer = 0;
    a.beat = 0;
    a.length = 4;
    a.lane = 1;
    command("automation.create", a);
    if (controller.project.automation.placements.size() != 1)
        return fail("GUI automation creation");
    const auto clip = controller.project.automation.placements.front().id;
    const auto source = controller.project.automation.sources.front().id;
    auto *editor = static_cast<Editor *>(windows[2]->getContentComponent());
    const auto clipBounds = editor->playlistClipBounds(clip);
    if (clipBounds.getWidth() < 45 || clipBounds.getHeight() < 30)
        return fail("Automation clip visible geometry");
    auto event =
        [](juce::Component *component, juce::Point<float> point, juce::Point<float> origin, int modifiers)
    {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(modifiers), 1.f, 0.f, 0.f, 0.f, 0.f, component, component,
                                juce::Time::getCurrentTime(), origin, juce::Time::getCurrentTime(), 1, false);
    };
    const auto insertAt = juce::Point<float>{clipBounds.getCentreX(), clipBounds.getBottom() - 8};
    editor->mouseDown(event(editor, insertAt, insertAt, juce::ModifierKeys::rightButtonModifier));
    editor->mouseUp(event(editor, insertAt, insertAt, 0));
    if (controller.project.automationSource(source)->points.size() != 3)
        return fail("Right-click curve inserts point");
    const auto point = controller.project.automationSource(source)->points[1].id;
    const auto beforeDrag = controller.project.state();
    const auto from = editor->automationPointLocation(clip, point);
    const auto to = from + juce::Point<float>{12, -7};
    editor->mouseDown(event(editor, from, from, juce::ModifierKeys::leftButtonModifier));
    editor->mouseDrag(event(editor, to, from, juce::ModifierKeys::leftButtonModifier));
    editor->mouseUp(event(editor, to, from, 0));
    const auto moved = controller.project.state();
    if (moved.isEquivalentTo(beforeDrag) || controller.hasGesture())
        return fail("Actual curve drag commits");
    controller.undoEdit();
    if (!beforeDrag.isEquivalentTo(controller.project.state()))
        return fail("GUI point drag one Undo");
    controller.redoEdit();
    if (!moved.isEquivalentTo(controller.project.state()))
        return fail("GUI point drag exact Redo");
    const auto shifted = editor->automationPointLocation(clip, point);
    editor->mouseDown(event(editor, shifted, shifted, juce::ModifierKeys::leftButtonModifier));
    editor->mouseDrag(
        event(editor, shifted + juce::Point<float>{8, -4}, shifted, juce::ModifierKeys::leftButtonModifier));
    editor->cancelPointerGesture();
    if (!moved.isEquivalentTo(controller.project.state()) || controller.hasGesture())
        return fail("GUI automation Escape rollback");
    const auto middlePoint = [&]() -> AutomationPoint
    {
        for (const auto &candidate : controller.project.automationSource(source)->points)
            if (candidate.id == point)
                return candidate;
        return {};
    };
    // Use the real shortcut/gesture path. Axis constraints must preserve the original unsnapped value.
    for (const auto modifier : {juce::ModifierKeys::ctrlModifier, juce::ModifierKeys::shiftModifier})
    {
        const auto before = middlePoint();
        const auto start = editor->automationPointLocation(clip, point);
        editor->mouseDown(event(editor, start, start, juce::ModifierKeys::leftButtonModifier));
        const float verticalDelta = before.value >= .5f ? 5.f : -5.f;
        editor->mouseDrag(event(editor, start + juce::Point<float>{10, verticalDelta}, start,
                                juce::ModifierKeys::leftButtonModifier | modifier));
        editor->mouseUp(event(editor, start, start, 0));
        const auto after = middlePoint();
        if ((modifier == juce::ModifierKeys::ctrlModifier &&
             (after.beat != before.beat || after.value == before.value)) ||
            (modifier == juce::ModifierKeys::shiftModifier &&
             (after.value != before.value || after.beat == before.beat)))
            return juce::Result::fail(
                "Automation point axis modifier=" + juce::String(modifier) +
                " before=" + juce::String(before.beat, 6) + "/" + juce::String(before.value, 6) +
                " after=" + juce::String(after.beat, 6) + "/" + juce::String(after.value, 6));
        controller.undoEdit();
        if (!moved.isEquivalentTo(controller.project.state()))
            return fail("Axis drag exact Undo");
    }
    // Insertion and continuous right-drag are one cancellable, undoable operation.
    const auto area = editor->playlistClipBounds(clip).withTrimmedTop(19).reduced(4, 3);
    const auto insertStart = juce::Point<float>{area.getX() + area.getWidth() * .25f, area.getBottom()};
    const auto insertedId = controller.project.nextId;
    const auto beforeInsert = controller.project.state();
    editor->mouseDown(event(editor, insertStart, insertStart, juce::ModifierKeys::rightButtonModifier));
    editor->mouseDrag(event(editor, insertStart + juce::Point<float>{12, -8}, insertStart,
                            juce::ModifierKeys::rightButtonModifier));
    editor->mouseUp(event(editor, insertStart, insertStart, 0));
    auto inserted = std::find_if(controller.project.automationSource(source)->points.begin(),
                                 controller.project.automationSource(source)->points.end(),
                                 [&](const auto &p) { return p.id == insertedId; });
    if (inserted == controller.project.automationSource(source)->points.end() || inserted->value <= 0 ||
        inserted->beat <= 1)
        return fail("Right insertion continues dragging newly allocated point");
    controller.undoEdit();
    if (!beforeInsert.isEquivalentTo(controller.project.state()))
        return fail("Insert and right-drag one exact Undo");
    const auto expectedLevel = automationSourceValueAt(*controller.project.automationSource(source), 1);
    editor->mouseDown(event(editor, insertStart, insertStart,
                            juce::ModifierKeys::rightButtonModifier | juce::ModifierKeys::shiftModifier));
    editor->mouseUp(event(editor, insertStart, insertStart, 0));
    inserted = std::find_if(controller.project.automationSource(source)->points.begin(),
                            controller.project.automationSource(source)->points.end(),
                            [&](const auto &p) { return p.id == insertedId; });
    if (inserted == controller.project.automationSource(source)->points.end() ||
        std::abs(inserted->value - expectedLevel) > 1.e-6f)
        return fail("Shift-right insertion preserves curve level instead of mouse height");
    controller.undoEdit();
    // Trim uses source beats, preserving shared source data and undoing/cancelling exactly.
    const auto trimStart = juce::Point<float>{clipBounds.getX() + 1, clipBounds.getBottom() - 5};
    editor->mouseDown(event(editor, trimStart, trimStart, juce::ModifierKeys::leftButtonModifier));
    editor->mouseDrag(event(editor, trimStart + juce::Point<float>{12, 0}, trimStart,
                            juce::ModifierKeys::leftButtonModifier));
    editor->mouseUp(event(editor, trimStart, trimStart, 0));
    const auto &trimmed = controller.project.automation.placements.front();
    if (trimmed.beat <= 0 || trimmed.offset != trimmed.beat || trimmed.length + trimmed.offset != 4 ||
        controller.project.automationSource(source)->points.size() != 3)
        return fail("Automation left border trims source window without modifying source points");
    controller.undoEdit();
    if (!moved.isEquivalentTo(controller.project.state()))
        return fail("Source window trim exact Undo");
    const auto escStart = editor->automationPointLocation(clip, point);
    editor->mouseDown(event(editor, escStart, escStart, juce::ModifierKeys::leftButtonModifier));
    editor->mouseDrag(event(editor, escStart + juce::Point<float>{9, -3}, escStart,
                            juce::ModifierKeys::leftButtonModifier));
    if (!editor->pointerGestureActive() || !keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)))
        return fail("Automation Escape shortcut dispatch");
    editor->mouseUp(event(editor, escStart, escStart, 0));
    if (!moved.isEquivalentTo(controller.project.state()) || editor->pointerGestureActive())
        return fail("Actual Escape key cancels automation point gesture");
    a.target = clip;
    command("automation.duplicate", a);
    const auto duplicate = controller.project.automation.placements.back().id;
    selectedClips = {duplicate};
    focus = Scope::playlist;
    refresh();
    if (selectedClips != std::vector<Id>{duplicate})
        return fail("Automation selection survives refresh");
    a.target = duplicate;
    command("clip.unique", a);
    if (controller.project.automation.sources.size() != 2 ||
        !controller.project.automation.sources.back().links.empty())
        return fail("Playlist Make Unique independent automation target");
    a.target = controller.project.automation.sources.back().id;
    a.mixer = 0;
    command("automation.link", a);
    a.target = point;
    a.value = -.25;
    a.text = "smooth";
    command("automation.point.shape", a);
    const auto beforeProperties = controller.project.state();
    const auto header = editor->playlistClipBounds(clip).getTopLeft() + juce::Point<float>{24, 8};
    editor->mouseDoubleClick(event(editor, header, header, juce::ModifierKeys::leftButtonModifier));
    auto *properties = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!properties)
        return fail("Automation source properties did not open");
    properties->getTextEditor("name")->setText("Original fade");
    properties->getComboBoxComponent("target")->setSelectedItemIndex(17, juce::sendNotificationSync);
    properties->getTextEditor("minimum")->setText("1.8");
    properties->getTextEditor("maximum")->setText("0.2");
    properties->exitModalState(1);
    dispatchMessages(30);
    const auto *updated = controller.project.automationSource(source);
    if (!updated || updated->name != "Original fade" || updated->links.size() != 2 ||
        std::abs(updated->links.back().minimum - 1.8f) > 1.e-6f ||
        std::abs(updated->links.back().maximum - .2f) > 1.e-6f)
        return fail("Actual source properties rename/link/inverted range");
    const auto editedProperties = controller.project.state();
    controller.undoEdit();
    if (!beforeProperties.isEquivalentTo(controller.project.state()))
        return fail("Source properties one exact Undo");
    controller.redoEdit();
    if (!editedProperties.isEquivalentTo(controller.project.state()))
        return fail("Source properties exact Redo");
    editor->mouseDoubleClick(event(editor, header, header, juce::ModifierKeys::leftButtonModifier));
    properties = dynamic_cast<juce::AlertWindow *>(juce::Component::getCurrentlyModalComponent());
    if (!properties)
        return fail("Automation source invalid-value fixture");
    properties->getTextEditor("name")->setText("Must roll back");
    properties->getTextEditor("minimum")->setText("-1");
    properties->exitModalState(1);
    dispatchMessages(30);
    if (!editedProperties.isEquivalentTo(controller.project.state()) || controller.hasGesture())
        return fail("Invalid target range must roll back whole properties transaction");
    const auto path = folder.getChildFile("automation-gui.vibra");
    if (saveProject(controller.project, path).failed())
        return fail("GUI automation save");
    Project reopened;
    if (loadProject(path, reopened).failed() || !reopened.state().isEquivalentTo(controller.project.state()))
        return fail("GUI automation disk round trip");
    const auto wav = folder.getChildFile("automation-gui.wav");
    if (audio.renderSync(controller.project, wav).failed())
        return fail("GUI automation real export");
    // Exercise a real injected MIDI take on a virtual device while the actual curve handlers run.
    // Separate Pattern/Song checks preserve the sampler, note gates, device and capture session.
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
        return fail("Automation MIDI-take virtual device unavailable");
    juce::AudioBuffer<float> buffer(2, 256);
    juce::String recordingEvidence;
    for (bool pattern : {true, false})
    {
        patternMode = pattern;
        mode.setButtonText(pattern ? "Pattern" : "Song");
        if (auto result = audio.rebuild(controller.project, pattern, selectedPattern); result.failed())
            return result;
        needsRebuild = false;
        audio.seekBeat(0);
        audio.playStop();
        dispatchMessages(20);
        for (int i = 0; i < 12; ++i)
            device->process(buffer);
        record.onClick();
        if (!midiTaking || !audio.midiInput.capture.active.load())
            return fail("Automation edits fixture did not arm real MIDI capture");
        const auto session = audio.midiInput.capture.session.load();
        const auto beforeProjection = audio.projectionDiagnostics();
        const auto beforeGainStages = audio.mixerGraphInitialisations();
        const int starts = device->starts, stops = device->stops;
        const auto beforeNotes = controller.project.pattern(selectedPattern)->notes.size();
        const auto pitch = pattern ? 64 : 67;
        audio.midiInput.handleIncomingMidiMessage(nullptr,
                                                  juce::MidiMessage::noteOn(1, pitch, juce::uint8(96)));
        double maximumMs = 0;
        int overruns = 0;
        for (int edit = 0; edit < 12; ++edit)
        {
            const auto beforeClone = controller.project.state();
            const auto beforeCloneCount = controller.project.automation.sources.size();
            CommandArgs cloneArgs;
            cloneArgs.target = source;
            cloneArgs.text = edit % 2 ? "preserve" : "clear";
            command("automation.source.clone", cloneArgs);
            if (controller.project.automation.sources.size() != beforeCloneCount + 1)
                return fail("Automation source clone refused during held MIDI playback");
            const auto cloned = controller.project.state();
            // Canonical history/admission; global UI Undo intentionally finishes a take first.
            if (controller.undoEditChecked().failed() ||
                !beforeClone.isEquivalentTo(controller.project.state()))
                return fail("Automation clone Undo during held MIDI playback");
            if (controller.redoEditChecked().failed() || !cloned.isEquivalentTo(controller.project.state()))
                return fail("Automation clone Redo during held MIDI playback");
            if (controller.undoEditChecked().failed() ||
                !beforeClone.isEquivalentTo(controller.project.state()))
                return fail("Automation clone second Undo during held MIDI playback");
            const auto from = editor->automationPointLocation(clip, point);
            editor->mouseDown(event(editor, from, from, juce::ModifierKeys::leftButtonModifier));
            editor->mouseDrag(
                event(editor, from + juce::Point<float>{0, edit % 2 ? -2.f : 2.f}, from,
                      juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier));
            if (edit == 3)
            {
                if (!keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)))
                    return fail("Automation Escape dispatch during MIDI take");
            }
            editor->mouseUp(event(editor, from, from, 0));
            timerCallback();
            for (int tick = 0; tick < 8; ++tick)
            {
                const auto start = juce::Time::getMillisecondCounterHiRes();
                device->process(buffer);
                const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
                maximumMs = std::max(maximumMs, ms);
                overruns += ms >= 256000. / 48000.;
            }
            drainMidiTake();
            if (!midiTaking || !audio.midiInput.capture.active.load() ||
                audio.midiInput.capture.session.load() != session || takePattern != selectedPattern ||
                takePatternMode != pattern || midiPreview.empty())
                return fail("Automation point edit/Escape interrupted the held-note MIDI take");
        }
        audio.midiInput.handleIncomingMidiMessage(nullptr, juce::MidiMessage::noteOff(1, pitch));
        device->process(buffer);
        drainMidiTake();
        const auto afterProjection = audio.projectionDiagnostics();
        const bool retained = beforeProjection.edit == afterProjection.edit &&
                              beforeProjection.playbackContext == afterProjection.playbackContext &&
                              beforeProjection.replacements == afterProjection.replacements &&
                              starts == device->starts && stops == device->stops &&
                              beforeGainStages == audio.mixerGraphInitialisations() &&
                              beforeProjection.channels.size() == afterProjection.channels.size();
        if (!retained || overruns)
            return juce::Result::fail(
                "Automation MIDI take lifecycle/deadline: retained=" + juce::String(int(retained)) +
                " overruns=" + juce::String(overruns) + " maxMs=" + juce::String(maximumMs, 6));
        for (size_t i = 0; i < beforeProjection.channels.size(); ++i)
            if (beforeProjection.channels[i].sampler != afterProjection.channels[i].sampler ||
                beforeProjection.channels[i].sample != afterProjection.channels[i].sample ||
                beforeProjection.channels[i].graphInitialisations !=
                    afterProjection.channels[i].graphInitialisations)
                return fail("Automation MIDI take reset sampler/sample/gates");
        const auto beforeTakeCommit = controller.project.state();
        stop.onClick();
        const auto *patternState = controller.project.pattern(selectedPattern);
        if (midiTaking || !patternState || patternState->notes.size() != beforeNotes + 1 ||
            std::none_of(patternState->notes.begin(), patternState->notes.end(), [&](const auto &n)
                         { return n.pitch == pitch && n.channel == selectedChannel && n.length > 0; }))
            return fail("Stop did not commit preserved automation-edit MIDI take");
        controller.undoEdit();
        if (!beforeTakeCommit.isEquivalentTo(controller.project.state()))
            return fail("MIDI take one Undo must retain prior automation edits");
        controller.redoEdit();
        recordingEvidence +=
            (pattern ? "Pattern" : "Song") +
            juce::String(
                ": held injected MIDI note through12 source clones with clear/preserve links and "
                "exact canonical Undo/Redo (global UI Undo finishes a take),12 curve drags and actual Escape "
                "cancellation;96 measured callbacks maxMs=") +
            juce::String(maximumMs, 6) +
            "; zero overruns, retained session/device/Edit/context/gain and sampler graphs; "
            "Stop commits one exact Undo/Redo.\n";
    }
    folder.getChildFile("automation-midi-take.txt")
        .replaceWithText("PASS\n" + recordingEvidence +
                         "Injected MIDI and virtual audio; no physical input/listening claim.\n");
    const auto recordedFile = folder.getChildFile("automation-recorded.vibra");
    Project recorded;
    if (saveProject(controller.project, recordedFile).failed() ||
        loadProject(recordedFile, recorded).failed() ||
        !recorded.state().isEquivalentTo(controller.project.state()))
        return fail("Recorded MIDI and edited curves exact file round trip");
    if (audio.renderSync(recorded, folder.getChildFile("automation-recorded.wav")).failed())
        return fail("Recorded MIDI and edited automation real Song export");
    for (const auto size : {juce::Point<int>{1000, 650}, juce::Point<int>{1600, 940}})
    {
        setSize(size.x, size.y);
        resized();
        refresh();
        auto output = folder.getChildFile("automation-" + juce::String(size.x) + ".png").createOutputStream();
        if (!output)
            return fail("GUI automation PNG stream");
        output->setPosition(0);
        output->truncate();
        if (!juce::PNGImageFormat{}.writeImageToStream(createComponentSnapshot(getLocalBounds()), *output))
            return fail("GUI automation PNG capture");
    }
    folder.getChildFile("automation-gui-verification.txt")
        .replaceWithText(
            "PASS: real component right-click point insertion/continuous drag/Shift preserved value, axis "
            "modifiers, source-window trim, actual Escape and point drag/Undo/Redo, held MIDI take "
            "preservation, linked "
            "duplicate, Playlist Make Unique/relink, curve shape, actual source "
            "properties/link/range/Undo/Redo and invalid-value rollback, selection refresh, exact "
            "save/reopen and "
            "DAW export. Fresh compact/1600px component snapshots. Creation invoked through canonical "
            "command; Mixer popup selection, native OS input, listening and hardware are separate gates.\n");
    return juce::Result::ok();
}
} // namespace vibera
