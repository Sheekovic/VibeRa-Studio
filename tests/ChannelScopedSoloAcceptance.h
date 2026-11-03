// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "ChannelSolo.h"
#include <array>
#include <set>

static void channelScopedSoloTests(const juce::File &folder)
{
    Project fixture = Project::empty();
    while (fixture.channels.size() < 7)
    {
        auto channel = fixture.channels.front();
        channel.id = fixture.allocate();
        channel.name = "Scope fixture " + juce::String(int(fixture.channels.size()));
        fixture.channels.push_back(std::move(channel));
    }
    const std::array<Id, 7> ids{fixture.channels[0].id, fixture.channels[1].id, fixture.channels[2].id,
                                fixture.channels[3].id, fixture.channels[4].id, fixture.channels[5].id,
                                fixture.channels[6].id};
    const auto a = ids[0], b = ids[1], c = ids[2], d = ids[3], e = ids[4], f = ids[5], g = ids[6];
    const auto drums = fixture.allocate(), leads = fixture.allocate();
    fixture.channelGroups = {{drums, "Scoped drums"}, {leads, "Scoped leads"}};
    const std::array<bool, 7> baseline{false, true, false, false, true, true, false};
    for (size_t i = 0; i < ids.size(); ++i)
    {
        auto &channel = fixture.channels[i];
        channel.group = i < 3 ? drums : i < 5 ? leads : 0;
        channel.muted = baseline[i];
        channel.muteLocked = i == 2 || i == 4;
        channel.mixer = int(i + 1);
        channel.sampler.cents = int(i * 7);
        fixture.patterns.front().notes.push_back(
            {fixture.allocate(), channel.id, double(i) * .25, .5, 48 + int(i), .6f, false});
    }
    fixture.placements = {{fixture.allocate(), fixture.patterns.front().id, 0, 0, 4, 0}};
    require(fixture.validate().wasOk(),
            "Independent scoped solo fixture has two groups, Unsorted and real score");
    const auto sameIds = [](const std::vector<Id> &actual, std::initializer_list<Id> expected)
    {
        return actual.size() == expected.size() &&
               std::set<Id>(actual.begin(), actual.end()) == std::set<Id>(expected);
    };
    const auto mask = [&](const Project &project, const std::array<bool, 7> &expected)
    {
        for (size_t i = 0; i < ids.size(); ++i)
            require(project.channel(ids[i]) && project.channel(ids[i])->muted == expected[i],
                    "Scoped solo matches independent literal Channel mute mask");
        require(project.channel(c)->muteLocked && !project.channel(c)->muted &&
                    project.channel(e)->muteLocked && project.channel(e)->muted,
                "Audible and muted locked Channels retain their exact states");
    };
    const auto solo = [](Id channel, const char *scope)
    {
        CommandArgs args;
        args.channel = channel;
        if (scope)
            args.soloScope = juce::String(scope);
        return args;
    };
    const auto unchangedMusic = [&](const Project &project)
    {
        auto normalized = project;
        require(normalized.channels.size() == fixture.channels.size(), "Solo never adds or drops Channels");
        normalized.solo = fixture.solo;
        for (auto &channel : normalized.channels)
        {
            const auto *original = fixture.channel(channel.id);
            require(original != nullptr, "Solo retains stable Channel identities");
            channel.muted = original->muted;
            channel.muteLocked = original->muteLocked;
        }
        require(normalized.state().isEquivalentTo(fixture.state()),
                "Solo/lock preserves notes, placements, routes, sources, settings, metadata and allocator");
    };
    Controller controller;
    controller.replace(fixture);
    const auto refused = [&](const char *command, const CommandArgs &args)
    {
        const auto before = controller.project.state();
        const auto revision = controller.revision;
        const auto undo = controller.undo.getUndoDescriptions(), redo = controller.undo.getRedoDescriptions();
        require(controller.execute(command, args).failed() &&
                    controller.project.state().isEquivalentTo(before) && controller.revision == revision &&
                    controller.undo.getUndoDescriptions() == undo &&
                    controller.undo.getRedoDescriptions() == redo,
                "Invalid scoped solo/lock refuses atomically including both history directions");
    };
    const auto noOp = [&](const char *command, const CommandArgs &args)
    {
        const auto before = controller.project.state();
        const auto revision = controller.revision;
        const auto undo = controller.undo.getUndoDescriptions(), redo = controller.undo.getRedoDescriptions();
        require(controller.execute(command, args).wasOk() &&
                    controller.project.state().isEquivalentTo(before) && controller.revision == revision &&
                    controller.undo.getUndoDescriptions() == undo &&
                    controller.undo.getRedoDescriptions() == redo,
                "No-op retains exact solo ownership, revision and both history directions");
    };

    auto lockedOnly = fixture;
    for (auto &channel : lockedOnly.channels)
        channel.muted = channel.id != c;
    controller.replace(lockedOnly);
    auto lockedExit = solo(c, "global");
    lockedExit.enabled = false;
    refused("channel.solo", lockedExit);
    controller.replace(fixture);
    const auto initial = controller.project.state();
    require(controller.execute("channel.solo", solo(b, "group")).wasOk(),
            "Solo explicit target in its actual group");
    mask(controller.project, {true, false, false, false, true, true, false});
    require(controller.project.solo && controller.project.solo->channel == b &&
                controller.project.solo->groupScoped && controller.project.solo->group == drums &&
                sameIds(controller.project.solo->affected, {a, b}) &&
                sameIds(controller.project.solo->mutedBefore, {b, e, f}) &&
                channelIsSoloed(controller.project, b),
            "Group scope owns only unlocked target-group Channels and captures the whole first baseline");
    unchangedMusic(controller.project);
    const auto firstSolo = controller.project.state();
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(initial), "Group solo is one exact Undo");
    controller.redoEdit();
    require(controller.project.state().isEquivalentTo(firstSolo),
            "Group solo Redo restores scope and baseline");
    noOp("channel.solo", solo(b, "group"));
    require(controller.execute("channel.solo", solo(d, "group")).wasOk(), "Switch to another group");
    mask(controller.project, baseline);
    require(controller.project.solo->group == leads && sameIds(controller.project.solo->affected, {d}) &&
                sameIds(controller.project.solo->mutedBefore, {b, e, f}),
            "Cross-group switch restores former affected Channels and retains first baseline");
    require(controller.execute("channel.solo", solo(d, "global")).wasOk(),
            "Switch same owner from group to global");
    mask(controller.project, {true, true, false, false, true, true, true});
    require(!controller.project.solo->groupScoped && controller.project.solo->group == 0 &&
                sameIds(controller.project.solo->affected, {a, b, d, f, g}),
            "Global scope includes unlocked Channels hidden by either group filter");
    require(controller.execute("channel.solo", solo(f, "group")).wasOk(),
            "Unsorted is a real group-zero scope");
    mask(controller.project, {false, true, false, false, true, false, true});
    require(controller.project.solo->groupScoped && controller.project.solo->group == 0 &&
                sameIds(controller.project.solo->affected, {f, g}),
            "Unsorted scope remains distinguishable from global scope");
    CommandArgs exit;
    exit.restore = false;
    require(controller.execute("channel.unsolo", exit).wasOk() && !controller.project.solo,
            "Restore-off exit clears the owned scope");
    mask(controller.project, {false, true, false, false, true, false, false});
    controller.replace(fixture);
    require(controller.execute("channel.solo", solo(b, "group")).wasOk() &&
                controller.execute("channel.unsolo").wasOk() &&
                controller.project.state().isEquivalentTo(initial),
            "Default restoration restores exact original scope, locks and outside-group mutes");
    require(controller.execute("channel.solo", solo(d, nullptr)).wasOk() &&
                !controller.project.solo->groupScoped,
            "Absent scope preserves existing global command compatibility");
    mask(controller.project, {true, true, false, false, true, true, true});

    for (const auto *invalid : {"", "visible", "GROUP", "group "})
        refused("channel.solo", solo(a, invalid));
    auto misplacedScope = solo(d, "group");
    refused("channel.unsolo", misplacedScope);
    refused("channel.mute", misplacedScope);
    refused("channel.solo", solo(999999, "group"));
    refused("channel.solo", solo(c, "group"));
    refused("channel.solo", solo(e, "global"));
    auto invalidArgs = solo(a, "group");
    invalidArgs.enabled = false;
    refused("channel.solo", invalidArgs); // Another owner cannot end d's solo.
    invalidArgs = {};
    invalidArgs.channel = c;
    invalidArgs.enabled = true;
    refused("channel.mute", invalidArgs);
    invalidArgs.channel = e;
    invalidArgs.enabled = false;
    refused("channel.mute", invalidArgs);
    require(controller.execute("channel.unsolo", exit).wasOk(), "Restore-off global exit");
    mask(controller.project, {false, false, false, false, true, false, false});

    controller.replace(fixture);
    require(controller.execute("channel.solo", solo(b, "group")).wasOk(), "Lock ownership fixture");
    CommandArgs lock;
    lock.channel = c;
    lock.enabled = true;
    noOp("channel.muteLock", lock);
    auto bulkLock = lock;
    bulkLock.channel = 0;
    bulkLock.ids = {c, e};
    noOp("rack.muteLock", bulkLock);
    lock.channel = a;
    require(controller.execute("channel.muteLock", lock).wasOk() && !controller.project.solo &&
                controller.project.channel(a)->muteLocked,
            "Changing a lock ends restore ownership without changing audible state");
    mask(controller.project, {true, false, false, false, true, true, false});
    const auto changedLock = controller.project.state();
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(firstSolo),
            "Lock Undo restores prior active solo exactly");
    controller.redoEdit();
    require(controller.project.state().isEquivalentTo(changedLock),
            "Lock Redo restores cancellation and lock");
    require(controller.execute("channel.unsolo").wasOk() &&
                controller.project.state().isEquivalentTo(changedLock),
            "Later unsolo cannot overwrite a newer manual lock/mute state");
    lock = {};
    lock.ids = {c, e};
    lock.enabled = false;
    require(controller.execute("rack.muteLock", lock).wasOk() && !controller.project.channel(c)->muteLocked &&
                !controller.project.channel(e)->muteLocked && !controller.project.channel(c)->muted &&
                controller.project.channel(e)->muted,
            "Bulk unlock changes only explicit lock flags, preserving both audible states");
    for (const auto &targets : {std::vector<Id>{}, std::vector<Id>{a, a}, std::vector<Id>{a, 999999}})
    {
        lock.ids = targets;
        refused("rack.muteLock", lock);
    }
    lock = {};
    lock.channel = 999999;
    refused("channel.muteLock", lock);

    controller.replace(fixture);
    require(controller.execute("channel.solo", solo(b, "group")).wasOk(),
            "Bulk/manual mute ownership fixture");
    CommandArgs bulk;
    bulk.ids = {c, e};
    noOp("rack.muteSelected", bulk);
    noOp("rack.unmuteSelected", bulk);
    bulk.ids = {a, c};
    noOp("rack.muteSelected", bulk); // a is already muted, c is locked.
    bulk.ids = {b, c, e};
    require(controller.execute("rack.muteSelected", bulk).wasOk() && !controller.project.solo,
            "Actual unlocked bulk mute ends restore ownership and skips locked targets");
    mask(controller.project, {true, true, false, false, true, true, false});
    require(controller.execute("channel.unsolo").wasOk(),
            "Manual bulk change remains authoritative after unsolo");
    mask(controller.project, {true, true, false, false, true, true, false});
    controller.replace(fixture);
    require(controller.execute("channel.solo", solo(b, "group")).wasOk(), "Direct mute fixture");
    CommandArgs mute;
    mute.channel = g;
    mute.enabled = true;
    require(controller.execute("channel.mute", mute).wasOk() && !controller.project.solo,
            "Manual outside-group mute cancels restoration without restoring the old scope");
    mask(controller.project, {true, false, false, false, true, true, true});

    controller.replace(fixture);
    require(controller.execute("channel.solo", solo(a, "group")).wasOk(),
            "Membership reconciliation fixture");
    CommandArgs assign;
    assign.ids = {b};
    assign.target = leads;
    require(controller.execute("rack.group.assign", assign).wasOk() && controller.project.solo &&
                sameIds(controller.project.solo->affected, {a}) && controller.project.channel(b)->muted,
            "Leaving solo scope restores the member's original muted baseline");
    assign.ids = {g};
    assign.target = drums;
    require(controller.execute("rack.group.assign", assign).wasOk() && controller.project.channel(g)->muted &&
                sameIds(controller.project.solo->affected, {a, g}),
            "New scope member enters solo mask while retaining its original audible baseline");
    require(controller.execute("channel.unsolo").wasOk() && !controller.project.channel(g)->muted &&
                controller.project.channel(b)->muted,
            "Membership changes restore each member from its original baseline, not transient mutes");
    controller.replace(fixture);
    require(controller.execute("channel.solo", solo(b, "group")).wasOk(), "Moving scoped owner fixture");
    assign.ids = {b};
    assign.target = leads;
    require(controller.execute("rack.group.assign", assign).wasOk() && !controller.project.solo &&
                controller.project.channel(b)->group == leads,
            "Moving the solo owner to another group ends ownership instead of transferring its scope");
    mask(controller.project, baseline);
    controller.replace(fixture);
    require(controller.execute("channel.solo", solo(b, "group")).wasOk(), "Scoped group deletion fixture");
    CommandArgs remove;
    remove.target = drums;
    require(controller.execute("rack.group.delete", remove).wasOk() && !controller.project.solo &&
                !controller.project.channelGroup(drums),
            "Deleting the scoped group restores affected Channels and ends solo");
    mask(controller.project, baseline);
    const auto deletedGroup = controller.project.state();
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(firstSolo),
            "Group deletion Undo restores exact scope/session");
    controller.redoEdit();
    require(controller.project.state().isEquivalentTo(deletedGroup), "Group deletion Redo is exact");
    controller.replace(fixture);
    require(controller.execute("channel.solo", solo(b, "group")).wasOk(), "Solo target deletion fixture");
    remove = {};
    remove.ids = {b};
    require(controller.execute("channel.delete", remove).wasOk() && !controller.project.channel(b) &&
                !controller.project.solo && !controller.project.channel(a)->muted &&
                !controller.project.channel(c)->muted && controller.project.channel(e)->muted &&
                controller.project.channel(f)->muted && !controller.project.channel(g)->muted,
            "Target deletion restores only affected survivors and preserves locks/outside group");
    controller.undoEdit();
    require(controller.project.state().isEquivalentTo(firstSolo),
            "Target deletion Undo restores original IDs/session");

    controller.replace(fixture);
    require(controller.execute("channel.solo", solo(b, "group")).wasOk(),
            "New Channel reconciliation fixture");
    const auto added = controller.project.nextId;
    CommandArgs add;
    add.text = "builtin:hat";
    add.target = drums;
    require(controller.execute("channel.add", add).wasOk() && controller.project.channel(added)->muted &&
                !controller.project.channel(added)->muteLocked && controller.project.solo &&
                sameIds(controller.project.solo->affected, {a, b, added}),
            "Fresh unlocked group member is masked immediately without losing default restore state");
    require(controller.execute("channel.unsolo").wasOk() && !controller.project.channel(added)->muted,
            "Fresh Channel restores its actual pre-solo default after exit");
    for (const auto original : {a, b, c})
    {
        controller.replace(fixture);
        require(controller.execute("channel.solo", solo(b, "group")).wasOk(), "Clone baseline fixture");
        const auto cloneId = controller.project.nextId;
        CommandArgs clone;
        clone.ids = {original};
        require(controller.execute("channel.clone", clone).wasOk(), "Clone during scoped solo");
        const auto *copy = controller.project.channel(cloneId);
        require(copy && copy->group == drums && copy->muteLocked == (original == c) &&
                    copy->muted == (original != c) &&
                    controller.project.patterns.front().notes.size() ==
                        fixture.patterns.front().notes.size() &&
                    controller.project.placements.size() == fixture.placements.size(),
                "Clone inherits actual lock/group and active scope applies only to unlocked clone");
        require(controller.execute("channel.unsolo").wasOk() &&
                    controller.project.channel(cloneId)->muted == (original == b),
                "Clone restoration inherits original pre-solo baseline, not its temporary mask");
    }

    controller.replace(fixture);
    require(controller.execute("channel.solo", solo(b, "group")).wasOk(), "Scoped persistence fixture");
    const auto serialized = controller.project.state();
    Project loaded;
    const auto path = folder.getChildFile("scoped-channel-solo.vibra");
    require(saveProject(controller.project, path).wasOk() && loadProject(path, loaded).wasOk() &&
                loaded.state().isEquivalentTo(serialized),
            "Schema28 round trips group scope, locks, whole baseline and exact affected ownership");
    Controller reopened;
    reopened.replace(loaded);
    require(reopened.execute("channel.unsolo").wasOk() && reopened.project.state().isEquivalentTo(initial),
            "Reopened group solo restores exact original project");

    // Schema27 predates every lock/scope/affected field. Its active global solo
    // still carries a real mixed baseline and the legacy complete global mask.
    auto legacyProject = fixture;
    for (auto &pattern : legacyProject.patterns)
        pattern.autoLength = false; // Bind baseline restoration to genuine legacy extent semantics.
    for (auto &channel : legacyProject.channels)
        channel.muteLocked = false;
    Controller legacy;
    legacy.replace(legacyProject);
    const auto legacyBefore = legacy.project.state();
    require(legacy.execute("channel.solo", solo(d, nullptr)).wasOk(),
            "Author genuine legacy global solo state");
    auto version27 = legacy.project.state();
    version27.setProperty("version", 27, nullptr);
    stripPatternExtentForLegacy(version27);
    for (auto channel : version27.getChildWithName("CHANNELS"))
        channel.removeProperty("muteLocked", nullptr);
    auto oldSolo = version27.getChildWithName("CHANNEL_SOLO");
    oldSolo.removeProperty("groupScoped", nullptr);
    oldSolo.removeProperty("group", nullptr);
    for (int i = oldSolo.getNumChildren(); --i >= 0;)
        if (oldSolo.getChild(i).hasType("AFFECTED"))
            oldSolo.removeChild(i, nullptr);
    require(Project::parse(version27, loaded).wasOk() && loaded.solo && !loaded.solo->groupScoped &&
                loaded.solo->group == 0 && sameIds(loaded.solo->affected, {a, b, c, d, e, f, g}) &&
                std::none_of(loaded.channels.begin(), loaded.channels.end(),
                             [](const auto &channel) { return channel.muteLocked; }),
            "Genuine schema27 active global solo migrates all affected IDs and defaults locks false");
    reopened.replace(loaded);
    require(reopened.execute("channel.unsolo").wasOk() &&
                reopened.project.state().isEquivalentTo(legacyBefore),
            "Migrated active global solo restores genuine legacy mixed baseline");
    const auto rejectTree = [&](const juce::ValueTree &tree)
    {
        const auto before = loaded.state();
        require(Project::parse(tree, loaded).failed() && loaded.state().isEquivalentTo(before),
                "Malformed scoped solo/lock serialization preserves caller output atomically");
    };
    for (int fault = 0; fault < 9; ++fault)
    {
        auto broken = serialized.createCopy();
        auto tree = broken.getChildWithName("CHANNEL_SOLO");
        auto affected = tree.getChildWithName("AFFECTED");
        require(affected.isValid(), "Schema28 serializes explicit affected Channel children");
        if (fault == 0)
            tree.appendChild(affected.createCopy(), nullptr);
        if (fault == 1)
            affected.setProperty("channel", 999999, nullptr);
        if (fault == 2)
            affected.setProperty("channel", juce::int64(c), nullptr); // Locked, never affected.
        if (fault == 3)
            tree.setProperty("group", 999999, nullptr);
        if (fault == 4)
            tree.setProperty("groupScoped", "true", nullptr);
        if (fault == 5)
            broken.getChildWithName("CHANNELS").getChild(0).setProperty("muteLocked", "false", nullptr);
        if (fault == 6)
            broken.getChildWithName("CHANNELS").getChild(1).setProperty("muteLocked", true, nullptr);
        if (fault == 7)
            tree.removeChild(affected, nullptr); // Missing actual unlocked scope member.
        if (fault == 8)
            affected.setProperty("channel", juce::int64(d), nullptr); // Existing but outside scope.
        rejectTree(broken);
    }
    auto unsupported = serialized.createCopy();
    unsupported.setProperty("version", 27, nullptr);
    rejectTree(unsupported);
    unsupported = version27.createCopy();
    unsupported.getChildWithName("CHANNELS").getChild(0).setProperty("muteLocked", true, nullptr);
    rejectTree(unsupported);
}
