// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginProtocol.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace vibera
{
// One fixed owner table per instrument. Score, MIDI, typing and preview share a MIDI1
// key, but an owner's release must never cancel another owner's held key.
class MidiNoteOwners
{
  public:
    static constexpr int sourceCount = 4; // Score0, physical MIDI1, typing2, preview3.

  private:
    std::array<std::array<std::array<std::uint16_t, 128>, 16>, sourceCount> held{};
    bool other(int source, int channel, int pitch) const noexcept
    {
        for (int i = 0; i < sourceCount; ++i)
            if (i != source && held[size_t(i)][size_t(channel)][size_t(pitch)] != 0)
                return true;
        return false;
    }

  public:
    void reset() noexcept
    {
        held = {};
    }
    template <class Emit> void process(int source, const ipc::MidiEvent &event, Emit &&emit) noexcept
    {
        if (source < 0 || source >= sourceCount || event.frame < 0 || event.size == 0 || event.size > 3 ||
            event.bytes[0] < 0x80)
            return;
        const int channel = event.bytes[0] & 15, kind = event.bytes[0] & 0xf0;
        if (kind < 0xf0)
        {
            const int size = kind == 0xc0 || kind == 0xd0 ? 2 : 3;
            if (event.size != size || event.bytes[1] > 127 || (size == 3 && event.bytes[2] > 127))
                return;
        }
        const int pitch = event.bytes[1] & 127;
        auto &owner = held[size_t(source)][size_t(channel)];
        if (kind == 0x90 && event.bytes[2] != 0)
        {
            const bool shared = other(source, channel, pitch);
            // Score gates may overlap. Physical/typing/preview lanes already coalesce
            // repeated on events, and their final off releases that lane's key.
            if (source == 0)
                owner[size_t(pitch)] = std::uint16_t(std::min(65535, int(owner[size_t(pitch)]) + 1));
            else
                owner[size_t(pitch)] = 1;
            if (!shared)
                emit(event);
        }
        else if (kind == 0x80 || (kind == 0x90 && event.bytes[2] == 0))
        {
            if (source == 0 && owner[size_t(pitch)] > 0)
                --owner[size_t(pitch)];
            else
                owner[size_t(pitch)] = 0;
            if (owner[size_t(pitch)] == 0 && !other(source, channel, pitch))
                emit(event);
        }
        else if (kind == 0xb0 && (pitch == 120 || pitch == 123))
        {
            bool shared = false;
            for (int key = 0; key < 128; ++key)
            {
                const bool ownedElsewhere = other(source, channel, key);
                shared = shared || ownedElsewhere;
                if (owner[size_t(key)] != 0 && !ownedElsewhere)
                    emit(
                        ipc::MidiEvent{event.frame, {std::uint8_t(0x80 | channel), std::uint8_t(key), 0}, 3});
            }
            owner = {};
            // Lane panic cannot send channel-wide all-notes-off over another
            // live/score owner. A genuine global reset clears every table first.
            if (!shared)
                emit(event);
        }
        else
            emit(event);
    }
};
} // namespace vibera
