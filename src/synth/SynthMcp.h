// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SynthBrowser.h"
#include "SynthRegistry.h"
#include "SynthUserPresets.h"

namespace vibera::synth
{
inline juce::var synthToolDefinition()
{
    return juce::JSON::parse(
        R"JSON({"name":"vibera_synth","description":"Bundled native VibeRa Synthesizer. Genre/family/search/favorites use the exact native browser preference service; navigation and preview are transient. Discover stable preset IDs and parameter keys/ranges first. loadPreset/loadUserPreset use the existing native scalar ACK plus captured project Undo job; poll vibera_plugin job/cancel until terminal. Native editor operations use the established acknowledged editor service. No external scan, provider, account or network is needed.","inputSchema":{"type":"object","additionalProperties":false,"required":["action"],"properties":{"action":{"type":"string","enum":["list","listUserPresets","descriptor","browser","filter","favorite","navigate","previous","next","create","parameters","set","loadPreset","saveUserPreset","loadUserPreset","previewOn","previewOff","panic","openEditor","closeEditor","status"]},"plugin":{"type":"integer","minimum":1,"maximum":1000000000},"instance":{"type":"string","maxLength":100},"generation":{"type":"integer","minimum":1,"maximum":4294967295},"preset":{"type":"string","maxLength":128},"key":{"type":"string","maxLength":64},"value":{"type":"number"},"genre":{"type":"string","maxLength":64},"family":{"type":"string","maxLength":64},"search":{"type":"string","maxLength":512},"presetScrollY":{"type":"integer","minimum":0,"maximum":10000},"effectPage":{"type":"integer","minimum":0,"maximum":4},"octave":{"type":"integer","minimum":-1,"maximum":4},"scrollX":{"type":"integer","minimum":0,"maximum":10000},"scrollY":{"type":"integer","minimum":0,"maximum":10000},"selectedPresetId":{"type":"string","maxLength":128},"favoritesOnly":{"type":"boolean"},"enabled":{"type":"boolean"},"note":{"type":"integer","minimum":0,"maximum":127},"velocity":{"type":"integer","minimum":1,"maximum":127,"default":100},"overwrite":{"type":"boolean","default":false},"name":{"type":"string","maxLength":100,"description":"User preset basename inside the writable Synthesizer directory, without extension or path separators."}}}})JSON");
}
inline juce::var synthObject()
{
    return juce::var(new juce::DynamicObject);
}
inline juce::var synthBrowserJson(const SynthBrowserState &s)
{
    auto result = synthObject();
    auto *o = result.getDynamicObject();
    o->setProperty("genre", s.genre);
    o->setProperty("family", s.family);
    o->setProperty("search", s.search);
    o->setProperty("favoritesOnly", s.favouritesOnly);
    o->setProperty("presetScrollY", s.presetScrollY);
    o->setProperty("effectPage", s.effectPage);
    o->setProperty("octave", s.octave);
    o->setProperty("scrollX", s.scrollX);
    o->setProperty("scrollY", s.scrollY);
    o->setProperty("selectedPresetId", s.selectedPresetId);
    o->setProperty("navigationScope", "Saved native editor preferences; the editor clamps scroll to its "
                                      "actual current viewport on adoption.");
    juce::Array<juce::var> favorites;
    for (const auto &id : s.favouriteIds)
        favorites.add(id);
    o->setProperty("favorites", favorites);
    return result;
}
inline juce::var synthTelemetryJson(std::uint64_t bits)
{
    auto v = synthObject();
    auto *o = v.getDynamicObject();
    const bool available = (bits & (std::uint64_t(1) << 63)) != 0;
    const auto preset = std::size_t((bits >> 8) & 65535);
    const auto source = unsigned((bits >> 24) & 3);
    o->setProperty("available", available && preset < factoryPresets().size() && source < 3);
    if (!available || preset >= factoryPresets().size() || source >= 3)
        return v;
    o->setProperty("activeVoices", int(bits & 255));
    o->setProperty("presetId", factoryPresets()[preset].id);
    o->setProperty("sourceKind", sourceKindName(static_cast<SourceKind>(source)));
    o->setProperty("contentReady", (bits & (std::uint64_t(1) << 32)) != 0);
    o->setProperty("sampleAvailable", (bits & (std::uint64_t(1) << 33)) != 0);
    o->setProperty("parameterError",
                   (bits & (std::uint64_t(1) << 34))
                       ? "Sample source rejected: choose a preset with a recorded sample map"
                       : "");
    o->setProperty("loadStatus", (bits & (std::uint64_t(1) << 32))
                                     ? "Factory content ready / Native instrument"
                                     : "Factory content unavailable; inspect worker diagnostic");
    o->setProperty("scope", "Last processed native block (initial prepared patch before first audio); "
                            "CONTROL observations are not a device-timestamp acknowledgement.");
    return v;
}
inline juce::var synthDiscovery(const SynthBrowserState &state)
{
    auto result = synthObject();
    auto *o = result.getDynamicObject();
    o->setProperty("schemaVersion", 1);
    o->setProperty("builtIn", true);
    o->setProperty("separateVST3", false);
    juce::Array<juce::var> genres{"All Genres", "Favorites"}, families{"All"}, presets, parameters;
    for (const auto *name : genreNames())
        genres.add(name);
    for (const auto *name : familyNames())
        families.add(name);
    for (const auto &p : factoryPresets())
    {
        const auto tags = juce::StringArray::fromTokens(p.genre, "|,", "");
        if (!synthPresetMatchesBrowser(p, state))
            continue;
        auto row = synthObject();
        auto *r = row.getDynamicObject();
        r->setProperty("id", p.id);
        r->setProperty("name", p.name);
        r->setProperty("family", p.family);
        juce::Array<juce::var> tagList;
        for (const auto &tag : tags)
            tagList.add(tag);
        r->setProperty("genres", tagList);
        r->setProperty("sourceKind", sourceKindName(p.source));
        r->setProperty("soundSource", p.sampleAsset);
        r->setProperty("description", p.description);
        r->setProperty("favorite", state.favouriteIds.contains(p.id));
        r->setProperty("schemaVersion", 1);
        r->setProperty("factory", true);
        juce::Array<juce::var> descriptiveTags{p.family, sourceKindName(p.source), "polyphonic"};
        r->setProperty("tags", descriptiveTags);
        auto macros = synthObject();
        auto *m = macros.getDynamicObject();
        m->setProperty("toneMinimumHz", p.macros.toneMinimumHz);
        m->setProperty("toneMaximumHz", p.macros.toneMaximumHz);
        m->setProperty("warmthMaximumBoostDb", p.macros.warmthMaximumBoostDb);
        m->setProperty("spaceMaximumMix", p.macros.spaceMaximumMix);
        m->setProperty("motionMaximumCents", p.macros.motionMaximumCents);
        r->setProperty("macroMapping", macros);
        juce::Array<juce::var> settings;
        for (auto v : p.values)
            settings.add(v);
        r->setProperty("settings", settings);
        presets.add(row);
    }
    for (const auto &p : parameterSpecs())
    {
        auto row = synthObject();
        auto *r = row.getDynamicObject();
        r->setProperty("id", juce::int64(0x10000 + parameterIndex(p.id)));
        r->setProperty("key", p.key);
        r->setProperty("name", p.name);
        r->setProperty("minimum", p.minimum);
        r->setProperty("maximum", p.maximum);
        r->setProperty("default", p.initial);
        r->setProperty("unit", p.unit);
        r->setProperty("interval", p.interval);
        r->setProperty("skew", p.skew);
        juce::Array<juce::var> choices;
        if (p.id == ParameterId::sourceType)
            choices = {"Synthesis", "Sample", "Sample + synthesis"};
        else if (p.id == ParameterId::filterType)
            choices = {"Low pass", "High pass", "Band pass", "Bypass"};
        else if (p.id == ParameterId::oscillatorType || p.id == ParameterId::oscillator2Type)
            choices = {"Sine", "Saw", "Pulse", "Triangle", "Noise"};
        else if (p.interval == 1 && p.minimum == 0 && p.maximum == 1)
            choices = {"Off", "On"};
        if (!choices.isEmpty())
            r->setProperty("choices", choices);
        parameters.add(row);
    }
    std::vector<SynthUserPresetMetadata> users;
    juce::StringArray errors;
    const auto status = listSynthUserPresets(synthUserDirectory(), users, errors);
    juce::Array<juce::var> userRows, errorRows;
    for (const auto &user : users)
        userRows.add(user.toVar());
    for (const auto &error : errors)
        errorRows.add(error);
    o->setProperty("userPresets", userRows);
    o->setProperty("userPresetErrors", errorRows);
    if (status.failed())
        o->setProperty("userPresetError", status.getErrorMessage());
    o->setProperty("genres", genres);
    o->setProperty("families", families);
    o->setProperty("presets", presets);
    o->setProperty("parameters", parameters);
    o->setProperty("browser", synthBrowserJson(state));
    o->setProperty("total", presets.size());
    o->setProperty("userDirectory", synthUserDirectory().getFullPathName());
    return result;
}
} // namespace vibera::synth
