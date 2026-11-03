// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "Commands.h"
#include "SamplerFilterSettings.h"
#include "synth/SynthRegistry.h"
#include <limits>

// Original CONTROL model/migration tests, not filter DSP qualification.
static void samplerFilterModelTests(const juce::File &folder)
{
    using namespace vibera;
    const auto approximatelyEqual = [](double actual, double expected)
    { return std::abs(actual - expected) < 1.e-9; };
    const char *names[]{"Off", "LP12", "LP24", "HP12", "BP12", "BS12"};
    require(samplerFilterTypeCount == 6 && samplerFilterDefaultType == 1 && samplerFilterDefaultCutoff == 1 &&
                samplerFilterDefaultResonance == 0,
            "Original manual filter metadata has six exact types and neutral legacy defaults");
    for (int type = 0; type < 6; ++type)
        require(juce::String(samplerFilterTypeName(type)) == names[type],
                "Filter type IDs have stable literal names");
    require(juce::String(samplerFilterTypeName(-1)) == "Unknown" &&
                juce::String(samplerFilterTypeName(6)) == "Unknown",
            "Invalid filter metadata never masquerades as Off");
    require(approximatelyEqual(samplerFilterCutoffHz(0), 20) &&
                approximatelyEqual(samplerFilterCutoffHz(.5), 632.4555320336759) &&
                approximatelyEqual(samplerFilterCutoffHz(1), 20000) &&
                approximatelyEqual(samplerFilterCutoffHz(1, 44100), 19845) &&
                approximatelyEqual(samplerFilterCutoffHz(1, 32000), 14400) &&
                approximatelyEqual(samplerFilterCutoffHz(1, 96000), 20000) &&
                approximatelyEqual(samplerFilterCutoffHz(.75), 3556.5588200778453),
            "Independent literal cutoff frequencies follow logarithmic range and actual-rate cap");
    require(approximatelyEqual(samplerFilterQ(0), .7071067811865476) &&
                approximatelyEqual(samplerFilterQ(.5), 6.353553390593274) &&
                approximatelyEqual(samplerFilterQ(1), 12),
            "Independent literal resonance Q endpoints and midpoint");
    const auto nan = std::numeric_limits<double>::quiet_NaN(),
               infinity = std::numeric_limits<double>::infinity();
    require(
        samplerFilterCutoffHz(nan) == 0 && samplerFilterCutoffHz(1, 0) == 0 &&
            samplerFilterCutoffHz(1, infinity) == 0 && samplerFilterQ(nan) == samplerFilterMinimumQ &&
            samplerFilterCutoffHz(-1) == 20 && samplerFilterCutoffHz(2) == 20000,
        "Defensive metadata queries cannot propagate nonfinite coefficients or claim an unavailable rate");
    SamplerSettings defaults;
    const SamplerSettings oldAggregate{60, 120};
    require(defaults.valid() && defaults.filterType == 1 && defaults.filterCutoff == 1 &&
                defaults.filterResonance == 0 && oldAggregate.filterType == 1 &&
                oldAggregate.filterCutoff == 1 && oldAggregate.filterResonance == 0 &&
                PreparedSamplerSettings(defaults).tuning == 1,
            "Existing aggregates and neutral sampler tuning retain source-compatible defaults");
    for (int type : {-1, 6, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
    {
        auto invalid = defaults;
        invalid.filterType = type;
        require(!invalid.valid(), "Invalid filter type cannot enter prepared settings");
    }
    for (double value : {-.01, 1.01, nan, infinity})
    {
        auto invalid = defaults;
        invalid.filterCutoff = value;
        require(!invalid.valid(), "Invalid cutoff refuses in SamplerSettings");
        invalid = defaults;
        invalid.filterResonance = value;
        require(!invalid.valid(), "Invalid resonance refuses in SamplerSettings");
    }

    auto project = Project::empty();
    auto &part = project.patterns.front();
    part.autoLength = false;
    part.length = 4;
    const auto pattern = part.id, channel = project.channels.front().id;
    part.notes = {{project.allocate(), channel, .25, .5, 60, .7f, true, false, -.25, 25, 50, 10, .5}};
    auto &settings = project.channels.front().sampler;
    settings.root = 62;
    settings.cents = 37;
    settings.start = .1;
    settings.end = .9;
    settings.reverse = true;
    settings.normalize = true;
    settings.cut = 3;
    settings.cutBy = 4;
    settings.maxPoly = 11;
    settings.sourceTempo = 100;
    require(project.validate().wasOk(),
            "Original nonneutral sampler source with independent note expression validates");
    const auto before = project.state();
    Controller controller;
    controller.replace(project);
    CommandArgs args;
    args.channel = channel;
    args.value = 1;
    const auto initialRevision = controller.revision;
    require(controller.execute("channel.sampler.filterType", args).wasOk() &&
                controller.revision == initialRevision && !controller.undo.canUndo(),
            "Same default type is an exact no-op");
    args.value = 4;
    require(controller.execute("channel.sampler.filterType", args).wasOk(), "Canonical BP12 type edit");
    args.value = .25;
    require(controller.execute("channel.sampler.filterCutoff", args).wasOk(),
            "Canonical normalized cutoff edit");
    args.value = .5;
    require(controller.execute("channel.sampler.filterResonance", args).wasOk(),
            "Canonical normalized resonance edit");
    const auto edited = controller.project.state();
    const auto &changed = controller.project.channel(channel)->sampler;
    require(changed.filterType == 4 && changed.filterCutoff == .25 && changed.filterResonance == .5,
            "Canonical model retains exact requested controls without unit conversion or clamping");
    auto normalized = controller.project;
    auto &reset = normalized.channel(channel)->sampler;
    reset.filterType = 1;
    reset.filterCutoff = 1;
    reset.filterResonance = 0;
    require(normalized.state().isEquivalentTo(before),
            "Manual filter controls preserve all other source/score/route/allocator fields");
    for (int edit = 0; edit < 3; ++edit)
        require(controller.undoEditChecked().wasOk(), "Each manual filter command has actual Undo");
    require(controller.project.state().isEquivalentTo(before) && !controller.undo.canUndo(),
            "Three separate filter edits exactly restore baseline");
    for (int edit = 0; edit < 3; ++edit)
        require(controller.redoEditChecked().wasOk(), "Each manual filter command has actual Redo");
    require(controller.project.state().isEquivalentTo(edited), "Exact filter Redo");
    const auto refuse = [&](const char *command, double value)
    {
        args.value = value;
        const auto revision = controller.revision;
        const auto state = controller.project.state();
        const bool undo = controller.undo.canUndo(), redo = controller.undo.canRedo();
        require(controller.execute(command, args).failed() && controller.revision == revision &&
                    controller.project.state().isEquivalentTo(state) && controller.undo.canUndo() == undo &&
                    controller.undo.canRedo() == redo,
                "Invalid canonical filter input preserves exact model/history/cursor");
    };
    for (double value : {-1., .5, 6., nan, infinity})
        refuse("channel.sampler.filterType", value);
    for (double value : {-.01, 1.01, nan, infinity})
    {
        refuse("channel.sampler.filterCutoff", value);
        refuse("channel.sampler.filterResonance", value);
    }
    args.channel = 999999999;
    refuse("channel.sampler.filterCutoff", .5);
    args.channel = channel;
    require(controller.undoEditChecked().wasOk() && controller.undo.canRedo(),
            "Retain actual Redo branch before filter refusal");
    refuse("channel.sampler.filterType", .5);
    require(controller.redoEditChecked().wasOk() && controller.project.state().isEquivalentTo(edited),
            "Filter refusal never consumes the retained Redo branch");

    controller.replace(project);
    const juce::String owner("original-sampler-filter");
    require(controller.beginGesture(owner), "Own continuous filter model gesture");
    args.value = .2;
    require(controller.execute("channel.sampler.filterCutoff", args, owner).wasOk(), "Preview cutoff.2");
    args.value = .4;
    require(controller.execute("channel.sampler.filterCutoff", args, owner).wasOk() &&
                controller.project.channel(channel)->sampler.filterCutoff == .4,
            "Repeated filter previews are absolute normalized controls");
    require(controller.cancelGestureChecked(owner).wasOk() &&
                controller.project.state().isEquivalentTo(before) && !controller.undo.canUndo(),
            "Filter gesture Cancel restores exact baseline without history");
    require(controller.beginGesture(owner), "Own accepted filter gesture");
    require(controller.execute("channel.sampler.filterCutoff", args, owner).wasOk(), "Accepted cutoff");
    args.value = .6;
    require(controller.execute("channel.sampler.filterResonance", args, owner).wasOk() &&
                controller.endGestureChecked("Manual filter", owner).wasOk(),
            "Accept grouped filter controls into one history action");
    const auto gesture = controller.project.state();
    require(controller.undoEditChecked().wasOk() && controller.project.state().isEquivalentTo(before) &&
                !controller.undo.canUndo() && controller.redoEditChecked().wasOk() &&
                controller.project.state().isEquivalentTo(gesture),
            "Filter gesture exact one UndoRedo");
    for (bool audioClip : {false, true})
    {
        auto unsupported = project;
        unsupported.pattern(pattern)->notes.clear();
        auto *target = unsupported.channel(channel);
        if (audioClip)
        {
            target->audioClip = true;
            target->audioDuration = 1;
            target->source = "original-filter-model.wav";
        }
        else
        {
            const auto description = synth::nativeDescription();
            target->instrument = PluginSpec{
                unsupported.allocate(), description.name, description.createXml()->toString(), {}, true};
        }
        controller.replace(unsupported);
        const auto state = controller.project.state();
        const auto revision = controller.revision;
        for (const auto *command : {"channel.sampler.filterType", "channel.sampler.filterCutoff",
                                    "channel.sampler.filterResonance"})
        {
            args.value = juce::String(command).endsWith("filterType") ? 2 : .5;
            require(controller.execute(command, args).failed() && controller.revision == revision &&
                        controller.project.state().isEquivalentTo(state) && !controller.undo.canUndo(),
                    "Manual sampler filter commands refuse native/plugin instruments and Audio Clip Channels "
                    "atomically");
        }
    }

    Project loaded;
    const auto file = folder.getChildFile("manual-sampler-filter.vibra");
    controller.replace(project);
    args.value = 5;
    require(controller.execute("channel.sampler.filterType", args).wasOk(), "BS12 persisted model type");
    args.value = .375;
    require(controller.execute("channel.sampler.filterCutoff", args).wasOk(), "Persisted cutoff");
    args.value = .875;
    require(controller.execute("channel.sampler.filterResonance", args).wasOk(), "Persisted resonance");
    const auto persisted = controller.project.state();
    require(saveProject(controller.project, file).wasOk() && loadProject(file, loaded).wasOk() &&
                loaded.state().isEquivalentTo(persisted),
            "Actual schema35 file retains exact manual filter settings and source fields");
    const auto sampler = [](juce::ValueTree tree)
    { return tree.getChildWithName("CHANNELS").getChild(0).getChildWithName("SAMPLER"); };
    for (auto row : persisted.getChildWithName("CHANNELS"))
    {
        const auto s = row.getChildWithName("SAMPLER");
        require(s.hasProperty("filterType") && s.hasProperty("filterCutoff") &&
                    s.hasProperty("filterResonance"),
                "Current writer emits all three filter attributes on every Channel");
    }
    auto legacy = project.state();
    legacy.setProperty("version", 34, nullptr);
    stripSamplerModulationForLegacy(legacy);
    for (auto row : legacy.getChildWithName("CHANNELS"))
        for (const auto *field : {"filterType", "filterCutoff", "filterResonance"})
            row.getChildWithName("SAMPLER").removeProperty(field, nullptr);
    require(Project::parse(legacy, loaded).wasOk() && loaded.state().isEquivalentTo(before),
            "Genuine old34 absence migrates to LP12/cutoff1/resonance0 without changing raw source");
    const auto retained = loaded.state();
    for (const auto *field : {"filterType", "filterCutoff", "filterResonance"})
    {
        auto missing = persisted.createCopy();
        sampler(missing).removeProperty(field, nullptr);
        require(Project::parse(missing, loaded).failed() && loaded.state().isEquivalentTo(retained),
                "Current incomplete filter snapshot refuses atomically");
        auto smuggled = legacy.createCopy();
        sampler(smuggled).setProperty(field, 0, nullptr);
        require(Project::parse(smuggled, loaded).failed() && loaded.state().isEquivalentTo(retained),
                "Old schema refuses filter presence even neutral0");
    }
    for (const auto value : {juce::var(true), juce::var("junk"), juce::var(-1), juce::var(.5), juce::var(6),
                             juce::var(nan), juce::var(infinity)})
    {
        auto bad = persisted.createCopy();
        sampler(bad).setProperty("filterType", value, nullptr);
        require(Project::parse(bad, loaded).failed() && loaded.state().isEquivalentTo(retained),
                "Malformed type never coerces or publishes partial source");
    }
    for (const auto *field : {"filterCutoff", "filterResonance"})
        for (const auto value : {juce::var(true), juce::var("junk"), juce::var(-.01), juce::var(1.01),
                                 juce::var(nan), juce::var(infinity)})
        {
            auto bad = persisted.createCopy();
            sampler(bad).setProperty(field, value, nullptr);
            require(Project::parse(bad, loaded).failed() && loaded.state().isEquivalentTo(retained),
                    "Malformed normalized filter data never coerces or replaces destination");
        }
    auto explicitXmlNumbers = persisted.createCopy();
    sampler(explicitXmlNumbers).setProperty("filterType", "5", nullptr);
    sampler(explicitXmlNumbers).setProperty("filterCutoff", "0.375", nullptr);
    sampler(explicitXmlNumbers).setProperty("filterResonance", "0.875", nullptr);
    require(Project::parse(explicitXmlNumbers, loaded).wasOk() && loaded.state().isEquivalentTo(persisted),
            "Exact XML numeric attributes remain supported; this is not JSON boolean/string coercion");
}
