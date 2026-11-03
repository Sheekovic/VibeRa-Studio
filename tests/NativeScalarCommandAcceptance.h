// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Commands.h"
#include "../src/PluginParameterState.h"
#include <array>
#include <limits>
#include <stdexcept>

// Original pure Controller/actual UndoManager test. Synthetic owned VST3 XML
// overlay data is explicit; no native worker/capture/schema-authority claim.
namespace vibera::acceptance::nativeScalarCommand
{
__declspec(noinline) inline juce::Result verify()
{
    try
    {
        const auto check = [](bool condition, const char *text)
        {
            if (!condition)
                throw std::runtime_error(text);
        };
        const auto encode = [](const juce::XmlElement &xml)
        {
            juce::MemoryBlock binary;
            juce::AudioProcessor::copyXmlToBinary(xml, binary);
            return binary.toBase64Encoding();
        };
        const auto state = [&](float target, float other, float unbound, const char *chunk)
        {
            juce::XmlElement xml("VST3PluginState");
            xml.setAttribute("original", "retained metadata");
            xml.createNewChildElement("IComponent")->addTextElement(chunk);
            auto *values = xml.createNewChildElement("VibeRaParameterValues");
            values->setAttribute("version", 1);
            for (const auto item : {std::pair{17u, target}, std::pair{71u, other}, std::pair{0u, unbound}})
            {
                auto *entry = values->createNewChildElement("PARAM");
                entry->setAttribute("id", juce::String(item.first));
                entry->setAttribute("value", double(item.second));
            }
            return encode(xml);
        };
        const auto decode = [&](const juce::String &encoded)
        {
            juce::MemoryBlock binary;
            check(decodePluginState(encoded, binary), "Original test envelope decodes");
            auto xml = juce::AudioProcessor::getXmlFromBinary(binary.getData(), int(binary.getSize()));
            check(bool(xml), "Actual JUCE envelope carries XML");
            return xml;
        };
        const auto value = [&](const Project &p, Id id, unsigned parameter)
        {
            const auto xml = decode(p.plugin(id)->state);
            const auto *overlay = xml->getChildByName("VibeRaParameterValues");
            for (auto *entry = overlay->getFirstChildElement(); entry; entry = entry->getNextElement())
                if (entry->getStringAttribute("id") == juce::String(parameter))
                    return float(entry->getDoubleAttribute("value"));
            throw std::runtime_error("Original expected scalar missing");
        };
        juce::PluginDescription descriptor;
        descriptor.name = "Original absent scalar model fixture";
        descriptor.pluginFormatName = "VST3";
        descriptor.fileOrIdentifier = "original-scalar-model.vst3";
        descriptor.numInputChannels = descriptor.numOutputChannels = 2;
        auto project = Project::empty();
        const auto target = project.allocate(), neighbour = project.allocate();
        project.mixer[1].effects = {{target, "Target", descriptor.createXml()->toString(),
                                     state(.25f, .1f, .125f, "old native chunk")},
                                    {neighbour, "Neighbour", descriptor.createXml()->toString(),
                                     state(.5f, .2f, .25f, "old neighbour")}};
        PluginBinding binding;
        binding.id = 17;
        binding.manual = .6f;
        binding.name = "Existing manual";
        project.plugin(target)->automationBindings = {binding};
        check(project.validate().wasOk(), "Original supported scalar model validates");
        auto owner = std::make_unique<Controller>();
        owner->replace(project);
        owner->deliverPendingNotifications();
        int calls = 0;
        owner->changed = [&] { ++calls; };
        const auto snapshot = [&]
        {
            return owner->project.state().toXmlString() + owner->projectIdentity +
                   juce::String(juce::int64(owner->revision)) + juce::String(int(owner->dirty)) +
                   owner->undo.getUndoDescriptions().joinIntoString("|") +
                   owner->undo.getRedoDescriptions().joinIntoString("|") +
                   juce::String(owner->undo.getNumberOfUnitsTakenUpByStoredCommands());
        };
        const auto commit = [&](std::unique_ptr<PreparedControllerChange> &ticket)
        {
            check(ticket && owner->validatePreparedChange(*ticket).wasOk() &&
                      owner->commitPreparedChange(*ticket),
                  "Actual Controller/UndoManager prepared commit");
            ticket.reset();
        };
        const NativeScalarEdit edit{target, 17, .25f, .75f};
        auto after = owner->project;
        after.plugin(target)->state = state(.75f, .4f, .125f, "fresh AFTER chunk");
        after.plugin(neighbour)->state = state(.5f, .33f, .25f, "fresh neighbour");
        const auto original = snapshot();
        std::unique_ptr<PreparedControllerChange> ticket;
        check(owner->prepareNativeScalarEdit(after, std::span(&edit, 1), ticket).wasOk() &&
                  snapshot() == original &&
                  ticket->destinationBeforeCommit()->plugin(target)->automationBindings.size() == 1 &&
                  ticket->destinationBeforeCommit()->plugin(target)->automationBindings.front().manual ==
                      .75f,
              "Fresh native AFTER plus existing canonical manual prepares without Project/cursor mutation");
        commit(ticket);
        check(owner->isNativeScalarHistory(HistoryDirection::undo) &&
                  value(owner->project, target, 17) == .75f,
              "One actual owned positively tagged scalar Undo action");
        const auto stable = snapshot();
        check(owner->prepareHistoryChange(HistoryDirection::undo, ticket).failed() && snapshot() == stable &&
                  !ticket,
              "Generic history cannot restore stale native opaque snapshots for a scalar action");
        auto freshUndo = owner->project;
        freshUndo.plugin(target)->state = state(.75f, .9f, .375f, "current hidden chunk");
        freshUndo.plugin(neighbour)->state = state(.625f, .8f, .5f, "current neighbour");
        check(owner->prepareNativeScalarHistory(HistoryDirection::undo, freshUndo, ticket).wasOk() &&
                  snapshot() == stable,
              "Owned actual Undo preparation leaves manager cursor/source exact");
        const auto *undoDestination = ticket->destinationBeforeCommit();
        check(
            value(*undoDestination, target, 17) == .25f && value(*undoDestination, target, 71) == .9f &&
                value(*undoDestination, target, 0) == .375f &&
                undoDestination->plugin(target)->automationBindings.front().manual == .6f &&
                undoDestination->plugin(neighbour)->state == freshUndo.plugin(neighbour)->state &&
                decode(undoDestination->plugin(target)->state)
                        ->getChildByName("IComponent")
                        ->getAllSubText() == "current hidden chunk",
            "Undo changes only witnessed scalar/manual, retaining unrelated fresh parameters/chunks/plugins");
        ++owner->revision; // Explicit stale-admission fault injection; no history/model mutation.
        check(owner->validatePreparedChange(*ticket).failed() && !ticket->wasCommitted() &&
                  owner->undo.canUndo(),
              "Revision changes invalidate exact prepared native history without moving cursor");
        ticket.reset();
        check(owner->prepareNativeScalarHistory(HistoryDirection::undo, freshUndo, ticket).wasOk(),
              "Fresh retry prepares current cursor");
        commit(ticket);
        check(owner->isNativeScalarHistory(HistoryDirection::redo) && !owner->undo.canUndo(),
              "Undo moves exact actual cursor once");
        auto freshRedo = owner->project;
        freshRedo.plugin(target)->state = state(.25f, .125f, .625f, "new current Redo chunk");
        check(owner->prepareNativeScalarHistory(HistoryDirection::redo, freshRedo, ticket).wasOk() &&
                  value(*ticket->destinationBeforeCommit(), target, 17) == .75f &&
                  value(*ticket->destinationBeforeCommit(), target, 71) == .125f &&
                  value(*ticket->destinationBeforeCommit(), target, 0) == .625f &&
                  ticket->destinationBeforeCommit()->plugin(target)->automationBindings.front().manual ==
                      .75f,
              "Redo reconstructs from NEW current state rather than historical unrelated values");
        commit(ticket);
        auto mismatch = owner->project;
        mismatch.plugin(target)->state = state(.5f, .1f, .2f, "mismatch");
        const auto beforeMismatch = snapshot();
        check(owner->prepareNativeScalarHistory(HistoryDirection::undo, mismatch, ticket).failed() &&
                  !ticket && snapshot() == beforeMismatch,
              "Current native target mismatch refuses with exact model/history/output retained");

        // First-unbound zero native ID, multi-scalar same-plugin action.
        auto batch = owner->project;
        batch.plugin(target)->state = state(.875f, .7f, .875f, "batch current");
        const std::array<NativeScalarEdit, 2> edits{{{target, 17, .75f, .875f}, {target, 0, .625f, .875f}}};
        check(owner->prepareNativeScalarEdit(batch, edits, ticket).wasOk(),
              "Prepare real bounded multi-scalar model action");
        commit(ticket);
        check(owner->project.plugin(target)->automationBindings.size() == 1 &&
                  owner->project.plugin(target)->automationBindings.front().id == 17,
              "First-unbound native ID0 stays unbound; no automation binding is manufactured");
        auto batchUndo = owner->project;
        batchUndo.plugin(target)->state = state(.875f, .2f, .875f, "fresh batch Undo");
        check(owner->prepareNativeScalarHistory(HistoryDirection::undo, batchUndo, ticket).wasOk() &&
                  value(*ticket->destinationBeforeCommit(), target, 17) == .75f &&
                  value(*ticket->destinationBeforeCommit(), target, 0) == .625f &&
                  value(*ticket->destinationBeforeCommit(), target, 71) == .2f &&
                  ticket->destinationBeforeCommit()->plugin(target)->automationBindings.size() == 1,
              "Actual batch Undo reverts exactly two scalars and preserves fresh unrelated scalar");
        ticket.reset();

        // Every refusal keeps an already owned output ticket intact too.
        check(owner
                  ->prepareSnapshotChange(owner->project, owner->project, {}, false, false, owner->dirty,
                                          ticket)
                  .wasOk(),
              "Own output sentinel before negative admissions");
        const auto *sentinel = ticket.get();
        const auto refuses =
            [&](const Project &captured, std::span<const NativeScalarEdit> values, const char *text)
        {
            const auto source = snapshot();
            check(owner->prepareNativeScalarEdit(captured, values, ticket).failed() &&
                      ticket.get() == sentinel && snapshot() == source,
                  text);
        };
        NativeScalarEdit bad{target, 17, .875f, .25f};
        auto goodAfter = owner->project;
        goodAfter.plugin(target)->state = state(.25f, .3f, .875f, "negative source");
        bad.before = std::numeric_limits<float>::quiet_NaN();
        refuses(goodAfter, std::span(&bad, 1), "NaN witness refused");
        bad = {target, 17, .875f, 1.01f};
        refuses(goodAfter, std::span(&bad, 1), "Out-of-range normalized witness refused");
        bad = {target, 17, .875f, .25f, .1f};
        refuses(goodAfter, std::span(&bad, 1), "Foreign existing-manual witness refused");
        bad = {target, 0, .875f, .25f, .5f};
        refuses(goodAfter, std::span(&bad, 1), "Unbound parameter cannot claim a manual binding");
        bad = {target, 999, .875f, .25f};
        refuses(goodAfter, std::span(&bad, 1), "Unknown overlay native parameter refused");
        bad = {99999999, 17, .875f, .25f};
        refuses(goodAfter, std::span(&bad, 1), "Unknown plugin target refused");
        bad = {target, 17, .875f, .25f};
        const std::array<NativeScalarEdit, 2> duplicates{bad, bad};
        refuses(goodAfter, duplicates, "Duplicate same native scalar refused");
        std::vector<NativeScalarEdit> tooMany(nativeScalarHistoryMaxEdits + 1, bad);
        refuses(goodAfter, tooMany, "Explicit bounded delta capacity refuses before history ownership");
        refuses(goodAfter, {}, "Empty native scalar operation refused");
        auto malformed = goodAfter;
        malformed.plugin(target)->state = "not an encoded state";
        refuses(malformed, std::span(&bad, 1), "Malformed fresh state refused");
        auto schema = goodAfter;
        auto changedXml = decode(schema.plugin(target)->state);
        changedXml->getChildByName("VibeRaParameterValues")->setAttribute("version", 2);
        schema.plugin(target)->state = encode(*changedXml);
        refuses(schema, std::span(&bad, 1), "Unsupported scalar overlay schema refused");
        auto duplicateXml = decode(goodAfter.plugin(target)->state);
        auto *overlay = duplicateXml->getChildByName("VibeRaParameterValues");
        overlay->addChildElement(new juce::XmlElement(*overlay->getChildByName("PARAM")));
        schema.plugin(target)->state = encode(*duplicateXml);
        refuses(schema, std::span(&bad, 1), "Duplicate original overlay ID refused");
        auto stale = goodAfter;
        stale.tempo += 1;
        refuses(stale, std::span(&bad, 1), "Fresh native capture cannot change canonical music/model fields");
        auto wrongValue = goodAfter;
        wrongValue.plugin(target)->state = state(.5f, .3f, .875f, "bad AFTER");
        refuses(wrongValue, std::span(&bad, 1), "Fresh acknowledged AFTER mismatch refused");
        const auto source = snapshot();
        auto mixed = goodAfter;
        mixed.channels.front().gain *= .5f;
        check(owner->prepareSnapshotChange(mixed, owner->project, "forged mixed action", true, false, true,
                                           ticket, std::span(&bad, 1))
                      .failed() &&
                  ticket.get() == sentinel && snapshot() == source,
              "Generic snapshot cannot forge scalar tag for mixed music/hidden-state differences");
        check(
            !owner->isNativeScalarHistory(static_cast<HistoryDirection>(999)) &&
                owner->prepareNativeScalarHistory(static_cast<HistoryDirection>(999), owner->project, ticket)
                    .failed() &&
                ticket.get() == sentinel && !calls,
            "Invalid direction refuses; preparations/commits deliver no implicit application observers");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::nativeScalarCommand
