// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "dsp/EffectsCatalogue.h"
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <optional>
#include <vector>

namespace vibera::effects
{
inline std::optional<EffectId> findNativeType(const juce::String &slug)
{
    for (const auto &spec : catalogueEntries())
        if (slug == spec.slug)
            return spec.id;
    return std::nullopt;
}

inline juce::PluginDescription nativeDescription(EffectId effect)
{
    const auto &spec = effectSpec(effect);
    juce::PluginDescription description;
    description.name = description.descriptiveName = "VibeRa " + juce::String(spec.name);
    description.pluginFormatName = "VibeRa";
    description.category = spec.category;
    description.manufacturerName = "VibeRa";
    description.version = "1";
    description.fileOrIdentifier = "vibera.effect." + juce::String(spec.slug);
    description.uniqueId = description.deprecatedUid = 0x56520000 + static_cast<int>(effect) + 1;
    description.numInputChannels = description.numOutputChannels = 2;
    description.isInstrument = false;
    return description;
}

inline std::vector<juce::PluginDescription> nativeDescriptions()
{
    std::vector<juce::PluginDescription> descriptions;
    descriptions.reserve(catalogueEntries().size());
    for (const auto &spec : catalogueEntries())
        descriptions.push_back(nativeDescription(spec.id));
    return descriptions;
}

inline std::optional<EffectId> findNativeDescription(const juce::PluginDescription &description)
{
    if (description.pluginFormatName != "VibeRa" ||
        !description.fileOrIdentifier.startsWith("vibera.effect."))
        return std::nullopt;
    const auto id = findNativeType(description.fileOrIdentifier.substring(14));
    if (!id)
        return std::nullopt;
    const auto expected = nativeDescription(*id);
    if (description.name != expected.name || description.descriptiveName != expected.descriptiveName ||
        description.category != expected.category || description.manufacturerName != "VibeRa" ||
        description.version != "1" || description.uniqueId != expected.uniqueId ||
        description.deprecatedUid != expected.deprecatedUid || description.isInstrument ||
        description.numInputChannels != 2 || description.numOutputChannels != 2 ||
        description.hasSharedContainer || description.hasARAExtension)
        return std::nullopt;
    return id;
}

inline bool isNativeDescription(const juce::PluginDescription &description)
{
    return findNativeDescription(description).has_value();
}
} // namespace vibera::effects
