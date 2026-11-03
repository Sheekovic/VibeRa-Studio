// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Commands.h"
namespace vibera
{
struct Binding
{
    juce::String action;
    Scope scope = Scope::global;
    juce::KeyPress key;
};
struct MacroStep
{
    juce::String action;
    CommandArgs parameters;
    bool useCurrentContext = true;
};
struct Macro
{
    juce::String id, name;
    std::vector<MacroStep> steps;
};
struct MacroContext
{
    Id pattern = 0, channel = 0;
    int insert = 0;
    std::vector<Id> notes, clips;
    std::vector<Id> channels;
    std::vector<int> mixerSelection;
    juce::String mixerGroup;
};
class Customization
{
  public:
    std::vector<Binding> bindings;
    std::vector<Macro> macros;
    bool running = false;
    bool restoreMuteAfterSolo = true;
    bool muteRemovedSteps = false; // Older profiles retain their deletion behavior.
    // Maximum understood typed profile; writers still choose the smallest required version.
    static constexpr int maximumProfileVersion = 18;
    static Customization familiarDefaults();
    juce::String resolve(const juce::KeyPress &, Scope) const;
    juce::Result bind(const juce::String &, Scope, const juce::KeyPress &, bool alternate = false);
    void unbind(const juce::String &, Scope);
    static bool macroAllowed(const juce::String &);
    juce::Result addMacro(Macro);
    juce::Result removeMacro(const juce::String &);
    juce::Result runMacro(const juce::String &, Controller &, MacroContext &,
                          std::function<bool()> cancelled = {});
    juce::var state() const;
    juce::Result importState(const juce::var &);
    juce::Result save(const juce::File &) const;
    juce::Result load(const juce::File &);
};
} // namespace vibera
