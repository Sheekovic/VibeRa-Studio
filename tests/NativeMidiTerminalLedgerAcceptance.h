// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/PreparedMixerAudioPort.h"
#include <map>
#include <vector>

// Original component oracle. Include after require/callback heap helpers.
// Neither a worker, native input, full app, nor callback timing qualification.
namespace vibera::test::terminal_ledger
{
using Terminal = experimental::NativeMidiTerminal;
using Fault = Terminal::Fault;
struct Reference
{
    using Key = std::pair<unsigned, unsigned>;
    std::map<Key, std::uint64_t> keys;
    std::uint64_t blocks = 0, on = 0, off = 0, panicMessages = 0, panicFlags = 0, other = 0;
    std::vector<juce::MidiMessage> receipt;
    std::array<std::uint64_t, 16> channels() const
    {
        std::array<std::uint64_t, 16> result{};
        for (const auto &[key, count] : keys)
            result[key.first] += count;
        return result;
    }
    std::uint64_t total() const
    {
        std::uint64_t result = 0;
        for (const auto &entry : keys)
            result += entry.second;
        return result;
    }
    // Caller independently specifies whether this block and which prefix were
    // accepted. This model never infers expected faults from implementation.
    void accept(const tracktion::MidiMessageArray &input, int prefix, bool blockAccepted)
    {
        if (!blockAccepted)
            return;
        ++blocks;
        receipt.clear();
        if (input.isAllNotesOff)
        {
            ++panicFlags;
            keys.clear();
        }
        for (int n = 0; n < prefix; ++n)
        {
            const auto &message = input[n];
            const auto *bytes = message.getRawData();
            const unsigned kind = bytes[0] & 0xf0, channel = bytes[0] & 15;
            if (kind == 0x90 && bytes[2])
            {
                ++keys[{channel, bytes[1]}];
                ++on;
            }
            else if (kind == 0x80 || (kind == 0x90 && !bytes[2]))
            {
                auto entry = keys.find({channel, bytes[1]});
                if (entry != keys.end() && entry->second)
                    --entry->second;
                ++off;
            }
            else if (kind == 0xb0 && (bytes[1] == 120 || bytes[1] == 123))
            {
                for (auto it = keys.begin(); it != keys.end();)
                    if (it->first.first == channel)
                        it = keys.erase(it);
                    else
                        ++it;
                ++panicMessages;
            }
            else
                ++other;
            receipt.emplace_back(message.getRawData(), message.getRawDataSize(), message.getTimeStamp());
        }
    }
};
struct Trial
{
    std::unique_ptr<Terminal> terminal = std::make_unique<Terminal>();
    Reference reference;
    juce::File output;
    juce::String csv = "phase,fault,blocks,on,off,panicMessages,panicFlags,other,active,total,referenceTotal,"
                       "accepted,heapNew,heapDelete\n";
    explicit Trial(juce::File folder) : output(std::move(folder))
    {
        require(!output.exists() && output.createDirectory().wasOk(),
                "Fresh independent terminal ledger case");
    }
    void compare(Fault expected)
    {
        const auto actual = terminal->observe();
        const auto channels = reference.channels();
        std::uint64_t recount = 0;
        for (unsigned c = 0; c < 16; ++c)
        {
            std::uint64_t channelTotal = 0;
            for (unsigned key = 0; key < 128; ++key)
            {
                const auto found = reference.keys.find({c, key});
                const auto wanted = found == reference.keys.end() ? 0 : found->second;
                require(terminal->held[c][key] == wanted,
                        "Independent map vs every actual retained key count");
                channelTotal += terminal->held[c][key];
            }
            require(channelTotal == channels[c] && channelTotal == terminal->channelHeld[c],
                    "Independent channel recount equals incremental channel total");
            recount += channelTotal;
        }
        require(recount == reference.total() && recount == terminal->heldTotal && recount == actual.active,
                "Independent full recount, retained ledger, incremental total and atomic active agree");
        require(actual.fault == expected && actual.blocks == reference.blocks && actual.on == reference.on &&
                    actual.off == reference.off && actual.panicMessages == reference.panicMessages &&
                    actual.panicFlags == reference.panicFlags && actual.other == reference.other &&
                    terminal->lastCount == reference.receipt.size(),
                "Exact counters/sticky status and accepted-prefix receipt");
        for (std::size_t n = 0; n < reference.receipt.size(); ++n)
        {
            const auto &expectedMessage = reference.receipt[n];
            const auto &event = terminal->lastEvents[n];
            require(event.size == expectedMessage.getRawDataSize() &&
                        event.time == expectedMessage.getTimeStamp() && event.source == 19,
                    "Actual accepted event identity/time/source retained");
            for (int b = 0; b < event.size; ++b)
                require(event.bytes[std::size_t(b)] == expectedMessage.getRawData()[b],
                        "Accepted short event raw bytes unchanged");
        }
    }
    void run(const char *phase, tracktion::MidiMessageArray &input, int prefix, bool blockAccepted,
             Fault expected)
    {
        std::vector<juce::MidiMessage> original;
        for (const auto &event : input)
            original.emplace_back(event.getRawData(), event.getRawDataSize(), event.getTimeStamp());
        const auto panic = input.isAllNotesOff;
        const auto allocations = callbackAllocations.load(), deletions = callbackDestructions.load();
        {
            struct Probe
            {
                bool previous = callbackAllocationProbe;
                Probe()
                {
                    callbackAllocationProbe = true;
                }
                ~Probe()
                {
                    callbackAllocationProbe = previous;
                }
            } probe;
            terminal->consume(input);
        }
        reference.accept(input, prefix, blockAccepted);
        const auto actual = terminal->observe();
        csv += juce::String(phase) + "," + juce::String(int(actual.fault)) + "," +
               juce::String(juce::uint64(actual.blocks)) + "," + juce::String(juce::uint64(actual.on)) + "," +
               juce::String(juce::uint64(actual.off)) + "," +
               juce::String(juce::uint64(actual.panicMessages)) + "," +
               juce::String(juce::uint64(actual.panicFlags)) + "," +
               juce::String(juce::uint64(actual.other)) + "," + juce::String(actual.active) + "," +
               juce::String(terminal->heldTotal) + "," + juce::String(juce::uint64(reference.total())) + "," +
               juce::String(prefix) + "," + juce::String(callbackAllocations.load() - allocations) + "," +
               juce::String(callbackDestructions.load() - deletions) + "\n";
        juce::FileOutputStream file(output.getChildFile("actual.csv"));
        require(file.openedOk() && file.setPosition(0) && file.truncate().wasOk() &&
                    file.writeText(csv, false, false, "\n"),
                "Retain actual terminal ledger before assertions");
        file.flush();
        require(file.getStatus().wasOk(), "Terminal ledger evidence flush/status");
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == deletions,
                "Actual terminal consume has no callback heap/destruction");
        require(input.size() == int(original.size()) && input.isAllNotesOff == panic,
                "Terminal does not consume/mutate input container or panic flag");
        for (int n = 0; n < input.size(); ++n)
        {
            require(input[n].getRawDataSize() == original[std::size_t(n)].getRawDataSize() &&
                        input[n].getTimeStamp() == original[std::size_t(n)].getTimeStamp() &&
                        input[n].mpeSourceID == 19,
                    "Input raw message/time/source identity preserved on success/refusal");
            for (int b = 0; b < input[n].getRawDataSize(); ++b)
                require(input[n].getRawData()[b] == original[std::size_t(n)].getRawData()[b],
                        "Input payload unchanged");
        }
        compare(expected);
    }
    void seed(std::initializer_list<std::pair<Reference::Key, unsigned>> entries)
    {
        // Explicit fully consistent fixture state. Never make held[] overflow
        // tests pass by leaving new aggregate/channel totals at zero.
        for (const auto &[key, count] : entries)
            reference.keys[key] = count;
        const auto channels = reference.channels();
        const auto total = reference.total();
        require(total <= std::numeric_limits<unsigned>::max(),
                "Genuine representable ownership boundary fixture");
        terminal->held = {};
        for (const auto &[key, count] : reference.keys)
            terminal->held[key.first][key.second] = unsigned(count);
        for (unsigned c = 0; c < 16; ++c)
            terminal->channelHeld[c] = unsigned(channels[c]);
        terminal->heldTotal = unsigned(total);
        terminal->active = unsigned(total);
        reference.on = total;
        terminal->on = total;
        reference.blocks = (total + 511) / 512 + 1;
        terminal->blocks = reference.blocks;
        compare(Fault::none);
    }
};
inline tracktion::MidiMessageArray messages(std::initializer_list<juce::MidiMessage> items)
{
    tracktion::MidiMessageArray result;
    result.reserve(int(items.size()));
    int n = 0;
    for (const auto &message : items)
        result.addMidiMessage(message, double(n++) / 1000., 19);
    return result;
}
inline void ordinary(const juce::File &folder)
{
    Trial trial(folder);
    auto overlap =
        messages({juce::MidiMessage::noteOn(1, 60, juce::uint8(90)),
                  juce::MidiMessage::noteOn(1, 60, juce::uint8(100)),
                  juce::MidiMessage::noteOn(2, 60, juce::uint8(80)), juce::MidiMessage::noteOff(1, 60),
                  juce::MidiMessage::noteOff(3, 61), juce::MidiMessage::noteOn(4, 62, juce::uint8(0))});
    trial.run("overlap_channels_unmatched_and_velocityzero_off", overlap, 6, true, Fault::none);
    // Actual one/two-byte constructors intentionally do not initialize
    // unused packed payload. Classification must not read such bytes.
    auto shortOther = messages({juce::MidiMessage(0xf8), juce::MidiMessage(0xc0, 17),
                                juce::MidiMessage(0xd0, 37), juce::MidiMessage(0xf1, 66)});
    require(shortOther[0].getRawDataSize() == 1 && shortOther[1].getRawDataSize() == 2 &&
                shortOther[2].getRawDataSize() == 2 && shortOther[3].getRawDataSize() == 2,
            "Genuine realtime/program/pressure/quarter-frame short lengths");
    trial.run("one_and_two_byte_other_messages", shortOther, 4, true, Fault::none);
    tracktion::MidiMessageArray every;
    every.reserve(32);
    for (int c = 1; c <= 16; ++c)
    {
        every.addMidiMessage(juce::MidiMessage::noteOn(c, 63, juce::uint8(100)), 0., 19);
        every.addMidiMessage(juce::MidiMessage::noteOn(c, 63, juce::uint8(100)), 0., 19);
    }
    trial.run("all16_channels_overlapping", every, 32, true, Fault::none);
    tracktion::MidiMessageArray empty;
    trial.run("empty_retains_all_owned_notes", empty, 0, true, Fault::none);
    auto panic =
        messages({juce::MidiMessage::allNotesOff(2), juce::MidiMessage::allSoundOff(16),
                  juce::MidiMessage::noteOff(16, 63), juce::MidiMessage::noteOn(16, 64, juce::uint8(100)),
                  juce::MidiMessage::controllerEvent(1, 1, 64)});
    trial.run("perchannel_panic_and_reentry", panic, 5, true, Fault::none);
    auto flag =
        messages({juce::MidiMessage::noteOn(8, 70, juce::uint8(100)), juce::MidiMessage::noteOff(8, 70),
                  juce::MidiMessage::noteOn(9, 71, juce::uint8(100))});
    flag.isAllNotesOff = true;
    trial.run("global_flag_before_new_events", flag, 3, true, Fault::none);
    tracktion::MidiMessageArray global;
    global.isAllNotesOff = true;
    trial.run("global_flag_clears_all", global, 0, true, Fault::none);
    auto full = messages({});
    full.reserve(512);
    for (int n = 0; n < 512; ++n)
        full.addMidiMessage(juce::MidiMessage::noteOn(n % 16 + 1, n % 128, juce::uint8(100)), 0., 19);
    trial.run("exact512_short_messages", full, 512, true, Fault::none);
}
inline void invalid(const juce::File &folder, int mode)
{
    Trial trial(folder);
    auto seed = messages({juce::MidiMessage::noteOn(1, 60, juce::uint8(100))});
    trial.run("valid_seed", seed, 1, true, Fault::none);
    tracktion::MidiMessageArray input;
    input.reserve(mode == 2 ? 513 : 2);
    input.addMidiMessage(juce::MidiMessage::noteOn(2, 61, juce::uint8(100)), 0., 19);
    if (mode == 2)
        for (int n = 0; n < 512; ++n)
            input.addMidiMessage(juce::MidiMessage::noteOff(2, 61), 0., 19);
    else if (mode == 3)
    {
        const std::array<juce::uint8, 4> payload{1, 2, 3, 4};
        input.addMidiMessage(juce::MidiMessage::createSysExMessage(payload.data(), int(payload.size())), 0.,
                             19);
    }
    else
        input.addMidiMessage(juce::MidiMessage(mode == 0 ? 0x90 : 0x80, 255, 100), 0., 19);
    trial.run("invalid_whole_input_refused", input, 0, false, mode == 2 ? Fault::quota : Fault::unsupported);
    trial.run("sticky_refusal_does_not_process_later_input", seed, 0, false,
              mode == 2 ? Fault::quota : Fault::unsupported);
}
inline void ownership(const juce::File &folder, bool aggregate)
{
    Trial trial(folder);
    constexpr auto maximum = std::numeric_limits<unsigned>::max();
    if (aggregate)
        trial.seed({{{0, 60}, maximum - 1}, {{1, 61}, 1}});
    else
        trial.seed({{{0, 60}, maximum}});
    auto input =
        messages({juce::MidiMessage::noteOn(aggregate ? 3 : 1, aggregate ? 62 : 60, juce::uint8(100))});
    trial.run(aggregate ? "aggregate_capacity_new_channel_refused" : "full_key_channel_capacity_refused",
              input, 0, true, Fault::ownershipOverflow);
    auto off = messages({juce::MidiMessage::noteOff(1, 60)});
    trial.run("sticky_ownership_fault", off, 0, false, Fault::ownershipOverflow);
}
inline void releaseAtMaximum(const juce::File &folder)
{
    Trial trial(folder);
    constexpr auto maximum = std::numeric_limits<unsigned>::max();
    trial.seed({{{0, 60}, maximum - 1}, {{1, 61}, 1}});
    auto input =
        messages({juce::MidiMessage::noteOff(1, 60), juce::MidiMessage::noteOn(3, 62, juce::uint8(100)),
                  juce::MidiMessage::allSoundOff(1)});
    trial.run("full_total_release_then_admit_then_channel_clear", input, 3, true, Fault::none);
}
inline void counter(const juce::File &folder, int mode)
{
    Trial trial(folder);
    trial.seed({{{0, 60}, 1}});
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    // These states are consistent with a long history of matched/unmatched
    // messages; block count can contain arbitrary earlier empty callbacks.
    trial.reference.blocks = std::uint64_t(1) << 57;
    trial.terminal->blocks = trial.reference.blocks;
    auto input = messages({juce::MidiMessage::noteOn(1, 61, juce::uint8(100))});
    bool accepted = true;
    if (mode == 0)
    {
        trial.reference.blocks = maximum;
        trial.terminal->blocks = maximum;
        accepted = false;
    }
    if (mode == 1)
    {
        trial.reference.on = maximum;
        trial.terminal->on = maximum;
        trial.reference.off = maximum - 1;
        trial.terminal->off = maximum - 1;
    }
    if (mode == 2)
    {
        trial.reference.off = maximum;
        trial.terminal->off = maximum;
        input = messages({juce::MidiMessage::noteOff(1, 60)});
    }
    if (mode == 3)
    {
        trial.reference.panicMessages = maximum;
        trial.terminal->panicMessages = maximum;
        input = messages({juce::MidiMessage::allNotesOff(1)});
    }
    if (mode == 4)
    {
        trial.reference.blocks = maximum;
        trial.terminal->blocks = maximum;
        trial.reference.panicFlags = maximum;
        trial.terminal->panicFlags = maximum;
        input.isAllNotesOff = true;
        accepted = false;
    }
    if (mode == 5)
    {
        trial.reference.other = maximum;
        trial.terminal->other = maximum;
        input = messages({juce::MidiMessage::controllerEvent(1, 1, 32)});
    }
    trial.compare(Fault::none);
    trial.run("counter_exhaustion_before_event_or_block_mutation", input, 0, accepted,
              Fault::counterExhausted);
    trial.run("counter_fault_remains_sticky", input, 0, false, Fault::counterExhausted);
}
inline void acceptedPrefix(const juce::File &folder)
{
    Trial trial(folder);
    trial.seed({{{0, 60}, 1}});
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    trial.reference.on = maximum - 1;
    trial.terminal->on = maximum - 1;
    trial.reference.off = maximum - 2;
    trial.terminal->off = maximum - 2;
    trial.reference.blocks = std::uint64_t(1) << 57;
    trial.terminal->blocks = trial.reference.blocks;
    auto input = messages({juce::MidiMessage::noteOn(2, 61, juce::uint8(100)),
                           juce::MidiMessage::noteOn(3, 62, juce::uint8(100))});
    trial.run("first_admitted_second_counter_exhausted_exact_prefix", input, 1, true,
              Fault::counterExhausted);
}
inline void tests(const juce::File &folder)
{
    require(!folder.exists() && folder.createDirectory().wasOk(), "Fresh terminal running-total acceptance");
    std::exception_ptr first;
    juce::String summary;
    const auto run = [&](const juce::String &name, auto &&body)
    {
        try
        {
            body();
            summary += name + "=completed\n";
        }
        catch (const std::exception &error)
        {
            if (!first)
                first = std::current_exception();
            summary += name + "=failed:" + juce::String(error.what()) + "\n";
        }
    };
    run("ordinary", [&] { ordinary(folder.getChildFile("ordinary")); });
    for (int n = 0; n < 4; ++n)
        run("invalid" + juce::String(n),
            [&] { invalid(folder.getChildFile("invalid" + juce::String(n)), n); });
    for (bool aggregate : {false, true})
        run(aggregate ? "aggregate_overflow" : "key_overflow",
            [&]
            {
                ownership(folder.getChildFile(aggregate ? "aggregate_overflow" : "key_overflow"), aggregate);
            });
    run("release_maximum", [&] { releaseAtMaximum(folder.getChildFile("release_maximum")); });
    for (int n = 0; n < 6; ++n)
        run("counter" + juce::String(n),
            [&] { counter(folder.getChildFile("counter" + juce::String(n)), n); });
    run("accepted_prefix", [&] { acceptedPrefix(folder.getChildFile("accepted_prefix")); });
    require(folder.getChildFile("cases.txt").replaceWithText(summary),
            "All independent terminal outcomes retained");
    if (first)
        std::rethrow_exception(first);
}
} // namespace vibera::test::terminal_ledger
