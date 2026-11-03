// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Project.h"
#include "PatternChannelLoops.h"
#include "NoteTiming.h"
#include "NoteRepeat.h"
#include "NoteRelease.h"
#include <array>
#include <deque>
#include <cmath>
#include <algorithm>

namespace vibera
{
struct MidiScoreOptions
{
    int track = -1;  // All tracks, or zero-based track index.
    int channel = 0; // All channels, or 1..16.
    bool realign = false;
    std::uint32_t channelMask = 65535; // Arbitrary subset, intersected with legacy channel filter.
};
struct MidiScore
{
    std::vector<Note> notes;
    // CC, pitch bend and channel pressure of the selected tracks/channels (IDs unset). In notes
    // and controllers `channel` holds the 1-based source MIDI track until the import applies them.
    std::vector<ControllerEvent> controllers;
    // File tempo map (beat, BPM) and first time signature, from every track; 0/0 without a meter.
    std::vector<std::pair<double, double>> tempos;
    int numerator = 0, denominator = 0;
    std::vector<juce::String> trackNames;
    int tracks = 0, ignoredEvents = 0;
    bool tempoMetadata = false, meterMetadata = false;
};
// Counts are realized unsupported occurrences, not persisted source IDs. Categories
// may overlap; the existing optional int remains the union, never a double count.
struct MidiScoreOmissions
{
    int expressionNotes = 0; // Pan/Fine/Mod X/Y have no MIDI1 score representation.
    int slideNotes = 0;      // Entire Slide occurrence omitted: no phantom note-on/off.
    int portamentoNotes = 0; // Flag omitted; no invented CC or universal glide conversion.
    bool operator==(const MidiScoreOmissions &) const = default;
};
// Control-thread only. Decode to a temporary result: failure never changes the caller's score.
inline juce::Result readMidiScore(const juce::MemoryBlock &bytes, const MidiScoreOptions &options,
                                  MidiScore &output)
{
    auto fail = [](const char *why) { return juce::Result::fail(why); };
    if (bytes.getSize() < 14 || bytes.getSize() > 16 * 1024 * 1024)
        return fail("MIDI file is empty, truncated or exceeds 16 MiB");
    if (options.channelMask == 0 || options.channelMask > 65535)
        return fail("Select at least one valid MIDI channel");
    if (options.track < -1 || options.channel < 0 || options.channel > 16)
        return fail("Invalid MIDI track/channel filter");
    const auto *data = static_cast<const unsigned char *>(bytes.getData());
    auto word = [&](size_t p) { return unsigned(data[p]) * 256 + data[p + 1]; };
    auto dword = [&](size_t p) { return (uint64_t(word(p)) << 16) | word(p + 2); };
    if (std::memcmp(data, "MThd", 4) || dword(4) != 6)
        return fail("Expected a standard MIDI header");
    const auto type = word(8), tracks = word(10), division = word(12);
    if (type > 1 || tracks == 0 || tracks > 256 || (type == 0 && tracks != 1))
        return fail("Only synchronous standard MIDI type 0/1 files with 1..256 tracks are supported");
    if (!division || (division & 0x8000))
        return fail("SMPTE MIDI timing is unsupported; musical PPQ timing is required");
    if (options.track >= int(tracks))
        return fail("Selected MIDI track does not exist");
    // Validate every encoded event before JUCE's permissive decoder sees it.
    size_t pos = 14;
    int eventCount = 0;
    for (unsigned track = 0; track < tracks; ++track)
    {
        if (pos + 8 > bytes.getSize() || std::memcmp(data + pos, "MTrk", 4))
            return fail("Missing MIDI track chunk");
        const auto length = dword(pos + 4);
        pos += 8;
        if (length > bytes.getSize() - pos)
            return fail("Truncated MIDI track");
        const size_t end = pos + size_t(length);
        unsigned running = 0;
        bool ended = false;
        auto variable = [&](unsigned &value)
        {
            value = 0;
            for (int i = 0; i < 4; ++i)
            {
                if (pos >= end)
                    return false;
                const unsigned byte = data[pos++];
                value = (value << 7) | (byte & 127);
                if (!(byte & 128))
                    return true;
            }
            return false;
        };
        while (pos < end)
        {
            unsigned delta = 0;
            if (ended || !variable(delta) || pos >= end || ++eventCount > 1000000)
                return fail("Invalid MIDI event stream or event limit exceeded");
            unsigned status = data[pos];
            if (status & 128)
                ++pos;
            else
                status = running;
            if (status >= 0x80 && status < 0xf0)
            {
                running = status;
                const size_t count = (status >> 4) == 0xc || (status >> 4) == 0xd ? 1 : 2;
                if (count > end - pos)
                    return fail("Truncated MIDI channel message");
                for (size_t i = 0; i < count; ++i)
                    if (data[pos++] & 128)
                        return fail("Invalid MIDI data byte");
            }
            else if (status == 0xff || status == 0xf0 || status == 0xf7)
            {
                running = 0;
                unsigned meta = 0, count = 0;
                if (status == 0xff)
                {
                    if (pos >= end)
                        return fail("Truncated MIDI metadata");
                    meta = data[pos++];
                }
                if (!variable(count) || count > end - pos)
                    return fail("Truncated MIDI event payload");
                if (status == 0xff && meta == 0x2f)
                {
                    if (count != 0)
                        return fail("Invalid MIDI end-of-track");
                    ended = true;
                }
                pos += count;
            }
            else
                return fail("Invalid MIDI status or running status");
        }
        if (!ended)
            return fail("Missing MIDI end-of-track");
    }
    if (pos != bytes.getSize())
        return fail("Unexpected data after MIDI tracks");
    juce::MemoryInputStream input(bytes, false);
    juce::MidiFile file;
    if (!file.readFrom(input, false) || file.getNumTracks() != int(tracks))
        return fail("MIDI decoding failed");
    MidiScore score;
    score.tracks = int(tracks);
    score.trackNames.resize(tracks);
    for (int t = 0; t < file.getNumTracks(); ++t)
    {
        const auto &events = *file.getTrack(t);
        // Tempo and meter belong to the whole file (normally the first track), whatever is selected.
        for (int i = 0; i < events.getNumEvents(); ++i)
        {
            const auto &message = events.getEventPointer(i)->message;
            const double beat = message.getTimeStamp() / division;
            if (message.isTempoMetaEvent() && message.getTempoSecondsPerQuarterNote() > 0 &&
                score.tempos.size() < 4096)
                score.tempos.push_back({beat, 60. / message.getTempoSecondsPerQuarterNote()});
            else if (message.isTimeSignatureMetaEvent() && score.numerator == 0)
                message.getTimeSignatureInfo(score.numerator, score.denominator);
        }
        if (options.track >= 0 && t != options.track)
            continue;
        std::array<std::deque<juce::MidiMessage>, 16 * 128> pending;
        for (int i = 0; i < events.getNumEvents(); ++i)
        {
            const auto &message = events.getEventPointer(i)->message;
            if (message.isTrackNameEvent())
                score.trackNames[size_t(t)] = message.getTextFromTextMetaEvent()
                                                  .substring(0, 128)
                                                  .replaceCharacters("\r\n\t", "   ")
                                                  .trim();
            if (message.isTempoMetaEvent())
                score.tempoMetadata = true;
            else if (message.isTimeSignatureMetaEvent())
                score.meterMetadata = true;
            if (message.getChannel() && ((options.channel && message.getChannel() != options.channel) ||
                                         !(options.channelMask & (1u << (message.getChannel() - 1)))))
                continue;
            if (message.isNoteOn())
                pending[(message.getChannel() - 1) * 128 + message.getNoteNumber()].push_back(message);
            else if (message.isNoteOff())
            {
                auto &queue = pending[(message.getChannel() - 1) * 128 + message.getNoteNumber()];
                if (queue.empty())
                    return fail("Unmatched MIDI note-off in selected data");
                auto on = queue.front();
                queue.pop_front();
                const double beat = on.getTimeStamp() / division;
                const double length = (message.getTimeStamp() - on.getTimeStamp()) / division;
                if (!std::isfinite(beat) || !std::isfinite(length) || beat < 0 || length <= 0 ||
                    beat + length > 1000000)
                    return fail("Invalid or zero-length MIDI note");
                if (score.notes.size() >= 100000)
                    return fail("MIDI note limit exceeded");
                score.notes.push_back(
                    {0, Id(t + 1), beat, length, on.getNoteNumber(), on.getFloatVelocity(), false});
                score.notes.back().colourGroup = on.getChannel() - 1;
                // A velocity-zero NoteOn is an off with neutral release;
                // preserve the actual byte only for an explicit 0x80 event.
                score.notes.back().releaseVelocity =
                    (message.getRawData()[0] & 0xf0) == 0x80 ? message.getVelocity() / 127. : 0;
            }
            else if (message.isController() || message.isPitchWheel() || message.isChannelPressure() ||
                     message.isProgramChange())
            {
                if (score.controllers.size() >= 100000)
                    return fail("MIDI controller event limit exceeded");
                ControllerEvent control;
                control.channel = Id(t + 1);
                control.beat = message.getTimeStamp() / division;
                control.midiChannel = message.getChannel() - 1;
                if (message.isController())
                {
                    control.number = message.getControllerNumber();
                    control.value = message.getControllerValue() / 127.;
                }
                else if (message.isProgramChange())
                {
                    control.type = 3;
                    control.number = message.getProgramChangeNumber();
                }
                else if (message.isPitchWheel())
                {
                    control.type = 1;
                    control.value = juce::jlimit(-1., 1., (message.getPitchWheelValue() - 8192) / 8191.);
                }
                else
                {
                    control.type = 2;
                    control.value = message.getChannelPressureValue() / 127.;
                }
                score.controllers.push_back(control);
            }
            else if (!message.isMetaEvent())
                ++score.ignoredEvents;
        }
        for (const auto &queue : pending)
            if (!queue.empty())
                return fail("Unterminated MIDI note in selected data");
    }
    if (score.notes.empty())
        return fail("No notes match the MIDI import filters");
    std::stable_sort(score.notes.begin(), score.notes.end(),
                     [](const auto &a, const auto &b) { return a.beat < b.beat; });
    std::stable_sort(score.controllers.begin(), score.controllers.end(),
                     [](const auto &a, const auto &b) { return a.beat < b.beat; });
    std::stable_sort(score.tempos.begin(), score.tempos.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    if (options.realign)
    {
        const auto first = score.notes.front().beat;
        for (auto &note : score.notes)
            note.beat -= first;
        std::erase_if(score.controllers, [&](const auto &c) { return c.beat < first; });
        for (auto &control : score.controllers)
            control.beat -= first;
        // The tempo in force at the first note becomes the tempo at beat zero.
        std::vector<std::pair<double, double>> shifted;
        for (const auto &[beat, bpm] : score.tempos)
            if (beat <= first)
                shifted.assign(1, {0., bpm});
            else
                shifted.push_back({beat - first, bpm});
        score.tempos = std::move(shifted);
    }
    output = std::move(score);
    return juce::Result::ok();
}
inline juce::Result writeMidiScore(const Pattern &pattern, Id channel, double tempo, int numerator,
                                   int denominator, juce::MemoryBlock &output,
                                   int *expressionNotesOmitted = nullptr,
                                   MidiScoreOmissions *omissions = nullptr)
{
    if (pattern.numerator != 0)
    {
        numerator = pattern.numerator;
        denominator = pattern.denominator;
    }
    if (!std::isfinite(pattern.length) || pattern.length <= 0 || pattern.length > 1000000 ||
        !std::isfinite(tempo) || tempo < 20 || tempo > 400 || numerator < 1 || numerator > 32 ||
        denominator < 1 || denominator > 32 || (denominator & (denominator - 1)))
        return juce::Result::fail("Invalid MIDI tempo/time signature");
    juce::MidiMessageSequence events;
    events.addEvent(juce::MidiMessage::textMetaEvent(3, pattern.name.substring(0, 128)));
    events.addEvent(juce::MidiMessage::tempoMetaEvent(int(std::lround(60000000. / tempo))));
    events.addEvent(juce::MidiMessage::timeSignatureMetaEvent(numerator, denominator));
    double end = pattern.length * 960.;
    int count = 0, omitted = 0;
    MidiScoreOmissions details;
    bool hasSourcePart = false;
    Project loopContext;
    loopContext.numerator = numerator;
    loopContext.denominator = denominator;
    for (const auto &loop : pattern.channelLoops)
        loopContext.channels.push_back({loop.channel, "MIDI part", "builtin:bass"});
    if (auto valid = validateChannelLoops(loopContext, pattern); valid.failed())
        return valid;
    for (const auto &occurrence : rawPatternOccurrences(loopContext, pattern))
    {
        const auto &note = occurrence.note;
        if (note.channel != channel)
            continue;
        hasSourcePart = true;
        if (note.muted)
            continue;
        if (!std::isfinite(note.beat) || !std::isfinite(note.length) || note.beat < 0 || note.length <= 0 ||
            note.beat + note.length > 1000000 || note.pitch < 0 || note.pitch > 127 ||
            !std::isfinite(note.velocity) || note.velocity < 0 || (!note.slide && note.velocity == 0) ||
            note.velocity > 1 || note.colourGroup < 0 || note.colourGroup > 15 ||
            (note.slide && note.repeatMode != 0) || !std::isfinite(note.pan) || std::abs(note.pan) > 1 ||
            !std::isfinite(note.fineCents) || std::abs(note.fineCents) > 100 ||
            !std::isfinite(note.shiftPercent) || note.shiftPercent < 0 || note.shiftPercent > 100 ||
            !noteRepeatRate(note.repeatMode) || !std::isfinite(note.releaseVelocity) ||
            note.releaseVelocity < 0 || note.releaseVelocity > 1 || !std::isfinite(note.modX) ||
            note.modX < 0 || note.modX > 1 || !std::isfinite(note.modY) || note.modY < 0 || note.modY > 1)
            return juce::Result::fail("Invalid score note (MIDI note-on requires positive velocity)");
        const bool expressionOmitted =
            note.pan != 0 || note.fineCents != 0 || note.modX != .5 || note.modY != .5;
        if (note.slide)
        {
            ++omitted;
            ++details.slideNotes;
            details.expressionNotes += expressionOmitted ? 1 : 0;
            details.portamentoNotes += note.portamento ? 1 : 0;
            continue;
        }
        // Raw source-part export stays unswung/uncropped. Shift and Repeat
        // are encoded once; each sub-trigger uses its original chain phase.
        const double shiftedBeat = note.beat + noteShiftDelay(note.shiftPercent);
        NoteRepeatWindow repeats;
        if (!noteRepeatVisibleWindow(note.repeatMode, shiftedBeat, note.length, shiftedBeat,
                                     shiftedBeat + note.length, repeats) ||
            repeats.count > std::uint32_t(100000 - count))
            return juce::Result::fail("repeat.capacity: MIDI export exceeds100000 note occurrences");
        count += int(repeats.count);
        omitted += expressionOmitted || note.portamento ? int(repeats.count) : 0;
        details.expressionNotes += expressionOmitted ? int(repeats.count) : 0;
        details.portamentoNotes += note.portamento ? int(repeats.count) : 0;
        for (std::uint32_t index = 0; index < repeats.count; ++index)
        {
            const auto ordinal = repeats.first + index;
            const double onset = shiftedBeat + noteRepeatOffset(note.repeatMode, ordinal);
            const double duration = noteRepeatGateDuration(note.repeatMode, note.length, ordinal);
            const double start = std::round(onset * 960.);
            const double finish = std::max(start + 1., std::round((onset + duration) * 960.));
            auto on =
                juce::MidiMessage::noteOn(note.colourGroup + 1, note.pitch,
                                          juce::uint8(std::max(1, int(std::lround(note.velocity * 127)))));
            auto off = juce::MidiMessage::noteOff(note.colourGroup + 1, note.pitch,
                                                  noteReleaseMidiVelocity(note.releaseVelocity));
            on.setTimeStamp(start);
            off.setTimeStamp(finish);
            events.addEvent(on);
            events.addEvent(off);
            end = std::max(end, finish);
        }
    }
    // The Channel's recorded/imported CC, pitch bend and channel pressure, at their beats.
    for (const auto &control : pattern.controllers)
    {
        if (control.channel != channel || !std::isfinite(control.beat) || control.beat < 0 ||
            control.beat > 1000000 || !std::isfinite(control.value) || control.midiChannel < 0 ||
            control.midiChannel > 15)
            continue;
        hasSourcePart = true;
        const int midiChannel = control.midiChannel + 1;
        auto message = control.type == 3
                           ? juce::MidiMessage::programChange(midiChannel, juce::jlimit(0, 127, control.number))
                       : control.type == 1
                           ? juce::MidiMessage::pitchWheel(
                                 midiChannel, juce::jlimit(0, 16383, juce::roundToInt(8192 + control.value * 8191)))
                       : control.type == 2
                           ? juce::MidiMessage::channelPressureChange(
                                 midiChannel, juce::jlimit(0, 127, juce::roundToInt(control.value * 127)))
                           : juce::MidiMessage::controllerEvent(midiChannel, juce::jlimit(0, 127, control.number),
                                                                juce::jlimit(0, 127, juce::roundToInt(control.value * 127)));
        message.setTimeStamp(std::round(control.beat * 960.));
        events.addEvent(message);
        end = std::max(end, message.getTimeStamp());
    }
    events.sort();
    if (!hasSourcePart)
        return juce::Result::fail("Selected Channel has no score notes");
    auto tail = juce::MidiMessage::endOfTrack();
    tail.setTimeStamp(end);
    events.addEvent(tail);
    juce::MidiFile file;
    file.setTicksPerQuarterNote(960);
    file.addTrack(events);
    juce::MemoryOutputStream stream;
    if (!file.writeTo(stream, 0))
        return juce::Result::fail("MIDI encoding failed");
    output = stream.getMemoryBlock();
    if (expressionNotesOmitted)
        *expressionNotesOmitted = omitted;
    if (omissions)
        *omissions = details;
    return juce::Result::ok();
}
} // namespace vibera
