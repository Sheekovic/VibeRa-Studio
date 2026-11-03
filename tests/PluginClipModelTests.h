// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <limits>
static void nativeClipModelTests(const juce::File &folder)
{
    Controller controller;
    auto project = Project::empty();
    juce::PluginDescription description;
    description.name = "Original unavailable native clip fixture";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = folder.getChildFile("absent-native-clip.vst3").getFullPathName();
    description.numInputChannels = description.numOutputChannels = 2;
    const auto plugin = project.allocate();
    project.mixer[1].effects.push_back(
        {plugin, description.name, description.createXml()->toString(), {}, false, false});
    controller.replace(project);
    const auto before = controller.project.state();
    CommandArgs create;
    create.target = plugin;
    create.binding = PluginBinding{0, 1, 0, .37f, "Gain", "dB"};
    create.beat = 2;
    create.length = 4;
    create.lane = 2;
    require(controller.execute("automation.plugin.create", create).wasOk(), "native clip canonical creation");
    const auto created = controller.project.state();
    const AutomationKey key{AutomationParameter::pluginNormalized, plugin, 0};
    const auto &source = controller.project.automation.sources.back();
    require(source.links.size() == 1 && source.links[0].key == key && source.links[0].maximum == 1 &&
                source.points[0].value == .37f && source.points[1].value == .37f &&
                controller.project.plugin(plugin)->automationBindings.size() == 1,
            "one operation declares exact native-zero target and normalized flat curve");
    controller.undoEdit();
    require(before.isEquivalentTo(controller.project.state()),
            "native clip declaration and initialization one Undo");
    controller.redoEdit();
    require(created.isEquivalentTo(controller.project.state()), "native clip exact Redo");
    Project restored;
    const auto file = folder.getChildFile("native-clip-model.vibra");
    require(saveProject(controller.project, file).wasOk() && loadProject(file, restored).wasOk() &&
                created.isEquivalentTo(restored.state()),
            "active native clip plus missing-plugin descriptor roundtrip");
    require(controller.execute("automation.plugin.init.remove", create).wasOk(),
            "remove native initialized control");
    require(controller.project.automation.initialValues.empty(),
            "native initialization removed without deleting curve");
    AutomationProgram compiled;
    std::string error;
    require(AutomationProgram::compile(controller.project.automation, true, compiled, error,
                                       controller.project.automationDefaults()) &&
                compiled.pluginLanes(plugin).size() == 1 &&
                compiled.pluginLanes(plugin)[0].valueAt(0) == .37f,
            "saved manual fallback applies before a later native placement without explicit initialization");
    require(AutomationProgram::compile(controller.project.automation, false, compiled, error,
                                       controller.project.automationDefaults()) &&
                compiled.targetCount() == 0,
            "Playlist native curves do not hijack Pattern playback");
    create.value = .68;
    require(controller.execute("automation.plugin.init", create).wasOk() &&
                controller.project.automation.initialValues[0].key == key &&
                controller.project.automation.initialValues[0].value == float(create.value),
            "native initialization uses normalized current value");
    const auto intact = controller.project.state();
    auto bad = create;
    bad.binding->flags = 0;
    require(controller.execute("automation.plugin.create", bad).failed() &&
                intact.isEquivalentTo(controller.project.state()),
            "non-automatable native clip refuses atomically");
    bad = create;
    bad.binding->steps = 3;
    require(controller.execute("automation.plugin.create", bad).failed() &&
                intact.isEquivalentTo(controller.project.state()),
            "changed native schema refuses existing binding");
    bad = create;
    bad.binding->manual = std::numeric_limits<float>::quiet_NaN();
    require(controller.execute("automation.plugin.create", bad).failed() &&
                intact.isEquivalentTo(controller.project.state()),
            "malformed native value cannot partially create source");
    auto missing = controller.project;
    missing.automation.sources[0].links[0].key.parameter = 99;
    require(missing.validate().failed(), "active native key must have a saved matching declaration");
    missing = controller.project;
    missing.mixer[1].effects.clear();
    require(missing.validate().failed(), "deleted native owner cannot leave falsely active project links");
    require(folder.getChildFile("native-clip-model.txt")
                .replaceWithText(
                    "PASS native clip canonical create/init/remove-init, exact grouped Undo/Redo, normalized "
                    "manual fallback, missing-plugin descriptor/active-curve roundtrip, Pattern independence "
                    "and atomic invalid/stale/undeclared target refusals. Model evidence only.\n"),
            "native model evidence saved");
}
