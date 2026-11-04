// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/Updater.h"

// In-app updater logic (src/Updater.h): version parsing, GitHub release selection per
// platform, preview/skip rules and checksum verification. No network.
namespace updater_acceptance
{
using namespace vibera::updates;

inline juce::var releasesFixture()
{
    // Shape of GitHub's GET /repos/{owner}/{repo}/releases, newest first, with a draft,
    // a stable release, a preview and an unversioned tag.
    return juce::JSON::parse(R"JSON([
      {"tag_name":"v0.0.90","name":"Draft","draft":true,"prerelease":false,"body":"draft","assets":[
        {"name":"VibeRa-Studio-0.0.90-windows-x64-Setup.exe","browser_download_url":"https://example.invalid/d.exe","size":1}]},
      {"tag_name":"v0.0.80","name":"VibeRa Studio 0.0.80","draft":false,"prerelease":true,"body":"Preview notes",
       "html_url":"https://github.com/x/y/releases/tag/v0.0.80","assets":[
        {"name":"VibeRa-Studio-0.0.80-windows-x64-Setup.exe","browser_download_url":"https://example.invalid/80.exe","size":10},
        {"name":"VibeRa-Studio-0.0.80-windows-x64-Setup.exe.sha256","browser_download_url":"https://example.invalid/80.sha","size":1}]},
      {"tag_name":"v0.0.75","name":"VibeRa Studio 0.0.75","draft":false,"prerelease":false,"body":"Stable notes","assets":[
        {"name":"VibeRa-Studio-0.0.75-windows-x64-Setup.exe","browser_download_url":"https://example.invalid/75.exe","size":10},
        {"name":"VibeRa-Studio-0.0.75-windows-x64-Setup.exe.sha256","browser_download_url":"https://example.invalid/75.sha","size":1},
        {"name":"VibeRa-Studio-0.0.75-linux-x64.tar.gz","browser_download_url":"https://example.invalid/75.tgz","size":10},
        {"name":"VibeRa-Studio-0.0.75-macos-universal.zip","browser_download_url":"https://example.invalid/75.zip","size":10},
        {"name":"insecure.exe","browser_download_url":"http://example.invalid/x.exe","size":1}]},
      {"tag_name":"nightly","name":"Nightly","draft":false,"prerelease":true,"assets":[]},
      {"tag_name":"v0.0.70","name":"Old","draft":false,"prerelease":false,"assets":[
        {"name":"VibeRa-Studio-0.0.70-windows-x64-Setup.exe","browser_download_url":"https://example.invalid/70.exe","size":1}]}
    ])JSON");
}

inline void tests(const juce::File &output)
{
    // Versions.
    require(parseVersion("v0.0.73") == Version{0, 0, 73} && parseVersion("0.1") == Version{0, 1, 0} &&
                parseVersion("V2.3.4-preview.1") == Version{2, 3, 4} && parseVersion(" 1.0.0+build ") == Version{1, 0, 0},
            "Versions parse with optional v, missing parts and suffixes");
    require(!parseVersion("nightly") && !parseVersion("") && !parseVersion("1.2.3.4") && !parseVersion("1..2") &&
                !parseVersion("v1.x"),
            "Non-versions are rejected");
    require(Version{0, 0, 74} > Version{0, 0, 73} && Version{0, 1, 0} > Version{0, 0, 99} &&
                Version{1, 0, 0} > Version{0, 99, 99},
            "Versions compare numerically, not as text");

    // Releases.
    const auto releases = parseReleases(releasesFixture());
    require(releases.size() == 3, "Drafts and unversioned tags are ignored");
    require(releases[0].tag == "v0.0.80" && releases[0].prerelease && releases[0].notes == "Preview notes" &&
                releases[0].page.startsWith("https://github.com/"),
            "Release fields are read");
    require(releases[1].assets.size() == 4, "Only https assets are kept");

    const Version current{0, 0, 73};
    const auto withPreview = newestUpdate(releases, current, true, {}, "windows-x64");
    require(withPreview && withPreview->tag == "v0.0.80", "With previews, the newest release is offered");
    const auto stable = newestUpdate(releases, current, false, {}, "windows-x64");
    require(stable && stable->tag == "v0.0.75", "Without previews, the newest stable release is offered");
    const auto skipped = newestUpdate(releases, current, true, "v0.0.80", "windows-x64");
    require(skipped && skipped->tag == "v0.0.75", "A skipped version is passed over for an older newer one");
    require(!newestUpdate(releases, Version{0, 0, 80}, true, {}, "windows-x64"), "The current version is not offered");
    require(!newestUpdate(releases, Version{0, 1, 0}, true, {}, "windows-x64"), "Older releases are not offered");
    const auto linux = newestUpdate(releases, current, true, {}, "linux-x64");
    require(linux && linux->tag == "v0.0.75", "A release without this platform's file is skipped");

    // Assets per platform.
    const auto &r75 = releases[1];
    const auto *windows = installerAsset(r75, "windows-x64");
    require(windows && windows->name.endsWith("-windows-x64-Setup.exe"), "Windows installs the setup");
    require(installerAsset(r75, "linux-x64") && installerAsset(r75, "linux-x64")->name.endsWith(".tar.gz") &&
                installerAsset(r75, "macos-universal") &&
                installerAsset(r75, "macos-universal")->name.endsWith("-macos-universal.zip"),
            "Linux and macOS download their archives");
    require(checksumAsset(r75, *windows) && checksumAsset(r75, *windows)->name == windows->name + ".sha256" &&
                !checksumAsset(r75, *installerAsset(r75, "linux-x64")),
            "Checksums pair by name; a missing one is reported");

    // Checksums (format of scripts/ci-package.py) and file hashing.
    const juce::String digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    require(parseChecksum(digest + "  file.exe\n", "file.exe") == digest &&
                parseChecksum(digest.toUpperCase() + " *file.exe", "file.exe") == digest &&
                parseChecksum(digest, "file.exe") == digest,
            "Checksum lines parse");
    require(!parseChecksum(digest + "  other.exe", "file.exe") && !parseChecksum("abc  file.exe", "file.exe") &&
                !parseChecksum("", "file.exe"),
            "Wrong-name, short or empty checksums are rejected");
    const auto sample = output.getChildFile("update-sample.bin");
    require(output.createDirectory() && sample.replaceWithText("abc"), "Write a sample file");
    require(fileSha256(sample) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "File hashing is SHA-256 of the bytes (FIPS 180-2 \"abc\" vector)");
    require(fileSha256(output.getChildFile("missing.bin")).isEmpty(), "A missing file has no hash");
}
} // namespace updater_acceptance
