// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginStateEncoding.h"
#include "PluginProtocol.h"
#include "synth/SynthState.h"
#include "effects/NativeEffectsRegistry.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <charconv>
#include <cmath>
#include <exception>
#include <map>
#include <set>
#include <span>
#include <vector>

namespace vibera
{
namespace parameter_state_detail
{
// CONTROL only. Bound parser recursion before JUCE parses the owned XML. The
// actual JUCE state writer emits ordinary tags/text, never DTD/entity/CDATA.
inline bool boundedXml(const juce::String &text)
{
    int depth = 0, roots = 0;
    const auto *cursor = text.toRawUTF8();
    while (*cursor)
    {
        if (*cursor++ != '<')
            continue;
        if (*cursor == '!')
            return false;
        const bool instruction = *cursor == '?';
        const bool closing = *cursor == '/';
        char quote = 0, previous = 0;
        while (*cursor && (quote || *cursor != '>'))
        {
            const auto c = *cursor++;
            if (quote)
            {
                if (c == quote)
                    quote = 0;
            }
            else if (c == '\'' || c == '"')
                quote = c;
            previous = c;
        }
        if (!*cursor || quote)
            return false;
        ++cursor;
        if (instruction)
        {
            if (previous != '?')
                return false;
            continue;
        }
        if (closing)
        {
            if (--depth < 0)
                return false;
        }
        else
        {
            if (depth == 0 && ++roots != 1)
                return false;
            if (previous != '/' && ++depth > 32)
                return false;
        }
    }
    return depth == 0 && roots == 1;
}
} // namespace parameter_state_detail

// CONTROL preparation only; no Host/Project mutation or native-DSP claim.
// freshAfter must be the caller's exact completed capture. Replaces existing
// original-owned scalars after verifying every captured value. Native component/
// controller chunks and all other XML data are retained. Failure preserves out.
// A complete patch is validated atomically: coupled bank/source changes never
// expose an invalid intermediate state to the strict native parser.
struct PluginParameterStateEdit
{
    std::uint32_t id;
    float expected, replacement;
};
inline juce::Result rewritePluginParameterStates(const juce::String &freshAfter,
                                                 std::span<const PluginParameterStateEdit> edits,
                                                 juce::String &out)
{
    const auto valid = [](float value) { return std::isfinite(value) && value >= 0 && value <= 1; };
    try
    {
        if (edits.empty() || edits.size() > std::size_t(ipc::parameters))
            return juce::Result::fail("parameterHistory.capacity");
        std::map<std::uint32_t, std::size_t> requested;
        for (std::size_t index = 0; index < edits.size(); ++index)
        {
            if (!valid(edits[index].expected) || !valid(edits[index].replacement))
                return juce::Result::fail("parameterHistory.value");
            if (!requested.emplace(edits[index].id, index).second)
                return juce::Result::fail("parameterHistory.duplicate");
        }
        juce::MemoryBlock binary;
        if (freshAfter.isEmpty() || !decodePluginState(freshAfter, binary) || binary.getSize() <= 9 ||
            binary.getSize() > std::size_t(ipc::stateBytes))
            return juce::Result::fail("parameterHistory.encoding");
        const auto *bytes = static_cast<const char *>(binary.getData());
        const auto length = juce::ByteOrder::littleEndianInt(bytes + 4);
        if (juce::ByteOrder::littleEndianInt(bytes) != 0x21324356u || length == 0 ||
            std::size_t(length) != binary.getSize() - 9 || bytes[binary.getSize() - 1] != 0 ||
            !juce::CharPointer_UTF8::isValidString(bytes + 8, int(length)))
            return juce::Result::fail("parameterHistory.envelope");
        for (std::uint32_t i = 0; i < length; ++i)
            if (bytes[8 + i] == 0)
                return juce::Result::fail("parameterHistory.envelope");
        const auto xmlText = juce::String::fromUTF8(bytes + 8, int(length));
        if (!parameter_state_detail::boundedXml(xmlText))
            return juce::Result::fail("parameterHistory.xml");
        auto xml = juce::AudioProcessor::getXmlFromBinary(binary.getData(), int(binary.getSize()));
        if (!xml || (!xml->hasTagName("VST3PluginState") && !xml->hasTagName("VIBERA_NATIVE_EFFECT") &&
                     !xml->hasTagName("VIBERA_SYNTHESIZER")))
            return juce::Result::fail("parameterHistory.xml");
        if (xml->hasTagName("VIBERA_SYNTHESIZER"))
        {
            synth::Values normalized;
            if (auto result =
                    synth::parseSynthNativeState(binary.getData(), int(binary.getSize()), normalized);
                result.failed())
                return result;
            for (const auto &edit : edits)
                if (edit.id < synth::synthParameterBaseId ||
                    edit.id - synth::synthParameterBaseId >= synth::parameterCount)
                    return juce::Result::fail("parameterHistory.nativeParameters");
        }
        if (xml->hasTagName("VIBERA_NATIVE_EFFECT"))
        {
            const auto effect = effects::findNativeType(xml->getStringAttribute("effect"));
            if (!effect || xml->getNumAttributes() != 2 ||
                xml->getStringAttribute("schemaVersion") != "1")
                return juce::Result::fail("parameterHistory.nativeType");
            const auto &spec = effects::effectSpec(*effect);
            const auto first = (static_cast<std::uint32_t>(*effect) + 1) * 256 + 1;
            std::set<std::uint32_t> originalIds;
            for (auto *child = xml->getFirstChildElement(); child; child = child->getNextElement())
            {
                if (child->hasTagName("VibeRaParameterValues"))
                    continue;
                const auto key = child->getStringAttribute("id").toStdString();
                std::uint32_t originalId = 0;
                const auto parsed = std::from_chars(key.data(), key.data() + key.size(), originalId);
                const auto valueText = child->getStringAttribute("value").toStdString();
                float originalValue = 0;
                const auto parsedValue = std::from_chars(valueText.data(), valueText.data() + valueText.size(), originalValue);
                if (!child->hasTagName("PARAM") || child->getNumAttributes() != 2 || child->getFirstChildElement() ||
                    parsed.ec != std::errc{} || parsed.ptr != key.data() + key.size() || originalId < first ||
                    originalId - first >= spec.parameterCount || !originalIds.insert(originalId).second ||
                    parsedValue.ec != std::errc{} || parsedValue.ptr != valueText.data() + valueText.size() ||
                    !valid(originalValue))
                    return juce::Result::fail("parameterHistory.nativeParameters");
            }
            if (originalIds.size() != spec.parameterCount)
                return juce::Result::fail("parameterHistory.nativeParameters");
            for (const auto &edit : edits)
                if (!originalIds.contains(edit.id))
                    return juce::Result::fail("parameterHistory.nativeParameters");
        }
        juce::XmlElement *overlay = nullptr;
        for (auto *child = xml->getFirstChildElement(); child; child = child->getNextElement())
            if (child->hasTagName("VibeRaParameterValues"))
            {
                if (overlay)
                    return juce::Result::fail("parameterHistory.overlay");
                overlay = child;
            }
        if (!overlay || overlay->getNumAttributes() != 1 || overlay->getStringAttribute("version") != "1" ||
            overlay->getNumChildElements() > ipc::parameters)
            return juce::Result::fail("parameterHistory.overlay");
        std::set<std::uint32_t> seen;
        std::vector<juce::XmlElement *> targets(edits.size(), nullptr);
        for (auto *entry = overlay->getFirstChildElement(); entry; entry = entry->getNextElement())
        {
            if (!entry->hasTagName("PARAM") || entry->getNumAttributes() != 2 || !entry->hasAttribute("id") ||
                !entry->hasAttribute("value") || entry->getFirstChildElement())
                return juce::Result::fail("parameterHistory.overlay");
            const auto idText = entry->getStringAttribute("id").toStdString();
            std::uint32_t nativeId = 0;
            const auto parsedId = std::from_chars(idText.data(), idText.data() + idText.size(), nativeId);
            const auto valueText = entry->getStringAttribute("value").toStdString();
            float value = 0;
            const auto parsedValue =
                std::from_chars(valueText.data(), valueText.data() + valueText.size(), value);
            if (idText.empty() || parsedId.ec != std::errc{} ||
                parsedId.ptr != idText.data() + idText.size() || !seen.insert(nativeId).second ||
                valueText.empty() || parsedValue.ec != std::errc{} ||
                parsedValue.ptr != valueText.data() + valueText.size() || !valid(value))
                return juce::Result::fail("parameterHistory.overlay");
            if (const auto requestedEdit = requested.find(nativeId); requestedEdit != requested.end())
            {
                const auto index = requestedEdit->second;
                if (value != edits[index].expected)
                    return juce::Result::fail("parameterHistory.afterMismatch");
                targets[index] = entry;
            }
        }
        for (const auto *target : targets)
            if (!target)
                return juce::Result::fail("parameterHistory.targetMissing");
        for (std::size_t index = 0; index < edits.size(); ++index)
            targets[index]->setAttribute("value", double(edits[index].replacement));
        juce::MemoryBlock nextBinary;
        juce::AudioProcessor::copyXmlToBinary(*xml, nextBinary);
        if (nextBinary.getSize() > std::size_t(ipc::stateBytes))
            return juce::Result::fail("parameterHistory.encoding");
        if (xml->hasTagName("VIBERA_SYNTHESIZER"))
        {
            synth::Values normalized;
            if (auto result =
                    synth::parseSynthNativeState(nextBinary.getData(), int(nextBinary.getSize()), normalized);
                result.failed())
                return result;
        }
        auto next = nextBinary.toBase64Encoding();
        out.swapWith(next);
        return juce::Result::ok();
    }
    catch (const std::exception &)
    {
        return juce::Result::fail("parameterHistory.exception");
    }
}
inline juce::Result rewritePluginParameterState(const juce::String &freshAfter, std::uint32_t id,
                                                float expectedAfter, float replacement, juce::String &out)
{
    const PluginParameterStateEdit edit{id, expectedAfter, replacement};
    return rewritePluginParameterStates(freshAfter, std::span(&edit, 1), out);
}
} // namespace vibera
