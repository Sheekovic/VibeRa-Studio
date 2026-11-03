// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "MidiRecording.h"
#include <deque>
#include <tuple>
#include <memory>
namespace vibera
{
// One producer per input source; no clock/device/playback dependency or callback allocation.
struct ScoreQueue
{
    static constexpr unsigned capacity = 4096;
    std::unique_ptr<std::array<MidiCapture::Event, capacity>> events =
        std::make_unique<std::array<MidiCapture::Event, capacity>>();
    std::atomic<unsigned> read{0}, write{0}, generation{1};
    std::atomic<bool> failed{false};
    void push(Id channel, unsigned epoch, const unsigned char *bytes) noexcept
    {
        int kind = bytes[0] & 0xf0;
        if (kind != 0x80 && kind != 0x90)
            return;
        auto w = write.load(std::memory_order_relaxed);
        if (w - read.load(std::memory_order_acquire) >= capacity)
        {
            failed = true;
            return;
        }
        (*events)[w % capacity] = {channel,
                                   juce::Time::getMillisecondCounterHiRes() / 1000.,
                                   generation.load(),
                                   epoch,
                                   {bytes[0], bytes[1], bytes[2]}};
        write.store(w + 1, std::memory_order_release);
    }
    bool pop(MidiCapture::Event &event)
    {
        auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire))
            return false;
        event = (*events)[r % capacity];
        read.store(r + 1, std::memory_order_release);
        return true;
    }
    void clear()
    {
        ++generation;
        read.store(write.load());
        failed = false;
    }
};
// Message-thread rolling score; musical model is changed only by an explicit dump.
class ScoreLog
{
    using Key = std::tuple<Id, unsigned, int>;
    struct Held
    {
        double start;
        float velocity;
        unsigned epoch;
    };
    struct Played
    {
        Id channel;
        double start, end;
        int pitch;
        float velocity;
        double releaseVelocity = 0;
    };
    std::map<Key, Held> held;
    std::deque<Played> notes;
    void close(const Key &key, double time, bool bound = true, double releaseVelocity = 0)
    {
        auto i = held.find(key);
        if (i == held.end())
            return;
        auto n = i->second;
        held.erase(i);
        if (time > n.start)
            notes.push_back(
                {std::get<0>(key), n.start, time, std::get<2>(key) & 127, n.velocity, releaseVelocity});
        if (bound && notes.size() > 65536)
        {
            notes.pop_front();
            truncated = true;
        }
    }

  public:
    bool truncated = false;
    void clear()
    {
        held.clear();
        notes.clear();
        truncated = false;
    }
    void prune(double now)
    {
        std::erase_if(notes, [&](auto &n) { return n.end <= now - 1800.; });
        // Bound held-note origin but keep its velocity/ownership for a later release.
        for (auto &[key, n] : held)
            n.start = std::max(n.start, now - 1800.);
    }
    void add(const MidiCapture::Event &e)
    {
        if (!std::isfinite(e.seconds) || !e.channel)
            return;
        const int keyPitch = (e.bytes[0] & 15) * 128 + (e.bytes[1] & 127);
        Key key{e.channel, e.source, keyPitch};
        for (auto i = held.begin(); i != held.end();)
            if (std::get<0>(i->first) == e.channel && std::get<1>(i->first) == e.source &&
                i->second.epoch != e.epoch)
            {
                auto old = i->first;
                ++i;
                close(old, e.seconds);
            }
            else
                ++i;
        close(key, e.seconds, true, (e.bytes[0] & 0xf0) == 0x80 ? e.bytes[2] / 127. : 0);
        if ((e.bytes[0] & 0xf0) == 0x90 && e.bytes[2] > 0)
        {
            if (held.size() >= 4096)
            {
                truncated = true;
                return;
            }
            held[key] = {e.seconds, e.bytes[2] / 127.f, e.epoch};
        }
    }
    void interrupt(Id channel, unsigned source, double now)
    {
        for (auto i = held.begin(); i != held.end();)
            if (std::get<0>(i->first) == channel && std::get<1>(i->first) == source)
            {
                auto key = i->first;
                ++i;
                close(key, now);
            }
            else
                ++i;
    }
    std::vector<Note> dump(double now, double seconds, double tempo, const Project &project) const
    {
        auto copy = *this;
        while (!copy.held.empty())
            copy.close(copy.held.begin()->first, now, false);
        std::vector<Note> result;
        double origin = now, cut = now - juce::jlimit(1., 1800., seconds);
        for (auto &n : copy.notes)
            if (n.end > cut && n.start < now && project.channel(n.channel))
                origin = std::min(origin, std::max(cut, n.start));
        for (auto &n : copy.notes)
            if (n.end > cut && n.start < now && project.channel(n.channel))
            {
                double start = std::max(cut, n.start), end = std::min(now, n.end);
                if (end > start)
                {
                    result.push_back({0, n.channel, (start - origin) * tempo / 60.,
                                      std::max(1. / 960., (end - start) * tempo / 60.), n.pitch, n.velocity,
                                      false});
                    // Cropping the historical right edge creates a synthetic
                    // off; only the actual retained physical end has a velocity.
                    result.back().releaseVelocity = n.end <= now ? n.releaseVelocity : 0;
                }
            }
        std::stable_sort(result.begin(), result.end(), [](auto &a, auto &b) { return a.beat < b.beat; });
        return result;
    }
};
} // namespace vibera
