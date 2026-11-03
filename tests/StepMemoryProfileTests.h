// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Customization.h"
#include "MacroConfiguration.h"

// Actual application types. Unregistered draft: the dependency-free structural
// probe does not compile/run this test and cannot qualify profile behavior.
static void stepMemoryProfileTests(const juce::File &folder)
{
    using namespace vibera;
    const auto sameProfile = [](const juce::var &a, const juce::var &b)
    { return juce::JSON::toString(a) == juce::JSON::toString(b); };
    auto settings = Customization::familiarDefaults();
    const auto legacy = settings.state();
    require(int(legacy["version"]) < 10 && !legacy.hasProperty("muteRemovedSteps"),
            "Unchanged legacy settings remain readable without unsupported step-memory data");
    settings.muteRemovedSteps = true;
    auto retained = settings.state();
    require(int(retained["version"]) == 10 && retained["muteRemovedSteps"].isBool() &&
                bool(retained["muteRemovedSteps"]),
            "Enabled retained steps require explicit profile10, preventing old-reader silent loss");
    Customization reopened;
    require(reopened.importState(retained).wasOk() && reopened.muteRemovedSteps &&
                sameProfile(reopened.state(), retained),
            "Profile10 preference round trip preserves exact bindings/macros/settings");
    const auto path = folder.getChildFile("step-memory-shortcuts.json");
    require(settings.save(path).wasOk() && reopened.load(path).wasOk() && reopened.muteRemovedSteps,
            "Real atomic profile save/reopen preserves retained-step policy");

    MacroConfigurationSnapshot matching;
    require(readMacroConfiguration(settings, path, matching).wasOk() && matching.exists && matching.matches,
            "Actual shared profile snapshot binds complete settings and persisted bytes");
    auto external = settings;
    external.restoreMuteAfterSolo = !settings.restoreMuteAfterSolo;
    require(external.save(path).wasOk(), "Owned fixture simulates another instance changing saved settings");
    MacroConfigurationSnapshot changedOnDisk;
    require(
        readMacroConfiguration(settings, path, changedOnDisk).wasOk() && changedOnDisk.exists &&
            !changedOnDisk.matches && changedOnDisk.generation != matching.generation &&
            sameProfile(settings.state(), retained),
        "Step-memory setter's existing guard detects external profile edits without changing live settings");
    require(settings.save(path).wasOk(), "Restore only this disposable profile fixture");
    MacroConfigurationSnapshot restoredIdentity;
    require(readMacroConfiguration(settings, path, restoredIdentity).wasOk() && restoredIdentity.matches &&
                restoredIdentity.generation == matching.generation,
            "Exact saved/current profile identity returns only after matching bytes are restored");

    for (int old = 1; old <= 9; ++old)
    {
        auto input = juce::JSON::parse(juce::JSON::toString(legacy));
        input.getDynamicObject()->setProperty("version", old);
        require(reopened.importState(input).wasOk() && !reopened.muteRemovedSteps,
                "Genuine older profile migrates to legacy deletion rather than inherit stale runtime policy");
        const auto before = reopened.state();
        input.getDynamicObject()->setProperty("muteRemovedSteps", false);
        require(reopened.importState(input).failed() && sameProfile(reopened.state(), before),
                "Older-version payload cannot smuggle even false retained-step preference");
    }
    for (const auto invalid : {juce::var(1), juce::var("true"), juce::var()})
    {
        auto input = juce::JSON::parse(juce::JSON::toString(retained));
        input.getDynamicObject()->setProperty("muteRemovedSteps", invalid);
        const auto before = reopened.state();
        require(reopened.importState(input).failed() && sameProfile(reopened.state(), before),
                "Nonboolean preference refuses without changing any imported settings");
    }

    CommandArgs parameters;
    parameters.retainSteps = false;
    Macro action{"macro.retained-step", "Explicit deletion step", {{"steps.set", parameters, true}}};
    auto explicitPolicy = Customization::familiarDefaults();
    require(explicitPolicy.addMacro(action).wasOk(), "Typed false override is present and valid");
    auto encoded = explicitPolicy.state();
    require(int(encoded["version"]) == 10 &&
                encoded["macros"][0]["steps"][0]["parameters"]["retainSteps"].isBool() &&
                !bool(encoded["macros"][0]["steps"][0]["parameters"]["retainSteps"]),
            "False optional override survives serialization and advertises profile10");
    require(reopened.importState(encoded).wasOk() &&
                reopened.macros.front().steps.front().parameters.retainSteps == std::optional<bool>(false),
            "False override remains distinct from omission after profile import");
    for (const auto invalid : {juce::var(0), juce::var("false"), juce::var()})
    {
        auto input = juce::JSON::parse(juce::JSON::toString(encoded));
        input["macros"][0]["steps"][0]["parameters"].getDynamicObject()->setProperty("retainSteps", invalid);
        const auto before = reopened.state();
        require(reopened.importState(input).failed() && sameProfile(reopened.state(), before),
                "Malformed macro flag cannot silently become another musical edit");
    }
    auto masquerade = juce::JSON::parse(juce::JSON::toString(encoded));
    masquerade.getDynamicObject()->setProperty("version", 9);
    masquerade.getDynamicObject()->removeProperty("muteRemovedSteps");
    const auto before = reopened.state();
    require(reopened.importState(masquerade).failed() && sameProfile(reopened.state(), before),
            "Macro retention cannot masquerade as profile9 even when global preference is absent");
    action.id = "macro.invalid-retention";
    action.steps.front().action = "notes.add";
    require(reopened.addMacro(action).failed() && sameProfile(reopened.state(), before),
            "Retention flag belongs only to the explicit step family, including false overrides");
}
