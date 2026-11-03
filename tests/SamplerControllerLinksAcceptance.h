// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include <limits>

// Original model/persistence/history checks. Runtime MIDI routing is qualified separately.
static void samplerControllerLinksTests(const juce::File &folder)
{
    using namespace vibera;
    auto opening = Project::empty();
    const auto owner = opening.channels.front().id;
    const auto otherOwner = opening.channels[1].id;
    const auto original = opening.state();
    require(!original.getChildWithName("SAMPLER_CONTROLLER_LINKS").isValid(),
            "Neutral projects omit the new sparse link collection");
    SamplerControllerLink row;
    row.channel = owner;
    row.parameter = 17; // Independent stable Volume LFO period code, physical .01..64.
    row.port = 1;
    row.midiChannel = 2;
    row.cc = 21;
    row.minimum = .01;
    row.maximum = 64;
    row.label = "Original period CC";
    require(validateSamplerControllerLink(row).wasOk(),
            "Typed original CC source and period endpoints validate");
    auto probe = row;
    probe.port = 0;
    for (int cc : {64, 120, 123})
    {
        probe.cc = cc;
        require(validateSamplerControllerLink(probe).failed(),
                "Performance sustain and panic messages cannot become project control reservations");
        probe.port = 1;
        require(validateSamplerControllerLink(probe).wasOk(),
                "Control port retains genuine CC64/120/123 semantics");
        probe.port = 0;
    }
    Controller controller;
    controller.replace(opening);
    CommandArgs create;
    create.samplerControllerLink = row;
    require(controller.execute("channel.sampler.controller.create", create).wasOk(),
            "Canonical project link creation is one document command");
    const auto first = controller.project.samplerControllerLinks.front().id;
    require(first == opening.nextId && controller.project.nextId == first + 1 && controller.dirty &&
                controller.project.samplerControllerLinks.front().channel == owner,
            "Link uses actual Project allocator and fixed destination, and dirties document");
    const auto firstState = controller.project.state();
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(original) &&
                !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(firstState) && !controller.undo.canRedo(),
            "One link creation has exact real Undo/Redo including allocator and all score fields");
    const auto assertFailure = [&](const char *command, const CommandArgs &args)
    {
        const auto before = controller.project.state();
        const auto revision = controller.revision;
        const auto dirty = controller.dirty;
        const auto undo = controller.undo.canUndo(), redo = controller.undo.canRedo();
        require(controller.execute(command, args).failed() &&
                    controller.project.state().isEquivalentTo(before) && controller.revision == revision &&
                    controller.dirty == dirty && controller.undo.canUndo() == undo &&
                    controller.undo.canRedo() == redo,
                "Rejected controller metadata preserves actual Project/history/allocator/dirty/revision");
    };
    assertFailure("channel.sampler.controller.create",
                  create); // Exact source conflict, not target uniqueness.
    auto second = row;
    second.cc = 22;
    create.samplerControllerLink = second;
    require(controller.execute("channel.sampler.controller.create", create).wasOk() &&
                controller.project.samplerControllerLinks.size() == 2,
            "Two independent hardware sources may target the same sampler parameter");
    const auto secondId = controller.project.samplerControllerLinks.back().id;
    auto sourceWildcard = row;
    sourceWildcard.midiChannel = 0;
    sourceWildcard.channel = otherOwner;
    create.samplerControllerLink = sourceWildcard;
    assertFailure("channel.sampler.controller.create", create);
    create.samplerControllerReplacements = {secondId};
    assertFailure("channel.sampler.controller.create",
                  create); // Wrong named owner cannot be silently removed.
    create.samplerControllerReplacements = {first, first};
    assertFailure("channel.sampler.controller.create", create);
    create.samplerControllerReplacements = {first};
    const auto beforeReplacement = controller.project.state();
    require(controller.execute("channel.sampler.controller.create", create).wasOk() &&
                controller.project.samplerControllerLinks.size() == 2 &&
                controller.project.samplerControllerLinks.front().id == secondId &&
                controller.project.samplerControllerLinks.back().channel == otherOwner,
            "Explicit exact overlapping PROJECT row is displaced atomically; unrelated source retained");
    const auto afterReplacement = controller.project.state();
    require(controller.undoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(beforeReplacement) &&
                controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(afterReplacement),
            "Conflict replacement is exactly one owned history action");
    CommandArgs update;
    update.target = secondId;
    update.samplerControllerLink = controller.project.samplerControllerLinks.front();
    const auto revision = controller.revision;
    require(controller.execute("channel.sampler.controller.update", update).wasOk() &&
                controller.revision == revision,
            "Identical link update is a no-op without history or revision");
    update.samplerControllerLink->minimum = 64;
    update.samplerControllerLink->maximum = .01;
    assertFailure("channel.sampler.controller.update", update);
    update.samplerControllerLink->minimum = std::numeric_limits<double>::quiet_NaN();
    assertFailure("channel.sampler.controller.update", update);
    update.samplerControllerLink->minimum = .01;
    update.samplerControllerLink->maximum = 64;
    update.samplerControllerLink->parameter = 32; // Pan envelope unsupported.
    assertFailure("channel.sampler.controller.update", update);
    update.samplerControllerLink->parameter = 0;
    update.samplerControllerLink->minimum = .5;
    update.samplerControllerLink->maximum = 1;
    assertFailure("channel.sampler.controller.update", update); // Boolean endpoint cannot be fractional.
    CommandArgs foreign;
    foreign.channel = owner;
    foreign.value = .5;
    foreign.samplerControllerLink = row;
    assertFailure("channel.gain", foreign);
    require(controller.beginGesture("sampler-controller-model"),
            "Own unrelated edit gesture before config refusal");
    create.samplerControllerReplacements.clear();
    create.samplerControllerLink = row;
    create.samplerControllerLink->cc = 23;
    assertFailure("channel.sampler.controller.create", create);
    require(controller.cancelGestureChecked().wasOk(),
            "Configuration refusal did not steal gesture ownership");

    const auto persistent = controller.project;
    Project restored;
    const auto path = folder.getChildFile("sampler-controller-links-model.vibra");
    require(
        saveProject(persistent, path).wasOk() && loadProject(path, restored).wasOk() &&
            restored.state().isEquivalentTo(persistent.state()),
        "Actual project file roundtrip preserves exact IDs/targets/physical doubles/source configuration");
    const auto intact = restored.state();
    const auto refuse = [&](const juce::ValueTree &tree)
    {
        require(Project::parse(tree, restored).failed() && restored.state().isEquivalentTo(intact),
                "Malformed/newer project link encoding leaves destination exactly unchanged");
    };
    const auto setExact = [](juce::ValueTree tree, const char *key, juce::var value)
    {
        tree.removeProperty(key, nullptr); // JUCE same-type epsilon must not erase a deliberate bad double.
        tree.setProperty(key, value, nullptr);
    };
    for (auto [field, value] :
         std::vector<std::pair<const char *, juce::var>>{{"id", true},
                                                         {"id", 1.5},
                                                         {"channel", juce::int64(999999999)},
                                                         {"parameter", 32},
                                                         {"parameter", 20},
                                                         {"cc", 128},
                                                         {"midiChannel", 17},
                                                         {"layer", 4},
                                                         {"port", 2},
                                                         {"minimum", true},
                                                         {"minimum", "garbage"},
                                                         {"minimum", std::numeric_limits<double>::infinity()},
                                                         {"maximum", 1000.},
                                                         {"pickup", 1},
                                                         {"enabled", "true"},
                                                         {"label", 7}})
    {
        auto malformed = intact.createCopy();
        setExact(malformed.getChildWithName("SAMPLER_CONTROLLER_LINKS").getChild(0), field, value);
        refuse(malformed);
    }
    auto malformed = intact.createCopy();
    malformed.getChildWithName("SAMPLER_CONTROLLER_LINKS").getChild(0).setProperty("unknown", 1, nullptr);
    refuse(malformed);
    malformed = intact.createCopy();
    malformed.getChildWithName("SAMPLER_CONTROLLER_LINKS").getChild(0).removeProperty("pickup", nullptr);
    refuse(malformed);
    malformed = intact.createCopy();
    malformed.appendChild(malformed.getChildWithName("SAMPLER_CONTROLLER_LINKS").createCopy(), nullptr);
    refuse(malformed);
    malformed = intact.createCopy();
    auto links = malformed.getChildWithName("SAMPLER_CONTROLLER_LINKS");
    links.appendChild(links.getChild(0).createCopy(), nullptr);
    refuse(malformed);
    malformed = intact.createCopy();
    malformed.setProperty("version", 38, nullptr);
    refuse(malformed);
    auto genuine38 = opening.state();
    genuine38.setProperty("version", 38, nullptr);
    Project migrated;
    require(Project::parse(genuine38, migrated).wasOk() && migrated.state().isEquivalentTo(original),
            "Genuine schema38 absence migrates with exact old score and settings");
    genuine38.appendChild(juce::ValueTree("SAMPLER_CONTROLLER_LINKS"), nullptr);
    refuse(genuine38); // Even empty newer child is smuggling under old schema.

    auto dormant = persistent;
    dormant.channel(owner)->source = "original-dormant-audio.wav";
    dormant.channel(owner)->audioClip = true;
    dormant.channel(owner)->audioDuration = 1;
    require(dormant.validate().wasOk(),
            "Role change retains valid dormant fixed links without native access");
    controller.replace(dormant);
    update.target = secondId;
    update.samplerControllerLink = dormant.samplerControllerLinks.front();
    update.samplerControllerLink->enabled = false;
    require(controller.execute("channel.sampler.controller.update", update).wasOk(),
            "Existing same-target dormant link can be disabled without falling back or retargeting");
    create.samplerControllerLink = row;
    create.samplerControllerLink->cc = 30;
    assertFailure("channel.sampler.controller.create", create);
    const auto dormantBeforeDelete = controller.project.state();
    CommandArgs remove;
    remove.channel = owner;
    require(
        controller.execute("channel.delete", remove).wasOk() &&
            controller.project.samplerControllerLinks.size() == 1 &&
            controller.project.samplerControllerLinks.front().channel == otherOwner &&
            controller.undoEditChecked().wasOk() &&
            controller.project.state().isEquivalentTo(dormantBeforeDelete),
        "Channel deletion removes only owned rows and actual Undo restores dormant link IDs/configuration");
    controller.replace(persistent);
    const auto beforeCloneLinks = controller.project.samplerControllerLinks;
    require(controller.execute("channel.clone", remove).wasOk() &&
                controller.project.samplerControllerLinks == beforeCloneLinks,
            "Channel clone preserves existing links without duplicating external hardware ownership");
    remove = {};
    remove.target = secondId;
    require(controller.execute("channel.sampler.controller.remove", remove).wasOk() &&
                controller.project.samplerControllerLinks.size() == 1 &&
                controller.undoEditChecked().wasOk() &&
                controller.project.samplerControllerLinks == beforeCloneLinks,
            "Explicit link removal and Undo preserve every other target and exact row identity");

    auto full = opening;
    for (size_t i = 0; i < samplerControllerLinkLimit; ++i)
    {
        auto link = row;
        link.id = full.allocate();
        link.cc = int(i % 128);
        link.midiChannel = 1 + int(i / 128);
        full.samplerControllerLinks.push_back(link);
    }
    require(full.validate().wasOk(),
            "Exactly512 project sources validate independently of global preferences");
    controller.replace(full);
    create.samplerControllerLink = row;
    create.samplerControllerLink->midiChannel = 5;
    assertFailure("channel.sampler.controller.create", create);
}
