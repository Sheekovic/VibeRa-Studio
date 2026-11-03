// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "AutomationState.h"
#include "SamplerAutomationParameters.h"
#include "synth/SynthRegistry.h"
#include <limits>

// Original model/compiler/persistence proof only. No automation DSP/UI claim.
static void samplerModulationAutomationModelTests(const juce::File &folder)
{
    using namespace vibera;
    static_assert(unsigned(AutomationParameter::mixerSend) == 6 &&
                  unsigned(AutomationParameter::samplerModulation) == 7);
    auto project = Project::empty();
    const auto owner = project.channels.front().id;
    project.channel(owner)->sampler.modulation[0].attack = 2;
    project.channel(owner)->sampler.modulation[0].lfoPeriod = .25;
    project.channel(owner)->sampler.modulation[0].lfoShape = 2;
    project.channel(owner)->sampler.modulation[2].amount = -.25;
    AutomationSource source;
    source.id = project.allocate();
    source.length = 4;
    source.name = "Original sampler automation";
    source.points = {{project.allocate(), 0, 0}, {project.allocate(), 4, 1}};
    // Independent golden codes: Volume attack/period, cutoff amount, Volume flag/shape.
    for (std::uint32_t code : {3u, 17u, 75u, 0u, 19u})
    {
        const auto parameter = *lookupSamplerAutomationParameter(code);
        source.links.push_back({{AutomationParameter::samplerModulation, owner, code},
                                float(parameter.minimum),
                                float(parameter.maximum)});
    }
    project.automation.sources.push_back(source);
    project.automation.placements.push_back({project.allocate(), source.id, 2, 4, 0});
    require(project.validate().wasOk(),
            "Stable Channel-owned sampler links validate without plugin bindings");
    const auto periodKey = AutomationKey{AutomationParameter::samplerModulation, owner, 17};
    const auto flagKey = AutomationKey{AutomationParameter::samplerModulation, owner, 0};
    const auto defaults = project.automationDefaults();
    unsigned samplerDefaults = 0;
    for (const auto &value : defaults)
        if (value.key.kind == AutomationParameter::samplerModulation)
            ++samplerDefaults;
    require(samplerDefaults == 5,
            "Only five referenced sampler defaults are prepared, not88 rows per unused Channel");
    AutomationProgram program;
    std::string error;
    require(AutomationProgram::compile(project.automation, true, program, error, defaults) &&
                program.targetCount() == 5 && program.valueAt(periodKey, 1) == .25f &&
                program.valueAt(periodKey, 2) == .01f && program.valueAt(periodKey, 4) == 32.005f &&
                program.valueAt(periodKey, 6) == 64.f && program.valueAt(flagKey, 4) == .5f,
            "Song compiler preserves manual-before-clip, literal physical endpoints/midpoint and held end; "
            "discrete curve quantization is not prematurely baked");
    double physical = -99;
    const auto period = *lookupSamplerAutomationParameter(17);
    require(samplerAutomationFloatToPhysical(period, *program.valueAt(periodKey, 2), physical) &&
                physical == .01,
            "Compiled float LFO minimum maps to exact canonical .01 without epsilon");
    AutomationProgram pattern;
    require(AutomationProgram::compile(project.automation, false, pattern, error, defaults) &&
                pattern.targetCount() == 0,
            "Existing Pattern-mode automation policy remains unchanged");
    auto initialized = project;
    initialized.automation.initialValues = {{periodKey, .01f}, {flagKey, 1.f}};
    require(initialized.validate().wasOk() &&
                AutomationProgram::compile(initialized.automation, true, pattern, error,
                                           initialized.automationDefaults()) &&
                pattern.valueAt(periodKey, 1) == .01f && pattern.valueAt(flagKey, 1) == 1.f,
            "Explicit sampler initial values override actual manual settings with legal float endpoints");
    Project restored;
    const auto file = folder.getChildFile("sampler-modulation-automation-model.vibra");
    require(
        saveProject(initialized, file).wasOk() && loadProject(file, restored).wasOk() &&
            restored.state().isEquivalentTo(initialized.state()),
        "Current38 project disk roundtrip preserves exact source IDs, physical ranges and float initials");
    const auto intact = restored.state();
    unsigned refusedCases = 0;
    const auto refuse = [&](juce::ValueTree tree)
    {
        ++refusedCases;
        require(Project::parse(tree, restored).failed() && restored.state().isEquivalentTo(intact),
                "Malformed or old-schema sampler automation preserves the destination atomically (case " +
                    std::to_string(refusedCases) + ")");
    };
    auto malformed = intact.createCopy();
    malformed.setProperty("version", 37, nullptr);
    refuse(malformed);
    auto initialOnly = Project::empty();
    initialOnly.automation.initialValues.push_back({periodKey, .01f});
    require(initialOnly.validate().wasOk(), "Sampler initialized-only target derives its manual default");
    malformed = initialOnly.state();
    malformed.setProperty("version", 37, nullptr);
    refuse(malformed);
    // Genuine37 with no kind7 remains valid; no sampler MODULATION removal is required.
    auto neutral = Project::empty();
    auto legacy = neutral.state();
    legacy.setProperty("version", 37, nullptr);
    Project migrated;
    require(Project::parse(legacy, migrated).wasOk() && migrated.state().isEquivalentTo(neutral.state()),
            "Genuine schema37 absence migrates without losing any existing sampler settings");
    const auto firstLink = [](juce::ValueTree tree)
    {
        return tree.getChildWithName("AUTOMATION")
            .getChildWithName("SOURCES")
            .getChild(0)
            .getChildWithName("LINKS")
            .getChild(0);
    };
    for (auto code : {20, 32, 43, 160})
    {
        malformed = intact.createCopy();
        firstLink(malformed).setProperty("parameter", code, nullptr);
        refuse(malformed);
    }
    for (juce::var invalid : {juce::var(true), juce::var(7.5), juce::var("7junk"), juce::var(8)})
    {
        malformed = intact.createCopy();
        firstLink(malformed).setProperty("kind", invalid, nullptr);
        refuse(malformed);
    }
    for (juce::int64 missing : {juce::int64(-1), juce::int64(0), juce::int64(999999999)})
    {
        malformed = intact.createCopy();
        firstLink(malformed).setProperty("owner", missing, nullptr);
        refuse(malformed);
    }
    for (juce::var invalid : {juce::var(true), juce::var("false"), juce::var(-1.),
                              juce::var(std::numeric_limits<double>::quiet_NaN())})
    {
        malformed = intact.createCopy();
        firstLink(malformed).setProperty("minimum", invalid, nullptr);
        refuse(malformed);
    }
    // Strict input validation occurs before float narrowing, including values that round legal.
    for (auto value : {.5, .999999999, 1.000000001})
    {
        malformed = intact.createCopy();
        malformed.getChildWithName("AUTOMATION")
            .getChildWithName("INITIAL_VALUES")
            .getChild(1)
            .setProperty("value", value, nullptr);
        refuse(malformed);
    }
    malformed = intact.createCopy();
    auto periodLink = malformed.getChildWithName("AUTOMATION")
                          .getChildWithName("SOURCES")
                          .getChild(0)
                          .getChildWithName("LINKS")
                          .getChild(1);
    periodLink.setProperty("minimum", double(std::nextafter(.01f, -std::numeric_limits<float>::infinity())),
                           nullptr);
    refuse(malformed);
    malformed = intact.createCopy();
    auto invalidInitial =
        malformed.getChildWithName("AUTOMATION").getChildWithName("INITIAL_VALUES").getChild(0);
    const auto belowEncoding = std::nextafter(double(.01f), -std::numeric_limits<double>::infinity());
    // JUCE var compares doubles within epsilon when replacing existing properties.
    // Remove first so this precise one-ULP negative fixture actually reaches the parser.
    invalidInitial.removeProperty("value", nullptr);
    invalidInitial.setProperty("value", belowEncoding, nullptr);
    require(invalidInitial["value"].isDouble() && double(invalidInitial["value"]) == belowEncoding &&
                double(invalidInitial["value"]) != double(.01f),
            "Malformed fixture retains the exact one-double-ULP value below the float endpoint encoding");
    refuse(malformed);
    for (std::uint32_t badCode : {20u, 32u, 160u})
    {
        auto invalid = project.automation;
        invalid.sources.front().links.front().key.parameter = badCode;
        const auto oldCount = program.targetCount();
        const auto oldValue = program.valueAt(periodKey, 4);
        require(!AutomationProgram::compile(invalid, true, program, error, defaults) &&
                    program.targetCount() == oldCount && program.valueAt(periodKey, 4) == oldValue,
                "Invalid compiler key preserves the actual prior prepared program");
    }
    auto invalid = initialized;
    invalid.automation.initialValues.front().value =
        std::nextafter(.01f, -std::numeric_limits<float>::infinity());
    require(invalid.validate().failed(), "Adjacent out-of-domain stored float initial is never legalized");
    invalid = initialized;
    invalid.automation.initialValues.back().value = .5f;
    require(invalid.validate().failed(),
            "Stored fractional boolean initial is not a curve interpolation sample");
    auto dormant = project;
    const auto description = synth::nativeDescription();
    dormant.channel(owner)->instrument =
        PluginSpec{dormant.allocate(), description.name, description.createXml()->toString(), {}, true};
    require(dormant.validate().wasOk() && Project::parse(dormant.state(), restored).wasOk() &&
                restored.automation.sources.front().links.size() == 5,
            "A genuine native instrument role preserves recoverable sampler curves without plugin bindings");
    dormant = project;
    dormant.channel(owner)->audioClip = true;
    dormant.channel(owner)->source = "original-sampler-automation.wav";
    dormant.channel(owner)->audioDuration = 1;
    require(dormant.validate().wasOk() && Project::parse(dormant.state(), restored).wasOk() &&
                restored.state().isEquivalentTo(dormant.state()),
            "AudioClip role preserves dormant sampler ownership/settings/curves exactly");
    // A model-only VST3 descriptor is inert metadata, not provider qualification.
    dormant = project;
    juce::PluginDescription vst;
    vst.name = "Original dormant model fixture";
    vst.pluginFormatName = "VST3";
    vst.fileOrIdentifier = "original-dormant-model.vst3";
    vst.isInstrument = true;
    vst.numOutputChannels = 2;
    dormant.channel(owner)->instrument =
        PluginSpec{dormant.allocate(), vst.name, vst.createXml()->toString(), {}, true};
    require(dormant.validate().wasOk() && Project::parse(dormant.state(), restored).wasOk() &&
                restored.state().isEquivalentTo(dormant.state()),
            "VST3 role preserves dormant curves as model metadata without loading or applying them");
    // Canonical deletion uses existing real history cleanup, not a detached test mutator.
    Controller controller;
    auto deletionSource = initialized;
    const AutomationKey survivor{AutomationParameter::samplerModulation, project.channels.back().id, 17};
    deletionSource.automation.sources.front().links.push_back({survivor, .01f, 64.f});
    deletionSource.automation.initialValues.push_back({survivor, .5f});
    controller.replace(deletionSource);
    const auto beforeDelete = controller.project.state();
    CommandArgs args;
    args.channel = owner;
    require(controller.execute("channel.delete", args).wasOk() && !controller.project.channel(owner) &&
                controller.project.automation.sources.size() == 1 &&
                controller.project.automation.sources.front().links.size() == 1 &&
                controller.project.automation.sources.front().links.front().key == survivor &&
                controller.project.automation.sources.front().points.size() == 2 &&
                controller.project.automation.placements.size() == 1 &&
                controller.project.automation.initialValues.size() == 1 &&
                controller.project.automation.initialValues.front().key == survivor,
            "Deleting exact Channel removes only owned links/initials, preserving foreign "
            "targets/shapes/placements");
    const auto afterDelete = controller.project.state();
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(beforeDelete) &&
                controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(afterDelete),
            "Actual canonical delete Undo/Redo restores complete sampler ownership and automation history");
}
