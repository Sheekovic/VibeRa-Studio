// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SynthPresets.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace vibera::synth
{
struct SynthBrowserState
{
    juce::String genre = "All Genres", family = "All", search;
    bool favouritesOnly = false;
    juce::StringArray favouriteIds;
    // Editor navigation only: no instrument parameter or document Undo edge.
    int effectPage = 0, octave = 0, scrollX = 0, scrollY = 0, presetScrollY = 0;
    juce::String selectedPresetId;
};
inline juce::File synthUserDirectory()
{
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto override = juce::SystemStats::getEnvironmentVariable("VIBERA_SYNTH_USER_DIRECTORY", "");
    const auto name = executable.getFileName();
    if (override.isNotEmpty() &&
        (name.equalsIgnoreCase("VibeRaSynthPluginTests.exe") || name.equalsIgnoreCase("VibeRa Studio.exe") ||
         name.equalsIgnoreCase("VibeRa Plugin Worker.exe")))
    {
        auto buildRoot = executable.getParentDirectory();
        bool found = false;
        for (int level = 0; level < 8; ++level)
        {
            if (buildRoot.getFileName() == "windows" &&
                buildRoot.getParentDirectory().getFileName() == "build")
            {
                found = true;
                break;
            }
            buildRoot = buildRoot.getParentDirectory();
        }
        if (found && (!juce::File::isAbsolutePath(override) || !juce::File(override).isAChildOf(buildRoot)))
            throw std::invalid_argument(
                "Synth test user directory must be an absolute child of build/windows");
        if (found)
            return juce::File(override);
    }
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("VibeRa Studio/Synthesizer");
}
inline SynthBrowserState readSynthBrowserState()
{
    SynthBrowserState s;
    const auto file = synthUserDirectory().getChildFile("browser.json");
    if (file.getSize() > 262144)
        return s;
    const auto json = juce::JSON::parse(file);
    if (!json.isObject())
        return s;
    s.genre = json.getProperty("genre", "All Genres").toString();
    s.family = json.getProperty("family", "All").toString();
    s.search = json["search"].toString();
    s.favouritesOnly = bool(json["favouritesOnly"]);
    s.effectPage = int(json.getProperty("effectPage", 0));
    s.octave = int(json.getProperty("octave", 0));
    s.scrollX = int(json.getProperty("scrollX", 0));
    s.scrollY = int(json.getProperty("scrollY", 0));
    s.presetScrollY = int(json.getProperty("presetScrollY", 0));
    s.selectedPresetId = json["selectedPresetId"].toString();
    // Corrupt navigation preferences must not index controls or select an
    // unknown patch. Preserve otherwise readable musical browsing preferences.
    if (s.effectPage < 0 || s.effectPage > 4)
        s.effectPage = 0;
    if (s.octave < -1 || s.octave > 4)
        s.octave = 0;
    if (s.scrollX < 0 || s.scrollX > 10000)
        s.scrollX = 0;
    if (s.scrollY < 0 || s.scrollY > 10000)
        s.scrollY = 0;
    if (s.presetScrollY < 0 || s.presetScrollY > 10000)
        s.presetScrollY = 0;
    if (s.selectedPresetId.isNotEmpty() && !findFactoryPreset(s.selectedPresetId.toStdString()))
        s.selectedPresetId.clear();
    if (auto *ids = json["favouriteIds"].getArray())
        for (const auto &id : *ids)
            if (findFactoryPreset(id.toString().toStdString()))
                s.favouriteIds.addIfNotAlreadyThere(id.toString());
    return s;
}
inline juce::Result validateSynthBrowserState(const SynthBrowserState &s)
{
    if (s.effectPage < 0 || s.effectPage > 4 || s.octave < -1 || s.octave > 4 || s.scrollX < 0 ||
        s.scrollX > 10000 || s.scrollY < 0 || s.scrollY > 10000 || s.presetScrollY < 0 ||
        s.presetScrollY > 10000)
        return juce::Result::fail(
            "synth.navigationRange: Effect page 0..4, octave -1..4 and scroll positions 0..10000 required");
    if (s.selectedPresetId.isNotEmpty() && !findFactoryPreset(s.selectedPresetId.toStdString()))
        return juce::Result::fail(
            "synth.unknownPreset: Highlight an existing stable factory ID or clear the selection");
    if (s.search.length() > 512 || s.favouriteIds.size() > 4096)
        return juce::Result::fail("synth.browserLimit");
    bool genre = s.genre == "All Genres" || s.genre == "Favorites", family = s.family == "All";
    for (const auto *name : genreNames())
        genre = genre || s.genre == name;
    for (const auto *name : familyNames())
        family = family || s.family == name;
    if (!genre || !family)
        return juce::Result::fail("synth.unknownFilter: rediscover genres and families");
    for (const auto &id : s.favouriteIds)
        if (!findFactoryPreset(id.toStdString()))
            return juce::Result::fail("synth.unknownPreset: favorite ID is not in factory library");
    return juce::Result::ok();
}
inline bool synthPresetMatchesBrowser(const FactoryPreset &preset, const SynthBrowserState &state)
{
    const bool favourites = state.favouritesOnly || state.genre == "Favorites" || state.genre == "Favourites";
    if (state.genre != "Favorites" && state.genre != "Favourites" &&
        !presetHasGenre(preset, state.genre.toStdString()))
        return false;
    if (state.family != "All" && state.family != preset.family)
        return false;
    if (favourites && !state.favouriteIds.contains(preset.id))
        return false;
    // Presets built on the Wavetable oscillator are findable as "wavetable" even when
    // their name does not say so.
    constexpr float wavetableOscillator = 5;
    const bool wavetable =
        preset.values[size_t(ParameterId::oscillatorType)] == wavetableOscillator ||
        preset.values[size_t(ParameterId::oscillator2Type)] == wavetableOscillator;
    const auto searchable = juce::String(preset.name) + " " + preset.family + " " + preset.genre + " " +
                            preset.description + " " + sourceKindName(preset.source) +
                            (wavetable ? " Wavetable" : "");
    return state.search.isEmpty() || searchable.containsIgnoreCase(state.search);
}
// Pure CONTROL helper: factory order, matching filters, wrap and hidden-current
// behaviour are shared by the native buttons and semantic previous/next.
inline juce::String adjacentSynthPresetId(const SynthBrowserState &state, const juce::String &current,
                                          int direction)
{
    if (direction != -1 && direction != 1)
        return {};
    std::vector<const FactoryPreset *> visible;
    for (const auto &preset : factoryPresets())
        if (synthPresetMatchesBrowser(preset, state))
            visible.push_back(&preset);
    if (visible.empty())
        return {};
    const auto found = std::find_if(visible.begin(), visible.end(),
                                    [&](const auto *preset) { return current == preset->id; });
    if (found == visible.end())
        return direction > 0 ? visible.front()->id : visible.back()->id;
    const auto index = static_cast<std::ptrdiff_t>(found - visible.begin());
    const auto count = static_cast<std::ptrdiff_t>(visible.size());
    return visible[std::size_t((index + direction + count) % count)]->id;
}
inline juce::Result writeSynthBrowserState(const SynthBrowserState &s)
{
    if (auto valid = validateSynthBrowserState(s); valid.failed())
        return valid;
    juce::var v(new juce::DynamicObject);
    auto *o = v.getDynamicObject();
    o->setProperty("genre", s.genre);
    o->setProperty("family", s.family);
    o->setProperty("search", s.search);
    o->setProperty("favouritesOnly", s.favouritesOnly);
    juce::Array<juce::var> ids;
    o->setProperty("effectPage", s.effectPage);
    o->setProperty("octave", s.octave);
    o->setProperty("scrollX", s.scrollX);
    o->setProperty("scrollY", s.scrollY);
    o->setProperty("presetScrollY", s.presetScrollY);
    o->setProperty("selectedPresetId", s.selectedPresetId);
    for (const auto &id : s.favouriteIds)
    {
        if (!findFactoryPreset(id.toStdString()))
            return juce::Result::fail("synth.unknownPreset: favorite ID is not in factory library");
        ids.add(id);
    }
    o->setProperty("favouriteIds", ids);
    if (!synthUserDirectory().createDirectory())
        return juce::Result::fail("Cannot create user preset directory");
    juce::TemporaryFile temporary(synthUserDirectory().getChildFile("browser.json"));
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(v)) ||
        !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Cannot save browser preferences; previous file retained");
    return juce::Result::ok();
}
} // namespace vibera::synth
