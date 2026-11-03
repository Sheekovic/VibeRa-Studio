// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/SamplerLfoPhaseBlock.h"
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

// Include after existing require() and CallbackAllocationProbe.h.
// Original phase/kernel PCM experiment; no product automation consumer is installed.
namespace sampler_lfo_phase_block_acceptance
{
using namespace vibera;
static_assert(!std::is_copy_constructible_v<SamplerLfoPhaseBlock> &&
              !std::is_copy_assignable_v<SamplerLfoPhaseBlock> &&
              !std::is_move_constructible_v<SamplerLfoPhaseBlock> &&
              !std::is_move_assignable_v<SamplerLfoPhaseBlock>);
inline void tests(const juce::File &folder)
{
    const auto same = [](const auto &a, const auto &b)
    {
        for (size_t i = 0; i < a.size(); ++i)
            if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || std::abs(a[i] - b[i]) > 2.e-11)
                return false;
        return true;
    };
    const auto triangle = [](double phase) { return 1. - 4. * std::abs(std::fmod(phase + .25, 1.) - .5); };
    for (double rate : {48000., 44100.})
    {
        SamplerModulationArray settings{};
        settings[1].lfoEnabled = settings[1].lfoGlobal = true;
        settings[1].lfoShape = 1;
        settings[1].lfoAmount = .5;
        PreparedSamplerModulation prepared;
        require(prepared.prepare(rate, 120, settings), "CONTROL prepares actual kernel waveform storage");
        SamplerLfoPhaseBlock block, partitioned;
        require(block.prepare(256) && partitioned.prepare(256), "CONTROL allocates bounded phase storage");
        SamplerModulationPhases seed{.01, .025, .2, .3, .4};
        require(block.seedOnControl(seed) && partitioned.seedOnControl(seed),
                "Seed actual common phase owner");
        require(!block.commit() && !block.cancel(), "No completion/cancellation without an admitted block");

        // Empty spans retain the exact current manual origin+offset*increment expression.
        require(block.begin(prepared, 256), "Admit unchanged manual phase path");
        auto manual = block.view();
        bool manualExact = true;
        for (int frame = 0; frame <= 256; ++frame)
        {
            SamplerModulationPhases phases{};
            manualExact &= manual.read(frame, phases);
            for (size_t target = 0; target < phases.size(); ++target)
                manualExact &=
                    phases[target] ==
                    PreparedSamplerModulation::wrap(seed[target] + frame * prepared.globalIncrements[target]);
        }
        require(manualExact && block.phases() == seed && block.commit() && !block.commit(),
                "Manual reads do not advance; exactly one end commits the original formula");
        SamplerModulationPhases sentinel{.9, .8, .7, .6, .5}, out = sentinel;
        require(!manual.read(0, out) && out == sentinel,
                "After-commit view read refuses without overwriting caller output");
        require(block.seedOnControl(seed), "Restore only under quiescent CONTROL ownership");

        std::array<double, 256> periods;
        std::array<std::uint8_t, 256> flags;
        for (bool ramp : {false, true})
        {
            for (size_t frame = 0; frame < periods.size(); ++frame)
            {
                periods[frame] = ramp ? .02 + .06 * double(frame) / 255. : frame < 128 ? .02 : .08;
                flags[frame] = ramp ? std::uint8_t((frame / 64) % 2) : std::uint8_t(frame >= 128);
            }
            SamplerLfoPhaseAutomationBlock inputs{};
            inputs[1] = {periods, flags};
            std::array<double, 257> expected;
            long double integrated = seed[1];
            expected[0] = seed[1];
            for (size_t frame = 0; frame < periods.size(); ++frame)
            {
                // Independent musical integration; no production phase/wave helper produces this oracle.
                integrated = std::fmod(integrated + (flags[frame] ? 2.L : 1.L) /
                                                        (static_cast<long double>(rate) * periods[frame]),
                                       1.L);
                expected[frame + 1] = double(integrated);
            }
            require(block.seedOnControl(seed) && partitioned.seedOnControl(seed),
                    "Set original step/ramp origin");
            std::array<VoiceSamplerModulation, 4> voices;
            for (auto &voice : voices)
                voice.noteOn(prepared);
            const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
            const auto oldProbe = callbackAllocationProbe;
            callbackAllocationProbe = true;
            bool correct = block.begin(prepared, 256, inputs);
            const auto first = block.view(), duplicate = block.view();
            correct &= !block.begin(prepared, 256, inputs); // Cannot replace an outstanding span.
            double smoothed = 0;
            for (int frame = 0; frame < 256; ++frame)
            {
                SamplerModulationPhases actual{}, repeated{};
                correct &= first.read(frame, actual) && duplicate.read(frame, repeated) && actual == repeated;
                correct &= std::abs(actual[1] - expected[size_t(frame)]) < 2.e-11;
                for (size_t target : {size_t(0), size_t(2), size_t(3), size_t(4)})
                    correct &=
                        actual[target] == PreparedSamplerModulation::wrap(
                                              seed[target] + frame * prepared.globalIncrements[target]);
                const auto wantedWave = triangle(expected[size_t(frame)]);
                // This chosen step/ramp stays on the positive rising triangle with a changing target each
                // frame.
                smoothed += (.5 * wantedWave - smoothed) / std::ceil(.005 * rate);
                for (int bank = 3; bank >= 0; --bank)
                {
                    if (bank == 2 && frame == 128)
                        voices[size_t(bank)].noteOn(prepared); // Retrigger must not reset common phase.
                    const auto observed = voices[size_t(bank)].tick(prepared, actual);
                    correct &= std::abs(observed.lfo[1] - wantedWave) < 2.e-10;
                    if (bank == 0)
                    {
                        const auto actualLeft = .125 * (1. - std::max(0., observed.output[1]));
                        const auto actualRight = -.0625 * (1. + std::min(0., observed.output[1]));
                        correct &=
                            std::abs(actualLeft - .125 * (1. - smoothed)) < 2.e-10 && actualRight == -.0625;
                    }
                }
            }
            SamplerModulationPhases endpoint{};
            correct &= first.read(256, endpoint) && std::abs(endpoint[1] - expected[256]) < 2.e-11;
            correct &= block.phases() == seed && block.commit() && !block.commit();
            out = sentinel;
            correct &= !first.read(0, out) && out == sentinel;
            callbackAllocationProbe = oldProbe;
            require(correct && callbackAllocations.load() == allocations &&
                        callbackDestructions.load() == destructions,
                    "Step/ramp period+tempo integration, duplicate/reverse readers, retrigger and literal "
                    "PCM are heap-free");

            // Real block boundary, and independently split/reversed reads within each block.
            for (int segment = 0; segment < 2; ++segment)
            {
                SamplerLfoPhaseAutomationBlock half{};
                half[1] = {std::span<const double>(periods).subspan(size_t(segment * 128), 128),
                           std::span<const std::uint8_t>(flags).subspan(size_t(segment * 128), 128)};
                require(partitioned.begin(prepared, 128, half),
                        "Admit next actual half-block automation span");
                const auto view = partitioned.view();
                for (int frame = 128; frame >= 0; --frame)
                {
                    SamplerModulationPhases actual{};
                    require(view.read(frame, actual) &&
                                std::abs(actual[1] - expected[size_t(segment * 128 + frame)]) < 2.e-11,
                            "Reversed/split reads preserve real block-boundary phase continuity");
                }
                require(partitioned.commit(), "Commit actual half block exactly once");
            }
            require(same(partitioned.phases(), block.phases()),
                    "Two committed blocks match one complete phase trajectory");
        }
        // No active voice is required to advance the actual Channel clock.
        const auto beforeSilent = block.phases();
        require(block.begin(prepared, 17) && block.commit(),
                "Commit a silent processed block without any voice ticks");
        for (size_t target = 0; target < beforeSilent.size(); ++target)
            require(block.phases()[target] ==
                        PreparedSamplerModulation::wrap(beforeSilent[target] +
                                                        17 * prepared.globalIncrements[target]),
                    "No-active-voice phase follows declared rendered frames");

        // Either automation column can be independently omitted; fallback remains its actual manual field.
        std::array<double, 2> minimumPeriods{.01, 64.};
        std::array<std::uint8_t, 2> alternatingTempo{0, 1};
        SamplerLfoPhaseAutomationBlock independent{};
        independent[2].periods = minimumPeriods;
        independent[3].tempoFlags = alternatingTempo;
        const auto beforeIndependent = block.phases();
        require(block.begin(prepared, 2, independent),
                "Canonical double .01/64 endpoints and independent optional columns admit");
        auto independentView = block.view();
        SamplerModulationPhases independentEnd{};
        require(independentView.read(2, independentEnd) &&
                    std::abs(independentEnd[2] -
                             std::fmod(beforeIndependent[2] + 1. / (rate * .01) + 1. / (rate * 64.), 1.)) <
                        2.e-11 &&
                    std::abs(independentEnd[3] - std::fmod(beforeIndependent[3] + 3. / rate, 1.)) < 2.e-11 &&
                    block.commit(),
                "Period-only retains manual seconds; flag-only retains manual period1 with actual tempo "
                "increment");
        auto wrappingSeed = block.phases();
        wrappingSeed[4] = .999;
        std::array<double, 4> wrappingPeriods{.01, .01, .01, .01};
        SamplerLfoPhaseAutomationBlock wrapping{};
        wrapping[4].periods = wrappingPeriods;
        require(block.seedOnControl(wrappingSeed) && block.begin(prepared, 4, wrapping) && block.commit() &&
                    std::abs(block.phases()[4] - std::fmod(.999 + 4. / (rate * .01), 1.)) < 2.e-11,
                "Automated integration wraps the actual cycle boundary and commits a normalized endpoint");
        const auto retained = block.phases();
        const auto refused = [&](int count, const SamplerLfoPhaseAutomationBlock &inputs)
        {
            require(!block.begin(prepared, count, inputs) && block.phases() == retained && !block.pending(),
                    "Invalid phase input cannot publish or commit partial integration");
        };
        for (int count : {-1, 0, 257})
            refused(count, {});
        require(!block.prepare(0) && !block.prepare(-1) && block.capacity() == 256 &&
                    block.phases() == retained,
                "Invalid CONTROL capacity retains owned storage and committed phase");
        std::array<double, 2> badPeriod{.02, .02};
        std::array<std::uint8_t, 2> badFlag{0, 0};
        SamplerLfoPhaseAutomationBlock invalid{};
        invalid[1] = {badPeriod, badFlag};
        refused(3, invalid);
        for (double value :
             {0., .009, std::nextafter(.01, 0.), double(.01f), 64.1, std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::infinity()})
        {
            badPeriod[1] = value;
            refused(2, invalid); // The invalid value is after a valid first frame.
        }
        badPeriod[1] = .02;
        badFlag[1] = 2;
        refused(2, invalid);
        auto invalidSeed = retained;
        invalidSeed[0] = 1;
        require(!block.seedOnControl(invalidSeed) && block.phases() == retained,
                "Phase seed excludes1 and preserves owner");
        invalidSeed[0] = std::numeric_limits<double>::quiet_NaN();
        require(!block.seedOnControl(invalidSeed) && block.phases() == retained,
                "Nonfinite phase seed refuses atomically");
        auto invalidPrepared = prepared;
        invalidPrepared.sampleRate = std::numeric_limits<double>::quiet_NaN();
        require(!block.begin(invalidPrepared, 2) && block.phases() == retained,
                "Nonfinite declared rate cannot open a phase block");
        invalidPrepared = prepared;
        invalidPrepared.secondsPerSample = 0;
        require(!block.begin(invalidPrepared, 2) && block.phases() == retained,
                "Invalid prepared clock cannot open a phase block");

        require(block.begin(prepared, 16), "Open cancellable actual phase block");
        auto cancelled = block.view();
        require(!block.prepare(64) && !block.seedOnControl(seed),
                "Outstanding readers prevent CONTROL resource changes");
        out = sentinel;
        require(!cancelled.read(-1, out) && !cancelled.read(17, out) && out == sentinel,
                "View frame bounds refuse without changing caller output");
        require(block.cancel() && !block.cancel() && block.phases() == retained,
                "Cancel invalidates publication without committing elapsed phase");
        require(block.begin(prepared, 16), "Start distinct later phase span");
        out = sentinel;
        require(!cancelled.read(0, out) && out == sentinel,
                "Old raw view cannot alias a newer admitted block");
        require(block.cancel() && block.phases() == retained,
                "Second cancellation retains the same committed phase");
    }
    require(
        folder.createDirectory().wasOk() &&
            folder.getChildFile("sampler-lfo-phase-block.txt")
                .replaceWithText(
                    "PASS original48k/44.1k phase-block and VoiceSamplerModulation scalar PCM cases. "
                    "Prototype only; no VoiceBank/automation/application/provider/native/hardware/listening "
                    "qualification.\n"),
        "Retain actually executed phase-block experiment scope");
}
} // namespace sampler_lfo_phase_block_acceptance
