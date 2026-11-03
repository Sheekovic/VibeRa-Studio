// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "BrowserCollections.h"
static void browserTagTests(const juce::File &root)
{
    using namespace vibera::browser_search;
    const auto a = root.getChildFile("tagged A.wav"), b = root.getChildFile("tagged B.mid");
    juce::StringArray parsed{"retained"};
    require(parseTags(" Drum, Tight, drum, 808, ", parsed).wasOk() &&
                parsed == juce::StringArray{"Drum", "Tight", "808"},
            "Comma tag parsing trims and deduplicates case-insensitively");
    const auto validParsed = parsed;
    require(parseTags(juce::String::repeatedString("x", 65), parsed).failed() && parsed == validParsed,
            "Invalid tag input preserves previous parsed state");
    require(validateTags({"a", "A"}).failed() && validateTags({"a,b"}).failed() &&
                validateTags({"bad\nname"}).failed() && validateTags({" spaced "}).failed(),
            "Tag name validation");
    const auto arabic = juce::String::charToString(0x639) + juce::String::charToString(0x648) +
                        juce::String::charToString(0x62f);
    require(parseTags(arabic + ", C#", parsed).wasOk() && parsed[0] == arabic && parsed[1] == "C#",
            "Unicode and musical tag names retained");
    TagCatalog catalog;
    require(catalog.set(a, {"Drum", "Tight"}).wasOk() && catalog.set(b, {"Drum"}).wasOk(),
            "File tag associations");
    TagFilter filter{{"drum", "tight"}, false};
    require(catalog.matches(a.getFullPathName().toStdString(), filter) &&
                catalog.matches(b.getFullPathName().toStdString(), filter),
            "ANY union");
    filter.all = true;
    require(catalog.matches(a.getFullPathName().toStdString(), filter) &&
                !catalog.matches(b.getFullPathName().toStdString(), filter),
            "ALL intersection");
    require(!catalog.matches(root.getChildFile("tagged A copy.wav").getFullPathName().toStdString(), filter),
            "Tags bind exact path, not filename prefix");
    require(
        catalog.matches(a.getFullPathName().toUpperCase().replaceCharacter('\\', '/').toStdString(), filter),
        "Windows path case/separator identity");
    require(!a.exists() && !b.exists(), "Tag association does not create/read sample contents");
    const auto encoded = juce::JSON::toString(catalog.state());
    TagCatalog restored;
    require(restored.restore(juce::JSON::parse(encoded)).wasOk() &&
                juce::JSON::toString(restored.state()) == encoded,
            "Tag catalog round trip");
    auto bad = juce::JSON::parse(encoded);
    bad.getArray()->add((*bad.getArray())[0]);
    require(restored.restore(bad).failed() && juce::JSON::toString(restored.state()) == encoded,
            "Duplicate tagged path refuses partial restore");
    bad = juce::JSON::parse(encoded);
    (*bad.getArray())[0].getDynamicObject()->setProperty("path", "relative.wav");
    require(restored.restore(bad).failed(), "Relative tagged path rejected");
    bad = juce::JSON::parse(encoded);
    (*bad.getArray())[0]["tags"].getArray()->add("DRUM");
    require(restored.restore(bad).failed(), "Duplicate stored tag rejected");
    Collections collections;
    require(collections.setTags(a, {"Drum"}).wasOk() && collections.toggle(a).wasOk(),
            "Collections tag snapshot fixture");
    const auto before = collections.tags;
    require(collections.setTags(a, {"Drum", "Tight"}).wasOk() && before != collections.tags &&
                before->tagsFor(a.getFullPathName().toStdString()).size() == 1 &&
                collections.tags->tagsFor(a.getFullPathName().toStdString()).size() == 2,
            "Metadata edits publish a new immutable catalog while old query snapshot remains valid");
    const auto snapshot = collections.tags;
    require(collections.setTags(a, {"Drum", "drum"}).failed() && collections.tags == snapshot,
            "Invalid edit preserves catalog identity");
    collections.allFilter = filter;
    require(collections.save("Drums", "kick", {root.getFullPathName().toStdString()}, filter).wasOk(),
            "Saved filter captures ANY/ALL and selected tags");
    const auto id = collections.searches[0].id;
    const auto v3 = juce::JSON::toString(collections.state());
    Collections loaded;
    require(loaded.restore(juce::JSON::parse(v3)).wasOk() && juce::JSON::toString(loaded.state()) == v3 &&
                loaded.searches[0].filter.all && loaded.searches[0].filter.selected == filter.selected &&
                loaded.starred(a),
            "Preferences v3 exact round trip including tag catalog/default filters/saved IDs/favorites");
    auto v2 = juce::JSON::parse(v3);
    v2.getDynamicObject()->setProperty("version", 2);
    for (const auto *property : {"fileTags", "allTagFilter", "starredTagFilter"})
        v2.getDynamicObject()->removeProperty(property);
    for (auto &saved : *v2["searches"].getArray())
    {
        saved.getDynamicObject()->removeProperty("filter");
        saved.getDynamicObject()->removeProperty("starred");
    }
    require(loaded.restore(v2).wasOk() && loaded.searches[0].id == id && loaded.starred(a) &&
                loaded.searches[0].filter.empty() && loaded.tags->allTags().isEmpty(),
            "v2 migration retains original tab/favorite identity with empty new filters");
    require(collections.save("Tag-only", "", {root.getFullPathName().toStdString()}, filter).wasOk() &&
                collections.save("Starred tag-only", "", {}, filter, true).wasOk(),
            "Tag-only normal and Starred search tabs");
    require(loaded.restore(collections.state()).wasOk() && loaded.searches.back().starred &&
                loaded.searches.back().roots.empty() && !loaded.searches.back().filter.empty(),
            "Saved Starred scope round trip without library roots");
    const auto good = juce::JSON::toString(loaded.state());
    bad = juce::JSON::parse(good);
    bad.getDynamicObject()->setProperty("allTagFilter", juce::var{});
    require(loaded.restore(bad).failed() && juce::JSON::toString(loaded.state()) == good,
            "Malformed default filter preserves collections");
    bad = juce::JSON::parse(good);
    (*bad["searches"].getArray())[0].getDynamicObject()->setProperty("starred", "true");
    require(loaded.restore(bad).failed() && juce::JSON::toString(loaded.state()) == good,
            "Malformed saved scope preserves collections");

    // Independent membership oracle over all four-bit file/tag sets and both
    // query modes. No assumptions about storage iteration or UI ordering.
    TagCatalog masks;
    for (int fileMask = 0; fileMask < 16; ++fileMask)
    {
        juce::StringArray names;
        for (int bit = 0; bit < 4; ++bit)
            if (fileMask & (1 << bit))
                names.add("Tag" + juce::String(bit));
        require(masks.set(root.getChildFile("mask" + juce::String(fileMask) + ".wav"), names).wasOk(),
                "Tag truth-table fixture");
    }
    for (int fileMask = 0; fileMask < 16; ++fileMask)
        for (int queryMask = 0; queryMask < 16; ++queryMask)
            for (bool all : {false, true})
            {
                TagFilter query;
                query.all = all;
                for (int bit = 0; bit < 4; ++bit)
                    if (queryMask & (1 << bit))
                        query.selected.add("tag" + juce::String(bit));
                const bool expected = queryMask == 0 || (all ? (fileMask & queryMask) == queryMask
                                                             : (fileMask & queryMask) != 0);
                require(masks.matches(root.getChildFile("mask" + juce::String(fileMask) + ".wav")
                                          .getFullPathName()
                                          .toStdString(),
                                      query) == expected,
                        "ANY/ALL independent set-membership oracle");
            }
    TagCatalog bounded;
    for (int i = 0; i < 4096; ++i)
        require(bounded.set(root.getChildFile("bounded" + juce::String(i) + ".wav"), {"Drum"}).wasOk(),
                "Bounded tagged-file metadata fixture");
    require(bounded.set(root.getChildFile("overflow.wav"), {"Drum"}).failed(),
            "Metadata file-reference cap refuses overflow");
    require(bounded.set(root.getChildFile("bounded0.wav"), {}).wasOk() &&
                bounded.set(root.getChildFile("overflow.wav"), {"Drum"}).wasOk(),
            "Full metadata store permits removal and reuse");
}
