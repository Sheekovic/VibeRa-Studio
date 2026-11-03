// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "PluginClipModelTests.h"
#include <array>
#include <limits>
#include "PluginStateEncoding.h"

static void pluginBindingAdmissionTests()
{
    PluginParameterCatalogue metadata;
    metadata.generation = 7;
    metadata.reportedCount = 2;
    metadata.parameters = {{0, 1, 3, 0, 0, .4f, .5f, "Gain", "dB"},
                           {0xfedcba98u, 1, 3, 3, 0, 1.f, 0.f, "Mode", ""}};
    const std::vector<PluginBinding> declarations{{0, 1, 0, .21f, "Old display name", "old unit"},
                                                  {0xfedcba98u, 1, 3, .75f, "Mode", ""}};
    auto check = [&](const PluginParameterCatalogue &next, unsigned current, PluginBindingStatus expected,
                     std::uint32_t parameter = 0)
    {
        const auto admitted = admitPluginBindings(declarations, next, current);
        require(admitted.status == expected && admitted.parameter == parameter &&
                    admitted.generation == (expected == PluginBindingStatus::ready ? current : 0),
                "runtime native binding admission is complete or refuses without an active generation");
    };
    check(metadata, 7, PluginBindingStatus::ready);
    auto changed = metadata;
    std::reverse(changed.parameters.begin(), changed.parameters.end());
    changed.parameters.front().name = "Renamed mode";
    changed.parameters.front().label = "different display label";
    check(changed, 7, PluginBindingStatus::ready); // IDs, not names or array order.
    changed.parameters.front().availability = 1;   // Missing default is not missing current value.
    check(changed, 7, PluginBindingStatus::ready);
    check(metadata, 8, PluginBindingStatus::staleCatalogue);
    check(metadata, 0, PluginBindingStatus::staleCatalogue);
    changed = metadata;
    changed.parameters.erase(changed.parameters.begin());
    --changed.reportedCount;
    check(changed, 7, PluginBindingStatus::missingParameter);
    changed = metadata;
    changed.parameters[1].availability = 2;
    check(changed, 7, PluginBindingStatus::unavailableParameter, 0xfedcba98u);
    for (const auto flags : {0u, 3u, 17u, 32769u})
    {
        changed = metadata;
        changed.parameters[1].flags = flags;
        check(changed, 7, PluginBindingStatus::notAutomatable, 0xfedcba98u);
    }
    changed = metadata;
    changed.parameters[1].steps = 4;
    check(changed, 7, PluginBindingStatus::schemaChanged, 0xfedcba98u);
    changed = metadata;
    changed.parameters[1].flags |= 1u << 2;
    check(changed, 7, PluginBindingStatus::schemaChanged, 0xfedcba98u);
    for (int failure = 0; failure < 6; ++failure)
    {
        changed = metadata;
        if (failure == 0)
            changed.error = 1;
        if (failure == 1)
            changed.reportedCount = 3;
        if (failure == 2)
            changed.generation = 0;
        if (failure == 3)
            changed.parameters[1].id = 0;
        if (failure == 4)
            changed.parameters[1].value = std::numeric_limits<float>::quiet_NaN();
        if (failure == 5)
            changed.parameters[1].steps = -1;
        check(changed, 7, PluginBindingStatus::catalogueUnavailable);
    }
    auto invalid = declarations;
    invalid.push_back(invalid.front());
    const auto refused = admitPluginBindings(invalid, metadata, 7);
    require(refused.status == PluginBindingStatus::invalidDeclaration && refused.generation == 0,
            "invalid declaration cannot be admitted despite valid native metadata");
    check(metadata, 7, PluginBindingStatus::ready); // Refusal does not poison later recovery.
}

static void pluginBindingTests(const juce::File &folder)
{
    pluginBindingAdmissionTests();
    std::array<unsigned char, 32> payload{};
    for (int i = 0; i < int(payload.size()); ++i)
        payload[std::size_t(i)] = static_cast<unsigned char>(i * 17);
    for (std::size_t size = 0; size <= payload.size(); ++size)
    {
        const juce::MemoryBlock original(payload.data(), size);
        const auto encoded = original.toBase64Encoding();
        juce::MemoryBlock decoded;
        require(validPluginStateEncoding(encoded) && decodePluginState(encoded, decoded) &&
                    decoded == original,
                "bounded state validator preserves all canonical tail-bit lengths");
    }
    for (const auto *invalid : {"AQID", "-1.", "16777217.", "2147483647.", "999999999999999999999.",
                                "16777216.", ".", "1.A", "1.AAA", "1.A/", "1.A=", "1.++", "0.A"})
    {
        juce::MemoryBlock preserved(payload.data(), 3);
        const auto before = preserved;
        require(!validPluginStateEncoding(invalid) && !decodePluginState(invalid, preserved) &&
                    preserved == before,
                "malformed/oversized state rejected before decode with output preserved");
    }
    Controller controller;
    auto fixture = Project::empty();
    juce::PluginDescription description;
    description.name = "Original missing binding fixture";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = "missing-vibera-binding-fixture.vst3";
    description.numInputChannels = description.numOutputChannels = 2;
    const auto plugin = fixture.allocate();
    const std::array<unsigned char, 3> bytes{1, 2, 3};
    const auto opaque = juce::MemoryBlock(bytes.data(), bytes.size()).toBase64Encoding();
    fixture.mixer[1].effects.push_back(
        {plugin, description.name, description.createXml()->toString(), opaque, false, false});
    const auto fixtureResult = fixture.validate();
    require(fixtureResult.wasOk(),
            "valid missing-plugin binding fixture: " + fixtureResult.getErrorMessage().toStdString());
    controller.replace(fixture);
    const auto before = controller.project.state();
    CommandArgs args;
    args.target = plugin;
    args.binding = PluginBinding{0, 1, 0, .23456789f, "Gain", "dB"};
    const auto declarationResult = controller.execute("plugin.binding.declare", args);
    require(declarationResult.wasOk(),
            "canonical native-zero declaration: " + declarationResult.getErrorMessage().toStdString());
    const auto declared = controller.project.state();
    controller.undoEdit();
    require(before.isEquivalentTo(controller.project.state()), "binding declaration one exact Undo");
    controller.redoEdit();
    require(declared.isEquivalentTo(controller.project.state()), "binding declaration exact Redo");
    require(controller.project.plugin(plugin) &&
                controller.project.plugin(plugin)->automationBindings.size() == 1 &&
                !controller.project.plugin(999999),
            "persistent plugin identity lookup");
    require(controller.execute("plugin.binding.declare", args).failed() &&
                declared.isEquivalentTo(controller.project.state()),
            "duplicate declaration rejects atomically");
    args.binding = PluginBinding{0xfedcba98u, 1, 3, .75f, "Unsigned compatibility target", ""};
    require(controller.execute("plugin.binding.declare", args).wasOk(),
            "native ID preserved without signed narrowing");
    const auto path = folder.getChildFile("plugin-bindings.vibra");
    require(saveProject(controller.project, path).wasOk(), "binding project saved");
    Project loaded;
    require(loadProject(path, loaded).wasOk() && loaded.version == Project::currentVersion &&
                loaded.state().isEquivalentTo(controller.project.state()),
            "binding schema9 disk roundtrip");
    require(loaded.plugin(plugin)->state == opaque &&
                loaded.plugin(plugin)->descriptor == fixture.plugin(plugin)->descriptor,
            "missing plugin retains opaque recovery state and descriptor alongside bindings");
    const auto defaults = loaded.automationDefaults();
    const auto manual = std::find_if(
        defaults.begin(), defaults.end(), [&](const auto &value)
        { return value.key == AutomationKey{AutomationParameter::pluginNormalized, plugin, 0}; });
    require(manual != defaults.end() && manual->value == .23456789f,
            "stored manual fallback uses native key");
    const auto intact = loaded.state();
    const auto bindingsIn = [](juce::ValueTree root)
    {
        return root.getChildWithName("MIXER").getChild(1).getChildWithName("PLUGIN").getChildWithName(
            "AUTOMATION_BINDINGS");
    };
    const auto rejected = [&](const juce::ValueTree &tree)
    {
        const auto previous = loaded.state();
        require(Project::parse(tree, loaded).failed() && previous.isEquivalentTo(loaded.state()),
                "invalid binding parse preserves whole current project");
    };
    for (const auto &id : {juce::var("-1"), juce::var("4294967296"), juce::var("0.5"), juce::var(.5)})
    {
        auto bad = intact.createCopy();
        bindingsIn(bad).getChild(0).setProperty("id", id, nullptr);
        rejected(bad);
    }
    for (const int flags : {0, 3, 17, 32769})
    {
        auto bad = intact.createCopy();
        bindingsIn(bad).getChild(0).setProperty("flags", flags, nullptr);
        rejected(bad);
    }
    for (const double value : {-.1, 1.1, std::numeric_limits<double>::quiet_NaN()})
    {
        auto bad = intact.createCopy();
        bindingsIn(bad).getChild(0).setProperty("manual", value, nullptr);
        rejected(bad);
    }
    auto bad = intact.createCopy();
    bindingsIn(bad).getParent().setProperty("state", "-1.", nullptr);
    rejected(bad);
    bad = intact.createCopy();
    bindingsIn(bad).appendChild(bindingsIn(bad).getChild(0).createCopy(), nullptr);
    rejected(bad);
    bad = intact.createCopy();
    bindingsIn(bad).getChild(0).setProperty("steps", -1, nullptr);
    rejected(bad);
    bad = intact.createCopy();
    bindingsIn(bad).getChild(0).setProperty("unknown", 1, nullptr);
    rejected(bad);
    bad = intact.createCopy();
    bindingsIn(bad).getParent().appendChild(bindingsIn(bad).createCopy(), nullptr);
    rejected(bad);
    bad = intact.createCopy();
    bindingsIn(bad).getParent().removeChild(bindingsIn(bad), nullptr);
    rejected(bad);
    auto legacy = intact.createCopy();
    legacy.setProperty("version", 8, nullptr);
    stripSamplerVoiceFieldsForLegacy(legacy);
    rejected(legacy); // New declarations cannot be smuggled into an old schema.
    bindingsIn(legacy).getParent().removeChild(bindingsIn(legacy), nullptr);
    require(Project::parse(legacy, loaded).wasOk() && loaded.version == Project::currentVersion &&
                loaded.plugin(plugin)->automationBindings.empty() && loaded.plugin(plugin)->state == opaque,
            "actual schema8 plugin without new node migrates losslessly with empty declarations");
    controller.replace(fixture);
    Customization settings;
    require(settings.bind("plugin.binding.declare", Scope::global, juce::KeyPress('Q')).failed(),
            "internal metadata helper is not advertised as a standalone shortcut action");
    Macro macro{"macro.binding", "Store native target", {{"plugin.binding.declare", args, false}}};
    require(settings.addMacro(macro).wasOk(), "semantic binding macro accepted");
    const auto profile = folder.getChildFile("plugin-binding-macro.json");
    require(settings.save(profile).wasOk(), "typed native binding macro saved");
    Customization restored;
    const auto restoredResult = restored.load(profile);
    if (restoredResult.failed() ||
        juce::JSON::toString(settings.state()) != juce::JSON::toString(restored.state()))
    {
        folder.getChildFile("binding-profile-expected.json")
            .replaceWithText(juce::JSON::toString(settings.state()));
        folder.getChildFile("binding-profile-actual.json")
            .replaceWithText(juce::JSON::toString(restored.state()));
    }
    require(restoredResult.wasOk() &&
                juce::JSON::toString(settings.state()) == juce::JSON::toString(restored.state()),
            "binding macro preserves native unsigned ID/capabilities/steps/manual/name/label: " +
                restoredResult.getErrorMessage().toStdString());
    MacroContext context;
    require(restored.runMacro(macro.id, controller, context).wasOk() &&
                controller.project.plugin(plugin)->automationBindings.front() == *args.binding,
            "restored semantic macro stores exact declaration");
    controller.undoEdit();
    require(before.isEquivalentTo(controller.project.state()), "whole binding macro one exact Undo");
    auto badProfile = juce::JSON::parse(juce::JSON::toString(restored.state()));
    badProfile["macros"][0]["steps"][0]["parameters"]["binding"].getDynamicObject()->setProperty("flags", 0);
    const auto priorProfile = juce::JSON::toString(restored.state());
    require(restored.importState(badProfile).failed() &&
                priorProfile == juce::JSON::toString(restored.state()),
            "malformed typed binding macro rejects without replacing preferences");
    require(
        folder.getChildFile("plugin-bindings.txt")
            .replaceWithText(
                "PASS schema9 native target declarations/manual fallback, strict parse/legacy migration, "
                "opaque missing-plugin recovery state, canonical Undo/Redo and typed macro disk roundtrip. "
                "No active plugin clip target/UI or DSP binding claim.\n"),
        "binding evidence saved");
    nativeClipModelTests(folder);
}
