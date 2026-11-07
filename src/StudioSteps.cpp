// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Studio.h"
#include "Editors.h"
namespace vibera
{
void Studio::addStepFillItems(juce::PopupMenu &menu, const std::vector<Id> &targets, Id pattern)
{
    const auto identity = controller.projectIdentity;
    const bool enabled = controller.project.pattern(pattern) && !targets.empty() &&
                         std::all_of(targets.begin(), targets.end(),
                                     [&](Id id)
                                     {
                                         const auto *channel = controller.project.channel(id);
                                         return channel && !channel->audioClip;
                                     });
    for (int interval : {2, 4, 8})
    {
        juce::PopupMenu::Item item("Fill each " + juce::String(interval) + " steps");
        item.itemID = 100 + interval;
        item.isEnabled = enabled;
        item.action =
            [safe = juce::Component::SafePointer<Studio>(this), identity, pattern, targets, interval]
        {
            if (!safe || safe->controller.projectIdentity != identity)
                return;
            CommandArgs args;
            args.pattern = pattern;
            args.ids = targets;
            safe->command("steps.fill" + juce::String(interval), args);
        };
        menu.addItem(std::move(item));
    }
    juce::PopupMenu::Item advanced("Advanced Fill...");
    advanced.itemID = 109;
    advanced.isEnabled = enabled;
    advanced.action = [safe = juce::Component::SafePointer<Studio>(this), identity, pattern, targets]
    {
        if (safe && safe->controller.projectIdentity == identity)
            safe->openStepComposer(pattern, targets);
    };
    menu.addItem(std::move(advanced));
}
juce::Result Studio::Editor::verifyStepFill(const juce::File &folder)
{
    auto fail = [](const juce::String &message) { return juce::Result::fail("Step fill: " + message); };
    const auto pattern = s.selectedPattern;
    const auto kick = s.controller.project.channels[0].id, hat = s.controller.project.channels[2].id;
    const auto active = s.selectedChannel;
    const auto before = s.controller.project.state();
    auto action = [&](const std::vector<Id> &targets, int interval)
    {
        juce::PopupMenu menu;
        s.addStepFillItems(menu, targets, pattern);
        for (juce::PopupMenu::MenuItemIterator it(menu); it.next();)
            if (it.getItem().itemID == 100 + interval && it.getItem().isEnabled)
                return it.getItem().action;
        return std::function<void()>{};
    };
    auto fill2 = action({hat}, 2);
    if (!fill2)
        return fail("missing named Channel fill menu action");
    fill2();
    auto *part = s.controller.project.pattern(pattern);
    if (part->notes.size() != 50 || s.selectedChannel != active || !part->notes.back().step ||
        part->notes.back().beat != 23.5)
        return fail("menu fill did not reach last hit beyond64 steps or changed MIDI recipient");
    const auto filled = s.controller.project.state();
    s.controller.undoEdit();
    if (!s.controller.project.state().isEquivalentTo(before))
        return fail("menu exact one Undo");
    s.controller.redoEdit();
    if (!s.controller.project.state().isEquivalentTo(filled))
        return fail("menu exact Redo");
    s.selectRackChannels({kick, hat});
    auto fill4 = action(s.selectedChannels, 4);
    if (!fill4)
        return fail("bulk fill menu action");
    fill4();
    if (s.controller.project.pattern(pattern)->notes.size() != 50 || s.selectedChannel != active)
        return fail("bulk selection fill");
    const auto bulk = s.controller.project.state();
    setRackViewport(0, 64);
    if (int(rackViewport()["firstStep"]) != 64 || int(rackViewport()["totalSteps"]) != 96)
        return fail("full Pattern horizontal viewport");
    const juce::Point<float> point{float(stepStart() + stepWidth() * 12.5), 60.f};
    auto event = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), point,
                                  juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.f, 0.f, 0.f,
                                  0.f, 0.f, this, this, juce::Time::getCurrentTime(), point,
                                  juce::Time::getCurrentTime(), 1, false);
    mouseDown(event);
    mouseUp(event);
    const auto &notes = s.controller.project.pattern(pattern)->notes;
    if (std::any_of(notes.begin(), notes.end(),
                    [&](const auto &n) { return n.channel == kick && n.beat == 19; }) ||
        std::none_of(notes.begin(), notes.end(),
                     [&](const auto &n) { return n.channel == hat && n.beat == 19; }))
        return fail("filtered scrolled step off hit wrong cell/Channel");
    s.controller.undoEdit();
    if (!s.controller.project.state().isEquivalentTo(bulk))
        return fail("scrolled step exact Undo");
    s.selectRackChannels({kick, hat});
    s.focus = Scope::rack;
    const auto key =
        juce::KeyPress('8', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier, 0);
    if (s.customization.bind("steps.fill8", Scope::rack, key).failed() || !s.keyPressed(key) ||
        s.controller.project.pattern(pattern)->notes.size() != 26)
        return fail("custom contextual fill shortcut");
    s.controller.undoEdit();
    if (!s.controller.project.state().isEquivalentTo(bulk))
        return fail("shortcut Undo");
    s.selectRackChannels({kick});
    s.focus = Scope::rack;
    const auto rotateKey = juce::KeyPress(juce::KeyPress::rightKey,
                                          juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0);
    const auto kickBeats = [&]
    {
        std::vector<double> beats;
        for (const auto &n : s.controller.project.pattern(pattern)->notes)
            if (n.channel == kick && n.step)
                beats.push_back(n.beat);
        std::sort(beats.begin(), beats.end());
        return beats;
    };
    const auto beforeRotate = kickBeats();
    const bool rotated = s.keyPressed(rotateKey);
    if (!rotated || kickBeats() == beforeRotate)
        return fail("Ctrl+Shift+Right rotates the selected Channel's steps: " + s.status);
    s.controller.undoEdit();
    if (!s.controller.project.state().isEquivalentTo(bulk))
        return fail("rotate shortcut Undo");
    setRackViewport(0, 0);
    s.selectedChannel = active;
    s.refresh();
    s.showEditor(1);
    static_cast<Editor *>(s.windows[1]->getContentComponent())->viewAction("piano.fit");
    s.focus = Scope::rack;
    s.status = "Fill each 2 / 4 / 8 steps | Channel name right-click or Rack Menu > Fill selected Channels";
    juce::PNGImageFormat png;
    auto snapshot = [&](const char *name, float scale)
    {
        auto stream = folder.getChildFile(name).createOutputStream();
        if (!stream)
            return false;
        stream->setPosition(0);
        stream->truncate();
        return png.writeImageToStream(s.createComponentSnapshot(s.getLocalBounds(), true, scale), *stream);
    };
    if (!snapshot("step-fill-workspace.png", 1.f))
        return fail("workspace PNG");
    s.setSize(2560, 1392);
    s.resized();
    if (!snapshot("step-fill-4k.png", 1.5f))
        return fail("4K PNG");
    s.setSize(1920, 1040);
    s.resized();
    const auto replacement = Project::empty();
    s.controller.replace(replacement);
    fill2();
    if (!s.controller.project.state().isEquivalentTo(replacement.state()))
        return fail("stale menu changed new project");
    folder.getChildFile("step-fill-gui.txt")
        .replaceWithText("PASS actual shared Channel/bulk menu callbacks; all96 cells; filtered/scrolled "
                         "mouse step off and Undo despite dormant outside-loop melody; "
                         "custom contextual shortcut/Undo; stale menu identity guard; actual "
                         "populated1080p/4K component PNG. "
                         "No OS musical input, physical MIDI or listening. Held take fill/macro cases run in "
                         "Rack acceptance.\n");
    return juce::Result::ok();
}
juce::Result Studio::verifyStepFill(const juce::File &folder)
{
    stopTimer();
    audio.stop();
    releaseTypingNotes();
    customization = Customization::familiarDefaults();
    auto project = Project::empty();
    project.patterns[0].length = 24;
    project.patterns[0].autoLength = false; // Fixed96-step fixture with a dormant note at beat25.
    selectedPattern = project.patterns[0].id;
    selectedChannel = project.channels[3].id;
    project.patterns[0].notes = {{project.allocate(), selectedChannel, 2, 1, 67, .7f, false},
                                 {project.allocate(), project.channels[0].id, 25, 1, 50, .6f, false}};
    const auto group = project.allocate();
    project.channelGroups.push_back({group, "Drums"});
    project.channels[0].group = project.channels[2].group = group;
    project.placements = {{project.allocate(), selectedPattern, 0, 0, 24, 0},
                          {project.allocate(), selectedPattern, 0, 24, 24, 0}};
    controller.replace(project);
    refresh();
    setRackFilter("group", group);
    setSize(1920, 1040);
    resized();
    return static_cast<Editor *>(windows[0]->getContentComponent())->verifyStepFill(folder);
}
} // namespace vibera
