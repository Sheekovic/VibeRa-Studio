// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "StepExpression.h"
#include "Customization.h"
#include "McpContracts.h"
#include "ImportedNoteMacroAcceptance.h"
#include <limits>

// Original actual model/history/profile tests. No DSP/provider qualification.
static void stepExpressionModelTests(const juce::File &folder)
{
    importedNoteMacroTests(folder);
    using namespace vibera;
    auto source = Project::empty();
    auto &part = source.patterns.front();
    part.autoLength = false;
    part.length = 4;
    const auto pattern = part.id, channel = source.channels[0].id, other = source.channels[1].id;
    const double beats[] = {0, .25, .25, .5, .75};
    const double values[] = {-.5, .25, .75, 1, -1};
    for (size_t i = 0; i < 5; ++i)
        part.notes.push_back({source.allocate(), channel, beats[i], .5 + i * .25, 60 + int(i),
                              i == 2 ? 0.f : .7f, true, i == 1, values[i], values[i] * 100});
    part.notes.push_back({source.allocate(), channel, 0, 2, 77, .4f, false, false, -.2, 20});
    part.notes.push_back({source.allocate(), channel, 4, .25, 78, .5f, true, true, .3, -30});
    part.notes.push_back({source.allocate(), other, .25, 1, 79, .6f, true, false, -.4, 40});
    part.channelLoops = {{channel, ChannelLoopMode::fixed, 1}};
    source.placements = {{source.allocate(), pattern, 0, 0, 4, 0}, {source.allocate(), pattern, 1, 4, 4, 0}};
    require(source.validate().wasOk(), "Original sampler expression source validates");
    const Note oldNote{1, channel, 0, .25, 60, .8f, true, false};
    const ScheduledNote oldSchedule{channel, 0, .25, 60, .8f, 1, 0, true};
    require(oldNote.pan == 0 && oldNote.fineCents == 0 && oldSchedule.pan == 0 && oldSchedule.fineCents == 0,
            "Old Note and ScheduledNote aggregate initializers remain source-compatible and neutral");
    const auto before = source.state();
    for (bool fine : {false, true})
    {
        const juce::String command = fine ? "steps.fine" : "steps.pan";
        const auto property = fine ? StepExpression::fine : StepExpression::pan;
        const double limit = fine ? 100 : 1;
        const auto *info = stepExpressionInfo(property);
        require(info && juce::String(info->command) == command && info->minimum == -limit &&
                    info->maximum == limit && info->defaultValue == 0 &&
                    juce::String(info->units) == (fine ? "cents" : "normalized") &&
                    !stepExpressionInfo(static_cast<StepExpression>(99)),
                "Authoritative property metadata exposes exact stable command/range/units/default");
        const auto value = [fine](const Note &note) { return fine ? note.fineCents : note.pan; };
        const auto literal = [&](const Project &project, std::initializer_list<double> expected)
        {
            size_t index = 0;
            for (double wanted : expected)
                require(std::abs(value(project.pattern(pattern)->notes[index++]) - wanted * limit) < 1.e-10,
                        "Graph expression matches independently literal signed values and chord columns");
        };
        const auto otherFields = [&](const Project &project)
        {
            auto restored = project;
            for (size_t i = 0; i < 5; ++i)
                (fine ? restored.pattern(pattern)->notes[i].fineCents
                      : restored.pattern(pattern)->notes[i].pan) = values[i] * limit;
            require(restored.state().isEquivalentTo(before),
                    "Expression edits preserve all other note fields, dormant/melodic/foreign data, loops, "
                    "placements and allocator");
        };
        Controller c;
        c.replace(source);
        CommandArgs args;
        args.pattern = pattern;
        args.channel = channel;
        args.value = .5 * limit;
        require(c.execute(command, args).wasOk(), "Canonical sampler expression set uses actual Controller");
        literal(c.project, {.5, .5, .5, .5, .5});
        otherFields(c.project);
        const auto setState = c.project.state();
        const auto setRevision = c.revision;
        require(c.execute(command, args).wasOk() && c.revision == setRevision &&
                    c.project.state().isEquivalentTo(setState),
                "Repeated identical expression set is an exact revision/allocator/history no-op");
        require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before) &&
                    !c.undo.canUndo() && c.redoEditChecked().wasOk() &&
                    c.project.state().isEquivalentTo(setState) && !c.undo.canRedo(),
                "Canonical expression set is one exact Undo/Redo");
        c.replace(source);
        const juce::String owner = "step-expression:original-model";
        require(c.beginGesture(owner), "Acquire real expression gesture baseline");
        args.text = "scale";
        args.target = part.notes.front().id;
        args.value = 0;
        require(c.execute(command, args, owner).wasOk(), "Apply additive signed scale from baseline anchor");
        literal(c.project, {0, .75, 1, 1, -.5});
        args.value = -limit;
        require(c.execute(command, args, owner).wasOk(), "Repeated signed scale does not compound clamping");
        literal(c.project, {-1, -.25, .25, .5, -1});
        otherFields(c.project);
        args.text = "ramp";
        args.beat = .75;
        args.length = -.75;
        args.value = limit;
        args.offset = -limit;
        require(c.execute(command, args, owner).wasOk(),
                "Reverse expression ramp uses musical column direction");
        literal(c.project, {-1, -1. / 3, -1. / 3, 1. / 3, 1});
        args.beat = .25;
        args.length = 0;
        args.offset = -.75 * limit;
        require(c.execute(command, args, owner).wasOk(),
                "Zero-span expression ramp edits complete chord column");
        literal(c.project, {-.5, -.75, -.75, 1, -1});
        otherFields(c.project);
        require(c.cancelGestureChecked(owner).wasOk() && c.project.state().isEquivalentTo(before) &&
                    !c.undo.canUndo() && !c.hasGesture(),
                "Expression gesture cancel restores full source and history");
        args.text = "reset";
        args.value = std::numeric_limits<double>::quiet_NaN();
        require(c.beginGesture(owner) && c.execute(command, args, owner).wasOk() &&
                    c.endGestureChecked("Sampler expression", owner).wasOk(),
                "Neutral reset ignores unused value and commits actual gesture");
        literal(c.project, {0, 0, 0, 0, 0});
        otherFields(c.project);
        const auto reset = c.project.state();
        require(c.undoEditChecked().wasOk() && c.project.state().isEquivalentTo(before) &&
                    !c.undo.canUndo() && c.redoEditChecked().wasOk() &&
                    c.project.state().isEquivalentTo(reset),
                "Accepted expression gesture is exactly one history action");
        const auto refuse = [&](const CommandArgs &invalid, const Project &baseline, Project candidate)
        {
            const auto snapshot = candidate.state();
            require(applyStepExpression(candidate, baseline, invalid, property).failed() &&
                        candidate.state().isEquivalentTo(snapshot),
                    "Direct expression validation is atomic before any property/allocator change");
        };
        args = {};
        args.pattern = pattern;
        args.channel = channel;
        for (double invalid : {-limit - 1, limit + 1, std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()})
        {
            auto bad = args;
            bad.value = invalid;
            refuse(bad, source, source);
            bad = args;
            bad.text = "ramp";
            bad.offset = invalid;
            refuse(bad, source, source);
        }
        for (auto ids : {std::vector<Id>{part.notes[0].id, part.notes[0].id},
                         std::vector<Id>{part.notes[0].id, part.notes[5].id},
                         std::vector<Id>{part.notes[0].id, part.notes[6].id},
                         std::vector<Id>{part.notes[0].id, part.notes[7].id},
                         std::vector<Id>{part.notes[0].id, 999999999}})
        {
            auto bad = args;
            bad.ids = ids;
            refuse(bad, source, source);
        }
        auto stale = source;
        stale.pattern(pattern)->notes[0].beat = .25;
        auto bad = args;
        bad.text = "ramp";
        refuse(bad, stale, source);
        bad.text = "scale";
        bad.target = part.notes[5].id;
        refuse(bad, source, source);
        bad = args;
        bad.text = "ramp";
        bad.length = 4;
        refuse(bad, source, source);
        bad.beat = .1;
        bad.length = .25;
        refuse(bad, source, source);
        bad = args;
        bad.text = "invented";
        refuse(bad, source, source);
        bad = args;
        bad.pattern = 999999999;
        refuse(bad, source, source);
        bad = args;
        bad.channel = 999999999;
        refuse(bad, source, source);

        juce::PluginDescription descriptor;
        descriptor.name = "Original absent expression model instrument";
        descriptor.pluginFormatName = "VST3";
        descriptor.fileOrIdentifier = "original-expression-model.vst3";
        descriptor.isInstrument = true;
        descriptor.numOutputChannels = 2;
        auto unsupported = source;
        unsupported.channel(other)->instrument =
            PluginSpec{unsupported.allocate(), descriptor.name, descriptor.createXml()->toString(), {}, true};
        require(unsupported.validate().wasOk(),
                "Authored absent-provider instrument is a valid model fixture");
        c.replace(unsupported);
        const auto unsupportedBefore = c.project.state();
        args.channel = other;
        require(c.execute(command, args).failed() && c.project.state().isEquivalentTo(unsupportedBefore) &&
                    !c.undo.canUndo(),
                "Canonical instrument target refuses sampler expression without loading a provider");
        auto clip = source;
        clip.channel(channel)->audioClip = true;
        auto clipArgs = args;
        clipArgs.channel = channel;
        refuse(clipArgs, clip, clip); // Missing local sampler capability remains an atomic admission failure.
        Customization settings = Customization::familiarDefaults();
        auto firstEdit = args;
        firstEdit.channel = channel;
        firstEdit.value = limit;
        Macro macro{"macro.original-expression",
                    "Mixed capability atomicity",
                    {{command, firstEdit, false}, {command, args, false}}};
        require(settings.addMacro(macro).wasOk(),
                "Canonical expression commands share existing typed macro admission");
        MacroContext context;
        require(settings.runMacro(macro.id, c, context).failed() &&
                    c.project.state().isEquivalentTo(unsupportedBefore) && !c.undo.canUndo(),
                "Mixed sampler/plugin macro refuses and rolls back earlier sampler edit as one actual "
                "transaction");
        auto schemaArgs = juce::JSON::parse(R"({"value":0,"text":"set"})");
        schemaArgs.getDynamicObject()->setProperty("pattern", juce::int64(pattern));
        schemaArgs.getDynamicObject()->setProperty("channel", juce::int64(channel));
        validateMcpSchema(commandArgumentSchema(command), schemaArgs);
        schemaArgs.getDynamicObject()->setProperty("pitch", 60);
        bool extraRefused = false;
        try
        {
            validateMcpSchema(commandArgumentSchema(command), schemaArgs);
        }
        catch (const McpFailure &)
        {
            extraRefused = true;
        }
        require(extraRefused, "Sampler expression schema rejects unrelated ignored arguments");
    }

    Project loaded;
    require(Project::parse(before, loaded).wasOk() && loaded.state().isEquivalentTo(before),
            "Schema31 stores exact signed note pan/Fine Pitch values and source identity");
    const auto file = folder.getChildFile("sampler-note-expression.vibra");
    require(saveProject(source, file).wasOk() && loadProject(file, loaded).wasOk() &&
                loaded.state().isEquivalentTo(before),
            "Real project persistence retains every expression field");
    auto legacy = before.createCopy();
    legacy.setProperty("version", 30, nullptr);
    stripSamplerFilterForLegacy(legacy);
    for (auto patternTree : legacy.getChildWithName("PATTERNS"))
        for (auto note : patternTree)
            if (note.hasType("NOTE"))
            {
                note.removeProperty("pan", nullptr);
                note.removeProperty("fineCents", nullptr);
            }
    auto neutral = source;
    for (auto &note : neutral.patterns.front().notes)
        note.pan = note.fineCents = 0;
    for (auto note : neutral.state().getChildWithName("PATTERNS").getChild(0))
        if (note.hasType("NOTE"))
            require(!note.hasProperty("pan") && !note.hasProperty("fineCents"),
                    "Neutral expression emits neither optional schema31 attribute");
    require(Project::parse(legacy, loaded).wasOk() && loaded.state().isEquivalentTo(neutral.state()),
            "Genuine schema30 migrates expression only to neutral0 without changing old source data");
    const auto migrated = loaded.state();
    const auto firstNote = [](juce::ValueTree state)
    {
        for (auto note : state.getChildWithName("PATTERNS").getChild(0))
            if (note.hasType("NOTE"))
                return note;
        require(false, "Locate actual NOTE child rather than Channel-loop metadata");
        return juce::ValueTree{};
    };
    for (const auto *field : {"pan", "fineCents"})
    {
        auto smuggled = legacy.createCopy();
        firstNote(smuggled).setProperty(field, 0, nullptr);
        require(Project::parse(smuggled, loaded).failed() && loaded.state().isEquivalentTo(migrated),
                "Old schema cannot smuggle even neutral new note expression fields");
        for (const auto invalid : {juce::var(true), juce::var("garbage"), juce::var(101),
                                   juce::var(std::numeric_limits<double>::quiet_NaN())})
        {
            auto malformed = before.createCopy();
            firstNote(malformed).setProperty(field, invalid, nullptr);
            require(Project::parse(malformed, loaded).failed() && loaded.state().isEquivalentTo(migrated),
                    "Malformed/range-invalid new expression refuses without replacing load destination");
        }
        auto missing = before.createCopy();
        firstNote(missing).removeProperty(field, nullptr);
        auto expected = source;
        if (juce::String(field) == "pan")
            expected.pattern(pattern)->notes.front().pan = 0;
        else
            expected.pattern(pattern)->notes.front().fineCents = 0;
        require(Project::parse(missing, loaded).wasOk() && loaded.state().isEquivalentTo(expected.state()),
                "Schema31 independently defaults each absent optional expression field to neutral0");
        require(Project::parse(legacy, loaded).wasOk(),
                "Restore genuine legacy destination for the next malformed test");
    }
    const auto score = sequence(source, true, pattern);
    require(score.size() == 17,
            "Prepared score retains four sounding loop sources four times plus one foreign event");
    require(sequence(source, false, pattern).size() == 34,
            "Two explicit Song placements retain both copies of the same expression-bearing source score");
    for (const auto &event : score)
    {
        const auto found = std::find_if(part.notes.begin(), part.notes.end(),
                                        [&](const auto &note) { return note.id == event.note; });
        require(
            found != part.notes.end() && event.pan == found->pan && event.fineCents == found->fineCents,
            "Pattern scheduling propagates expression through real repeated source/occurrence identities");
    }
    auto glue = source;
    auto &gluePart = glue.patterns.front();
    gluePart.channelLoops.clear();
    gluePart.notes = {{glue.allocate(), channel, 0, .5, 60, .8f, false, false, -.5, -50},
                      {glue.allocate(), channel, .5, .5, 60, .8f, false, false, .5, 50}};
    CommandArgs join;
    join.pattern = pattern;
    join.channel = channel;
    join.ids = {gluePart.notes[0].id, gluePart.notes[1].id};
    Controller glued;
    glued.replace(glue);
    const auto unequal = glued.project.state();
    require(glued.execute("notes.glue", join).wasOk() && glued.project.state().isEquivalentTo(unequal) &&
                !glued.undo.canUndo(),
            "Glue never discards unequal pan/fine expression at a note boundary");
    gluePart.notes[1].pan = gluePart.notes[0].pan;
    gluePart.notes[1].fineCents = gluePart.notes[0].fineCents;
    glued.replace(glue);
    const auto equal = glued.project.state();
    require(glued.execute("notes.glue", join).wasOk() && glued.project.pattern(pattern)->notes.size() == 1 &&
                glued.project.pattern(pattern)->notes.front().length == 1 &&
                glued.project.pattern(pattern)->notes.front().pan == -.5 &&
                glued.project.pattern(pattern)->notes.front().fineCents == -50 &&
                glued.undoEditChecked().wasOk() && glued.project.state().isEquivalentTo(equal),
            "Equal-expression Glue retains the old union behavior and exact Undo");
}
