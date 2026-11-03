// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "Customization.h"
#include "MixerInsertOrder.h"
#include "MixerNativeIdentity.h"
#include <limits>
#include <stdexcept>
namespace vibera::test::mixer_insert_order
{
inline void check(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
inline std::vector<int> ids(const Project &p)
{
    std::vector<int> result;
    for (const auto &insert : p.mixer)
        result.push_back(insert.index);
    return result;
}
inline juce::ValueTree identityContents(Project p)
{
    std::sort(p.mixer.begin(), p.mixer.end(), [](const auto &a, const auto &b) { return a.index < b.index; });
    return p.state(); // Independent test comparison, not the production move planner.
}
__declspec(noinline) inline void verify(const juce::File &folder)
{
    check(folder.createDirectory().wasOk(), "Owned original order acceptance output");
    Controller c;
    const auto stableChannel = c.project.channels.front().id;
    c.project.channels.front().mixer = 1;
    c.project.mixer[1].name = "Original source";
    c.project.mixer[1].pan = -.25f;
    c.project.mixer[1].routes = {{5, .5f}};
    c.project.mixer[3].routes = {{5, .25f}};
    juce::PluginDescription description;
    description.name = "Original missing insert-order state fixture";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = folder.getChildFile("deliberately-absent.vst3").getFullPathName();
    description.uniqueId = 0x56314d58;
    description.isInstrument = false;
    check(!juce::File(description.fileOrIdentifier).exists(), "Genuine missing descriptor, no hosting mock");
    PluginSpec effect;
    effect.id = c.project.allocate();
    effect.name = description.name;
    effect.descriptor = description.createXml()->toString();
    effect.state = juce::MemoryBlock("original-state", 14).toBase64Encoding();
    effect.wet = .375f;
    effect.bypass = true;
    c.project.mixer[1].effects = {effect};
    c.project.mixer[1].effectsBypassed = true;
    c.project.automation.initialValues = {{{AutomationParameter::mixerGain, 1, 0}, .625f}};
    check(c.project.validate().wasOk(), "Original complete linked model fixture valid");
    const auto original = c.project;
    const auto originalIds = ids(original);
    const auto contents = identityContents(original);
    CommandArgs a;
    a.mixer = 1;
    a.mixerPosition = 4;
    check(c.execute("mixer.move", a).wasOk(), "Canonical absolute stable-ID reorder");
    auto expected = originalIds;
    expected.erase(expected.begin() + 1);
    expected.insert(expected.begin() + 4, 1);
    check(ids(c.project) == expected && identityContents(c.project).isEquivalentTo(contents) &&
              c.project.channel(stableChannel)->mixer == 1 && *c.project.plugin(effect.id) == effect,
          "Full insert values, Channel links, routes, opaque state, bypass/wet and automation stay bound to "
          "IDs");
    check(c.undo.getUndoDescriptions().size() == 1 && c.undoEditChecked().wasOk() &&
              c.project.state().isEquivalentTo(original.state()) && !c.undo.canUndo() &&
              c.redoEditChecked().wasOk() && ids(c.project) == expected,
          "One exact actual Undo/Redo");
    check(c.undoEditChecked().wasOk(), "Restore original before invalid/no-op checks");
    const auto revision = c.revision;
    const auto dirty = c.dirty;
    const auto undo = c.undo.getUndoDescriptions().joinIntoString("|"),
               redo = c.undo.getRedoDescriptions().joinIntoString("|");
    auto unchanged = [&]
    {
        return c.project.state().isEquivalentTo(original.state()) && c.revision == revision &&
               c.dirty == dirty && c.undo.getUndoDescriptions().joinIntoString("|") == undo &&
               c.undo.getRedoDescriptions().joinIntoString("|") == redo;
    };
    a.mixerPosition = 1;
    check(c.execute("mixer.move", a).wasOk() && unchanged(),
          "Same-row no-op preserves dirty/revision/Redo branch");
    a.mixerPosition.reset();
    check(c.execute("mixer.moveLeft", a).wasOk() && unchanged(), "First non-Master relative boundary no-op");
    for (const int bad : {-1, 0, 18, 129, std::numeric_limits<int>::max()})
    {
        a.mixerPosition = bad;
        check(c.execute("mixer.move", a).failed() && unchanged(),
              "Invalid/Master absolute destination atomic, never clamped");
    }
    a.mixerPosition.reset();
    check(c.execute("mixer.move", a).failed() && unchanged(),
          "Absolute row is explicit, not a magic default");
    a.mixerPosition = 2;
    check(c.execute("mixer.moveRight", a).failed() && unchanged(),
          "Relative alias refuses absolute row field");
    a.mixer = 0;
    check(c.execute("mixer.move", a).failed() && unchanged(), "Master source protected");
    a.mixer = 999;
    check(c.execute("mixer.move", a).failed() && unchanged(), "Missing stable source refuses unchanged");
    auto interior = original;
    std::swap(interior.mixer[0], interior.mixer[2]);
    c.replace(interior);
    a.mixer = 1;
    a.mixerPosition = 4;
    check(c.execute("mixer.move", a).wasOk() && ids(c.project)[2] == 0 && ids(c.project)[0] == 2 &&
              ids(c.project)[1] == 3 && ids(c.project)[3] == 4 && ids(c.project)[4] == 1 &&
              identityContents(c.project).isEquivalentTo(contents),
          "Crossing nonleading Master shifts only other rows");
    a.mixerPosition.reset();
    check(c.execute("mixer.moveLeft", a).wasOk() && mixerInsertPosition(c.project, 1) == 3 &&
              c.execute("mixer.moveLeft", a).wasOk() && mixerInsertPosition(c.project, 1) == 1 &&
              mixerInsertPosition(c.project, 0) == 2,
          "Relative move skips immutable interior Master slot");
    c.replace(original);
    const auto intent = MixerInsertOrderIntent::capture(c.project, c.projectIdentity, c.revision, 1);
    check(intent.validate(c.project, c.projectIdentity, c.revision).wasOk() &&
              intent.validate(c.project, "replacement-same-IDs", c.revision).failed() &&
              intent.validate(c.project, c.projectIdentity, c.revision + 1).failed(),
          "Queued menu intent owns project/revision/order");
    check(c.beginGesture("original-order-owner"), "Actual owned reorder gesture");
    a.mixer = 1;
    a.mixerPosition = 4;
    check(c.execute("mixer.move", a, "foreign-owner").failed() &&
              c.project.state().isEquivalentTo(original.state()) &&
              c.execute("mixer.move", a, "original-order-owner").wasOk() &&
              c.cancelGestureChecked("original-order-owner").wasOk() &&
              c.project.state().isEquivalentTo(original.state()) && !c.undo.canUndo() && !c.dirty,
          "Owned preview/cancel restores exact baseline; foreign owner refuses");
    Customization custom = Customization::familiarDefaults();
    check(custom.resolve(juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::altModifier, 0),
                         Scope::mixer) == "mixer.moveLeft" &&
              custom.resolve(juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::altModifier, 0),
                             Scope::mixer) == "mixer.moveRight",
          "Familiar contextual defaults use canonical customizable command IDs");
    Macro macro;
    macro.id = "macro.original-mixer-order";
    macro.name = "Original insert order";
    macro.steps = {{"mixer.move", a, false}, {"mixer.moveLeft", CommandArgs{}, true}};
    check(custom.addMacro(macro).wasOk(), "Typed absolute and contextual relative macro accepted");
    Customization restored;
    check(restored.importState(custom.state()).wasOk() && restored.macros.size() == 1 &&
              restored.macros.front().steps.front().parameters.mixerPosition == 4 &&
              !restored.macros.front().steps.back().parameters.mixerPosition,
          "Exact optional row profile serialization");
    MacroContext context;
    context.insert = 1;
    check(restored.runMacro(macro.id, c, context).wasOk() && mixerInsertPosition(c.project, 1) == 3 &&
              c.undo.getUndoDescriptions().size() == 1 && c.undoEditChecked().wasOk() &&
              c.project.state().isEquivalentTo(original.state()),
          "Actual grouped macro resolves stable context and has one Undo");
    const auto beforeCancel = c.project.state();
    const auto cancelDirty = c.dirty;
    int polls = 0;
    check(restored.runMacro(macro.id, c, context, [&] { return ++polls > 1; }).failed() &&
              beforeCancel.isEquivalentTo(c.project.state()) && c.dirty == cancelDirty && !c.hasGesture(),
          "Actual cancelled macro restores earlier moves and dirty baseline");
    auto badProfile = custom.state();
    (*badProfile["macros"].getArray())[0]["steps"]
        .getArray()
        ->getReference(0)["parameters"]
        .getDynamicObject()
        ->setProperty("mixerPosition", 1.5);
    check(restored.importState(badProfile).failed(),
          "Fractional typed macro position cannot truncate silently");
    const auto saved = folder.getChildFile("original-insert-order.vibra");
    check(!saved.exists() && c.execute("mixer.move", a).wasOk() && saveProject(c.project, saved).wasOk(),
          "Save canonical ordered fixture without overwrite");
    Project disk;
    check(loadProject(saved, disk).wasOk() && disk.state().isEquivalentTo(c.project.state()),
          "Current genuine project round trip retains exact order and identity state");
    check(folder.getChildFile("model.txt")
              .replaceWithText("PASS original model/history/profile/macro/persistence checks. No audio, "
                               "provider, native input or listening qualification.\n"),
          "Retain actual model result");
}
} // namespace vibera::test::mixer_insert_order
