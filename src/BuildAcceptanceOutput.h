// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Narrow explicit acceptance launch only. No ordinary application output policy.
#include <juce_core/juce_core.h>
#if JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace vibera::test
{
inline juce::Result validateAcceptanceAncestors(juce::File path)
{
#if JUCE_WINDOWS
    for (;;)
    {
        const auto attributes = GetFileAttributesW(path.getFullPathName().toWideCharPointer());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            const auto error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                return juce::Result::fail("Acceptance output ancestry is unavailable");
        }
        else if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
            return juce::Result::fail("Acceptance output requires ordinary non-reparse directory ancestors");
        const auto parent = path.getParentDirectory();
        if (parent == path)
            break;
        path = parent;
    }
    return juce::Result::ok();
#else
    juce::ignoreUnused(path);
    return juce::Result::fail("This acceptance launch requires Windows");
#endif
}
inline bool ordinaryAcceptanceInput(const juce::File &file)
{
#if JUCE_WINDOWS
    const auto attributes = GetFileAttributesW(file.getFullPathName().toWideCharPointer());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY));
#else
    juce::ignoreUnused(file);
    return false;
#endif
}
inline bool acceptanceHex(const juce::String &text, int digits)
{
    if (text.length() != digits)
        return false;
    for (const auto c : text)
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F'))
            return false;
    return true;
}
inline juce::Result validateAcceptancePackage(const juce::File &folder)
{
    if (auto valid = validateAcceptanceAncestors(folder); valid.failed())
        return valid;
    for (const auto *name :
         {"VibeRa Studio.exe", "VibeRaTests.exe", "VibeRa Plugin Worker.exe", "provenance.json"})
        if (!ordinaryAcceptanceInput(folder.getChildFile(name)))
            return juce::Result::fail(
                "Package acceptance requires ordinary packaged application/test/worker/provenance inputs");
    const auto provenance = folder.getChildFile("provenance.json");
    if (provenance.getSize() < 2 || provenance.getSize() > 4 * 1024 * 1024)
        return juce::Result::fail("Package acceptance provenance is unavailable or exceeds4MiB");
    const auto manifest = juce::JSON::parse(provenance);
    const auto *files = manifest["files"].getArray();
    const auto *sources = manifest["sourceHashes"].getArray();
    if (!manifest.isObject() || !manifest["format"].isInt() || int(manifest["format"]) != 1 ||
        !manifest["sourceCommit"].isString() || !acceptanceHex(manifest["sourceCommit"].toString(), 40) ||
        !files || !sources || sources->isEmpty())
        return juce::Result::fail("Package acceptance requires existing versioned package provenance");
    for (const auto *name : {"VibeRa Studio.exe", "VibeRaTests.exe", "VibeRa Plugin Worker.exe"})
    {
        int matches = 0;
        for (const auto &entry : *files)
            if (entry.isObject() && entry["path"].isString() && entry["path"].toString() == name)
            {
                if (++matches != 1 || !entry["sha256"].isString() ||
                    !acceptanceHex(entry["sha256"].toString(), 64))
                    return juce::Result::fail(
                        "Package acceptance provenance has an ambiguous/invalid binary input");
            }
        if (matches != 1)
            return juce::Result::fail("Package acceptance provenance omits a required packaged binary");
    }
    // distribution.py verifies the full cryptographic manifest before package
    // acceptance. This is the narrow location/input guard, not a second verifier.
    return juce::Result::ok();
}
// Nonmutating admission, factored for original path tests. Actual launch callers
// ALWAYS pass currentExecutableFile; no command-line executable/root override.
inline juce::Result validateBuildAcceptanceOutput(const juce::String &rawOutput, const juce::File &executable,
                                                  juce::File &output)
{
    if (rawOutput.length() < 3 || !juce::CharacterFunctions::isLetter(rawOutput[0]) || rawOutput[1] != ':' ||
        (rawOutput[2] != '/' && rawOutput[2] != '\\') || !juce::File::isAbsolutePath(rawOutput) ||
        rawOutput.substring(2).containsChar(':'))
        return juce::Result::fail("Acceptance output must be an ordinary absolute local-drive directory");
    const auto parts = juce::StringArray::fromTokens(rawOutput.replaceCharacter('\\', '/'), "/", "");
    for (const auto &part : parts)
        if (part == "." || part == ".." || part.endsWithChar('.') || part.endsWithChar(' '))
            return juce::Result::fail("Acceptance output refuses traversal/Win32 alias components");
    const auto folder = executable.getParentDirectory();
    const auto windows = folder.getParentDirectory().getParentDirectory();
    auto workspace = windows.getParentDirectory().getParentDirectory();
    juce::File allowed;
    if (windows.getFileName() == "windows" && windows.getParentDirectory().getFileName() == "build")
        allowed = windows; // Existing source-build layout, unchanged.
    else
    {
        workspace = folder.getParentDirectory().getParentDirectory();
        const bool staging = folder == workspace.getChildFile("build/package-stage");
        const bool current = folder == workspace.getChildFile("dist/VibeRa Studio");
        if ((!staging && !current) || (executable.getFileName() != "VibeRa Studio.exe" &&
                                       executable.getFileName() != "VibeRaTests.exe"))
            return juce::Result::fail(
                "Acceptance executable is outside source build or exact package locations");
        if (auto valid = validateAcceptancePackage(folder); valid.failed())
            return valid;
        allowed = workspace.getChildFile(staging ? "build/distribution-verification/staging"
                                                 : "build/distribution-verification/current");
    }
    if (!ordinaryAcceptanceInput(workspace.getChildFile("AGENTS.md")) ||
        !ordinaryAcceptanceInput(workspace.getChildFile("src/Studio.cpp")) ||
        !juce::File(rawOutput).isAChildOf(allowed))
        return juce::Result::fail(
            "Acceptance output must stay below its executable-derived workspace verification root");
    if (auto valid = validateAcceptanceAncestors(juce::File(rawOutput)); valid.failed())
        return valid;
    output = juce::File(rawOutput);
    return juce::Result::ok();
}
inline juce::Result prepareBuildAcceptanceAttempt(const juce::String &rawOutput, juce::File &attempt)
{
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File output;
    if (auto valid = validateBuildAcceptanceOutput(rawOutput, executable, output); valid.failed())
        return valid;
    if (auto created = output.createDirectory(); created.failed())
        return created;
    if (auto valid = validateAcceptanceAncestors(output); valid.failed())
        return valid;
    auto fresh = output.getChildFile("attempt-" + juce::Uuid().toString());
    if (fresh.exists())
        return juce::Result::fail("Acceptance attempt already exists; refusing overwrite");
    if (auto created = fresh.createDirectory(); created.failed())
        return created;
    if (auto valid = validateAcceptanceAncestors(fresh); valid.failed())
        return valid;
    attempt = std::move(fresh);
    return juce::Result::ok();
}
} // namespace vibera::test
