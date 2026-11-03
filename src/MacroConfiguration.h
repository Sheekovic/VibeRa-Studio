// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Customization.h"
#include <juce_cryptography/juce_cryptography.h>
namespace vibera
{
enum class MacroConfigurationChange
{
    create,
    update,
    upsert,
    remove
};
struct MacroConfigurationSnapshot
{
    juce::String generation;
    juce::var current, persisted;
    bool exists = false, matches = false;
};
// CONTROL only. Normalizes genuine old profiles through the authoritative
// importer before comparison. Never replaces corrupted/newer external data.
inline juce::Result readMacroConfiguration(const Customization &current, const juce::File &file,
                                           MacroConfigurationSnapshot &output)
{
    try
    {
        MacroConfigurationSnapshot next;
        next.current = current.state();
        next.exists = file.exists();
        juce::MemoryBlock disk;
        if (next.exists)
        {
            if (!file.existsAsFile() || file.getSize() > 1024 * 1024 || !file.loadFileAsData(disk))
                return juce::Result::fail(
                    "macro.profileUnreadable: Preserve the current settings file; cannot read it safely");
            Customization loaded;
            if (auto r = loaded.importState(juce::JSON::parse(
                    juce::String::fromUTF8(static_cast<const char *>(disk.getData()), int(disk.getSize()))));
                r.failed())
                return juce::Result::fail("macro.profileUnreadable: " + r.getErrorMessage());
            next.persisted = loaded.state();
            next.matches =
                juce::JSON::toString(next.current, true) == juce::JSON::toString(next.persisted, true);
        }
        const auto identity = juce::JSON::toString(next.current, true) + "\nprofile:" +
                              (next.exists ? juce::SHA256(disk).toHexString() : juce::String("absent"));
        next.generation =
            juce::SHA256(identity.toRawUTF8(), size_t(identity.getNumBytesAsUTF8())).toHexString();
        output = std::move(next);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("macro.preparationFailed: No settings changed");
    }
}
inline juce::Result changeMacroConfiguration(Customization &current, const juce::File &file,
                                             MacroConfigurationChange change, const Macro &macro,
                                             const juce::String &id, const juce::String &generation)
{
    if (current.running)
        return juce::Result::fail("macro.busy: Cannot edit a running recipe");
    try
    {
        MacroConfigurationSnapshot before;
        if (auto r = readMacroConfiguration(current, file, before); r.failed())
            return r;
        if (generation.isEmpty() || generation != before.generation)
            return juce::Result::fail(
                "macro.staleConfiguration: Re-read definitions/settings before applying");
        if (before.exists && !before.matches)
            return juce::Result::fail("macro.profileChanged: In-memory and saved settings differ; explicitly "
                                      "reconcile through Settings before saving a macro");
        const auto target = change == MacroConfigurationChange::remove ? id : macro.id;
        const bool exists = std::any_of(current.macros.begin(), current.macros.end(),
                                        [&](const auto &value) { return value.id == target; });
        if (change == MacroConfigurationChange::create && exists)
            return juce::Result::fail(
                "macro.alreadyExists: Use update for this exact existing stable macro ID");
        if ((change == MacroConfigurationChange::update || change == MacroConfigurationChange::remove) &&
            !exists)
            return juce::Result::fail("macro.unavailable: Exact stable macro ID does not exist");
        auto candidate = current;
        const auto result = change == MacroConfigurationChange::remove ? candidate.removeMacro(id)
                                                                       : candidate.addMacro(macro);
        if (result.failed())
            return juce::Result::fail("macro.validationFailed: " + result.getErrorMessage());
        const auto encoded = juce::JSON::toString(candidate.state());
        if (encoded.getNumBytesAsUTF8() > 1024 * 1024)
            return juce::Result::fail("macro.profileLimit: Saved settings must remain loadable within1MiB");
        MacroConfigurationSnapshot immediatelyBeforeWrite;
        if (auto r = readMacroConfiguration(current, file, immediatelyBeforeWrite); r.failed())
            return r;
        if (immediatelyBeforeWrite.generation != generation)
            return juce::Result::fail("macro.staleConfiguration: Settings changed during preparation");
        if (auto saved = candidate.save(file); saved.failed())
            return juce::Result::fail("macro.persistenceFailed: " + saved.getErrorMessage());
        // Atomic file replacement succeeded. This is the entire intrinsic
        // memory commit: no allocation, observer, UI call or fallible rollback.
        // removeMacro also erases shortcut bindings targeting that stable ID.
        // Publish ALL touched candidate fields using non-throwing vector swaps.
        static_assert(noexcept(current.bindings.swap(candidate.bindings)));
        static_assert(noexcept(current.macros.swap(candidate.macros)));
        current.bindings.swap(candidate.bindings);
        current.macros.swap(candidate.macros);
        return juce::Result::ok();
    }
    catch (...)
    {
        return juce::Result::fail("macro.preparationFailed: Macro/settings publication was not reached");
    }
}
} // namespace vibera
