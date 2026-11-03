// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Customization.h"
#include <stdexcept>

// Original pure customization/Controller test; native I/O macros are explicitly
// unsupported. No worker, file-write, capture or asynchronous graph claim.
namespace vibera::acceptance::pluginMainIoMacro
{
__declspec(noinline) inline juce::Result verify()
{
    try
    {
        const auto check = [](bool value, const char *message)
        {
            if (!value)
                throw std::runtime_error(message);
        };
        auto settings = Customization::familiarDefaults();
        CommandArgs tempo;
        tempo.value = 137;
        Macro ordinary{"macro.io-boundary", "Original ordinary control", {{"project.tempo", tempo, false}}};
        check(settings.addMacro(ordinary).wasOk(), "Existing ordinary macro remains supported");
        const auto profile = settings.state();
        const auto before = juce::JSON::toString(profile);
        Customization restored;
        check(restored.importState(profile).wasOk() && restored.macros.size() == 1 &&
                  restored.macros[0].steps[0].parameters.value == 137,
              "Unchanged genuine supported profile roundtrip");
        check(!Customization::macroAllowed("plugin.io"),
              "Native asynchronous I/O is explicitly unavailable to macros");
        CommandArgs io;
        io.target = 17;
        io.pluginIo = PluginMainIo{1, 2, 1, 3};
        Macro unsupported = ordinary;
        unsupported.steps.push_back({"plugin.io", io, false});
        check(settings.addMacro(unsupported).failed() && juce::JSON::toString(settings.state()) == before,
              "Whole macro replacement refuses before settings publication");
        unsupported.steps.back().action = "project.tempo";
        check(settings.addMacro(unsupported).failed() && juce::JSON::toString(settings.state()) == before,
              "Payload on unrelated step cannot be silently discarded");
        // Imported object must refuse even null or malformed payload, before
        // decodeArgs loses presence. Earlier valid steps must not be published.
        for (const auto payload :
             {juce::var(), juce::var(5), juce::JSON::parse("{\"inputBus\":1}"),
              juce::JSON::parse("{\"inputBus\":1,\"outputBus\":2,\"inputChannels\":1,\"monoSource\":3}")})
        {
            auto bad = juce::JSON::parse(before);
            auto &steps = *bad["macros"].getArray()->getReference(0)["steps"].getArray();
            auto step = juce::JSON::parse(juce::JSON::toString(steps[0]));
            step["parameters"].getDynamicObject()->setProperty("pluginIo", payload);
            steps.add(step);
            const auto original = juce::JSON::toString(restored.state());
            check(restored.importState(bad).failed() && juce::JSON::toString(restored.state()) == original,
                  "Unsupported raw profile payload refuses atomically");
        }
        auto badAction = juce::JSON::parse(before);
        badAction["macros"]
            .getArray()
            ->getReference(0)["steps"]
            .getArray()
            ->getReference(0)
            .getDynamicObject()
            ->setProperty("action", "plugin.io");
        check(restored.importState(badAction).failed(),
              "Missing I/O payload does not make macro action legal");
        // Direct internal vector mutation is also rejected at runtime BEFORE
        // the first ordinary step or gesture, not by rollback after mutation.
        settings.macros[0] = ordinary;
        settings.macros[0].steps.push_back({"plugin.io", io, false});
        check(settings.state()["macros"][0]["steps"][1]["parameters"]["pluginIo"]["monoSource"] ==
                  juce::var(3),
              "Direct invalid storage readback retains payload rather than dropping data");
        Controller owner;
        owner.replace(Project::empty());
        owner.deliverPendingNotifications();
        const auto source = owner.project.state().toXmlString();
        const auto revision = owner.revision;
        const auto dirty = owner.dirty;
        int calls = 0;
        owner.applicationCommand = [&](const juce::String &, const CommandArgs &,
                                       const juce::String &) -> std::optional<juce::Result>
        {
            ++calls;
            return std::nullopt;
        };
        MacroContext context;
        context.pattern = owner.project.patterns[0].id;
        const auto initialPattern = context.pattern;
        check(settings.runMacro(ordinary.id, owner, context).failed() && !owner.hasGesture() &&
                  !settings.running && calls == 0 && owner.project.state().toXmlString() == source &&
                  owner.revision == revision && owner.dirty == dirty && !owner.undo.canUndo() &&
                  context.pattern == initialPattern,
              "Whole sequence refuses before canonical/native work");
        settings.macros[0].steps.back().action = "project.tempo";
        check(settings.runMacro(ordinary.id, owner, context).failed() && calls == 0 &&
                  owner.project.state().toXmlString() == source,
              "Direct malformed unrelated payload also refuses before any step");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::pluginMainIoMacro
