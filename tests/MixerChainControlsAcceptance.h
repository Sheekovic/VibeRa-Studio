// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "../src/Commands.h"
#include "../src/HostWetMix.h"
#include "../src/LiveTimeline.h"
#include <limits>
#include <stdexcept>
namespace vibera::acceptance::chainControls
{
inline void rampOracle()
{
    const auto check = [](bool value, const char *message)
    {
        if (!value)
            throw std::runtime_error(message);
    };
    for (const int frames : {240, 221}) // Independent ceil(48k*.005), ceil(44.1k*.005).
    {
        HostWetMix ramp;
        ramp.prepare(frames);
        ramp.setTarget(.25f);
        check(ramp.next() == .25f, "Saved initial wet applies immediately");
        const float target = .25f + float(frames) / 1024;
        ramp.setTarget(target);
        for (int n = 1; n <= frames; ++n)
            check(ramp.next() == .25f + float(n) / 1024, "Exact binary5ms ramp oracle");
        check(ramp.next() == target, "Ramp stops exactly at target");
    }
    HostWetMix ramp;
    ramp.prepare(240);
    ramp.setTarget(.25f);
    ramp.setTarget(.71875f);
    for (int n = 1; n <= 7; ++n)
        check(ramp.next() == .25f + float(n) / 512, "Exact initial partial ramp");
    ramp.setTarget(.498046875f); // Known emitted.263671875 +240/1024.
    for (int n = 1; n <= 240; ++n)
        check(ramp.next() == .263671875f + float(n) / 1024, "Retarget from actual emitted mix");
    check(HostWetMix::render(.75f, .125f, .5f, 0) == .4375f &&
              HostWetMix::render(.75f, .125f, .5f, .25f) == .359375f,
          "Independent linear blend and bypass composition");
    for (const float bypass : {0.f, .125f, .5f, 1.f})
        check(HostWetMix::render(.75f, .125f, 1, bypass) ==
                  (bypass == 1 ? .125f : .75f + (.125f - .75f) * bypass),
              "Exact legacy default bypass arithmetic");
    check(HostWetMix::render(std::numeric_limits<float>::quiet_NaN(), .125f, 0, 0) == .125f &&
              HostWetMix::render(std::numeric_limits<float>::quiet_NaN(), .125f, 1, 1) == .125f,
          "Dry/failure path excludes native NaN");
}
__declspec(noinline) inline juce::Result verify(const juce::File &output)
{
    try
    {
        const auto check = [](bool value, const char *message)
        {
            if (!value)
                throw std::runtime_error(message);
        };
        rampOracle();
        auto source = Project::empty();
        juce::PluginDescription type;
        type.name = "Original missing chain A";
        type.pluginFormatName = "VST3";
        type.fileOrIdentifier = output.getChildFile("absent-chain-fixture.vst3").getFullPathName();
        type.numInputChannels = type.numOutputChannels = 2;
        const auto a = source.allocate(), b = source.allocate(), instrument = source.allocate();
        source.mixer[1].effects.push_back({a, type.name, type.createXml()->toString(), "1...", false, false});
        type.name = "Original missing chain B";
        source.mixer[1].effects.push_back({b, type.name, type.createXml()->toString(), "1.A.", false, true});
        source.mixer[1].effects[0].automationBindings.push_back({17, 1, 0, .25f, "Original binding", "unit"});
        type.isInstrument = true;
        source.channels[0].instrument =
            PluginSpec{instrument, type.name, type.createXml()->toString(), "1...", true, false};
        check(source.validate().wasOk(), "Genuine independent missing-state model fixture");
        auto controller = std::make_unique<Controller>();
        controller->replace(source);
        const auto initial = controller->project.state();
        CommandArgs args;
        args.target = a;
        args.value = .25;
        check(controller->execute("plugin.effect.mix", args).wasOk(), "Canonical host wet mix");
        auto expected = source;
        expected.mixer[1].effects[0].wet = .25f;
        const auto wetState = expected.state();
        check(controller->project.state().isEquivalentTo(wetState), "Only wet changes");
        args.mixer = 1;
        args.enabled = true;
        check(controller->execute("mixer.effects.bypass", args).wasOk(), "Canonical chain bypass");
        expected.mixer[1].effectsBypassed = true;
        const auto changed = expected.state();
        check(controller->project.state().isEquivalentTo(changed),
              "Override retains individual flags, state, bindings, IDs and order");
        const auto revision = controller->revision;
        const auto dirty = controller->dirty;
        const auto undo = controller->undo.getUndoDescription(), redo = controller->undo.getRedoDescription();
        const auto unchanged = [&]
        {
            return controller->revision == revision && controller->dirty == dirty &&
                   controller->undo.getUndoDescription() == undo &&
                   controller->undo.getRedoDescription() == redo &&
                   controller->project.state().isEquivalentTo(changed);
        };
        check(controller->execute("plugin.effect.mix", args).wasOk() && unchanged(), "Same mix exact no-op");
        check(controller->execute("mixer.effects.bypass", args).wasOk() && unchanged(),
              "Redundant chain bypass exact no-op");
        for (const double bad :
             {-1., 1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        {
            args.value = bad;
            check(controller->execute("plugin.effect.mix", args).failed() && unchanged(),
                  "Invalid mix no mutation");
        }
        args.value = .5;
        for (const Id bad : {instrument, source.channels[0].id, Id(999999)})
        {
            args.target = bad;
            check(controller->execute("plugin.effect.mix", args).failed() && unchanged(),
                  "Non-effect target refuses");
        }
        args.mixer = 999999;
        check(controller->execute("mixer.effects.bypass", args).failed() && unchanged(),
              "Unknown insert refuses");
        check(controller->undoEditChecked().wasOk() && controller->project.state().isEquivalentTo(wetState),
              "One chain Undo");
        const auto tail = controller->undo.getRedoDescription();
        const auto rev = controller->revision;
        args.mixer = 1;
        args.enabled = false;
        check(controller->execute("mixer.effects.bypass", args).wasOk() && controller->revision == rev &&
                  controller->undo.getRedoDescription() == tail,
              "No-op retains real redo tail");
        check(controller->redoEditChecked().wasOk() && controller->project.state().isEquivalentTo(changed),
              "Exact chain Redo");
        check(controller->undoEditChecked().wasOk() && controller->undoEditChecked().wasOk() &&
                  controller->project.state().isEquivalentTo(initial),
              "Independent mix and chain Undo records");
        check(controller->beginGesture("chain-fixture"), "Owned gesture");
        args.target = a;
        args.value = .5;
        check(controller->execute("plugin.effect.mix", args, "chain-fixture").wasOk() &&
                  controller->cancelGestureChecked("chain-fixture").wasOk() &&
                  controller->project.state().isEquivalentTo(initial),
              "Owned canonical preview cancellation");
        auto invalidInstrument = source;
        invalidInstrument.channels[0].instrument->wet = .5;
        check(invalidInstrument.validate().failed(),
              "Unsupported instrument mix refused by project validation");
        auto compiled = compileLiveDocument(expected, true, source.patterns[0].id, 1);
        check(compiled && compiled->effect(a) && compiled->effect(a)->wet == .25f &&
                  compiled->effect(a)->bypass && compiled->effect(b)->bypass && !compiled->effect(instrument),
              "Immutable effective targets compile correctly");
        LiveTimeline timeline;
        check(timeline.publish(compiled), "Preowned control publication");
        auto old = timeline.acquire();
        auto newer = expected;
        newer.mixer[1].effects[0].wet = .75f;
        newer.mixer[1].effectsBypassed = false;
        auto next = compileLiveDocument(newer, true, source.patterns[0].id, 2);
        check(timeline.publish(next), "Publish newer targets while old leased");
        auto now = timeline.acquire();
        check(old.document->effect(a)->wet == .25f && old.document->effect(a)->bypass &&
                  now.document->effect(a)->wet == .75f && !now.document->effect(a)->bypass &&
                  now.document->effect(b)->bypass,
              "Independent old/new leased target ownership");
        Project parsed;
        check(Project::parse(changed, parsed).wasOk() && parsed.state().isEquivalentTo(changed),
              "Schema20 exact model roundtrip");
        auto old19 = source.state();
        old19.setProperty("version", 19, nullptr);
        stripPatternExtentForLegacy(old19);
        check(Project::parse(old19, parsed).wasOk() && parsed.version == Project::currentVersion &&
                  parsed.mixer[1].effects[0].wet == 1 && !parsed.mixer[1].effectsBypassed,
              "Genuine19 migration uses defaults");
        for (const bool wetField : {true, false})
        {
            auto disguised = old19.createCopy();
            auto insert = disguised.getChildWithName("MIXER").getChild(1);
            if (wetField)
                insert.getChildWithName("PLUGIN").setProperty("wet", 1., nullptr);
            else
                insert.setProperty("effectsBypassed", false, nullptr);
            const auto unchanged = parsed.state();
            check(Project::parse(disguised, parsed).failed() && parsed.state().isEquivalentTo(unchanged),
                  "Even default-valued new fields cannot masquerade as genuine19");
        }
        const auto parseBefore = parsed.state();
        auto invalid19 = changed.createCopy();
        invalid19.setProperty("version", 19, nullptr);
        check(Project::parse(invalid19, parsed).failed() && parsed.state().isEquivalentTo(parseBefore),
              "New fields under old schema refuse without output mutation");
        for (const juce::var bad :
             {juce::var(-.1), juce::var(1.1), juce::var(true), juce::var("nan"), juce::var(".5junk")})
        {
            auto tree = changed.createCopy();
            tree.getChildWithName("MIXER").getChild(1).getChildWithName("PLUGIN").setProperty("wet", bad,
                                                                                              nullptr);
            check(Project::parse(tree, parsed).failed() && parsed.state().isEquivalentTo(parseBefore),
                  "Strict wet encoding and output ownership");
        }
        for (const juce::var bad : {juce::var(2), juce::var("true"), juce::var("1junk")})
        {
            auto tree = changed.createCopy();
            tree.getChildWithName("MIXER").getChild(1).setProperty("effectsBypassed", bad, nullptr);
            check(Project::parse(tree, parsed).failed() && parsed.state().isEquivalentTo(parseBefore),
                  "Strict boolean chain encoding");
        }
        auto master = std::make_unique<Controller>();
        master->replace(Project::empty());
        CommandArgs masterArgs;
        masterArgs.mixer = 0;
        masterArgs.enabled = true;
        check(master->execute("mixer.effects.bypass", masterArgs).wasOk() &&
                  master->project.mixer.front().effectsBypassed &&
                  master->project.mixer.front().effects.empty(),
              "Empty Master retains explicit chain override");
        masterArgs.text = source.mixer[1].effects[0].descriptor;
        check(master->execute("plugin.effect.insert", masterArgs).wasOk(),
              "Canonical later insertion into bypassed empty chain");
        auto masterDocument = compileLiveDocument(master->project, true, master->project.patterns[0].id, 3);
        const auto inserted = master->project.mixer.front().effects.front().id;
        check(masterDocument && masterDocument->effect(inserted) &&
                  masterDocument->effect(inserted)->bypass && masterDocument->effect(inserted)->wet == 1 &&
                  !master->project.plugin(inserted)->bypass,
              "New slot inherits chain effective override without changing its own defaults");
        check(output.createDirectory().wasOk(), "Owned evidence output");
        const auto file = output.getChildFile("chain-controls.vibra");
        check(!file.exists() && saveProject(expected, file).wasOk() && loadProject(file, parsed).wasOk() &&
                  parsed.state().isEquivalentTo(changed),
              "Real fresh XML save/reopen retains full mix/chain/state");
        check(output.getChildFile("verification.txt")
                  .replaceWithText(
                      "PASS model/history/cancel/schema20/strict19 migration/file roundtrip/immutable "
                      "targets and independent pure ramp. Not actual plugin/graph/native input/listening.\n"),
              "Evidence receipt");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::chainControls
