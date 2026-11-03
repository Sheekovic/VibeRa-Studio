// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LegacySamplerVoices.h"
#include "../src/Commands.h"
#include "../src/McpContracts.h"
#include "../src/EffectChain.h"
#include <array>
#include <cstring>
#include <stdexcept>

// Original pure Project/Controller/UndoManager acceptance. Descriptor XML and
// opaque bytes are authored absent-provider data, not a native capability proof.
namespace vibera::acceptance::pluginMainIoModel
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
        const auto bytes = [](const char *text)
        { return juce::MemoryBlock(text, std::strlen(text)).toBase64Encoding(); };
        juce::PluginDescription effect;
        effect.name = "Original I/O model effect";
        effect.pluginFormatName = "VST3";
        effect.fileOrIdentifier = "original-main-io-model.vst3";
        effect.numInputChannels = effect.numOutputChannels = 2;
        auto instrument = effect;
        instrument.isInstrument = true;
        auto p = Project::empty();
        const auto fx = p.allocate(), synth = p.allocate();
        p.mixer[1].effects = {{fx, "Effect", effect.createXml()->toString(), bytes("old effect")}};
        p.channels[0].instrument = PluginSpec{synth, "Instrument", instrument.createXml()->toString(),
                                              bytes("old instrument"), true};
        p.plugin(fx)->wet = .375f;
        p.plugin(fx)->bypass = true;
        PluginBinding binding;
        binding.id = 17;
        binding.manual = .25f;
        binding.name = "Preserved binding";
        p.plugin(fx)->automationBindings = {binding};
        check(p.validate().wasOk(), "Authored defaults validate");
        auto legacy = p.state();
        legacy.setProperty("version", 25, nullptr);
        stripPatternExtentForLegacy(legacy);
        auto legacyExpected = p;
        for (auto &pattern : legacyExpected.patterns)
            pattern.autoLength = false;
        Project loaded;
        check(Project::parse(legacy, loaded).wasOk() && loaded.version == Project::currentVersion &&
                  loaded.state().isEquivalentTo(legacyExpected.state()),
              "Genuine schema25 stereo migration");
        check(pluginMainIoFor(*loaded.plugin(fx)) == PluginMainIo{}, "Legacy defaults preserved");
        auto changed = p;
        check(setPluginMainIo(changed, fx, {2, 3, 1, 3}).wasOk() && changed.validate().wasOk(),
              "Mono average and independent output selection validate");
        auto tree = changed.state();
        check(Project::parse(tree, loaded).wasOk() && loaded.state().isEquivalentTo(tree),
              "Selected main I/O roundtrip retains every other field");
        const auto pluginTree = [&](juce::ValueTree root)
        {
            for (const auto &row : root.getChildWithName("MIXER"))
                for (const auto &child : row)
                    if (child.hasType("PLUGIN") && juce::int64(child["id"]) == fx)
                        return child;
            throw std::runtime_error("Original persisted effect missing");
        };
        const auto refuseTree = [&](juce::ValueTree malformed)
        {
            const auto before = loaded.state().toXmlString();
            check(Project::parse(malformed, loaded).failed() && loaded.state().toXmlString() == before,
                  "Malformed encoding leaves output unchanged");
        };
        auto malformed = tree.createCopy();
        malformed.setProperty("version", 25, nullptr);
        refuseTree(malformed);
        for (const auto *field :
             {"mainInputBus", "mainOutputBus", "mainInputChannels", "mainInputMonoSource"})
        {
            malformed = tree.createCopy();
            pluginTree(malformed).removeProperty(field, nullptr);
            refuseTree(malformed);
            malformed = tree.createCopy();
            pluginTree(malformed).setProperty(field, .5, nullptr);
            refuseTree(malformed);
        }
        for (const auto invalid :
             {PluginMainIo{-1, 0, 2, 0}, PluginMainIo{0, 32, 2, 0}, PluginMainIo{0, 0, 1, 0},
              PluginMainIo{0, 0, 2, 1}, PluginMainIo{0, 0, 1, 4}, PluginMainIo{0, 0, 3, 0}})
        {
            const auto before = changed.state().toXmlString();
            check(setPluginMainIo(changed, fx, invalid).failed() && changed.state().toXmlString() == before,
                  "Invalid selection is atomic");
        }
        changed.plugin(fx)->detector = PluginDetectorAssignment{2, 4, 2};
        check(setPluginMainIo(changed, fx, {4, 0, 2, 0}).failed(), "Main and aux bus cannot alias");
        changed.plugin(fx)->mainInputBus = 4;
        check(changed.validate().failed(), "Direct malformed spec also refuses");
        check(!purePluginIoHistory(p, p), "Noop is not structural history");
        auto owner = std::make_unique<Controller>();
        owner->replace(p);
        owner->deliverPendingNotifications();
        const auto snapshot = [&]
        {
            return owner->project.state().toXmlString() + owner->projectIdentity +
                   juce::String(juce::int64(owner->revision)) + juce::String(int(owner->dirty)) +
                   owner->undo.getUndoDescriptions().joinIntoString("|") +
                   owner->undo.getRedoDescriptions().joinIntoString("|");
        };
        const auto commit = [&](std::unique_ptr<PreparedControllerChange> &ticket)
        {
            check(ticket && owner->validatePreparedChange(*ticket).wasOk() &&
                      owner->commitPreparedChange(*ticket),
                  "Actual owned canonical commit");
            ticket.reset();
        };
        for (const auto id : {fx, synth})
        {
            CommandArgs args;
            args.target = id;
            args.pluginIo = PluginMainIo{1, 2, 1, 2};
            auto captured = owner->project;
            captured.plugin(id)->state = bytes("fresh edit state");
            const auto before = snapshot();
            std::unique_ptr<PreparedControllerChange> ticket;
            check(owner->prepareCapturedMixerCommand("plugin.io", args, captured, ticket).wasOk() &&
                      snapshot() == before &&
                      purePluginIoHistory(captured, *ticket->destinationBeforeCommit()),
                  "Instrument/effect prepare is nonmutating and positively I/O-only");
            commit(ticket);
            auto fresh = owner->project;
            fresh.plugin(fx)->state = bytes("fresh unrelated effect state");
            fresh.plugin(synth)->state = bytes("fresh current instrument state");
            check(owner->preparePluginIoHistory(HistoryDirection::undo, fresh, ticket).wasOk() &&
                      ticket->destinationBeforeCommit()->plugin(fx)->state == fresh.plugin(fx)->state &&
                      ticket->destinationBeforeCommit()->plugin(synth)->state == fresh.plugin(synth)->state,
                  "Undo uses current opaque state for both roles");
            commit(ticket);
            check(pluginMainIoFor(*owner->project.plugin(id)) == PluginMainIo{},
                  "Undo restores exact defaults");
            fresh = owner->project;
            fresh.plugin(id)->state = bytes("fresh Redo state");
            check(owner->preparePluginIoHistory(HistoryDirection::redo, fresh, ticket).wasOk(),
                  "Fresh Redo prepares actual history cursor");
            commit(ticket);
            check(pluginMainIoFor(*owner->project.plugin(id)) == *args.pluginIo &&
                      owner->project.plugin(id)->state == bytes("fresh Redo state"),
                  "Redo retains fresh state");
            const auto unchanged = snapshot();
            captured = owner->project;
            captured.plugin(id)->state = bytes("newer noop capture");
            check(owner->prepareCapturedMixerCommand("plugin.io", args, captured, ticket).wasOk() &&
                      ticket->isNoOp() && snapshot() == unchanged,
                  "Noop ignores newer capture without history");
            ticket.reset();
            args.target = 999999;
            check(owner->prepareCapturedMixerCommand("plugin.io", args, owner->project, ticket).failed() &&
                      !ticket && snapshot() == unchanged,
                  "Unknown target leaves all ownership unchanged");
            args.target = id;
            args.pluginIo.reset();
            check(owner->prepareCapturedMixerCommand("plugin.io", args, owner->project, ticket).failed() &&
                      !ticket && snapshot() == unchanged,
                  "Missing typed payload refuses");
            args.pluginIo = PluginMainIo{};
            captured = owner->project;
            captured.tempo += 1;
            check(owner->prepareCapturedMixerCommand("plugin.io", args, captured, ticket).failed() &&
                      !ticket && snapshot() == unchanged,
                  "Capture cannot smuggle music changes");
        }
        auto mixed = owner->project;
        setPluginMainIo(mixed, fx, {});
        mixed.mixer[1].gain = .25f;
        check(!purePluginIoHistory(owner->project, mixed), "Mixed control/I/O history refuses classifier");
        CommandArgs unrelated;
        unrelated.pluginIo = PluginMainIo{};
        check(owner->execute("project.tempo", unrelated).failed(), "Payload cannot leak into other commands");
        const auto schema = commandArgumentSchema("plugin.io");
        check(schema["required"].isArray() && schema["properties"]["pluginIo"].isObject(),
              "Typed shared discovery schema exists");
        // Independent format1 bytes: one default stereo slot, literal float1.
        // No production writer generates the migration oracle.
        std::vector<std::byte> golden;
        for (const auto c : std::string("VibeRaFX"))
            golden.push_back(std::byte(static_cast<unsigned char>(c)));
        const auto u32 = [&](std::uint32_t value)
        {
            for (unsigned shift = 0; shift < 32; shift += 8)
                golden.push_back(std::byte((value >> shift) & 255));
        };
        const auto text = [&](const juce::String &value)
        {
            u32(static_cast<std::uint32_t>(value.getNumBytesAsUTF8()));
            const auto *first = reinterpret_cast<const std::byte *>(value.toRawUTF8());
            golden.insert(golden.end(), first, first + value.getNumBytesAsUTF8());
        };
        u32(1);
        u32(0);
        text("Original");
        u32(1);
        u32(0);
        u32(0x3f800000);
        text("Effect");
        text(effect.createXml()->toString());
        text(bytes("old effect"));
        std::unique_ptr<effect_chain::Chain> chain;
        check(effect_chain::decode(golden, chain).wasOk() && chain->effects.size() == 1 &&
                  chain->effects[0].mainInputBus == 0 && chain->effects[0].mainOutputBus == 0 &&
                  chain->effects[0].mainInputChannels == 2 && chain->effects[0].mainInputMonoSource == 0,
              "Genuine original wire1 preserves stereo defaults");
        std::unique_ptr<juce::MemoryBlock> encoded;
        check(effect_chain::encode(*chain, encoded).wasOk() && encoded->getSize() == golden.size() &&
                  std::memcmp(encoded->getData(), golden.data(), golden.size()) == 0,
              "Default writer preserves old bytes exactly");
        auto &slot = chain->effects[0];
        slot.mainInputBus = 2;
        slot.mainOutputBus = 3;
        slot.mainInputChannels = 1;
        slot.mainInputMonoSource = 1;
        check(effect_chain::encode(*chain, encoded).wasOk() &&
                  static_cast<const unsigned char *>(encoded->getData())[8] == 2,
              "Nondefault selection requires explicit wire2");
        const auto wire = std::span(static_cast<const std::byte *>(encoded->getData()), encoded->getSize());
        std::unique_ptr<effect_chain::Chain> decoded;
        check(effect_chain::decode(wire, decoded).wasOk() && *decoded == *chain,
              "Wire2 retains exact selection, state, wet and order");
        std::unique_ptr<effect_chain::ReplacementPlan> replacement;
        check(effect_chain::prepareReplacement(p, 1, *decoded, p.nextId, replacement).wasOk() &&
                  pluginMainIoFor(replacement->destination.mixer[1].effects[0]) == PluginMainIo{2, 3, 1, 1},
              "Paste proposal keeps selected I/O with new identity");
        auto captureProject = p;
        check(setPluginMainIo(captureProject, fx, {2, 3, 1, 1}).wasOk(), "Capture selection prepared");
        PluginRuntimeState placeholder;
        placeholder.plugin = fx;
        const std::array owners{placeholder};
        std::unique_ptr<effect_chain::CaptureIntent> intent;
        check(effect_chain::beginCapture(captureProject, "original-session", 7, 1, owners, intent).wasOk(),
              "Original missing-provider capture intent");
        effect_chain::CaptureWitness witness;
        witness.current = placeholder;
        const std::array witnesses{witness};
        std::unique_ptr<effect_chain::Clipboard> clipboard;
        check(effect_chain::finishCapture(*intent, captureProject, "original-session", 7, captureProject,
                                          witnesses, clipboard)
                      .wasOk() &&
                  clipboard->chain.effects[0].mainInputBus == 2 &&
                  clipboard->chain.effects[0].mainInputMonoSource == 1,
              "Clipboard capture keeps selection without fabricated native acknowledgement");
        auto broken = std::vector<std::byte>(wire.begin(), wire.end());
        // Exact first slot field offset:8magic+8header+4name length+8name+4count+8flags/wet.
        const std::size_t ioOffset = 8 + 8 + 4 + 8 + 4 + 8;
        broken[ioOffset] = std::byte{32};
        const auto *heldOutput = decoded.get();
        check(effect_chain::decode(broken, decoded).failed() && decoded.get() == heldOutput,
              "Malformed wire2 preserves owning output");
        slot.mainInputMonoSource = 0;
        const auto *heldBytes = encoded.get();
        check(effect_chain::encode(*chain, encoded).failed() && encoded.get() == heldBytes,
              "Invalid codec mapping cannot replace output");
        return juce::Result::ok();
    }
    catch (const std::exception &error)
    {
        return juce::Result::fail(error.what());
    }
}
} // namespace vibera::acceptance::pluginMainIoModel
