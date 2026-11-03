// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <juce_cryptography/juce_cryptography.h>
#include <map>

// Missing samples and audio files: a project keeps working (and saving) when a file is gone,
// lists what is missing, and can find the files again in a chosen folder.
namespace vibera
{
inline bool mediaChannel(const Channel &channel)
{
    return !channel.instrument && !channel.source.startsWith("builtin:") && channel.source.isNotEmpty();
}
// Channels whose sample or audio file is not on disk.
inline std::vector<Id> missingMedia(const Project &project)
{
    std::vector<Id> missing;
    for (const auto &channel : project.channels)
        if (mediaChannel(channel) &&
            (!juce::File::isAbsolutePath(channel.source) || !juce::File(channel.source).existsAsFile()))
            missing.push_back(channel.id);
    return missing;
}
// Looks under `folder` (recursively, at most `fileLimit` files) for each missing file: the same
// file name first (case-insensitive); for collected media named by MD5 (see saveProject), a file
// with matching content. Returns Channel -> found absolute path; files not found are left out.
inline std::vector<std::pair<Id, juce::String>> findMissingMedia(const Project &project, const juce::File &folder,
                                                                 int fileLimit = 50000)
{
    std::vector<std::pair<Id, juce::String>> found;
    const auto missing = missingMedia(project);
    if (missing.empty() || !folder.isDirectory())
        return found;
    std::map<juce::String, juce::File> byName;
    std::vector<juce::File> candidates;
    int seen = 0;
    for (const auto &entry : juce::RangedDirectoryIterator(folder, true, "*", juce::File::findFiles))
    {
        if (++seen > fileLimit)
            break;
        const auto file = entry.getFile();
        byName.emplace(file.getFileName().toLowerCase(), file);
        candidates.push_back(file);
    }
    std::map<juce::String, juce::String> hashes; // Path -> MD5, computed on demand.
    int hashed = 0;
    for (const auto id : missing)
    {
        const auto *channel = project.channel(id);
        const juce::File wanted(channel->source.replaceCharacter('\\', '/'));
        const auto name = wanted.getFileName();
        if (auto hit = byName.find(name.toLowerCase()); hit != byName.end())
        {
            found.push_back({id, hit->second.getFullPathName()});
            continue;
        }
        const auto stem = wanted.getFileNameWithoutExtension().toLowerCase();
        if (stem.length() != 32 || !stem.containsOnly("0123456789abcdef"))
            continue;
        for (const auto &candidate : candidates)
        {
            if (!candidate.hasFileExtension(wanted.getFileExtension()) || candidate.getSize() > 512 * 1024 * 1024)
                continue;
            auto &hash = hashes[candidate.getFullPathName()];
            if (hash.isEmpty())
            {
                if (++hashed > 4000)
                    break;
                hash = juce::MD5(candidate).toHexString();
            }
            if (hash == stem)
            {
                found.push_back({id, candidate.getFullPathName()});
                break;
            }
        }
    }
    return found;
}
} // namespace vibera
