// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
#include "ClipGroups.h"
namespace vibera
{
juce::Result Studio::verifyClipGroups(const juce::File &output)
{
    if (auto r = output.createDirectory(); r.failed())
        return r;
    auto fail = [&](const juce::String &message)
    {
        output.getChildFile("failure.txt").replaceWithText(message);
        return juce::Result::fail(message);
    };
    auto project = Project::empty();
    project.name = "Grouped arrangement";
    const auto pattern = project.patterns.front().id, channel = project.channels.front().id;
    project.patterns.front().notes = {{project.allocate(), channel, 0, .5, 60, .8f, true},
                                      {project.allocate(), channel, 1, .5, 60, .7f, true}};
    project.placements = {{project.allocate(), pattern, 1, 1, 2, 0},
                          {project.allocate(), pattern, 2, 3, 2, 0},
                          {project.allocate(), pattern, 4, 8, 2, 0}};
    controller.replace(project);
    selectedPattern = pattern;
    selectedChannel = channel;
    const auto first = project.placements[0].id, second = project.placements[1].id;
    selectedClips = {first, second};
    focus = Scope::playlist;
    showEditor(2);
    maximiseEditor(2);
    refresh();
    auto *editor = static_cast<Editor *>(windows[2]->getContentComponent());
    auto *button = dynamic_cast<juce::Button *>(editor->findChildWithID("playlist.grouping"));
    if (!button || !button->isVisible() || button->getWidth() < 24 || !button->getToggleState())
        return fail("Actual grouping toolbar button unavailable");
    const auto original = controller.project.state();
    if (!keyPressed(juce::KeyPress('G', juce::ModifierKeys::shiftModifier, 0)) ||
        controller.project.clipGroups.size() != 1)
        return fail("Actual contextual Shift+G handler failed");
    const auto grouped = controller.project.state();
    selectItems(true, {second});
    if (effectiveClipSelection().size() != 2)
        return fail("Member selection did not expand");
    const auto revision = controller.revision;
    button->onClick();
    if (controller.clipGroupingEnabled || effectiveClipSelection() != std::vector<Id>{second} ||
        controller.revision != revision)
        return fail("Actual toolbar bypass changed membership/history or retained whole-group selection");
    button->onClick();
    if (!controller.clipGroupingEnabled || effectiveClipSelection().size() != 2)
        return fail("Actual toolbar re-enable lost membership");
    // Actual Editor handlers, distinct from OS synthetic input.
    editor->setPlaylistTool(0);
    const auto bounds = editor->playlistClipBounds(first);
    const auto start = juce::Point<float>{bounds.getX() + bounds.getWidth() * .5f, bounds.getY() + 8};
    auto event = [&](juce::Point<float> point)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0,
                                editor, editor, now, start, now, 1, true);
    };
    editor->mouseDown(event(start));
    editor->mouseDrag(event(start + juce::Point<float>{32, 0}));
    editor->mouseUp(event(start + juce::Point<float>{32, 0}));
    const auto &moved = controller.project.placements;
    if (moved[0].beat == 1 || moved[1].beat - moved[0].beat != 2 || moved[2].beat != 8 ||
        controller.hasGesture())
        return fail("Actual member drag lost group geometry/nonmember/gesture completion");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(grouped))
        return fail("Actual grouped drag was not one exact Undo");
    selectItems(true, {first});
    focus = Scope::playlist;
    if (!keyPressed(juce::KeyPress('G', juce::ModifierKeys::altModifier, 0)) ||
        !controller.project.clipGroups.empty())
        return fail("Actual contextual Alt+G failed");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(grouped))
        return fail("Ungroup Undo lost stable identity");
    controller.undoEdit();
    if (!controller.project.state().isEquivalentTo(original))
        return fail("Grouping Undo lost original arrangement");
    controller.redoEdit();
    selectedClips = {first};
    refresh();
    const auto layout = output.getChildFile("group-layout.json");
    if (saveWorkspaceLayout(layout).failed())
        return fail("Grouping workspace persistence failed");
    auto malformed = juce::JSON::parse(layout);
    malformed.getDynamicObject()->setProperty("clipGroupingEnabled", "true");
    const auto corrupt = output.getChildFile("invalid-layout.json");
    corrupt.replaceWithText(juce::JSON::toString(malformed));
    const auto windowBounds = windows[2]->getBounds();
    if (loadWorkspaceLayout(corrupt).wasOk() || windowBounds != windows[2]->getBounds() ||
        !controller.clipGroupingEnabled)
        return fail("Malformed grouping preference partially applied layout");
    if (saveProject(controller.project, output.getChildFile("grouped-showcase.vibra")).failed())
        return fail("Cannot save populated grouping showcase");
    for (const auto size : {juce::Point<int>{1920, 1044}, juce::Point<int>{2560, 1404}})
    {
        setSize(size.x, size.y);
        maximiseEditor(2);
        refresh();
        const float scale = size.x == 2560 ? 1.5f : 1.f;
        const auto image = createComponentSnapshot(getLocalBounds(), true, scale);
        auto stream = output.getChildFile(size.x == 1920 ? "grouped-1080p.png" : "grouped-4k-150.png")
                          .createOutputStream();
        if (!stream)
            return fail("Cannot capture grouped workspace");
        stream->setPosition(0);
        stream->truncate();
        if (!juce::PNGImageFormat().writeImageToStream(image, *stream))
            return fail("Cannot encode grouped workspace");
    }
    output.getChildFile("clip-groups-gui.txt")
        .replaceWithText("PASS actual JUCE Shift+G/Alt+G, toolbar bypass, member selection/drag, exact "
                         "Undo/Redo, atomic workspace preference and populated logical1080p/4K150 component "
                         "renders. OS input, hardware and listening separate.\n");
    return juce::Result::ok();
}
} // namespace vibera
