// SPDX-License-Identifier: AGPL-3.0-or-later
// Original standalone acceptance launcher. No DAW, Tracktion, plugin or hardware.
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_cryptography/juce_cryptography.h>
#include "BuildAcceptanceOutput.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#ifndef VIBERA_PHYSICAL_BURST_V2_VERIFIED
#error The leaf target requires exact pinned physical-burst v2 prerequisites, not v1 or pristine JUCE.
#endif
#if !JUCE_WINDOWS
#error This owned-output acceptance launcher is for the existing Windows build cache.
#endif

static void require(bool condition, const std::string &message)
{
    if (!condition)
        throw std::runtime_error(message);
}

#include "CallbackAllocationProbe.h"
#include "SyntheticDevice.h"
#include "PhysicalBurstAcceptance.h"

namespace
{
juce::String binaryHash(const juce::File &file)
{
    auto stream = file.createInputStream();
    require(stream && stream->openedOk(), "Cannot read actual leaf executable for exact identity");
    const auto size = file.getSize();
    require(size > 0, "Leaf executable cannot be empty");
    const auto hash = juce::SHA256(*stream).toHexString();
    require(stream->getPosition() == size && file.getSize() == size,
            "Leaf executable changed or became unreadable during identity capture");
    return hash;
}

juce::var receipt(const juce::File &executable, const juce::String &hash, const juce::File &attempt,
                  const char *state, const juce::String &failure, double elapsedMs)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty("schemaVersion", 1);
    object->setProperty("operation", "physical-burst-v2-leaf");
    object->setProperty("completionState", state);
    object->setProperty("passed", juce::String(state) == "completed");
    object->setProperty("executable", executable.getFullPathName());
    object->setProperty("executableSha256", hash);
    object->setProperty("attempt", attempt.getFullPathName());
    object->setProperty("scenario", attempt.getChildFile("scenario").getFullPathName());
    object->setProperty("elapsedMilliseconds", elapsedMs);
    object->setProperty("error", failure.isEmpty() ? juce::var{} : juce::var(failure));
    object->setProperty("prerequisites",
                        "Pinned physical-callback vendor family verified "
                        "by target configuration; contract/fixture are compiled inputs. Build receipts must "
                        "bind their exact source identities to the leaf binary above.");
    object->setProperty("evidenceKind",
                        "Actual standalone JUCE manager/enforcer + original synthetic device");
    object->setProperty("nativeOsInput", false);
    object->setProperty("physicalAudioOrMidi", false);
    object->setProperty("pluginHosting", false);
    object->setProperty("tracktionGraph", false);
    object->setProperty("listening", false);
    object->setProperty("callbackHeapScope",
                        "Observed owner-thread C++ new/delete only, not arbitrary CRT or provider heap");
    object->setProperty("qualificationLimit",
                        "Not real backend maximum, physical deadline, hosted fallback/recovery, "
                        "whole-source graph-speed or production policy qualification.");
    return object.release();
}

bool writeReceipt(const juce::File &file, const juce::var &value)
{
    // These are UUID-owned attempt paths, never project/user files or prior runs.
    return !file.exists() && file.replaceWithText(juce::JSON::toString(value));
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 3 || std::string_view(argv[1]) != "--physical-burst-check")
    {
        std::cerr << "Usage: VibeRaPhysicalBurstTests --physical-burst-check <owned-absolute-build-output>\n";
        return 2;
    }

    juce::File attempt;
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::String identity;
    const auto began = juce::Time::getMillisecondCounterHiRes();
    try
    {
        const auto prepared = vibera::test::prepareBuildAcceptanceAttempt(argv[2], attempt);
        require(prepared.wasOk(), prepared.getErrorMessage().toStdString());
        identity = binaryHash(executable);
        require(writeReceipt(attempt.getChildFile("startup.json"),
                             receipt(executable, identity, attempt, "running", {}, 0)),
                "Cannot preserve actual leaf startup receipt; scenario was not started");
        // Events/message-manager initialization only; the selected synthetic
        // device owns all callbacks. No physical audio driver or GUI is opened.
        juce::ScopedJuceInitialiser_GUI initialiser;
        const auto scenarioBegan = juce::Time::getMillisecondCounterHiRes();
        physicalBurstTests(attempt.getChildFile("scenario")); // The unchanged fixture requires fresh child.
        physicalBurstReaderTests(attempt.getChildFile("retained-reader"));
        const auto scenarioMs = juce::Time::getMillisecondCounterHiRes() - scenarioBegan;
        require(binaryHash(executable) == identity, "Leaf executable identity changed during acceptance");
        auto result = receipt(executable, identity, attempt, "completed", {},
                              juce::Time::getMillisecondCounterHiRes() - began);
        result.getDynamicObject()->setProperty("scenarioMilliseconds", scenarioMs);
        require(writeReceipt(attempt.getChildFile("verification.json"), result),
                "Cannot preserve terminal leaf verification receipt");
        std::cout << "PASS physical-burst v2 leaf: " << attempt.getFullPathName() << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        const auto message = juce::String::fromUTF8(error.what());
        if (attempt.existsAsFile() || !attempt.isDirectory())
        {
            std::cerr << "FAIL physical-burst leaf (no valid owned attempt): " << error.what() << '\n';
            return 1;
        }
        try
        {
            const auto failed = receipt(executable, identity, attempt, "failed", message,
                                        juce::Time::getMillisecondCounterHiRes() - began);
            if (!writeReceipt(attempt.getChildFile("verification.json"), failed))
                std::cerr
                    << "Could not preserve terminal failure JSON; retain startup and scenario evidence.\n";
        }
        catch (...)
        {
            std::cerr << "Could not allocate/write terminal failure JSON; retain owned startup/scenario "
                         "evidence.\n";
        }
        std::cerr << "FAIL physical-burst leaf: " << error.what()
                  << "\nAttempt: " << attempt.getFullPathName() << '\n';
        return 1;
    }
    catch (...)
    {
        try
        {
            if (attempt.isDirectory())
                writeReceipt(attempt.getChildFile("verification.json"),
                             receipt(executable, identity, attempt, "failed", "Unclassified exception",
                                     juce::Time::getMillisecondCounterHiRes() - began));
        }
        catch (...)
        {
        }
        // A fatal process exit may leave only startup 'running'; that is
        // incomplete evidence, never terminal success.
        std::cerr << "FAIL physical-burst leaf: unclassified exception\n";
        return 1;
    }
}
