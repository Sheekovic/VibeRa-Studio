// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginControlRequests.h"
#include "PluginParameterState.h"
#include "Commands.h"
#include "synth/SynthRegistry.h"
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>

static void synthScalarPatchHistoryTests()
{
    const auto presets = synth::factoryPresets();
    const auto sampled = std::find_if(presets.begin(), presets.end(), [](const auto &preset)
                                      { return preset.source == synth::SourceKind::sample; });
    const auto electronic = std::find_if(presets.begin(), presets.end(), [](const auto &preset)
                                         { return preset.source == synth::SourceKind::synthesis; });
    require(sampled != presets.end() && electronic != presets.end(),
            "Synth history has actual sampled and electronic factory metadata");
    const auto normalized = [&](const synth::FactoryPreset &preset)
    {
        synth::Values values{};
        for (const auto &spec : synth::parameterSpecs())
        {
            const auto range = synth::synthParameterRange(spec);
            const auto native = preset.values[synth::parameterIndex(spec.id)];
            values[synth::parameterIndex(spec.id)] = range.convertTo0to1(range.snapToLegalValue(native));
        }
        return values;
    };
    const auto state = [](const synth::Values &values)
    {
        auto binary = synth::makeSynthNativeState(values);
        auto xml = juce::AudioProcessor::getXmlFromBinary(binary.getData(), int(binary.getSize()));
        auto *overlay = xml->createNewChildElement("VibeRaParameterValues");
        overlay->setAttribute("version", 1);
        for (std::size_t index = 0; index < synth::parameterCount; ++index)
        {
            auto *entry = overlay->createNewChildElement("PARAM");
            entry->setAttribute("id", juce::String(juce::int64(synth::synthParameterBaseId + index)));
            entry->setAttribute("value", double(values[index]));
        }
        juce::AudioProcessor::copyXmlToBinary(*xml, binary);
        return binary.toBase64Encoding();
    };
    const auto read = [](const juce::String &encoded)
    {
        juce::MemoryBlock binary;
        synth::Values values{};
        require(decodePluginState(encoded, binary) &&
                    synth::parseSynthNativeState(binary.getData(), int(binary.getSize()), values).wasOk(),
                "Controller synth history endpoint passes unchanged strict native parser");
        return values;
    };
    const auto same = [](const synth::Values &actual, const synth::Values &expected)
    {
        for (std::size_t index = 0; index < actual.size(); ++index)
            require(actual[index] == expected[index], "Controller synth history retains every exact scalar");
    };
    const auto source = synth::parameterIndex(synth::ParameterId::sourceType);
    const auto selector = synth::parameterIndex(synth::ParameterId::factoryPreset);
    const auto unrelated = synth::parameterIndex(synth::ParameterId::modWheel);
    const auto sampleValues = normalized(*sampled), synthValues = normalized(*electronic);
    const auto synthState = state(synthValues);
    juce::String output = "owned unchanged sentinel";
    require(rewritePluginParameterState(synthState, synth::synthParameterBaseId + std::uint32_t(source),
                                        synthValues[source], sampleValues[source], output)
                    .failed() &&
                output == "owned unchanged sentinel",
            "Public single-scalar rewrite cannot publish an invalid mapless recorded source");
    std::array<PluginParameterStateEdit, synth::parameterCount> wholePatch{};
    for (std::size_t index = 0; index < wholePatch.size(); ++index)
        wholePatch[index] = {synth::synthParameterBaseId + std::uint32_t(index), synthValues[index],
                             sampleValues[index]};
    require(rewritePluginParameterStates(synthState, wholePatch, output).wasOk(),
            "Atomic whole64-parameter rewrite accepts coupled recorded source/bank and unchanged values");
    same(read(output), sampleValues);
    auto incomplete = wholePatch;
    incomplete[selector].replacement = synthValues[selector];
    const auto previousOutput = output;
    require(rewritePluginParameterStates(synthState, incomplete, output).failed() && output == previousOutput,
            "Atomic helper rejects an invalid final source/bank without publishing partial output");
    auto duplicate = wholePatch;
    duplicate.back() = duplicate.front();
    require(rewritePluginParameterStates(synthState, duplicate, output).failed() && output == previousOutput,
            "Atomic helper rejects duplicate witnesses before publishing output");

    for (const bool sampledToSynth : {true, false})
    {
        const auto before = sampledToSynth ? sampleValues : synthValues;
        const auto after = sampledToSynth ? synthValues : sampleValues;
        auto project = Project::empty();
        const auto target = project.allocate();
        PluginSpec instrument{target, "Original synth history fixture",
                              synth::nativeDescription().createXml()->toString(), state(before), true};
        PluginBinding binding;
        binding.id = synth::synthParameterBaseId + std::uint32_t(source);
        binding.name = "Source";
        binding.manual = before[source];
        instrument.automationBindings.push_back(binding);
        project.channels.front().instrument = instrument;
        require(project.validate().wasOk(), "Native synth Controller instrument fixture validates");
        auto owner = std::make_unique<Controller>();
        owner->replace(project);
        owner->deliverPendingNotifications();
        auto captured = owner->project;
        captured.plugin(target)->state = state(after);
        std::array<NativeScalarEdit, synth::parameterCount> gestures{};
        std::vector<NativeScalarEdit> edits;
        for (std::size_t index = 0; index < gestures.size(); ++index)
        {
            gestures[index] = {target, synth::synthParameterBaseId + std::uint32_t(index), before[index],
                               after[index]};
            if (before[index] != after[index])
                edits.push_back(gestures[index]);
        }
        require(gestures.size() == synth::parameterCount && gestures.size() >= 64 && !edits.empty() && edits.size() < gestures.size() &&
                    before[source] != after[source] && before[selector] != after[selector] &&
                    source < selector && before[unrelated] == after[unrelated],
                "Full-parameter UI witnesses contain changed source-before-selector and explicit filtered no-ops");
        for (const auto &edit : edits)
            require(edit.before != edit.after, "History receives zero no-op witnesses after UI filtering");
        const auto original = owner->project.state().toXmlString();
        const auto revision = owner->revision;
        std::unique_ptr<PreparedControllerChange> ticket;
        require(owner->prepareNativeScalarEdit(captured, gestures, ticket).failed() && !ticket &&
                    owner->project.state().toXmlString() == original && owner->revision == revision &&
                    !owner->undo.canUndo(),
                "Controller refuses unfiltered no-op gestures without mutation or history admission");
        const auto prepared = owner->prepareNativeScalarEdit(captured, edits, ticket);
        require(prepared.wasOk() && ticket && owner->project.state().toXmlString() == original &&
                    owner->revision == revision,
                "Controller atomically prepares either cross-bank patch without mutating its source");
        same(read(ticket->destinationBeforeCommit()->plugin(target)->state), after);
        const auto commit = [&]
        {
            require(ticket && owner->validatePreparedChange(*ticket).wasOk() &&
                        owner->commitPreparedChange(*ticket),
                    "Actual Controller/UndoManager commits validated native synth patch history");
            ticket.reset();
        };
        commit();
        require(owner->isNativeScalarHistory(HistoryDirection::undo) && owner->undo.canUndo(),
                "Cross-bank patch owns a genuine positively tagged Undo action");
        auto freshAfter = after;
        freshAfter[unrelated] = .3125f;
        auto undoCapture = owner->project;
        undoCapture.plugin(target)->state = state(freshAfter);
        const auto undoPrepared =
            owner->prepareNativeScalarHistory(HistoryDirection::undo, undoCapture, ticket);
        require(undoPrepared.wasOk() && ticket,
                "Actual Controller Undo reconstructs source and selector together across banks");
        auto undoExpected = before;
        undoExpected[unrelated] = freshAfter[unrelated];
        same(read(ticket->destinationBeforeCommit()->plugin(target)->state), undoExpected);
        require(ticket->destinationBeforeCommit()->plugin(target)->automationBindings.front().manual ==
                    before[source],
                "Cross-bank Undo restores the exact existing manual source binding");
        commit();
        require(owner->isNativeScalarHistory(HistoryDirection::redo) && !owner->undo.canUndo(),
                "Actual cross-bank Undo moves the real history cursor exactly once");
        auto freshBefore = before;
        freshBefore[unrelated] = .6875f;
        auto redoCapture = owner->project;
        redoCapture.plugin(target)->state = state(freshBefore);
        const auto redoPrepared =
            owner->prepareNativeScalarHistory(HistoryDirection::redo, redoCapture, ticket);
        require(redoPrepared.wasOk() && ticket,
                "Actual Controller Redo reconstructs source and selector together across banks");
        auto redoExpected = after;
        redoExpected[unrelated] = freshBefore[unrelated];
        same(read(ticket->destinationBeforeCommit()->plugin(target)->state), redoExpected);
        require(ticket->destinationBeforeCommit()->plugin(target)->automationBindings.front().manual ==
                    after[source],
                "Cross-bank Redo restores the exact existing manual source binding");
        commit();
        same(read(owner->project.plugin(target)->state), redoExpected);
        require(owner->isNativeScalarHistory(HistoryDirection::undo) && !owner->undo.canRedo(),
                "Actual cross-bank Redo returns the exact native history cursor");
    }
}
static void pluginScalarHistorySupportTests()
{
    synthScalarPatchHistoryTests();
    static_assert(ipc::version == 22); // Wrapper actions, bounded native patches and multi-output transfer require the matching IPC22 pair.
    {
        synth::Values values{};
        const auto &preset=synth::factoryPresets().front();
        for(const auto &p:synth::parameterSpecs())
            values[synth::parameterIndex(p.id)]=synth::synthParameterRange(p).convertTo0to1(preset.values[synth::parameterIndex(p.id)]);
        auto state=synth::makeSynthNativeState(values);
        auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),int(state.getSize()));
        auto *overlay=xml->createNewChildElement("VibeRaParameterValues");overlay->setAttribute("version",1);
        const auto id=synth::synthParameterBaseId+synth::parameterIndex(synth::ParameterId::masterDb);
        const auto after=values[synth::parameterIndex(synth::ParameterId::masterDb)];
        auto *p=overlay->createNewChildElement("PARAM");p->setAttribute("id",juce::String(juce::int64(id)));p->setAttribute("value",double(after));
        juce::AudioProcessor::copyXmlToBinary(*xml,state);
        juce::String rewritten;
        require(rewritePluginParameterState(state.toBase64Encoding(),std::uint32_t(id),after,.2f,rewritten).wasOk(),"Synthesizer scalar history accepts strict native state");
        juce::MemoryBlock before;require(decodePluginState(rewritten,before),"Synthesizer history decodes");
        synth::Values restored;
        require(synth::parseSynthNativeState(before.getData(),int(before.getSize()),restored).wasOk() &&
                restored[synth::parameterIndex(synth::ParameterId::masterDb)]==.2f,"Synthesizer history restores exact previous scalar");
    }
    const auto encode = [](const juce::XmlElement &xml)
    {
        juce::MemoryBlock binary;
        juce::AudioProcessor::copyXmlToBinary(xml, binary);
        return binary.toBase64Encoding();
    };
    const auto decode = [](const juce::String &encoded)
    {
        juce::MemoryBlock binary;
        require(decodePluginState(encoded, binary), "scalar history test state decodes");
        auto xml = juce::AudioProcessor::getXmlFromBinary(binary.getData(), int(binary.getSize()));
        require(xml != nullptr, "scalar history test state is genuine JUCE XML envelope");
        return xml;
    };
    const auto rawXml = [](const juce::String &text)
    {
        juce::MemoryBlock binary;
        {
            juce::MemoryOutputStream stream(binary, false);
            const auto bytes = text.getNumBytesAsUTF8();
            stream.writeInt(0x21324356);
            stream.writeInt(int(bytes));
            stream.write(text.toRawUTF8(), bytes);
            stream.writeByte(0);
        }
        return binary.toBase64Encoding();
    };
    juce::XmlElement state("VST3PluginState");
    state.setAttribute("unrelated", "original & <escaped> text");
    state.setAttribute("utf8", juce::String::fromUTF8("retained \xce\xbb"));
    auto *component = state.createNewChildElement("IComponent");
    component->addTextElement("native-component-chunk");
    auto *controller = state.createNewChildElement("IEditController");
    controller->addTextElement("native-controller-chunk");
    auto *values = state.createNewChildElement("VibeRaParameterValues");
    values->setAttribute("version", 1);
    auto *target = values->createNewChildElement("PARAM");
    target->setAttribute("id", "0"); // A real zero native ID, unrelated to Project binding IDs.
    target->setAttribute("value", .875);
    auto *neighbour = values->createNewChildElement("PARAM");
    neighbour->setAttribute("id", "4294967295");
    neighbour->setAttribute("value", .25);
    const auto after = encode(state);
    juce::String before = "owned sentinel";
    require(rewritePluginParameterState(after, 0, .875f, .125f, before).wasOk(),
            "first unbound native ID zero obtains BEFORE through existing owned overlay");
    auto actual = decode(before);
    auto expected = std::make_unique<juce::XmlElement>(state);
    expected->getChildByName("VibeRaParameterValues")->getChildByName("PARAM")->setAttribute("value", .125);
    require(expected->isEquivalentTo(actual.get(), false),
            "only witnessed scalar changes; opaque chunks, escaped attributes and other scalar remain exact");
    require(encode(state) == after, "overlay rewrite does not mutate fresh AFTER source");
    juce::String restored = before;
    require(rewritePluginParameterState(restored, 0, .125f, .875f, restored).wasOk() &&
                state.isEquivalentTo(decode(restored).get(), false),
            "reverse rewrite accepts aliasing output and reconstructs every fresh AFTER XML field");
    const auto refuses = [&](const juce::String &encoded, std::uint32_t id, float expectedValue,
                             float replacement, const char *message)
    {
        juce::String output = "unchanged owned output";
        const auto result = rewritePluginParameterState(encoded, id, expectedValue, replacement, output);
        require(result.failed() && output == "unchanged owned output", message);
    };
    refuses(after, 0, .5f, .125f, "mismatched acknowledged AFTER refuses with owning output preserved");
    refuses(after, 1, .875f, .125f, "missing target cannot create a fabricated binding or overlay entry");
    refuses(after, 0, .875f, std::numeric_limits<float>::quiet_NaN(), "NaN replacement refused");
    refuses(after, 0, .875f, 1.01f, "out of range replacement refused");
    refuses(after, 0, std::numeric_limits<float>::infinity(), .25f, "infinite expected value refused");
    refuses({}, 0, .875f, .125f, "empty state refused");
    refuses("not-an-owned-state", 0, .875f, .125f, "invalid length-prefixed encoding refused");
    refuses(rawXml("<!DOCTYPE VST3PluginState><VST3PluginState/>"), 0, .875f, .125f,
            "public helper refuses literal DTD before XML parsing");
    refuses(rawXml("<VST3PluginState/><VST3PluginState/>"), 0, .875f, .125f,
            "public helper refuses multiple XML roots");
    {
        auto malformed = std::make_unique<juce::XmlElement>(state);
        malformed->removeChildElement(malformed->getChildByName("VibeRaParameterValues"), true);
        refuses(encode(*malformed), 0, .875f, .125f, "missing overlay refused");
    }
    {
        auto malformed = std::make_unique<juce::XmlElement>(state);
        malformed->addChildElement(new juce::XmlElement(*values));
        refuses(encode(*malformed), 0, .875f, .125f, "duplicate overlay refused");
    }
    {
        auto malformed = std::make_unique<juce::XmlElement>(state);
        malformed->getChildByName("VibeRaParameterValues")->setAttribute("version", 2);
        refuses(encode(*malformed), 0, .875f, .125f, "unknown overlay version refused");
    }
    for (const auto &id : {juce::String("-1"), juce::String("4294967296"), juce::String("1junk")})
    {
        auto malformed = std::make_unique<juce::XmlElement>(state);
        malformed->getChildByName("VibeRaParameterValues")->getChildByName("PARAM")->setAttribute("id", id);
        refuses(encode(*malformed), 0, .875f, .125f, "invalid unsigned native ID refused");
    }
    for (const auto &value : {juce::String("nan"), juce::String("1.25"), juce::String("0.875junk")})
    {
        auto malformed = std::make_unique<juce::XmlElement>(state);
        malformed->getChildByName("VibeRaParameterValues")
            ->getChildByName("PARAM")
            ->setAttribute("value", value);
        refuses(encode(*malformed), 0, .875f, .125f, "malformed normalized scalar refused");
    }
    {
        auto malformed = std::make_unique<juce::XmlElement>(state);
        malformed->getChildByName("VibeRaParameterValues")->addChildElement(new juce::XmlElement(*target));
        refuses(encode(*malformed), 0, .875f, .125f, "duplicate native ID refused");
    }
    {
        auto malformed = std::make_unique<juce::XmlElement>(state);
        malformed->getChildByName("VibeRaParameterValues")->getChildByName("PARAM")->setAttribute("extra", 1);
        refuses(encode(*malformed), 0, .875f, .125f, "unknown scalar metadata refused");
    }
    {
        juce::MemoryBlock malformed;
        require(decodePluginState(after, malformed), "test creates actual owned binary envelope");
        static_cast<char *>(malformed.getData())[4] ^= 1;
        refuses(malformed.toBase64Encoding(), 0, .875f, .125f, "false JUCE XML byte length refused");
    }
    require(!parameter_state_detail::boundedXml("<!DOCTYPE VST3PluginState><VST3PluginState/>"),
            "DTD refused before invoking XML parser");
    require(!parameter_state_detail::boundedXml("<VST3PluginState/><VST3PluginState/>"),
            "multiple root elements refused before invoking XML parser");
    juce::String deep;
    for (int i = 0; i < 33; ++i)
        deep += "<n>";
    for (int i = 0; i < 33; ++i)
        deep += "</n>";
    require(!parameter_state_detail::boundedXml(deep), "XML nesting is bounded before recursive parsing");

    PluginControlRequests requests;
    const auto oldToken = requests.reserve(7, 1), knownToken = requests.reserve(8, 2);
    require(requests.complete({oldToken, 7, .75f, 0}) &&
                requests.complete({knownToken, 8, .875f, 0, .125f, true}),
            "old aggregate fixtures and exact appended native BEFORE both retain normal receipts");
    PluginParameterResult result;
    require(requests.consume(oldToken, result) && !result.beforeKnown && result.value == .75f,
            "legacy aggregate success does not fabricate known BEFORE zero");
    require(requests.consume(knownToken, result) && result.beforeKnown && result.previousValue == .125f &&
                result.value == .875f,
            "actual known BEFORE survives retained receipt ownership and consumption");

    PluginGestureTracker tracker;
    PluginCompletedGestures completed;
    tracker.synchronize(9, 4, 0);
    const auto finish = [&](unsigned id, float first, float final, std::uint64_t ticks)
    {
        require(!tracker.consume({id, 9, 4, first, PluginGesturePhase::begin}) &&
                    !tracker.consume({id, 9, 4, final, PluginGesturePhase::value}),
                "incomplete gesture cannot enter completed history queue");
        const auto gesture = tracker.consume({id, 9, 4, final, PluginGesturePhase::end, ticks});
        require(gesture && completed.push(*gesture), "every supported completion enters retained queue");
    };
    finish(11, .1f, .8f, 200);
    finish(12, .2f, .7f, 100); // Older QPC completion intentionally drained later.
    require(tracker.latest() && tracker.latest()->id == 11 && tracker.latest()->completedTicks == 200,
            "Last Tweaked keeps original QPC target selection independently of history FIFO");
    PluginGesture gesture;
    require(completed.pop(gesture) && gesture.id == 11 && gesture.before == .1f && gesture.after == .8f &&
                gesture.serial == 1 && gesture.generation == 9 && gesture.epoch == 4,
            "first completion retains exact before/after and source epoch");
    require(completed.pop(gesture) && gesture.id == 12 && gesture.serial == 2,
            "second older-QPC completion is not silently lost");
    const auto sentinel = gesture;
    require(!completed.pop(gesture) && gesture.id == sentinel.id && gesture.serial == sentinel.serial,
            "empty completion queue preserves caller output");
    for (unsigned i = 0; i < 256; ++i)
    {
        gesture.serial = i + 1;
        require(completed.push(gesture), "all bounded retained receipt slots are usable");
    }
    require(!completed.push(gesture) && completed.losses() == 257 && !completed.pop(gesture),
            "overflow counts incoming plus quarantined prefix; no partial batch claims success");
    require(completed.push(gesture) && completed.pop(gesture) && completed.losses() == 257,
            "new independent batch works while prior loss remains explicit");
    require(completed.push(gesture), "schema invalidation fixture has real pending receipt");
    completed.invalidate();
    require(completed.losses() == 259 && !completed.pop(gesture),
            "schema/notification invalidation drops pending receipt with visible loss");
    tracker.consume({1, 9, 4, .2f, PluginGesturePhase::begin});
    const auto invalid = tracker.invalidatedCompletions();
    require(!tracker.consume({1, 9, 4, .2f, PluginGesturePhase::begin}) &&
                tracker.invalidatedCompletions() == invalid + 1,
            "malformed nested gesture exposes loss instead of inventing a completed operation");
    tracker.synchronize(10, 5, 1);
    require(!tracker.consume({2, 9, 4, .5f, PluginGesturePhase::end}) && !tracker.latest(),
            "stale schema and epoch cannot resurrect a completion or last target");
}
