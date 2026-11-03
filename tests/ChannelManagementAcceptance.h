// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "ChannelManagement.h"
static void channelManagementTests(const juce::File &folder)
{
    Controller c;
    const auto kick = c.project.channels[0].id, snare = c.project.channels[1].id,
               hat = c.project.channels[2].id, bass = c.project.channels[3].id;
    c.project.channels[0].sampler.cents = 17;
    const auto group = c.project.allocate();
    c.project.channelGroups.push_back({group, "Drums"});
    c.project.channels[0].group = group;
    c.project.channels[0].pan = -.25f;
    c.project.channels[0].mixer = 2;
    for (auto &channel : c.project.channels)
        c.project.patterns[0].notes.push_back({c.project.allocate(), channel.id, .5, 1, 60, .7f, false});
    auto second = c.project.patterns[0];
    second.id = c.project.allocate();
    for (auto &note : second.notes)
        note.id = c.project.allocate();
    c.project.patterns.push_back(second);
    c.project.placements = {{c.project.allocate(), second.id, 0, 0, 4, 0}};
    const auto initial = c.project.state();
    auto order = [&]
    {
        std::vector<Id> result;
        for (const auto &channel : c.project.channels)
            result.push_back(channel.id);
        return result;
    };
    CommandArgs a;
    a.ids = {kick, hat};
    const auto first = c.project.nextId;
    require(c.execute("channel.clone", a).wasOk() && c.project.channels.size() == 6,
            "Bulk clone inserts settings-only Channels");
    const auto cloneA = c.project.channels[1].id, cloneB = c.project.channels[4].id;
    require(cloneA == first && order() == std::vector<Id>{kick, cloneA, snare, hat, cloneB, bass},
            "Clones adjacent in original project order even for bulk selection");
    const auto *clone = c.project.channel(cloneA);
    require(clone->source == c.project.channel(kick)->source && clone->mixer == 2 && clone->group == group &&
                clone->sampler.cents == 17 && clone->pan == -.25f && clone->name == "KICK #2",
            "Clone retains sampler/gain/routing/group and independent display name");
    require(c.project.patterns[0].notes.size() == 4 && c.project.patterns[1].notes.size() == 4 &&
                c.project.placements.size() == 1 && c.project.placements[0].pattern == second.id,
            "Clone neither duplicates score/events nor changes linked placements");
    const auto cloned = c.project.state();
    c.undoEdit();
    require(c.project.state().isEquivalentTo(initial), "Clone one-step exact Undo");
    c.redoEdit();
    require(c.project.state().isEquivalentTo(cloned), "Clone exact Redo IDs/settings");
    require(saveProject(c.project, folder.getChildFile("channel-clones.vibra")).wasOk(), "Clone save");
    Project round;
    require(loadProject(folder.getChildFile("channel-clones.vibra"), round).wasOk() &&
                round.state().isEquivalentTo(cloned),
            "Clone project round trip");
    a.ids = {cloneB, cloneA};
    require(c.execute("channel.moveUp", a).wasOk() &&
                order() == std::vector<Id>{cloneA, kick, snare, cloneB, hat, bass},
            "Unsorted stable IDs move each selected run up once");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(cloned), "Move Undo retains all musical data");
    a.ids = {kick, cloneA};
    auto revision = c.revision;
    require(c.execute("channel.moveUp", a).wasOk() && c.revision == revision,
            "Top boundary no-op adds no history/revision");
    require(c.execute("channel.moveDown", a).wasOk() &&
                order() == std::vector<Id>{snare, kick, cloneA, hat, cloneB, bass},
            "Contiguous selected run moves down without reversal");
    c.undoEdit();
    a.ids = {kick};
    require(c.execute("channel.delete", a).wasOk() && !c.project.channel(kick), "Delete target Channel");
    for (const auto &pattern : c.project.patterns)
        require(std::none_of(pattern.notes.begin(), pattern.notes.end(),
                             [&](const auto &n) { return n.channel == kick; }),
                "Delete removes owned notes from every Pattern");
    require(c.project.placements.size() == 1 && c.project.channelGroups.size() == 1,
            "Delete keeps Pattern links and empty display groups");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(cloned), "Delete restores exact Channel and all Pattern notes");
    for (const auto &ids : {std::vector<Id>{kick, kick}, std::vector<Id>{kick, 999999}})
        for (const auto &action : {"channel.clone", "channel.delete", "channel.moveUp", "channel.moveDown"})
        {
            a.ids = ids;
            require(c.execute(action, a).failed() && c.project.state().isEquivalentTo(cloned),
                    "Duplicate/stale Channel target refuses atomically");
        }
    a.ids.clear();
    a.channel = 0;
    require(c.execute("channel.clone", a).failed(), "Empty Channel target refuses");
    // Fresh state is internal and contributes to both sides of the one Undo.
    juce::PluginDescription description;
    description.name = "Missing instrument fixture";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = folder.getChildFile("missing-owned-fixture.vst3").getFullPathName();
    description.isInstrument = true;
    PluginSpec spec{c.project.allocate(),
                    "fixture",
                    description.createXml()->toString(),
                    juce::MemoryBlock("saved", 5).toBase64Encoding(),
                    true,
                    false};
    c.project.channel(kick)->instrument = spec;
    // A valid descriptor may name an unavailable plugin bundle.
    const auto saved = c.project.validate();
    require(saved.wasOk(), "Saved missing instrument model fixture");
    a.ids = {kick};
    a.capturedInstruments = {spec};
    a.capturedInstruments[0].state = juce::MemoryBlock("fresh", 5).toBase64Encoding();
    require(c.execute("channel.clone", a).wasOk(), "Missing plugin clone retains recoverable state");
    const auto &instrument = *c.project.channels[1].instrument;
    require(instrument.id != spec.id && instrument.descriptor == spec.descriptor &&
                instrument.state == a.capturedInstruments[0].state &&
                c.project.channel(kick)->instrument->state == instrument.state,
            "Independent plugin identity keeps descriptor/state without sharing runtime IDs");
    c.undoEdit();
    require(c.project.channel(kick)->instrument->id == spec.id &&
                c.project.channel(kick)->instrument->state == a.capturedInstruments[0].state,
            "Plugin clone Undo retains original identity");
    a.capturedInstruments[0].descriptor = "changed target";
    const auto beforeInvalid = c.project.state();
    require(c.execute("channel.delete", a).failed() && c.project.state().isEquivalentTo(beforeInvalid),
            "Mismatched captured descriptor refuses before data mutation");
    Customization customization = Customization::familiarDefaults();
    require(customization.resolve(juce::KeyPress('C', juce::ModifierKeys::altModifier, 0), Scope::rack) ==
                    "channel.clone" &&
                customization.resolve(juce::KeyPress('C', juce::ModifierKeys::altModifier, 0),
                                      Scope::patterns) == "pattern.clone",
            "Alt+C is contextual and existing Pattern clone binding is retained");
    require(customization.addMacro({"macro.channel", "Reorder", {{"channel.moveDown", {}, true}}}).wasOk(),
            "Reorder macro registration");
    MacroContext context;
    context.channel = kick;
    context.channels = {kick};
    const auto beforeMacro = c.project.state();
    require(customization.runMacro("macro.channel", c, context).wasOk(), "Current selection reorder macro");
    c.undoEdit();
    require(c.project.state().isEquivalentTo(beforeMacro), "Reorder macro one exact Undo");
    require(customization.addMacro({"macro.async", "Clone", {{"channel.clone", {}, true}}}).failed(),
            "Synchronous macros explicitly refuse asynchronous state-capture actions");
    Controller cleanup;
    const auto removed = cleanup.project.channels[0].id;
    auto linkedPlugin = spec;
    linkedPlugin.id = cleanup.project.allocate();
    linkedPlugin.automationBindings = {{7, 1, 0, .5f, "Fixture parameter", ""}};
    cleanup.project.channels[0].instrument = linkedPlugin;
    const AutomationKey removedKey{AutomationParameter::pluginNormalized, linkedPlugin.id, 7},
        retainedKey{AutomationParameter::mixerGain, 0, 0};
    const auto curveId = cleanup.project.allocate();
    cleanup.project.automation.initialValues = {{removedKey, .5f}, {retainedKey, .5f}};
    cleanup.project.automation.sources.push_back(
        {curveId,
         4,
         {{cleanup.project.allocate(), 0, .2f}, {cleanup.project.allocate(), 4, .8f}},
         {{removedKey, 0, 1}, {retainedKey, 0, 1}},
         "Shared curve"});
    cleanup.project.automation.placements.push_back({cleanup.project.allocate(), curveId, 0, 4, 0, 1});
    const auto media = folder.getChildFile("owned-retained-media.wav");
    require(media.replaceWithText("owned preservation fixture"), "Create owned media preservation fixture");
    const auto audioId = cleanup.project.allocate();
    Channel audioChannel{audioId, "Audio", media.getFullPathName(), 0, .7f, 0, false};
    audioChannel.audioClip = true;
    audioChannel.audioDuration = 1;
    cleanup.project.channels.push_back(audioChannel);
    cleanup.project.audioPlacements.push_back({cleanup.project.allocate(), audioId, 0, 0, 0, 1});
    require(cleanup.project.validate().wasOk(), "Shared automation/Audio Clip fixture");
    const auto cleanupBefore = cleanup.project.state();
    CommandArgs remove;
    remove.ids = {removed, audioId};
    require(cleanup.execute("channel.delete", remove).wasOk() && cleanup.project.audioPlacements.empty() &&
                cleanup.project.automation.sources[0].links.size() == 1 &&
                cleanup.project.automation.sources[0].links[0].key == retainedKey &&
                cleanup.project.automation.initialValues.size() == 1 &&
                cleanup.project.automation.placements.size() == 1 &&
                media.loadFileAsString() == "owned preservation fixture",
            "Delete prunes only owned automation/audio references and never deletes sample files");
    cleanup.undoEdit();
    require(cleanup.project.state().isEquivalentTo(cleanupBefore),
            "Delete Undo restores shared automation and Audio Clips exactly");
    CommandArgs solo;
    solo.channel = removed;
    require(cleanup.execute("channel.solo", solo).wasOk(), "Solo deletion fixture");
    remove.ids = {removed};
    require(cleanup.execute("channel.delete", remove).wasOk() && !cleanup.project.solo &&
                std::none_of(cleanup.project.channels.begin(), cleanup.project.channels.end(),
                             [](const auto &channel) { return channel.muted; }),
            "Deleting solo owner restores baseline without stale solo IDs");
    cleanup.undoEdit();
    require(cleanup.project.solo && cleanup.project.solo->channel == removed,
            "Undo restores solo mask/session");
    remove.ids.clear();
    for (const auto &channel : cleanup.project.channels)
        remove.ids.push_back(channel.id);
    require(cleanup.execute("channel.delete", remove).wasOk() && cleanup.project.channels.empty() &&
                cleanup.project.validate().wasOk(),
            "Deleting every Channel leaves a valid empty Rack with reusable Mixer automation");
    Controller full;
    while (full.project.channels.size() < 128)
        full.project.channels.push_back(
            {full.project.allocate(), "Capacity", "builtin:kick", 0, .7f, 0, false});
    const auto fullState = full.project.state();
    CommandArgs over;
    over.channel = full.project.channels.front().id;
    require(full.execute("channel.clone", over).failed() && full.project.state().isEquivalentTo(fullState),
            "Clone capacity failure is atomic including allocator");
}
