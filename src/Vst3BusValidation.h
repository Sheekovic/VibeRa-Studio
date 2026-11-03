// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/vstspeaker.h"

namespace vibera
{
// Worker preparation/control thread only, before starting DSP. JUCE's public
// layout can be cached even after native setupProcessing refuses arrangements.
// Query supported SDK interfaces independently; do not trust that cached count
// to describe native pointers. No external plugin ABI or vendor patch changes.
inline bool nativeVst3BusesMatch(juce::AudioPluginInstance &plugin,
                                 const juce::AudioProcessor::BusesLayout &requested)
{
    using namespace Steinberg;
    using namespace Steinberg::Vst;
    auto *bridge = plugin.getVST3Client();
    auto *component = bridge ? bridge->getIComponentPtr() : nullptr;
    IAudioProcessor *processor = nullptr;
    if (!component ||
        component->queryInterface(IAudioProcessor_iid, reinterpret_cast<void **>(&processor)) != kResultOk ||
        !processor)
        return false;
    const auto matchesDirection = [&](bool input)
    {
        const auto direction = input ? kInput : kOutput;
        const auto &buses = input ? requested.inputBuses : requested.outputBuses;
        if (component->getBusCount(kAudio, direction) != buses.size())
            return false;
        for (int i = 0; i < buses.size(); ++i)
        {
            // JUCE keeps a disabled bus's last enabled native arrangement while
            // activateBus disables delivery. Only enabled buses reach DSP.
            if (buses[i].isDisabled())
                continue;
            SpeakerArrangement actual = SpeakerArr::kEmpty;
            const auto expected = buses[i] == juce::AudioChannelSet::stereo() ? SpeakerArr::kStereo
                                  : buses[i] == juce::AudioChannelSet::mono() ? SpeakerArr::kMono
                                                                              : SpeakerArr::kEmpty;
            if (expected == SpeakerArr::kEmpty ||
                processor->getBusArrangement(direction, i, actual) != kResultOk || actual != expected)
                return false;
        }
        return true;
    };
    const bool matches = matchesDirection(true) && matchesDirection(false);
    processor->release();
    return matches;
}
} // namespace vibera
