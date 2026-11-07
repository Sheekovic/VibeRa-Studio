// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace vibera
{
// Message thread only. Shows a window above the others and activates it. JUCE's toFront alone
// raises without taking the foreground, so a freshly opened window could stay behind.
void bringWindowToFront(juce::Component &window);
// Lets another process (a plugin worker about to show its editor) take the foreground once.
void allowProcessForeground(juce::uint32 processId);
} // namespace vibera
