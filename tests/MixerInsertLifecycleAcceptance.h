// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Commands.h"
#include "../src/MixerInsertLifecycle.h"
#include <cstring>
#include <stdexcept>

// Original pure MODEL/prepared-history proof. No native variable-universe,
// runtime sidechain, callback/voice/tail/physical device or provider claim.
namespace vibera::acceptance::mixerInsertLifecycle
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
        auto owner = std::make_unique<Controller>();
        auto original = Project::empty();
        check(original.mixer.size() == 18, "Original18 default inserts retained");
        original.mixer[5].muted = true; // Independent manual mute must survive membership edits.
        std::swap(original.mixer.front(), original.mixer[4]);
        owner->replace(original);
        int callbacks = 0;
        owner->changed = [&] { ++callbacks; };
        owner->applicationCommand = [&](const juce::String &, const CommandArgs &,
                                        const juce::String &) -> std::optional<juce::Result>
        {
            ++callbacks;
            return juce::Result::fail("unexpected app callback");
        };
        const auto commit = [&](std::unique_ptr<PreparedControllerChange> &ticket)
        {
            check(owner->validatePreparedChange(*ticket).wasOk() && owner->commitPreparedChange(*ticket),
                  "Actual prepared canonical commit");
        };
        CommandArgs create;
        create.text = "Original new bus";
        std::unique_ptr<PreparedControllerChange> ticket;
        check(owner->prepareMixerInsertCommand("mixer.insert.create", create, owner->project, ticket).wasOk(),
              "Prepare create");
        const int abandoned = ticket->destinationBeforeCommit()->mixer.back().index;
        ticket.reset();
        check(owner->prepareMixerInsertCommand("mixer.insert.create", create, owner->project, ticket).wasOk(),
              "Retry create after cancellation");
        const int created = ticket->destinationBeforeCommit()->mixer.back().index;
        auto expected = original;
        expected.mixer.emplace_back(created, create.text, 1.f, 0.f, false);
        expected.nextId = created + 1;
        check(created == abandoned + 1 &&
                  ticket->destinationBeforeCommit()->state().isEquivalentTo(expected.state()) &&
                  owner->project.state().isEquivalentTo(original.state()) && !owner->undo.canUndo() &&
                  !callbacks,
              "Fresh monotonic session ID; neutral Master route, exact other18 row values and nonleading "
              "Master unchanged");
        commit(ticket);
        check(owner->undo.getUndoDescriptions().size() == 1 && !callbacks,
              "One real create history action without observers");
        check(owner->prepareMixerInsertHistory(HistoryDirection::undo, owner->project, ticket).wasOk(),
              "Actual create Undo prepared");
        commit(ticket);
        check(owner->project.state().isEquivalentTo(original.state()),
              "Create Undo restores exact original model and allocator snapshot");
        check(owner->prepareMixerInsertHistory(HistoryDirection::redo, owner->project, ticket).wasOk(),
              "Actual create Redo prepared");
        commit(ticket);
        check(owner->project.state().isEquivalentTo(expected.state()),
              "Create Redo retains original allocated ID");
        check(owner->prepareMixerInsertHistory(HistoryDirection::undo, owner->project, ticket).wasOk(),
              "Prepare abandoned Redo branch");
        commit(ticket);
        check(
            owner->prepareMixerInsertCommand("mixer.insert.create", create, owner->project, ticket).wasOk() &&
                ticket->destinationBeforeCommit()->mixer.back().index > created,
            "Undo branch never recycles insert IDs in this session");
        juce::PluginDescription type;
        type.name = "Original absent delete effect";
        type.pluginFormatName = "VST3";
        type.fileOrIdentifier = "original-absent-insert-delete.vst3";
        type.numInputChannels = type.numOutputChannels = 2;
        auto referenced = original;
        const auto effect = referenced.allocate(), keepEffect = referenced.allocate();
        for (auto &row : referenced.mixer)
        {
            if (row.index == 1)
                row.effects = {
                    {effect, "Removed", type.createXml()->toString(), bytes("old-removed"), false, true}};
            if (row.index == 2)
                row.routes = {{1, .375f}};
            if (row.index == 3)
                row.effects = {{keepEffect, "Retained", type.createXml()->toString(), bytes("old-retained"),
                                false, false}};
        }
        referenced.channels.front().mixer = 1;
        referenced.mixerGroups.push_back({juce::Uuid().toString(), "Original group", 0, {1, 3}});
        referenced.plugin(effect)->automationBindings.push_back({7, 1, 0, .25f, "Removed control", "unit"});
        const AutomationKey gone{AutomationParameter::pluginNormalized, effect, 7};
        const AutomationKey retained{AutomationParameter::mixerPan, 3, 0};
        const AutomationKey goneMixer{AutomationParameter::mixerStereoSeparation, 1, 0};
        referenced.automation.initialValues = {{gone, .25f}, {goneMixer, .5f}, {retained, 0}};
        AutomationSource curve;
        curve.id = referenced.allocate();
        curve.points = {{referenced.allocate(), 0, .25f}, {referenced.allocate(), 4, .75f}};
        curve.links = {{gone, 0, 1}, {goneMixer, -1, 1}, {retained, -1, 1}};
        referenced.automation.sources.push_back(curve);
        check(referenced.validate().wasOk(), "Genuine assigned/send/group/native automation delete fixture");
        owner->applicationCommand = {};
        owner->changed = {};
        owner->replace(referenced);
        auto capture = owner->project;
        capture.plugin(effect)->state = bytes("fresh-removed");
        capture.plugin(keepEffect)->state = bytes("fresh-retained");
        CommandArgs remove;
        remove.mixer = 1;
        MixerInsertDeletePolicy full;
        full.replacement = 3;
        full.removeEffects = full.removeAutomation = full.removeGroupMembership = true;
        remove.mixerDelete = full;
        const auto beforeFailure = owner->project.state();
        for (int mode : {0, 1, 2, 3})
        {
            auto args = remove;
            if (mode == 0)
                args.mixerDelete->removeEffects = false;
            if (mode == 1)
                args.mixerDelete->removeAutomation = false;
            if (mode == 2)
                args.mixerDelete->removeGroupMembership = false;
            if (mode == 3)
                args.mixerDelete->replacement.reset();
            auto *const prior = ticket.get();
            check(owner->prepareMixerInsertCommand("mixer.insert.delete", args, capture, ticket).failed() &&
                      ticket.get() == prior && owner->project.state().isEquivalentTo(beforeFailure) &&
                      !owner->undo.canUndo(),
                  "Every dependent deletion decision is explicit and refusal preserves exact old "
                  "output/history/model");
        }
        check(owner->prepareMixerInsertCommand("mixer.insert.delete", remove, capture, ticket).wasOk(),
              "Explicit safe referenced deletion prepares");
        auto deleted = capture;
        deleted.channels.front().mixer = 3;
        for (auto &row : deleted.mixer)
            if (row.index == 2)
                row.routes = {{3, .375f}};
        std::erase_if(deleted.mixer, [](const auto &row) { return row.index == 1; });
        deleted.mixerGroups.front().members = {3};
        deleted.automation.sources.front().links = {{retained, -1, 1}};
        deleted.automation.initialValues = {{retained, 0}};
        check(ticket->destinationBeforeCommit()->state().isEquivalentTo(deleted.state()),
              "Literal full-project cascade preserves incoming level, curves/points, unrelated "
              "links/FX/groups, and all other document state");
        commit(ticket);
        check(owner->undo.getUndoDescriptions().size() == 1, "Delete is one actual history transaction");
        auto undoCapture = owner->project;
        undoCapture.plugin(keepEffect)->state = bytes("retained-at-Undo");
        check(owner->prepareMixerInsertHistory(HistoryDirection::undo, undoCapture, ticket).wasOk() &&
                  ticket->destinationBeforeCommit()->plugin(effect)->state == bytes("fresh-removed") &&
                  ticket->destinationBeforeCommit()->plugin(keepEffect)->state == bytes("retained-at-Undo"),
              "Delete Undo restores captured removed state/references and retains current unrelated native "
              "bytes");
        commit(ticket);
        auto redoCapture = owner->project;
        redoCapture.plugin(effect)->state = bytes("removed-edited-after-Undo");
        check(owner->prepareMixerInsertHistory(HistoryDirection::redo, redoCapture, ticket).wasOk(),
              "Fresh captured delete Redo");
        commit(ticket);
        check(owner->prepareMixerInsertHistory(HistoryDirection::undo, owner->project, ticket).wasOk() &&
                  ticket->destinationBeforeCommit()->plugin(effect)->state ==
                      bytes("removed-edited-after-Undo"),
              "Actual opposite history edge owns newer removed native state");
        commit(ticket);
        Project reopened;
        check(Project::parse(owner->project.state(), reopened).wasOk() &&
                  reopened.state().isEquivalentTo(owner->project.state()) &&
                  reopened.version == Project::currentVersion,
              "Existing schema exact sparse-ID/group/effect/reference serialization roundtrip");
        CommandArgs master = remove;
        master.mixer = 0;
        check(
            owner->prepareMixerInsertCommand("mixer.insert.delete", master, owner->project, ticket).failed(),
            "Master cannot be deleted");
        for (int mode : {0, 1, 2})
        {
            auto collision = referenced;
            if (mode == 0)
                for (auto &row : collision.mixer)
                    if (row.index == 2)
                        row.routes.push_back({3, .5f});
            if (mode == 1)
                remove.mixerDelete->replacement = 2; // Incoming source would point to itself.
            if (mode == 2)
                for (auto &row : collision.mixer)
                    if (row.index == 3)
                        row.routes = {{2, .5f}};
            owner->replace(collision);
            check(
                owner->prepareMixerInsertCommand("mixer.insert.delete", remove, collision, ticket).failed() &&
                    owner->project.state().isEquivalentTo(collision.state()) && !owner->undo.canUndo(),
                "Duplicate/self/cycle target collisions fail rather than merge levels or partially redirect "
                "Channels");
            remove.mixerDelete = full;
        }
        auto mixed = original;
        mixed.tempo += 1;
        mixed.mixer.emplace_back(18, "Mixed", 1, 0, false);
        mixed.nextId = 19;
        owner->replace(original);
        check(
            owner->prepareSnapshotChange(mixed, original, "Mixed create", true, false, true, ticket).wasOk(),
            "Real mixed history fixture");
        commit(ticket);
        check(owner->prepareMixerInsertHistory(HistoryDirection::undo, owner->project, ticket).failed(),
              "Mixed tempo/create cannot masquerade as pure insert history");
        auto capacity = original;
        for (int id = 18; id <= 128; ++id)
            capacity.mixer.emplace_back(id, "Bounded", 1, 0, false);
        capacity.nextId = 129;
        owner->replace(capacity);
        check(
            capacity.validate().wasOk() &&
                owner->prepareMixerInsertCommand("mixer.insert.create", create, capacity, ticket).failed() &&
                owner->project.mixer.size() == 129 && !owner->undo.canUndo(),
            "Actual129 native/model capacity refuses without partial create");
        auto sparse = original;
        for (auto &row : sparse.mixer)
            if (row.index == 17)
                row.index = 500;
        owner->replace(sparse);
        check(owner->prepareMixerInsertCommand("mixer.insert.create", create, sparse, ticket).wasOk() &&
                  ticket->destinationBeforeCommit()->mixer.back().index == 501 &&
                  mixerInsert(*ticket->destinationBeforeCommit(), 500),
              "New identity follows actual sparse stable universe, never display count or a missing low ID");
        owner->replace(original);
        CommandArgs solo;
        solo.mixer = 1;
        check(owner->execute("mixer.solo", solo).wasOk(), "Actual Mixer solo fixture");
        const auto soloBefore = owner->project.state();
        check(owner->prepareMixerInsertCommand("mixer.insert.create", create, owner->project, ticket)
                      .failed() &&
                  owner->prepareMixerInsertCommand("mixer.insert.delete", remove, owner->project, ticket)
                      .failed() &&
                  owner->project.state().isEquivalentTo(soloBefore),
              "Active solo baseline is never implicitly rewritten by membership changes");
        owner->replace(original);
        check(owner->beginGesture("owned") &&
                  owner->prepareMixerInsertCommand("mixer.insert.create", create, owner->project, ticket)
                      .failed() &&
                  owner->hasGesture() && owner->gestureOwner() == "owned",
              "Membership preparation never takes over active gesture");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::mixerInsertLifecycle
