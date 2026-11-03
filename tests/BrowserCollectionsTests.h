// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "BrowserCollections.h"
static void browserCollectionsTests(const juce::File &root)
{
    using namespace vibera::browser_search;
    const auto file = root.getChildFile("missing favorite.wav");
    Collections state;
    require(state.toggle(file).wasOk() && state.starred(file), "Starred stores unavailable file reference");
    require(
        state.save("Dry drums", "(kick OR snare) NOT wet", {root.getFullPathName().toStdString()}).wasOk(),
        "Saved Browser query captures explicit root scope");
    const auto original = juce::JSON::toString(state.state());
    Collections loaded;
    require(loaded.restore(juce::JSON::parse(original)).wasOk() && loaded.starred(file) &&
                juce::JSON::toString(loaded.state()) == original,
            "Browser collections exact JSON round trip including stable tab ID");
    auto reject = [&](juce::var bad)
    {
        require(loaded.restore(bad).failed() && juce::JSON::toString(loaded.state()) == original,
                "Invalid collection document retains previous in-memory state");
    };
    reject(juce::JSON::parse("{broken"));
    auto bad = juce::JSON::parse(original);
    bad.getDynamicObject()->setProperty("version", 99);
    reject(bad);
    bad = juce::JSON::parse(original);
    bad["favorites"].getArray()->add(file.getFullPathName());
    reject(bad);
    bad = juce::JSON::parse(original);
    bad["favorites"].getArray()->set(0, "relative.wav");
    reject(bad);
    bad = juce::JSON::parse(original);
    bad["searches"].getArray()->add((*bad["searches"].getArray())[0]);
    reject(bad);
    bad = juce::JSON::parse(original);
    (*bad["searches"].getArray())[0].getDynamicObject()->setProperty("query", "kick OR");
    reject(bad);
    require(loaded.toggle(file).wasOk() && !loaded.starred(file) && !file.exists(),
            "Removing missing favorite never touches filesystem");
    require(loaded.restore(juce::JSON::parse("{\"version\":1}")).wasOk() && loaded.searches.empty() &&
                loaded.favorites.isEmpty(),
            "Legacy v1 Browser preferences migrate with empty collections");
    require(state.save("Invalid", "kick OR", {root.getFullPathName().toStdString()}).failed() &&
                state.save("Empty", "", {root.getFullPathName().toStdString()}).failed() &&
                state.save("Relative", "kick", {"relative"}).failed(),
            "Invalid saved search cannot be inserted");
    for (int i = 1; i < 16; ++i)
        require(
            state.save("Query " + juce::String(i), "kick*", {root.getFullPathName().toStdString()}).wasOk(),
            "Saved search bounded fixture");
    require(state.save("Overflow", "kick", {root.getFullPathName().toStdString()}).failed(),
            "Saved search limit refuses overflow");
    for (int i = 1; i < 512; ++i)
        require(state.toggle(root.getChildFile("favorite " + juce::String(i) + ".wav")).wasOk(),
                "Starred bounded fixture");
    require(state.toggle(root.getChildFile("overflow.wav")).failed() && state.favorites.size() == 512,
            "Starred limit refuses overflow without losing existing entries");
    require(state.toggle(file).wasOk() && !state.starred(file),
            "Full Starred collection still permits removal");
}
