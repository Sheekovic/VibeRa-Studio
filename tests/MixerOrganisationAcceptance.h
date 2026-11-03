// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "Commands.h"
#include "Customization.h"
#include "MixerGroups.h"
#include "MixerSelection.h"
#include "MixerOrganisationReferences.h"
#include <stdexcept>
namespace vibera::test::mixer_organisation
{
inline void check(bool ok, const char *text)
{
    if (!ok)
        throw std::runtime_error(text);
}
__declspec(noinline) inline void verifyGroupMemberColours(const juce::File &output)
{
    auto c = std::make_unique<Controller>();
    c->project.mixer[1].colour = 0xff735bd8u;
    c->project.mixer[3].colour = 0xff5ac8d8u;
    c->project.mixer[2].colour = 0xffd88b5au;
    c->project.mixer[1].routes = {{5, .375f}};
    c->project.mixer[7].muted = true;
    CommandArgs args;
    args.ids = {1, 3};
    args.text = "Original colour family";
    check(c->execute("mixer.group.create", args).wasOk(), "Original colour group");
    const auto group = c->project.mixerGroups.front().id;
    args = {};
    args.mixerGroup = group;
    args.value = double(0xffd85a7fu);
    check(c->execute("mixer.group.colour", args).wasOk(), "Distinct marker colour");
    const auto before = c->project;
    auto expected = before;
    expected.mixer[3].colour = 0xff735bd8u;
    args = {};
    args.mixerGroup = group;
    args.mixer = 1;
    const auto undoCount = c->undo.getUndoDescriptions().size();
    check(
        c->execute("mixer.group.colourMembers", args).wasOk() &&
            c->project.state().isEquivalentTo(expected.state()) &&
            c->undo.getUndoDescriptions().size() == undoCount + 1,
        "Literal colour-only change; marker, unrelated colours, sources/routes/mutes unchanged; one history");
    const auto coloured = c->project.state();
    check(c->undoEditChecked().wasOk() && c->project.state().isEquivalentTo(before.state()) &&
              c->redoEditChecked().wasOk() && c->project.state().isEquivalentTo(coloured),
          "Exact whole-project colour Undo/Redo");
    const auto revision = c->revision;
    check(c->execute("mixer.group.colourMembers", args).wasOk() && c->revision == revision &&
              c->undo.getUndoDescriptions().size() == undoCount + 1,
          "Already matching member colours are an actual no-op");
    const auto refuse = [&](CommandArgs invalid, const char *code)
    {
        const auto state = c->project.state();
        const auto rev = c->revision;
        const auto dirty = c->dirty;
        const auto undo = c->undo.getUndoDescriptions().joinIntoString("|");
        const auto redo = c->undo.getRedoDescriptions().joinIntoString("|");
        const auto result = c->execute("mixer.group.colourMembers", invalid);
        check(result.failed() && result.getErrorMessage().startsWith(code) &&
                  c->project.state().isEquivalentTo(state) && rev == c->revision && dirty == c->dirty &&
                  undo == c->undo.getUndoDescriptions().joinIntoString("|") &&
                  redo == c->undo.getRedoDescriptions().joinIntoString("|"),
              "Typed invalid member/UUID refuses without model/history/dirty mutation");
    };
    auto bad = args;
    bad.mixer = 0;
    refuse(bad, "mixer.group.invalidColourSource:");
    bad.mixer = 999;
    refuse(bad, "mixer.group.invalidColourSource:");
    bad.mixer = 2;
    refuse(bad, "mixer.group.nonMemberColourSource:");
    bad = args;
    bad.mixerGroup = juce::Uuid().toString();
    refuse(bad, "mixer.group.unavailable:");
    bad.mixerGroup = "bad";
    refuse(bad, "mixer.group.invalidArguments:");
    const juce::String owner = "original.groupColour.preview";
    check(c->beginGesture(owner), "Own original colour preview");
    CommandArgs theme;
    theme.mixer = 1;
    theme.value = 0;
    check(c->execute("mixer.colour", theme, owner).wasOk() &&
              c->execute("mixer.group.colourMembers", args, owner).wasOk() &&
              c->project.mixer[1].colour == 0 && c->project.mixer[3].colour == 0 &&
              c->project.mixerGroups.front().colour == 0xffd85a7fu,
          "Sentinel0 resets stored member defaults, never marker/global theme");
    check(c->cancelGestureChecked(owner).wasOk() && c->project.state().isEquivalentTo(coloured),
          "Owned multi-step preview cancels exact colour baseline");
    check(mixerGroupColourActionLabel(0) == "Reset group insert colours to theme" &&
              mixerGroupColourActionLabel(0xff735bd8u) == "Apply this insert colour to group",
          "Actual menu labels distinguish defaults from explicit RGB");
    CommandArgs different;
    different.mixer = 3;
    different.value = double(0xff5ac8d8u);
    check(c->execute("mixer.colour", different).wasOk(), "Prepare actual macro colour change");
    auto macro = std::make_unique<Customization>();
    check(macro
              ->addMacro({"macro.originalGroupColour",
                          "Original sampled colour",
                          {{"mixer.group.colourMembers", args, false}}})
              .wasOk(),
          "Actual canonical explicit-member colour macro");
    auto restoredMacro = std::make_unique<Customization>();
    const auto profile = macro->state();
    check(int(profile["version"]) == 5 && restoredMacro->importState(profile).wasOk(),
          "Typed source ID and group UUID retain actual profile5 serialization");
    MacroContext context;
    context.insert = 2;
    context.mixerGroup = juce::Uuid().toString();
    const auto beforeMacro = c->project.state();
    check(restoredMacro->runMacro("macro.originalGroupColour", *c, context).wasOk() &&
              c->project.state().isEquivalentTo(coloured) && c->undoEditChecked().wasOk() &&
              c->project.state().isEquivalentTo(beforeMacro),
          "Saved explicit macro never retargets other current group/member; one Undo");
    const auto file = output.getChildFile("original-group-member-colours.vibra");
    Project reopened;
    check(!file.exists() && saveProject(c->project, file).wasOk() && loadProject(file, reopened).wasOk() &&
              reopened.state().isEquivalentTo(c->project.state()),
          "Actual colour/marker/source/route persistence");
}

__declspec(noinline) inline void verify(const juce::File &output)
{
    check(output.createDirectory().wasOk(), "Original owned organisation output");
    auto c = std::make_unique<Controller>();
    check(c->project.mixer.size() == 18 && c->project.mixerGroups.empty(), "Default18 strips retained");
    c->project.mixer[1].stereoSeparation = -.375f;
    c->project.mixer[1].stereoSwapped = true;
    c->project.mixer[1].routes = {{5, .375f}};
    c->project.mixer[3].routes = {{5, .5f}};
    c->project.mixer[4].routes = {{3, .25f}};
    c->project.mixer[7].muted = true;
    const auto original = c->project.state();
    CommandArgs args;
    args.ids = {3, 1};
    args.text = "Drums";
    check(c->execute("mixer.group.create", args).wasOk(), "Actual canonical group creation");
    const auto groupId = c->project.mixerGroups.front().id;
    check(validMixerGroupId(groupId) && c->project.mixerGroups.front().members == std::vector<int>({1, 3}),
          "Stable UUID and canonical member storage");
    const auto created = c->project.state();
    check(c->undo.getUndoDescriptions().size() == 1 && c->undoEditChecked().wasOk() &&
              c->project.state().isEquivalentTo(original) && c->redoEditChecked().wasOk() &&
              c->project.state().isEquivalentTo(created),
          "One real Undo/Redo restores exact group UUID/state");
    auto untouched = c->project;
    untouched.mixerGroups.clear();
    check(untouched.state().isEquivalentTo(original),
          "Grouping never changes sources, routes, stereo controls or mute state");
    auto refused = [&](const juce::String &id, const CommandArgs &a)
    {
        const auto before = c->project.state();
        const auto revision = c->revision;
        const auto dirty = c->dirty;
        const auto undo = c->undo.getUndoDescriptions().joinIntoString("|"),
                   redo = c->undo.getRedoDescriptions().joinIntoString("|");
        check(c->execute(id, a).failed(), "Invalid canonical operation must refuse");
        check(before.isEquivalentTo(c->project.state()) && revision == c->revision && dirty == c->dirty &&
                  undo == c->undo.getUndoDescriptions().joinIntoString("|") &&
                  redo == c->undo.getRedoDescriptions().joinIntoString("|"),
              "Refusal preserves project, dirty, revision and both actual history directions");
    };
    auto invalid = args;
    invalid.ids = {0, 1};
    refused("mixer.group.create", invalid);
    invalid.ids = {1, 1};
    refused("mixer.group.create", invalid);
    invalid.ids = {17, 300};
    refused("mixer.group.create", invalid);
    invalid.ids = {2};
    invalid.text = "drums";
    refused("mixer.group.create", invalid);
    args = {};
    args.mixerGroup = groupId;
    args.value = double(0xff5ac8d8u);
    check(c->execute("mixer.group.colour", args).wasOk() && c->project.mixer[1].colour == 0,
          "Group marker colour retains individual insert colours");
    args.text = "Percussion";
    check(c->execute("mixer.group.rename", args).wasOk() &&
              mixerGroup(c->project, groupId)->name == "Percussion",
          "Group rename retains UUID");
    args.enabled = true;
    check(c->execute("mixer.group.mute", args).wasOk() && c->project.mixer[1].muted &&
              c->project.mixer[3].muted && c->project.mixer[7].muted,
          "Group mute changes only its members");
    check(c->undoEditChecked().wasOk() && !c->project.mixer[1].muted && !c->project.mixer[3].muted &&
              c->project.mixer[7].muted,
          "One group mute Undo preserves unrelated baseline");
    const auto beforeSolo = c->project.state();
    check(c->execute("mixer.group.solo", args).wasOk() && c->project.mixerSolo->group == groupId,
          "Real group solo owner");
    for (const auto &insert : c->project.mixer)
        check(insert.muted ==
                  !(insert.index == 0 || insert.index == 1 || insert.index == 3 || insert.index == 5),
              "Independent literal group downstream solo mask");
    auto membership = args;
    membership.ids = {2};
    refused("mixer.group.assign", membership);
    refused("mixer.group.delete", args);
    check(c->execute("mixer.group.soloConnected", args).wasOk() && !c->project.mixer[4].muted,
          "Connected group solo keeps actual upstream4-to3 dependency");
    const auto file = output.getChildFile("original-group-solo.vibra");
    check(!file.exists() && saveProject(c->project, file).wasOk(), "New disposable group solo project saved");
    Project restored;
    check(loadProject(file, restored).wasOk() && restored.state().isEquivalentTo(c->project.state()),
          "Actual project file roundtrip includes group solo restoration baseline");
    check(c->execute("mixer.group.unsolo", args).wasOk() && c->project.state().isEquivalentTo(beforeSolo),
          "Group solo switching restores exact original mute mask");
    auto ordinary = CommandArgs{};
    ordinary.mixer = 2;
    check(c->execute("mixer.group.solo", args).wasOk() && c->execute("mixer.solo", ordinary).wasOk() &&
              c->project.mixerSolo->group.isEmpty() && c->execute("mixer.unsolo", ordinary).wasOk() &&
              c->project.state().isEquivalentTo(beforeSolo),
          "Single/group switching preserves one original mute baseline");
    check(c->execute("mixer.group.solo", args).wasOk(), "Enter group solo for newer manual mute");
    CommandArgs newerMute;
    newerMute.mixer = 1;
    newerMute.enabled = true;
    check(c->execute("mixer.mute", newerMute).wasOk() && !c->project.mixerSolo && c->project.mixer[1].muted,
          "Manual insert mute ends group restore ownership and retains the newer actual mask");
    refused("mixer.group.unsolo", args);
    check(c->project.mixer[1].muted && c->undoEditChecked().wasOk() && c->project.mixerSolo &&
              c->project.mixerSolo->group == groupId && c->execute("mixer.group.unsolo", args).wasOk() &&
              c->project.state().isEquivalentTo(beforeSolo),
          "Refused stale restore cannot overwrite newer mute; Undo recovers original restore session");
    check(c->execute("mixer.group.solo", args).wasOk(), "Enter group solo for explicit group mute");
    auto unmuteGroup = args;
    unmuteGroup.enabled = false;
    check(c->execute("mixer.group.mute", unmuteGroup).wasOk() && !c->project.mixerSolo &&
              !c->project.mixer[1].muted && !c->project.mixer[3].muted,
          "Manual group mask change ends restore session explicitly");
    refused("mixer.group.unsolo", args);
    check(c->undoEditChecked().wasOk() && c->execute("mixer.group.unsolo", args).wasOk() &&
              c->project.state().isEquivalentTo(beforeSolo),
          "Group mute Undo preserves pre-existing mute restoration");
    auto second = CommandArgs{};
    second.ids = {2, 4};
    second.text = "Temporary abandoned group";
    check(c->execute("mixer.group.create", second).wasOk(), "Original abandoned branch group");
    const auto abandoned = c->project.mixerGroups.back().id;
    check(c->undoEditChecked().wasOk(), "Undo abandoned group creation");
    second.text = "Replacement group";
    check(c->execute("mixer.group.create", second).wasOk() && c->project.mixerGroups.back().id != abandoned,
          "New branch never reuses the abandoned group UUID");
    auto staleGroup = CommandArgs{};
    staleGroup.mixerGroup = abandoned;
    staleGroup.text = "Never retarget";
    refused("mixer.group.rename", staleGroup);
    auto dissolve = CommandArgs{};
    dissolve.mixerGroup = c->project.mixerGroups.back().id;
    check(c->execute("mixer.group.delete", dissolve).wasOk() && c->project.state().isEquivalentTo(beforeSolo),
          "Dissolve display metadata preserves every original insert/source/route/control");
    auto parseFailure = [&](juce::ValueTree tree)
    {
        Project target = c->project;
        const auto previous = target.state();
        check(Project::parse(tree, target).failed() && target.state().isEquivalentTo(previous),
              "Malformed parse preserves prior project");
    };
    auto olderMasquerade = c->project.state();
    olderMasquerade.setProperty("version", 22, nullptr);
    parseFailure(olderMasquerade);
    auto duplicated = c->project.state();
    duplicated.appendChild(duplicated.getChildWithName("MIXER_GROUPS").createCopy(), nullptr);
    parseFailure(duplicated);
    auto masterMember = c->project.state();
    masterMember.getChildWithName("MIXER_GROUPS").getChild(0).getChild(0).setProperty("insert", 0, nullptr);
    parseFailure(masterMember);
    auto malformedUuid = c->project.state();
    malformedUuid.getChildWithName("MIXER_GROUPS").getChild(0).setProperty("id", "not-a-UUID", nullptr);
    parseFailure(malformedUuid);
    auto genuine22 = Project::empty().state();
    genuine22.setProperty("version", 22, nullptr);
    stripPatternExtentForLegacy(genuine22);
    check(Project::parse(genuine22, restored).wasOk() && restored.version == Project::currentVersion &&
              restored.mixerGroups.empty(),
          "Genuine schema22 migrates without inventing group data");
    auto nine = Project::empty();
    nine.mixer.resize(9);
    auto nineTree = nine.state();
    check(Project::parse(nineTree, restored).wasOk() && restored.mixer.size() == 9,
          "Current nine-insert project never resurrects removed rows");
    nineTree.setProperty("version", 22, nullptr);
    stripPatternExtentForLegacy(nineTree);
    check(Project::parse(nineTree, restored).wasOk() && restored.mixer.size() == 18,
          "Genuine old nine-default rack still extends18");

    MixerSelection selection{c->projectIdentity, {1}, 1, 1}, next;
    check(
        MixerSelection::prepare(c->project, c->projectIdentity, selection, {3}, "toggle", {}, next).wasOk() &&
            next.ids == std::vector<int>({1, 3}) && next.active == 3,
        "Shared Ctrl-toggle selection and independent active source");
    selection = next;
    check(
        MixerSelection::prepare(c->project, c->projectIdentity, selection, {5}, "range", {}, next).wasOk() &&
            next.ids == std::vector<int>({3, 4, 5}),
        "Shared Shift-range follows current display order/anchor");
    selection = next;
    check(MixerSelection::prepare(c->project, c->projectIdentity, selection, {3, 3}, "replace", {}, next)
                  .failed() &&
              next.ids == selection.ids,
          "Invalid selection leaves prepared output unchanged");
    auto reorder = CommandArgs{};
    reorder.mixer = 3;
    reorder.mixerPosition = 6;
    check(c->execute("mixer.move", reorder).wasOk() && mixerGroupFor(c->project, 3) == groupId &&
              c->project.mixerSolo == std::nullopt,
          "Existing stable-ID order command retains group membership");

    auto macro = std::make_unique<Customization>();
    MacroContext context;
    context.insert = 1;
    context.mixerSelection = {2, 4};
    context.mixerGroup = groupId;
    CommandArgs create;
    create.text = "Synths";
    CommandArgs rename;
    rename.text = "Synth family";
    CommandArgs colour;
    colour.value = double(0xff735bd8u);
    Macro recipe{"macro.originalMixerGroups",
                 "Original group sequence",
                 {{"mixer.group.create", create, true},
                  {"mixer.group.rename", rename, true},
                  {"mixer.group.colour", colour, true}}};
    check(macro->addMacro(recipe).wasOk(), "Actual macro accepts typed canonical group commands");
    const auto preMacro = c->project.state();
    check(macro->runMacro(recipe.id, *c, context).wasOk() &&
              mixerGroup(c->project, context.mixerGroup)->name == "Synth family",
          "Actual current-context macro carries newly created UUID to following steps");
    check(c->undoEditChecked().wasOk() && c->project.state().isEquivalentTo(preMacro),
          "One macro Undo restores complete model");
    CommandArgs staticTarget;
    staticTarget.mixerGroup = groupId;
    staticTarget.text = "Static name";
    check(macro
              ->addMacro({"macro.staticMixerGroup",
                          "Static original group",
                          {{"mixer.group.rename", staticTarget, false}}})
              .wasOk(),
          "Static UUID macro target");
    const auto profile = macro->state();
    auto imported = std::make_unique<Customization>();
    check(int(profile["version"]) == 5 && imported->importState(profile).wasOk(),
          "Actual profile5 typed UUID roundtrip");
    auto fakeOld = juce::JSON::parse(juce::JSON::toString(profile));
    fakeOld.getDynamicObject()->setProperty("version", 4);
    check(imported->importState(fakeOld).failed(), "Old profile refuses newer UUID payload");
    auto references = c->project;
    references.automation.initialValues.push_back({{AutomationParameter::mixerStereoSeparation, 1, 0}, .25f});
    check(int(mixerDeletionReferences(references, 1)["initializedTargets"]) == 1 &&
              mixerDeletionReferences(references, 1)["available"].isBool() &&
              bool(mixerDeletionReferences(references, 1)["available"]) == !bool(references.mixerSolo) &&
              !bool(mixerDeletionReferences(references, 0)["available"]),
          "Deletion readback includes stereo references, model eligibility and protected Master; explicit "
          "policy still required");
    verifyGroupMemberColours(output);
    check(
        output.getChildFile("source-only-contract.json")
            .replaceWithText(
                R"({"implementedFixture":"actual Controller/Undo/parse/files/Customization and pure selection","audio":false,"nativeInput":false,"listening":false,"hardware":false})"),
        "Write truthful bounded fixture receipt");
}
} // namespace vibera::test::mixer_organisation
