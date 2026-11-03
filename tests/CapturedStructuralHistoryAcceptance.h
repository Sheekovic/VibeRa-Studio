// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Commands.h"
#include <cstring>
#include <algorithm>
#include <stdexcept>

// Original actual Controller/UndoManager model test. Absent-provider descriptors
// and opaque bytes are authored data, not a native capture or worker proof.
namespace vibera::acceptance::capturedStructuralHistory
{
__declspec(noinline) inline juce::Result verify()
{
    try
    {
        const auto check = [](bool value, const char *message)
        {
            if (!value)
                throw std::runtime_error(message);
        };
        const auto state = [](const char *text)
        { return juce::MemoryBlock(text, std::strlen(text)).toBase64Encoding(); };
        juce::PluginDescription type;
        type.name = "Original structural history instrument";
        type.pluginFormatName = "VST3";
        type.fileOrIdentifier = "original-absent-structural-history.vst3";
        type.isInstrument = true;
        type.numOutputChannels = 2;
        auto project = Project::empty();
        const auto background = project.allocate();
        project.channels[0].instrument =
            PluginSpec{background, "Background", type.createXml()->toString(), state("old background"), true};
        auto nonNative = project;
        nonNative.tempo = 133;
        check(!pluginNativeTopologyDifference(project, nonNative),
              "Tempo Undo does not select plugin topology adapter");
        nonNative = project;
        nonNative.mixer[1].gain = .5f;
        check(!pluginNativeTopologyDifference(project, nonNative), "Control Undo stays ordinary");
        nonNative = project;
        nonNative.channels[1].source = "builtin:snare";
        check(!pluginNativeTopologyDifference(project, nonNative),
              "Builtin source edit does not select plugin adapter");
        nonNative = project;
        nonNative.patterns[0].notes.push_back(
            {nonNative.allocate(), nonNative.channels[0].id, 1, .25, 60, .5f});
        check(!pluginNativeTopologyDifference(project, nonNative),
              "Ordinary note history does not select plugin adapter");
        nonNative = project;
        nonNative.channels[1].audioClip = true;
        nonNative.channels[1].source = "original-absent-clip.wav";
        nonNative.channels[1].audioDuration = 10;
        const auto beforePlacement = nonNative;
        nonNative.audioPlacements.push_back({nonNative.allocate(), nonNative.channels[1].id, 0, 0, 0, 2});
        check(beforePlacement.validate().wasOk() && nonNative.validate().wasOk() &&
                  !pluginNativeTopologyDifference(beforePlacement, nonNative),
              "Valid AudioClip placement history does not select plugin adapter");
        auto resized = nonNative;
        resized.audioPlacements[0].duration = 7;
        resized.audioPlacements[0].beat = 3;
        check(resized.validate().wasOk() && !pluginNativeTopologyDifference(nonNative, resized),
              "Valid AudioClip resize/move does not select plugin adapter");
        nonNative = project;
        std::reverse(nonNative.channels.begin(), nonNative.channels.end());
        std::reverse(nonNative.mixer.begin(), nonNative.mixer.end());
        check(!pluginNativeTopologyDifference(project, nonNative),
              "Display-only row order is not plugin topology");
        nonNative = project;
        nonNative.plugin(background)->state = state("new native bytes only");
        check(!pluginNativeTopologyDifference(project, nonNative), "Opaque native state is not topology");
        auto native = project;
        native.channels[0].instrument.reset();
        check(pluginNativeTopologyDifference(project, native) &&
                  pluginNativeTopologyDifference(native, project),
              "Actual instrument removal/reinsertion selects adapter in both directions");
        native = project;
        native.channels[0].id = native.allocate();
        check(pluginNativeTopologyDifference(project, native),
              "Actual owning native Channel identity is structural");
        native = project;
        native.plugin(background)->descriptor += " ";
        check(pluginNativeTopologyDifference(project, native), "Native descriptor replacement is structural");
        native = project;
        native.plugin(background)->mainInputChannels = 1;
        native.plugin(background)->mainInputMonoSource = 3;
        check(pluginNativeTopologyDifference(project, native), "Native I/O arrangement change is structural");
        auto owner = std::make_unique<Controller>();
        owner->replace(project);
        owner->deliverPendingNotifications();
        CommandArgs create;
        create.text = type.createXml()->toString();
        check(owner->execute("plugin.instrument.create", create).wasOk(),
              "Actual canonical instrument creation");
        const auto added = owner->project.channels.back().instrument->id;
        const auto addedChannel = owner->project.channels.back().id;
        const auto snapshot = [&]
        {
            return owner->project.state().toXmlString() + owner->projectIdentity +
                   juce::String(juce::int64(owner->revision)) + juce::String(int(owner->dirty)) +
                   owner->undo.getUndoDescriptions().joinIntoString("|") +
                   owner->undo.getRedoDescriptions().joinIntoString("|");
        };
        auto captured = owner->project;
        captured.plugin(added)->state = state("fresh removed instrument");
        captured.plugin(background)->state = state("fresh unchanged background");
        const auto unchanged = snapshot();
        std::unique_ptr<PreparedControllerChange> ticket;
        check(owner->prepareCapturedStructuralHistory(HistoryDirection::undo, captured, ticket).wasOk() &&
                  ticket && !ticket->destinationBeforeCommit()->plugin(added) &&
                  ticket->destinationBeforeCommit()->plugin(background)->state ==
                      state("fresh unchanged background") &&
                  snapshot() == unchanged,
              "Undo prepares destination and fresh surviving state without moving canonical cursor");
        ticket.reset();
        check(snapshot() == unchanged, "Cancelled private preparation retains model and actual Undo cursor");
        const auto commit = [&]
        {
            check(ticket && owner->validatePreparedChange(*ticket).wasOk() &&
                      owner->commitPreparedChange(*ticket),
                  "Actual prepared publication");
            ticket.reset();
        };
        check(owner->prepareCapturedStructuralHistory(HistoryDirection::undo, captured, ticket).wasOk(),
              "Cancelled history remains preparable once");
        commit();
        check(!owner->project.channel(addedChannel) && owner->undo.canRedo(),
              "Undo removes exact created owner");
        captured = owner->project;
        captured.plugin(background)->state = state("newer Redo background");
        check(owner->prepareCapturedStructuralHistory(HistoryDirection::redo, captured, ticket).wasOk() &&
                  ticket->destinationBeforeCommit()->plugin(added)->state ==
                      state("fresh removed instrument") &&
                  ticket->destinationBeforeCommit()->plugin(background)->state ==
                      state("newer Redo background"),
              "Redo restores freshly removed native state and latest unrelated current state");
        commit();
        check(owner->project.channel(addedChannel) &&
                  owner->project.plugin(added)->state == state("fresh removed instrument"),
              "Original stable Channel/plugin identity and captured state survive Redo");
        const auto stable = snapshot();
        captured = owner->project;
        captured.tempo += 1;
        check(owner->prepareCapturedStructuralHistory(HistoryDirection::undo, captured, ticket).failed() &&
                  !ticket && snapshot() == stable,
              "Capture cannot inject a musical difference");
        check(
            owner->prepareCapturedStructuralHistory(static_cast<HistoryDirection>(17), owner->project, ticket)
                    .failed() &&
                !ticket && snapshot() == stable,
            "Invalid direction leaves source and history untouched");
        // A prepared target must not survive a subsequent real canonical edit.
        check(owner->prepareCapturedStructuralHistory(HistoryDirection::undo, owner->project, ticket).wasOk(),
              "Prepared history pins actual current owner/cursor");
        CommandArgs tempo;
        tempo.value = 141;
        check(owner->execute("project.tempo", tempo).wasOk() &&
                  owner->validatePreparedChange(*ticket).failed(),
              "Canonical revision change invalidates prepared history without publication");
        const auto now = snapshot();
        check(!owner->commitPreparedChange(*ticket) && snapshot() == now,
              "Stale commit cannot move model/cursor");
        ticket.reset();
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::capturedStructuralHistory
