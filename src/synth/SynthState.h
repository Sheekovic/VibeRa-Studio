// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SynthPresets.h"
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace vibera::synth
{
inline constexpr std::size_t maximumSynthStateBytes = 262144;
inline constexpr juce::uint32 synthParameterBaseId = 0x10000;
inline juce::NormalisableRange<float> synthParameterRange(const ParameterSpec &spec)
{
    return {spec.minimum, spec.maximum, spec.interval, spec.skew};
}
// CONTROL/off-DSP only. Every root/child, native scalar and optional sidecar is
// checked before the destination is assigned. Project state keeps its schema.
inline juce::Result parseSynthNativeState(const void *data, int bytes, Values &destination)
{
    const auto fail = [](const char *reason)
    { return juce::Result::fail("synth.invalidState: " + juce::String(reason)); };
    if (!data || bytes < 8 || std::size_t(bytes) > maximumSynthStateBytes)
        return fail("Native state exceeds bounded input size");
    const auto xml = juce::AudioProcessor::getXmlFromBinary(data, bytes);
    if (!xml || !xml->hasTagName("VIBERA_SYNTHESIZER") || xml->getNumAttributes() != 2 ||
        xml->getStringAttribute("schemaVersion") != "1")
        return fail("Unknown native envelope/schema");
    const auto *preset = findFactoryPreset(xml->getStringAttribute("preset").toStdString());
    if (!preset)
        return fail("Unknown factory reference");
    Values next{};
    for (std::size_t index = legacyParameterCount; index < parameterCount; ++index)
    {
        const auto &spec = parameterSpec(static_cast<ParameterId>(index));
        next[index] = synthParameterRange(spec).convertTo0to1(spec.initial);
    }
    std::array<bool, parameterCount> seen{};
    const juce::XmlElement *sidecar = nullptr;
    const auto read = [&](const juce::XmlElement &parent, bool complete)
    {
        for (auto *node = parent.getFirstChildElement(); node; node = node->getNextElement())
        {
            if (node->hasTagName("VibeRaParameterValues") && complete)
            {
                if (sidecar || node->getNumAttributes() != 1 || node->getStringAttribute("version") != "1")
                    return false;
                sidecar = node;
                continue;
            }
            if (!node->hasTagName("PARAM") || node->getNumAttributes() != 2 ||
                node->getNumChildElements() != 0)
                return false;
            const auto idText = node->getStringAttribute("id");
            const auto *begin = idText.toRawUTF8();
            const auto *end = begin + idText.getNumBytesAsUTF8();
            juce::uint32 id = 0;
            const auto parsed = std::from_chars(begin, end, id);
            if (parsed.ec != std::errc{} || parsed.ptr != end || id < synthParameterBaseId ||
                id - synthParameterBaseId >= parameterCount)
                return false;
            const auto index = std::size_t(id - synthParameterBaseId);
            const auto text = node->getStringAttribute("value");
            char *numericEnd = nullptr;
            const auto value = std::strtod(text.toRawUTF8(), &numericEnd);
            if (text.isEmpty() || !numericEnd || *numericEnd != '\0' || seen[index] ||
                !std::isfinite(value) || value < 0 || value > 1)
                return false;
            const auto &spec = parameterSpec(static_cast<ParameterId>(index));
            const auto range = synthParameterRange(spec);
            const auto normalized = float(value);
            if (spec.interval > 0 && std::abs(normalized - range.convertTo0to1(range.snapToLegalValue(
                                                               range.convertFrom0to1(normalized)))) > 1e-6f)
                return false;
            seen[index] = true;
            next[index] = normalized;
        }
        // Earlier schema 1 states predate the appended engine scalars; those
        // stay at their neutral defaults, every earlier scalar stays required.
        return !complete || std::all_of(seen.begin(), seen.begin() + legacyParameterCount,
                                        [](bool value) { return value; });
    };
    if (!read(*xml, true))
        return fail("Missing, duplicate, unsupported or invalid native scalar");
    const auto selected = parameterIndex(ParameterId::factoryPreset);
    const auto presetRange = synthParameterRange(parameterSpec(ParameterId::factoryPreset));
    const auto expected = presetRange.convertTo0to1(float(preset - factoryPresets().data()));
    if (std::abs(next[selected] - expected) > 1e-6f)
        return fail("Factory root and native scalar identity disagree");
    if (sidecar)
    {
        seen.fill(false);
        if (!read(*sidecar, false))
            return fail("Invalid worker scalar sidecar");
    }
    const auto finalIndex =
        std::lround(presetRange.snapToLegalValue(presetRange.convertFrom0to1(next[selected])));
    if (finalIndex < 0 || std::size_t(finalIndex) >= factoryPresets().size())
        return fail("Final factory reference is unavailable");
    const auto sourceRange = synthParameterRange(parameterSpec(ParameterId::sourceType));
    const auto source = int(std::lround(sourceRange.snapToLegalValue(
        sourceRange.convertFrom0to1(next[parameterIndex(ParameterId::sourceType)]))));
    if (source != 0 && factoryPresets()[std::size_t(finalIndex)].sampleAsset[0] == '\0')
        return fail("Sample or hybrid source requires a recorded factory sample map");
    destination = next;
    return juce::Result::ok();
}
inline const FactoryPreset &synthStateBasePreset(const Values &normalized)
{
    const auto range = synthParameterRange(parameterSpec(ParameterId::factoryPreset));
    const auto index = std::size_t(std::lround(range.snapToLegalValue(
        range.convertFrom0to1(normalized[parameterIndex(ParameterId::factoryPreset)]))));
    return factoryPresets()[index]; // Only called after parseSynthNativeState validation.
}
inline juce::MemoryBlock makeSynthNativeState(const Values &normalized)
{
    juce::XmlElement xml("VIBERA_SYNTHESIZER");
    xml.setAttribute("schemaVersion", 1);
    xml.setAttribute("preset", synthStateBasePreset(normalized).id);
    for (std::size_t i = 0; i < parameterCount; ++i)
    {
        auto *node = xml.createNewChildElement("PARAM");
        node->setAttribute("id", juce::String(juce::int64(synthParameterBaseId + i)));
        node->setAttribute("value", double(normalized[i]));
    }
    juce::MemoryBlock result;
    juce::AudioProcessor::copyXmlToBinary(xml, result);
    return result;
}
} // namespace vibera::synth
