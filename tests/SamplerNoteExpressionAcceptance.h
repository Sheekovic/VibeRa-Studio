// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "LiveTimeline.h"
#include <limits>

// Original periodic PCM, independent frequency/neutral-image oracles. This
// bounded experiment qualifies sampler coefficients and prepared publication,
// not project persistence, Graph UI, native Synth or vendor note expression.
static void samplerNoteExpressionTests()
{
    require(PreparedNoteExpression::valid(-1, -100) && PreparedNoteExpression::valid(1, 100) &&
                !PreparedNoteExpression::valid(1.01, 0) && !PreparedNoteExpression::valid(0, 100.01) &&
                !PreparedNoteExpression::valid(std::numeric_limits<double>::quiet_NaN(), 0),
            "Expression preparation has finite musical boundaries");
    Sample sample;
    sample.sampleRate = 48000;
    sample.oneShot = false;
    sample.data.setSize(2, 96000);
    for (int i = 0; i < sample.data.getNumSamples(); ++i)
    {
        const auto value = float(.2 * std::sin(2 * juce::MathConstants<double>::pi * 1000 * i / 48000.));
        sample.data.setSample(0, i, value);
        sample.data.setSample(1, i, value * .5f);
    }
    sample.prepareNormalization();
    SamplerSettings settings;
    settings.envelope = true;
    settings.attack = settings.decay = 0;
    settings.sustain = 1;
    settings.loop = true;
    const PreparedSamplerSettings prepared(settings);
    const auto left = PreparedNoteExpression::prepare(-1, 100);
    const auto right = PreparedNoteExpression::prepare(1, -100);
    VoiceBank voices;
    voices.prepare(sample, 48000);
    voices.configure(prepared);
    voices.noteOn(60, .75f, 10, 20, 0, 0, left);
    voices.noteOn(60, .75f, 11, 20, 0, 0, right);
    juce::AudioBuffer<float> pcm(2, 24000);
    pcm.clear();
    const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
    callbackAllocationProbe = true;
    voices.render(pcm, 0, pcm.getNumSamples());
    callbackAllocationProbe = false;
    require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
            "Independent expression rendering performs no measured C++ allocation or destruction");
    auto frequency = [&](int channel)
    {
        double first = 0, last = 0;
        int crossings = 0;
        for (int i = 1000; i + 1 < pcm.getNumSamples(); ++i)
        {
            const auto a = pcm.getSample(channel, i), b = pcm.getSample(channel, i + 1);
            if (a <= 0 && b > 0)
            {
                const auto crossing = i - double(a) / (double(b) - a);
                if (crossings++ == 0)
                    first = crossing;
                last = crossing;
            }
        }
        require(crossings > 100, "Independent frequency oracle has sufficient actual zero crossings");
        return (crossings - 1) * 48000. / (last - first);
    };
    require(std::abs(frequency(0) - 1000 * std::pow(2., 1. / 12)) < .05 &&
                std::abs(frequency(1) - 1000 * std::pow(2., -1. / 12)) < .05,
            "Simultaneous same-key notes retain independent +/-100-cent pitch and hard pan");
    require(pcm.getMagnitude(0, 24000) > .149f && pcm.getMagnitude(1, 24000) > .074f,
            "Original stereo side amplitude is retained at the respective hard endpoints");

    VoiceBank neutral;
    neutral.prepare(sample, 48000);
    neutral.configure(prepared);
    neutral.noteOn(60, .75f);
    pcm.clear();
    neutral.render(pcm, 0, 1024);
    for (int i = 0; i < 1024; ++i)
        for (int c = 0; c < 2; ++c)
            require(pcm.getSample(c, i) == float(double(sample.data.getSample(c, i)) * .75),
                    "Neutral expression preserves original stereo samples exactly");

    // Exercise the actual prepared sequencer/reconciliation path in both modes.
    for (bool patternMode : {true, false})
    {
        auto project = Project::empty();
        const auto channel = project.channels.front().id, pattern = project.patterns.front().id;
        project.patterns.front().autoLength = false;
        project.patterns.front().length = 4;
        project.channels.front().sampler = settings;
        const auto first = project.allocate(), second = project.allocate();
        project.patterns.front().notes = {{first, channel, 0, 3, 60, .75f, true},
                                          {second, channel, 0, 3, 60, .75f, true}};
        project.placements.push_back({project.allocate(), pattern, 0, 0, 4, 0});
        auto opening = compileLiveDocument(project, patternMode, pattern, 1);
        for (auto &entry : opening->channels.front().notes)
            entry.expression = entry.note == first ? left : right;
        LiveTimeline timeline;
        require(timeline.publish(opening),
                "Opening expression document publishes through real immutable slots");
        auto openingLease = timeline.acquire();
        require(openingLease.document != nullptr, "Audio consumer pins the prepared expression document");
        VoiceBank score, held;
        score.prepare(sample, 48000);
        held.prepare(sample, 48000);
        score.configure(prepared);
        held.configure(prepared);
        held.noteOn(60, .25f, 0, 0, 0, 93);
        std::array<VoiceBank *, 1> input{&held};
        LiveSequencer sequence;
        pcm.clear();
        sequence.process(*openingLease.document, channel, 0, 1024, 48000, true, score, pcm, 0, input);
        require(score.activeVoices() == 2 && held.activeVoices() == 1,
                "Actual prepared score has two independent same-key voices plus separate held input");
        std::array<double, 2> positions{};
        std::array<std::uint64_t, 2> serials{};
        score.visitActive(
            [&](auto &v)
            {
                const auto index = v.note == first ? 0 : 1;
                positions[size_t(index)] = v.position;
                serials[size_t(index)] = v.triggerSerial;
            });
        auto changed = compileLiveDocument(project, patternMode, pattern, 2);
        const auto changedLeft = PreparedNoteExpression::prepare(-.5, -50);
        for (auto &entry : changed->channels.front().notes)
            entry.expression = entry.note == first ? changedLeft : right;
        require(timeline.publish(changed),
                "Edited expression publishes without replacing the retained voice banks");
        auto changedLease = timeline.acquire();
        require(changedLease.document != nullptr, "Audio consumer pins the new expression generation");
        pcm.clear();
        const auto beforeAllocations = callbackAllocations.load(),
                   beforeDestructions = callbackDestructions.load();
        callbackAllocationProbe = true;
        sequence.process(*changedLease.document, channel, 1024. / 48000, 256, 48000, true, score, pcm, 0,
                         input);
        callbackAllocationProbe = false;
        require(callbackAllocations.load() == beforeAllocations &&
                    callbackDestructions.load() == beforeDestructions,
                "Expression reconciliation and shared held rendering allocate and destroy no C++ objects");
        score.visitActive(
            [&](auto &v)
            {
                const auto index = v.note == first ? 0 : 1;
                const auto &expected = index == 0 ? changedLeft : right;
                require(v.triggerSerial == serials[size_t(index)] && v.position > positions[size_t(index)] &&
                            v.envelope == 1 && v.targetDelta == expected.tuning && !v.releasing,
                        "Generation update retains phase/voice/envelope and independent expression");
            });
        // Render the real shared path after retargeting. Remove the neutral held
        // voice only from this frequency measurement, keeping its identity alive.
        pcm.clear();
        sequence.process(*changedLease.document, channel, 1280. / 48000, 16384, 48000, true, score, pcm, 0);
        juce::AudioBuffer<float> heldPcm(2, 16384);
        heldPcm.clear();
        held.render(heldPcm, 0, 16384);
        require(std::abs(frequency(0) - 1000 * std::pow(2., -50. / 1200)) < .15,
                "Live fine-pitch change reaches actual rendered PCM without a voice restart");
        auto projection = [&](int channel)
        {
            const auto hz = 1000 * std::pow(2., -50. / 1200);
            double real = 0, imaginary = 0;
            for (int i = 4096; i < 16384; ++i)
            {
                const auto phase = 2 * juce::MathConstants<double>::pi * hz * i / 48000;
                real += pcm.getSample(channel, i) * std::cos(phase);
                imaginary += pcm.getSample(channel, i) * std::sin(phase);
            }
            return std::hypot(real, imaginary);
        };
        const auto ratio = projection(1) / projection(0);
        require(ratio > .20 && ratio < .30,
                "Changed pan retains quarter R/L amplitude: original stereo ratio .5 times pan ratio .5");
        settings.cents = 1200;
        const PreparedSamplerSettings octave(settings);
        score.configure(octave);
        score.visitActive(
            [&](auto &v)
            {
                const auto &expected = v.note == first ? changedLeft : right;
                require(v.targetDelta == 2 * expected.tuning,
                        "Global sampler reconfiguration retains the note-specific tuning factor");
            });
        settings.cents = 0;
        held.visitActive(
            [&](auto &v)
            {
                require(v.liveSequence == 93 && v.position == 17664 && v.targetDelta == 1 &&
                            v.leftGain == 1 && v.rightGain == 1 && !v.releasing,
                        "Score expression never changes physical/typing/preview voice ownership");
            });
        auto stopped = compileLiveDocument(project, patternMode, pattern, 3);
        stopped->channels.front().notes.clear();
        stopped->channels.front().events.clear();
        require(timeline.publish(stopped),
                "Deletion publishes while earlier immutable generations remain pinned");
        auto stoppedLease = timeline.acquire();
        require(stoppedLease.document != nullptr, "Audio consumer pins the deleted-note generation");
        pcm.clear();
        sequence.process(*stoppedLease.document, channel, 17664. / 48000, 1024, 48000, true, score, pcm, 0,
                         input);
        require(score.activeVoices() == 0 && held.activeVoices() == 1,
                "Deleted sounding expression notes release without stealing held input");
        score.reset();
        score.configure(prepared);
        score.noteOn(60, .75f);
        score.visitActive(
            [&](auto &v)
            {
                require(v.expression.tuning == 1 && v.leftGain == 1 && v.rightGain == 1,
                        "Reused voice slots cannot retain earlier pan or fine pitch");
            });
    }
    // Independent source-position oracle: no product ratio/preparation helper
    // generates the expected33/32 or32/33 source position.
    for (bool patternMode : {true, false})
        for (bool positive : {true, false})
        {
            auto project = Project::empty();
            auto &part = project.patterns.front();
            part.autoLength = false;
            part.length = 4;
            const auto channel = project.channels.front().id;
            const auto id = project.allocate();
            part.notes = {{id, channel, 0, 1, 60, 1.f, true, false, positive ? -.5 : .5,
                           positive ? 53.272943230144 : -53.272943230144}};
            project.placements = {{project.allocate(), part.id, 0, 0, 4, 0}};
            const auto document = compileLiveDocument(project, patternMode, part.id, 1);
            const auto &preparedNote = document->channels.front().notes.front();
            const double ratio = positive ? 33. / 32 : 32. / 33;
            require(std::abs(preparedNote.expression.tuning - ratio) < 1.e-14 &&
                        preparedNote.expression.left == (positive ? 1.f : .5f) &&
                        preparedNote.expression.right == (positive ? .5f : 1.f),
                    "Actual model/sequence/CONTROL projection preserves literal note expression");
            VoiceBank bank;
            bank.prepare(sample, 48000);
            bank.configure(prepared);
            LiveSequencer sequencer;
            juce::AudioBuffer<float> actual(2, 256);
            actual.clear();
            sequencer.process(*document, channel, 0, 256, 48000, true, bank, actual, 0);
            for (int frame = 16; frame < 256; ++frame)
            {
                const double position = frame * ratio;
                const int index = int(position);
                const double fraction = position - index;
                for (int side = 0; side < 2; ++side)
                {
                    const auto *original = sample.data.getReadPointer(side);
                    const double interpolated =
                        original[index] + fraction * (double(original[index + 1]) - original[index]);
                    const double balance = side == 0 ? (positive ? 1 : .5) : (positive ? .5 : 1);
                    require(std::abs(actual.getSample(side, frame) - interpolated * balance) < 2.e-6,
                            "Pattern/Song note expression PCM matches independent rational phase and stereo "
                            "balance");
                }
            }
        }
}
