// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <map>
#include <memory>

namespace vibera::browser_search
{
inline std::string normalizedPath(const std::string &path)
{
    auto value = juce::String::fromUTF8(path.c_str()).replaceCharacter('\\', '/').toLowerCase();
    while (value.length() > 1 && value.endsWithChar('/'))
        value = value.dropLastCharacters(1);
    return value.toStdString();
}
inline juce::Result validateTags(const juce::StringArray &tags)
{
    if (tags.size() > 32)
        return juce::Result::fail("Use at most 32 tags per file or filter");
    juce::StringArray seen;
    for (const auto &tag : tags)
    {
        if (tag.trim().isEmpty() || tag != tag.trim() || tag.length() > 64 || tag.containsChar(','))
            return juce::Result::fail("Tag names must be 1-64 characters without commas");
        for (auto c : tag)
            if (c < 32 || c == 127)
                return juce::Result::fail("Tag names cannot contain control characters");
        if (seen.contains(tag, true))
            return juce::Result::fail("Duplicate tag name");
        seen.add(tag);
    }
    return juce::Result::ok();
}
inline juce::Result parseTags(const juce::String &text, juce::StringArray &result)
{
    if (text.length() > 4096)
        return juce::Result::fail("Tag input is limited to 4096 characters");
    juce::StringArray candidate;
    for (auto tag : juce::StringArray::fromTokens(text, ",", {}))
    {
        tag = tag.trim();
        if (tag.isNotEmpty())
            candidate.addIfNotAlreadyThere(tag, true);
    }
    const auto valid = validateTags(candidate);
    if (valid.failed())
        return valid;
    result = std::move(candidate);
    return juce::Result::ok();
}
struct TagFilter
{
    juce::StringArray selected;
    bool all = false;
    bool empty() const
    {
        return selected.isEmpty();
    }
    juce::var state() const
    {
        auto data = std::make_unique<juce::DynamicObject>();
        juce::Array<juce::var> values;
        for (const auto &tag : selected)
            values.add(tag);
        data->setProperty("selected", values);
        data->setProperty("all", all);
        return juce::var(data.release());
    }
    juce::Result restore(const juce::var &data)
    {
        const auto *values = data["selected"].getArray();
        if (!data.isObject() || !values || !data["all"].isBool() || values->size() > 32)
            return juce::Result::fail("Invalid saved tag filter");
        juce::StringArray candidate;
        for (const auto &value : *values)
        {
            if (!value.isString())
                return juce::Result::fail("Invalid tag filter name");
            candidate.add(value.toString());
        }
        const auto valid = validateTags(candidate);
        if (valid.failed())
            return valid;
        selected = std::move(candidate);
        all = bool(data["all"]);
        return juce::Result::ok();
    }
};
class TagCatalog
{
    struct Record
    {
        juce::String path;
        juce::StringArray tags;
    };
    std::map<std::string, Record> files;

  public:
    const juce::StringArray &tagsFor(const std::string &path) const
    {
        static const juce::StringArray empty;
        const auto found = files.find(normalizedPath(path));
        return found == files.end() ? empty : found->second.tags;
    }
    juce::Result set(const juce::File &file, const juce::StringArray &tags)
    {
        const auto path = file.getFullPathName();
        if (!juce::File::isAbsolutePath(path) || path.length() > 4096 ||
            !file.hasFileExtension("wav;aif;aiff;flac;mid;midi"))
            return juce::Result::fail("Tags support absolute sample/MIDI file references");
        const auto valid = validateTags(tags);
        if (valid.failed())
            return valid;
        const auto key = normalizedPath(path.toStdString());
        if (tags.isEmpty())
        {
            files.erase(key);
            return juce::Result::ok();
        }
        if (files.find(key) == files.end() && files.size() >= 4096)
            return juce::Result::fail("Tag metadata limit reached (4096 file references)");
        files[key] = {path, tags};
        return juce::Result::ok();
    }
    bool matches(const std::string &path, const TagFilter &filter) const
    {
        if (filter.selected.size() > 32)
            return false;
        if (filter.empty())
            return true;
        const auto &tags = tagsFor(path);
        for (const auto &tag : filter.selected)
        {
            const bool found = tags.contains(tag, true);
            if (filter.all && !found)
                return false;
            if (!filter.all && found)
                return true;
        }
        return filter.all;
    }
    juce::StringArray allTags() const
    {
        std::map<std::string, juce::String> unique;
        for (const auto &[_, file] : files)
            for (const auto &tag : file.tags)
                unique.try_emplace(tag.toLowerCase().toStdString(), tag);
        juce::StringArray values;
        for (const auto &[_, tag] : unique)
            values.add(tag);
        return values;
    }
    juce::var state() const
    {
        juce::Array<juce::var> records;
        for (const auto &[_, file] : files)
        {
            auto entry = std::make_unique<juce::DynamicObject>();
            entry->setProperty("path", file.path);
            juce::Array<juce::var> tags;
            for (const auto &tag : file.tags)
                tags.add(tag);
            entry->setProperty("tags", tags);
            records.add(juce::var(entry.release()));
        }
        return records;
    }
    juce::Result restore(const juce::var &data)
    {
        const auto *records = data.getArray();
        if (!records || records->size() > 4096)
            return juce::Result::fail("Invalid tag metadata records");
        TagCatalog candidate;
        for (const auto &entry : *records)
        {
            const auto *values = entry["tags"].getArray();
            const auto path = entry["path"].toString();
            if (!entry.isObject() || !entry["path"].isString() || !juce::File::isAbsolutePath(path) ||
                !values || values->isEmpty() || values->size() > 32)
                return juce::Result::fail("Invalid/duplicate tagged file reference");
            const juce::File file(path);
            if (candidate.files.contains(normalizedPath(file.getFullPathName().toStdString())))
                return juce::Result::fail("Duplicate canonical tagged file reference");
            juce::StringArray tags;
            for (const auto &value : *values)
            {
                if (!value.isString())
                    return juce::Result::fail("Invalid stored tag name");
                tags.add(value.toString());
            }
            const auto valid = candidate.set(file, tags);
            if (valid.failed())
                return valid;
        }
        files = std::move(candidate.files);
        return juce::Result::ok();
    }
};
} // namespace vibera::browser_search
