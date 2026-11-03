// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <array>
#include <atomic>
#include <memory>
#include "MidiRecording.h"

namespace vibera
{
struct ControlMessage
{
    int port = 0, channel = 0, kind = 0, number = 0, value = 0;
    unsigned generation = 0;
    unsigned automationSession = 0;
    double automationSeconds = 0;
    bool automationClockValid = false;
    unsigned controllerGeneration = 0; // CC-only configuration/learn fence; note ownership is separate.
    // kind: CC, note, program, pitch bend, MIDI Start, Stop, Continue.
};
class MidiControlInput
{
    struct Queue
    {
        std::array<ControlMessage, 1024> events{};
        std::atomic<unsigned> read{0}, write{0}, dropped{0};
        void push(ControlMessage event) noexcept
        {
            const auto w = write.load(std::memory_order_relaxed);
            if (w - read.load(std::memory_order_acquire) == events.size())
            {
                ++dropped;
                return;
            }
            events[w % events.size()] = event;
            write.store(w + 1, std::memory_order_release);
        }
        bool pop(ControlMessage &event) noexcept
        {
            const auto r = read.load(std::memory_order_relaxed);
            if (r == write.load(std::memory_order_acquire))
                return false;
            event = events[r % events.size()];
            read.store(r + 1, std::memory_order_release);
            return true;
        }
    };
    // Prepared once on the control-thread heap, never allocated/resized by a MIDI callback.
    std::unique_ptr<std::array<Queue, 2>> queues = std::make_unique<std::array<Queue, 2>>();
    std::array<std::atomic<unsigned>, 2> generation{1, 1};
    std::atomic<unsigned> ccGeneration{1};
    std::atomic<bool> ccLearning{false};
    // Published by the UI; only the primary MIDI producer reads/updates ownership.
    std::array<std::atomic<uint64_t>, 32> noteMask{}, ccMask{}, programMask{};
    std::array<unsigned char, 2048> noteOwnership{};
    std::array<unsigned char, 16> pedalOwnership{};
    std::atomic<unsigned> ownershipGeneration{1};
    unsigned seenOwnershipGeneration = 1; // Primary producer only.
    std::array<std::atomic<unsigned>, 17> extraMask{};
    std::atomic<const MidiClock *> automationClock{nullptr};
    std::atomic<unsigned> automationSession{0}, automationReaders{0};
    static_assert(std::atomic<const MidiClock *>::is_always_lock_free &&
                  std::atomic<unsigned>::is_always_lock_free);
    struct StampLease
    {
        MidiControlInput *owner = nullptr;
        const MidiClock *clock = nullptr;
        unsigned session = 0;
        explicit StampLease(MidiControlInput &input) noexcept
        {
            session = input.automationSession.load(std::memory_order_acquire);
            if (!input.automationClock.load(std::memory_order_acquire))
                return;
            owner = &input;
            input.automationReaders.fetch_add(1, std::memory_order_acq_rel);
            clock = input.automationClock.load(std::memory_order_acquire);
            if (session != input.automationSession.load(std::memory_order_acquire))
                clock = nullptr;
        }
        ~StampLease()
        {
            if (owner)
                owner->automationReaders.fetch_sub(1, std::memory_order_release);
        }
    };

  public:
    std::atomic<bool> learning{false};
    struct PreparedMappings
    {
        std::array<std::uint64_t, 32> notes{}, controllers{}, programs{};
        std::array<unsigned, 17> extra{};
    };
    // CONTROL prepares every word before publication; each producer still reads one atomic word.
    // No clear-all interval, performance reset, held-note/pedal reset or queue discard.
    void replaceMappings(const PreparedMappings &next) noexcept
    {
        for (size_t i = 0; i < 32; ++i)
        {
            noteMask[i].store(next.notes[i], std::memory_order_relaxed);
            ccMask[i].store(next.controllers[i], std::memory_order_relaxed);
            programMask[i].store(next.programs[i], std::memory_order_relaxed);
        }
        for (size_t i = 0; i < 17; ++i)
            extraMask[i].store(next.extra[i], std::memory_order_relaxed);
        ccGeneration.fetch_add(1, std::memory_order_release);
    }
    unsigned controllerGeneration() const noexcept
    {
        return ccGeneration.load(std::memory_order_acquire);
    }
    static bool projectLearnEligible(const ControlMessage &event) noexcept
    {
        return event.kind == 0 &&
               (event.port == 1 || (event.number != 64 && event.number != 120 && event.number != 123));
    }
    void beginControllerLearn() noexcept
    {
        ccLearning.store(true, std::memory_order_release);
        ccGeneration.fetch_add(1, std::memory_order_release);
    }
    void endControllerLearn() noexcept
    {
        ccLearning.store(false, std::memory_order_release);
        ccGeneration.fetch_add(1, std::memory_order_release);
    }
    // CONTROL owns/pins this clock until endAutomationStamp AND producer-idle.
    // Pointer is never copied into an event or used after the producer lease.
    bool beginAutomationStamp(const MidiClock &clock, unsigned session) noexcept
    {
        if (!session || automationClock.load() || automationReaders.load())
            return false;
        automationSession.store(session, std::memory_order_release);
        automationClock.store(&clock, std::memory_order_release);
        return true;
    }
    void endAutomationStamp() noexcept
    {
        automationClock.store(nullptr, std::memory_order_release);
    }
    // CONTROL only, same bounded producer-quiescence policy as MIDI take Stop.
    // No callback waits; timeout retains the clock owner/take and is not success.
    bool quiesceAutomationStamp() noexcept
    {
        endAutomationStamp();
        const auto began = juce::Time::getMillisecondCounter();
        while (!automationStampIdle() && juce::Time::getMillisecondCounter() - began < 100)
            juce::Thread::yield();
        return automationStampIdle();
    }
    bool automationStampIdle() const noexcept
    {
        return automationReaders.load(std::memory_order_acquire) == 0;
    }
    void resetPerformance() noexcept
    {
        ++ownershipGeneration;
        ++generation[0];
    }
    void clearMappings() noexcept
    {
        for (auto &word : noteMask)
            word.store(0);
        for (auto &word : ccMask)
            word.store(0);
        for (auto &word : programMask)
            word.store(0);
        for (auto &word : extraMask)
            word.store(0);
    }
    void consume(int channel, int kind, int number) noexcept
    {
        if (kind >= 3)
        {
            if (channel >= 0 && channel <= 16 && kind <= 6)
                extraMask[size_t(channel)].fetch_or(1u << unsigned(kind));
            return;
        }
        if (channel < 1 || channel > 16 || number < 0 || number > 127)
            return;
        auto &mask = kind == 1 ? noteMask : kind == 2 ? programMask : ccMask;
        mask[size_t((channel - 1) * 2 + number / 64)].fetch_or(uint64_t(1) << (number % 64));
    }
    // One producer per port. No UI execution, waits, allocation or pointer lifetime transfer.
    // True means a performance-port message belongs to controls instead of the instrument.
    bool observe(const juce::MidiMessage &message, int port) noexcept
    {
        StampLease stamp(*this);
        if (port < 0 || port > 1)
            return false;
        if (port == 0 && seenOwnershipGeneration != ownershipGeneration.load())
        {
            seenOwnershipGeneration = ownershipGeneration.load();
            noteOwnership = {};
            pedalOwnership = {};
        }
        ControlMessage event;
        event.port = port;
        event.generation = generation[size_t(port)].load();
        event.channel = message.getChannel();
        if (message.isController())
        {
            event.kind = 0;
            event.number = message.getControllerNumber();
            event.value = message.getControllerValue();
            event.controllerGeneration = ccGeneration.load(std::memory_order_acquire);
        }
        else if (message.isNoteOnOrOff())
        {
            event.kind = 1;
            event.number = message.getNoteNumber();
            event.value = message.isNoteOn() ? int(message.getVelocity()) : 0;
        }
        else if (message.isProgramChange())
        {
            event.kind = 2;
            event.number = message.getProgramChangeNumber();
            event.value = 127;
        }
        else if (message.isPitchWheel())
        {
            event.kind = 3;
            event.value = message.getPitchWheelValue();
        }
        else if (message.isMidiStart())
        {
            event.kind = 4;
            event.value = 127;
        }
        else if (message.isMidiStop())
        {
            event.kind = 5;
            event.value = 127;
        }
        else if (message.isMidiContinue())
        {
            event.kind = 6;
            event.value = 127;
        }
        else
            return false;
        if (stamp.clock)
        {
            event.automationSession = stamp.session;
            MidiClock::Position position;
            if (stamp.clock->read(position) && position.playing && position.wall > 0)
            {
                event.automationSeconds =
                    position.seconds +
                    juce::jlimit(0., position.block,
                                 juce::Time::getMillisecondCounterHiRes() / 1000. - position.wall);
                event.automationClockValid = true;
            }
        }
        bool mapped = port == 1;
        if (event.kind < 3 && event.channel > 0)
        {
            const auto &mask = event.kind == 1 ? noteMask : event.kind == 2 ? programMask : ccMask;
            mapped = mapped || bool(mask[size_t((event.channel - 1) * 2 + event.number / 64)].load() &
                                    (uint64_t(1) << (event.number % 64)));
        }
        else
            mapped = mapped || bool(extraMask[size_t(event.channel)].load() & (1u << unsigned(event.kind)));
        const bool learn =
            learning.load() || (ccLearning.load(std::memory_order_acquire) && projectLearnEligible(event));
        if (port || mapped || learn || event.kind == 0)
            (*queues)[size_t(port)].push(event);
        bool consumed = mapped || learn;
        if (port == 0 && event.kind == 1)
        {
            auto &owner = noteOwnership[size_t((event.channel - 1) * 128 + event.number)];
            if (event.value)
            {
                if (owner)
                    consumed = owner == 2;
                else
                    owner = consumed ? 2 : 1;
            }
            else
            {
                consumed = owner == 2;
                owner = 0;
            }
        }
        if (port == 0 && event.kind == 0)
        {
            if (event.number == 64)
            {
                auto &owner = pedalOwnership[size_t(event.channel - 1)];
                if (event.value >= 64)
                {
                    if (owner)
                        consumed = owner == 2;
                    else
                        owner = consumed ? 2 : 1;
                }
                else
                {
                    if (owner)
                        consumed = owner == 2;
                    owner = 0;
                }
            }
            if (event.number == 120 || event.number == 123)
                consumed = false;
        }
        return consumed;
    }
    bool pop(int port, ControlMessage &event) noexcept
    {
        return (*queues)[size_t(port)].pop(event);
    }
    void discard() noexcept
    {
        for (int port = 0; port < 2; ++port)
            ++generation[size_t(port)];
    }
    bool current(const ControlMessage &event) const noexcept
    {
        return event.generation == generation[size_t(event.port)].load() &&
               (event.kind != 0 ||
                event.controllerGeneration == ccGeneration.load(std::memory_order_acquire));
    }
    std::array<unsigned, 2> mappingGenerations() const noexcept
    {
        return {generation[0].load(std::memory_order_acquire), generation[1].load(std::memory_order_acquire)};
    }
    unsigned dropped() const noexcept
    {
        return (*queues)[0].dropped.load() + (*queues)[1].dropped.load();
    }
};
} // namespace vibera
