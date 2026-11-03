// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "../src/Commands.h"
#include "../src/MixerRouting.h"
#include <array>
#include <stdexcept>

// Independently authored pure MODEL/codec proof. Descriptors/opaque bytes below
// belong to an absent original model fixture; no Host, worker or graph is run.
namespace vibera::acceptance::mixerDetectorModel
{
__declspec(noinline) inline juce::Result verify()
{
    try
    {
        const auto check = [](bool value, const char *why)
        {
            if (!value)
                throw std::runtime_error(why);
        };
        auto base = Project::empty();
        juce::PluginDescription type;
        type.name = "Original detector model fixture";
        type.pluginFormatName = "VST3";
        type.fileOrIdentifier = "original-absent-detector-model.vst3";
        type.numInputChannels = type.numOutputChannels = 2;
        const std::array<unsigned char, 8> originalBytes{0, 71, 255, 12, 0, 113, 3, 9};
        const auto opaque = juce::MemoryBlock(originalBytes.data(), originalBytes.size()).toBase64Encoding();
        const auto effect = base.allocate();
        auto *target = const_cast<MixerInsert *>(mixerInsert(base, 2));
        target->effects.push_back({effect, type.name, type.createXml()->toString(), opaque, false, true});
        target->effects.back().wet = .375f;
        target->effects.back().automationBindings = {{71, 1, 0, .25f, "Original amount", ""}};
        target->gain = .625f;
        target->pan = -.25f;
        target->muted = target->effectsBypassed = true;
        check(base.validate().wasOk() && base.mixer.size() == 18,
              "Valid original default18 model with nondefault native slot fields");
        const auto baseline = base.state();
        auto expected = base;
        expected.plugin(effect)->detector = PluginDetectorAssignment{1, 31, 2};
        auto assigned = base;
        check(setPluginDetector(assigned, effect, PluginDetectorAssignment{1, 31, 2}).wasOk() &&
                  assigned.state().isEquivalentTo(expected.state()) && assigned.validate().wasOk(),
              "Literal source1/bus31/stereo assignment preserves every unrelated field, opaque byte, wet and "
              "binding");
        check(pluginBusRequestFor(*assigned.plugin(effect)) == PluginBusRequest{31, 2} &&
                  pluginBusRequestFor(*base.plugin(effect)) == PluginBusRequest{},
              "Worker request maps exact saved bus/channels; disconnected retains original default");
        const auto observed = pluginDetectorState(assigned, effect);
        const auto refs = mixerDetectorReferences(assigned, 1);
        check(
            bool(observed["exists"]) && bool(observed["connected"]) && int(observed["ownerMixer"]) == 2 &&
                int(observed["sourceMixer"]) == 1 && int(observed["inputBus"]) == 31 &&
                int(observed["channels"]) == 2 && refs.size() == 1 &&
                juce::int64(refs[0]["effect"]) == effect && int(refs[0]["ownerMixer"]) == 2,
            "Saved reference readback exposes exact stable identities without claiming native availability");
        const auto xml = assigned.state().createXml();
        const auto decodedXml = juce::XmlDocument::parse(xml->toString());
        Project reopened;
        check(decodedXml && Project::parse(juce::ValueTree::fromXml(*decodedXml), reopened).wasOk() &&
                  reopened.state().isEquivalentTo(assigned.state()),
              "Schema24 XML roundtrip retains exact assignment and entire original model");
        check(setPluginDetector(assigned, effect, PluginDetectorAssignment{1, 1, 1}).wasOk() &&
                  pluginBusRequestFor(*assigned.plugin(effect)) == PluginBusRequest{1, 1},
              "Bus1 mono detector is separately valid");
        check(setPluginDetector(assigned, effect, std::nullopt).wasOk() &&
                  assigned.state().isEquivalentTo(baseline),
              "Clearing only the assignment restores literal original model including opaque/control state");

        // Genuine schema23 shape: no detector child and no schema30 extent fields.
        auto legacy = base.state();
        legacy.setProperty("version", 23, nullptr);
        stripPatternExtentForLegacy(legacy);
        auto legacyBaseline = base;
        for (auto &pattern : legacyBaseline.patterns)
            pattern.autoLength = false;
        check(Project::parse(legacy, reopened).wasOk() && reopened.version == Project::currentVersion &&
                  !reopened.plugin(effect)->detector &&
                  reopened.state().isEquivalentTo(legacyBaseline.state()) && int(legacy["version"]) == 23,
              "Old23 no-detector document migrates in memory only; input tree is unchanged");
        const auto pluginTree = [&](juce::ValueTree tree)
        {
            for (auto row : tree.getChildWithName("MIXER"))
                for (auto child : row)
                    if (child.hasType("PLUGIN") && juce::int64(child["id"]) == effect)
                        return child;
            return juce::ValueTree{};
        };
        const auto good = expected.state();
        const auto refuseTree = [&](const juce::ValueTree &tree)
        {
            auto output = base;
            check(Project::parse(tree, output).failed() && output.state().isEquivalentTo(baseline),
                  "Malformed/old-schema detector refuses without changing output owner/model");
        };
        {
            auto tree = good.createCopy();
            tree.setProperty("version", 23, nullptr);
            refuseTree(tree);
        }
        {
            auto tree = legacy.createCopy();
            pluginTree(tree).setProperty("detector", "ignored-new-field", nullptr);
            refuseTree(tree);
        }
        {
            auto master = base;
            const auto id = master.allocate();
            auto *row = const_cast<MixerInsert *>(mixerInsert(master, 0));
            row->effects.push_back({id, type.name, type.createXml()->toString(), opaque, false, false});
            check(setPluginDetector(master, id, PluginDetectorAssignment{1, 1, 2}).wasOk() &&
                      master.validate().wasOk(),
                  "Non-Master detector may feed an effect on Master without fabricating a reverse send");
        }
        {
            auto invalid = *base.plugin(effect);
            invalid.detector = PluginDetectorAssignment{1, 0, 3};
            check(!pluginBusRequestFor(invalid).valid(),
                  "Invalid declared layout is never silently converted to disconnected request");
        }
        for (int mode = 0; mode < 12; ++mode)
        {
            auto tree = good.createCopy();
            auto plugin = pluginTree(tree), detector = plugin.getChildWithName("DETECTOR");
            check(detector.isValid(), "Expected literal persisted DETECTOR child exists");
            switch (mode)
            {
            case 0:
                detector.setProperty("inputBus", 0, nullptr);
                break;
            case 1:
                detector.setProperty("inputBus", 32, nullptr);
                break;
            case 2:
                detector.setProperty("channels", 3, nullptr);
                break;
            case 3:
                detector.setProperty("sourceMixer", 1.5, nullptr);
                break;
            case 4:
                detector.setProperty("channels", true, nullptr);
                break;
            case 5:
                detector.setProperty("inputBus", "NaN", nullptr);
                break;
            case 6:
                detector.removeProperty("sourceMixer", nullptr);
                break;
            case 7:
                detector.setProperty("unknown", 1, nullptr);
                break;
            case 8:
                plugin.appendChild(detector.createCopy(), nullptr);
                break;
            case 9:
                detector.appendChild(juce::ValueTree("EXTRA"), nullptr);
                break;
            case 10:
                detector.setProperty("sourceMixer", 999999, nullptr);
                break;
            case 11:
                detector.setProperty("sourceMixer", 2, nullptr);
                break;
            }
            refuseTree(tree);
        }
        for (const auto bad : {PluginDetectorAssignment{0, 1, 2},
                               {2, 1, 2},
                               {999999, 1, 2},
                               {1, 0, 2},
                               {1, 32, 2},
                               {1, 1, 0},
                               {1, 1, 3}})
        {
            auto unchanged = base;
            check(
                setPluginDetector(unchanged, effect, bad).failed() &&
                    unchanged.state().isEquivalentTo(baseline),
                "Master/self/missing source and malformed bus/channel request preserve exact supplied model");
        }
        {
            auto instrument = base;
            auto instrumentType = type;
            instrumentType.isInstrument = true;
            const auto id = instrument.allocate();
            instrument.channels.front().instrument = PluginSpec{
                id, "Original instrument", instrumentType.createXml()->toString(), opaque, true, false};
            check(instrument.validate().wasOk(),
                  "Independent valid instrument model before detector refusal");
            const auto before = instrument.state();
            check(setPluginDetector(instrument, id, PluginDetectorAssignment{1, 1, 2}).failed() &&
                      instrument.state().isEquivalentTo(before),
                  "Only exact effect owner accepts detector, never an instrument");
            instrument.channels.front().instrument->detector = PluginDetectorAssignment{1, 1, 2};
            check(instrument.validate().failed(),
                  "Direct in-memory instrument detector is rejected by Project validation");
        }
        {
            auto disguised = base;
            auto instrumentType = type;
            instrumentType.isInstrument = true;
            disguised.plugin(effect)->descriptor = instrumentType.createXml()->toString();
            const auto before = disguised.state();
            check(setPluginDetector(disguised, effect, PluginDetectorAssignment{1, 1, 2}).failed() &&
                      disguised.state().isEquivalentTo(before),
                  "Detector target cannot hide an instrument descriptor behind an effect flag");
        }
        {
            auto cycle = base;
            check(setMixerRoute(cycle, 2, 1, true).wasOk(), "Independent audible2-to1 edge initially legal");
            const auto before = cycle.state();
            check(setPluginDetector(cycle, effect, PluginDetectorAssignment{1, 1, 2}).failed() &&
                      cycle.state().isEquivalentTo(before),
                  "Detector1-to2 cannot close existing audible feedback");
        }
        {
            auto cycle = expected;
            cycle.plugin(effect)->wet = 0;
            const auto before = cycle.state();
            check(cycle.validate().wasOk() && canMixerRoute(cycle, 2, 1).failed() &&
                      setMixerRoute(cycle, 2, 1, true).failed() && cycle.state().isEquivalentTo(before),
                  "Ordinary route command respects muted/bypassed/wet0 detector edge before mutation");
            const auto other = cycle.allocate();
            auto *source = const_cast<MixerInsert *>(mixerInsert(cycle, 1));
            source->effects.push_back({other, type.name, type.createXml()->toString(), opaque, false, true});
            source->effects.back().wet = 0;
            source->muted = source->effectsBypassed = true;
            const auto both = cycle.state();
            check(setPluginDetector(cycle, other, PluginDetectorAssignment{2, 1, 1}).failed() &&
                      cycle.state().isEquivalentTo(both),
                  "Detector-only cycle also refuses under both mute/bypass overrides");
        }
        {
            auto owner = std::make_unique<Controller>();
            owner->replace(expected);
            const auto revision = owner->revision;
            CommandArgs remove;
            remove.mixer = 1;
            remove.mixerDelete = MixerInsertDeletePolicy{3, true, true, true};
            std::unique_ptr<PreparedControllerChange> ticket;
            check(owner->prepareMixerInsertCommand("mixer.insert.delete", remove, owner->project, ticket)
                          .failed() &&
                      !ticket && owner->revision == revision &&
                      owner->project.state().isEquivalentTo(expected.state()) && !owner->undo.canUndo(),
                  "Explicit current deletion permissions cannot silently redirect/prune detector references");
        }
        return juce::Result::ok();
    }
    catch (const std::exception &e)
    {
        return juce::Result::fail(juce::String("detector MODEL: ") + e.what());
    }
}
} // namespace vibera::acceptance::mixerDetectorModel
