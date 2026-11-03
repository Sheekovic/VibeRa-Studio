// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
#include "Customization.h"
#include "McpContracts.h"
#include "synth/SynthRegistry.h"
#include <limits>

// Original settings/model/schema/history proof; runtime audio is separately qualified.
static void samplerVoiceModeModelTests(const juce::File &folder)
{
    using namespace vibera;
    const SamplerSettings defaults;
    const SamplerSettings oldAggregate{60, 120};
    require(defaults.valid() && !defaults.mono && !defaults.portamento && !defaults.fullPortamento &&
                defaults.slideTime == .06 && defaults.gateBeats == 0 && !oldAggregate.mono &&
                oldAggregate.slideTime == .06 && oldAggregate.gateBeats == 0 &&
                PreparedSamplerSettings(defaults).tuning == 1,
            "Five appended sampler controls preserve neutral defaults/aggregate compatibility/tuning");
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    for (double value : {-1., 10.0001, nan, infinity})
    {
        auto invalid = defaults;
        invalid.slideTime = value;
        require(!invalid.valid(), "Slide settings reject nonfinite/out-of-range seconds");
    }
    for (double value : {-1., 64.0001, nan, infinity})
    {
        auto invalid = defaults;
        invalid.gateBeats = value;
        require(!invalid.valid(), "Gate settings reject nonfinite/out-of-range beats");
    }
    for (double slide : {0., 10.})
        for (double gate : {0., 64.})
        {
            auto boundary = defaults;
            boundary.slideTime = slide;
            boundary.gateBeats = gate;
            boundary.mono = boundary.portamento = boundary.fullPortamento = true;
            require(boundary.valid(), "Inclusive physical endpoints and independent flags remain valid");
        }
    auto source = Project::empty();
    const auto channel = source.channels.front().id;
    const auto pattern = source.patterns.front().id;
    source.patterns.front().autoLength = false;
    source.patterns.front().notes.push_back({source.allocate(), channel, .25, .5, 67, .75f, true});
    SamplerControllerLink link;
    link.id = source.allocate();
    link.channel = channel;
    link.parameter = 17;
    link.port = 1;
    link.cc = 21;
    link.minimum = .01;
    link.maximum = 64;
    source.samplerControllerLinks.push_back(link);
    require(source.validate().wasOk(),
            "Original voice-mode source includes genuine preserved MIDI CC metadata");
    const auto before = source.state();
    const auto sampler = [](juce::ValueTree tree)
    { return tree.getChildWithName("CHANNELS").getChild(0).getChildWithName("SAMPLER"); };
    require(!sampler(before).getChildWithName("VOICE").isValid(),
            "Neutral voice defaults allocate no persisted VOICE tree");
    const char *commands[]{"channel.sampler.mono", "channel.sampler.portamento", "channel.sampler.slideTime",
                           "channel.sampler.gateBeats", "channel.sampler.fullPortamento"};
    Controller controller;
    CommandArgs args;
    args.channel = channel;
    for (int index = 0; index < 5; ++index)
    {
        controller.replace(source);
        args.enabled = true;
        args.value = index == 2 ? .125 : 2.5;
        auto expected = source;
        auto &value = expected.channel(channel)->sampler;
        if (index == 0)
            value.mono = true;
        if (index == 1)
            value.portamento = true;
        if (index == 2)
            value.slideTime = .125;
        if (index == 3)
            value.gateBeats = 2.5;
        if (index == 4)
            value.fullPortamento = true;
        require(
            controller.execute(commands[index], args).wasOk() && controller.dirty &&
                controller.project.state().isEquivalentTo(expected.state()),
            "Canonical voice command changes only its literal field, preserving score/routes/IDs/CC links");
        const auto revision = controller.revision;
        require(controller.execute(commands[index], args).wasOk() && controller.revision == revision,
                "Repeated identical voice command is a no-op");
        require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(before) &&
                    !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                    controller.project.state().isEquivalentTo(expected.state()) && !controller.undo.canRedo(),
                "Each voice command is one actual exact Undo/Redo");
    }
    const auto refuse = [&](const char *command, const CommandArgs &request)
    {
        const auto old = controller.project.state();
        const auto revision = controller.revision;
        const bool dirty = controller.dirty, undo = controller.undo.canUndo(),
                   redo = controller.undo.canRedo();
        require(controller.execute(command, request).failed() &&
                    controller.project.state().isEquivalentTo(old) && controller.revision == revision &&
                    controller.dirty == dirty && controller.undo.canUndo() == undo &&
                    controller.undo.canRedo() == redo,
                "Voice refusal preserves exact Project/allocator/history/dirty/revision");
    };
    for (double value : {-.01, 10.01, nan, infinity})
    {
        args.value = value;
        refuse(commands[2], args);
    }
    for (double value : {-.01, 64.01, nan, infinity})
    {
        args.value = value;
        refuse(commands[3], args);
    }
    args.channel = 999999999;
    args.value = 1;
    refuse(commands[3], args);
    args.channel = channel;
    for (bool audioClip : {false, true})
    {
        auto unsupported = source;
        unsupported.pattern(pattern)->notes.clear();
        auto *target = unsupported.channel(channel);
        if (audioClip)
        {
            target->audioClip = true;
            target->audioDuration = 1;
            target->source = "original-voice-mode-model.wav";
        }
        else
        {
            const auto descriptor = synth::nativeDescription();
            target->instrument = PluginSpec{
                unsupported.allocate(), descriptor.name, descriptor.createXml()->toString(), {}, true};
        }
        controller.replace(unsupported);
        for (const auto *command : commands)
            refuse(command, args);
    }
    controller.replace(source);
    const juce::String owner = "original-sampler-voice-modes";
    require(controller.beginGesture(owner), "Own a real baseline for continuous voice controls");
    args.value = .25;
    require(controller.execute(commands[2], args, owner).wasOk(), "Preview absolute Slide time");
    args.value = .5;
    require(controller.execute(commands[2], args, owner).wasOk() &&
                controller.project.channel(channel)->sampler.slideTime == .5 &&
                controller.cancelGestureChecked(owner).wasOk() &&
                controller.project.state().isEquivalentTo(before) && !controller.dirty &&
                !controller.undo.canUndo(),
            "Repeated previews are absolute; Cancel restores exact musical/CC/history baseline");
    require(controller.beginGesture(owner), "Own accepted voice control group");
    args.enabled = true;
    require(controller.execute(commands[0], args, owner).wasOk(), "Preview Mono without coercing Portamento");
    args.value = 2.5;
    require(controller.execute(commands[3], args, owner).wasOk() &&
                controller.endGestureChecked("Original voice controls", owner).wasOk(),
            "Accept exact grouped voice edit");
    const auto accepted = controller.project.state();
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(before) &&
                !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(accepted),
            "Accepted voice gesture has one exact Undo/Redo");

    auto configured = source;
    auto &voice = configured.channel(channel)->sampler;
    voice.mono = voice.portamento = voice.fullPortamento = true;
    voice.slideTime = .125;
    voice.gateBeats = 2.5;
    const auto originalScore = sequence(source, true, pattern);
    const auto configuredScore = sequence(configured, true, pattern);
    require(originalScore.size() == 1 && configuredScore.size() == 1 && configuredScore[0].beat == .25 &&
                configuredScore[0].length == .5 && configuredScore[0].note == originalScore[0].note &&
                configuredScore[0].channel == channel && configuredScore[0].pitch == 67,
            "Voice controls retain raw score timing/duration/identity; runtime Gate does not rewrite source "
            "notes");
    const auto encoded = configured.state();
    const auto tree = sampler(encoded).getChildWithName("VOICE");
    require(tree.isValid() && tree.getNumProperties() == 5 && tree.getNumChildren() == 0 &&
                bool(tree["mono"]) && bool(tree["portamento"]) && bool(tree["fullPortamento"]) &&
                double(tree["slideTime"]) == .125 && double(tree["gateBeats"]) == 2.5,
            "Configured writer emits exactly the independent flags and physical units");
    Project loaded;
    require(saveProject(configured, folder.getChildFile("sampler-voice-modes.vibra")).wasOk() &&
                loadProject(folder.getChildFile("sampler-voice-modes.vibra"), loaded).wasOk() &&
                loaded.state().isEquivalentTo(encoded),
            "Actual project file preserves exact voice controls and existing CC links");
    const auto intact = loaded.state();
    const auto badTree = [&](juce::ValueTree bad)
    {
        require(Project::parse(bad, loaded).failed() && loaded.state().isEquivalentTo(intact),
                "Malformed/newer VOICE data leaves destination exactly unchanged");
    };
    for (const auto *field : {"mono", "portamento", "fullPortamento"})
        for (juce::var invalid : {juce::var(1), juce::var(1.), juce::var("true"), juce::var()})
        {
            auto bad = encoded.createCopy();
            auto node = sampler(bad).getChildWithName("VOICE");
            node.removeProperty(field, nullptr);
            node.setProperty(field, invalid, nullptr);
            badTree(bad);
        }
    for (const auto *field : {"slideTime", "gateBeats"})
        for (juce::var invalid : {juce::var(true), juce::var("junk"), juce::var(-.001), juce::var(65.),
                                  juce::var(nan), juce::var(infinity)})
        {
            auto bad = encoded.createCopy();
            auto node = sampler(bad).getChildWithName("VOICE");
            node.removeProperty(field, nullptr);
            node.setProperty(field, invalid, nullptr);
            badTree(bad);
        }
    for (const auto *field : {"mono", "portamento", "fullPortamento", "slideTime", "gateBeats"})
    {
        auto missing = encoded.createCopy();
        sampler(missing).getChildWithName("VOICE").removeProperty(field, nullptr);
        badTree(missing);
    }
    auto bad = encoded.createCopy();
    sampler(bad).getChildWithName("VOICE").setProperty("unknown", 0, nullptr);
    badTree(bad);
    bad = encoded.createCopy();
    sampler(bad).getChildWithName("VOICE").appendChild(juce::ValueTree("UNKNOWN"), nullptr);
    badTree(bad);
    bad = encoded.createCopy();
    sampler(bad).appendChild(tree.createCopy(), nullptr);
    badTree(bad);
    bad = encoded.createCopy();
    bad.setProperty("version", 39, nullptr);
    badTree(bad);
    auto legacy = source.state();
    legacy.setProperty("version", 39, nullptr);
    Project migrated;
    require(Project::parse(legacy, migrated).wasOk() && migrated.state().isEquivalentTo(before),
            "Genuine schema39 absence migrates neutral with exact MIDI CC collection and musical fields");
    auto neutralTree = tree.createCopy();
    neutralTree.setProperty("mono", false, nullptr);
    neutralTree.setProperty("portamento", false, nullptr);
    neutralTree.setProperty("fullPortamento", false, nullptr);
    neutralTree.setProperty("slideTime", .06, nullptr);
    neutralTree.setProperty("gateBeats", 0., nullptr);
    sampler(legacy).appendChild(neutralTree, nullptr);
    badTree(legacy); // Presence is newer even when every value is neutral.
    auto explicitNeutral = source.state();
    sampler(explicitNeutral).appendChild(neutralTree.createCopy(), nullptr);
    require(Project::parse(explicitNeutral, migrated).wasOk() && migrated.state().isEquivalentTo(before),
            "Current explicit neutral VOICE reads but canonical writer returns to sparse defaults");

    // Closed wire contracts reject coercion independently from strongly typed C++ args.
    for (int index = 0; index < 5; ++index)
    {
        const bool flag = index == 0 || index == 1 || index == 4;
        auto value = juce::var(new juce::DynamicObject);
        value.getDynamicObject()->setProperty("channel", juce::int64(channel));
        value.getDynamicObject()->setProperty(flag ? "enabled" : "value",
                                              flag ? juce::var(true) : juce::var(.25));
        const auto schema = commandArgumentSchema(commands[index]);
        validateMcpSchema(schema, value);
        require(!bool(schema["additionalProperties"]) && schema["required"].size() == 2,
                "Five canonical wire schemas require stable Channel and exact typed field");
        value.getDynamicObject()->setProperty(flag ? "enabled" : "value",
                                              flag ? juce::var(1) : juce::var(true));
        bool refused = false;
        try
        {
            validateMcpSchema(schema, value);
        }
        catch (const std::exception &)
        {
            refused = true;
        }
        require(refused, "Wire contracts never coerce numeric booleans or boolean physical values");
    }
    auto customization = Customization::familiarDefaults();
    const int previousProfile = int(customization.state()["version"]);
    Macro macro{"macro.original-sampler-voice", "Original voice controls", {}};
    for (int index = 0; index < 5; ++index)
    {
        args.value = index == 2 ? .125 : 2.5;
        args.enabled = true;
        macro.steps.push_back({commands[index], args, false});
    }
    require(customization.addMacro(macro).wasOk() && int(customization.state()["version"]) == previousProfile,
            "Existing scalar macro payloads need no unrelated profile-version bump");
    Customization restored;
    const auto file = folder.getChildFile("sampler-voice-modes-macro.json");
    require(customization.save(file).wasOk() && restored.load(file).wasOk() &&
                juce::JSON::toString(restored.state()) == juce::JSON::toString(customization.state()),
            "Actual macro file retains all five known commands and typed scalar values");
    controller.replace(source);
    MacroContext context;
    context.pattern = pattern;
    context.channel = channel;
    require(restored.runMacro(macro.id, controller, context).wasOk() &&
                controller.project.state().isEquivalentTo(encoded),
            "Real macro runner executes same canonical voice controls");
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(before) &&
                !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(encoded),
            "Five-step scalar macro is one exact Undo/Redo");
    auto invalidMacro = macro;
    invalidMacro.id = "macro.original-sampler-voice-invalid";
    invalidMacro.steps.back() = {commands[3], args, false};
    invalidMacro.steps.back().parameters.value = 65;
    // The internal macro collection is public; runner must still reject a
    // corrupted later step atomically even when the external schema was bypassed.
    restored.macros.push_back(invalidMacro);
    controller.replace(source);
    require(restored.runMacro(invalidMacro.id, controller, context).failed() &&
                controller.project.state().isEquivalentTo(before) && !controller.undo.canUndo() &&
                !controller.hasGesture(),
            "A refused later voice macro step cancels earlier scalar changes without history leakage");
}
