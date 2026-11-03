// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveTimeline.h"
#include "PluginAutomationPacket.h"

namespace vibera
{
// One prepared instance per instrument/effect processing owner. Caller retains
// its document lease throughout process; no document/program pointer is stored.
class PluginAutomationStream
{
  public:
    using Status = PluginHost::AutomationStatus;
    Status process(PluginHost &, const LiveDocument *, Id plugin, juce::AudioBuffer<float> &, int offset,
                   int frames, std::span<const ipc::MidiEvent>, double seconds, double sampleRate,
                   double tempo, bool playing, bool bypass, int numerator = 4, int denominator = 4,
                   const juce::AudioBuffer<float> *auxiliary = nullptr, int auxiliaryOffset = 0,
                   float wet = 1, PluginPcmCapture *capture = nullptr) noexcept;
    void reset() noexcept
    {
        packet.reset();
        nativeActive = false;
    }

  private:
    PluginAutomationPacket packet;
    AutomationProgram empty;
    bool nativeActive = false;
    struct MidiItem
    {
        ipc::MidiEvent event;
        std::size_t order;
    };
    std::array<MidiItem, ipc::events> midiItems;
    std::array<ipc::MidiEvent, ipc::events> midi;
};
} // namespace vibera
