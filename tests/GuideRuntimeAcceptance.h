// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera::test
{
// Actual original GuidePlugin component/native-lifecycle checks only.
// Definition included after GuidePlugin in AudioEngine.cpp; no private DSP API.
void guideRuntimeTests(const juce::File &ownedOutputFolder);
} // namespace vibera::test
