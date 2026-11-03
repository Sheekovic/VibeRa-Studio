// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/LiveTimeline.h"
#include <array>
#include <cstring>
#include <limits>
#include <memory>

// Original PCM and literal manual-target oracles, not FL DSP equivalence.
// Include after require and the existing callback allocation/destruction probes.
namespace sampler_modulation_audio_acceptance
{
using namespace vibera;
inline SamplerSettings settings(double cutoff, double resonance, int type = 1)
{
    SamplerSettings result;
    result.envelope = true;
    result.attack = result.decay = 0;
    result.sustain = 1;
    result.filterType = type;
    result.filterCutoff = cutoff;
    result.filterResonance = resonance;
    return result;
}
inline Sample stimulus(int rate)
{
    Sample result;
    result.sampleRate = rate;
    result.oneShot = false;
    result.data.setSize(2, rate * 2 + 1);
    for (int frame = 0; frame < result.data.getNumSamples(); ++frame)
    {
        double value = 0;
        for (double hz : {128., 512., 2048., 8192.})
            value += .03 * std::sin(2 * juce::MathConstants<double>::pi * hz * frame / rate);
        result.data.setSample(0, frame, float(value));
        result.data.setSample(1, frame, float(-value * .375));
    }
    return result;
}
template <class F> inline void callback(F &&action)
{
    const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
    const auto previous = callbackAllocationProbe;
    callbackAllocationProbe = true;
    action();
    callbackAllocationProbe = previous;
    require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
            "Mod X/Y configure, retarget and rendering allocate/destroy no measured C++ objects");
}
inline void render(VoiceBank &bank, juce::AudioBuffer<float> &pcm, int frames)
{
    pcm.clear();
    callback([&]() noexcept { bank.render(pcm, 0, frames); });
}
inline bool same(const juce::AudioBuffer<float> &actual, const juce::AudioBuffer<float> &expected, int frames)
{
    for (int side = 0; side < 2; ++side)
        for (int frame = 0; frame < frames; ++frame)
            if (actual.getSample(side, frame) != expected.getSample(side, frame))
                return false;
    return true;
}
inline bool sumEquals(const juce::AudioBuffer<float> &actual, const juce::AudioBuffer<float> &first,
                      const juce::AudioBuffer<float> &second, int frames)
{
    for (int side = 0; side < 2; ++side)
        for (int frame = 0; frame < frames; ++frame)
            if (actual.getSample(side, frame) !=
                float(first.getSample(side, frame) + second.getSample(side, frame)))
                return false;
    return true;
}
inline double amplitude(const juce::AudioBuffer<float> &pcm, double hz, int rate)
{
    const int count = rate / 4, start = pcm.getNumSamples() - count;
    double real = 0, imaginary = 0;
    for (int frame = 0; frame < count; ++frame)
    {
        const double angle = 2 * juce::MathConstants<double>::pi * hz * frame / rate;
        real += pcm.getSample(0, start + frame) * std::cos(angle);
        imaginary += pcm.getSample(0, start + frame) * std::sin(angle);
    }
    return 2 * std::hypot(real, imaginary) / count;
}
inline void directTests()
{
    const auto neutral = PreparedNoteExpression::prepare(0, 0);
    require(neutral.filterCutoffOffset == 0 && neutral.filterResonanceOffset == 0 &&
                PreparedNoteExpression::valid(0, 0, 0, 1) &&
                !PreparedNoteExpression::valid(0, 0, -.001, .5) &&
                !PreparedNoteExpression::valid(0, 0, .5, 1.001) &&
                !PreparedNoteExpression::valid(0, 0, std::numeric_limits<double>::quiet_NaN(), .5),
            "Prepared Mod X/Y has finite normalized bounds and exact zero neutral offsets");
    const auto invalid = PreparedNoteExpression::prepare(0, 0, .5, std::numeric_limits<double>::infinity());
    require(invalid.filterCutoffOffset == 0 && invalid.filterResonanceOffset == 0,
            "Invalid defensive preparation cannot publish nonfinite filter offsets");
    for (int rate : {44100, 48000, 96000})
    {
        auto sample = stimulus(rate);
        const juce::AudioBuffer<float> original(sample.data);
        auto actual = std::make_unique<VoiceBank>(), reference = std::make_unique<VoiceBank>();
        actual->prepare(sample, rate);
        reference->prepare(sample, rate);
        juce::AudioBuffer<float> pcm(2, 4096), expected(2, 4096);
        actual->configure(PreparedSamplerSettings(settings(1, 0)));
        actual->noteOn(60, .75f, 1, 2, 0, 0, neutral);
        render(*actual, pcm, 4096);
        bool dry = true;
        for (int side = 0; side < 2; ++side)
            for (int frame = 0; frame < 4096; ++frame)
                dry = dry &&
                      pcm.getSample(side, frame) == float(double(sample.data.getSample(side, frame)) * .75);
        require(dry, "Neutral Mod X/Y preserves literal legacy signed stereo PCM bit exactly");

        struct Case
        {
            double cutoff, resonance, x, y, expectedCutoff, expectedResonance;
        };
        // Hand-authored normalized targets. Never call the combination helper
        // when producing the expected manual reference settings.
        const std::array<Case, 4> cases{{{1, 0, .25, .75, .5, .5},
                                         {.25, .25, .625, .375, .5, 0},
                                         {.75, .25, 0, 1, 0, 1},
                                         {.25, .75, 1, 0, 1, 0}}};
        for (int type = 0; type <= 5; ++type)
            for (const auto &test : cases)
            {
                actual->reset();
                reference->reset();
                actual->configure(PreparedSamplerSettings(settings(test.cutoff, test.resonance, type)));
                reference->configure(
                    PreparedSamplerSettings(settings(test.expectedCutoff, test.expectedResonance, type)));
                const auto expression = PreparedNoteExpression::prepare(0, 0, test.x, test.y);
                actual->noteOn(60, .75f, 1, 2, 0, 0, expression);
                reference->noteOn(60, .75f);
                render(*actual, pcm, 4096);
                render(*reference, expected, 4096);
                bool finite = true;
                for (int side = 0; side < 2; ++side)
                    for (int frame = 0; frame < 4096; ++frame)
                        finite = finite && std::isfinite(pcm.getSample(side, frame)) &&
                                 std::abs(pcm.getSample(side, frame)) < 128;
                require(finite && same(pcm, expected, 4096),
                        "Every mode/rate maps literal note offsets and saturated endpoints to manual PCM");
            }
        juce::AudioBuffer<float> spectral(2, rate / 2);
        actual->reset();
        actual->configure(PreparedSamplerSettings(settings(1, 0)));
        actual->noteOn(60, 1, 1, 2, 0, 0, PreparedNoteExpression::prepare(0, 0, .25, .5));
        render(*actual, spectral, spectral.getNumSamples());
        const auto low = amplitude(spectral, 512, rate), high = amplitude(spectral, 8192, rate);
        require(low > .012 && high < .0006,
                "Mod X lowers actual cutoff:512Hz remains audible while8192Hz is rejected");
        actual->reset();
        actual->noteOn(60, 1, 1, 2, 0, 0, PreparedNoteExpression::prepare(0, 0, .25, .75));
        render(*actual, spectral, spectral.getNumSamples());
        require(amplitude(spectral, 512, rate) > low * 1.4,
                "Mod Y changes actual resonance independently of the Mod X cutoff target");
        bool immutable = true;
        for (int side = 0; side < 2; ++side)
            immutable =
                immutable && std::memcmp(sample.data.getReadPointer(side), original.getReadPointer(side),
                                         size_t(sample.data.getNumSamples()) * sizeof(float)) == 0;
        require(immutable, "Per-note filtering leaves original source PCM immutable at every tested rate");
    }
}
inline void heldAndReuseTests()
{
    auto sample = stimulus(48000);
    auto actual = std::make_unique<VoiceBank>(), first = std::make_unique<VoiceBank>(),
         second = std::make_unique<VoiceBank>();
    for (auto *bank : {actual.get(), first.get(), second.get()})
        bank->prepare(sample, 48000);
    auto global = settings(.75, .25);
    actual->configure(PreparedSamplerSettings(global));
    first->configure(PreparedSamplerSettings(settings(.25, .25)));
    second->configure(PreparedSamplerSettings(settings(.75, .75)));
    const auto original = PreparedNoteExpression::prepare(0, 0, .25, .5);
    const auto changed = PreparedNoteExpression::prepare(0, 0, .625, .375);
    actual->noteOn(60, .5f, 101, 17, 0, 0, original, 4);
    actual->noteOn(60, .25f, 102, 17, 0, 0, PreparedNoteExpression::prepare(0, 0, .5, .75), 7);
    first->noteOn(60, .5f);
    second->noteOn(60, .25f);
    juce::AudioBuffer<float> pcm(2, 4096), a(2, 4096), b(2, 4096);
    int position = 0;
    std::array<std::uint64_t, 2> serial{};
    actual->visitActive([&](const auto &voice) noexcept
                        { serial[voice.note == 101 ? 0 : 1] = voice.triggerSerial; });
    const auto drive = [&](int frames)
    {
        render(*actual, pcm, frames);
        render(*first, a, frames);
        render(*second, b, frames);
        position += frames;
        require(sumEquals(pcm, a, b, frames),
                "Contrasting same-key voices match separately rounded literal manual filter banks");
        bool retained = actual->activeVoices() == 2;
        actual->visitActive(
            [&](const auto &voice) noexcept
            {
                const auto index = voice.note == 101 ? 0 : 1;
                retained = retained && voice.triggerSerial == serial[size_t(index)] &&
                           voice.position == position && voice.envelope == 1 && voice.placement == 17 &&
                           voice.pitch == 60 && voice.repeatOrdinal == (index == 0 ? 4 : 7);
            });
        require(retained, "Held modulation retains source phase/envelope and exact occurrence ownership");
    };
    const auto retarget = [&](const PreparedNoteExpression &expression, double cutoff, double resonance)
    {
        const PreparedSamplerSettings expected(settings(cutoff, resonance));
        callback(
            [&]() noexcept
            {
                actual->visitActive(
                    [&](auto &voice) noexcept
                    {
                        if (voice.note == 101)
                            actual->retargetExpression(voice, expression);
                    });
                first->configure(expected);
            });
        drive(512);
    };
    drive(256);
    retarget(changed, 1, 0); // Tuning stays1: filter retarget must still occur.
    global.filterCutoff = global.filterResonance = .5;
    const PreparedSamplerSettings edited(global), expectedFirst(settings(.75, .25)),
        expectedSecond(settings(.5, 1));
    callback(
        [&]() noexcept
        {
            actual->configure(edited);
            first->configure(expectedFirst);
            second->configure(expectedSecond);
        });
    drive(512);
    global = settings(.75, .25);
    const PreparedSamplerSettings restored(global), restoredFirst(settings(1, 0)),
        restoredSecond(settings(.75, .75));
    callback(
        [&]() noexcept
        {
            actual->configure(restored);
            first->configure(restoredFirst);
            second->configure(restoredSecond);
        });
    drive(512);                                   // Cancel global edit while retaining both raw note offsets.
    retarget(PreparedNoteExpression{}, .75, .25); // Reset note to neutral.
    retarget(original, .25, .25);                 // Restore exact opening note target.

    // Existing maxPoly admission steals the old filter voice before new note-on.
    // Reuse must initialize fresh memories and neutral offsets, not inherit sound.
    actual->reset();
    auto oneVoice = settings(1, 0);
    oneVoice.maxPoly = 1;
    actual->configure(PreparedSamplerSettings(oneVoice));
    actual->noteOn(60, .75f, 201, 0, 0, 0, original);
    render(*actual, pcm, 256);
    actual->noteOn(60, .75f, 202);
    render(*actual, pcm, 4096);
    bool dry = actual->activeVoices() == 1;
    for (int side = 0; side < 2; ++side)
        for (int frame = 0; frame < 4096; ++frame)
            dry =
                dry && pcm.getSample(side, frame) == float(double(sample.data.getSample(side, frame)) * .75);
    require(dry, "Voice stealing clears old modulation/filter memories before a neutral legacy note");
    actual->reset();
    actual->noteOn(60, .75f, 203);
    render(*actual, pcm, 4096);
    bool resetDry = true;
    for (int side = 0; side < 2; ++side)
        for (int frame = 0; frame < 4096; ++frame)
            resetDry = resetDry &&
                       pcm.getSample(side, frame) == float(double(sample.data.getSample(side, frame)) * .75);
    require(resetDry, "Explicit bank reset cannot leave stale per-note modulation in the next source");
}
inline void preparedTests()
{
    for (bool patternMode : {true, false})
    {
        auto sample = stimulus(48000);
        auto project = Project::empty();
        const auto channel = project.channels.front().id, pattern = project.patterns.front().id;
        const auto source = project.allocate();
        project.patterns.front().autoLength = false;
        project.patterns.front().length = 4;
        project.patterns.front().notes = {{source, channel, 0, 3, 60, .5f, true}};
        auto &note = project.patterns.front().notes.front();
        note.modX = .25;
        note.modY = .5;
        project.channels.front().sampler = settings(.75, .25);
        project.placements = {{project.allocate(), pattern, 0, 0, 4, 0}};
        auto score = std::make_unique<VoiceBank>(), input = std::make_unique<VoiceBank>(),
             expectedScore = std::make_unique<VoiceBank>(), expectedInput = std::make_unique<VoiceBank>();
        for (auto *bank : {score.get(), input.get(), expectedScore.get(), expectedInput.get()})
            bank->prepare(sample, 48000);
        input->configure(PreparedSamplerSettings(settings(.75, .25)));
        input->noteOn(60, .25f, 0, 0, 0, 93);
        expectedScore->configure(PreparedSamplerSettings(settings(.25, .25)));
        // Literal score preparation: round(.5*127)/127 =64/127. The input
        // velocity stays.25; each voice rounds before its buffer addition.
        expectedScore->noteOn(60, 64.f / 127.f);
        expectedInput->configure(PreparedSamplerSettings(settings(.75, .25)));
        expectedInput->noteOn(60, .25f);
        LiveTimeline timeline;
        LiveSequencer sequencer;
        std::array<VoiceBank *, 1> extra{input.get()};
        juce::AudioBuffer<float> pcm(2, 4096), a(2, 4096), b(2, 4096);
        int position = 0;
        std::uint64_t generation = 0, scoreSerial = 0, inputSerial = 0;
        const auto publish = [&]()
        {
            auto document = compileLiveDocument(project, patternMode, pattern, ++generation);
            require(timeline.publish(document) && !document,
                    "Mod X/Y publishes actual immutable Pattern/Song prepared documents");
        };
        const auto drive = [&](int frames)
        {
            pcm.clear();
            callback(
                [&]() noexcept
                {
                    auto lease = timeline.acquire();
                    if (lease.document)
                    {
                        const auto *lane = lease.document->find(channel);
                        if (lane)
                        {
                            score->configure(lane->sampler);
                            input->configure(lane->sampler);
                            sequencer.process(*lease.document, channel, position / 48000., frames, 48000,
                                              true, *score, pcm, 0, extra);
                        }
                    }
                });
            render(*expectedScore, a, frames);
            render(*expectedInput, b, frames);
            position += frames;
            require(sumEquals(pcm, a, b, frames),
                    "Prepared publication/retarget/global restoration matches literal score and held PCM");
            bool scoreRetained = false, inputRetained = false;
            score->visitActive(
                [&](const auto &voice) noexcept
                {
                    if (scoreSerial == 0)
                        scoreSerial = voice.triggerSerial;
                    scoreRetained = voice.note == source && voice.position == position &&
                                    voice.triggerSerial == scoreSerial && voice.envelope == 1 &&
                                    voice.pitch == 60;
                });
            input->visitActive(
                [&](const auto &voice) noexcept
                {
                    if (inputSerial == 0)
                        inputSerial = voice.triggerSerial;
                    inputRetained = voice.liveSequence == 93 && voice.position == position &&
                                    voice.triggerSerial == inputSerial && voice.envelope == 1;
                });
            require(scoreRetained && inputRetained && score->activeVoices() == 1 &&
                        input->activeVoices() == 1,
                    "Generation updates preserve real score and held-input source/phase/envelope owners");
        };
        publish();
        drive(256);
        note.modX = .625;
        note.modY = .375;
        expectedScore->configure(PreparedSamplerSettings(settings(1, 0)));
        publish();
        drive(512);
        project.channels.front().sampler = settings(.5, .5);
        expectedScore->configure(PreparedSamplerSettings(settings(.75, .25)));
        expectedInput->configure(PreparedSamplerSettings(settings(.5, .5)));
        publish();
        drive(512);
        project.channels.front().sampler = settings(.75, .25);
        expectedScore->configure(PreparedSamplerSettings(settings(1, 0)));
        expectedInput->configure(PreparedSamplerSettings(settings(.75, .25)));
        publish();
        drive(512);
        note.modX = note.modY = .5;
        expectedScore->configure(PreparedSamplerSettings(settings(.75, .25)));
        publish();
        drive(512);
        note.modX = .25;
        expectedScore->configure(PreparedSamplerSettings(settings(.25, .25)));
        publish();
        drive(512);
        require(note.id == source && note.beat == 0 && note.length == 3 && note.pitch == 60 &&
                    note.velocity == .5f && note.step && note.modX == .25 && note.modY == .5,
                "Prepared modulation changes neither source geometry/identity nor unrelated properties");
        // Audio-side restoration only. Canonical Undo/cancel/model persistence
        // is tested by the shared command/application acceptance owner.
        note.modX = note.modY = .5;
        project.channels.front().sampler = settings(1, 0);
        expectedScore->configure(PreparedSamplerSettings(settings(1, 0)));
        expectedInput->configure(PreparedSamplerSettings(settings(1, 0)));
        const int begin = position;
        publish();
        drive(4096);
        bool dry = true;
        for (int side = 0; side < 2; ++side)
            for (int frame = 3840; frame < 4096; ++frame)
            {
                const double value = sample.data.getSample(side, begin + frame);
                dry = dry && pcm.getSample(side, frame) == float(value * (64.f / 127.f)) + float(value * .25);
            }
        require(dry, "Restored neutral/default reaches exact legacy per-voice PCM without a retrigger");
    }
}
} // namespace sampler_modulation_audio_acceptance
inline void samplerModulationAudioTests()
{
    sampler_modulation_audio_acceptance::directTests();
    sampler_modulation_audio_acceptance::heldAndReuseTests();
    sampler_modulation_audio_acceptance::preparedTests();
}
