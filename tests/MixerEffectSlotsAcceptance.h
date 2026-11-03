// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/MixerEffectSlots.h"
#include "../src/MixerEffectOrder.h"
#include "../src/Commands.h"
#include <stdexcept>
namespace vibera::acceptance::effectSlots
{
inline void geometry()
{
    auto check = [](bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    };
    using Action = MixerEffectSlots::Action;
    // Logical1080p and4K150% layouts, not fixed bitmap scaling.
    for (const auto size : {juce::Point<int>{1920, 1044}, {2560, 1392}, {760, 210}})
    {
        const MixerEffectSlots layout(size.x, size.y, size.x - 240, 12);
        check(layout.pane.getRight() == size.x - 12, "Reserved scrollbar gutter");
        check(layout.pane.getBottom() == size.y - 12, "Reserved horizontal gutter");
        check(layout.hit({size.x - 1, 80}, 0, 16).action == Action::none, "Scrollbar is not a slot target");
        check(layout.hit(layout.add.getCentre(), 0, 16).action == Action::add, "Header Add is independent");
        check(layout.chainBypass.getWidth() >= 26 && !layout.chainBypass.intersects(layout.add) &&
                  layout.hit(layout.chainBypass.getCentre(), 0, 16).action == Action::chainBypass,
              "Practical independent chain header target");
        const int offset = layout.clampedOffset(3, 16);
        for (int visible = 0; visible < layout.visibleRows(); ++visible)
        {
            if (offset + visible >= 16)
                break;
            const auto row = layout.row(visible);
            check(layout.rows.contains(row), "Fully visible rows only");
            check(!layout.bypass(visible).intersects(layout.name(visible)) &&
                      !layout.menu(visible).intersects(layout.name(visible)),
                  "Separate native-name/bypass/context targets");
            check(!layout.grip(visible).intersects(layout.name(visible)) &&
                      !layout.grip(visible).intersects(layout.menu(visible)) &&
                      !layout.grip(visible).intersects(layout.bypass(visible)),
                  "Separate24px reorder grip");
            check(row.contains(layout.wet(visible)) && layout.wet(visible).getWidth() >= 28 &&
                      !layout.wet(visible).intersects(layout.name(visible)) &&
                      !layout.wet(visible).intersects(layout.bypass(visible)) &&
                      !layout.wet(visible).intersects(layout.grip(visible)) &&
                      !layout.wet(visible).intersects(layout.menu(visible)) &&
                      !layout.wet(visible).intersects(layout.wetLabel(visible)) &&
                      layout.wetLabel(visible).getHeight() >= 24,
                  "Two-line wet target/label remain readable and disjoint");
            check(layout.hit(layout.wet(visible).getCentre(), offset, 16).action == Action::wet,
                  "Wet hit never opens native editor, toggles power or reorders");
            check(layout.bypass(visible).getWidth() >= 24 && layout.menu(visible).getHeight() >= 24,
                  "Practical logical click targets");
            const auto name = layout.hit(layout.name(visible).getCentre(), offset, 16);
            check(name.action == Action::open && name.slot == offset + visible,
                  "Scrolled row resolves actual model index");
            check(layout.hit(layout.bypass(visible).getCentre(), offset, 16).action == Action::bypass,
                  "Power dispatch");
            check(layout.hit(layout.menu(visible).getCentre(), offset, 16).action == Action::menu,
                  "Context dispatch");
            check(layout.hit(layout.grip(visible).getCentre(), offset, 16).action == Action::reorder,
                  "Grip dispatch never opens editor or bypasses");
            check(layout.hit({row.getX(), row.getBottom()}, offset, 16).action == Action::none,
                  "Row spacing has no accidental editor target");
        }
        check(layout.hit(layout.row(0).getCentre(), 0, 0).action == Action::add,
              "Empty insert has functional Add row");
        check(layout.clampedOffset(999, 16) == std::max(0, 17 - layout.visibleRows()),
              "Bounds account for separate final Add row");
        check(layout.clampedOffset(-1, 16) == 0, "Wheel clamp at first slot");
    }
    check(MixerEffectSlots(699, 500, 699, 12).pane.isEmpty(), "Compact strip-only mode preserved");
    check(MixerEffectSlots(800, 80, 560, 12).hit({570, 70}, 0, 3).action == Action::none,
          "No hidden tiny-height slot target");
}
inline void canonicalOrder(const juce::File &folder)
{
    auto check = [](bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    };
    Controller controller;
    auto project = Project::empty();
    juce::PluginDescription type;
    type.name = "Original missing order A";
    type.pluginFormatName = "VST3";
    type.fileOrIdentifier = folder.getChildFile("absent-order.vst3").getFullPathName();
    type.numInputChannels = type.numOutputChannels = 2;
    const Id a = project.allocate(), b = project.allocate(), instrument = project.allocate();
    project.mixer[1].effects.push_back({a, type.name, type.createXml()->toString(), "1...", false, false});
    type.name = "Original missing order B";
    project.mixer[1].effects.push_back({b, type.name, type.createXml()->toString(), "1.A.", false, true});
    project.mixer[1].effects[0].automationBindings.push_back({17, 1, 0, .25f, "Original A", "unit"});
    project.mixer[1].effects[1].automationBindings.push_back({18, 1, 0, .75f, "Original B", "unit"});
    type.name = "Original missing instrument";
    type.isInstrument = true;
    project.channels[0].instrument =
        PluginSpec{instrument, type.name, type.createXml()->toString(), "1...", true, false};
    check(project.validate().wasOk(), "Original two-state order fixture is valid");
    controller.replace(project);
    const auto initial = controller.project.state();
    auto intent =
        MixerEffectOrderIntent::capture(project, controller.projectIdentity, controller.revision, 1, a);
    check(bool(intent) &&
              intent->validate(project, controller.projectIdentity, controller.revision, 1).wasOk(),
          "Exact independent reorder owner");
    CommandArgs args;
    args.target = a;
    args.lane = 1;
    check(controller.execute("plugin.effect.move", args).wasOk(), "Move existing complete effect");
    auto expected = project;
    std::swap(expected.mixer[1].effects[0], expected.mixer[1].effects[1]);
    check(controller.project.state().isEquivalentTo(expected.state()),
          "Independent swap preserves both unequal states/bindings/bypass/IDs");
    const auto changed = controller.project.state();
    const auto revision = controller.revision;
    const auto dirty = controller.dirty;
    const auto undo = controller.undo.getUndoDescription(), redo = controller.undo.getRedoDescription();
    check(controller.execute("plugin.effect.move", args).wasOk() && controller.revision == revision &&
              controller.dirty == dirty && controller.undo.getUndoDescription() == undo &&
              controller.undo.getRedoDescription() == redo &&
              controller.project.state().isEquivalentTo(changed),
          "Same-slot exact no-op retains history/revision/dirty");
    for (const int destination : {-1, 2, 16})
    {
        args.lane = destination;
        check(controller.execute("plugin.effect.move", args).failed() && controller.revision == revision &&
                  controller.dirty == dirty && controller.undo.getUndoDescription() == undo &&
                  controller.undo.getRedoDescription() == redo &&
                  controller.project.state().isEquivalentTo(changed),
              "Invalid destination refuses without clamping/mutation");
    }
    for (const Id target : {instrument, project.channels.front().id, Id(999999)})
    {
        args.target = target;
        args.lane = 0;
        check(controller.execute("plugin.effect.move", args).failed() && controller.revision == revision &&
                  controller.dirty == dirty && controller.undo.getUndoDescription() == undo &&
                  controller.undo.getRedoDescription() == redo &&
                  controller.project.state().isEquivalentTo(changed),
              "Instrument/Channel/unknown target is not an effect no-op");
    }
    check(intent->validate(controller.project, controller.projectIdentity, controller.revision, 1).failed(),
          "Changed revision/order refuses retained intent");
    check(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(initial),
          "One exact order Undo");
    check(controller.redoEditChecked().wasOk() && controller.project.state().isEquivalentTo(changed),
          "One exact order Redo");
    check(controller.undoEditChecked().wasOk(), "Prepare actual redo tail for no-op");
    const auto undoRevision = controller.revision;
    const auto redoTail = controller.undo.getRedoDescription();
    args.target = a;
    args.lane = 0;
    check(controller.execute("plugin.effect.move", args).wasOk() && controller.revision == undoRevision &&
              controller.undo.getRedoDescription() == redoTail &&
              controller.project.state().isEquivalentTo(initial),
          "Same-slot no-op cannot discard an actual redo tail");
    check(controller.redoEditChecked().wasOk() && controller.project.state().isEquivalentTo(changed),
          "Retained tail actually Redoes");
    check(intent->validate(project, "another project", intent->revision, 1).failed() &&
              intent->validate(project, intent->project, intent->revision, 2).failed(),
          "Project and insert ownership refusal");
    auto silentlyChanged = project;
    std::swap(silentlyChanged.mixer[1].effects[0], silentlyChanged.mixer[1].effects[1]);
    check(intent->validate(silentlyChanged, intent->project, intent->revision, 1).failed(),
          "Untracked order mismatch refuses too");
}
} // namespace vibera::acceptance::effectSlots
