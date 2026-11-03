// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Customization.h"
#include <limits>

// Original real Customization/profile-file/Controller/history coverage; no app or DSP claim.
static void samplerAutomationMacroTests(const juce::File &folder)
{
    using namespace vibera;
    const auto copy = [](const juce::var &v) { return juce::JSON::parse(juce::JSON::toString(v)); };
    const auto same = [](const juce::var &a, const juce::var &b)
    { return juce::JSON::toString(a) == juce::JSON::toString(b); };
    auto project = Project::empty();
    const auto owner = project.channels.front().id, other = project.channels[1].id;
    Controller controller;
    controller.replace(project);
    CommandArgs create;
    create.channel = owner;
    create.samplerAutomationParameter = 0;
    create.length = 4;
    require(controller.execute("automation.sampler.create", create).wasOk() &&
                controller.project.automation.sources.size() == 1,
            "Real canonical code0 creates owned sampler automation source");
    project = controller.project;
    const auto source = project.automation.sources.front().id;
    controller.replace(project);
    const auto before = controller.project.state();
    CommandArgs initialize;
    initialize.channel = owner;
    initialize.samplerAutomationParameter = 0;
    initialize.value = 1;
    auto range = initialize;
    range.target = source;
    range.beat = 1;
    range.length = 0;
    Macro recipe{
        "macro.sampler-profile17",
        "Original sampler profile17",
        {{"automation.sampler.init", initialize, false}, {"automation.sampler.range", range, false}}};
    auto settings = Customization::familiarDefaults();
    require(settings.addMacro(recipe).wasOk(), "Real typed sampler init/range recipe validates");
    const auto encoded = settings.state();
    require(int(encoded["version"]) == 17, "Sampler presence, including code0, selects actual profile17");
    const auto steps = [&](juce::var &profile) -> juce::Array<juce::var> *
    {
        for (auto &row : *profile["macros"].getArray())
            if (row["id"].toString() == recipe.id)
                return row["steps"].getArray();
        require(false, "Find authored exact sampler macro");
        return nullptr;
    };
    auto inspected = copy(encoded);
    require(steps(inspected)->getReference(0)["parameters"].hasProperty("samplerParameter") &&
                int(steps(inspected)->getReference(0)["parameters"]["samplerParameter"]) == 0 &&
                steps(inspected)->getReference(1)["parameters"].hasProperty("minimum") &&
                steps(inspected)->getReference(1)["parameters"].hasProperty("maximum") &&
                !steps(inspected)->getReference(1)["parameters"].hasProperty("beat") &&
                !steps(inspected)->getReference(1)["parameters"].hasProperty("length"),
            "Code0 remains explicitly present; physical range uses unambiguous minimum/maximum aliases");
    Customization loaded;
    require(loaded.importState(inspected).wasOk() && same(loaded.state(), encoded),
            "Actual profile17 exact encode/import roundtrip");
    const auto file = folder.getChildFile("sampler-automation-profile17.json");
    require(settings.save(file).wasOk() && loaded.load(file).wasOk() && same(loaded.state(), encoded),
            "Real profile17 file save/load retains exact settings and unrelated shortcuts");
    const auto disk = file.loadFileAsString();
    MacroContext context;
    context.channel = other;
    require(loaded.runMacro(recipe.id, controller, context).wasOk() && !controller.hasGesture() &&
                !loaded.running,
            "Real two-operation recipe uses explicit Channel, not different selected context");
    const auto &link = controller.project.automation.sources.front().links.front();
    require(link.key == AutomationKey{AutomationParameter::samplerModulation, owner, 0} &&
                link.minimum == 1 && link.maximum == 0,
            "Exact stable source/Channel/code0 and inverted boolean physical endpoints survive real run");
    const auto after = controller.project.state();
    require(
        !after.isEquivalentTo(before) && controller.undoEditChecked().wasOk() &&
            controller.project.state().isEquivalentTo(before) && !controller.undo.canUndo() &&
            controller.redoEditChecked().wasOk() && controller.project.state().isEquivalentTo(after) &&
            !controller.undo.canRedo(),
        "Actual canonical multi-step sampler macro produces exactly one source/allocator-exact Undo/Redo");

    const auto single = [&](const juce::String &action, const CommandArgs &args, bool current)
    {
        auto result = Customization::familiarDefaults();
        Macro macro{recipe.id, recipe.name, {{action, args, current}}};
        require(result.addMacro(macro).wasOk(), "Prepare genuine supported single sampler recipe");
        return result.state();
    };
    const auto refusal = [&](const juce::var &bad)
    {
        const auto state = loaded.state();
        const auto model = controller.project.state();
        const auto revision = controller.revision;
        require(
            loaded.importState(bad).failed() && same(loaded.state(), state) &&
                controller.project.state().isEquivalentTo(model) && controller.revision == revision &&
                file.loadFileAsString() == disk,
            "Malformed raw sampler profile refuses before coercion, preserving settings/disk/model/history");
    };
    for (const auto *action :
         {"automation.sampler.link", "automation.sampler.unlink", "automation.sampler.range"})
    {
        auto args = range;
        const auto valid = single(action, args, false);
        for (const auto &invalid :
             juce::Array<juce::var>{true, juce::String(source), double(source) + .5, 0, -1, 1000000001.})
        {
            auto bad = copy(valid);
            steps(bad)->getReference(0)["parameters"].getDynamicObject()->setProperty("target", invalid);
            refusal(bad);
        }
        auto bad = copy(valid);
        steps(bad)->getReference(0)["parameters"].getDynamicObject()->removeProperty("target");
        refusal(bad);
        for (const auto &invalid :
             juce::Array<juce::var>{true, juce::String(owner), double(owner) + .5, 0, -1, 1000000001.})
        {
            bad = copy(valid);
            steps(bad)->getReference(0)["parameters"].getDynamicObject()->setProperty("channel", invalid);
            refusal(bad);
        }
        bad = copy(valid);
        steps(bad)->getReference(0)["parameters"].getDynamicObject()->removeProperty("channel");
        refusal(bad);
    }
    const auto validInit = single("automation.sampler.init", initialize, false);
    auto bad = copy(validInit);
    steps(bad)->getReference(0)["parameters"].getDynamicObject()->removeProperty("value");
    refusal(bad);
    for (const auto &invalid : juce::Array<juce::var>{true, "1", std::numeric_limits<double>::infinity(), .5})
    {
        bad = copy(validInit);
        steps(bad)->getReference(0)["parameters"].getDynamicObject()->setProperty("value", invalid);
        refusal(bad);
    }
    for (const auto &invalid : juce::Array<juce::var>{true, "0", .5, -1, 32, 159})
    {
        bad = copy(validInit);
        steps(bad)->getReference(0)["parameters"].getDynamicObject()->setProperty("samplerParameter",
                                                                                  invalid);
        refusal(bad);
    }
    bad = copy(validInit);
    bad.getDynamicObject()->setProperty("version", 16);
    refusal(bad);
    auto zero = initialize;
    zero.value = 0;
    const auto explicitZero = single("automation.sampler.init", zero, false);
    require(loaded.importState(explicitZero).wasOk() &&
                loaded.runMacro(recipe.id, controller, context).wasOk(),
            "Explicit numeric0 value/code0 remain valid, distinct from missing field");

    // Existing generic profile current-context channel0 sentinel remains supported, never coerced.
    auto current = initialize;
    current.channel = 0;
    const auto currentProfile = single("automation.sampler.init", current, true);
    require(loaded.importState(currentProfile).wasOk() && same(loaded.state(), currentProfile),
            "Actual current-context profile roundtrip preserves its numeric inert Channel0 sentinel");
    controller.replace(project);
    context.channel = other;
    require(loaded.runMacro(recipe.id, controller, context).wasOk(),
            "Current-context macro resolves actual runtime Channel");
    const auto found = std::find_if(
        controller.project.automation.initialValues.begin(),
        controller.project.automation.initialValues.end(),
        [&](const auto &value)
        {
            return value.key == AutomationKey{AutomationParameter::samplerModulation, other, 0} &&
                   value.value == 1;
        });
    require(found != controller.project.automation.initialValues.end(),
            "Current context targets other Channel, never encoded sentinel or selected source owner");
    auto omitted = copy(currentProfile);
    steps(omitted)->getReference(0)["parameters"].getDynamicObject()->removeProperty("channel");
    require(loaded.importState(omitted).wasOk(), "Omitted current-context Channel remains admitted");
    for (const auto &invalid : juce::Array<juce::var>{false, "0", .5, -1})
    {
        bad = copy(currentProfile);
        steps(bad)->getReference(0)["parameters"].getDynamicObject()->setProperty("channel", invalid);
        refusal(bad);
    }
    bad = copy(currentProfile);
    steps(bad)->getReference(0).getDynamicObject()->setProperty("currentContext", "true");
    refusal(bad);
    // Dormant unlink/removal remains a genuine owned operation, not a new unsupported assignment.
    auto dormant = project;
    dormant.channel(owner)->audioClip = true;
    dormant.channel(owner)->source = "original-sampler-profile-metadata.wav";
    dormant.channel(owner)->audioDuration = 1;
    require(dormant.validate().wasOk(),
            "Valid original missing-source AudioClip metadata fixture keeps dormant sampler targets");
    controller.replace(dormant);
    const auto dormantBefore = controller.project.state();
    auto unlink = initialize;
    unlink.target = source;
    require(loaded.importState(single("automation.sampler.unlink", unlink, false)).wasOk() &&
                loaded.runMacro(recipe.id, controller, context).wasOk() &&
                controller.project.automation.sources.front().links.empty() &&
                controller.project.automation.initialValues.size() == 1,
            "Actual profile17 unlink removes only exact dormant source link, preserving initialization");
    const auto unlinked = controller.project.state();
    require(controller.undoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(dormantBefore) &&
                controller.redoEditChecked().wasOk() && controller.project.state().isEquivalentTo(unlinked),
            "Dormant unlink has exact canonical history and never revives AudioClip/sample workers");
    require(loaded.importState(single("automation.sampler.init.remove", initialize, false)).wasOk() &&
                loaded.runMacro(recipe.id, controller, context).wasOk() &&
                controller.project.automation.initialValues.empty(),
            "Explicit dormant initialization removal uses existing stable Channel/code0");
    controller.replace(project);
    auto misplaced = unlink;
    misplaced.target = project.automation.placements.front().id;
    require(loaded.importState(single("automation.sampler.unlink", misplaced, false)).wasOk(),
            "Well-typed source identity remains subject to actual runtime owner resolution");
    require(
        loaded.runMacro(recipe.id, controller, context).failed() &&
            controller.project.state().isEquivalentTo(project.state()) && !controller.hasGesture() &&
            !loaded.running && !controller.undo.canUndo(),
        "Placement identity never aliases Automation source; failed macro restores exact document/history");
    for (int version = 1; version <= 16; ++version)
    {
        auto legacy = Customization::familiarDefaults().state();
        legacy.getDynamicObject()->setProperty("version", version);
        Customization previous;
        require(previous.importState(legacy).wasOk(), "Unrelated prior profile remains readable");
    }
}
