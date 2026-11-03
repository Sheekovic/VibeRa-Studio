// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Commands.h"
#include "../src/MixerRouting.h"
#include <cstring>
#include <stdexcept>

// Original absent-provider MODEL/history proof only. Native capture, workers,
// graph activation, recording/input continuity and application jobs are UNRUN.
namespace vibera::acceptance::capturedMixerStructural
{
__declspec(noinline) inline juce::Result verify()
{
    try
    {
        const auto check = [](bool value, const char *why)
        {
            if (!value)
                throw std::runtime_error(why);
        };
        const auto bytes = [](const char *text)
        { return juce::MemoryBlock(text, std::strlen(text)).toBase64Encoding(); };
        juce::PluginDescription effect;
        effect.name = "Original absent captured structural effect";
        effect.pluginFormatName = "VST3";
        effect.fileOrIdentifier = "original-absent-structural-effect.vst3";
        effect.numInputChannels = effect.numOutputChannels = 2;
        auto synth = effect;
        synth.name = "Original absent captured structural instrument";
        synth.isInstrument = true;
        auto original = Project::empty();
        const auto a = original.allocate(), b = original.allocate(), instrument = original.allocate();
        original.mixer[1].effects = {
            {a, effect.name, effect.createXml()->toString(), bytes("old-A"), false, true},
            {b, effect.name, effect.createXml()->toString(), bytes("old-B"), false, false}};
        original.mixer[1].effects[0].wet = .375f;
        original.channels.front().instrument = PluginSpec{
            instrument, synth.name, synth.createXml()->toString(), bytes("old-synth"), true, false};
        std::swap(original.mixer[0], original.mixer[4]); // Master is not vector0.
        check(original.validate().wasOk(), "Original complete model is valid");
        const auto refresh = [&](Project &p, const char *value)
        {
            for (const auto id : {a, b, instrument})
                if (auto *slot = p.plugin(id))
                    slot->state = bytes(value);
        };
        const auto snapshot = [](const Controller &owner)
        {
            return owner.project.state().toXmlString() + owner.projectIdentity +
                   juce::String(juce::int64(owner.revision)) + juce::String(int(owner.dirty)) +
                   owner.undo.getUndoDescriptions().joinIntoString("|") +
                   owner.undo.getRedoDescriptions().joinIntoString("|") +
                   juce::String(owner.undo.getNumberOfUnitsTakenUpByStoredCommands());
        };
        const auto commit = [&](Controller &owner, std::unique_ptr<PreparedControllerChange> &ticket)
        {
            check(ticket && owner.validatePreparedChange(*ticket).wasOk() &&
                      owner.commitPreparedChange(*ticket),
                  "Same actual canonical ticket validates and commits");
        };
        const char *commands[] = {"channel.mixer",        "mixer.route",   "mixer.routeOnly",   "mixer.send",
                                  "plugin.effect.insert", "plugin.remove", "plugin.effect.move"};
        for (int mode = 0; mode < 7; ++mode)
        {
            auto owner = std::make_unique<Controller>();
            owner->replace(original);
            owner->deliverPendingNotifications();
            int calls = 0;
            owner->applicationCommand = [&](const juce::String &, const CommandArgs &,
                                            const juce::String &) -> std::optional<juce::Result>
            {
                ++calls;
                return juce::Result::fail("Unexpected application callback");
            };
            auto captured = owner->project;
            refresh(captured, "ack-one");
            auto expected = captured;
            CommandArgs args;
            args.channel = original.channels.front().id;
            args.mixer = 1;
            args.target = 2;
            if (mode == 0)
                expected.channels.front().mixer = 1;
            if (mode == 1)
                const_cast<MixerInsert *>(mixerInsert(expected, 1))->routes.push_back({2, 1});
            if (mode == 2)
                const_cast<MixerInsert *>(mixerInsert(expected, 1))->routes = {{2, 1}};
            if (mode == 3)
            {
                args.value = .625;
                const_cast<MixerInsert *>(mixerInsert(expected, 1))->routes.push_back({2, .625f});
            }
            if (mode == 4)
            {
                args.text = effect.createXml()->toString();
                const auto id = expected.allocate();
                const_cast<MixerInsert *>(mixerInsert(expected, 1))
                    ->effects.push_back({id, effect.name, args.text, {}, false, false});
            }
            if (mode == 5)
            {
                args.target = a;
                const_cast<MixerInsert *>(mixerInsert(expected, 1))
                    ->effects.erase(const_cast<MixerInsert *>(mixerInsert(expected, 1))->effects.begin());
            }
            if (mode == 6)
            {
                args.target = a;
                args.lane = 1;
                auto &slots = const_cast<MixerInsert *>(mixerInsert(expected, 1))->effects;
                std::swap(slots[0], slots[1]);
            }
            const auto before = snapshot(*owner);
            std::unique_ptr<PreparedControllerChange> ticket;
            if (mode == 3)
            {
                check(owner->prepareCapturedMixerCommand(commands[mode], args, captured, ticket).failed() &&
                          !ticket && snapshot(*owner) == before && calls == 0,
                      "Missing send refuses: enable mixer.route first; mixer.send remains level-only");
                continue;
            }
            check(owner->prepareCapturedMixerCommand(commands[mode], args, captured, ticket).wasOk() &&
                      ticket && ticket->destinationBeforeCommit()->state().isEquivalentTo(expected.state()) &&
                      snapshot(*owner) == before && calls == 0,
                  "Each closed command prepares literal expected destination without application/model "
                  "mutation");
            commit(*owner, ticket);
            check(owner->project.state().isEquivalentTo(expected.state()) && owner->undo.canUndo(),
                  "One actual owned history action publishes complete capture and structural mutation");
            auto fresh = owner->project;
            refresh(fresh, "ack-two");
            std::unique_ptr<PreparedControllerChange> inverse;
            const auto undo =
                mode < 4 ? owner->prepareCapturedMixerRoutingHistory(HistoryDirection::undo, fresh, inverse)
                         : owner->prepareMixerEffectHistory(HistoryDirection::undo, fresh, inverse);
            auto undoExpected = original;
            for (const auto id : {a, b, instrument})
                if (const auto *present = fresh.plugin(id))
                    undoExpected.plugin(id)->state = present->state;
                else
                    undoExpected.plugin(id)->state = bytes("ack-one");
            check(undo.wasOk() && inverse &&
                      inverse->destinationBeforeCommit()->state().isEquivalentTo(undoExpected.state()),
                  "Owned Undo refreshes every surviving native owner, retaining removed owner's captured "
                  "bytes");
            commit(*owner, inverse);
            fresh = owner->project;
            refresh(fresh, "ack-three");
            const auto redo =
                mode < 4 ? owner->prepareCapturedMixerRoutingHistory(HistoryDirection::redo, fresh, inverse)
                         : owner->prepareMixerEffectHistory(HistoryDirection::redo, fresh, inverse);
            auto redoExpected = expected;
            for (const auto id : {a, b, instrument})
                if (auto *slot = redoExpected.plugin(id))
                    slot->state = fresh.plugin(id)->state;
            check(redo.wasOk() && inverse &&
                      inverse->destinationBeforeCommit()->state().isEquivalentTo(redoExpected.state()),
                  "Actual Redo uses refreshed states in both endpoints without mutating source early");
            commit(*owner, inverse);
        }
        auto owner = std::make_unique<Controller>();
        owner->replace(original);
        owner->deliverPendingNotifications();
        auto capture = original;
        refresh(capture, "fresh-noop");
        CommandArgs args;
        args.channel = original.channels.front().id;
        args.mixer = 0;
        std::unique_ptr<PreparedControllerChange> ticket;
        const auto before = snapshot(*owner);
        check(owner->prepareCapturedMixerCommand("channel.mixer", args, capture, ticket).wasOk() &&
                  ticket->isNoOp() &&
                  ticket->destinationBeforeCommit()->state().isEquivalentTo(original.state()),
              "True no-op does not turn refreshed opaque state into a history action");
        commit(*owner, ticket);
        check(snapshot(*owner) == before, "No-op commit preserves every model/history/revision field");
        const auto *preserved = ticket.get();
        args.target = instrument;
        check(owner->prepareCapturedMixerCommand("plugin.remove", args, capture, ticket).failed() &&
                  ticket.get() == preserved && snapshot(*owner) == before,
              "Instrument removal refuses atomically with output ownership retained");
        args.mixer = 1;
        args.target = 0;
        check(owner->prepareCapturedMixerCommand("mixer.send", args, capture, ticket).failed() &&
                  ticket.get() == preserved,
              "Existing send levels refuse this structural API");
        check(owner->prepareCapturedMixerCommand("project.tempo", args, capture, ticket).failed() &&
                  ticket.get() == preserved,
              "Foreign command cannot broaden captured preparation");
        auto malformed = capture;
        malformed.tempo += 1;
        check(owner->prepareCapturedMixerCommand("channel.mixer", args, malformed, ticket).failed() &&
                  ticket.get() == preserved && snapshot(*owner) == before,
              "Non-opaque capture difference refuses before canonical preparation");
        auto mixed = owner->project;
        mixed.channels.front().mixer = 2;
        mixed.tempo += 1;
        check(owner
                  ->prepareSnapshotChange(mixed, owner->project, "Original mixed history", true, false, true,
                                          ticket)
                  .wasOk(),
              "Actual mixed owned history prepared");
        commit(*owner, ticket);
        const auto mixedBefore = snapshot(*owner);
        preserved = ticket.get();
        check(owner->prepareCapturedMixerRoutingHistory(HistoryDirection::undo, owner->project, ticket)
                      .failed() &&
                  ticket.get() == preserved && snapshot(*owner) == mixedBefore,
              "Route-plus-musical mixed action cannot obtain route-only history authority");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::capturedMixerStructural
