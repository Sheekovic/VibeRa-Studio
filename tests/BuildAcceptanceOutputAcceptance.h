// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "BuildAcceptanceOutput.h"

// Original path/layout admission only. Dummy files are NEVER executed and do
// not represent verified packages; distribution.py owns cryptographic verification.
static void buildAcceptanceOutputTests()
{
    const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("vibera-package-path-" + juce::Uuid().toString());
    require(!temp.exists() && temp.createDirectory().wasOk(), "Fresh owned output-guard test workspace");
    struct Cleanup
    {
        juce::File folder;
        ~Cleanup()
        {
            folder.deleteRecursively();
        } // Only this freshly generated owned test tree.
    } cleanup{temp};
    const auto write = [&](const juce::String &relative, const juce::String &text)
    {
        const auto file = temp.getChildFile(relative);
        require(file.getParentDirectory().createDirectory().wasOk() && file.replaceWithText(text),
                "Create original non-executable path-layout fixture input");
        return file;
    };
    write("AGENTS.md", "owned path fixture");
    write("src/Studio.cpp", "owned source marker");
    const auto app = write("build/windows/VibeRa_artefacts/Release/VibeRa Studio.exe", "NOT EXECUTABLE");
    const auto manifest = []
    {
        auto *root = new juce::DynamicObject;
        root->setProperty("format", 1);
        root->setProperty("sourceCommit", juce::String::repeatedString("a", 40));
        auto *source = new juce::DynamicObject;
        source->setProperty("path", "src/Studio.cpp");
        source->setProperty("sha256", juce::String::repeatedString("b", 64));
        root->setProperty("sourceHashes", juce::Array<juce::var>{juce::var(source)});
        juce::Array<juce::var> files;
        for (const auto *name : {"VibeRa Studio.exe", "VibeRaTests.exe", "VibeRa Plugin Worker.exe"})
        {
            auto *file = new juce::DynamicObject;
            file->setProperty("path", name);
            file->setProperty("sha256", juce::String::repeatedString("c", 64));
            files.add(juce::var(file));
        }
        root->setProperty("files", files);
        return juce::var(root);
    };
    for (const auto *directory : {"build/package-stage", "dist/VibeRa Studio"})
    {
        for (const auto *name : {"VibeRa Studio.exe", "VibeRaTests.exe", "VibeRa Plugin Worker.exe"})
            write(juce::String(directory) + "/" + name, "NOT EXECUTABLE");
        write(juce::String(directory) + "/provenance.json", juce::JSON::toString(manifest()));
    }
    juce::File output = temp.getChildFile("sentinel");
    const auto admitted = [&](const juce::File &executable, const juce::File &destination)
    {
        return vibera::test::validateBuildAcceptanceOutput(destination.getFullPathName(), executable, output)
                   .wasOk() &&
               output == destination && !destination.exists();
    };
    const auto refused = [&](const juce::File &executable, const juce::String &destination)
    {
        output = temp.getChildFile("sentinel");
        const auto result = vibera::test::validateBuildAcceptanceOutput(destination, executable, output);
        return result.failed() && output == temp.getChildFile("sentinel");
    };
    const auto sourceOut = temp.getChildFile("build/windows/fixture");
    const auto stageOut = temp.getChildFile("build/distribution-verification/staging/fixture");
    const auto currentOut = temp.getChildFile("build/distribution-verification/current/fixture");
    const auto stageApp = temp.getChildFile("build/package-stage/VibeRa Studio.exe");
    const auto currentApp = temp.getChildFile("dist/VibeRa Studio/VibeRa Studio.exe");
    require(admitted(app, sourceOut),
            "Existing source executable keeps exact build/windows output admission");
    require(admitted(stageApp, stageOut), "Staging app admits only corresponding staging verification child");
    require(admitted(currentApp, currentOut),
            "Final app admits only corresponding current verification child");
    require(admitted(temp.getChildFile("build/package-stage/VibeRaTests.exe"), stageOut) &&
                admitted(temp.getChildFile("dist/VibeRa Studio/VibeRaTests.exe"), currentOut),
            "Actual packaged test runner shares narrow corresponding output roots");
    for (const auto &destination :
         {sourceOut, currentOut, temp.getChildFile("unrelated-output"),
          temp.getChildFile("build/distribution-verification/staging-sibling/fixture")})
        require(refused(stageApp, destination.getFullPathName()),
                "Staging cannot write source/current/foreign/prefix-sibling outputs");
    require(
        refused(currentApp, stageOut.getFullPathName()) && refused(app, stageOut.getFullPathName()) &&
            refused(stageApp, temp.getChildFile("build/distribution-verification/staging").getFullPathName()),
        "Source/final/staging roots cannot cross and verification root itself is not a fixture output");
    for (const auto &raw :
         {juce::String("relative/output"), juce::String("\\\\server\\share\\output"),
          stageOut.getFullPathName() + ":ads", stageOut.getFullPathName() + "/../escape",
          stageOut.getFullPathName() + "/./child", stageOut.getFullPathName() + "/.. /escape"})
        require(refused(stageApp, raw),
                "Relative/UNC/ADS/traversal/Win32-alias paths refuse before normalization");
    require(
        refused(temp.getChildFile("build/package-input/VibeRa Studio.exe"), stageOut.getFullPathName()) &&
            refused(temp.getChildFile("dist/.previous/VibeRa Studio.exe"), currentOut.getFullPathName()) &&
            refused(temp.getChildFile("build/package-stage/foreign.exe"), stageOut.getFullPathName()),
        "Package input/retired/arbitrary executable paths never gain fixture-output authority");
    const auto worker = temp.getChildFile("build/package-stage/VibeRa Plugin Worker.exe");
    require(worker.deleteFile() && refused(stageApp, stageOut.getFullPathName()),
            "Missing packaged worker refuses");
    write("build/package-stage/VibeRa Plugin Worker.exe", "NOT EXECUTABLE");
    const auto provenance = temp.getChildFile("build/package-stage/provenance.json");
    require(provenance.replaceWithText("{}") && refused(stageApp, stageOut.getFullPathName()),
            "Malformed/missing versioned provenance refuses");
    auto missing = manifest();
    missing.getDynamicObject()->removeProperty("files");
    require(provenance.replaceWithText(juce::JSON::toString(missing)) &&
                refused(stageApp, stageOut.getFullPathName()),
            "Provenance missing binary inventory refuses");
    auto duplicate = manifest();
    const auto duplicateFile = duplicate["files"].getArray()->getFirst();
    duplicate["files"].getArray()->add(duplicateFile);
    require(provenance.replaceWithText(juce::JSON::toString(duplicate)) &&
                refused(stageApp, stageOut.getFullPathName()),
            "Ambiguous executable provenance refuses");
    require(provenance.replaceWithText(juce::JSON::toString(manifest())), "Restore valid fixture provenance");
    require(temp.getChildFile("AGENTS.md").deleteFile() && refused(stageApp, stageOut.getFullPathName()),
            "Package location without actual ordinary source-workspace markers refuses");
    write("AGENTS.md", "owned path fixture");
    write("build/distribution-verification/staging/existing-file", "owned existing file preserved");
    require(refused(stageApp, temp.getChildFile("build/distribution-verification/staging/existing-file/child")
                                  .getFullPathName()),
            "File ancestor refuses before any output creation or overwrite");
    require(temp.getChildFile("build/distribution-verification/staging/existing-file").loadFileAsString() ==
                "owned existing file preserved",
            "Admission refusal preserves existing output data");
    // Existing Win32 ancestor rejection remains verbatim. This fixture does not
    // manufacture a reparse flag or claim a junction/symlink OS test was run.
}
