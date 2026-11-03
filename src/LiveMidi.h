// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveTimeline.h"
#include "PluginProtocol.h"
namespace vibera
{
class LiveMidi
{
    struct Gate
    {
        bool active = false;
        Id note = 0, placement = 0;
        int pitch = 0;
        double start = 0;
        std::uint32_t repeatOrdinal = 0;
        std::uint8_t noteOffVelocity = 0;
        std::uint8_t midiChannel = 0; // Actual emitted channel, independent of later source recoloring.
    };
    std::array<Gate, 128> gates{};
    std::uint64_t generation = 0;
    double previousEnd = -1;
    double frameOrigin = 0, frameRate = 0;
    std::uint64_t frameCursor = 0;
    bool wasPlaying = false;
    int count = 0;
    void emit(int frame, int status, int pitch, int velocity = 0) noexcept
    {
        if (count == ipc::events)
        {
            ++overflow;
            return;
        }
        events[size_t(count++)] = {
            frame, {std::uint8_t(status), std::uint8_t(pitch), std::uint8_t(velocity)}, 3};
    }
    bool active(Id note, Id placement, std::uint32_t repeatOrdinal) const noexcept
    {
        for (auto &g : gates)
            if (g.active && g.note == note && g.placement == placement && g.repeatOrdinal == repeatOrdinal)
                return true;
        return false;
    }
    void on(const LiveNote &n, int frame) noexcept
    {
        // Slide is unsupported control metadata for hosted MIDI, never a phantom note.
        if (n.slide || n.colourGroup < 0 || n.colourGroup > 15)
            return;
        Gate *free = nullptr;
        for (auto &g : gates)
            if (!g.active)
            {
                free = &g;
                break;
            }
        if (!free)
        {
            free = &gates[0];
            emit(frame, 0x80 | free->midiChannel, free->pitch);
        }
        emit(frame, 0x90 | n.colourGroup, n.pitch, juce::roundToInt(n.velocity * 127));
        *free = {true,    n.note,          n.placement,       n.pitch,
                 n.start, n.repeatOrdinal, n.noteOffVelocity, std::uint8_t(n.colourGroup)};
    }
    void off(Id note, Id placement, int frame, std::uint32_t repeatOrdinal) noexcept
    {
        for (auto &g : gates)
            if (g.active && g.note == note && g.placement == placement && g.repeatOrdinal == repeatOrdinal)
            {
                emit(frame, 0x80 | g.midiChannel, g.pitch, g.noteOffVelocity);
                g.active = false;
            }
    }

  public:
    std::array<ipc::MidiEvent, ipc::events> events;
    std::uint64_t overflow = 0;
    void reset() noexcept
    {
        gates = {};
        generation = 0;
        previousEnd = -1;
        frameOrigin = frameRate = 0;
        frameCursor = 0;
        wasPlaying = false;
    }
    int process(const LiveDocument &document, Id channelId, double time, int frames, double rate,
                bool playing) noexcept
    {
        count = 0;
        auto previousOverflow = overflow;
        auto *channel = document.find(channelId);
        if (!playing || !channel)
        {
            if (wasPlaying)
                for (auto &g : gates)
                    if (g.active)
                        emit(0, 0x80 | g.midiChannel, g.pitch);
            reset();
            return count;
        }
        bool jump = !wasPlaying || rate != frameRate || std::abs(time - previousEnd) > 2.1 / rate;
        if (jump)
        {
            if (wasPlaying)
                for (auto &g : gates)
                    if (g.active)
                        emit(0, 0x80 | g.midiChannel, g.pitch);
            gates = {};
            frameOrigin = time;
            frameRate = rate;
            frameCursor = 0;
        }
        // Wide nearest-even coordinate anchored to this contiguous playback
        // run. Preserve the same wire frame through callback partitions.
        const auto realizedFrame = [&](double eventTime) noexcept
        { return std::nearbyint((eventTime - frameOrigin) * rate); };
        const double beginFrame = double(frameCursor), endFrame = beginFrame + frames;
        if (jump || generation != document.generation)
        {
            for (auto &g : gates)
                if (g.active)
                {
                    auto *n = channel->find(g.note, g.placement, g.repeatOrdinal);
                    if (!n || n->slide || n->colourGroup != g.midiChannel || n->pitch != g.pitch ||
                        n->start != g.start || realizedFrame(n->end) < beginFrame)
                    {
                        emit(0, 0x80 | g.midiChannel, g.pitch);
                        g.active = false;
                    }
                    else
                        g.noteOffVelocity = n->noteOffVelocity;
                }
            for (auto &n : channel->notes)
                if (n.start < time && n.end > time && realizedFrame(n.start) < beginFrame &&
                    !active(n.note, n.placement, n.repeatOrdinal))
                    on(n, 0);
        }
        // Quantize each event once against this contiguous playback origin.
        // Keep the wide sample coordinate as double, never int32: valid long
        // timelines exceed2^31 samples. Nearest-even matches JUCE's normal
        // rounding mode while preserving ownership across odd/one-frame calls.
        // Conservative search padding only; exact rounded-frame membership
        // below decides which callback owns an event, without an epsilon gate.
        const auto searchStart = frameOrigin + (beginFrame - 1) / rate;
        auto i = std::lower_bound(channel->events.begin(), channel->events.end(), searchStart,
                                  [](auto &e, double t) { return e.time < t; });
        while (i != channel->events.end())
        {
            const auto absoluteFrame = realizedFrame(i->time);
            if (absoluteFrame < beginFrame)
            {
                ++i;
                continue;
            }
            if (absoluteFrame >= endFrame)
                break;
            const int frame = int(absoluteFrame - beginFrame);
            auto groupEnd = i;
            bool hasOn = false, hasOff = false;
            do
            {
                hasOn = hasOn || groupEnd->on;
                hasOff = hasOff || !groupEnd->on;
                ++groupEnd;
            } while (groupEnd != channel->events.end() && realizedFrame(groupEnd->time) == absoluteFrame);
            // Release already-sounding occurrences before same-sample starts.
            // An occurrence whose own start is inside this group must finish
            // after the on pass, including a short gate chased at this origin.
            if (hasOff)
                for (auto event = i; event != groupEnd; ++event)
                    if (!event->on)
                    {
                        const auto &n = channel->notes[event->index];
                        if (n.start < i->time)
                            off(n.note, n.placement, frame, n.repeatOrdinal);
                    }
            if (hasOn)
                for (auto event = i; event != groupEnd; ++event)
                    if (event->on)
                    {
                        const auto &n = channel->notes[event->index];
                        if (!active(n.note, n.placement, n.repeatOrdinal))
                            on(n, frame);
                    }
            if (hasOn && hasOff)
                for (auto event = i; event != groupEnd; ++event)
                    if (!event->on)
                    {
                        const auto &n = channel->notes[event->index];
                        off(n.note, n.placement, frame, n.repeatOrdinal);
                    }
            i = groupEnd;
        }
        // Recorded controllers in this block, then (on a jump) the latest value of each
        // controller before the new position so plugins resume in the recorded state.
        if (!channel->controls.empty())
        {
            const int notesEmitted = count;
            if (jump)
            {
                std::array<int, 16 * 130> latest;
                latest.fill(-1);
                for (int k = 0; k < int(channel->controls.size()) && channel->controls[size_t(k)].time < time; ++k)
                {
                    const auto &bytes = channel->controls[size_t(k)].bytes;
                    const int kind = bytes[0] & 0xf0, slot = kind == 0xe0 ? 128 : kind == 0xd0 ? 129 : bytes[1] & 127;
                    latest[size_t((bytes[0] & 15) * 130 + slot)] = k;
                }
                for (int k : latest)
                    if (k >= 0)
                    {
                        const auto &bytes = channel->controls[size_t(k)].bytes;
                        emit(0, bytes[0], bytes[1], bytes[2]);
                    }
            }
            auto c = std::lower_bound(channel->controls.begin(), channel->controls.end(), searchStart,
                                      [](const auto &control, double t) { return control.time < t; });
            for (; c != channel->controls.end(); ++c)
            {
                const auto absoluteFrame = realizedFrame(c->time);
                if (absoluteFrame < beginFrame)
                    continue;
                if (absoluteFrame >= endFrame)
                    break;
                emit(int(absoluteFrame - beginFrame), c->bytes[0], c->bytes[1], c->bytes[2]);
            }
            // Keep the packet in frame order (insertion sort: fixed storage, no allocation).
            if (count > notesEmitted)
                for (int a = 1; a < count; ++a)
                    for (int b = a; b > 0 && events[size_t(b - 1)].frame > events[size_t(b)].frame; --b)
                        std::swap(events[size_t(b - 1)], events[size_t(b)]);
        }
        if (overflow != previousOverflow)
        {
            // Fixed tail covers all16 emitted channels without growing the packet.
            // MidiNoteOwners routes these as score-lane panics, retaining other
            // held input owners instead of sending a global reset over them.
            static_assert(ipc::events >= 16);
            for (int index = 0; index < 16; ++index)
            {
                const int midiChannel = (index + 1) & 15; // Keep legacy channel1 panic last.
                events[size_t(ipc::events - 16 + index)] = {
                    frames - 1, {std::uint8_t(0xb0 | midiChannel), 123, 0}, 3};
            }
            gates = {};
            generation = 0;
        }
        else
            generation = document.generation;
        wasPlaying = true;
        previousEnd = time + frames / rate;
        frameCursor += std::uint64_t(frames);
        return count;
    }
};
} // namespace vibera
