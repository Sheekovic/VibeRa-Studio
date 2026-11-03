// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Commands.h"
#include "../src/MixerRouting.h"
#include <cstring>
#include <stdexcept>

// Original absent-provider model/history proof only. No worker, native state
// acknowledgement, audio reconfiguration, runtime or listening qualification.
namespace vibera::acceptance::mixerEffectReplacement
{
__declspec(noinline) inline juce::Result verify()
{
    try
    {
        const auto check = [](bool ok, const char *why)
        {
            if (!ok)
                throw std::runtime_error(why);
        };
        const auto bytes = [](const char *s)
        { return juce::MemoryBlock(s, std::strlen(s)).toBase64Encoding(); };
        juce::PluginDescription oldType;
        oldType.name = "Original absent old effect";
        oldType.pluginFormatName = "VST3";
        oldType.fileOrIdentifier = "original-absent-replace-old.vst3";
        oldType.numInputChannels = oldType.numOutputChannels = 2;
        auto newType = oldType;
        newType.name = "Original absent replacement";
        newType.fileOrIdentifier = "original-absent-replace-new.vst3";
        auto project = Project::empty();
        const auto left = project.allocate(), target = project.allocate(), right = project.allocate();
        project.mixer[1].effects = {
            {left, "Left", oldType.createXml()->toString(), bytes("left-state"), false, false},
            {target, "Target", oldType.createXml()->toString(), bytes("target-state"), false, true},
            {right, "Right", oldType.createXml()->toString(), bytes("right-state"), false, false}};
        project.mixer[1].effects[1].wet = .375f;
        project.mixer[1].effectsBypassed = true;
        project.plugin(left)->automationBindings.push_back({7, 1, 0, .5f, "Retained", "unit"});
        project.automation.initialValues.push_back({{AutomationParameter::pluginNormalized, left, 7}, .5f});
        project.plugin(target)->automationBindings.push_back({19, 1, 0, .25f, "Old-only", "unit"});
        std::swap(project.mixer.front(), project.mixer[4]);
        auto owner = std::make_unique<Controller>();
        owner->replace(project);
        int observers = 0, applications = 0;
        owner->changed = [&] { ++observers; };
        owner->applicationCommand = [&](const juce::String &, const CommandArgs &,
                                        const juce::String &) -> std::optional<juce::Result>
        {
            ++applications;
            return juce::Result::fail("unexpected application hook");
        };
        const auto snapshot = [&]
        {
            return owner->project.state().toXmlString() + owner->projectIdentity +
                   juce::String(juce::int64(owner->revision)) + juce::String(int(owner->dirty)) +
                   owner->undo.getUndoDescriptions().joinIntoString("|") +
                   owner->undo.getRedoDescriptions().joinIntoString("|");
        };
        const auto commit = [&](std::unique_ptr<PreparedControllerChange> &ticket)
        {
            check(owner->validatePreparedChange(*ticket).wasOk() && owner->commitPreparedChange(*ticket),
                  "Real canonical manager commit");
        };
        auto capture = owner->project;
        capture.plugin(target)->state = bytes("fresh-target");
        capture.plugin(left)->state = bytes("fresh-left");
        const auto before = snapshot();
        std::unique_ptr<PreparedControllerChange> discarded;
        check(
            owner->prepareMixerEffectReplacement(capture, target, newType.createXml()->toString(), discarded)
                .wasOk(),
            "Prepare a true middle-slot replacement");
        const auto abandoned = mixerInsert(*discarded->destinationBeforeCommit(), 1)->effects[1].id;
        discarded.reset();
        std::unique_ptr<PreparedControllerChange> prepared;
        check(owner->prepareMixerEffectReplacement(capture, target, newType.createXml()->toString(), prepared)
                  .wasOk(),
              "Retry after a cancelled/discarded native candidate");
        const auto destination = *prepared->destinationBeforeCommit();
        const auto id = mixerInsert(destination, 1)->effects[1].id;
        auto expected = capture;
        auto *slot = expected.plugin(target);
        *slot = {id, newType.name, newType.createXml()->toString(), {}, false, true};
        slot->wet = .375f;
        expected.nextId = id + 1;
        check(id == abandoned + 1 && destination.state().isEquivalentTo(expected.state()) &&
                  snapshot() == before && !owner->undo.canUndo() && !observers && !applications &&
                  !prepared->nativeEffectHistoryBeforeCommit(),
              "Literal full-project oracle preserves every other "
              "slot/ID/state/binding/automation/route/flag; fresh ID, no invented state");
        auto *const originalOutput = prepared.get();
        auto badCapture = capture;
        badCapture.plugin(right)->wet = .25f;
        check(owner->prepareMixerEffectReplacement(badCapture, target, newType.createXml()->toString(),
                                                   prepared)
                      .failed() &&
                  prepared.get() == originalOutput && snapshot() == before,
              "Only opaque source-state refresh is admitted and refusal retains previous ticket");
        for (const auto &bad : {juce::String{}, juce::String("invalid XML")})
            check(owner->prepareMixerEffectReplacement(capture, target, bad, prepared).failed() &&
                      prepared.get() == originalOutput && snapshot() == before,
                  "Malformed replacement is atomic");
        auto instrument = newType;
        instrument.isInstrument = true;
        check(owner->prepareMixerEffectReplacement(capture, target, instrument.createXml()->toString(),
                                                   prepared)
                      .failed() &&
                  owner
                      ->prepareMixerEffectReplacement(capture, project.channels.front().id,
                                                      newType.createXml()->toString(), prepared)
                      .failed() &&
                  prepared.get() == originalOutput && snapshot() == before,
              "Instrument descriptors and non-effect stable targets refuse without changes");
        commit(prepared);
        check(owner->undo.getUndoDescriptions().size() == 1 && !observers && !applications,
              "Exactly one real history action; preparation and commit deliver no observer or application "
              "hook");
        auto undoCapture = owner->project;
        undoCapture.plugin(id)->state = bytes("replacement-edited-live");
        undoCapture.plugin(left)->state = bytes("left-at-Undo");
        std::unique_ptr<PreparedControllerChange> undo;
        check(
            owner->prepareMixerEffectHistory(HistoryDirection::undo, undoCapture, undo).wasOk() &&
                undo->destinationBeforeCommit()->plugin(target)->state == bytes("fresh-target") &&
                undo->destinationBeforeCommit()->plugin(left)->state == bytes("left-at-Undo"),
            "Undo restores old target's captured native bytes while retaining current unaffected slot state");
        commit(undo);
        auto redoCapture = owner->project;
        redoCapture.plugin(target)->state = bytes("old-edited-after-Undo");
        std::unique_ptr<PreparedControllerChange> redo;
        check(owner->prepareMixerEffectHistory(HistoryDirection::redo, redoCapture, redo).wasOk() &&
                  redo->destinationBeforeCommit()->plugin(id)->state == bytes("replacement-edited-live"),
              "Redo recovers exact fresh captured replacement bytes and original replacement ID");
        commit(redo);
        check(owner->prepareMixerEffectHistory(HistoryDirection::undo, owner->project, undo).wasOk() &&
                  undo->destinationBeforeCommit()->plugin(target)->state == bytes("old-edited-after-Undo"),
              "Fresh native bytes live in actual immutable opposite history endpoint");
        commit(undo);
        check(owner->prepareMixerEffectReplacement(owner->project, target, newType.createXml()->toString(),
                                                   prepared)
                      .wasOk() &&
                  mixerInsert(*prepared->destinationBeforeCommit(), 1)->effects[1].id > id,
              "Abandoned Redo branch cannot recycle prior replacement IDs");
        owner->project.plugin(right)->state = bytes("late-current-source");
        check(owner->validatePreparedChange(*prepared).failed() && !owner->commitPreparedChange(*prepared),
              "Current canonical opaque drift invalidates publication without a musical revision change");
        auto other = std::make_unique<Controller>();
        for (int mode : {0, 1})
        {
            auto referenced = project;
            const AutomationKey key{AutomationParameter::pluginNormalized, target, 19};
            if (mode == 0)
                referenced.automation.initialValues.push_back({key, .25f});
            else
            {
                AutomationSource curve;
                curve.id = referenced.allocate();
                curve.points = {{referenced.allocate(), 0, .25f}, {referenced.allocate(), 4, .75f}};
                curve.links.push_back({key, 0, 1});
                referenced.automation.sources.push_back(curve);
            }
            check(referenced.validate().wasOk(), "Genuine initialized/linked old target reference fixture");
            other->replace(referenced);
            check(other->prepareMixerEffectReplacement(referenced, target, newType.createXml()->toString(),
                                                       redo)
                          .failed() &&
                      other->project.state().isEquivalentTo(referenced.state()) && !other->undo.canUndo(),
                  "Target native initialization or source links refuse; unrelated target links were "
                  "preserved in positive case");
        }
        other->replace(project);
        CommandArgs args;
        args.target = target;
        args.text = newType.createXml()->toString();
        check(other->execute("plugin.effect.replace", args).wasOk() &&
                  mixerInsert(other->project, 1)->effects[0] == *project.plugin(left) &&
                  mixerInsert(other->project, 1)->effects[2] == *project.plugin(right) &&
                  mixerInsert(other->project, 1)->effects[1].descriptor == args.text,
              "Direct canonical MODEL command uses the same slot mutator; no separate replacement algorithm");
        check(other->beginGesture("owned"), "Actual owned gesture starts");
        const auto gestureProject = other->project.state();
        check(other->prepareMixerEffectReplacement(
                       other->project, mixerInsert(other->project, 1)->effects[1].id, args.text, redo)
                      .failed() &&
                  other->execute("plugin.effect.replace", args).failed() && other->hasGesture() &&
                  other->gestureOwner() == "owned" && other->project.state().isEquivalentTo(gestureProject),
              "Prepared replacement never commits/cancels/takes over an existing gesture");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::mixerEffectReplacement
