// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// A fixture relabelled as an older project must contain only fields that its
// actual writer could emit. Deliberate voice-field masquerades are tested apart.
inline void stripSamplerModulationForLegacy(juce::ValueTree &tree)
{
    for (auto channel : tree.getChildWithName("CHANNELS"))
    {
        auto sampler = channel.getChildWithName("SAMPLER");
        sampler.removeChild(sampler.getChildWithName("MODULATION"), nullptr);
    }
}
inline void stripSamplerFilterForLegacy(juce::ValueTree &tree)
{
    stripSamplerModulationForLegacy(tree);
    for (auto channel : tree.getChildWithName("CHANNELS"))
        for (const auto *field : {"filterType", "filterCutoff", "filterResonance"})
            channel.getChildWithName("SAMPLER").removeProperty(field, nullptr);
}
inline void stripPatternExtentForLegacy(juce::ValueTree &tree)
{
    stripSamplerFilterForLegacy(tree);
    for (auto pattern : tree.getChildWithName("PATTERNS"))
        for (const auto *field : {"autoLength", "numerator", "denominator"})
            pattern.removeProperty(field, nullptr);
}
inline void stripNoteMuteForLegacy(juce::ValueTree &tree)
{
    stripPatternExtentForLegacy(tree);
    for (auto pattern : tree.getChildWithName("PATTERNS"))
    {
        for (auto note : pattern)
            note.removeProperty("muted", nullptr);
    }
}
inline void stripSamplerVoiceFieldsForLegacy(juce::ValueTree &tree)
{
    stripNoteMuteForLegacy(tree);
    tree.removeChild(tree.getChildWithName("CLIP_GROUPS"), nullptr);
    for (auto channel : tree.getChildWithName("CHANNELS"))
        for (const auto *field : {"cut", "cutBy", "maxPoly", "normalize"})
            channel.getChildWithName("SAMPLER").removeProperty(field, nullptr);
}
