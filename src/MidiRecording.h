// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <set>
#include "TempoMap.h"
#include "Project.h"
#include <atomic>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <limits>
#include <stdexcept>

namespace vibera
{
// One audio-graph writer per lane. Readers never spin or read non-atomic data.
struct MidiClock
{
    struct Position
    {
        double seconds = 0, wall = 0, block = 0;
        bool playing = false;
    };
    std::atomic<unsigned> serial{0};
    std::atomic<double> seconds{0}, wall{0}, block{0};
    std::atomic<bool> playing{false};
    static_assert(std::atomic<double>::is_always_lock_free);
    void publish(double position, double now, double duration, bool running) noexcept
    {
        ++serial;
        seconds.store(position);
        wall.store(now);
        block.store(duration);
        playing.store(running);
        ++serial;
    }
    bool read(Position &out) const noexcept
    {
        for (int attempt = 0; attempt < 2; ++attempt)
        {
            auto before = serial.load();
            if (before & 1)
                continue;
            Position p{seconds.load(), wall.load(), block.load(), playing.load()};
            if (serial.load() == before)
            {
                out = p;
                return true;
            }
        }
        return false;
    }
};
struct MidiCapture
{
    struct Event
    {
        Id channel = 0;
        double seconds = 0;
        unsigned session = 0, epoch = 0;
        std::array<unsigned char, 3> bytes{};
        unsigned source = 0;
    };
    std::array<Event, 4096> queue{};
    std::atomic<unsigned> read{0}, write{0}, session{0};
    std::atomic<bool> active{false}, failed{false};
    // Wait for input (FL Ctrl+I): while armed and stopped, the first note-on is captured at the
    // stopped playhead and raises triggered; the control thread then starts the transport.
    // Events until the transport actually runs are kept at the same start position.
    std::atomic<bool> waitArmed{false}, waitTriggered{false};
    // Wait for input without a take (Only when recording off, or the recording filter ignored):
    // the first note-on raises playWaitTriggered and the control thread presses Play.
    std::atomic<bool> playWaitArmed{false}, playWaitTriggered{false};
    void begin()
    {
        active = false;
        read.store(write.load());
        ++session;
        failed = false;
        active = true;
    }
    void push(Id channel, const MidiClock &clock, unsigned epoch, const unsigned char *bytes) noexcept
    {
        if (playWaitArmed.load(std::memory_order_relaxed) && (bytes[0] & 0xf0) == 0x90 && bytes[2] != 0)
        {
            playWaitArmed.store(false);
            playWaitTriggered.store(true);
        }
        if (!active.load())
            return;
        auto generation = session.load();
        auto kind = bytes[0] & 0xf0;
        // Notes, controllers (sustain becomes note length), channel pressure and pitch bend.
        if (kind != 0x80 && kind != 0x90 && kind != 0xb0 && kind != 0xd0 && kind != 0xe0 && bytes[0] != 0)
            return;
        MidiClock::Position position;
        if (!clock.read(position))
        {
            failed = true;
            return;
        }
        bool waiting = false;
        if (!position.playing)
        {
            if (waitTriggered.load())
                waiting = true;
            else if (waitArmed.load() && kind == 0x90 && bytes[2] != 0)
            {
                waiting = true;
                waitArmed.store(false);
                waitTriggered.store(true);
            }
            else
                return;
        }
        auto w = write.load(std::memory_order_relaxed);
        if (w - read.load(std::memory_order_acquire) >= queue.size())
        {
            failed = true;
            return;
        }
        const double elapsed = waiting ? 0.
                                       : juce::jlimit(0., position.block,
                                                      juce::Time::getMillisecondCounterHiRes() / 1000. - position.wall);
        queue[w % queue.size()] = {
            channel, position.seconds + elapsed, generation, epoch, {bytes[0], bytes[1], bytes[2]}};
        write.store(w + 1, std::memory_order_release);
    }
    bool pop(Event &event)
    {
        auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire))
            return false;
        event = queue[r % queue.size()];
        read.store(r + 1, std::memory_order_release);
        return true;
    }
};
// Message-thread take builder. Never call from an audio/input callback.
// Allocation failure in add leaves musical state unchanged. Queue consumers must
// use acceptCapturedEvent/retryPendingEvent to retain a popped event as well.
class MidiTake
{
  public:
    static constexpr size_t maximumNotes = 16384;
    enum class Failure { none, invalidTime, invalidEvent, noteQuota, captureInput };
    enum class EventResult { applied, ignored, retryPending, busy, invalid };
    enum class SealResult { ready, pendingInput, retryRequired, invalid };
  private:
    struct Held { double beat; float velocity; unsigned epoch; };
    using Key = std::pair<Id, int>;
    using HeldMap = std::map<Key, Held>;
    HeldMap held;
    std::vector<Note> notes;
    // Controllers: sustain-pedal state per input/MIDI channel defers note releases; every
    // other controller is kept as an event (Project ControllerEvent without id).
    std::set<Key> pedals, deferred;
    std::vector<ControllerEvent> controls;
    std::optional<MidiCapture::Event> retainedEvent;
    double origin = 0, initialBeat = 0, beatsPerSecond = 2, loopLength = 0, startSnap = 0, endSnap = 0;
    bool keepDuration = false; // Start quantize keeps each note's played length (end quantize is skipped).
    // Song takes through tempo changes: beats follow the map from the Song beat at `origin`.
    TempoMap songMap;
    double songBeatAtOrigin = 0;
    bool mapped = false;
    double beatAt(double seconds) const noexcept
    {
        return mapped ? songMap.beatAt(songMap.secondsAt(songBeatAtOrigin) + (seconds - origin)) - songBeatAtOrigin +
                            initialBeat
                      : initialBeat + (seconds - origin) * beatsPerSecond;
    }
    unsigned session = 0;
    Failure failure = Failure::none;
    bool sealed = false;
    void close(Key key, double end, double releaseVelocity = 0)
    {
        auto found = held.find(key);
        if (found == held.end() || failure != Failure::none)
            return;
        const auto value = found->second;
        double start = value.beat;
        if (startSnap > 0)
        {
            const double snapped = std::round(start / startSnap) * startSnap;
            if (keepDuration)
                end += snapped - start; // Leave note duration: the end follows the quantized start.
            start = snapped;
        }
        if (endSnap > 0 && !keepDuration) end = std::round(end / endSnap) * endSnap;
        end = std::max(start + 1. / 960, end);
        // Storage was prepared BEFORE any held entry/epoch/event mutation.
        // Count first: quota/invalid duration preserves the entire held owner and
        // existing complete notes rather than appending a truncated split note.
        auto cursor = start;
        size_t count = 0;
        while (cursor < end)
        {
            const double beat = loopLength > 0 ? std::fmod(cursor, loopLength) : cursor;
            const bool finalSegment = loopLength <= 0 || end - cursor <= loopLength - beat;
            const double length = finalSegment ? end - cursor : loopLength - beat;
            if (count >= maximumNotes - notes.size() || !std::isfinite(length) || length <= 0)
            {
                failure = Failure::noteQuota;
                return;
            }
            ++count;
            cursor = finalSegment ? end : cursor + length;
        }
        while (start < end)
        {
            const double beat = loopLength > 0 ? std::fmod(start, loopLength) : start;
            const bool finalSegment = loopLength <= 0 || end - start <= loopLength - beat;
            const double length = finalSegment ? end - start : loopLength - beat;
            notes.push_back({0, key.first, beat, length, key.second & 127, value.velocity, false});
            // Loop boundaries are synthetic closes. Only the segment ending at
            // the real physical note-off retains its release velocity.
            notes.back().releaseVelocity = finalSegment ? releaseVelocity : 0;
            notes.back().colourGroup = (key.second % 2048) / 128;
            start = finalSegment ? end : start + length;
        }
        // The held note is retired ONLY once all its note segments exist.
        held.erase(found);
    }
    EventResult applyEvent(const MidiCapture::Event &event)
    {
        if (failure != Failure::none) return EventResult::invalid;
        if (sealed || event.session != session || event.seconds < origin) return EventResult::ignored;
        const double rawBeat = beatAt(event.seconds);
        if (!std::isfinite(rawBeat) || rawBeat > 1000000)
        { failure = Failure::invalidTime; return EventResult::invalid; }
        const double beat = std::max(0., rawBeat);
        const auto kind = event.bytes[0] & 0xf0;
        if (event.source > unsigned((std::numeric_limits<int>::max() - 2047) / 2048) ||
            (event.bytes[0] != 0 && kind != 0x80 && kind != 0x90 && kind != 0xb0 && kind != 0xd0 && kind != 0xe0))
        { failure = Failure::invalidEvent; return EventResult::invalid; }
        if (kind == 0xb0 || kind == 0xd0 || kind == 0xe0)
        {
            const int midiChannel = event.bytes[0] & 15;
            const Key pedal{event.channel, int(event.source) * 16 + midiChannel};
            if (kind == 0xb0 && (event.bytes[1] & 127) == 64)
            {
                if (event.bytes[2] >= 64)
                    pedals.insert(pedal);
                else if (pedals.erase(pedal))
                    // Pedal up releases every note whose key was let go while it was down.
                    for (auto it = deferred.begin(); it != deferred.end();)
                        if (it->first == event.channel && it->second / 2048 == int(event.source) &&
                            (it->second / 128) % 16 == midiChannel)
                        {
                            const auto key = *it;
                            it = deferred.erase(it);
                            close(key, beat);
                        }
                        else
                            ++it;
                return failure == Failure::none ? EventResult::applied : EventResult::invalid;
            }
            if (controls.size() >= maximumNotes)
            { failure = Failure::noteQuota; return EventResult::invalid; }
            ControllerEvent control;
            control.channel = event.channel;
            control.beat = beat;
            control.midiChannel = midiChannel;
            if (kind == 0xb0)
            {
                control.type = 0;
                control.number = event.bytes[1] & 127;
                control.value = (event.bytes[2] & 127) / 127.;
            }
            else if (kind == 0xd0)
            {
                control.type = 2;
                control.value = (event.bytes[1] & 127) / 127.;
            }
            else
            {
                control.type = 1;
                const int raw = (event.bytes[2] & 127) * 128 + (event.bytes[1] & 127);
                control.value = juce::jlimit(-1., 1., (raw - 8192) / 8191.);
            }
            controls.push_back(control);
            return EventResult::applied;
        }
        // May throw, but no musical/ownership mutation has occurred yet. Normally
        // begin already reserved this; a copied preview can have smaller capacity.
        notes.reserve(maximumNotes);
        if (event.bytes[0] == 0)
        {
            for (auto it = held.begin(); it != held.end() && failure == Failure::none;)
                if (it->first.first == event.channel && unsigned(it->first.second / 2048) == event.source)
                { auto key = it->first; ++it; close(key, beat); }
                else ++it;
            return failure == Failure::none ? EventResult::applied : EventResult::invalid;
        }
        const auto key = std::make_pair(event.channel, int(event.source) * 2048 +
            int(event.bytes[0] & 15) * 128 + int(event.bytes[1] & 127));
        const bool noteOn = kind == 0x90 && event.bytes[2] > 0;
        HeldMap preparedNode;
        if (noteOn)
            preparedNode.emplace(key, Held{beat, event.bytes[2] / 127.f, event.epoch});
        // Everything below is bounded non-allocating map ownership transfer and
        // reserved Note writes. In particular new note-on allocation precedes the
        // old note close and stale-epoch erasure, so retry cannot duplicate a close.
        for (auto it = held.begin(); it != held.end();)
            if (it->first.first == event.channel && unsigned(it->first.second / 2048) == event.source &&
                it->second.epoch != event.epoch)
                it = held.erase(it);
            else ++it;
        const Key pedal{event.channel, int(event.source) * 16 + int(event.bytes[0] & 15)};
        if (!noteOn && pedals.count(pedal) && held.count(key))
            deferred.insert(key); // Released under the pedal: it sounds until the pedal goes up.
        else
        {
            deferred.erase(key);
            close(key, beat, kind == 0x80 ? event.bytes[2] / 127. : 0);
        }
        if (failure != Failure::none) return EventResult::invalid;
        if (noteOn)
            held.insert(preparedNode.extract(preparedNode.begin()));
        return EventResult::applied;
    }
  public:
    // CONTROL, after begin(): Song beat at the take origin and the Song tempo map.
    void useTempoMap(const TempoMap &map, double songBeat)
    {
        songMap = map;
        songBeatAtOrigin = songBeat;
        mapped = !map.constant();
    }
    void begin(unsigned generation, double seconds, double beat, double tempo, double loop,
               double quantizeStart = 0, double quantizeEnd = 0, bool leaveDuration = false)
    {
        keepDuration = leaveDuration;
        if (!std::isfinite(seconds) || !std::isfinite(beat) || !std::isfinite(tempo) || tempo <= 0 ||
            !std::isfinite(loop) || loop < 0 || !std::isfinite(quantizeStart) || quantizeStart < 0 ||
            !std::isfinite(quantizeEnd) || quantizeEnd < 0)
            throw std::invalid_argument("Invalid MIDI capture timing");
        // Allocation happens BEFORE replacing any previous take ownership.
        notes.reserve(maximumNotes);
        controls.reserve(maximumNotes);
        held.clear(); notes.clear(); retainedEvent.reset();
        pedals.clear(); deferred.clear(); controls.clear();
        failure = Failure::none; sealed = false;
        session = generation; origin = seconds; initialBeat = beat; beatsPerSecond = tempo / 60; mapped = false;
        loopLength = loop; startSnap = quantizeStart; endSnap = quantizeEnd;
    }
    void add(const MidiCapture::Event &event) { (void) applyEvent(event); }
    bool hasPendingEvent() const noexcept { return retainedEvent.has_value(); }
    const MidiCapture::Event *pendingEvent() const noexcept { return retainedEvent ? &*retainedEvent : nullptr; }
    EventResult retryPendingEvent() noexcept
    {
        if (!retainedEvent) return EventResult::ignored;
        try
        {
            const auto result = applyEvent(*retainedEvent);
            if (result != EventResult::invalid) retainedEvent.reset();
            return result;
        }
        catch (...) { return EventResult::retryPending; }
    }
    EventResult acceptCapturedEvent(const MidiCapture::Event &event) noexcept
    {
        // Busy does NOT accept ownership of another event. Consumer must retry
        // the existing owner BEFORE popping its next queue event.
        if (retainedEvent) return EventResult::busy;
        retainedEvent = event;
        return retryPendingEvent();
    }
    SealResult seal(double seconds) noexcept
    {
        if (failure != Failure::none) return SealResult::invalid;
        if (retainedEvent) return SealResult::pendingInput;
        if (sealed) return SealResult::ready;
        const double rawEnd = beatAt(seconds);
        if (!std::isfinite(rawEnd)) { failure = Failure::invalidTime; return SealResult::invalid; }
        try { notes.reserve(maximumNotes); }
        catch (...) { return SealResult::retryRequired; }
        const double end = std::max(0., rawEnd);
        while (!held.empty() && failure == Failure::none)
            close(held.begin()->first, end);
        if (failure != Failure::none) return SealResult::invalid;
        sealed = true;
        return SealResult::ready;
    }
    // Readback remains available even for invalid captures; never claim partial
    // notes are a complete take. Release refuses until valid sealed ownership.
    const std::vector<Note> &finishedNotes() const noexcept { return notes; }
    size_t heldCount() const noexcept { return held.size(); }
    Failure failureCode() const noexcept { return failure; }
    void markInputFailure() noexcept
    { if (failure == Failure::none) failure = Failure::captureInput; }
    bool isSealed() const noexcept { return sealed; }
    // Recorded controllers of a sealed take, by beat (ids are allocated on commit).
    std::vector<ControllerEvent> releaseFinishedControllers() noexcept
    {
        if (!sealed || failure != Failure::none || retainedEvent) return {};
        std::stable_sort(controls.begin(), controls.end(),
                         [](const auto &a, const auto &b) { return a.beat < b.beat; });
        return std::move(controls);
    }
    std::vector<Note> releaseFinishedNotes() noexcept
    {
        if (!sealed || failure != Failure::none || retainedEvent) return {};
        return std::move(notes);
    }
    std::vector<Note> finish(double seconds)
    {
        const auto result = seal(seconds);
        if (result == SealResult::retryRequired) throw std::bad_alloc{};
        return result == SealResult::ready ? releaseFinishedNotes() : std::vector<Note>{};
    }
    bool overflowed() const noexcept { return failure != Failure::none; }
    void cancel() noexcept
    {
        held.clear(); notes.clear(); retainedEvent.reset(); failure = Failure::none; sealed = false;
        pedals.clear(); deferred.clear(); controls.clear();
    }
    void interrupt(Id channel, double seconds, unsigned source = 0)
    { add({channel, seconds, session, 0, {0, 0, 0}, source}); }
    size_t size() const noexcept { return notes.size(); }
    std::vector<Note> preview(double seconds) const
    {
        auto copy = *this;
        return copy.finish(seconds);
    }
};
// Record to step sequencer: every recorded note becomes a step on the 1/16 grid (a quarter beat),
// wrapped into `loop` beats when the take loops a Pattern. One step per Channel and slot; the first
// note played on a slot keeps its pitch and velocity.
inline void recordedNotesToSteps(std::vector<Note> &notes, double loop)
{
    std::set<std::pair<Id, long long>> seen;
    std::vector<Note> steps;
    steps.reserve(notes.size());
    for (auto note : notes)
    {
        auto slot = std::llround(note.beat / .25);
        if (loop >= .25)
            slot %= std::max(1ll, std::llround(loop / .25));
        if (slot < 0 || !seen.insert({note.channel, slot}).second)
            continue;
        note.beat = double(slot) * .25;
        note.length = .25;
        note.step = true;
        note.muted = false;
        steps.push_back(note);
    }
    std::stable_sort(steps.begin(), steps.end(), [](const Note &a, const Note &b) { return a.beat < b.beat; });
    notes = std::move(steps);
}
} // namespace vibera
