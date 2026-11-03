// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginHost.h"
namespace vibera::test
{
// Original component/actual-worker experiment; declaration only. Definition is
// included after FixedSampler in AudioEngine.cpp so no production private DSP
// class or general mutable access API is exposed. Root alone wires the runner.
void instrumentRuntimeTests(const juce::File &ownedFolder, const PluginSpec &originalFixture);
} // namespace vibera::test
