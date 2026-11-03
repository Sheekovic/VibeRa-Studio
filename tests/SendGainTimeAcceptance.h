// SPDX-License-Identifier: AGPL-3.0-or-later
// Original SOURCE-ONLY acceptance. Include after SendGainStateAcceptance.h.
#pragma once
#include <array>
#include <limits>
#include <vector>

namespace vibera::test::send_gain_time
{
using Bank = send_gain_state::Bank;
using Player = send_gain_state::Player;
struct Render
{
    std::array<float, 512> pcm{};
    unsigned callbacks = 0;
    bool callbackClean = true, getterControlClean = true;
};

// Actual native NodePlayer -> SendNode -> ReturnNode -> GainNode, not a stand-in
// smoother. Independent oracle deliberately makes every ramp step binary exact:
// target=expectedFrames/256 and the expected per-frame step is exactly1/256.
inline Render render(double rate, int maximum, bool variable, double seconds,
                     float target, int samples, const juce::File &folder, const juce::String &name)
{
    Render result;
    auto bank = std::make_shared<Bank>(Bank::EdgeIdentity{11, 22, 42}, seconds);
    send_gain_state::Counts counts;
    ready_graph_acceptance::Counts poolCounts;
    {
        Player player([&](Player &p)
        { return std::make_unique<ready_graph_acceptance::CountingPool>(p, poolCounts); });
        player.setNumThreads(0);
        player.enablePooledMemoryAllocations(false);
        player.setNode(send_gain_state::graph(bank, counts, target), rate, maximum);
        result.getterControlClean = counts.gainCalls == 0 && bank->attachmentsControl() == 1 &&
                                    bank->rampSecondsControl() == seconds;
        tracktion::MidiMessageArray midi;
        midi.reserve(64);
        constexpr std::array<int, 4> irregular{7, 13, 17, 31};
        int done = 0;
        while (done < samples)
        {
            const int requested = variable && result.callbacks % 5 != 4
                                    ? irregular[result.callbacks % 5] : maximum;
            const auto frames = std::min({maximum, requested, samples - done});
            choc::buffer::ChannelArrayBuffer<float> pcm(choc::buffer::Size::create(1, frames));
            pcm.clear(); midi.clear();
            const auto allocs = callbackAllocations.load(), deletes = callbackDestructions.load();
            const auto sourceCalls = counts.sourceCalls, gainCalls = counts.gainCalls;
            {
                struct Scope
                {
                    bool previous = callbackAllocationProbe;
                    Scope() { callbackAllocationProbe = true; }
                    ~Scope() { callbackAllocationProbe = previous; }
                } scope;
                player.process({choc::buffer::FrameCount(frames), {done, done + frames},
                                {pcm.getView(), midi}});
                for (int i = 0; i < frames; ++i)
                    result.pcm[std::size_t(done + i)] = pcm.getView().getSample(0, i);
                result.callbackClean = result.callbackClean &&
                    callbackAllocations.load() == allocs && callbackDestructions.load() == deletes &&
                    counts.sourceCalls == sourceCalls + 1 && counts.gainCalls == gainCalls + 1;
            }
            ++result.callbacks;
            done += frames;
        }
        player.clearNode();
    }
    // Keep the complete actual PCM even if an independent musical oracle fails.
    require(folder.getChildFile(name + ".f32").replaceWithData(result.pcm.data(), sizeof(result.pcm)),
            "Retain complete original fixed-time native render");
    return result;
}

inline void tests(const juce::File &folder)
{
    require(folder.createDirectory().wasOk(), "Owned fixed-time native evidence folder");
    const auto reportFile = folder.getChildFile("send-gain-time.txt");
    require(!reportFile.exists(), "Fixed-time native receipt must be fresh");
    juce::String report =
        "Original actual native NodePlayer fixed-time gain tests. No engine/export/provider/"
        "hardware/native-input/listening claim. Existing N64 default-policy tests unchanged.\n"
        "Independent oracles: 48000Hz*5ms ->240samples;44100Hz*5ms ->220samples.\n"
        "Target=N/256 makes exact step1/256. All actual512sample PCM retained before assertions.\n";
    bool passed = true;
    auto check = [&](bool ok, const char *name)
    {
        passed = passed && ok;
        report += juce::String(name) + "=" + juce::String(int(ok)) + "\n";
        require(reportFile.replaceWithText(report), "Retain independent fixed-time results");
    };
    struct Oracle { double rate; int expectedFrames; };
    constexpr std::array<Oracle, 2> rates{{{48000., 240}, {44100., 220}}};
    constexpr std::array<int, 3> maxima{64, 256, 512};
    std::array<std::array<Render, 6>, 2> actual;
    for (std::size_t rateIndex = 0; rateIndex < rates.size(); ++rateIndex)
    {
        const auto oracle = rates[rateIndex];
        for (std::size_t maximumIndex = 0; maximumIndex < maxima.size(); ++maximumIndex)
            for (int shape = 0; shape != 2; ++shape)
            {
                const auto index = maximumIndex * 2 + std::size_t(shape);
                const auto name = "rate" + juce::String(int(oracle.rate)) + "_max" +
                                  juce::String(maxima[maximumIndex]) + (shape ? "_variable" : "_fixed");
                actual[rateIndex][index] = render(oracle.rate, maxima[maximumIndex], shape != 0,
                    .005, float(oracle.expectedFrames) / 256.f, 512, folder, name);
                report += name + "_callbacks=" + juce::String(actual[rateIndex][index].callbacks) + "\n";
            }
    }
    // Assert only after every meaningful actual waveform has been retained.
    for (std::size_t rateIndex = 0; rateIndex < rates.size(); ++rateIndex)
    {
        const auto oracle = rates[rateIndex];
        bool exact = true, clean = true, equivalent = true;
        for (const auto &rendered : actual[rateIndex])
        {
            clean = clean && rendered.callbackClean && rendered.getterControlClean;
            equivalent = equivalent && rendered.pcm == actual[rateIndex][0].pcm;
            for (int frame = 0; frame < 512; ++frame)
                exact = exact && rendered.pcm[std::size_t(frame)] ==
                    float(std::min(frame + 1, oracle.expectedFrames)) / 256.f;
            exact = exact && rendered.pcm[std::size_t(oracle.expectedFrames - 2)] <
                                  float(oracle.expectedFrames) / 256.f &&
                    rendered.pcm[std::size_t(oracle.expectedFrames - 1)] ==
                                  float(oracle.expectedFrames) / 256.f;
        }
        check(exact, rateIndex == 0 ? "48000_5ms_exact240_independent_samples" :
                                     "44100_5ms_exact220_independent_samples");
        check(equivalent, rateIndex == 0 ? "48000_max64_256_512_fixed_variable_bit_identical" :
                                          "44100_max64_256_512_fixed_variable_bit_identical");
        check(clean, "all_actual_callbacks_no_cpp_new_delete_single_writer_no_control_getter");
    }
    bool invalid = true;
    for (const auto seconds : {-0.005, std::numeric_limits<double>::quiet_NaN(),
                               std::numeric_limits<double>::infinity()})
    {
        bool refused = false;
        try { Bank bad({11, 22, 42}, seconds); }
        catch (const std::invalid_argument &) { refused = true; }
        invalid = invalid && refused;
    }
    check(invalid, "negative_nan_and_infinite_immutable_seconds_refused");
    for (const auto seconds : {std::numeric_limits<double>::max(), 100000.0})
    {
        auto bank = std::make_shared<Bank>(Bank::EdgeIdentity{11, 22, 42}, seconds);
        send_gain_state::Counts counts;
        ready_graph_acceptance::Counts poolCounts;
        Player player([&](Player &p)
        { return std::make_unique<ready_graph_acceptance::CountingPool>(p, poolCounts); });
        player.setNumThreads(0);
        bool refused = false;
        try { player.setNode(send_gain_state::graph(bank, counts, .5f), 48000., 64); }
        catch (const std::overflow_error &) { refused = true; }
        check(refused && bank->attachmentsControl() == 0 && bank->epochControl() == 1 &&
                      bank->canRetainControl({11, 22, 42}, 1) == Bank::Admission::unprepared &&
                      counts.gainCalls == 0,
              "product_or_integer_overflow_refused_before_fresh_bank_mutation");
    }
    const auto tiny = render(48000., 64, true, 1.0e-9,
                             .5f, 64, folder, "positive_duration_floor0_minimum1");
    bool minimum = tiny.callbackClean && tiny.getterControlClean;
    for (int frame = 0; frame < 64; ++frame)
        minimum = minimum && tiny.pcm[std::size_t(frame)] == .5f;
    check(minimum, "positive_duration_floor0_is_one_sample_minimum");
    Bank legacy({11, 22, 42});
    check(legacy.rampSecondsControl() == 0.0, "default0_retains_original_prepared_block_policy");
    check(passed, "all_original_native_fixed_time_oracles");
    require(passed, "Actual native fixed-time send gain acceptance");
}
}
