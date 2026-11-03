// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerMonoPortamentoAcceptance.h"
#include "../src/MidiRecording.h"

// Original musical and ownership oracles, not observed FL DSP curve evidence.
namespace sampler_note_modes_audio_acceptance
{
using namespace vibera;
using sampler_mono_portamento_acceptance::Banks;
using sampler_mono_portamento_acceptance::callback;
using sampler_mono_portamento_acceptance::settings;
using sampler_mono_portamento_acceptance::stimulus;
struct State
{
    bool found = false, released = false;
    int group = -1;
    double delta = 0, position = 0, cutoff = 0, resonance = 0, gate = 0;
    float gain = 0, left = 0, right = 0;
    std::uint64_t serial = 0, frames = 0;
};
inline State state(VoiceBank &bank, Id id)
{
    State result;
    bank.visitActive(
        [&](const auto &v) noexcept
        {
            if (v.note == id)
                result = {true,
                          v.noteReleased,
                          v.colourGroup,
                          std::abs(v.delta),
                          v.position,
                          v.glide.frames ? v.glide.current.cutoff : v.expression.filterCutoffOffset,
                          v.glide.frames ? v.glide.current.resonance : v.expression.filterResonanceOffset,
                          v.gateBeats,
                          v.gain,
                          v.leftGain,
                          v.rightGain,
                          v.triggerSerial,
                          v.glide.frames};
        });
    return result;
}
inline void tests()
{
    for (const int rate : {44100, 48000, 96000})
    {
        auto sample = stimulus(rate);
        auto mode = settings();
        mode.slideTime = .01;
        mode.gateBeats = 1;
        mode.fullPortamento = true;
        juce::AudioBuffer<float> pcm(2, rate / 10 + 8);
        for (bool channelPorta : {false, true})
            for (bool notePorta : {false, true})
            {
                mode.portamento = channelPorta;
                auto b = std::make_unique<Banks>(sample, rate, mode);
                callback(
                    [&]() noexcept
                    {
                        b->bank[0].noteOn(60, .8f, 1, 0, 0, 0, {}, 0, false, 3);
                        b->render(pcm, 100);
                        b->bank[1].noteOn(72, .6f, 2, 0, 0, 0, {}, 0, notePorta, 3);
                    });
                const auto incoming = state(b->bank[1], 2);
                const bool effective = channelPorta != notePorta;
                require(incoming.found && (incoming.frames > 0) == effective &&
                            (incoming.gate == 0) == effective,
                        "Four Porta XOR combinations also govern captured FullPorta Gate exemption");
            }
        mode = settings();
        mode.mono = true;
        mode.maxPoly = 1;
        auto mono = std::make_unique<Banks>(sample, rate, mode);
        callback(
            [&]() noexcept
            {
                mono->bank[0].noteOn(60, .8f, 1, 0, 0, 0, {}, 0, false, 0);
                mono->bank[1].noteOn(64, .8f, 2, 0, 0, 0, {}, 0, false, 1);
                mono->render(pcm, 100);
            });
        require(state(mono->bank[0], 1).found && state(mono->bank[1], 2).found,
                "Mono is per-color and overrides user Max poly without stealing another color");
        callback([&]() noexcept { mono->bank[2].noteOn(67, .8f, 3, 0, 0, 0, {}, 0, false, 0); });
        require(!state(mono->bank[0], 1).found && state(mono->bank[1], 2).found &&
                    state(mono->bank[2], 3).found,
                "Later same-color Mono note releases only its own logical predecessors across banks");
        callback(
            [&]() noexcept
            {
                mono->bank[1].visitActive([&](auto &voice) noexcept
                                          { mono->bank[1].reconcileScore(voice, .8f, {}, 5); });
                mono->bank[3].noteOn(72, .8f, 4, 0, 0, 0, {}, 0, false, 5);
            });
        require(state(mono->bank[3], 4).frames > 0 && state(mono->bank[2], 3).found,
                "Live recoloring migrates Mono foreground without losing glide or another group");

        mode = settings();
        auto b = std::make_unique<Banks>(sample, rate, mode);
        const auto target = PreparedNoteExpression::prepare(.5, 0, .7, .3);
        const int duration = int(std::floor(.01 * rate + .5));
        callback(
            [&]() noexcept
            {
                b->bank[0].applySlide(99, 7, 0, 72, .2f, target, 0, 0, .01);
                b->render(pcm, duration);
            });
        require(b->active() == 0 && pcm.getMagnitude(0, 0, duration) == 0,
                "A Slide without sounding targets is genuinely silent and consumes no voice");
        callback(
            [&]() noexcept
            {
                b->bank[0].noteOn(60, .8f, 10);
                b->bank[1].noteOn(64, .8f, 11);
                b->bank[2].noteOn(67, .8f, 12, 0, 0, 0, {}, 0, false, 1);
                b->render(pcm, 100);
            });
        const auto lower = state(b->bank[0], 10), upper = state(b->bank[1], 11);
        const auto other = state(b->bank[2], 12);
        const double reference = std::max(b->bank[0].slideReference(0), b->bank[1].slideReference(0));
        callback(
            [&]() noexcept
            {
                for (auto &bank : b->bank)
                    bank.applySlide(99, 7, 0, 72, .2f, target, 0, reference, .01);
                b->render(pcm, duration / 2);
            });
        const auto middle = state(b->bank[0], 10);
        require(middle.delta > lower.delta && middle.delta < 2. && middle.gain < lower.gain &&
                    middle.gain > .2f && middle.position > lower.position && middle.serial == lower.serial,
                "A silent Slide interpolates pitch and velocity without source/envelope retrigger");
        auto tuned = mode;
        tuned.cents = 50;
        const auto tunedPreparation = PreparedSamplerSettings(tuned);
        callback(
            [&]() noexcept
            {
                b->bank[0].configure(tunedPreparation);
                b->render(pcm, 1);
            });
        require(std::abs(state(b->bank[0], 10).gain - middle.gain) < .01f,
                "Mid-Slide global tuning rebase keeps current gain instead of jumping to its origin");
        const auto ordinaryPreparation = PreparedSamplerSettings(mode);
        callback([&]() noexcept { b->bank[0].configure(ordinaryPreparation); });
        callback([&]() noexcept { b->render(pcm, duration - duration / 2 - 1); });
        const auto finalLow = state(b->bank[0], 10), finalHigh = state(b->bank[1], 11);
        require(std::abs(finalHigh.delta - 2.) < 1.e-10 &&
                    std::abs(finalLow.delta / finalHigh.delta - lower.delta / upper.delta) < 1.e-10 &&
                    std::abs(finalLow.gain - .2f) < 1.e-6 && finalLow.left == .5f && finalLow.right == 1 &&
                    std::abs(finalLow.cutoff - .4) < 1.e-12 && std::abs(finalLow.resonance + .4) < 1.e-12,
                "Chord interval is preserved; highest reaches Slide pitch with exact property endpoints");
        const auto untouched = state(b->bank[2], 12);
        require(untouched.delta == other.delta && untouched.gain == other.gain && untouched.group == 1 &&
                    untouched.frames == 0 && untouched.serial == other.serial,
                "Another color is excluded from every Slide property and ownership change");
        const auto prepared = PreparedSamplerSettings(mode);
        callback(
            [&]() noexcept
            {
                b->configure(prepared);
                for (auto &bank : b->bank)
                    bank.visitActive([&](auto &voice) noexcept
                                     { bank.reconcileScore(voice, .8f, {}, voice.colourGroup); });
                b->render(pcm, 100);
            });
        require(state(b->bank[0], 10).delta == finalLow.delta &&
                    state(b->bank[1], 11).delta == finalHigh.delta,
                "Identical score/settings publication retains completed Slide endpoints");
        callback(
            [&]() noexcept
            {
                for (auto &bank : b->bank)
                    bank.applySlide(100, 7, .1, 60, .4f, {}, 0, 2., .01);
                b->render(pcm, duration / 2);
                b->bank[0].releaseTagged(10, 0);
            });
        // visitActive excludes releasing envelope voices. A one-shot source
        // retains the logical off and audible source, making its endpoint visible.
        auto oneShot = sample;
        oneShot.oneShot = true;
        auto oneShotMode = settings();
        oneShotMode.envelope = false;
        auto tail = std::make_unique<Banks>(oneShot, rate, oneShotMode);
        callback(
            [&]() noexcept
            {
                tail->bank[0].noteOn(60, .8f, 21);
                tail->render(pcm, 100);
                tail->bank[0].applySlide(101, 7, 0, 72, .2f, {}, 0, 1., .01);
                tail->render(pcm, duration / 2);
                tail->bank[0].releaseTagged(21, 0);
            });
        const auto frozen = state(tail->bank[0], 21);
        require(frozen.found && frozen.released && frozen.frames == 0 && frozen.delta > 1 && frozen.delta < 2,
                "Early logical off terminates Slide participation while one-shot PCM continues");
        const auto tailPrepared = PreparedSamplerSettings(oneShotMode);
        callback(
            [&]() noexcept
            {
                tail->configure(tailPrepared);
                tail->render(pcm, duration);
            });
        require(state(tail->bank[0], 21).delta == frozen.delta &&
                    state(tail->bank[0], 21).gain == frozen.gain,
                "Unrelated publication cannot pull a released one-shot to an abandoned Slide target");

        // Production immutable timeline, including velocity-zero silent control.
        auto project = Project::empty();
        project.tempo = 60;
        auto &pattern = project.patterns.front();
        const auto channel = project.channels.front().id;
        pattern.notes = {{project.allocate(), channel, 0, 2, 60, .8f, false}};
        Note slide{project.allocate(), channel, .01, .02, 72, 0.f, false};
        slide.slide = true;
        pattern.notes.push_back(slide);
        project.channels.front().sampler = mode;
        auto document = compileLiveDocument(project, true, pattern.id, 1);
        require(document && document->find(channel)->notes.size() == 2,
                "Zero-velocity silent Slide reaches prepared timeline as a control event");
        auto score = std::make_unique<Banks>(sample, rate, mode);
        LiveSequencer sequencer;
        callback(
            [&]() noexcept
            {
                pcm.clear();
                sequencer.process(*document, channel, 0, int(.02 * rate), rate, true, score->bank[0], pcm, 0);
            });
        const auto scoreNote = pattern.notes.front().id;
        require(score->active() == 1 && std::abs(state(score->bank[0], scoreNote).delta - 1.5) < 1.e-10 &&
                    state(score->bank[0], scoreNote).gain < .8f,
                "Real score scheduling applies a silent Slide without an extra sounding gate");
        const auto sourceOwner = state(score->bank[0], scoreNote).serial;
        document = compileLiveDocument(project, true, pattern.id, 2);
        callback(
            [&]() noexcept
            {
                pcm.clear();
                sequencer.process(*document, channel, .02, int(.01 * rate), rate, true, score->bank[0], pcm,
                                  0);
            });
        require(state(score->bank[0], scoreNote).serial == sourceOwner &&
                    std::abs(state(score->bank[0], scoreNote).delta - 2.) < 1.e-10,
                "Unchanged timeline generation does not restart or shorten Slide trajectory");
        pattern.notes.back().velocity = .5f;
        pattern.notes.back().pan = -1;
        document = compileLiveDocument(project, true, pattern.id, 3);
        callback(
            [&]() noexcept
            {
                pcm.clear();
                sequencer.process(*document, channel, .03, 1, rate, true, score->bank[0], pcm, 0);
            });
        const auto endpointEdit = state(score->bank[0], scoreNote);
        require(endpointEdit.serial == sourceOwner && endpointEdit.gain > 0 && endpointEdit.gain < .01f &&
                    endpointEdit.right > .99f,
                "Post-Slide gain/pan edits dezipper from actual audible bases without a one-sample jump");
        pattern.notes.pop_back();
        document = compileLiveDocument(project, true, pattern.id, 4);
        callback(
            [&]() noexcept
            {
                pcm.clear();
                sequencer.process(*document, channel, .03 + 1. / rate, int(.05 * rate), rate, true,
                                  score->bank[0], pcm, 0);
            });
        require(state(score->bank[0], scoreNote).serial == sourceOwner &&
                    std::abs(state(score->bank[0], scoreNote).delta - 1.) < .0001,
                "Deleting Slide reconciles toward ordinary score without resetting source owner");
    }

    auto source = stimulus(48000);
    auto mode = settings();
    auto b = std::make_unique<Banks>(source, 48000, mode);
    auto project = Project::empty();
    project.tempo = 60;
    auto &part = project.patterns.front();
    const auto channel = project.channels.front().id;
    project.channels.front().sampler = mode;
    Note normal{project.allocate(), channel, 0, 2, 60, .8f, false};
    Note a{project.allocate(), channel, .01, .2, 48, .4f, false};
    Note newer{project.allocate(), channel, .02, .08, 72, .3f, false};
    Note joined{project.allocate(), channel, .03, 1, 60, .8f, false};
    a.slide = newer.slide = true;
    part.notes = {normal, a, newer, joined};
    auto doc = compileLiveDocument(project, true, part.id, 1);
    LiveSequencer sequence;
    juce::AudioBuffer<float> pcm(2, 2400);
    callback(
        [&]() noexcept
        {
            pcm.clear();
            sequence.process(*doc, channel, 0, 1920, 48000, true, b->bank[0], pcm, 0);
        });
    require(state(b->bank[0], joined.id).delta == 1 && state(b->bank[0], joined.id).frames == 0,
            "A voice born after Slide admission does not silently join its ongoing trajectory");
    part.notes.erase(part.notes.begin() + 2);
    doc = compileLiveDocument(project, true, part.id, 2);
    callback(
        [&]() noexcept
        {
            pcm.clear();
            sequence.process(*doc, channel, .04, 256, 48000, true, b->bank[0], pcm, 0);
        });
    require(state(b->bank[0], joined.id).delta == 1 && state(b->bank[0], joined.id).frames == 0,
            "Deleting newer Slide B cannot revive superseded A or recruit later voices");

    MidiTake take;
    take.begin(1, 0, 0, 120, 1, 0, 0);
    take.add({channel, 0, 1, 0, {0x95, 60, 100}});
    take.add({channel, .75, 1, 0, {0x85, 60, 64}});
    const auto recorded = take.finish(1);
    require(recorded.size() == 2 && recorded[0].colourGroup == 5 && recorded[1].colourGroup == 5 &&
                recorded[0].releaseVelocity == 0 && recorded[1].releaseVelocity == 64. / 127,
            "Physical MIDI channel survives loop-split take notes and actual release velocity");
}
} // namespace sampler_note_modes_audio_acceptance
