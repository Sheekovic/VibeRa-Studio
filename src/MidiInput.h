// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Sampler.h"
#include "PluginProtocol.h"
#include "MidiRecording.h"
#include "ScoreLog.h"
#include "MidiControlInput.h"
#include "SamplerCuts.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <array>
#include <vector>
namespace vibera
{
struct MidiLane
{
    Id channel = 0;
    std::shared_ptr<SamplerCutSignal> cutSignal = std::make_shared<SamplerCutSignal>();
    MidiClock clock;
    struct Event
    {
        std::array<unsigned char, 3> bytes;
        unsigned epoch;
        unsigned char size;
        std::uint64_t sequence = 0;
    };
    std::array<Event, 1024> queue{};
    std::atomic<unsigned> read{0}, write{0}, epoch{1};
    std::atomic<bool> enabled{false};
    std::atomic<unsigned> overflows{0}, panicEpoch{0};
    void push(const unsigned char *bytes, unsigned char size) noexcept
    {
        if (!enabled.load())
            return;
        auto w = write.load(std::memory_order_relaxed);
        if (w - read.load(std::memory_order_acquire) >= queue.size())
        {
            ++overflows;
            ++panicEpoch;
            ++epoch;
            return;
        }
        const auto sequence = (bytes[0] & 0xf0) == 0x90 && bytes[2] > 0 ? cutSignal->next() : 0;
        queue[w % queue.size()] = {{bytes[0], bytes[1], bytes[2]}, epoch.load(), size, sequence};
        write.store(w + 1, std::memory_order_release);
        cutSignal->publish(sequence); // A cut cannot precede admission of its source MIDI event.
    }
};
// Keys held on a MIDI input, for the chord display: the MIDI callback sets and clears bits
// (atomics only); the interface reads the pitches. All notes off / all sound off clear the set.
struct HeldKeys final
{
    std::array<std::atomic<std::uint64_t>, 2> bits{};
    void observe(const unsigned char *bytes) noexcept
    {
        const auto kind = bytes[0] & 0xf0;
        const auto pitch = bytes[1] & 0x7f;
        const auto bit = std::uint64_t(1) << (pitch & 63);
        if (kind == 0x90 && bytes[2] != 0)
            bits[size_t(pitch >> 6)].fetch_or(bit, std::memory_order_relaxed);
        else if (kind == 0x80 || kind == 0x90)
            bits[size_t(pitch >> 6)].fetch_and(~bit, std::memory_order_relaxed);
        else if (kind == 0xb0 && (bytes[1] == 120 || bytes[1] == 123))
            clear();
    }
    void clear() noexcept
    {
        bits[0].store(0, std::memory_order_relaxed);
        bits[1].store(0, std::memory_order_relaxed);
    }
    std::vector<int> pitches() const
    {
        std::vector<int> held;
        for (int pitch = 0; pitch < 128; ++pitch)
            if (bits[size_t(pitch >> 6)].load(std::memory_order_relaxed) & (std::uint64_t(1) << (pitch & 63)))
                held.push_back(pitch);
        return held;
    }
};
class MidiInputRouter final : public juce::MidiInputCallback
{
    std::unique_ptr<juce::MidiInput> device;
    std::vector<std::shared_ptr<MidiLane>> lanes;
    std::atomic<MidiLane *> target{nullptr};
    std::atomic<unsigned> readers{0}, received{0};
    std::atomic<int> filter{0};
    std::atomic<Id> historyChannel{0};
    std::atomic<unsigned> historyEpoch{1};
    std::atomic<bool> historyEnabled{true};
    juce::String wanted, active, error;
    bool persistent;
    bool quantizeStart = false, quantizeEnd = false;
    juce::uint32 lastPoll = 0;
    void save();
    void quiesceHistory();

  public:
    MidiControlInput controls;
    MidiCapture capture;
    ScoreQueue history;
    std::function<void(Id, double)> historyInterrupted;
    bool recordingQuantizeStart() const
    {
        return quantizeStart;
    }
    bool recordingQuantizeEnd() const
    {
        return quantizeEnd;
    }
    void setRecordingQuantize(bool start, bool end)
    {
        quantizeStart = start;
        quantizeEnd = end;
        save();
    }
    std::function<void(Id, double)> captureInterrupted; // Control thread only.
    void stopCapture();
    bool recordingPosition(MidiClock::Position &position) const;
    // CONTROL-only retained source for bounded automation timestamp ownership.
    std::shared_ptr<MidiLane> retainRecordingLane() const
    {
        const auto *selected = target.load();
        for (const auto &lane : lanes)
            if (lane.get() == selected)
                return lane;
        return {};
    }
    explicit MidiInputRouter(bool enableHardware);
    ~MidiInputRouter();
    std::shared_ptr<MidiLane> createLane();
    void select(MidiLane *);
    void setHistoryTarget(Id);
    void close();
    void panic();
    juce::Result setDevice(const juce::String &);
    void setChannel(int);
    int channel() const
    {
        return filter.load();
    }
    juce::String identifier() const
    {
        return wanted;
    }
    juce::String status() const;
    void poll();
    void route(const juce::MidiMessage &, bool monitor, bool record);
    void handleIncomingMidiMessage(juce::MidiInput *, const juce::MidiMessage &) override;
    // Receive notes from: MIDI channel 1..16 -> the lane of the Channel that receives it (null:
    // the selected Channel's lane gets it). Set on the control thread; read lock-free.
    std::array<std::atomic<MidiLane *>, 17> receivers{};
    void setReceivers(const std::array<MidiLane *, 17> &next);
    // Snap incoming MIDI to scale: bit 12 on, bits 0..11 allowed pitch classes. Note on/off keys
    // move to the nearest allowed key (ties down) before monitoring and recording; the mapping is
    // fixed per key so every note-off matches its note-on. Lock-free; set by the control thread.
    std::atomic<std::uint32_t> scaleSnap{0};
    HeldKeys heldKeys; // Keys down on this input right now (after the channel filter and scale snap).
    static int snapToScale(int key, std::uint32_t mask) noexcept
    {
        if (!(mask & (1u << 12)) || (mask & 0xfffu) == 0)
            return key;
        for (int distance = 0; distance < 12; ++distance)
            for (const int candidate : {key - distance, key + distance})
                if (candidate >= 0 && candidate <= 127 && (mask >> (candidate % 12)) & 1u)
                    return candidate;
        return key;
    }
};
class MidiMonitor
{
    std::shared_ptr<MidiLane> lane;
    unsigned epoch = 0, panicEpoch = 0;
    std::array<std::array<unsigned char, 128>, 16> liveKeys{};
    std::array<bool, 16> livePedal{};
    std::array<std::array<bool, 128>, 16> held{}, deferred{};
    std::array<bool, 16> sustain{};
    static std::int64_t tag(int channel, int pitch)
    {
        return -(channel * 128 + pitch + 1);
    }

  public:
    std::array<ipc::MidiEvent, 256> events{};
    std::array<std::uint64_t, 256> sequences{};
    int count = 0;
    bool panicked = false, globalPanic = false;
    int releaseCount = 0;
    void bind(std::shared_ptr<MidiLane> value)
    {
        lane = std::move(value);
    }
    MidiLane *inputLane() const
    {
        return lane.get();
    }
    // Audio-thread observation only. Publish through an atomic before exposing
    // this state to control/UI threads; never read these mutable keys there.
    int liveKeyCountRT() const noexcept
    {
        int count = 0;
        for (const auto &channel : liveKeys)
            for (auto key : channel)
                count += key != 0;
        return count;
    }
    void reset()
    {
        held = {};
        deferred = {};
        sustain = {};
        epoch = 0;
        panicEpoch = 0;
        liveKeys = {};
        livePedal = {};
    }
    void read() noexcept
    {
        count = 0;
        sequences.fill(0);
        panicked = false;
        globalPanic = false;
        releaseCount = 0;
        if (!lane)
            return;
        auto current = lane->epoch.load();
        bool enabled = lane->enabled.load();
        if (current != epoch)
        {
            panicked = true;
            epoch = current;
            held = {};
            deferred = {};
            sustain = {};
            globalPanic = panicEpoch != lane->panicEpoch.load();
            panicEpoch = lane->panicEpoch.load();
            for (int ch = 0; ch < 16; ++ch)
            {
                if (globalPanic)
                {
                    events[size_t(count++)] = {0, {std::uint8_t(0xb0 + ch), 64, 0}, 3};
                    events[size_t(count++)] = {0, {std::uint8_t(0xb0 + ch), 123, 0}, 3};
                }
                else
                {
                    if (livePedal[ch] && count < int(events.size()))
                        events[size_t(count++)] = {0, {std::uint8_t(0xb0 + ch), 64, 0}, 3};
                    for (int note = 0; note < 128; ++note)
                        if (liveKeys[ch][note])
                        {
                            if (count < int(events.size()))
                                events[size_t(count++)] = {
                                    0, {std::uint8_t(0x80 + ch), std::uint8_t(note), 0}, 3};
                            else
                                globalPanic = true;
                        }
                }
            }
            if (globalPanic && count == int(events.size()))
                for (int ch = 0; ch < 16; ++ch)
                    events[events.size() - 16 + ch] = {0, {std::uint8_t(0xb0 + ch), 123, 0}, 3};
            liveKeys = {};
            livePedal = {};
            releaseCount = count;
        }
        auto r = lane->read.load(std::memory_order_relaxed), w = lane->write.load(std::memory_order_acquire);
        for (; r != w && count < int(events.size()); ++r)
        {
            auto &e = lane->queue[r % lane->queue.size()];
            if (enabled && e.epoch == current)
            {
                sequences[size_t(count)] = e.sequence;
                events[size_t(count++)] = {0, {e.bytes[0], e.bytes[1], e.bytes[2]}, e.size};
                int ch = e.bytes[0] & 15, kind = e.bytes[0] & 0xf0, pitch = e.bytes[1] & 127;
                if (kind == 0x90 && e.bytes[2] > 0)
                    liveKeys[ch][pitch] = 1;
                else if (kind == 0x80 || (kind == 0x90 && e.bytes[2] == 0))
                    liveKeys[ch][pitch] = livePedal[ch] ? 2 : 0;
                else if (kind == 0xb0 && pitch == 64)
                {
                    livePedal[ch] = e.bytes[2] >= 64;
                    if (!livePedal[ch])
                        for (auto &key : liveKeys[ch])
                            if (key == 2)
                                key = 0;
                }
                else if (kind == 0xb0 && (pitch == 120 || pitch == 123))
                {
                    liveKeys[ch] = {};
                    livePedal[ch] = false;
                }
            }
        }
        lane->read.store(r, std::memory_order_release);
    }
    // `keys`/`expressions` (128 each, optional) play the Channel's live tuning: the sounding key
    // and cents for every played key. Release still matches the played key's tag.
    void apply(VoiceBank &voices, const std::uint8_t *keys = nullptr,
               const PreparedNoteExpression *expressions = nullptr) noexcept
    {
        if (panicked)
            voices.visitActive([&](auto &v) { voices.releaseTagged(v.note, v.placement, true); });
        for (int i = 0; i < count; ++i)
        {
            auto &bytes = events[size_t(i)].bytes;
            int kind = bytes[0] & 0xf0, channel = bytes[0] & 15, pitch = bytes[1] & 127;
            if (kind == 0x90 && bytes[2] > 0)
            {
                held[channel][pitch] = true;
                deferred[channel][pitch] = false;
                voices.releaseTagged(tag(channel, pitch), 0, true);
                voices.noteOn(keys ? int(keys[pitch]) : pitch, bytes[2] / 127.f, tag(channel, pitch), 0, 0,
                              sequences[size_t(i)], expressions ? expressions[pitch] : PreparedNoteExpression{}, 0,
                              false, channel);
            }
            else if (kind == 0x80 || (kind == 0x90 && bytes[2] == 0))
            {
                held[channel][pitch] = false;
                if (sustain[channel])
                    deferred[channel][pitch] = true;
                else
                    voices.releaseTagged(tag(channel, pitch), 0);
            }
            else if (kind == 0xb0)
            {
                if (pitch == 64)
                {
                    sustain[channel] = bytes[2] >= 64;
                    if (!sustain[channel])
                        for (int n = 0; n < 128; ++n)
                            if (deferred[channel][n])
                            {
                                deferred[channel][n] = false;
                                if (!held[channel][n])
                                    voices.releaseTagged(tag(channel, n), 0);
                            }
                }
                else if (pitch == 120 || pitch == 123)
                {
                    held[channel] = {};
                    deferred[channel] = {};
                    sustain[channel] = false;
                    for (int n = 0; n < 128; ++n)
                        voices.releaseTagged(tag(channel, n), 0, true);
                }
            }
        }
    }
    void render(VoiceBank &voices, juce::AudioBuffer<float> &output, int start, int frames) noexcept
    {
        apply(voices);
        voices.render(output, start, frames);
    }
};
} // namespace vibera
