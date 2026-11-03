// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/MixerTargetChoices.h"
// Include after the existing require() in VibeRaTests. Original pure
// presentation + actual canonical Controller tests; no component/OS/audio claim.
inline void mixerTargetChoicesTests(const juce::File &output)
{
    using namespace vibera;
    Controller controller;
    auto &project = controller.project;
    project.channels.resize(1);
    const auto channel = project.channels.front().id;
    project.channels.front().mixer = 1;
    require(project.mixer.size() > 2 && project.mixer.back().index != 0,
            "Original target fixture changes one dormant Mixer identity only");
    project.mixer.back().index = std::numeric_limits<int>::max();
    require(project.validate().wasOk(), "INT_MAX is a genuine valid existing sparse Mixer index");
    const MixerTargetChoices choices(project, controller.projectIdentity, channel);
    const auto master = choices.ordinalForTarget(0);
    const auto large = choices.ordinalForTarget(std::numeric_limits<int>::max());
    require(master > 0 && large > 0 && master != large && choices.items().size() == project.mixer.size() &&
                large <= int(project.mixer.size()),
            "Master and INT_MAX Mixer targets have small independent JUCE ordinal IDs");
    CommandArgs args;
    require(choices.resolve(project, controller.projectIdentity, master, args).wasOk() &&
                args.channel == channel && args.mixer == 0,
            "Master remains an explicit stable target, not popup cancellation");
    require(choices.resolve(project, controller.projectIdentity, large, args).wasOk() &&
                args.channel == channel && args.mixer == std::numeric_limits<int>::max(),
            "Ordinal resolves exact INT_MAX target without index arithmetic/overflow");
    const auto revision = controller.revision;
    const auto history = controller.undo.getUndoDescriptions().size();
    require(controller.execute("channel.mixer", args).wasOk() &&
                project.channel(channel)->mixer == std::numeric_limits<int>::max() &&
                controller.revision == revision + 1 &&
                controller.undo.getUndoDescriptions().size() == history + 1,
            "Mapped choice uses actual canonical channel.mixer validation and one real history action");
    require(controller.undoEditChecked().wasOk() && project.channel(channel)->mixer == 1 &&
                controller.redoEditChecked().wasOk() &&
                project.channel(channel)->mixer == std::numeric_limits<int>::max(),
            "Actual Undo/Redo preserves exact sparse target identity");
    const auto source = project.state().toXmlString();
    const auto sourceRevision = controller.revision;
    for (const auto ordinal : {0, -1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
    {
        CommandArgs refused;
        refused.channel = 12345;
        refused.mixer = 777;
        require(choices.resolve(project, controller.projectIdentity, ordinal, refused).failed() &&
                    refused.channel == 12345 && refused.mixer == 777,
                "Invalid/cancelled/out-of-range ordinal never aliases an insert or mutates output");
    }
    auto replacement = project;
    CommandArgs refused;
    refused.channel = 12345;
    refused.mixer = 777;
    require(choices.resolve(replacement, "different-project-same-IDs", large, refused).failed() &&
                refused.channel == 12345 && refused.mixer == 777,
            "A new session reusing every numeric ID cannot consume an old menu choice");
    auto removedChannel = project;
    removedChannel.channels.clear();
    require(choices.resolve(removedChannel, controller.projectIdentity, large, refused).failed(),
            "Original Channel must still exist at callback time");
    auto removedTarget = project;
    removedTarget.channel(channel)->mixer = 0;
    std::erase_if(removedTarget.mixer,
                  [](const auto &insert) { return insert.index == std::numeric_limits<int>::max(); });
    require(removedTarget.validate().wasOk() &&
                choices.resolve(removedTarget, controller.projectIdentity, large, refused).failed(),
            "A valid later project without the captured target refuses the stale item");
    auto reordered = project;
    std::reverse(reordered.mixer.begin(), reordered.mixer.end());
    require(reordered.validate().wasOk() &&
                choices.resolve(reordered, controller.projectIdentity, large, args).wasOk() &&
                args.mixer == std::numeric_limits<int>::max(),
            "Captured popup ordinal retains its stable target across current-row reordering");
    require(project.state().toXmlString() == source && controller.revision == sourceRevision,
            "Presentation/invalid/stale lookup changes no actual model or history");
    auto evolving = project;
    MixerTargetChoices refreshed(evolving, controller.projectIdentity, channel);
    const auto oldLarge = refreshed.ordinalForTarget(std::numeric_limits<int>::max());
    evolving.mixer.back().name = "Updated sparse name";
    require(!refreshed.matches(evolving, controller.projectIdentity) &&
                refreshed.refresh(evolving, controller.projectIdentity).wasOk() &&
                refreshed.matches(evolving, controller.projectIdentity) &&
                refreshed.ordinalForTarget(std::numeric_limits<int>::max()) == oldLarge,
            "Universe refresh updates names and preserves an existing stable token");
    evolving.channel(channel)->mixer = 0;
    std::erase_if(evolving.mixer,
                  [](const auto &insert) { return insert.index == std::numeric_limits<int>::max(); });
    evolving.mixer.emplace_back(137, "Distinct replacement target");
    require(refreshed.refresh(evolving, controller.projectIdentity).wasOk() &&
                refreshed.ordinalForTarget(137) > oldLarge &&
                refreshed.resolve(evolving, controller.projectIdentity, oldLarge, refused).failed(),
            "Removed ordinal is a tombstone, never reassigned to a new stable target");
    evolving.mixer.emplace_back(std::numeric_limits<int>::max(), "Same stable identity returns");
    std::reverse(evolving.mixer.begin(), evolving.mixer.end());
    require(refreshed.refresh(evolving, controller.projectIdentity).wasOk() &&
                refreshed.ordinalForTarget(std::numeric_limits<int>::max()) == oldLarge &&
                refreshed.resolve(evolving, controller.projectIdentity, oldLarge, args).wasOk() &&
                args.mixer == std::numeric_limits<int>::max(),
            "Same reintroduced stable index retains identity; row order does not redefine identity");
    require(refreshed.refresh(evolving, "new-session").failed() &&
                !refreshed.matches(evolving, "new-session"),
            "Refresh cannot rebind a retained menu to a different project session");
    require(output.createDirectory().wasOk() &&
                output.getChildFile("mixer-target-choices.txt")
                    .replaceWithText("Original pure mapping + actual Controller: INT_MAX stable target, "
                                     "Master/cancel, stale session/channel/"
                                     "target, row reordering and one Undo/Redo. Components/native OS/audio "
                                     "not executed by this test.\n"),
            "Retain exact target-choice scope evidence");
}
