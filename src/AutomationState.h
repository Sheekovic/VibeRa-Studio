// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Automation.h"
#include <juce_data_structures/juce_data_structures.h>
namespace vibera
{
juce::ValueTree automationState(const AutomationDocument &);
juce::Result parseAutomationState(const juce::ValueTree &, AutomationDocument &, int projectVersion = 25);
} // namespace vibera
