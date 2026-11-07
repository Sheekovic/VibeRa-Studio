// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "BrowserQuery.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <map>
#include <set>
#include <algorithm>
#include <vector>

namespace vibera::browser_search
{
// Independent local search behavior. Qwen's draft and official FL Browser
// documentation supplied feature leads, not a proven reference implementation.
inline constexpr char audioExtensions[] = "wav;aif;aiff;flac;mid;midi";
inline constexpr int indexFormatVersion = 1;
inline bool withinFolder(const std::string &path, const std::string &scope)
{
    const auto p = normalizedPath(path), s = normalizedPath(scope);
    return s.empty() || p == s ||
           (p.starts_with(s) && (s.ends_with('/') || (p.size() > s.size() && p[s.size()] == '/')));
}
struct Entry
{
    std::string path, name, lowerName, extension, parent;
    juce::int64 size = 0, modified = 0;
    bool missing = false;   // Ephemeral Starred references; never written to the index cache.
    juce::StringArray tags; // Query snapshot metadata; not part of the file index cache.
};
// Browser result order: 0 search order (unchanged), 1 name, 2 type then name, 3 newest first.
// Stable, case-insensitive; ties keep the search order.
inline void sortEntries(std::vector<Entry> &entries, int mode)
{
    auto byName = [](const Entry &a, const Entry &b) { return a.lowerName < b.lowerName; };
    if (mode == 1)
        std::stable_sort(entries.begin(), entries.end(), byName);
    else if (mode == 2)
        std::stable_sort(entries.begin(), entries.end(), [&](const Entry &a, const Entry &b)
                         { return a.extension != b.extension ? a.extension < b.extension : byName(a, b); });
    else if (mode == 3)
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry &a, const Entry &b) { return a.modified > b.modified; });
}
class Index
{
    std::map<std::string, Entry> entries; // Worker-owned; never accessed by DSP/UI.
  public:
    void insert(Entry entry)
    {
        entries[normalizedPath(entry.path)] = std::move(entry);
    }
    void clear()
    {
        entries.clear();
    }
    std::size_t size() const
    {
        return entries.size();
    }
    std::vector<Entry> snapshot() const
    {
        std::vector<Entry> out;
        out.reserve(entries.size());
        for (const auto &[_, entry] : entries)
            out.push_back(entry);
        return out;
    }
    bool match(const Query &query, std::vector<Entry> &out, const std::function<bool()> &canceled = {}) const
    {
        out.clear();
        if (query.error.isNotEmpty() || validateTags(query.tagFilter.selected).failed())
            return false;
        for (const auto &[_, entry] : entries)
        {
            if (canceled && canceled())
            {
                out.clear();
                return false;
            }
            if (!query.scopeFolder.empty() && !withinFolder(entry.parent, query.scopeFolder))
                continue;
            if (query.requireAnyExtension && std::find(query.extensions.begin(), query.extensions.end(),
                                                       entry.extension) == query.extensions.end())
                continue;
            if (!query.matches(entry.lowerName))
                continue;
            if (!query.tagFilter.empty() &&
                (!query.tagCatalog || !query.tagCatalog->matches(entry.path, query.tagFilter)))
                continue;
            out.push_back(entry);
            if (query.tagCatalog)
                out.back().tags = query.tagCatalog->tagsFor(entry.path);
        }
        return true;
    }
    std::vector<Entry> match(const Query &query) const
    {
        std::vector<Entry> result;
        match(query, result);
        return result;
    }
};
struct IndexStats
{
    std::uint64_t files = 0, scannedFolders = 0, skippedFolders = 0;
    juce::String error;
};
class Scanner
{
    Index &index;
    std::vector<std::string> roots;
    std::function<void(const IndexStats &)> progress;

  public:
    Scanner(Index &destination, std::vector<std::string> folders,
            std::function<void(const IndexStats &)> update = {})
        : index(destination), roots(std::move(folders)), progress(std::move(update))
    {
    }
    bool run(const std::function<bool()> &canceled, IndexStats &stats)
    {
        std::vector<juce::File> pending;
        std::set<std::string> visited;
        for (const auto &root : roots)
            if (juce::File::isAbsolutePath(juce::String(root)))
                pending.emplace_back(root);
        while (!pending.empty())
        {
            if (canceled())
                return false;
            auto folder = std::move(pending.back());
            pending.pop_back();
            if (!visited.insert(normalizedPath(folder.getFullPathName().toStdString())).second)
                continue;
            if (!folder.isDirectory())
            {
                ++stats.skippedFolders;
                continue;
            }
            ++stats.scannedFolders;
            for (const auto &item : juce::RangedDirectoryIterator(
                     folder, false, "*", juce::File::findFilesAndDirectories, juce::File::FollowSymlinks::no))
            {
                if (canceled())
                    return false;
                const auto file = item.getFile();
                if (file.isSymbolicLink())
                    continue; // Do not traverse links; physical junction coverage is separate.
                if (item.isDirectory())
                    pending.push_back(file);
                else if (file.hasFileExtension(audioExtensions))
                {
                    const auto name = file.getFileName();
                    index.insert(
                        {file.getFullPathName().toStdString(), name.toStdString(),
                         name.toLowerCase().toStdString(),
                         file.getFileExtension().trimCharactersAtStart(".").toLowerCase().toStdString(),
                         file.getParentDirectory().getFullPathName().toStdString(), item.getFileSize(),
                         item.getModificationTime().toMilliseconds()});
                    ++stats.files;
                }
                if (stats.files + stats.scannedFolders > 500000)
                {
                    stats.error = "Library index resource limit reached; choose smaller search folders";
                    return false;
                }
                if (progress)
                    progress(stats);
            }
        }
        return true;
    }
};
// Local, atomic cache; no personal path is chosen by this module. Call only on
// a background/control thread with an explicit cache location.
inline bool saveCache(const juce::File &file, const std::vector<std::string> &roots,
                      const std::vector<Entry> &entries)
{
    if (file == juce::File{})
        return true;
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("version", indexFormatVersion);
    object->setProperty("built", juce::Time::currentTimeMillis());
    juce::Array<juce::var> folders, rows;
    for (const auto &root : roots)
        folders.add(juce::String(root));
    for (const auto &entry : entries)
    {
        auto row = std::make_unique<juce::DynamicObject>();
        row->setProperty("path", juce::String(entry.path));
        row->setProperty("size", entry.size);
        row->setProperty("modified", entry.modified);
        rows.add(juce::var(row.release()));
    }
    object->setProperty("roots", folders);
    object->setProperty("entries", rows);
    const auto text = juce::JSON::toString(juce::var(object.release()), true);
    if (text.getNumBytesAsUTF8() > 64 * 1024 * 1024 || !file.getParentDirectory().createDirectory())
        return false;
    juce::TemporaryFile temporary(file);
    return temporary.getFile().replaceWithText(text) && temporary.overwriteTargetFileWithTemporary();
}
inline bool loadCache(Index &index, const juce::File &file, const std::vector<std::string> &roots)
{
    if (!file.existsAsFile() || file.getSize() > 64 * 1024 * 1024)
        return false;
    const auto data = juce::JSON::parse(file);
    if (!data["version"].isInt() || int(data["version"]) != indexFormatVersion)
        return false;
    const auto *folders = data["roots"].getArray(), *rows = data["entries"].getArray();
    if (!folders || !rows || folders->size() != int(roots.size()) || rows->size() > 500000)
        return false;
    for (int i = 0; i < folders->size(); ++i)
        if (!(*folders)[i].isString() || (*folders)[i].toString() != juce::String(roots[std::size_t(i)]))
            return false;
    if (!data["built"].isInt64() && !data["built"].isInt())
        return false;
    const auto age = juce::Time::currentTimeMillis() - juce::int64(data["built"]);
    if (age < 0 || age > 24 * 60 * 60 * juce::int64(1000))
        return false;
    Index candidate;
    for (const auto &row : *rows)
    {
        if (!row["path"].isString() || (!row["size"].isInt64() && !row["size"].isInt()) ||
            (!row["modified"].isInt64() && !row["modified"].isInt()))
            return false;
        const auto path = row["path"].toString();
        if (path.length() > 32768 || !juce::File::isAbsolutePath(path))
            return false;
        const juce::File file(path);
        if (!file.hasFileExtension(audioExtensions) || juce::int64(row["size"]) < 0)
            return false;
        bool allowed = false;
        for (const auto &root : roots)
            allowed |= withinFolder(file.getParentDirectory().getFullPathName().toStdString(), root);
        if (!allowed)
            return false;
        const auto name = file.getFileName();
        candidate.insert({path.toStdString(), name.toStdString(), name.toLowerCase().toStdString(),
                          file.getFileExtension().trimCharactersAtStart(".").toLowerCase().toStdString(),
                          file.getParentDirectory().getFullPathName().toStdString(), juce::int64(row["size"]),
                          juce::int64(row["modified"])});
    }
    if (candidate.size() != std::size_t(rows->size()))
        return false;
    index = std::move(candidate);
    return true;
}
} // namespace vibera::browser_search
