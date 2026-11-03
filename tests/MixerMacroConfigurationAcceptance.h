// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MacroConfiguration.h"
#include "MixerMacroContracts.h"
#include "../src/MixerRouting.h"
#include "../src/MixerAutomationUiIntent.h"
#include <stdexcept>
namespace vibera::test::mixer_macro_configuration
{
inline void check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
__declspec(noinline) inline void verifySendAutomationMacro(const juce::File &output);
__declspec(noinline) inline void verify(const juce::File &output)
{
    check(output.createDirectory().wasOk(), "Owned macro output directory");
    const auto file = output.getChildFile("original-mixer-profile.json");
    check(!file.exists(), "Fresh disposable profile destination");
    auto current = std::make_unique<Customization>(Customization::familiarDefaults());
    MacroConfigurationSnapshot initial;
    check(readMacroConfiguration(*current, file, initial).wasOk() && !initial.exists && !initial.matches,
          "Absent profile has explicit identity without fake persistence");
    CommandArgs colour;
    colour.value = double(0xff5ac8d8u);
    Macro recipe{"macro.originalPersistedMixer",
                 "Original persisted Mixer recipe",
                 {{"mixer.selection.colour", colour, true}}};
    check(changeMacroConfiguration(*current, file, MacroConfigurationChange::create, recipe, {},
                                   initial.generation)
              .wasOk(),
          "Actual validated create atomically writes real profile before memory publication");
    MacroConfigurationSnapshot saved;
    check(readMacroConfiguration(*current, file, saved).wasOk() && saved.exists && saved.matches &&
              saved.generation != initial.generation,
          "Saved recipe disk/current identities agree and generation advances");
    const juce::KeyPress boundKey(juce::KeyPress::F12Key,
                                  juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier |
                                      juce::ModifierKeys::shiftModifier,
                                  0);
    check(current->bind(recipe.id, Scope::mixer, boundKey).wasOk() && current->save(file).wasOk() &&
              readMacroConfiguration(*current, file, saved).wasOk() && saved.matches &&
              current->resolve(boundKey, Scope::mixer) == recipe.id,
          "Actual saved macro shortcut fixture");
    const auto unrelatedKey = juce::KeyPress(juce::KeyPress::F9Key);
    const auto unrelatedAction = current->resolve(unrelatedKey, Scope::mixer);
    const auto disk = file.loadFileAsString(), memory = juce::JSON::toString(current->state());
    auto renamed = recipe;
    renamed.name = "Updated persisted recipe";
    check(changeMacroConfiguration(*current, file, MacroConfigurationChange::update, renamed, {},
                                   initial.generation)
                  .failed() &&
              disk == file.loadFileAsString() && memory == juce::JSON::toString(current->state()),
          "Stale UI/MCP generation never overwrites newer saved/current definitions");
    check(changeMacroConfiguration(*current, file, MacroConfigurationChange::create, recipe, {},
                                   saved.generation)
              .failed(),
          "Create cannot silently replace existing stable recipe ID");
    auto invalid = recipe;
    invalid.steps.front().action = "no.such.canonical.command";
    check(changeMacroConfiguration(*current, file, MacroConfigurationChange::update, invalid, {},
                                   saved.generation)
                  .failed() &&
              disk == file.loadFileAsString() && memory == juce::JSON::toString(current->state()),
          "Authoritative validator failure preserves exact disk and memory");
    check(changeMacroConfiguration(*current, file, MacroConfigurationChange::update, renamed, {},
                                   saved.generation)
              .wasOk(),
          "Explicit update uses exact existing ID");
    check(readMacroConfiguration(*current, file, saved).wasOk() && saved.matches &&
              current->macros.back().name == renamed.name,
          "Update readback reflects actual persisted name");
    Customization reopened;
    check(reopened.load(file).wasOk() &&
              juce::JSON::toString(reopened.state()) == juce::JSON::toString(current->state()),
          "Real profile load preserves supported recipe and all unrelated shortcuts/settings");
    auto external = *current;
    external.macros.back().name = "External newer recipe";
    check(external.save(file).wasOk(), "Original external settings writer fixture");
    const auto externalBytes = file.loadFileAsString();
    check(changeMacroConfiguration(*current, file, MacroConfigurationChange::remove, {}, recipe.id,
                                   saved.generation)
                  .failed() &&
              file.loadFileAsString() == externalBytes && current->macros.back().name == renamed.name,
          "Exact external profile change refuses stale deletion");
    MacroConfigurationSnapshot mismatch;
    check(readMacroConfiguration(*current, file, mismatch).wasOk() && mismatch.exists && !mismatch.matches &&
              changeMacroConfiguration(*current, file, MacroConfigurationChange::remove, {}, recipe.id,
                                       mismatch.generation)
                  .failed(),
          "Fresh token does not authorize silently reconciling divergent whole settings");
    check(file.replaceWithText("{corrupt original fixture"), "Write bounded corrupt owned profile");
    check(readMacroConfiguration(*current, file, mismatch).failed() &&
              changeMacroConfiguration(*current, file, MacroConfigurationChange::remove, {}, recipe.id,
                                       saved.generation)
                  .failed() &&
              file.loadFileAsString() == "{corrupt original fixture",
          "Corrupt settings preserved; not overwritten by a recipe edit");
    check(current->save(file).wasOk() && readMacroConfiguration(*current, file, saved).wasOk(),
          "Explicit fixture reconciliation");
    current->running = true;
    check(changeMacroConfiguration(*current, file, MacroConfigurationChange::remove, {}, recipe.id,
                                   saved.generation)
              .failed(),
          "Running recipe cannot be reconfigured");
    current->running = false;
    const auto blockedParent = output.getChildFile("bound-removal-parent-file");
    check(blockedParent.replaceWithText("owned bound removal"), "Disposable real removal failure parent");
    const auto refusedDestination = blockedParent.getChildFile("profile.json");
    MacroConfigurationSnapshot beforeFailedRemoval;
    check(readMacroConfiguration(*current, refusedDestination, beforeFailedRemoval).wasOk(),
          "Absent failing removal destination identity");
    const auto fullBoundBaseline = juce::JSON::toString(current->state());
    check(changeMacroConfiguration(*current, refusedDestination, MacroConfigurationChange::remove, {},
                                   recipe.id, beforeFailedRemoval.generation)
                  .failed() &&
              juce::JSON::toString(current->state()) == fullBoundBaseline &&
              current->resolve(boundKey, Scope::mixer) == recipe.id &&
              blockedParent.loadFileAsString() == "owned bound removal",
          "Real failed bound-macro removal preserves both definitions and live shortcut bindings");
    check(changeMacroConfiguration(*current, file, MacroConfigurationChange::remove, {}, recipe.id,
                                   saved.generation)
              .wasOk(),
          "Explicit removal persists through shared service");
    check(readMacroConfiguration(*current, file, saved).wasOk() && saved.matches &&
              std::none_of(current->macros.begin(), current->macros.end(),
                           [&](const auto &m) { return m.id == recipe.id; }),
          "Removed recipe absent from actual saved/current state");
    check(current->resolve(boundKey, Scope::mixer).isEmpty() &&
              current->resolve(unrelatedKey, Scope::mixer) == unrelatedAction,
          "Successful removal clears its actual live shortcut and preserves unrelated bindings");
    check(reopened.load(file).wasOk() &&
              juce::JSON::toString(reopened.state()) == juce::JSON::toString(current->state()) &&
              reopened.resolve(boundKey, Scope::mixer).isEmpty() &&
              reopened.resolve(unrelatedKey, Scope::mixer) == unrelatedAction,
          "Actual reload/profileMatchesMemory and shortcut resolution agree after bound removal");
    // A normal file blocks a descendant destination without permissions tricks or user data.
    const auto blocker = output.getChildFile("ordinary-parent-file");
    check(blocker.replaceWithText("owned"), "Owned blocker");
    const auto impossible = blocker.getChildFile("profile.json");
    MacroConfigurationSnapshot unavailable;
    check(readMacroConfiguration(*current, impossible, unavailable).wasOk(),
          "Unavailable destination is still an absent profile");
    const auto baseline = juce::JSON::toString(current->state());
    check(changeMacroConfiguration(*current, impossible, MacroConfigurationChange::create, recipe, {},
                                   unavailable.generation)
                  .failed() &&
              baseline == juce::JSON::toString(current->state()) && blocker.loadFileAsString() == "owned",
          "Real persistence failure never publishes candidate definitions or damages blocking file");
    verifySendAutomationMacro(output);
}
__declspec(noinline) inline void verifySendAutomationMacro(const juce::File &output)
{
    auto customization = std::make_unique<Customization>(Customization::familiarDefaults());
    const auto legacy = juce::JSON::toString(customization->state());
    CommandArgs create;
    create.mixer = 1;
    create.length = 4;
    create.automationControl = MixerAutomationControl::send;
    create.automationSendTarget = 3;
    const Macro recipe{
        "macro.originalSendPair", "Original fixed send pair", {{"automation.create", create, false}}};
    const auto file = output.getChildFile("original-send-profile.json");
    check(!file.exists(), "Fresh actual send macro profile destination");
    MacroConfigurationSnapshot empty;
    check(readMacroConfiguration(*customization, file, empty).wasOk() && !empty.exists &&
              changeMacroConfiguration(*customization, file, MacroConfigurationChange::create, recipe, {},
                                       empty.generation)
                  .wasOk(),
          "Actual shared macro service persists send destination before publishing memory");
    check(int(customization->state()["version"]) == 6,
          "Only new send metadata advances settings profile6, not product version");
    // Keep the deliberate send-only profile6 assertion above. Verify the schema
    // against a real newest-feature profile rather than an obsolete ceiling pin.
    auto retained = *customization;
    retained.muteRemovedSteps = true;
    const auto retainedProfile = retained.state();
    check(int(retainedProfile["version"]) == 10,
          "Retained steps explicitly require profile10 without upgrading send-only profiles");
    const auto ceiling = int(mixerMacrosTool()["outputSchema"]["properties"]["profileVersion"]["maximum"]);
    Customization schemaProfile;
    auto advertised = juce::JSON::parse(juce::JSON::toString(retainedProfile));
    advertised.getDynamicObject()->setProperty("version", ceiling);
    check(ceiling >= int(retainedProfile["version"]) && schemaProfile.importState(advertised).wasOk() &&
              schemaProfile.muteRemovedSteps,
          "Authoritative MCP profile ceiling admits the actual retained-step profile");
    advertised.getDynamicObject()->setProperty("version", ceiling + 1);
    const auto beforeUnsupported = juce::JSON::toString(schemaProfile.state());
    check(schemaProfile.importState(advertised).failed() &&
              beforeUnsupported == juce::JSON::toString(schemaProfile.state()),
          "One beyond the advertised profile ceiling refuses atomically");
    Customization loaded;
    check(loaded.load(file).wasOk() &&
              juce::JSON::toString(loaded.state()) == juce::JSON::toString(customization->state()) &&
              loaded.macros.back().steps.front().parameters.automationSendTarget == 3 &&
              loaded.macros.back().steps.front().parameters.automationControl == MixerAutomationControl::send,
          "Actual disk reload preserves exact typed stable pair and unrelated settings");
    auto c = std::make_unique<Controller>();
    auto project = Project::empty();
    check(setMixerRoute(project, 1, 3, true).wasOk() && setMixerSend(project, 1, 3, .375).wasOk() &&
              setMixerRoute(project, 2, 3, true).wasOk() && setMixerSend(project, 2, 3, .75).wasOk(),
          "Two genuine model edges have unequal manual targets");
    c->replace(project);
    const auto baseline = c->project.state();
    MacroContext context;
    context.insert = 2;
    check(loaded.runMacro(recipe.id, *c, context).wasOk() &&
              c->project.automation.sources.front().links.front().key ==
                  AutomationKey{AutomationParameter::mixerSend, 1, 3} &&
              c->project.automation.sources.front().points.front().value == .1875f &&
              c->undo.getUndoDescriptions().size() == 1 && c->undoEditChecked().wasOk() &&
              c->project.state().isEquivalentTo(baseline),
          "Real persisted fixed-pair macro ignores selected source, captures exact route and is one Undo");
    const auto redo = c->undo.getRedoDescription();
    check(loaded.runMacro(recipe.id, *c, context, [] { return true; }).failed() &&
              c->project.state().isEquivalentTo(baseline) && c->undo.getRedoDescription() == redo,
          "Actual macro cancellation preserves source baseline and existing Redo");
    auto contextual = recipe;
    contextual.id = "macro.originalContextSendPair";
    contextual.steps.front().useCurrentContext = true;
    check(loaded.addMacro(contextual).wasOk() && loaded.runMacro(contextual.id, *c, context).wasOk() &&
              c->project.automation.sources.front().links.front().key ==
                  AutomationKey{AutomationParameter::mixerSend, 2, 3} &&
              c->project.automation.sources.front().points.front().value == .375f,
          "Explicit current-context source changes only source, retaining exact saved destination");
    const auto &automated = c->project.automation.sources.front();
    const auto choices = mixerAutomationUiChoices(c->project, automated);
    const auto count = c->project.mixer.size();
    std::size_t routes = 0;
    for (const auto &row : c->project.mixer)
        routes += row.routes.size();
    check(choices.size() == 3 * count + routes &&
              choices[0].target.key.kind == AutomationParameter::mixerGain &&
              choices[count].target.key.kind == AutomationParameter::mixerPan &&
              choices[2 * count].target.key.kind == AutomationParameter::mixerStereoSeparation &&
              choices[3 * count].target.key.kind == AutomationParameter::mixerSend,
          "Actual edge choices append after unchanged gain/pan/separation row indices");
    const auto chosen =
        std::find_if(choices.begin(), choices.end(), [](const auto &choice)
                     { return choice.target.key == AutomationKey{AutomationParameter::mixerSend, 2, 3}; });
    check(chosen != choices.end() && chosen->linked && chosen->target.manual == .75f,
          "Send source properties read exact edge manual value and existing link");
    MixerAutomationTarget resolved;
    const MixerAutomationUiIntent intent{"original-project", 41, 1, MixerAutomationControl::send, 3};
    check(intent.validate(c->project, "original-project", 41, resolved).wasOk() &&
              resolved.key == AutomationKey{AutomationParameter::mixerSend, 1, 3} &&
              intent.validate(c->project, "original-project", 42, resolved).failed() &&
              intent.validate(c->project, "other-project", 41, resolved).failed(),
          "Exact UI pair resolves independently of selection and refuses stale project/revision");
    auto missing = c->project;
    check(setMixerRoute(missing, 1, 3, false).wasOk() &&
              intent.validate(missing, "original-project", 41, resolved)
                  .getErrorMessage()
                  .startsWith("automation.missingRoute:"),
          "Removed edge is never replaced by another selected destination");
    const auto untouched = juce::JSON::toString(loaded.state());
    const auto freshState = [&] { return juce::JSON::parse(untouched); };
    const auto lastParameters = [](juce::var &profile)
    {
        auto *macros = profile["macros"].getArray();
        auto *steps = macros->getReference(macros->size() - 1)["steps"].getArray();
        return steps->getReference(0)["parameters"].getDynamicObject();
    };
    for (int version : {1, 2, 3, 4, 5})
    {
        auto old = freshState();
        old.getDynamicObject()->setProperty("version", version);
        check(loaded.importState(old).failed() && juce::JSON::toString(loaded.state()) == untouched,
              "Older settings formats refuse send metadata without partial settings publication");
    }
    for (const juce::var value :
         {juce::var(true), juce::var(.5), juce::var(-1), juce::var(2147483648.0), juce::var("3")})
    {
        auto malformed = freshState();
        lastParameters(malformed)->setProperty("automationSendTarget", value);
        check(loaded.importState(malformed).failed() && juce::JSON::toString(loaded.state()) == untouched,
              "Wrong-typed or out-of-range destination never drops or clamps silently");
    }
    auto malformed = freshState();
    lastParameters(malformed)->removeProperty("automationSendTarget");
    check(loaded.importState(malformed).failed() && juce::JSON::toString(loaded.state()) == untouched,
          "Missing required send destination refuses exact settings");
    malformed = freshState();
    lastParameters(malformed)->setProperty("automationControl", "futureSend");
    check(loaded.importState(malformed).failed() && juce::JSON::toString(loaded.state()) == untouched,
          "Unknown future control is not silently decoded as gain");
    check(customization->removeMacro(recipe.id).wasOk() &&
              juce::JSON::toString(customization->state()) == legacy,
          "Removing only new recipe restores exact prior profile format/settings");
}
} // namespace vibera::test::mixer_macro_configuration
