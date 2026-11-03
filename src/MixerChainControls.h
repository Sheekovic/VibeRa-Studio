// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include <cmath>
namespace vibera
{
// CONTROL only; Controller calls this on its private prepared next document.
// Host mix is not a native plugin parameter. Never change native state/bypass flags.
inline juce::Result setMixerChainControl(Project &project, const juce::String &command, Id plugin, int insert,
                                         double wet, bool bypass)
{
    if (command == "plugin.effect.mix")
    {
        if (!std::isfinite(wet) || wet < 0 || wet > 1)
            return juce::Result::fail("Host wet mix must be a finite number from0 to1");
        for (auto &chain : project.mixer)
            for (auto &effect : chain.effects)
                if (effect.id == plugin)
                {
                    effect.wet = float(wet);
                    return juce::Result::ok();
                }
        return juce::Result::fail(
            "Host wet mix requires an existing Mixer effect ID, not an instrument or Channel");
    }
    if (command == "mixer.effects.bypass")
    {
        for (auto &chain : project.mixer)
            if (chain.index == insert)
            {
                chain.effectsBypassed = bypass;
                return juce::Result::ok();
            }
        return juce::Result::fail("Chain bypass requires an existing Mixer insert index");
    }
    return juce::Result::fail("Unsupported Mixer chain control");
}
} // namespace vibera
