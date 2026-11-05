// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Customization.h"
#include "MixerInsertOrder.h"
#include "MixerGroups.h"
#include "ChannelAppearanceContracts.h"
#include "StepComposeContracts.h"
#include <algorithm>
#include <limits>
#include <unordered_set>
#include <cmath>
namespace vibera
{
static juce::Result validateMacroNoteModes(const juce::String &action, const CommandArgs &args)
{
    if (args.noteProperties.has_value() != (action == "notes.properties"))
        return juce::Result::fail("noteMode.invalidArguments: Typed Note Properties payload mismatch");
    if (args.noteProperties)
        if (auto valid = validateNotePropertiesPatch(*args.noteProperties); valid.failed())
            return valid;
    if (args.colourGroup < 0 || args.colourGroup > 15 ||
        (action != "notes.add" && (args.slide || args.portamento || args.colourGroup != 0)))
        return juce::Result::fail(
            "noteMode.invalidArguments: Typed creation fields belong only to notes.add");
    if (action == "notes.colourGroup" && (!std::isfinite(args.value) || args.value < 0 || args.value > 15 ||
                                          std::floor(args.value) != args.value))
        return juce::Result::fail("noteMode.invalidValue: Exact colour group0..15 required");
    return juce::Result::ok();
}
// Profile-only typed source payload. Runtime notes.import retains its existing
// fresh-ID/target-Channel semantics; these original IDs are never transplanted.
static juce::Result validateMacroImportedNotes(const juce::String &action, const std::vector<Note> &notes)
{
    if (action != "notes.import")
        return action == "notes.record" || !notes.empty()
                   ? juce::Result::fail(
                         "notesImport.invalidPayload: Note payload belongs only to notes.import macros")
                   : juce::Result::ok();
    if (notes.empty() || notes.size() > 100000)
        return juce::Result::fail("notesImport.invalidPayload: Import requires1..100000 source notes");
    for (const auto &note : notes)
        if (note.id < 0 || note.id > 1000000000 || note.channel < 1 || note.channel > 1000000000 ||
            !std::isfinite(note.beat) || note.beat < 0 || !std::isfinite(note.length) || note.length <= 0 ||
            note.pitch < 0 || note.pitch > 127 || !std::isfinite(note.velocity) || note.velocity < 0 ||
            note.velocity > 1 || !std::isfinite(note.pan) || std::abs(note.pan) > 1 ||
            !std::isfinite(note.fineCents) || std::abs(note.fineCents) > 100 ||
            !std::isfinite(note.shiftPercent) || note.shiftPercent < 0 || note.shiftPercent > 100 ||
            note.repeatMode < 0 || note.repeatMode > 14 || !std::isfinite(note.releaseVelocity) ||
            note.releaseVelocity < 0 || note.releaseVelocity > 1 || !std::isfinite(note.modX) ||
            note.modX < 0 || note.modX > 1 || !std::isfinite(note.modY) || note.modY < 0 || note.modY > 1 ||
            note.colourGroup < 0 || note.colourGroup > 15 || (note.slide && note.repeatMode != 0))
            return juce::Result::fail("notesImport.invalidPayload: Invalid typed source note");
    return juce::Result::ok();
}
static juce::Result parseMacroImportedNotes(const juce::var &value, std::vector<Note> &output)
{
    const auto *rows = value.getArray();
    if (!rows || rows->isEmpty() || rows->size() > 100000)
        return juce::Result::fail("notesImport.invalidPayload: Typed source-note array required");
    std::vector<Note> parsed;
    parsed.reserve(size_t(rows->size()));
    const auto number = [](const juce::var &v) { return v.isInt() || v.isInt64() || v.isDouble(); };
    const auto integer = [&](const juce::var &v)
    { return number(v) && std::isfinite(double(v)) && std::floor(double(v)) == double(v); };
    const juce::StringArray allowed{
        "id",    "channel", "beat",       "length",       "pitch",      "velocity",        "step",
        "muted", "pan",     "fineCents",  "shiftPercent", "repeatMode", "releaseVelocity", "modX",
        "modY",  "slide",   "portamento", "colourGroup"};
    for (const auto &row : *rows)
    {
        const auto *object = row.getDynamicObject();
        if (!object)
            return juce::Result::fail("notesImport.invalidPayload: Each source note must be an object");
        for (const auto &field : object->getProperties())
            if (!allowed.contains(field.name.toString()))
                return juce::Result::fail("notesImport.invalidPayload: Unknown source-note field");
        for (const auto *field : {"channel", "beat", "length", "pitch", "velocity"})
            if (!row.hasProperty(field))
                return juce::Result::fail("notesImport.invalidPayload: Missing source-note field");
        const auto id = row.getProperty("id", juce::int64(0));
        const auto step = row.getProperty("step", false), muted = row.getProperty("muted", false);
        const auto pan = row.getProperty("pan", 0.), fine = row.getProperty("fineCents", 0.);
        const auto shift = row.getProperty("shiftPercent", 0.);
        const auto repeat = row.getProperty("repeatMode", 0);
        const auto release = row.getProperty("releaseVelocity", 0.);
        const auto modX = row.getProperty("modX", .5), modY = row.getProperty("modY", .5);
        const auto slide = row.getProperty("slide", false), portamento = row.getProperty("portamento", false);
        const auto group = row.getProperty("colourGroup", 0);
        if (!integer(id) || double(id) < 0 || double(id) > 1000000000 || !integer(row["channel"]) ||
            double(row["channel"]) < 1 || double(row["channel"]) > 1000000000 || !integer(repeat) ||
            double(repeat) < 0 || double(repeat) > 14 || !integer(row["pitch"]) || double(row["pitch"]) < 0 ||
            double(row["pitch"]) > 127 || !step.isBool() || !muted.isBool() || !slide.isBool() ||
            !portamento.isBool() || !integer(group) || double(group) < 0 || double(group) > 15)
            return juce::Result::fail(
                "notesImport.invalidPayload: Exact integer identities/pitch and boolean flags required");
        for (const auto *field : {"beat", "length", "velocity"})
            if (!number(row[field]) || !std::isfinite(double(row[field])))
                return juce::Result::fail(
                    "notesImport.invalidPayload: Finite numeric source properties required");
        if (!number(pan) || !number(fine) || !std::isfinite(double(pan)) || !std::isfinite(double(fine)) ||
            !number(shift) || !std::isfinite(double(shift)) || double(shift) < 0 || double(shift) > 100 ||
            !number(release) || !std::isfinite(double(release)) || double(release) < 0 ||
            double(release) > 1 || !number(modX) || !std::isfinite(double(modX)) || double(modX) < 0 ||
            double(modX) > 1 || !number(modY) || !std::isfinite(double(modY)) || double(modY) < 0 ||
            double(modY) > 1)
            return juce::Result::fail("notesImport.invalidPayload: Finite numeric expression required");
        const double velocity = double(row["velocity"]);
        if (velocity < 0 || velocity > 1)
            return juce::Result::fail("notesImport.invalidPayload: Velocity must be normalized0..1");
        parsed.push_back({juce::int64(id), juce::int64(row["channel"]), double(row["beat"]),
                          double(row["length"]), int(row["pitch"]), float(velocity), bool(step), bool(muted),
                          double(pan), double(fine), double(shift), int(repeat), double(release),
                          double(modX), double(modY), bool(slide), bool(portamento), int(group)});
    }
    if (auto valid = validateMacroImportedNotes("notes.import", parsed); valid.failed())
        return valid;
    output = std::move(parsed);
    return juce::Result::ok();
}
static const CommandInfo *info(const juce::String &id)
{
    for (auto &c : commandRegistry())
        if (c.id == id)
            return &c;
    return nullptr;
}
static bool actionExists(const Customization &c, const juce::String &id)
{
    if (const auto *command = info(id))
        return command->userAction;
    for (auto &m : c.macros)
        if (m.id == id)
            return true;
    return false;
}
Customization Customization::familiarDefaults()
{
    Customization c;
    auto add = [&](const char *id, Scope scope, int key, int modifiers = 0)
    { c.bindings.push_back({id, scope, juce::KeyPress(key, modifiers, 0)}); };
    const int ctrl = juce::ModifierKeys::ctrlModifier, shift = juce::ModifierKeys::shiftModifier;
    add("transport.play", Scope::global, juce::KeyPress::spaceKey);
    add("transport.record", Scope::global, 'R');
    add("transport.metronome", Scope::global, 'M', ctrl);
    add("midi.typing", Scope::global, 'T', ctrl);
    add("transport.countIn", Scope::global, 'P', ctrl);
    add("transport.mode", Scope::global, 'L');
    add("project.save", Scope::global, 'S', ctrl);
    add("project.open", Scope::global, 'O', ctrl);
    add("project.export", Scope::global, 'R', ctrl);
    add("edit.undo", Scope::global, 'Z', ctrl);
    add("edit.redo", Scope::global, 'Z', ctrl | shift);
    add("edit.redo", Scope::global, 'Y', ctrl);
    add("view.playlist", Scope::global, juce::KeyPress::F5Key);
    add("view.rack", Scope::global, juce::KeyPress::F6Key);
    add("view.piano", Scope::global, juce::KeyPress::F7Key);
    add("view.mixer", Scope::global, juce::KeyPress::F9Key);
    add("mixer.rename", Scope::mixer, juce::KeyPress::F2Key);
    add("mixer.selectAll", Scope::mixer, 'A', ctrl);
    add("mixer.group.create", Scope::mixer, 'G', juce::ModifierKeys::altModifier);
    add("mixer.moveLeft", Scope::mixer, juce::KeyPress::leftKey, juce::ModifierKeys::altModifier);
    add("mixer.moveRight", Scope::mixer, juce::KeyPress::rightKey, juce::ModifierKeys::altModifier);
    add("view.plugins", Scope::global, juce::KeyPress::F8Key);
    add("view.settings", Scope::global, juce::KeyPress::F10Key);
    add("selection.cancel", Scope::global, juce::KeyPress::escapeKey);
    add("pattern.renameDialog", Scope::global, juce::KeyPress::F2Key);
    add("pattern.meter", Scope::global, 'P', ctrl | shift);
    add("channel.properties", Scope::rack, juce::KeyPress::F2Key);
    add("pattern.findEmpty", Scope::global, juce::KeyPress::F4Key);
    add("pattern.firstEmpty", Scope::global, juce::KeyPress::F4Key, shift);
    add("pattern.moveUp", Scope::rack, juce::KeyPress::upKey, ctrl | shift);
    add("pattern.moveDown", Scope::rack, juce::KeyPress::downKey, ctrl | shift);
    add("pattern.moveUp", Scope::patterns, juce::KeyPress::upKey, ctrl | shift);
    add("pattern.moveDown", Scope::patterns, juce::KeyPress::downKey, ctrl | shift);
    add("pattern.delete", Scope::patterns, juce::KeyPress::deleteKey);
    add("pattern.clone", Scope::patterns, 'C', juce::ModifierKeys::altModifier);
    add("rack.graph", Scope::rack, 'K', ctrl);
    add("channel.clone", Scope::rack, 'C', juce::ModifierKeys::altModifier);
    add("channel.delete", Scope::rack, juce::KeyPress::deleteKey, juce::ModifierKeys::altModifier);
    add("channel.moveUp", Scope::rack, juce::KeyPress::upKey, juce::ModifierKeys::altModifier);
    add("channel.moveDown", Scope::rack, juce::KeyPress::downKey, juce::ModifierKeys::altModifier);
    add("pattern.insert", Scope::patterns, juce::KeyPress::insertKey, ctrl | shift);
    for (auto scope : {Scope::piano, Scope::playlist})
    {
        add("selection.all", scope, 'A', ctrl);
        add("selection.none", scope, 'D', ctrl);
        add("selection.next", scope, juce::KeyPress::tabKey);
        add("selection.previous", scope, juce::KeyPress::tabKey, shift);
        add("tool.draw", scope, 'P');
        add("tool.paint", scope, 'B');
    }
    add("tool.slice", Scope::playlist, 'C');
    add("tool.slip", Scope::playlist, 'S');
    add("clip.group", Scope::playlist, 'G', shift);
    add("clip.ungroup", Scope::playlist, 'G', juce::ModifierKeys::altModifier);
    add("tool.mute", Scope::playlist, 'T');
    add("piano.toolMute", Scope::piano, 'T');
    add("piano.toolSlice", Scope::piano, 'C');
    add("piano.insertSlide", Scope::piano, 'S');
    add("piano.insertPortamento", Scope::piano, 'O');
    add("piano.changeColour", Scope::piano, 'C', juce::ModifierKeys::altModifier);
    add("piano.selectColour", Scope::piano, 'C', shift);
    add("tool.select", Scope::playlist, 'E');
    add("tool.delete", Scope::playlist, 'D');
    add("tool.zoom", Scope::playlist, 'Z', shift);
    add("playlist.zoomIn", Scope::playlist, juce::KeyPress::pageUpKey);
    add("playlist.zoomOut", Scope::playlist, juce::KeyPress::pageDownKey);
    add("playlist.zoom1", Scope::playlist, '1', shift);
    add("playlist.zoom2", Scope::playlist, '2', shift);
    add("playlist.zoom3", Scope::playlist, '3', shift);
    add("playlist.fit", Scope::playlist, '4', shift);
    add("playlist.picker", Scope::playlist, 'P', juce::ModifierKeys::altModifier);
    add("playlist.zoomSelection", Scope::playlist, '5', shift);
    add("clipboard.copy", Scope::rack, 'C', ctrl);
    add("clipboard.paste", Scope::rack, 'V', ctrl);
    add("piano.zoomIn", Scope::piano, juce::KeyPress::pageUpKey);
    add("piano.zoomOut", Scope::piano, juce::KeyPress::pageDownKey);
    add("piano.fit", Scope::piano, '4', shift);
    add("piano.selectionZoom", Scope::piano, '5', shift);
    add("piano.preset1", Scope::piano, '1', shift);
    add("piano.preset2", Scope::piano, '2', shift);
    add("piano.preset3", Scope::piano, '3', shift);
    add("piano.ghosts", Scope::piano, 'V', juce::ModifierKeys::altModifier);
    add("piano.editGhosts", Scope::piano, 'V', ctrl | juce::ModifierKeys::altModifier);
    add("notes.nudgeLeft", Scope::piano, juce::KeyPress::leftKey, shift);
    add("notes.nudgeRight", Scope::piano, juce::KeyPress::rightKey, shift);
    add("clip.nudgeLeft", Scope::playlist, juce::KeyPress::leftKey, shift);
    add("clip.nudgeRight", Scope::playlist, juce::KeyPress::rightKey, shift);
    add("clip.trackUp", Scope::playlist, juce::KeyPress::upKey, shift);
    add("clip.trackDown", Scope::playlist, juce::KeyPress::downKey, shift);
    add("transport.home", Scope::global, juce::KeyPress::homeKey);
    add("marker.addAtPlayhead", Scope::playlist, 'T', juce::ModifierKeys::altModifier);
    add("transport.end", Scope::global, juce::KeyPress::endKey);
    add("notes.transposeUp", Scope::piano, juce::KeyPress::upKey, shift);
    add("notes.transposeDown", Scope::piano, juce::KeyPress::downKey, shift);
    add("notes.octaveUp", Scope::piano, juce::KeyPress::upKey, ctrl);
    add("notes.octaveDown", Scope::piano, juce::KeyPress::downKey, ctrl);
    add("notes.quantize", Scope::piano, 'Q', juce::ModifierKeys::altModifier);
    add("notes.quickQuantize", Scope::piano, 'Q', ctrl);
    add("notes.quantizeStarts", Scope::piano, 'Q', shift);
    add("notes.legato", Scope::piano, 'L', ctrl);
    add("notes.chop", Scope::piano, 'U', ctrl);
    add("notes.glue", Scope::piano, 'G', ctrl);
    add("notes.duplicate", Scope::piano, 'B', ctrl);
    add("notes.delete", Scope::piano, juce::KeyPress::deleteKey);
    add("clip.duplicate", Scope::playlist, 'B', ctrl);
    add("clip.merge", Scope::playlist, 'G', ctrl);
    add("clip.copy", Scope::playlist, 'C', ctrl);
    add("clip.cut", Scope::playlist, 'X', ctrl);
    add("clip.paste", Scope::playlist, 'V', ctrl);
    add("clip.quantize", Scope::playlist, 'Q', ctrl);
    add("time.insert", Scope::playlist, juce::KeyPress::insertKey, ctrl);
    add("time.insertSliced", Scope::playlist, juce::KeyPress::insertKey, ctrl | juce::ModifierKeys::altModifier);
    add("time.delete", Scope::playlist, juce::KeyPress::deleteKey, ctrl);
    add("clip.delete", Scope::playlist, juce::KeyPress::deleteKey);
    add("channel.mixer", Scope::rack, 'L', ctrl);
    add("rack.group.dialog", Scope::rack, 'G', juce::ModifierKeys::altModifier);
    add("rack.group.previous", Scope::rack, juce::KeyPress::pageUpKey);
    add("rack.group.next", Scope::rack, juce::KeyPress::pageDownKey);
    add("channel.mixer", Scope::mixer, 'L', ctrl);
    add("browser.back", Scope::browser, juce::KeyPress::backspaceKey);
    add("browser.forward", Scope::browser, juce::KeyPress::backspaceKey, shift);
    add("browser.back", Scope::browser, juce::KeyPress::leftKey, juce::ModifierKeys::altModifier);
    add("browser.forward", Scope::browser, juce::KeyPress::rightKey, juce::ModifierKeys::altModifier);
    add("browser.up", Scope::browser, juce::KeyPress::upKey, juce::ModifierKeys::altModifier);
    add("browser.find", Scope::browser, 'F', ctrl);
    add("browser.reindex", Scope::browser, 'R', ctrl);
    add("browser.nextMatch", Scope::browser, juce::KeyPress::F3Key);
    add("browser.previousMatch", Scope::browser, juce::KeyPress::F2Key);
    add("browser.previousMatch", Scope::browser, juce::KeyPress::F3Key, shift);
    return c;
}
juce::String Customization::resolve(const juce::KeyPress &key, Scope scope) const
{
    for (auto &b : bindings)
        if (b.scope == scope && b.key == key)
            return b.action;
    for (auto &b : bindings)
        if (b.scope == Scope::global && b.key == key)
            return b.action;
    return {};
}
void Customization::unbind(const juce::String &action, Scope scope)
{
    std::erase_if(bindings, [&](auto &b) { return b.action == action && b.scope == scope; });
}
juce::Result Customization::bind(const juce::String &action, Scope scope, const juce::KeyPress &key,
                                 bool alternate)
{
    if (!actionExists(*this, action) || int(scope) < 0 || int(scope) > int(Scope::sampler) ||
        key.getKeyCode() == 0)
        return juce::Result::fail("Invalid action, scope or shortcut");
    for (auto &b : bindings)
        // Resolution is contextual first, then global fallback. Validate the
        // same rule so a Browser F2 can coexist with global Pattern rename.
        if (b.key == key && b.scope == scope && b.action != action)
            return juce::Result::fail("Shortcut conflicts with " + b.action);
    if (!alternate)
        unbind(action, scope);
    for (auto &b : bindings)
        if (b.action == action && b.scope == scope && b.key == key)
            return juce::Result::ok();
    bindings.push_back({action, scope, key});
    return juce::Result::ok();
}
bool Customization::macroAllowed(const juce::String &id)
{
    const auto *command = info(id);
    return command && command->documentEdit && id != "channel.paste" && id != "channel.clone" &&
           id != "channel.delete" && id != "notes.record" && id != "plugin.effect.replace" &&
           id != "plugin.effect.sidechain" && id != "plugin.io" && id != "mixer.chain.paste" &&
           id != "mixer.chain.loadApply" && id != "mixer.insert.create" && id != "mixer.insert.delete" &&
           !id.startsWith("channel.sampler.controller.");
}
juce::Result Customization::addMacro(Macro macro)
{
    if (running)
        return juce::Result::fail("Cannot edit a running macro");
    if (!macro.id.startsWith("macro.") || macro.name.trim().isEmpty() || macro.steps.empty() ||
        macro.steps.size() > 64)
        return juce::Result::fail("A macro needs a name and 1-64 steps");
    for (auto &step : macro.steps)
    {
        if (auto modes = validateMacroNoteModes(step.action, step.parameters); modes.failed())
            return modes;
        if (auto valid = validateMacroImportedNotes(step.action, step.parameters.recordedNotes);
            valid.failed())
            return valid;
        if (step.action == "steps.compose" || !step.parameters.composition.empty())
        {
            if (step.action != "steps.compose" || !step.parameters.enabled || step.parameters.composition.empty())
                return juce::Result::fail("stepCompose.invalidArguments: Explicit enabled recipes required");
            juce::Array<juce::var> rows;
            for (const auto &recipe : step.parameters.composition) rows.add(stepComposeRecipeState(recipe));
            std::vector<StepComposeRecipe> parsed;
            if (auto result = parseStepComposeRecipes(rows, parsed); result.failed()) return result;
        }
        if (step.action == "pattern.extent" && step.parameters.text != "auto" &&
            step.parameters.text != "manual")
            return juce::Result::fail("patternExtent.invalidMode: Macro requires auto or manual");
        if (step.action == "pattern.channelLoop" && step.parameters.text != "off" &&
            step.parameters.text != "fixed" && step.parameters.text != "step" &&
            step.parameters.text != "beat" && step.parameters.text != "bar")
            return juce::Result::fail("channelLoop.invalidMode: Macro requires an explicit loop mode");
        if (step.action == "plugin.io" || step.parameters.pluginIo)
            return juce::Result::fail("pluginIo.macroUnsupported: Native I/O needs fresh capture and owned "
                                      "asynchronous application");
        if (step.action == "automation.source.clone" && step.parameters.text.isNotEmpty() &&
            step.parameters.text != "clear" && step.parameters.text != "preserve")
            return juce::Result::fail("automation.invalidLinkPolicy: Expected clear or preserve");
        if (step.parameters.retainSteps && !isStepRetentionCommand(step.action))
            return juce::Result::fail(
                "stepMemory.invalidArguments: retainSteps belongs only to step activation/fill");
        if (step.parameters.soloScope &&
            (step.action != "channel.solo" ||
             (*step.parameters.soloScope != "global" && *step.parameters.soloScope != "group")))
            return juce::Result::fail("channel.solo.invalidScope: Scoped solo requires global or group");
        if (step.parameters.mixerGroup &&
            (!step.action.startsWith("mixer.group.") || step.action == "mixer.group.create" ||
             (!step.parameters.mixerGroup->isEmpty() && !validMixerGroupId(*step.parameters.mixerGroup)) ||
             (step.parameters.mixerGroup->isEmpty() && step.action != "mixer.group.assign")))
            return juce::Result::fail("mixer.group.invalidArguments: Macro group UUID is invalid here");
        const bool samplerAutomation = isSamplerAutomationTargetCommand(step.action);
        if (samplerAutomation != step.parameters.samplerAutomationParameter.has_value() ||
            (step.parameters.samplerAutomationParameter &&
             (!lookupSamplerAutomationParameter(*step.parameters.samplerAutomationParameter) ||
              step.parameters.automationControl || step.parameters.automationSendTarget)))
            return juce::Result::fail("automation.invalidArguments: Sampler macro requires its exact "
                                      "supported parameter code only");
        if (samplerAutomation && !step.useCurrentContext && step.parameters.channel <= 0)
            return juce::Result::fail(
                "automation.invalidArguments: Sampler macro requires an explicit Channel or current context");
        if (samplerAutomation)
        {
            const auto parameter =
                *lookupSamplerAutomationParameter(*step.parameters.samplerAutomationParameter);
            if ((step.action == "automation.sampler.init" &&
                 !samplerAutomationValueValid(parameter, step.parameters.value)) ||
                (step.action == "automation.sampler.range" &&
                 (!samplerAutomationValueValid(parameter, step.parameters.beat) ||
                  !samplerAutomationValueValid(parameter, step.parameters.length))))
                return juce::Result::fail(
                    "automation.invalidArguments: Sampler macro values must use the exact parameter domain");
        }
        if (step.parameters.automationControl &&
            (!isMixerAutomationTargetCommand(step.action) ||
             !validMixerAutomationControl(*step.parameters.automationControl)))
            return juce::Result::fail("automation.invalidArguments: Macro automation control is unsupported here");
        const bool sendAutomation = step.parameters.automationControl == MixerAutomationControl::send;
        if (sendAutomation != step.parameters.automationSendTarget.has_value() ||
            (step.parameters.automationSendTarget &&
             (!isMixerAutomationTargetCommand(step.action) || *step.parameters.automationSendTarget < 0)) ||
            (sendAutomation && !step.useCurrentContext &&
             (step.parameters.mixer <= 0 || step.parameters.mixer == *step.parameters.automationSendTarget)))
            return juce::Result::fail("automation.invalidArguments: Send macro requires an exact distinct "
                                      "destination and non-Master source");
        if (step.parameters.mixerPosition && step.action != "mixer.move")
            return juce::Result::fail("mixer.invalidArguments: mixerPosition belongs only to mixer.move");
        const bool appearanceAction = step.action == "channel.properties" || step.action == "rack.properties";
        if ((appearanceAction != step.parameters.appearance.has_value()) ||
            ((step.action == "rack.sort") != step.parameters.channelSort.has_value()))
            return juce::Result::fail(
                "channelAppearance.invalidArguments: Typed fields must match their Channel operation");
        if (isMixerInsertOrderCommand(step.action))
        {
            if ((step.action == "mixer.move") != step.parameters.mixerPosition.has_value() ||
                (step.parameters.mixerPosition && (*step.parameters.mixerPosition < 0 || *step.parameters.mixerPosition > 128)))
                return juce::Result::fail("mixer.invalidPosition: Macro requires a typed valid display row only for mixer.move");
        }
        if (!macroAllowed(step.action))
            return juce::Result::fail("Action is not supported by the document-macro runner: " + step.action);
    }
    for (auto &existing : macros)
        if (existing.id == macro.id)
        {
            existing = std::move(macro);
            return juce::Result::ok();
        }
    if (macros.size() >= 128)
        return juce::Result::fail("Macro limit reached");
    macros.push_back(std::move(macro));
    return juce::Result::ok();
}
juce::Result Customization::removeMacro(const juce::String &id)
{
    if (running)
        return juce::Result::fail("Cannot delete a running macro");
    std::erase_if(macros, [&](auto &m) { return m.id == id; });
    std::erase_if(bindings, [&](auto &b) { return b.action == id; });
    return juce::Result::ok();
}
juce::Result Customization::runMacro(const juce::String &id, Controller &controller, MacroContext &context,
                                     std::function<bool()> cancelled)
{
    if (running || controller.hasGesture())
        return juce::Result::fail("Finish the current edit before running a macro");
    auto found = std::find_if(macros.begin(), macros.end(), [&](auto &m) { return m.id == id; });
    if (found == macros.end())
        return juce::Result::fail("Macro unavailable");
    for (const auto &step : found->steps)
        if (auto modes = validateMacroNoteModes(step.action, step.parameters); modes.failed())
            return modes;
    for (const auto &step : found->steps)
        if (auto valid = validateMacroImportedNotes(step.action, step.parameters.recordedNotes);
            valid.failed())
            return valid; // Refuse before earlier commands or gesture ownership can change anything.
    // Public macro storage can be edited directly by internal callers. Refuse
    // the ENTIRE sequence before gesture acquisition or any earlier step runs.
    if (std::any_of(found->steps.begin(), found->steps.end(), [](const auto &step)
                    { return step.action == "plugin.io" || step.parameters.pluginIo.has_value(); }))
        return juce::Result::fail("pluginIo.macroUnsupported: Native I/O is not a document-macro operation");
    if (std::any_of(found->steps.begin(), found->steps.end(), [](const auto &step)
                    { return step.parameters.retainSteps && !isStepRetentionCommand(step.action); }))
        return juce::Result::fail("stepMemory.invalidArguments: Unsupported macro retention payload");
    running = true;
    struct Guard
    {
        bool &running;
        ~Guard()
        {
            running = false;
        }
    } guard{running};
    const auto originalContext = context;
    if (!controller.beginGesture())
        return juce::Result::fail("Cannot acquire macro edit gesture");
    auto fail = [&](const juce::String &message)
    {
        const auto rollback = controller.cancelGestureChecked();
        if (rollback.failed())
            return juce::Result::fail(message + "; rollback pending: " + rollback.getErrorMessage());
        context = originalContext;
        return juce::Result::fail(message);
    };
    for (size_t i = 0; i < found->steps.size(); ++i)
    {
        if (cancelled && cancelled())
            return fail("Macro cancelled");
        const auto &step = found->steps[i];
        auto a = step.parameters;
        if (!a.restore)
            a.restore = restoreMuteAfterSolo;
        if (isStepRetentionCommand(step.action) && !a.retainSteps)
            a.retainSteps = muteRemovedSteps;
        if (step.useCurrentContext)
        {
            a.pattern = context.pattern;
            if (step.action != "audio.add")
                a.channel = context.channel;
            if (step.action.startsWith("notes.") && step.action != "notes.slice")
                a.ids = context.notes;
            if (step.action == "notes.stretch")
            {
                a.beat = 1000000;
                const std::unordered_set<Id> targets(a.ids.begin(), a.ids.end());
                if (const auto *pattern = controller.project.pattern(a.pattern))
                    for (const auto &note : pattern->notes)
                        if (targets.contains(note.id))
                            a.beat = std::min(a.beat, note.beat);
            }
            if (step.action == "rack.group.assign")
                a.ids = context.channels;
            if (step.action.startsWith("steps.fill"))
                a.ids = context.channels;
            if (step.action == "rack.muteSelected" || step.action == "rack.unmuteSelected" ||
                step.action == "rack.muteLock")
                a.ids = context.channels;
            if (step.action == "channel.moveUp" || step.action == "channel.moveDown")
                a.ids = context.channels;
            if (step.action == "rack.properties" || step.action == "rack.sort")
                a.ids = context.channels;
            if (step.action == "steps.velocity" || step.action == "steps.pitch" ||
                step.action == "steps.pan" || step.action == "steps.fine" || step.action == "steps.modX" ||
                step.action == "steps.modY" || step.action == "steps.shift" ||
                step.action == "steps.repeat" || step.action == "steps.release")
                a.ids.clear(); // Current Channel's active step notes, never Piano Roll selection IDs.
            if (step.action == "rack.swingMix" || step.action == "rack.truncateSwing")
                a.ids = context.channels;
            if (step.action.startsWith("clip.") || step.action == "audio.trim")
            {
                a.ids = context.clips;
                if (!context.clips.empty())
                    a.target = context.clips.front();
            }
            if (isMixerGroupCommand(step.action))
            {
                if (step.action == "mixer.group.create" || step.action == "mixer.group.assign" ||
                    step.action.startsWith("mixer.selection."))
                    a.ids.assign(context.mixerSelection.begin(), context.mixerSelection.end());
                if (step.action.startsWith("mixer.group.") && step.action != "mixer.group.create")
                    a.mixerGroup = context.mixerGroup;
            }
            if (step.action.startsWith("mixer.") ||
                (a.automationControl && isMixerAutomationTargetCommand(step.action)))
                a.mixer = context.insert;
        }
        if ((step.action.startsWith("notes.") && step.action != "notes.add" &&
             step.action != "notes.import" && step.action != "notes.legato" && step.action != "notes.chop" &&
             step.action != "notes.glue" && step.action != "notes.quantize" &&
             step.action != "notes.quickQuantize" && step.action != "notes.quantizeStarts" &&
             step.action != "notes.slice" && step.action != "notes.mute" && step.action != "notes.unmute") ||
            (step.action.startsWith("clip.") && step.action != "clip.add"))
        {
            if (a.ids.empty())
                return fail("Step " + juce::String(int(i) + 1) + ": select a target first");
        }
        const auto previousNextId = controller.project.nextId;
        auto result = controller.execute(step.action, a);
        if (result.failed())
            return fail("Step " + juce::String(int(i) + 1) + ": " + result.getErrorMessage());
        if (step.action == "mixer.group.create") context.mixerGroup = controller.project.mixerGroups.back().id;
        if (step.action == "mixer.group.delete" && a.mixerGroup && context.mixerGroup == *a.mixerGroup)
            context.mixerGroup.clear();
        if (step.action == "notes.duplicate")
        {
            auto *p = controller.project.pattern(a.pattern);
            context.notes.clear();
            for (auto n = p->notes.rbegin(); n != p->notes.rend() && context.notes.size() < a.ids.size(); ++n)
                context.notes.push_back(n->id);
        }
        if (step.action == "notes.chop")
        {
            context.notes = a.ids;
            if (const auto *pattern = controller.project.pattern(a.pattern))
                for (const auto &note : pattern->notes)
                    if ((a.ids.empty() && note.channel == a.channel) ||
                        (!a.ids.empty() && note.id >= previousNextId))
                        context.notes.push_back(note.id);
        }
        if (step.action == "notes.quantize")
            if (const auto *pattern = controller.project.pattern(a.pattern))
                std::erase_if(context.notes,
                              [&](Id id)
                              {
                                  return std::none_of(pattern->notes.begin(), pattern->notes.end(),
                                                      [&](const auto &note) { return note.id == id; });
                              });
        if (step.action == "notes.glue")
        {
            context.notes.clear();
            if (const auto *pattern = controller.project.pattern(a.pattern))
                for (const auto &note : pattern->notes)
                    if (a.ids.empty() ? note.channel == a.channel
                                      : std::find(a.ids.begin(), a.ids.end(), note.id) != a.ids.end())
                        context.notes.push_back(note.id);
        }
        if (step.action.startsWith("steps.fill"))
            if (const auto *pattern = controller.project.pattern(a.pattern))
                std::erase_if(context.notes,
                              [&](Id id)
                              {
                                  return std::none_of(pattern->notes.begin(), pattern->notes.end(),
                                                      [&](const auto &note) { return note.id == id; });
                              });
        if (step.action == "clip.duplicate" || step.action == "clip.slice")
        {
            context.clips = step.action == "clip.slice" ? a.ids : std::vector<Id>{};
            for (const auto &clip : controller.project.placements)
                if (clip.id >= previousNextId)
                    context.clips.push_back(clip.id);
            for (const auto &clip : controller.project.audioPlacements)
                if (clip.id >= previousNextId)
                    context.clips.push_back(clip.id);
            std::sort(context.clips.begin(), context.clips.end(), std::greater<Id>());
        }
        if (step.action == "pattern.add" || step.action == "pattern.clone")
            context.pattern = controller.project.patterns.back().id;
        if (step.action == "clip.unique")
            for (auto &clip : controller.project.placements)
                if (clip.id == a.target)
                    context.pattern = clip.pattern;
        if (step.action == "notes.add")
            context.notes = {controller.project.pattern(a.pattern)->notes.back().id};
        if (step.action == "clip.add")
            context.clips = {controller.project.placements.back().id};
        if (step.action == "channel.add")
            context.channel = controller.project.channels.back().id;
        if (step.action == "notes.delete")
            context.notes.clear();
        if (step.action == "clip.delete")
            context.clips.clear();
    }
    const auto completed = controller.endGestureChecked(found->name);
    if (completed.failed())
        return fail(completed.getErrorMessage());
    return juce::Result::ok();
}
static juce::var object()
{
    return juce::var(new juce::DynamicObject());
}
static juce::var encodeArgs(const CommandArgs &a)
{
    auto v = object();
    auto *p = v.getDynamicObject();
    p->setProperty("target", juce::int64(a.target));
    p->setProperty("channel", juce::int64(a.channel));
    p->setProperty("pattern", juce::int64(a.pattern));
    p->setProperty("beat", a.beat);
    p->setProperty("length", a.length);
    p->setProperty("offset", a.offset);
    p->setProperty("value", a.value);
    p->setProperty("interval", a.interval);
    if (a.noteProperties)
        p->setProperty("noteProperties", notePropertiesState(*a.noteProperties));
    if (a.slide)
        p->setProperty("slide", a.slide);
    if (a.portamento)
        p->setProperty("portamento", a.portamento);
    if (a.colourGroup != 0)
        p->setProperty("colourGroup", a.colourGroup);
    if (!a.recordedNotes.empty())
    {
        juce::Array<juce::var> notes;
        for (const auto &note : a.recordedNotes)
        {
            auto row = object();
            auto *fields = row.getDynamicObject();
            fields->setProperty("id", juce::int64(note.id));
            fields->setProperty("channel", juce::int64(note.channel));
            fields->setProperty("beat", note.beat);
            fields->setProperty("length", note.length);
            fields->setProperty("pitch", note.pitch);
            fields->setProperty("velocity", double(note.velocity));
            fields->setProperty("step", note.step);
            fields->setProperty("muted", note.muted);
            fields->setProperty("pan", note.pan);
            fields->setProperty("fineCents", note.fineCents);
            if (note.shiftPercent != 0)
                fields->setProperty("shiftPercent", note.shiftPercent);
            if (note.repeatMode != 0)
                fields->setProperty("repeatMode", note.repeatMode);
            if (note.releaseVelocity != 0)
                fields->setProperty("releaseVelocity", note.releaseVelocity);
            if (note.modX != .5)
                fields->setProperty("modX", note.modX);
            if (note.modY != .5)
                fields->setProperty("modY", note.modY);
            if (note.slide)
                fields->setProperty("slide", note.slide);
            if (note.portamento)
                fields->setProperty("portamento", note.portamento);
            if (note.colourGroup != 0)
                fields->setProperty("colourGroup", note.colourGroup);
            notes.add(row);
        }
        p->setProperty("recordedNotes", notes);
    }
    if (!a.composition.empty())
    {
        juce::Array<juce::var> rows;
        for (const auto &r : a.composition) rows.add(stepComposeRecipeState(r));
        p->setProperty("recipes", rows);
    }
    p->setProperty("strength", a.strength);
    p->setProperty("sensitivity", a.sensitivity);
    p->setProperty("durationStrength", a.durationStrength);
    p->setProperty("velocityStrength", a.velocityStrength);
    p->setProperty("zeroVelocityDeletes", a.zeroVelocityDeletes);
    if (a.groove)
        p->setProperty("groove", grooveState(*a.groove));
    if (a.slice)
        p->setProperty("slice", sliceLineState(*a.slice));
    if (a.retainSteps)
        p->setProperty("retainSteps", *a.retainSteps);
    if (a.restore)
        p->setProperty("restore", *a.restore);
    if (a.soloScope)
        p->setProperty("soloScope", *a.soloScope);
    if (a.grouping)
        p->setProperty("grouping", *a.grouping);
    p->setProperty("pitch", a.pitch);
    p->setProperty("lane", a.lane);
    p->setProperty("mixer", a.mixer);
    if (a.samplerAutomationParameter)
        p->setProperty("samplerParameter", juce::int64(*a.samplerAutomationParameter));
    if (a.automationControl) p->setProperty("automationControl", mixerAutomationControlName(*a.automationControl));
    if (a.automationSendTarget)
        p->setProperty("automationSendTarget", *a.automationSendTarget);
    if (a.mixerPosition) p->setProperty("mixerPosition", *a.mixerPosition);
    encodeChannelAppearance(*p, a);
    if (a.mixerGroup) p->setProperty("mixerGroup", *a.mixerGroup);
    p->setProperty("numerator", a.numerator);
    p->setProperty("denominator", a.denominator);
    p->setProperty("enabled", a.enabled);
    p->setProperty("step", a.step);
    p->setProperty("text", a.text);
    // Observation of directly corrupted internal storage must retain payload
    // presence too; import/run/save independently REFUSE this unsupported data.
    if (a.pluginIo)
    {
        auto io = object();
        io.getDynamicObject()->setProperty("inputBus", a.pluginIo->inputBus);
        io.getDynamicObject()->setProperty("outputBus", a.pluginIo->outputBus);
        io.getDynamicObject()->setProperty("inputChannels", a.pluginIo->inputChannels);
        io.getDynamicObject()->setProperty("monoSource", a.pluginIo->monoSource);
        p->setProperty("pluginIo", io);
    }
    if (a.binding)
    {
        auto binding = object();
        const auto tree = pluginBindingsState({&*a.binding, 1}).getChild(0);
        for (int i = 0; i < tree.getNumProperties(); ++i)
        {
            const auto name = tree.getPropertyName(i);
            binding.getDynamicObject()->setProperty(name, tree[name]);
        }
        p->setProperty("binding", binding);
    }
    juce::Array<juce::var> ids;
    for (auto id : a.ids)
        ids.add(juce::int64(id));
    p->setProperty("ids", ids);
    return v;
}
static CommandArgs decodeArgs(const juce::var &v)
{
    CommandArgs a;
    decodeChannelAppearance(v, a); // Import validates first; malformed data is never silently accepted.
    a.target = juce::int64(v["target"]);
    a.channel = juce::int64(v["channel"]);
    a.pattern = juce::int64(v["pattern"]);
    a.beat = double(v["beat"]);
    a.length = double(v["length"]);
    a.offset = v.hasProperty("offset") ? double(v["offset"]) : 0;
    a.value = double(v["value"]);
    a.interval = int(v.getProperty("interval", 4));
    if (v.hasProperty("noteProperties"))
    {
        NotePropertiesPatch patch;
        if (decodeNotePropertiesPatch(v["noteProperties"], patch).wasOk())
            a.noteProperties = std::move(patch);
    }
    a.slide = bool(v.getProperty("slide", false));
    a.portamento = bool(v.getProperty("portamento", false));
    a.colourGroup = int(v.getProperty("colourGroup", 0));
    if (v.hasProperty("recordedNotes"))
        (void)parseMacroImportedNotes(v["recordedNotes"], a.recordedNotes); // Import validates before decode.
    if (v.hasProperty("recipes")) (void)parseStepComposeRecipes(v["recipes"], a.composition);
    a.strength = double(v.getProperty("strength", 1.));
    a.sensitivity = double(v.getProperty("sensitivity", 1.));
    a.durationStrength = double(v.getProperty("durationStrength", 1.));
    a.velocityStrength = double(v.getProperty("velocityStrength", 0.));
    a.zeroVelocityDeletes = bool(v.getProperty("zeroVelocityDeletes", true));
    if (v.hasProperty("slice"))
    {
        NoteSliceLine line;
        if (parseSliceLine(v["slice"], line).wasOk())
            a.slice = line;
    }
    if (v.hasProperty("groove"))
    {
        GrooveTemplate groove;
        if (parseGroove(v["groove"], groove).wasOk())
            a.groove = std::move(groove);
    }
    if (v.hasProperty("retainSteps"))
        a.retainSteps = bool(v["retainSteps"]);
    if (v.hasProperty("restore"))
        a.restore = bool(v["restore"]);
    if (v.hasProperty("soloScope"))
        a.soloScope = v["soloScope"].toString();
    if (v.hasProperty("grouping"))
        a.grouping = bool(v["grouping"]);
    a.pitch = int(v["pitch"]);
    a.lane = int(v["lane"]);
    a.mixer = int(v["mixer"]);
    if (v.hasProperty("samplerParameter"))
    {
        const auto code = v["samplerParameter"];
        bool valid = (code.isInt() || code.isInt64() || code.isDouble()) && std::isfinite(double(code)) &&
                     double(code) >= 0 && double(code) <= 159 && std::floor(double(code)) == double(code);
        for (const auto *field : {"value", "beat", "length"})
            if (v.hasProperty(field))
            {
                const auto value = v[field];
                valid = valid && (value.isInt() || value.isInt64() || value.isDouble()) &&
                        std::isfinite(double(value));
            }
        if (v.hasProperty("minimum") || v.hasProperty("maximum"))
        {
            valid = valid && v.hasProperty("minimum") && v.hasProperty("maximum");
            for (const auto *field : {"minimum", "maximum"})
            {
                const auto value = v[field];
                valid = valid && (value.isInt() || value.isInt64() || value.isDouble()) &&
                        std::isfinite(double(value));
            }
            a.beat = double(v["minimum"]);
            a.length = double(v["maximum"]);
        }
        a.samplerAutomationParameter =
            valid ? std::uint32_t(juce::int64(code)) : std::numeric_limits<std::uint32_t>::max();
    }
    if (v.hasProperty("automationControl")) a.automationControl = parseMixerAutomationControl(v["automationControl"]);
    if (v.hasProperty("automationSendTarget"))
        a.automationSendTarget = int(v["automationSendTarget"]);
    if (v.hasProperty("mixerPosition")) a.mixerPosition = int(v["mixerPosition"]);
    if (v.hasProperty("mixerGroup")) a.mixerGroup = v["mixerGroup"].toString();
    a.numerator = v.hasProperty("numerator") ? int(v["numerator"]) : 4;
    a.denominator = v.hasProperty("denominator") ? int(v["denominator"]) : 4;
    a.enabled = bool(v["enabled"]);
    a.step = bool(v["step"]);
    a.text = v["text"].toString();
    if (const auto *binding = v["binding"].getDynamicObject())
    {
        juce::ValueTree root("AUTOMATION_BINDINGS"), item("BINDING");
        for (const auto &property : binding->getProperties())
            item.setProperty(property.name, property.value, nullptr);
        root.appendChild(item, nullptr);
        std::vector<PluginBinding> parsed;
        if (parsePluginBindings(root, parsed).wasOk() && parsed.size() == 1)
            a.binding = parsed.front();
    }
    if (auto *ids = v["ids"].getArray())
        for (auto &id : *ids)
            a.ids.push_back(juce::int64(id));
    return a;
}
juce::var Customization::state() const
{
    auto root = object();
    const bool hasGrooves =
        std::any_of(macros.begin(), macros.end(),
                    [](const auto &macro)
                    {
                        return std::any_of(macro.steps.begin(), macro.steps.end(), [](const auto &step)
                                           { return step.parameters.groove.has_value(); });
                    });
    const bool hasSlices =
        std::any_of(macros.begin(), macros.end(),
                    [](const auto &m)
                    {
                        return std::any_of(m.steps.begin(), m.steps.end(),
                                           [](const auto &s) { return s.parameters.slice.has_value(); });
                    });
    const bool typedAutomation = std::any_of(macros.begin(), macros.end(), [](const auto &macro)
    { return std::any_of(macro.steps.begin(), macro.steps.end(), [](const auto &step)
      { return step.parameters.automationControl.has_value(); }); });
    const bool typedGroups = std::any_of(macros.begin(), macros.end(), [](const auto &macro)
    { return std::any_of(macro.steps.begin(), macro.steps.end(), [](const auto &step) { return step.parameters.mixerGroup.has_value(); }); });
    const bool sendAutomation = std::any_of(
        macros.begin(), macros.end(),
        [](const auto &macro)
        {
            return std::any_of(macro.steps.begin(), macro.steps.end(),
                               [](const auto &step)
                               {
                                   return step.parameters.automationControl == MixerAutomationControl::send ||
                                          step.parameters.automationSendTarget.has_value();
                               });
        });
    const bool channelAppearance =
        std::any_of(macros.begin(), macros.end(),
                    [](const auto &macro)
                    {
                        return std::any_of(macro.steps.begin(), macro.steps.end(),
                                           [](const auto &step)
                                           {
                                               return step.parameters.appearance.has_value() ||
                                                      step.parameters.channelSort.has_value();
                                           });
                    });
    const bool scopedSolo =
        std::any_of(macros.begin(), macros.end(),
                    [](const auto &macro)
                    {
                        return std::any_of(macro.steps.begin(), macro.steps.end(), [](const auto &step)
                                           { return step.parameters.soloScope.has_value(); });
                    });
    const bool hasComposition = std::any_of(macros.begin(), macros.end(), [](const auto &macro)
    { return std::any_of(macro.steps.begin(), macro.steps.end(), [](const auto &step)
      { return !step.parameters.composition.empty(); }); });
    const bool hasStepMemory =
        muteRemovedSteps ||
        std::any_of(macros.begin(), macros.end(),
                    [](const auto &macro)
                    {
                        return std::any_of(macro.steps.begin(), macro.steps.end(), [](const auto &step)
                                           { return step.parameters.retainSteps.has_value(); });
                    });
    const bool hasLoopSlice = std::any_of(
        macros.begin(), macros.end(),
        [](const auto &macro)
        {
            return std::any_of(macro.steps.begin(), macro.steps.end(), [](const auto &step)
                               { return step.parameters.slice && step.parameters.slice->loopPreview; });
        });
    const bool hasImportedNotes =
        std::any_of(macros.begin(), macros.end(),
                    [](const auto &macro)
                    {
                        return std::any_of(macro.steps.begin(), macro.steps.end(), [](const auto &step)
                                           { return !step.parameters.recordedNotes.empty(); });
                    });
    const bool hasShiftedImport =
        std::any_of(macros.begin(), macros.end(),
                    [](const auto &macro)
                    {
                        return std::any_of(macro.steps.begin(), macro.steps.end(),
                                           [](const auto &step)
                                           {
                                               return std::any_of(step.parameters.recordedNotes.begin(),
                                                                  step.parameters.recordedNotes.end(),
                                                                  [](const Note &note)
                                                                  { return note.shiftPercent != 0; });
                                           });
                    });
    const bool hasRepeatedImport = std::any_of(
        macros.begin(), macros.end(),
        [](const auto &macro)
        {
            return std::any_of(macro.steps.begin(), macro.steps.end(),
                               [](const auto &step)
                               {
                                   return std::any_of(step.parameters.recordedNotes.begin(),
                                                      step.parameters.recordedNotes.end(),
                                                      [](const Note &note) { return note.repeatMode != 0; });
                               });
        });
    const bool hasReleaseImport =
        std::any_of(macros.begin(), macros.end(),
                    [](const auto &macro)
                    {
                        return std::any_of(macro.steps.begin(), macro.steps.end(),
                                           [](const auto &step)
                                           {
                                               return std::any_of(step.parameters.recordedNotes.begin(),
                                                                  step.parameters.recordedNotes.end(),
                                                                  [](const Note &note)
                                                                  { return note.releaseVelocity != 0; });
                                           });
                    });
    const bool hasModulatedImport =
        std::any_of(macros.begin(), macros.end(),
                    [](const auto &macro)
                    {
                        return std::any_of(macro.steps.begin(), macro.steps.end(),
                                           [](const auto &step)
                                           {
                                               return std::any_of(
                                                   step.parameters.recordedNotes.begin(),
                                                   step.parameters.recordedNotes.end(), [](const Note &note)
                                                   { return note.modX != .5 || note.modY != .5; });
                                           });
                    });
    const bool samplerAutomation = std::any_of(
        macros.begin(), macros.end(),
        [](const auto &macro)
        {
            return std::any_of(macro.steps.begin(), macro.steps.end(), [](const auto &step)
                               { return step.parameters.samplerAutomationParameter.has_value(); });
        });
    const bool hasNoteModes = std::any_of(
        macros.begin(), macros.end(),
        [](const auto &macro)
        {
            return std::any_of(
                macro.steps.begin(), macro.steps.end(),
                [](const auto &step)
                {
                    const auto &args = step.parameters;
                    return args.noteProperties.has_value() || args.slide || args.portamento ||
                           args.colourGroup != 0 ||
                           std::any_of(args.recordedNotes.begin(), args.recordedNotes.end(),
                                       [](const Note &note)
                                       { return note.slide || note.portamento || note.colourGroup != 0; });
                });
        });
    root.getDynamicObject()->setProperty("version", hasNoteModes         ? 18
                                                    : samplerAutomation  ? 17
                                                    : hasModulatedImport ? 16
                                                    : hasReleaseImport   ? 15
                                                    : hasRepeatedImport  ? 14
                                                    : hasShiftedImport   ? 13
                                                    : hasImportedNotes   ? 12
                                                    : hasLoopSlice       ? 11
                                                    : hasStepMemory      ? 10
                                                    : hasComposition     ? 9
                                                    : scopedSolo         ? 8
                                                    : channelAppearance  ? 7
                                                    : sendAutomation     ? 6
                                                    : typedGroups        ? 5
                                                    : typedAutomation    ? 4
                                                    : hasSlices          ? 3
                                                    : hasGrooves         ? 2
                                                                         : 1);
    root.getDynamicObject()->setProperty("restoreMuteAfterSolo", restoreMuteAfterSolo);
    // Emit only in profile10: older readers must refuse, never silently discard.
    if (hasStepMemory)
        root.getDynamicObject()->setProperty("muteRemovedSteps", muteRemovedSteps);
    juce::Array<juce::var> knownScopes;
    for (int scope = 0; scope <= int(Scope::sampler); ++scope)
        knownScopes.add(scope);
    root.getDynamicObject()->setProperty("knownScopes", knownScopes);
    juce::Array<juce::var> knownActions;
    for (const auto &command : commandRegistry())
        knownActions.add(command.id);
    root.getDynamicObject()->setProperty("knownActions", knownActions);
    juce::Array<juce::var> bs, ms;
    for (auto &b : bindings)
    {
        auto v = object();
        v.getDynamicObject()->setProperty("action", b.action);
        v.getDynamicObject()->setProperty("scope", int(b.scope));
        v.getDynamicObject()->setProperty("key", b.key.getTextDescription());
        bs.add(v);
    }
    for (auto &m : macros)
    {
        auto v = object();
        auto *p = v.getDynamicObject();
        p->setProperty("id", m.id);
        p->setProperty("name", m.name);
        juce::Array<juce::var> steps;
        for (auto &s : m.steps)
        {
            auto e = object();
            e.getDynamicObject()->setProperty("action", s.action);
            e.getDynamicObject()->setProperty("currentContext", s.useCurrentContext);
            auto parameters = encodeArgs(s.parameters);
            if (s.action == "automation.sampler.range")
            {
                parameters.getDynamicObject()->setProperty("minimum", s.parameters.beat);
                parameters.getDynamicObject()->setProperty("maximum", s.parameters.length);
                parameters.getDynamicObject()->removeProperty("beat");
                parameters.getDynamicObject()->removeProperty("length");
            }
            e.getDynamicObject()->setProperty("parameters", parameters);
            steps.add(e);
        }
        p->setProperty("steps", steps);
        ms.add(v);
    }
    root.getDynamicObject()->setProperty("bindings", bs);
    root.getDynamicObject()->setProperty("macros", ms);
    return root;
}
juce::Result Customization::importState(const juce::var &v)
{
    if (running)
        return juce::Result::fail("Cannot import settings during a macro");
    if (!v.isObject() || !v["version"].isInt() ||
        (int(v["version"]) != 1 && int(v["version"]) != 2 && int(v["version"]) != 3 &&
         int(v["version"]) != 4 && int(v["version"]) != 5 && int(v["version"]) != 6 &&
         int(v["version"]) != 7 && int(v["version"]) != 8 && int(v["version"]) != 9 &&
         int(v["version"]) != 10 && int(v["version"]) != 11 && int(v["version"]) != 12 &&
         int(v["version"]) != 13 && int(v["version"]) != 14 && int(v["version"]) != 15 &&
         int(v["version"]) != 16 && int(v["version"]) != 17 && int(v["version"]) != maximumProfileVersion) ||
        !v["bindings"].isArray() || !v["macros"].isArray())
        return juce::Result::fail("Unsupported or incomplete shortcut profile");
    if (v["bindings"].size() > 1024 || v["macros"].size() > 128)
        return juce::Result::fail("Profile limits exceeded");
    if (int(v["version"]) >= 12 && juce::JSON::toString(v).getNumBytesAsUTF8() > 1024 * 1024)
        return juce::Result::fail("notesImport.profileLimit: Settings must remain loadable within1MiB");
    Customization next;
    if (v.hasProperty("muteRemovedSteps"))
    {
        if (int(v["version"]) < 10 || !v["muteRemovedSteps"].isBool())
            return juce::Result::fail("stepMemory.invalidProfile: Boolean preference requires profile10");
        next.muteRemovedSteps = bool(v["muteRemovedSteps"]);
    }
    if (v.hasProperty("restoreMuteAfterSolo"))
    {
        if (!v["restoreMuteAfterSolo"].isBool())
            return juce::Result::fail("Solo restore preference must be boolean");
        next.restoreMuteAfterSolo = bool(v["restoreMuteAfterSolo"]);
    }
    for (auto &m : *v["macros"].getArray())
    {
        if (!m["steps"].isArray())
            return juce::Result::fail("Incomplete macro");
        Macro macro;
        macro.id = m["id"].toString();
        macro.name = m["name"].toString();
        for (auto &step : *m["steps"].getArray())
        {
            // Check presence BEFORE decodeArgs; no unsupported payload may be
            // silently discarded and imported as a different ordinary command.
            if (step["action"].toString() == "plugin.io" || step["parameters"].hasProperty("pluginIo"))
                return juce::Result::fail(
                    "pluginIo.macroUnsupported: Native I/O cannot be imported as a macro");
            if (!step["parameters"].isObject())
                return juce::Result::fail("Invalid macro parameters");
            if (step["parameters"].hasProperty("noteProperties"))
            {
                NotePropertiesPatch patch;
                if (int(v["version"]) < 18 || step["action"].toString() != "notes.properties")
                    return juce::Result::fail("noteMode.invalidProfile: Note Properties requires profile18");
                if (auto valid = decodeNotePropertiesPatch(step["parameters"]["noteProperties"], patch);
                    valid.failed())
                    return valid;
            }
            if (step["action"].toString() == "notes.slide" || step["action"].toString() == "notes.portamento")
                if (!step["parameters"].hasProperty("enabled") || !step["parameters"]["enabled"].isBool())
                    return juce::Result::fail(
                        "noteMode.invalidProfile: Mode setters require enabled boolean");
            if (step["action"].toString() == "notes.colourGroup")
            {
                const auto value = step["parameters"]["value"];
                if ((!value.isInt() && !value.isInt64() && !value.isDouble()) ||
                    !std::isfinite(double(value)) || double(value) < 0 || double(value) > 15 ||
                    std::floor(double(value)) != double(value))
                    return juce::Result::fail(
                        "noteMode.invalidProfile: Colour group setter requires exact numeric0..15");
            }
            for (const auto *field : {"slide", "portamento", "colourGroup"})
                if (step["parameters"].hasProperty(field))
                {
                    const auto value = step["parameters"][field];
                    const bool group = juce::String(field) == "colourGroup";
                    const bool valid = group ? (value.isInt() || value.isInt64() || value.isDouble()) &&
                                                   std::isfinite(double(value)) && double(value) >= 0 &&
                                                   double(value) <= 15 &&
                                                   std::floor(double(value)) == double(value)
                                             : value.isBool();
                    if (int(v["version"]) < 18 || step["action"].toString() != "notes.add" || !valid)
                        return juce::Result::fail(
                            "noteMode.invalidProfile: Typed creation fields require profile18 and notes.add");
                }
            if (step["parameters"].hasProperty("retainSteps") &&
                (int(v["version"]) < 10 || !isStepRetentionCommand(step["action"].toString()) ||
                 !step["parameters"]["retainSteps"].isBool()))
                return juce::Result::fail(
                    "stepMemory.invalidProfile: Typed step retention requires profile10");
            if (step["parameters"].hasProperty("soloScope") &&
                (int(v["version"]) < 8 || step["action"].toString() != "channel.solo" ||
                 !step["parameters"]["soloScope"].isString() ||
                 (step["parameters"]["soloScope"].toString() != "global" &&
                  step["parameters"]["soloScope"].toString() != "group")))
                return juce::Result::fail(
                    "channel.solo.invalidScope: Scoped solo requires profile8/global or group");
            if (step["parameters"].hasProperty("appearance") || step["parameters"].hasProperty("channelSort"))
            {
                CommandArgs parsed;
                if (int(v["version"]) < 7)
                    return juce::Result::fail("Channel properties/sort macros require profile7");
                if (auto result = decodeChannelAppearance(step["parameters"], parsed); result.failed())
                    return result;
            }
            if (step["parameters"].hasProperty("mixerGroup") &&
                (int(v["version"]) < 5 || !step["parameters"]["mixerGroup"].isString()))
                return juce::Result::fail("mixer.group.invalidArguments: Typed group macros require profile5");
            if (step["parameters"].hasProperty("mixerPosition"))
            {
                const auto value = step["parameters"]["mixerPosition"];
                if ((!value.isInt() && !value.isInt64() && !value.isDouble()) ||
                    !std::isfinite(double(value)) || double(value) != std::floor(double(value)) ||
                    double(value) < 0 || double(value) > 128)
                    return juce::Result::fail("mixer.invalidPosition: Macro display row must be an exact integer0..128");
            }
            if (step["parameters"].hasProperty("restore") && !step["parameters"]["restore"].isBool())
                return juce::Result::fail("Macro solo restore must be boolean");
            if (step["parameters"].hasProperty("grouping") && !step["parameters"]["grouping"].isBool())
                return juce::Result::fail("Macro Clip grouping override must be boolean");
            if (step["parameters"].hasProperty("interval"))
            {
                const auto value = step["parameters"]["interval"];
                if ((!value.isInt() && !value.isInt64() && !value.isDouble()) ||
                    (double(value) != 2 && double(value) != 4 && double(value) != 8))
                    return juce::Result::fail("Macro fill interval must be 2, 4 or 8");
            }
            for (auto field : {"strength", "sensitivity", "durationStrength", "velocityStrength"})
                if (step["parameters"].hasProperty(field))
                {
                    const auto value = step["parameters"][field];
                    if ((!value.isInt() && !value.isInt64() && !value.isDouble()) ||
                        !std::isfinite(double(value)) || double(value) < 0 || double(value) > 1)
                        return juce::Result::fail("Quantizer macro strengths must be normalized numbers");
                }
            if (step["parameters"].hasProperty("groove") && int(v["version"]) < 2)
                return juce::Result::fail("Groove macros require shortcut profile version2");
            if (step["parameters"].hasProperty("slice") && int(v["version"]) < 3)
                return juce::Result::fail("Slice macro data requires profile version3");
            if (step["parameters"].hasProperty("automationControl") &&
                (int(v["version"]) < 4 || !parseMixerAutomationControl(step["parameters"]["automationControl"])))
                return juce::Result::fail("automation.invalidArguments: Typed automation macro requires profile4 and a supported control");
            const auto parameters = step["parameters"];
            if (isSamplerAutomationTargetCommand(step["action"].toString()))
            {
                // Validate raw identities BEFORE decodeArgs can coerce/truncate them.
                const bool context = bool(step["currentContext"]);
                if (step.hasProperty("currentContext") && !step["currentContext"].isBool())
                    return juce::Result::fail(
                        "automation.invalidProfile: Sampler currentContext must be boolean");
                const auto identity = [&](const char *field, bool allowZero)
                {
                    const auto value = parameters[field];
                    return parameters.hasProperty(field) &&
                           (value.isInt() || value.isInt64() || value.isDouble()) &&
                           std::isfinite(double(value)) && std::floor(double(value)) == double(value) &&
                           double(value) >= (allowZero ? 0 : 1) && double(value) <= 1000000000.;
                };
                // Generic profile serialization retains channel0 for current-context recipes.
                // It is a typed inert sentinel only there, never a source or explicit owner.
                if ((!context || parameters.hasProperty("channel")) && !identity("channel", context))
                    return juce::Result::fail("automation.invalidProfile: Sampler Channel must be an exact "
                                              "stable numeric identity");
                const auto action = step["action"].toString();
                if ((action == "automation.sampler.link" || action == "automation.sampler.unlink" ||
                     action == "automation.sampler.range") &&
                    !identity("target", false))
                    return juce::Result::fail("automation.invalidProfile: Sampler link operation requires an "
                                              "exact Automation source identity");
                if (action == "automation.sampler.init")
                {
                    const auto value = parameters["value"];
                    if (!parameters.hasProperty("value") ||
                        (!value.isInt() && !value.isInt64() && !value.isDouble()) ||
                        !std::isfinite(double(value)))
                        return juce::Result::fail("automation.invalidProfile: Sampler initialization "
                                                  "requires an explicit finite numeric value");
                }
            }
            if (parameters.hasProperty("samplerParameter") && int(v["version"]) < 17)
                return juce::Result::fail(
                    "automation.invalidProfile: Typed sampler automation requires profile17");
            if (step["action"].toString() == "automation.sampler.range")
            {
                if (!parameters.hasProperty("minimum") || !parameters.hasProperty("maximum") ||
                    parameters.hasProperty("beat") || parameters.hasProperty("length"))
                    return juce::Result::fail(
                        "automation.invalidProfile: Sampler range uses explicit minimum/maximum only");
            }
            else if (parameters.hasProperty("minimum") || parameters.hasProperty("maximum"))
                return juce::Result::fail(
                    "automation.invalidProfile: Range endpoints belong only to sampler range macros");
            const auto control = parameters.hasProperty("automationControl")
                                     ? parseMixerAutomationControl(parameters["automationControl"])
                                     : std::optional<MixerAutomationControl>{};
            const bool sendAutomation = control == MixerAutomationControl::send;
            if ((sendAutomation || parameters.hasProperty("automationSendTarget")) && int(v["version"]) < 6)
                return juce::Result::fail(
                    "automation.invalidArguments: Send targets require shortcut profile6");
            if (sendAutomation != parameters.hasProperty("automationSendTarget"))
                return juce::Result::fail(
                    "automation.invalidArguments: Send destination/control must be present together");
            if (parameters.hasProperty("automationSendTarget"))
            {
                const auto destination = parameters["automationSendTarget"];
                if ((!destination.isInt() && !destination.isInt64() && !destination.isDouble()) ||
                    !std::isfinite(double(destination)) ||
                    double(destination) != std::floor(double(destination)) || double(destination) < 0 ||
                    double(destination) > std::numeric_limits<int>::max())
                    return juce::Result::fail("automation.invalidArguments: Send destination must be an "
                                              "exact nonnegative stable ID");
            }
            if (parameters.hasProperty("recordedNotes"))
            {
                if (int(v["version"]) < 12 || step["action"].toString() != "notes.import")
                    return juce::Result::fail("notesImport.invalidProfile: Typed imported notes require "
                                              "profile12 and notes.import");
                if (int(v["version"]) < 13)
                    if (const auto *rows = parameters["recordedNotes"].getArray())
                        for (const auto &row : *rows)
                            if (row.hasProperty("shiftPercent"))
                                return juce::Result::fail(
                                    "notesImport.invalidProfile: Shift presence requires profile13");
                if (int(v["version"]) < 14)
                    if (const auto *rows = parameters["recordedNotes"].getArray())
                        for (const auto &row : *rows)
                            if (row.hasProperty("repeatMode"))
                                return juce::Result::fail(
                                    "notesImport.invalidProfile: Repeat presence requires profile14");
                if (int(v["version"]) < 15)
                    if (const auto *rows = parameters["recordedNotes"].getArray())
                        for (const auto &row : *rows)
                            if (row.hasProperty("releaseVelocity"))
                                return juce::Result::fail("notesImport.invalidProfile: Release velocity "
                                                          "presence requires profile15");
                if (int(v["version"]) < 16)
                    if (const auto *rows = parameters["recordedNotes"].getArray())
                        for (const auto &row : *rows)
                            if (row.hasProperty("modX") || row.hasProperty("modY"))
                                return juce::Result::fail(
                                    "notesImport.invalidProfile: Mod X/Y presence requires profile16");
                if (int(v["version"]) < 18)
                    if (const auto *rows = parameters["recordedNotes"].getArray())
                        for (const auto &row : *rows)
                            if (row.hasProperty("slide") || row.hasProperty("portamento") ||
                                row.hasProperty("colourGroup"))
                                return juce::Result::fail(
                                    "noteMode.invalidProfile: Imported Note modes require profile18");
                std::vector<Note> checked;
                if (auto valid = parseMacroImportedNotes(parameters["recordedNotes"], checked);
                    valid.failed())
                    return valid;
            }
            auto args = decodeArgs(parameters);
            if (parameters.hasProperty("recipes"))
            {
                if (int(v["version"]) < 9 || step["action"].toString() != "steps.compose")
                    return juce::Result::fail("stepCompose.invalidArguments: Recipes require profile9 and steps.compose");
                if (auto result = parseStepComposeRecipes(parameters["recipes"], args.composition); result.failed())
                    return result;
            }
            if (step["parameters"].hasProperty("slice") && !args.slice)
                return juce::Result::fail("Invalid Slice line in macro");
            if (args.slice && args.slice->loopPreview && int(v["version"]) < 11)
                return juce::Result::fail(
                    "loopPreview.profile: Display-space Slice recipes require profile11");
            if (step["parameters"].hasProperty("groove") && !args.groove)
                return juce::Result::fail("Invalid groove template in macro");
            if (step["parameters"].hasProperty("zeroVelocityDeletes") &&
                !step["parameters"]["zeroVelocityDeletes"].isBool())
                return juce::Result::fail("Groove rest flag must be boolean");
            if (step["parameters"].hasProperty("binding") && !args.binding)
                return juce::Result::fail("Invalid plugin binding macro parameters");
            macro.steps.push_back({step["action"].toString(), std::move(args), bool(step["currentContext"])});
        }
        auto r = next.addMacro(std::move(macro));
        if (r.failed())
            return r;
    }
    for (auto &b : *v["bindings"].getArray())
    {
        auto r = next.bind(b["action"].toString(), Scope(int(b["scope"])),
                           juce::KeyPress::createFromDescription(b["key"].toString()), true);
        if (r.failed())
            return juce::Result::fail("Shortcut " + b["action"].toString() + " (" + b["key"].toString() +
                                      "): " + r.getErrorMessage());
    }
    if (v.hasProperty("knownActions") && (!v["knownActions"].isArray() || v["knownActions"].size() > 1024))
        return juce::Result::fail("Invalid shortcut action registry");
    juce::StringArray known;
    if (v["knownActions"].isArray())
        for (const auto &action : *v["knownActions"].getArray())
            known.add(action.toString());
    else
        for (const auto &command : commandRegistry())
            if (command.id != "tool.select" && command.id != "tool.delete" && command.id != "tool.zoom" &&
                !command.id.startsWith("playlist.") && command.id != "piano.toolMute" &&
                command.id != "piano.toolSlice" && command.id != "pattern.renameDialog" &&
                command.id != "channel.properties" && command.id != "pattern.findEmpty" &&
                command.id != "pattern.firstEmpty" && command.id != "pattern.moveUp" &&
                command.id != "pattern.moveDown" && command.id != "browser.find" &&
                command.id != "browser.nextMatch" && command.id != "browser.previousMatch" &&
                command.id != "browser.reindex" && command.id != "notes.legato" &&
                command.id != "notes.chop" && command.id != "notes.glue" &&
                command.id != "piano.insertSlide" && command.id != "piano.insertPortamento" &&
                command.id != "piano.changeColour" && command.id != "piano.selectColour" &&
                command.id != "piano.noteProperties" && !command.id.startsWith("rack.group."))
                known.add(command.id);
    // Add newly introduced defaults only when they do not overwrite user bindings.
    // Saved knownActions keeps deliberately unbound commands unbound on later loads.
    const auto defaults = familiarDefaults();
    for (const auto &binding : defaults.bindings)
        if (!known.contains(binding.action) &&
            std::none_of(next.bindings.begin(), next.bindings.end(),
                         [&](const auto &existing) { return existing.action == binding.action; }) &&
            std::none_of(next.bindings.begin(), next.bindings.end(), [&](const auto &existing)
                         { return existing.scope == binding.scope && existing.key == binding.key; }) &&
            (next.resolve(binding.key, binding.scope).isEmpty() ||
             (binding.scope != Scope::global &&
              next.resolve(binding.key, binding.scope) == defaults.resolve(binding.key, Scope::global))))
            next.bindings.push_back(binding);
    bool newPatternScope = true;
    if (v.hasProperty("knownScopes"))
    {
        if (!v["knownScopes"].isArray() || v["knownScopes"].size() > 64)
            return juce::Result::fail("Invalid shortcut scope inventory");
        for (auto value : *v["knownScopes"].getArray())
        {
            if (!value.isInt() || int(value) < 0 || int(value) > int(Scope::sampler))
                return juce::Result::fail("Invalid shortcut scope inventory");
            if (int(value) == int(Scope::patterns))
                newPatternScope = false;
        }
    }
    else
        for (const auto &binding : next.bindings)
            if (binding.scope == Scope::patterns)
                newPatternScope = false;
    if (newPatternScope)
        for (const auto &binding : defaults.bindings)
            if (binding.scope == Scope::patterns && next.resolve(binding.key, binding.scope).isEmpty())
                next.bind(binding.action, binding.scope, binding.key, false);
    bindings = std::move(next.bindings);
    macros = std::move(next.macros);
    restoreMuteAfterSolo = next.restoreMuteAfterSolo;
    muteRemovedSteps = next.muteRemovedSteps;
    return juce::Result::ok();
}
juce::Result Customization::save(const juce::File &file) const
{
    for (const auto &macro : macros)
        for (const auto &step : macro.steps)
            if (auto modes = validateMacroNoteModes(step.action, step.parameters); modes.failed())
                return modes;
    for (const auto &macro : macros)
        for (const auto &step : macro.steps)
            if (auto valid = validateMacroImportedNotes(step.action, step.parameters.recordedNotes);
                valid.failed())
                return valid;
    for (const auto &macro : macros)
        for (const auto &step : macro.steps)
            if (step.action == "plugin.io" || step.parameters.pluginIo)
                return juce::Result::fail(
                    "pluginIo.macroUnsupported: Refusing to serialize unsupported native I/O macro data");
    for (const auto &macro : macros)
        for (const auto &step : macro.steps)
            if (step.parameters.retainSteps && !isStepRetentionCommand(step.action))
                return juce::Result::fail(
                    "stepMemory.invalidArguments: Refusing unsupported macro retention payload");
    const auto encoded = juce::JSON::toString(state());
    if (encoded.getNumBytesAsUTF8() > 1024 * 1024)
        return juce::Result::fail("notesImport.profileLimit: Settings must remain loadable within1MiB");
    if (file.getParentDirectory().createDirectory().failed())
        return juce::Result::fail("Cannot create settings directory");
    juce::TemporaryFile temporary(file);
    if (!temporary.getFile().replaceWithText(encoded) || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Cannot save shortcuts");
    return juce::Result::ok();
}
juce::Result Customization::load(const juce::File &file)
{
    if (!file.existsAsFile() || file.getSize() > 1024 * 1024)
        return juce::Result::fail("Profile unavailable or too large");
    return importState(juce::JSON::parse(file));
}
} // namespace vibera
