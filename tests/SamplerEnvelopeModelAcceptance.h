// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "SamplerModulationSettings.h"
#include "SamplerModulationMacroInput.h"
#include "synth/SynthRegistry.h"
#include <limits>

// Original canonical model/persistence tests; no DSP/UI/provider qualification.
static void samplerEnvelopeModelTests(const juce::File &folder)
{
    using namespace vibera;
    const SamplerSettings defaults;
    require(defaults.valid() && defaults.modulation.size() == 5 && samplerModulationParameters.size() == 20,
            "Five original modulation targets have20 authoritative parameters");
    const char *targets[]{"volume", "pan", "cutoff", "resonance", "pitch"};
    for (size_t i = 0; i < 5; ++i)
    {
        const auto &s = defaults.modulation[i];
        require(samplerModulationTargets[i] == targets[i] && !s.enabled && !s.tempo && s.delay == 0 &&
                    s.attack == .01 && s.hold == 0 && s.decay == .1 && s.sustain == 1 && s.release == .1 &&
                    s.attackTension == 0 && s.decayTension == 0 && s.releaseTension == 0 && s.amount == 0 &&
                    !s.lfoEnabled && !s.lfoTempo && !s.lfoGlobal && s.lfoDelay == 0 && s.lfoAttack == 0 &&
                    s.lfoPeriod == 1 && s.lfoAmount == 0 && s.lfoShape == 0,
                "Independent literal defaults leave legacy envelope/LFO destinations inactive");
    }
    auto source = Project::empty();
    const auto channel = source.channels.front().id;
    const auto before = source.state();
    for (const auto &parameter : samplerModulationParameters)
    {
        const juce::String command = "channel.sampler.modulation." + juce::String(parameter.id.data());
        require(std::count_if(commandRegistry().begin(), commandRegistry().end(), [&](const auto &entry)
                              { return entry.id == command && entry.documentEdit; }) == 1,
                "Each metadata parameter has exactly one canonical document/history command");
        Controller controller;
        controller.replace(source);
        CommandArgs args;
        args.channel = channel;
        args.text = "volume";
        const auto value = parameter.initial == parameter.maximum ? parameter.minimum : parameter.maximum;
        args.value = value;
        args.enabled = value != 0;
        require(controller.execute(command, args).wasOk() &&
                    samplerModulationValue(controller.project.channel(channel)->sampler.modulation[0],
                                           parameter) == value,
                "Canonical parameter edit uses exact target/metadata value");
        auto expected = source;
        require(
            setSamplerModulationValue(expected.channel(channel)->sampler.modulation[0], parameter, value) &&
                controller.project.state().isEquivalentTo(expected.state()),
            "A parameter edit preserves every other target/Channel/project field and allocator");
        const auto after = controller.project.state();
        const auto revision = controller.revision;
        require(controller.execute(command, args).wasOk() && controller.revision == revision &&
                    controller.project.state().isEquivalentTo(after),
                "Repeated parameter edit is an exact no-op");
        require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(before) &&
                    !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                    controller.project.state().isEquivalentTo(after) && !controller.undo.canRedo(),
                "Each canonical parameter has exactly one full-state Undo/Redo");
        if (!parameter.boolean)
            for (double invalid :
                 {parameter.minimum - 1, parameter.maximum + 1, std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::infinity()})
            {
                const auto unchanged = controller.project.state();
                const auto unchangedRevision = controller.revision;
                args.value = invalid;
                require(controller.execute(command, args).failed() &&
                            controller.revision == unchangedRevision &&
                            controller.project.state().isEquivalentTo(unchanged),
                        "Invalid numeric commands refuse atomically");
            }
    }
    Controller controller;
    controller.replace(source);
    CommandArgs args;
    args.channel = channel;
    args.text = "cutoff";
    args.value = 2;
    const juce::String owner = "sampler-envelope:original-model";
    require(controller.beginGesture(owner) &&
                controller.execute("channel.sampler.modulation.attack", args, owner).wasOk(),
            "Actual envelope gesture owns its immutable original model");
    args.value = 3;
    require(controller.execute("channel.sampler.modulation.attack", args, owner).wasOk() &&
                controller.cancelGestureChecked(owner).wasOk() &&
                controller.project.state().isEquivalentTo(before) && !controller.undo.canUndo(),
            "Cancel restores all settings/history without musical side effects");
    require(controller.beginGesture(owner) &&
                controller.execute("channel.sampler.modulation.attack", args, owner).wasOk() &&
                controller.endGestureChecked("Sampler attack", owner).wasOk(),
            "Accept one owned modulation gesture");
    const auto accepted = controller.project.state();
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(before) &&
                !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(accepted),
            "Accepted gesture is exactly one history action");
    controller.replace(source);
    const auto refuse = [&](const char *command, const CommandArgs &invalid)
    {
        const auto snapshot = controller.project.state();
        const auto revision = controller.revision;
        require(controller.execute(command, invalid).failed() &&
                    controller.project.state().isEquivalentTo(snapshot) && controller.revision == revision &&
                    !controller.undo.canUndo(),
                "Refusal retains actual source/history/revision");
    };
    args.text = "";
    refuse("channel.sampler.modulation.attack", args);
    args.text = "unknown";
    refuse("channel.sampler.modulation.attack", args);
    args.text = "pan";
    args.enabled = true;
    refuse("channel.sampler.modulation.enabled", args);
    args.value = 2;
    refuse("channel.sampler.modulation.attack", args);
    args.enabled = false;
    refuse("channel.sampler.modulation.tempo", args);
    args.enabled = true;
    require(controller.execute("channel.sampler.modulation.lfoEnabled", args).wasOk() &&
                controller.project.channel(channel)->sampler.modulation[1].lfoEnabled &&
                controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(before),
            "Pan exposes genuine LFO commands and their exact Undo while all envelope commands refuse");
    args.text = "volume";
    args.value = 1.5;
    refuse("channel.sampler.modulation.lfoShape", args);
    args.channel = 999999999;
    refuse("channel.sampler.modulation.attack", args);
    args.channel = channel;
    auto unsupported = source;
    const auto description = synth::nativeDescription();
    unsupported.channel(channel)->instrument =
        PluginSpec{unsupported.allocate(), description.name, description.createXml()->toString(), {}, true};
    require(unsupported.validate().wasOk(), "Unsupported native Synth role is a genuine valid model");
    controller.replace(unsupported);
    refuse("channel.sampler.modulation.attack", args);
    unsupported = source;
    unsupported.channel(channel)->audioClip = true;
    unsupported.channel(channel)->audioDuration = 1;
    unsupported.channel(channel)->source = "original-envelope-model.wav";
    require(unsupported.validate().wasOk(), "Unsupported Audio Clip role is a genuine valid model");
    controller.replace(unsupported);
    refuse("channel.sampler.modulation.attack", args);
    auto configured = source;
    for (size_t i = 0; i < 5; ++i)
    {
        auto &s = configured.channel(channel)->sampler.modulation[i];
        s.enabled = i != 1;
        s.tempo = true;
        s.delay = .125;
        s.attack = .25;
        s.hold = .375;
        s.decay = .5;
        s.sustain = .625;
        s.release = .75;
        s.attackTension = -.25;
        s.decayTension = .25;
        s.releaseTension = -.5;
        s.amount = .75;
        s.lfoEnabled = s.lfoTempo = s.lfoGlobal = true;
        s.lfoDelay = .5;
        s.lfoAttack = .25;
        s.lfoPeriod = 2;
        s.lfoAmount = -.625;
        s.lfoShape = 2;
    }
    require(configured.validate().wasOk(),
            "Original nondefault five-target model validates with Pan envelope disabled");
    Project loaded;
    const auto configuredState = configured.state();
    require(Project::parse(configuredState, loaded).wasOk() && loaded.state().isEquivalentTo(configuredState),
            "Schema37 complete nondefault target settings have exact typed round trip");
    const auto file = folder.getChildFile("sampler-envelope-settings.vibra");
    require(saveProject(configured, file).wasOk() && loadProject(file, loaded).wasOk() &&
                loaded.state().isEquivalentTo(configuredState),
            "Real XML transport preserves bool/numeric envelope state exactly");
    const auto sampler = [](juce::ValueTree tree)
    { return tree.getChildWithName("CHANNELS").getChild(0).getChildWithName("SAMPLER"); };
    const auto modulation = [&](juce::ValueTree tree)
    { return sampler(tree).getChildWithName("MODULATION"); };
    auto legacy = source.state();
    legacy.setProperty("version", 36, nullptr);
    for (auto row : legacy.getChildWithName("CHANNELS"))
    {
        auto s = row.getChildWithName("SAMPLER");
        s.removeChild(s.getChildWithName("MODULATION"), nullptr);
    }
    require(Project::parse(legacy, loaded).wasOk() && loaded.state().isEquivalentTo(before),
            "Genuine schema36 absence migrates all five targets to inactive defaults");
    const auto destination = loaded.state();
    const auto invalidState = [&](const juce::ValueTree &state)
    {
        require(Project::parse(state, loaded).failed() && loaded.state().isEquivalentTo(destination),
                "Malformed modulation is refused atomically before replacing actual load destination");
    };
    auto smuggled = legacy.createCopy();
    sampler(smuggled).appendChild(modulation(before).createCopy(), nullptr);
    invalidState(smuggled);
    for (int fault = 0; fault < 11; ++fault)
    {
        auto bad = configuredState.createCopy();
        auto m = modulation(bad);
        auto row = m.getChild(0);
        if (fault == 0)
            sampler(bad).removeChild(m, nullptr);
        if (fault == 1)
            sampler(bad).appendChild(m.createCopy(), nullptr);
        if (fault == 2)
            m.removeChild(0, nullptr);
        if (fault == 3)
            m.getChild(1).setProperty("id", "volume", nullptr);
        if (fault == 4)
            row.setProperty("id", "unknown", nullptr);
        if (fault == 5)
            row.setProperty("unknown", 0, nullptr);
        if (fault == 6)
            row.appendChild(juce::ValueTree("EXTRA"), nullptr);
        if (fault == 7)
            m.setProperty("unknown", true, nullptr);
        if (fault == 8)
            m.getChild(1).setProperty("enabled", true, nullptr);
        if (fault == 9)
            sampler(bad).appendChild(juce::ValueTree("UNKNOWN"), nullptr);
        if (fault == 10)
            bad.getChildWithName("CHANNELS").getChild(0).appendChild(sampler(bad).createCopy(), nullptr);
        invalidState(bad);
    }
    for (const auto &parameter : samplerModulationParameters)
    {
        const juce::Identifier field(juce::String(parameter.id.data()));
        auto missing = configuredState.createCopy();
        modulation(missing).getChild(0).removeProperty(field, nullptr);
        invalidState(missing);
        if (parameter.boolean)
            for (const juce::var invalid : {juce::var(1), juce::var(0), juce::var("true"), juce::var(.5)})
            {
                auto bad = configuredState.createCopy();
                modulation(bad).getChild(0).setProperty(field, invalid, nullptr);
                invalidState(bad);
            }
        else
            for (const juce::var invalid :
                 {juce::var(true), juce::var("not-number"), juce::var(parameter.minimum - 1),
                  juce::var(parameter.maximum + 1), juce::var(std::numeric_limits<double>::infinity())})
            {
                auto bad = configuredState.createCopy();
                modulation(bad).getChild(0).setProperty(field, invalid, nullptr);
                invalidState(bad);
            }
    }
    auto fractional = configuredState.createCopy();
    modulation(fractional).getChild(0).setProperty("lfoShape", 1.25, nullptr);
    invalidState(fractional);
    auto invalidSettings = defaults;
    invalidSettings.modulation[1].enabled = true;
    require(!invalidSettings.valid(), "Settings validity prohibits Pan envelope before preparation");
    invalidSettings = defaults;
    invalidSettings.modulation[2].amount = std::numeric_limits<double>::quiet_NaN();
    require(!invalidSettings.valid(), "Settings validity rejects nonfinite envelope parameters");
    for (const auto input : {"cutoff:-0.5", "resonance:0.25", "pitch:1", "volume:0"})
    {
        CommandArgs parsed;
        require(parseSamplerModulationMacro("channel.sampler.modulation.amount", input, parsed).wasOk() &&
                    samplerModulationTarget(parsed.text.toStdString()) >= 0,
                "Actual Settings macro parser retains explicit target and signed depth");
    }
    for (const auto input : {"cutoff", "cutoff:1junk", "unknown:.5", "pan:.5", "cutoff:nan", "cutoff:2"})
    {
        CommandArgs parsed;
        parsed.text = "untouched";
        parsed.value = .75;
        require(parseSamplerModulationMacro("channel.sampler.modulation.amount", input, parsed).failed() &&
                    parsed.text == "untouched" && parsed.value == .75,
                "Actual Settings macro parser refuses malformed/unsupported depth atomically");
    }
    CommandArgs flag;
    require(parseSamplerModulationMacro("channel.sampler.modulation.lfoEnabled", "pan:1", flag).wasOk() &&
                flag.enabled,
            "Actual Settings macro parser supports typed Pan LFO flag");
}
