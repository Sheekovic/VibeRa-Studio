// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_core/juce_core.h>

namespace vibera::synth
{
// Application-owned operations use the same semantic services as the native UI.
// These IDs are inventory/macro contracts, never independent model mutations.
inline juce::String actionForCommand(const juce::String &id)
{
    if(id=="synth.discover") return "list";
    if(id=="synth.browser.state") return "browser";
    if(id=="synth.browser.filter") return "filter";
    if(id=="synth.browser.favorite") return "favorite";
    if(id=="synth.editor.navigate") return "navigate";
    if(id=="synth.preset.load") return "loadPreset";
    if(id=="synth.preset.previous") return "previous";
    if(id=="synth.preset.next") return "next";
    if(id=="synth.user.list") return "listUserPresets";
    if(id=="synth.user.save") return "saveUserPreset";
    if(id=="synth.user.load") return "loadUserPreset";
    if(id=="synth.parameters") return "parameters";
    if(id=="synth.parameter.set") return "set";
    if(id=="synth.preview.on") return "previewOn";
    if(id=="synth.preview.off") return "previewOff";
    if(id=="synth.preview.panic") return "panic";
    if(id=="synth.editor.open") return "openEditor";
    if(id=="synth.editor.close") return "closeEditor";
    return {};
}
}
