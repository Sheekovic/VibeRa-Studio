// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
#include <juce_cryptography/juce_cryptography.h>
#include <optional>
#include <vector>

// Update discovery from GitHub Releases: pure parsing/selection logic, no network or UI.
// The Studio side (StudioUpdater.cpp) fetches the JSON and the files on a worker thread.
namespace vibera::updates
{
inline constexpr const char *repository = "Sheekovic/VibeRa-Studio";
inline juce::String releasesUrl()
{
    return "https://api.github.com/repos/" + juce::String(repository) + "/releases?per_page=30";
}

struct Version
{
    int major = 0, minor = 0, patch = 0;
    auto operator<=>(const Version &) const = default;
    juce::String text() const
    {
        return juce::String(major) + "." + juce::String(minor) + "." + juce::String(patch);
    }
};

// "v0.0.74", "0.0.74", "V1.2", "0.1.0-preview.2" -> numeric core; anything else is rejected.
inline std::optional<Version> parseVersion(juce::String text)
{
    text = text.trim();
    if (text.startsWithIgnoreCase("v"))
        text = text.substring(1);
    text = text.upToFirstOccurrenceOf("-", false, false).upToFirstOccurrenceOf("+", false, false);
    juce::StringArray parts;
    parts.addTokens(text, ".", {});
    if (parts.isEmpty() || parts.size() > 3)
        return std::nullopt;
    int values[3]{};
    for (int i = 0; i < parts.size(); ++i)
    {
        if (parts[i].isEmpty() || !parts[i].containsOnly("0123456789") || parts[i].length() > 6)
            return std::nullopt;
        values[i] = parts[i].getIntValue();
    }
    return Version{values[0], values[1], values[2]};
}

struct Asset
{
    juce::String name, url;
    juce::int64 size = 0;
};
struct Release
{
    juce::String tag, name, notes, page, published;
    Version version;
    bool prerelease = false;
    std::vector<Asset> assets;
};

// The archive/installer naming of scripts/ci-package.py: VibeRa-Studio-<version>-<platform>.
inline juce::String platformKey()
{
#if JUCE_WINDOWS
    return "windows-x64";
#elif JUCE_MAC
    return "macos-universal";
#else
    return "linux-x64";
#endif
}

// GitHub's /releases array. Drafts and tags without a version are ignored.
inline std::vector<Release> parseReleases(const juce::var &json)
{
    std::vector<Release> result;
    const auto *items = json.getArray();
    if (!items)
        return result;
    for (const auto &item : *items)
    {
        if (!item.isObject() || bool(item["draft"]))
            continue;
        Release release;
        release.tag = item["tag_name"].toString();
        const auto version = parseVersion(release.tag);
        if (!version)
            continue;
        release.version = *version;
        release.name = item["name"].toString().isNotEmpty() ? item["name"].toString() : release.tag;
        release.notes = item["body"].toString();
        release.page = item["html_url"].toString();
        release.published = item["published_at"].toString();
        release.prerelease = bool(item["prerelease"]);
        if (const auto *assets = item["assets"].getArray())
            for (const auto &asset : *assets)
                if (asset["name"].toString().isNotEmpty() && asset["browser_download_url"].toString().startsWith("https://"))
                    release.assets.push_back({asset["name"].toString(), asset["browser_download_url"].toString(),
                                              juce::int64(asset["size"])});
        result.push_back(std::move(release));
    }
    return result;
}

// What the user can install on this platform: the Windows setup, else the platform archive.
inline const Asset *installerAsset(const Release &release, const juce::String &platform = platformKey())
{
    const juce::StringArray suffixes =
        platform == "windows-x64" ? juce::StringArray{"-windows-x64-Setup.exe"}
        : platform == "linux-x64" ? juce::StringArray{"-linux-x64.tar.gz"}
                                  : juce::StringArray{"-" + platform + ".zip"};
    for (const auto &suffix : suffixes)
        for (const auto &asset : release.assets)
            if (asset.name.endsWithIgnoreCase(suffix))
                return &asset;
    return nullptr;
}
inline const Asset *checksumAsset(const Release &release, const Asset &file)
{
    for (const auto &asset : release.assets)
        if (asset.name == file.name + ".sha256")
            return &asset;
    return nullptr;
}

// The newest release above `current` that has an installable file for this platform.
// Preview (prerelease) builds count only with `includePreview`; `skipped` is a tag the user
// chose to skip (a later release is still offered).
inline std::optional<Release> newestUpdate(const std::vector<Release> &releases, Version current, bool includePreview,
                                           const juce::String &skipped = {},
                                           const juce::String &platform = platformKey())
{
    std::optional<Release> best;
    for (const auto &release : releases)
    {
        if (!(release.version > current) || (release.prerelease && !includePreview) ||
            (skipped.isNotEmpty() && release.tag == skipped) || !installerAsset(release, platform))
            continue;
        if (!best || release.version > best->version)
            best = release;
    }
    return best;
}

// "<64 hex>  <file name>" as written by scripts/ci-package.py (name optional, lower-case hex).
inline std::optional<juce::String> parseChecksum(const juce::String &text, const juce::String &fileName)
{
    const auto line = text.trim().upToFirstOccurrenceOf("\n", false, false).trim();
    const auto digest = line.upToFirstOccurrenceOf(" ", false, false).toLowerCase();
    const auto name = line.fromFirstOccurrenceOf(" ", false, false).trim().trimCharactersAtStart("*");
    if (digest.length() != 64 || !digest.containsOnly("0123456789abcdef") || (name.isNotEmpty() && name != fileName))
        return std::nullopt;
    return digest;
}
inline juce::String fileSha256(const juce::File &file)
{
    juce::FileInputStream input(file);
    return input.openedOk() ? juce::SHA256(input).toHexString().toLowerCase() : juce::String{};
}
} // namespace vibera::updates
