// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "BrowserQuery.h"

namespace vibera::browser_search
{
// Browser preferences are not project edits. These records only reference
// user files: removing a favorite/tab can never remove a sample from disk.
struct Collections
{
    struct Saved
    {
        juce::String id, name, query;
        std::vector<std::string> roots;
        TagFilter filter;
        bool starred = false;
    };
    juce::StringArray favorites;
    std::vector<Saved> searches;
    TagFilter allFilter, starredFilter;
    std::shared_ptr<const TagCatalog> tags = std::make_shared<TagCatalog>();
    juce::Result setTags(const juce::File &file, const juce::StringArray &values)
    {
        auto candidate = std::make_shared<TagCatalog>(*tags);
        const auto valid = candidate->set(file, values);
        if (valid.failed())
            return valid;
        tags = std::move(candidate);
        return juce::Result::ok();
    }
    static bool path(const juce::String &value)
    {
        return value.length() <= 4096 && juce::File::isAbsolutePath(value);
    }
    bool starred(const juce::File &file) const
    {
        return favorites.contains(file.getFullPathName(), true);
    }
    juce::Result toggle(const juce::File &file)
    {
        const auto name = file.getFullPathName();
        const int index = favorites.indexOf(name, true);
        if (index >= 0)
            favorites.remove(index);
        else
        {
            if (!path(name) || !file.hasFileExtension("wav;aif;aiff;flac;mid;midi"))
                return juce::Result::fail("Starred supports sample and MIDI file references");
            if (favorites.size() >= 512)
                return juce::Result::fail("Starred limit reached (512)");
            favorites.add(name);
        }
        return juce::Result::ok();
    }
    juce::Result save(juce::String name, const juce::String &query, std::vector<std::string> roots,
                      TagFilter filter = {}, bool starredOnly = false)
    {
        const auto parsed = parseQuery(query);
        if (parsed.error.isNotEmpty())
            return juce::Result::fail(parsed.error);
        const auto validFilter = validateTags(filter.selected);
        if (validFilter.failed())
            return validFilter;
        if ((query.trim().isEmpty() && filter.empty()) || query.length() > 512 ||
            (!starredOnly && roots.empty()) || roots.size() > 32)
            return juce::Result::fail("Search a configured library before saving a tab");
        for (const auto &root : roots)
            if (!path(juce::String(root)))
                return juce::Result::fail("Saved search root is invalid");
        if (searches.size() >= 16)
            return juce::Result::fail("Saved-search limit reached (16)");
        name = name.trim().substring(0, 48);
        if (name.isEmpty())
            name = (query.trim().isEmpty() ? filter.selected.joinIntoString(", ") : query).substring(0, 36);
        searches.push_back(
            {juce::Uuid().toString(), name, query, std::move(roots), std::move(filter), starredOnly});
        return juce::Result::ok();
    }
    juce::var state() const
    {
        auto data = std::make_unique<juce::DynamicObject>();
        data->setProperty("version", 3);
        data->setProperty("fileTags", tags->state());
        data->setProperty("allTagFilter", allFilter.state());
        data->setProperty("starredTagFilter", starredFilter.state());
        juce::Array<juce::var> stars, saved;
        for (const auto &file : favorites)
            stars.add(file);
        for (const auto &item : searches)
        {
            auto entry = std::make_unique<juce::DynamicObject>();
            entry->setProperty("id", item.id);
            entry->setProperty("name", item.name);
            entry->setProperty("query", item.query);
            juce::Array<juce::var> roots;
            for (const auto &root : item.roots)
                roots.add(juce::String(root));
            entry->setProperty("roots", roots);
            entry->setProperty("filter", item.filter.state());
            entry->setProperty("starred", item.starred);
            saved.add(juce::var(entry.release()));
        }
        data->setProperty("favorites", stars);
        data->setProperty("searches", saved);
        return juce::var(data.release());
    }
    juce::Result restore(const juce::var &data)
    {
        if (!data.isObject() || !data["version"].isInt())
            return juce::Result::fail("Invalid Browser preferences");
        const auto version = int(data["version"]);
        if (version == 1)
        {
            *this = {};
            return juce::Result::ok();
        }
        if (version != 2 && version != 3)
            return juce::Result::fail("Unsupported Browser preferences version; file preserved");
        Collections candidate;
        if (version == 3)
        {
            auto catalog = std::make_shared<TagCatalog>();
            const auto valid = catalog->restore(data["fileTags"]);
            if (valid.failed())
                return valid;
            candidate.tags = std::move(catalog);
            const auto all = candidate.allFilter.restore(data["allTagFilter"]);
            if (all.failed())
                return all;
            const auto stars = candidate.starredFilter.restore(data["starredTagFilter"]);
            if (stars.failed())
                return stars;
        }
        const auto *stars = data["favorites"].getArray();
        const auto *saved = data["searches"].getArray();
        if (!stars || !saved || stars->size() > 512 || saved->size() > 16)
            return juce::Result::fail("Invalid Browser collection limits");
        for (const auto &file : *stars)
        {
            if (!file.isString() || !path(file.toString()) ||
                !juce::File(file.toString()).hasFileExtension("wav;aif;aiff;flac;mid;midi") ||
                candidate.favorites.contains(file.toString(), true))
                return juce::Result::fail("Invalid or duplicate Starred reference");
            candidate.favorites.add(file.toString());
        }
        juce::StringArray ids;
        for (const auto &item : *saved)
        {
            const auto *roots = item["roots"].getArray();
            if (!item.isObject() || !item["id"].isString() || item["id"].toString().isEmpty() ||
                item["id"].toString().length() > 64 || ids.contains(item["id"].toString()) ||
                !item["name"].isString() || item["name"].toString().trim().isEmpty() ||
                item["name"].toString().length() > 48 || !item["query"].isString() || !roots ||
                roots->size() > 32)
                return juce::Result::fail("Invalid saved Browser search");
            std::vector<std::string> paths;
            for (const auto &root : *roots)
            {
                if (!root.isString() || !path(root.toString()))
                    return juce::Result::fail("Invalid saved search root");
                paths.push_back(root.toString().toStdString());
            }
            TagFilter filter;
            bool starred = false;
            if (version == 3)
            {
                const auto valid = filter.restore(item["filter"]);
                if (valid.failed())
                    return valid;
                if (!item["starred"].isBool())
                    return juce::Result::fail("Invalid saved Starred scope");
                starred = bool(item["starred"]);
            }
            const auto added = candidate.save(item["name"].toString(), item["query"].toString(),
                                              std::move(paths), std::move(filter), starred);
            if (added.failed())
                return added;
            candidate.searches.back().id = item["id"].toString();
            ids.add(item["id"].toString());
        }
        *this = std::move(candidate);
        return juce::Result::ok();
    }
};
} // namespace vibera::browser_search
