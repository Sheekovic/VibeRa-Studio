// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/SamplerModulation.h"
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <iterator>
// Include after require() and the existing callback allocation/destruction probes.
// Original pure-kernel PCM/control tests, not VoiceBank/app/provider/FL qualification.
namespace sampler_envelope_audio_acceptance
{
using namespace vibera;
inline void tests(const juce::File &folder)
{
    const auto closeEnough = [](double a, double b) { return std::abs(a - b) < 2.e-9; };
    for (double tension : {-1., 0., 1.})
    {
        double previous = 0;
        for (int i = 0; i <= 256; ++i)
        {
            const auto value = samplerEnvelopeCurve(i / 256., tension);
            require(value >= previous && value >= 0 && value <= 1,
                    "Original envelope curve monotonic/bounded");
            previous = value;
        }
        require(samplerEnvelopeCurve(0, tension) == 0 && samplerEnvelopeCurve(1, tension) == 1,
                "Original curve exact endpoints");
    }
    require(samplerEnvelopeCurve(.5, -1) == .25 && samplerEnvelopeCurve(.5, 0) == .5 &&
                samplerEnvelopeCurve(.5, 1) == .75,
            "Independent polynomial midpoint/tension magnitudes");
    SamplerModulation shaped;
    shaped.attack = .5;
    shaped.hold = 0;
    shaped.decay = .5;
    shaped.sustain = .25;
    shaped.attackTension = 1;
    shaped.decayTension = -1;
    shaped.releaseTension = 1;
    shaped.release = .5;
    require(samplerEnvelopeLevel(shaped, .25) == .75 && samplerEnvelopeLevel(shaped, .75) == .8125 &&
                samplerReleaseLevel(shaped, .25, .8125) == .203125,
            "Independent attack/decay/release tension magnitudes preserve their own stages");
    SamplerModulationArray settings{};
    PreparedSamplerModulation prepared;
    require(prepared.prepare(48000, 120, settings), "Prepare actual shared modulation table");
    for (int shape : {0, 1})
        require(prepared.wave(shape, 0) == 0 && prepared.wave(shape, .25) == 1 &&
                    prepared.wave(shape, .5) == 0 && prepared.wave(shape, .75) == -1,
                "Sine/triangle literal rising-zero cardinal phases");
    require(prepared.wave(2, 0) == 1 && prepared.wave(2, .499) == 1 && prepared.wave(2, .5) == -1,
            "Square's explicit rising-positive half-cycle contract");
    const auto oldRate = prepared.sampleRate;
    require(!prepared.prepare(0, 120, settings) && prepared.sampleRate == oldRate &&
                !prepared.configure(0, settings),
            "Invalid clock preparation preserves prior configuration");
    auto invalid = settings;
    invalid[1].enabled = true;
    require(!prepared.configure(120, invalid), "Pan page has no envelope runtime admission");
    invalid = settings;
    invalid[2].lfoPeriod = 0;
    require(!prepared.configure(120, invalid), "Invalid period refuses before publication");

    // Exact neutral default across three real rates, with no measured C++ callback heap work.
    for (double rate : {44100., 48000., 96000.})
    {
        require(prepared.prepare(rate, 120, settings), "Prepare neutral sampler modulation rate");
        VoiceSamplerModulation voice;
        voice.noteOn(prepared);
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        bool neutral = true;
        const bool previous = callbackAllocationProbe;
        callbackAllocationProbe = true;
        for (int i = 0; i < 1024; ++i)
        {
            const auto frame = voice.tick(prepared, {});
            neutral &= frame.volumeMultiplier == 1 && !frame.volumeEnvelopeActive &&
                       !frame.volumeReleaseFinished && frame.envelope[1] == 0;
            for (const auto output : frame.output)
                neutral &= output == 0;
        }
        callbackAllocationProbe = previous;
        require(neutral && callbackAllocations.load() == allocations &&
                    callbackDestructions.load() == destructions,
                "Legacy-neutral modulation is exact and callback heap-free");
        require(closeEnough(voice.ageSeconds(), 1024. / rate) && closeEnough(voice.ageBeats(), 2048. / rate),
                "Voice keeps simultaneous seconds/integrated-quarter-beat age at each rate");
    }
    // Fast neutral advancement preserves the same later live-enable trajectory.
    require(prepared.prepare(48000, 120, settings), "Prepare cheap inactive clock reference");
    VoiceSamplerModulation fullNeutral, cheapNeutral;
    fullNeutral.noteOn(prepared);
    cheapNeutral.noteOn(prepared);
    for (int i = 0; i < 9600; ++i)
        fullNeutral.tick(prepared, {});
    for (int i = 0; i < 100; ++i)
        cheapNeutral.advanceInactive(prepared, 96);
    require(closeEnough(fullNeutral.ageSeconds(), cheapNeutral.ageSeconds()) &&
                closeEnough(fullNeutral.ageBeats(), cheapNeutral.ageBeats()),
            "Cheap never-active voice retains seconds/beats across bounded spans");
    for (size_t i = 0; i < 5; ++i)
        require(closeEnough(fullNeutral.localPhases()[i], cheapNeutral.localPhases()[i]),
                "Cheap never-active voice retains every future local phase");
    auto lateSettings = settings;
    lateSettings[2].enabled = true;
    lateSettings[2].amount = 1;
    lateSettings[2].attack = 1;
    lateSettings[2].lfoEnabled = true;
    lateSettings[2].lfoAmount = 1;
    require(prepared.configure(120, lateSettings), "Enable actual late envelope/LFO sources");
    const auto fullEnabled = fullNeutral.tick(prepared, {});
    const auto cheapEnabled = cheapNeutral.tick(prepared, {});
    require(closeEnough(fullEnabled.envelope[2], .2) && closeEnough(cheapEnabled.envelope[2], .2) &&
                closeEnough(fullEnabled.lfo[2], cheapEnabled.lfo[2]) &&
                closeEnough(fullEnabled.output[2], cheapEnabled.output[2]) &&
                fullEnabled.volumeMultiplier == 1 && cheapEnabled.volumeMultiplier == 1,
            "Late enable preserves age/phase/output rather than silently retriggering an inactive note");
    settings = {};
    settings[0].enabled = true;
    settings[0].amount = 1;
    settings[0].delay = .125;
    settings[0].attack = .25;
    settings[0].hold = .125;
    settings[0].decay = .25;
    settings[0].sustain = .25;
    settings[0].release = .25;
    require(prepared.prepare(48000, 120, settings), "Prepare original D-A-H-D-S-R timing");
    VoiceSamplerModulation timed;
    timed.noteOn(prepared);
    constexpr int indices[]{0, 6000, 12000, 18000, 24000, 30000, 36000};
    constexpr double expected[]{0, 0, .5, 1, 1, .625, .25};
    size_t next = 0;
    for (int i = 0; i <= indices[6]; ++i)
    {
        const auto frame = timed.tick(prepared, {});
        if (i == indices[next])
        {
            require(closeEnough(frame.envelope[0], expected[next]),
                    "Independent literal sample-clock envelope stage/level");
            if (++next == std::size(indices))
                break;
        }
    }
    // Note-off during attack captures the actual current raw level; duplicate off cannot extend it.
    settings = {};
    settings[0].enabled = true;
    settings[0].amount = 1;
    settings[0].attack = 1;
    settings[0].decay = 0;
    settings[0].release = .5;
    require(prepared.configure(120, settings), "Select early-release envelope");
    VoiceSamplerModulation released;
    released.noteOn(prepared);
    for (int i = 0; i < 12000; ++i)
        released.tick(prepared, {});
    released.beginRelease(prepared);
    require(closeEnough(released.tick(prepared, {}).envelope[0], .25),
            "Release captures current attack level .25");
    for (int i = 1; i < 12000; ++i)
        released.tick(prepared, {});
    released.beginRelease(prepared);
    require(closeEnough(released.tick(prepared, {}).envelope[0], .125),
            "Half release/duplicate off retains original capture and age");
    for (int i = 12001; i < 24001; ++i)
        released.tick(prepared, {});
    const auto done = released.tick(prepared, {});
    require(done.envelope[0] == 0 && done.volumeReleaseFinished && done.volumeEnvelopeActive,
            "Bounded full-depth volume release completion is explicit; root owns voice termination");
    released.noteOn(prepared);
    require(released.ageSeconds() == 0 && released.ageBeats() == 0 && !released.released() &&
                released.localPhases()[0] == 0 && released.tick(prepared, {}).envelope[0] == 0,
            "Retrigger independently resets per-note ages/release/phase");

    // Live duration/tempo/rate selection never resets the accumulated age/oscillator phase.
    settings = {};
    settings[2].enabled = true;
    settings[2].amount = 1;
    settings[2].attack = 1;
    settings[2].decay = 0;
    settings[2].lfoEnabled = true;
    settings[2].lfoAmount = 1;
    settings[2].lfoTempo = true;
    require(prepared.prepare(48000, 120, settings), "Prepare seconds envelope/tempo LFO");
    VoiceSamplerModulation continuity;
    continuity.noteOn(prepared);
    for (int i = 0; i < 6000; ++i)
        continuity.tick(prepared, {});
    const auto oldSeconds = continuity.ageSeconds(), oldBeats = continuity.ageBeats();
    const auto oldPhase = continuity.localPhases()[2];
    settings[2].attack = .5;
    require(prepared.configure(60, settings), "Select new tempo/duration outside voice runtime");
    const auto changed = continuity.tick(prepared, {});
    require(closeEnough(changed.envelope[2], .25) && closeEnough(oldSeconds, .125) &&
                closeEnough(oldBeats, .25) && closeEnough(oldPhase, .25) &&
                closeEnough(continuity.localPhases()[2] - oldPhase, 1. / 48000.),
            "Live duration uses retained age and tempo changes only future phase increment");
    const auto phaseBeforeRate = continuity.localPhases()[2], secondsBeforeRate = continuity.ageSeconds();
    require(prepared.prepare(96000, 60, settings), "Prepare new rate/shared table");
    continuity.tick(prepared, {});
    require(closeEnough(continuity.ageSeconds() - secondsBeforeRate, 1. / 96000.) &&
                closeEnough(continuity.localPhases()[2] - phaseBeforeRate, 1. / 96000.),
            "Rate preparation changes increments while a retained runtime keeps phase/time");
    const auto phaseBeforeSpeed = continuity.localPhases()[2];
    settings[2].lfoPeriod = 2;
    require(prepared.configure(60, settings), "Select LFO period without phase reset");
    continuity.tick(prepared, {});
    require(closeEnough(continuity.localPhases()[2] - phaseBeforeSpeed, 1. / 192000.),
            "LFO speed edit preserves prior phase and changes only the next increment");
    settings[2].tempo = true;
    require(prepared.configure(60, settings), "Select tempo envelope without age reset");
    const auto beatsBefore = continuity.ageBeats();
    require(closeEnough(continuity.tick(prepared, {}).envelope[2], beatsBefore / .5),
            "Tempo envelope uses the continuous integrated beat age, not current-tempo times seconds");

    settings = {};
    settings[2].lfoEnabled = true;
    settings[2].lfoAmount = 1;
    require(prepared.prepare(48000, 120, settings), "Prepare independent per-note phase");
    VoiceSamplerModulation older, fresh;
    older.noteOn(prepared);
    for (int i = 0; i < 12000; ++i)
        older.tick(prepared, {});
    fresh.noteOn(prepared);
    require(closeEnough(older.tick(prepared, {}).lfo[2], 1) && fresh.tick(prepared, {}).lfo[2] == 0,
            "Per-note LFO retrigger is independent of an older note");
    settings[2].lfoGlobal = true;
    require(prepared.configure(120, settings), "Select externally owned global phase");
    SamplerModulationPhases shared{};
    shared[2] = .25;
    require(older.tick(prepared, shared).lfo[2] == 1 && fresh.tick(prepared, shared).lfo[2] == 1,
            "Global shares the actual supplied phase across different-aged voices");
    settings[2].lfoDelay = .1;
    settings[2].lfoAttack = .1;
    require(prepared.configure(120, settings), "Select global phase with per-note delay/fade");
    older.noteOn(prepared);
    for (int i = 0; i < 14400; ++i)
        older.tick(prepared, shared);
    fresh.noteOn(prepared);
    require(older.tick(prepared, shared).lfo[2] == 1 && fresh.tick(prepared, shared).lfo[2] == 0,
            "Fresh global note still observes its own delay, not the older note's gate");
    for (int i = 1; i < 7200; ++i)
        fresh.tick(prepared, shared);
    require(closeEnough(fresh.tick(prepared, shared).lfo[2], .5),
            "Per-note global-LFO fade is independently half-open");

    // Literal PCM multiplication and full signed Env+LFO sum, not implementation-derived coefficients.
    settings = {};
    settings[0].enabled = true;
    settings[0].amount = .5;
    settings[0].attack = settings[0].decay = 0;
    settings[0].sustain = .25;
    settings[0].lfoEnabled = true;
    settings[0].lfoAmount = .5;
    settings[0].lfoShape = 2;
    settings[2].enabled = true;
    settings[2].amount = 1;
    settings[2].attack = settings[2].decay = 0;
    settings[2].sustain = 1;
    settings[2].lfoEnabled = true;
    settings[2].lfoAmount = 1;
    settings[2].lfoShape = 2;
    require(prepared.prepare(48000, 120, settings), "Prepare literal signed/volume destinations");
    VoiceSamplerModulation audio;
    audio.noteOn(prepared);
    auto frame = audio.tick(prepared, {});
    require(frame.output[2] == 2 && frame.volumeMultiplier == .9375 &&
                .25 * frame.volumeMultiplier == .234375 && -.125 * frame.volumeMultiplier == -.1171875,
            "Exact original positive-volume/LFO stereo PCM and unclamped full2 addition");
    settings[0].amount = -.5;
    require(prepared.configure(120, settings), "Select inverted volume envelope");
    for (int i = 0; i < prepared.smoothingSamples; ++i)
        frame = audio.tick(prepared, {});
    require(frame.volumeMultiplier == 1.3125 && .25 * frame.volumeMultiplier == .328125 &&
                -.125 * frame.volumeMultiplier == -.1640625,
            "Exact independent inverted volume law after one five-millisecond target ramp");
    settings = {};
    require(prepared.configure(120, settings), "Disable all modulation");
    const auto beforeDisable = frame.volumeMultiplier;
    frame = audio.tick(prepared, {});
    require(!frame.volumeEnvelopeActive && frame.volumeMultiplier < beforeDisable &&
                frame.volumeMultiplier > 1,
            "Disable reports exact config admission while output returns smoothly toward neutral");
    for (int i = 1; i < prepared.smoothingSamples; ++i)
        frame = audio.tick(prepared, {});
    require(frame.volumeMultiplier == 1 && frame.output[2] == 0,
            "One five-millisecond smoothing stage reaches exact neutral, no hidden asymptotic residue");
    settings[0].enabled = true;
    settings[0].lfoEnabled = true;
    require(prepared.configure(120, settings), "Enable zero-depth sources");
    frame = audio.tick(prepared, {});
    require(frame.volumeMultiplier == 1 && !frame.volumeEnvelopeActive,
            "Zero envelope/LFO amounts remain exactly legacy-neutral");

    // Actual128-voice fixed-storage kernel block under the original256/48k virtual budget.
    settings = {};
    for (size_t i = 0; i < settings.size(); ++i)
    {
        settings[i].enabled = i != 1;
        settings[i].amount = .5;
        settings[i].lfoEnabled = true;
        settings[i].lfoAmount = .5;
    }
    require(prepared.prepare(48000, 120, settings), "Prepare original maximum-voice kernel workload");
    std::array<VoiceSamplerModulation, 128> voices;
    for (auto &voice : voices)
        voice.noteOn(prepared);
    const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
    const bool previous = callbackAllocationProbe;
    callbackAllocationProbe = true;
    const auto began = juce::Time::getHighResolutionTicks();
    bool finite = true;
    SamplerModulationPhases global{};
    for (int sample = 0; sample < 256; ++sample)
    {
        for (auto &voice : voices)
        {
            const auto observed = voice.tick(prepared, global);
            finite &= std::isfinite(observed.volumeMultiplier) && observed.volumeMultiplier >= 0 &&
                      observed.volumeMultiplier <= 2;
            for (size_t i = 1; i < observed.output.size(); ++i)
                finite &=
                    std::isfinite(observed.output[i]) && observed.output[i] >= -2 && observed.output[i] <= 2;
        }
        for (size_t i = 0; i < global.size(); ++i)
            global[i] = PreparedSamplerModulation::wrap(global[i] + prepared.globalIncrements[i]);
    }
    const auto elapsed =
        juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - began);
    callbackAllocationProbe = previous;
    require(finite && callbackAllocations.load() == allocations &&
                callbackDestructions.load() == destructions && elapsed <= 256. / 48000.,
            "Original128-voice modulation kernel finite/heap-free256/48k callback deadline");
    require(folder.createDirectory().wasOk(), "Owned envelope kernel evidence folder");
    require(folder.getChildFile("sampler-envelope-kernel.txt")
                .replaceWithText("PASS original D-A-H-D-S-R/curve/release/clock/LFO/global "
                                 "gate/neutral/PCM/smoothing cases.128 voices256/48k "
                                 "virtual kernel block seconds=" +
                                 juce::String(elapsed, 9) +
                                 "; no Sampler/VoiceBank integration, device/provider/native input, FL "
                                 "parity or listening claim.\n"),
            "Retain original kernel scope/actual measured block timing");
}
} // namespace sampler_envelope_audio_acceptance
