// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
namespace vibera
{
juce::Result Studio::verifyNoteMute(const juce::File &output)
{
    if (auto r = output.createDirectory(); r.failed())
        return r;
    auto fail = [&](const juce::String &message)
    {
        output.getChildFile("failure.txt").replaceWithText(message);
        return juce::Result::fail(message);
    };
    auto project = Project::empty();
    project.name = "Note mute - keep the melody";
    const auto pattern = project.patterns.front().id, channel = project.channels.front().id;
    project.channels.front().source = "builtin:bass";
    auto &notes = project.patterns.front().notes;
    for (int i = 0; i < 8; ++i)
        notes.push_back({project.allocate(), channel, i * .5, .375, 60, .5f + .05f * i, false});
    project.placements = {{project.allocate(), pattern, 0, 0, 4, 0},
                          {project.allocate(), pattern, 1, 4, 4, 0}};
    controller.replace(project);
    selectedPattern = pattern;
    selectedChannel = channel;
    focus = Scope::piano;
    showEditor(1);
    maximiseEditor(1);
    refresh();
    auto *editor = static_cast<Editor *>(windows[1]->getContentComponent());
    editor->viewAction("piano.fit");
    auto *button = dynamic_cast<juce::Button *>(editor->findChildWithID("piano.toolMute"));
    if (!button || !button->isVisible() || button->getWidth() < 24)
        return fail("Mute toolbar missing");
    if (!keyPressed(juce::KeyPress('T', {}, 0)) || !editor->noteMuteToolActive())
        return fail("Contextual T failed");
    auto point = [&](int index)
    { return editor->noteBounds(controller.project.pattern(pattern)->notes[size_t(index)]).getCentre(); };
    const auto start = point(1), end = point(5);
    auto event = [&](juce::Point<float> where, int modifiers = juce::ModifierKeys::leftButtonModifier)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), where,
                                juce::ModifierKeys(modifiers), 1, 0, 0, 0, 0, editor, editor, now, start, now,
                                1, true);
    };
    const auto original = controller.project.state();
    editor->mouseDown(event(start));
    editor->mouseDrag(event(end));
    editor->mouseDrag(event(start));
    editor->mouseUp(event(start));
    const auto muted = controller.project.state();
    const auto &changed = controller.project.pattern(pattern)->notes;
    for (size_t i = 0; i < changed.size(); ++i)
        if (changed[i].muted != (i >= 1 && i <= 5))
            return fail("Swept T tool lost intermediate notes or toggled revisits");
    if (controller.hasGesture())
        return fail("Mute stroke not ended");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Mute stroke not one exact Undo");
    controller.redoEdit();
    if (!controller.project.state().isEquivalentTo(muted))
        return fail("Mute Redo lost IDs/state");
    editor->mouseDown(event(start));
    editor->mouseDrag(event(end));
    editor->mouseUp(event(end));
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Muted-first stroke failed unmute");
    editor->mouseDown(event(start));
    editor->mouseDrag(event(end));
    editor->cancelPointerGesture();
    if (!controller.project.state().isEquivalentTo(original) || controller.hasGesture())
        return fail("Cancelled stroke not exact");
    editor->mouseDown(
        event(start, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier));
    editor->mouseDrag(event(start + juce::Point<float>{3, 3},
                            juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::ctrlModifier));
    editor->mouseUp(event(start + juce::Point<float>{3, 3}, juce::ModifierKeys::ctrlModifier));
    if (!controller.project.state().isEquivalentTo(original) || selectedNotes.empty())
        return fail("Ctrl selection unexpectedly muted");
    selectedNotes = {project.patterns.front().notes[2].id};
    if (!performAction("notes.mute") || !controller.project.pattern(pattern)->notes[2].muted)
        return fail("Shared selection command failed");
    controller.undoEdit();
    editor->mouseDown(event(point(7), juce::ModifierKeys::rightButtonModifier));
    editor->mouseUp(event(point(7), juce::ModifierKeys::rightButtonModifier));
    if (controller.project.pattern(pattern)->notes.size() != 7)
        return fail("Mute tool right-click no longer erases");
    controller.undoEdit();
    keyPressed(juce::KeyPress('P', {}, 0));
    if (editor->noteMuteToolActive())
        return fail("Draw shortcut failed to leave mute tool");
    performAction("notes.mute");
    selectedNotes.clear();
    if (saveProject(controller.project, output.getChildFile("note-mute-showcase.vibra")).failed())
        return fail("Showcase save failed");
    for (auto size : {juce::Point<int>{1920, 1044}, juce::Point<int>{2560, 1404}})
    {
        setSize(size.x, size.y);
        maximiseEditor(1);
        editor->viewAction("piano.fit");
        refresh();
        auto image = createComponentSnapshot(getLocalBounds(), true, size.x == 2560 ? 1.5f : 1.f);
        auto stream = output.getChildFile(size.x == 1920 ? "note-mute-1080p.png" : "note-mute-4k-150.png")
                          .createOutputStream();
        if (!stream)
            return fail("Screenshot stream failed");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(image, *stream))
            return fail("Screenshot encode failed");
    }
    output.getChildFile("note-mute-gui.txt")
        .replaceWithText("PASS actual JUCE T/P, toolbar, swept/revisited mute/unmute, Ctrl selection, right "
                         "erase, one Undo/Redo and cancellation; populated logical1080p/4K150 renders. "
                         "Native OS, listening and hardware separate.\n");
    return juce::Result::ok();
}
} // namespace vibera
