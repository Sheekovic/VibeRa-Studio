// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "CallbackAllocationProbe.h"
#include <optional>

inline void numberSerializationTests(const juce::File &folder)
{
    // Independent fixed decimal/scientific oracles; do not derive expected text
    // from the implementation being tested or accept partial numeric output.
    require(juce::String(1.25, 2) == "1.25", "Exact decimal formatting");
    require(juce::String(-.125, 3) == "-0.125", "Exact negative fraction");
    require(juce::String(-0., 2) == "-0.00", "Signed zero preserved");
    require(juce::String(1250., 2, true) == "1.25e+03", "Scientific exponent preserved");
    require(juce::String(.00000125, 3, true) == "1.250e-06", "Small exponent precision");
    bool exhausted = false;
    int faults = 0;
    for (int site = 0; site < 256 && !exhausted; ++site)
    {
        std::optional<juce::String> converted;
        bool threw = false;
        preparationAllocationFailureHit = false;
        preparationAllocationFailureAfter = site;
        try { converted.emplace(1250., 2, true); }
        catch (...) { threw = true; }
        preparationAllocationFailureAfter = -1;
        if (preparationAllocationFailureHit)
        {
            ++faults;
            require(threw && !converted, "Every reached numeric allocation fault propagates without partial text");
        }
        else
        {
            require(!threw && converted && *converted == "1.25e+03", "First nonfaulted conversion remains exact");
            exhausted = true;
        }
    }
    require(exhausted && faults > 0, "All actual sequential C++ numeric allocation sites reached");
    require(folder.getChildFile("number-serialization.txt").replaceWithText(
                "PASS five fixed normal-format oracles and " + juce::String(faults) +
                " reached C++ allocation faults, no silent partial conversion or terminate. "
                "No arbitrary CRT/whole-process OOM or audio qualification.\n"),
            "Numeric exception-safety receipt");
}
