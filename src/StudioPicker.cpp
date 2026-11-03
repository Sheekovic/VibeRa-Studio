// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "../tests/PickerAudioFixture.h"

namespace vibera
{
juce::Result Studio::verifyClipPicker(const juce::File &folder)
{
    folder.createDirectory();
    audio.stop();
    const auto previous = controller.project;
    const auto previousPattern = selectedPattern, previousChannel = selectedChannel;
    auto *editor = static_cast<Editor *>(windows[2]->getContentComponent());
    auto *picker = editor->clipPicker();
    auto restore = [&]
    {
        controller.replace(previous);
        selectedPattern = previousPattern;
        selectedChannel = previousChannel;
        picker->setKind(0);
        picker->setFilter({});
        picker->setGroupFilter({});
        refresh();
    };
    auto fail = [&](const char *message)
    {
        folder.getChildFile("picker-failure.txt").replaceWithText(message);
        restore();
        return juce::Result::fail(message);
    };
    auto project = Project::demo();
    project.name = "Picker Session";
    const auto first = project.patterns.front().id;
    auto secondPattern = project.patterns.front();
    secondPattern.id = project.allocate();
    secondPattern.name = "Second Pattern";
    for (auto &note : secondPattern.notes)
        note.id = project.allocate();
    const auto second = secondPattern.id;
    project.patterns.push_back(std::move(secondPattern));
    for (int i = 0; i < 22; ++i)
    {
        auto copy = project.patterns.front();
        copy.id = project.allocate();
        copy.name = "Variation " + juce::String(i + 1);
        copy.group = i % 2 ? "Verse" : "Chorus";
        for (auto &n : copy.notes)
            n.id = project.allocate();
        project.patterns.push_back(std::move(copy));
    }
    Channel audioSource;
    audioSource.id = project.allocate();
    const auto audioFile = pickerAudioFixture(folder);
    if (!audioFile.existsAsFile())
        return fail("Picker audio fixture preparation");
    audioSource.source = audioFile.getFullPathName();
    audioSource.name = "Audio test source";
    audioSource.audioClip = true;
    audioSource.audioDuration = 2.5;
    project.channels.push_back(audioSource);
    AutomationSource curve;
    curve.id = project.allocate();
    curve.name = "Master motion";
    curve.length = 4;
    curve.points = {{project.allocate(), 0, .2f}, {project.allocate(), 4, .8f}};
    curve.links = {{{AutomationParameter::mixerGain, 0, 0}, 0, 1}};
    project.automation.sources.push_back(curve);
    if (project.validate().failed())
        return fail("Picker GUI fixture invalid");
    controller.replace(project);
    selectedPattern = first;
    refresh();
    picker->setKind(0);
    picker->setFilter({});
    picker->setGroupFilter({});
    const auto original = controller.project.state();
    if (picker->visibleIds().size() != project.patterns.size() || !picker->isVisible())
        return fail("Picker omitted Pattern sources or is not visible");
    picker->selectIds({first, second});
    if (picker->selectedIds() != std::vector<Id>{first, second} || selectedPattern != second)
        return fail("Picker multi-selection did not retain source identities");
    picker->group({first, second}, "Daily beat");
    picker->setGroupFilter("Daily beat");
    if (picker->visibleIds() != std::vector<Id>{first, second})
        return fail("Picker group filter mismatch");
    picker->setFilter(project.pattern(first)->name.toUpperCase());
    if (picker->visibleIds() != std::vector<Id>{first})
        return fail("Picker text filter mismatch");
    picker->setFilter({});
    picker->selectIds({first, second});
    const auto grouped = controller.project.state();
    auto description = picker->dragDescription();
    CommandArgs decoded;
    if (!pickerPayload(description, controller.projectIdentity, decoded) ||
        decoded.ids != std::vector<Id>{first, second})
        return fail("Picker typed drag does not contain selected source IDs");
    const auto before = controller.project.placements.size();
    const auto point = editor->playlistPosition(8, 2);
    juce::DragAndDropTarget::SourceDetails drop(description, picker, point);
    if (!editor->isInterestedInDragSource(drop))
        return fail("Playlist rejected current-project Picker drag");
    editor->itemDropped(drop);
    if (controller.project.placements.size() != before + 2 ||
        controller.project.placements[before].pattern != first ||
        controller.project.placements[before + 1].pattern != second ||
        controller.project.placements[before].lane != 2 ||
        controller.project.placements[before + 1].lane != 3)
        return fail("Actual Playlist drop handler did not place linked source selection");
    const auto placed = controller.project.state();
    controller.undoEdit();
    if (!grouped.isEquivalentTo(controller.project.state()))
        return fail("Picker multi-drop was not one exact Undo");
    controller.redoEdit();
    if (!placed.isEquivalentTo(controller.project.state()))
        return fail("Picker multi-drop Redo mismatch");
    if (editor->placePickerSource(description, {2, point.y}))
        return fail("Picker drop on sidebar should not place a timeline clip");
    controller.replace(project);
    refresh();
    if (editor->isInterestedInDragSource(drop) || editor->placePickerSource(description, point) ||
        !original.isEquivalentTo(controller.project.state()))
        return fail("Stale project drag changed the replacement project");
    for (int kind : {1, 2})
    {
        picker->setKind(kind);
        const auto source = kind == 1 ? audioSource.id : curve.id;
        if (picker->visibleIds() != std::vector<Id>{source})
            return fail("Audio/Automation picker source filter mismatch");
        picker->selectIds({source});
        const auto selection = editor->pickerSelection();
        if (selection.ids != std::vector<Id>{source} ||
            selection.text != (kind == 1 ? "audio" : "automation"))
            return fail("Source click did not select Audio/Automation drawing source");
        auto payload = picker->dragDescription();
        if (!editor->placePickerSource(payload, editor->playlistPosition(8, kind)))
            return fail("Audio/Automation picker linked placement failed");
        controller.undoEdit();
        if (!original.isEquivalentTo(controller.project.state()))
            return fail("Audio/Automation picker placement Undo mismatch");
    }
    // Both context choices use the same canonical command, then select a fresh unused source.
    picker->setKind(2);
    picker->setFilter({});
    picker->selectIds({curve.id});
    const auto cloneMenu = ClipPicker::automationSourceMenu({curve.id});
    if (cloneMenu.getNumItems() != 2)
        return fail("Automation Picker clone context choices unavailable");
    for (bool preserveLinks : {false, true})
    {
        const auto baseline = controller.project.state();
        if (picker->cloneAutomationSource(curve.id, preserveLinks).failed())
            return fail("Automation Picker source clone command refused");
        const auto &copy = controller.project.automation.sources.back();
        const auto cloneId = copy.id;
        const auto cloned = controller.project.state();
        if (copy.id == curve.id || copy.points.front().id == curve.points.front().id ||
            copy.links.size() != (preserveLinks ? curve.links.size() : 0) ||
            controller.project.automation.placements.size() != project.automation.placements.size() ||
            picker->selectedIds() != std::vector<Id>{cloneId} ||
            editor->pickerSelection().ids != std::vector<Id>{cloneId})
            return fail("Automation Picker clone identity/link/unused source selection mismatch");
        const auto payload = picker->dragDescription();
        if (!editor->placePickerSource(payload, editor->playlistPosition(12, 3)) ||
            controller.project.automation.placements.back().source != cloneId)
            return fail("Selected cloned Automation source cannot be placed normally");
        controller.undoEdit();
        if (!cloned.isEquivalentTo(controller.project.state()))
            return fail("Cloned Automation placement Undo mismatch");
        auto png =
            folder.getChildFile(preserveLinks ? "automation-clone-with-links.png" : "automation-clone.png")
                .createOutputStream();
        if (!png || !png->setPosition(0) || !png->truncate().wasOk() ||
            !juce::PNGImageFormat{}.writeImageToStream(
                editor->createComponentSnapshot(editor->getLocalBounds()), *png))
            return fail("Automation clone Picker screenshot failed");
        controller.undoEdit();
        if (!baseline.isEquivalentTo(controller.project.state()))
            return fail("Automation Picker clone one-action Undo mismatch");
        controller.redoEdit();
        if (!cloned.isEquivalentTo(controller.project.state()))
            return fail("Automation Picker clone exact Redo mismatch");
        controller.undoEdit();
        picker->selectIds({curve.id});
    }
    const auto beforeStaleClone = controller.project.state();
    const auto staleRevision = controller.revision;
    if (picker->cloneAutomationSource(controller.project.nextId, true).wasOk() ||
        staleRevision != controller.revision || !beforeStaleClone.isEquivalentTo(controller.project.state()))
        return fail("Stale Automation source clone changed document/history");
    picker->setKind(0);
    picker->setGroupFilter({});
    picker->selectIds({first, second});
    editor->setPlaylistTool(0);
    const auto location = editor->playlistPosition(8, 2).toFloat();
    auto now = juce::Time::getCurrentTime();
    juce::MouseEvent click(juce::Desktop::getInstance().getMainMouseSource(), location,
                           juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, editor,
                           editor, now, location, now, 1, false);
    editor->mouseDown(click);
    editor->mouseUp(click);
    if (controller.project.placements.size() != before + 2)
        return fail("Select-then-click did not place all selected Patterns");
    controller.undoEdit();
    if (!original.isEquivalentTo(controller.project.state()))
        return fail("Select-then-click placement Undo mismatch");
    const auto revision = controller.revision;
    const auto width = picker->getWidth();
    editor->viewAction("playlist.picker");
    if (picker->isVisible())
        return fail("Picker Panel hide failed");
    editor->viewAction("playlist.picker");
    if (!picker->isVisible() || picker->getWidth() != width || controller.revision != revision ||
        !original.isEquivalentTo(controller.project.state()))
        return fail("Picker Panel restore edited the project or lost layout");
    picker->group({first, second}, "Daily beat");
    picker->setGroupFilter("Daily beat");
    const auto file = folder.getChildFile("picker-session.vibra");
    Project loaded;
    if (saveProject(controller.project, file).failed() || loadProject(file, loaded).failed() ||
        !loaded.state().isEquivalentTo(controller.project.state()))
        return fail("Picker GUI project round trip");
    auto out = folder.getChildFile("picker-panel.png").createOutputStream();
    if (!out)
        return fail("Picker screenshot stream");
    out->setPosition(0);
    out->truncate();
    if (!juce::PNGImageFormat{}.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()),
                                                   *out))
        return fail("Picker screenshot capture");
    folder.getChildFile("picker-verification.txt")
        .replaceWithText(
            "PASS: actual ListBox selection callbacks, source/group/text filtering, typed drag/drop target, "
            "multi-source linked placement/one Undo/Redo, stale project and sidebar drop refusal, "
            "Audio/Automation "
            "source placement, select-then-click mouse handler, hide/restore, schema10 disk round trip. "
            "Audio source is an original generated tone. Component input/capture, not OS interaction "
            "or listening.\n");
    // The actual UI Save path must cancel a leased MCP preview before capturing
    // a snapshot. A saved clean baseline must remain clean and recoverable.
    controller.dirty = false;
    const auto stable = controller.project.state();
    const auto begin = mcpCall("vibera_gesture", juce::JSON::parse(R"({"action":"begin"})"));
    const auto operation = begin["structuredContent"]["operation"].toString();
    auto update =
        juce::JSON::parse(R"({"action":"update","command":"mixer.gain","args":{"mixer":0,"value":0.17}})");
    update.getDynamicObject()->setProperty("operation", operation);
    mcpCall("vibera_gesture", update);
    if (controller.project.state().isEquivalentTo(stable))
        return fail("Remote Save boundary fixture did not preview a real change");
    bool saved = false;
    auto saveResult = juce::Result::fail("Save callback did not complete");
    const auto localSave = folder.getChildFile("mcp-ui-save-boundary.vibra");
    saveTo(localSave,
           [&](juce::Result result)
           {
               saved = true;
               saveResult = result;
           });
    timerCallback(); // Drives the real asynchronous capture/save completion path.
    const auto terminal = mcpCall("vibera_gesture", juce::JSON::parse(R"({"action":"state"})"));
    Project localSaved;
    if (!saved || saveResult.failed() || loadProject(localSave, localSaved).failed() ||
        !stable.isEquivalentTo(localSaved.state()) || !stable.isEquivalentTo(controller.project.state()) ||
        controller.dirty || terminal["structuredContent"]["completionState"].toString() != "cancelled")
        return fail("Local UI Save persisted a remote preview or failed to cancel its owner");
    folder.getChildFile("mcp-ui-save-boundary.txt")
        .replaceWithText(
            "PASS: actual shared UI Save cancels owned remote preview before plugin-state capture; "
            "real saved file and clean project match the baseline, remote operation is cancelled. "
            "Component/control path; not OS keyboard input or listening.\n");
    restore();
    return juce::Result::ok();
}
} // namespace vibera
