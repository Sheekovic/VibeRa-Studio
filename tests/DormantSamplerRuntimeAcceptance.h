// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>
namespace vibera::test
{
// Definition lives after actual private FixedSampler in AudioEngine.cpp.
// Actual component/native-lifecycle PCM, not graph publication or hardware.
void dormantSamplerRuntimeTests(const juce::File &ownedOutput);
} // namespace vibera::test
