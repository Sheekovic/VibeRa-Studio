// SPDX-License-Identifier: AGPL-3.0-or-later
// Headless contract checks use the real JUCE manager, without the DAW or plugins.
#include <juce_data_structures/juce_data_structures.h>
#include <iostream>
#include <stdexcept>

static void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

#include "PreparedHistoryMutationAcceptance.h"
#include "NumberSerializationAcceptance.h"

int main(int argc, char **argv)
{
    try
    {
        const bool numbers = argc == 3 && std::string_view(argv[1]) == "--number-safety";
        const char *outputPath = numbers ? argv[2] : argc == 2 ? argv[1] : nullptr;
        if (!outputPath || !juce::File::isAbsolutePath(outputPath))
            throw std::runtime_error("Expected one absolute owned test-output directory");
        const auto buildRoot = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                   .getParentDirectory().getParentDirectory().getParentDirectory();
        const juce::File output{juce::String(outputPath)};
        if (buildRoot.getFileName() != "windows" || buildRoot.getParentDirectory().getFileName() != "build" ||
            !output.isAChildOf(buildRoot))
            throw std::runtime_error("History tests require disposable output inside the one build/windows cache");
        juce::ScopedJuceInitialiser_GUI initialiser;
        const auto attempt = output.getChildFile("attempt-" + juce::Uuid().toString());
        require(attempt.createDirectory().wasOk(), "Create unique owned history-test attempt");
        if (numbers) numberSerializationTests(attempt);
        else vibera::draft::prepared_history::tests(attempt);
        std::cout << (numbers ? "PASS actual JUCE number safety: " : "PASS actual JUCE prepared history: ") << attempt.getFullPathName() << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL prepared history: " << error.what() << '\n';
        return 1;
    }
}
