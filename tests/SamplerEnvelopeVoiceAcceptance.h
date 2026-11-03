// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerFilterAudioAcceptance.h"
#include "Commands.h"

// Original VoiceBank/prepared timeline tests. Reuses only callback guards,
// snapshots and signal projection; expected PCM never calls a modulation kernel.
static void samplerEnvelopeVoiceTests(const juce::File &)
{
    using namespace vibera;
    using sampler_filter_audio_acceptance::projection;
    using sampler_filter_audio_acceptance::render;
    using sampler_filter_audio_acceptance::state;
    const auto realtime = [&](auto &&operation)
    {
        const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
        const bool previous = callbackAllocationProbe;
        callbackAllocationProbe = true;
        operation();
        callbackAllocationProbe = previous;
        require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
                "Envelope configure/render/prepared lease paths allocate/destroy no measured C++ objects");
    };
    Sample sample;
    sample.sampleRate = 48000;
    sample.oneShot = false;
    sample.data.setSize(2, 96001);
    const auto wave = [](int i) { return float(i % 257 - 128) / 1024.f; };
    for (int i = 0; i < sample.data.getNumSamples(); ++i)
        for (int side = 0; side < 2; ++side)
            sample.data.setSample(side, i, wave(i));
    sample.prepareNormalization();
    SamplerSettings neutral;
    neutral.envelope = true;
    neutral.attack = neutral.decay = 0;
    neutral.sustain = 1;
    juce::AudioBuffer<float> pcm(2, 24000), reference(2, 24000);
    auto bank = std::make_unique<VoiceBank>(), manual = std::make_unique<VoiceBank>();
    const auto start = [&](VoiceBank &b, const SamplerSettings &settings)
    {
        b.prepare(sample, 48000);
        b.configure(PreparedSamplerSettings(settings));
        b.noteOn(60, 1, 123);
    };
    start(*bank, neutral);
    render(*bank, pcm, 4096);
    bool exact = true;
    for (int i = 0; i < 4096; ++i)
        for (int side = 0; side < 2; ++side)
            exact = exact && pcm.getSample(side, i) == wave(i);
    require(exact,
            "Disabled five-target modulation preserves signed literal legacy unit-envelope PCM bit exactly");
    auto volume = neutral;
    auto &v = volume.modulation[0];
    v.enabled = true;
    v.attack = v.decay = 0;
    v.sustain = .25;
    v.amount = 1;
    v.release = .02;
    start(*bank, volume);
    render(*bank, pcm, 4096);
    exact = true;
    for (int i = 256; i < 4096; ++i)
        for (int side = 0; side < 2; ++side)
            exact = exact && pcm.getSample(side, i) == float(double(wave(i)) * .25);
    require(exact,
            "Actual Volume envelope reaches independent literal quarter-gain sustain on nonconstant PCM");
    for (double amount : {1., -1.})
    {
        v.amount = amount;
        start(*bank, volume);
        render(*bank, pcm, 4096);
        exact = true;
        for (int i = 256; i < 4096; ++i)
            exact = exact && pcm.getSample(0, i) == float(double(wave(i)) * (amount > 0 ? .25 : .75));
        require(exact, "Signed Volume amount gives independent literal .25 or inverted .75 sustain");
        bank->noteOff(60);
        render(*bank, pcm, 600);
        require(bank->activeVoices() == 1, "Custom20ms Volume release remains owned beyond the old12ms tail");
        render(*bank, pcm, 4096);
        require(bank->activeVoices() == 0 && pcm.getMagnitude(0, 3072, 1024) == 0,
                "Positive/inverted custom Volume release both finish with a bounded silent endpoint");
        start(*bank, volume);
        render(*bank, pcm, 4096);
        bank->releaseAllQuickly();
        render(*bank, pcm, 1024);
        require(bank->activeVoices() == 0 && pcm.getMagnitude(0, 896, 128) == 0,
                "Forced panic bypasses long/inverted envelope tails and actually releases the bank");
    }
    auto pan = neutral;
    pan.modulation[1].lfoEnabled = true;
    pan.modulation[1].lfoAmount = 1;
    pan.modulation[1].lfoPeriod = .02;
    start(*bank, pan);
    render(*bank, pcm, 480);
    double balance = 0;
    for (int i = 96; i < 480; ++i)
        balance += std::abs(pcm.getSample(1, i)) - std::abs(pcm.getSample(0, i));
    require(balance > .1,
            "Rising positive Pan LFO actually attenuates left while the equal right source remains audible");
    auto pitch = neutral;
    pitch.modulation[4].enabled = true;
    pitch.modulation[4].amount = .5;
    pitch.modulation[4].attack = pitch.modulation[4].decay = 0;
    pitch.modulation[4].sustain = 1;
    auto octave = neutral;
    octave.cents = 1200;
    start(*bank, pitch);
    start(*manual, octave);
    render(*bank, pcm, 4096);
    render(*manual, reference, 4096);
    exact = true;
    for (int i = 256; i < 4096; ++i)
        exact =
            exact && pcm.getSample(0, i) == wave(2 * i) && pcm.getSample(0, i) == reference.getSample(0, i);
    require(exact, "Constant half-amount Pitch envelope is an independent exact octave source index and "
                   "manual1200cents");
    Sample tones;
    tones.sampleRate = 48000;
    tones.oneShot = false;
    tones.data.setSize(2, 48001);
    for (int i = 0; i < tones.data.getNumSamples(); ++i)
        for (int side = 0; side < 2; ++side)
            tones.data.setSample(
                side, i,
                float(.04 * (std::sin(2 * juce::MathConstants<double>::pi * 128 * i / 48000.) +
                             std::sin(2 * juce::MathConstants<double>::pi * 4000 * i / 48000.))));
    tones.prepareNormalization();
    auto cutoff = neutral;
    cutoff.modulation[2].enabled = true;
    cutoff.modulation[2].amount = -.5;
    cutoff.modulation[2].attack = cutoff.modulation[2].decay = 0;
    cutoff.modulation[2].sustain = 1;
    bank->prepare(tones, 48000);
    bank->configure(PreparedSamplerSettings(cutoff));
    bank->noteOn(60, 1);
    render(*bank, pcm, 24000);
    require(projection(pcm, 0, 128, 48000) > .035 && projection(pcm, 0, 4000, 48000) < .004,
            "Actual cutoff envelope preserves128Hz and rejects4000Hz in independent literal two-tone PCM");
    {
        std::array<std::unique_ptr<VoiceBank>, 4> banks;
        std::array<juce::AudioBuffer<float>, 4> output;
        auto global = pan;
        global.modulation[1].lfoGlobal = true;
        global.modulation[1].lfoShape = 1;
        global.modulation[1].lfoPeriod = .1;
        for (size_t i = 0; i < banks.size(); ++i)
        {
            banks[i] = std::make_unique<VoiceBank>();
            banks[i]->prepare(sample, 48000);
            banks[i]->configure(PreparedSamplerSettings(global));
            output[i].setSize(2, 2048);
            output[i].clear();
        }
        SamplerVoiceBudget budget(*banks[0], *banks[1], *banks[2], *banks[3]);
        realtime(
            [&]
            {
                budget.beginModulationBlock(0, 1200);
                for (size_t i = 0; i < banks.size(); ++i)
                    banks[i]->render(output[i], 0, 1200);
                budget.endModulationBlock();
            }); // Silent time advances the ONE common Channel clock.
        for (auto &b : banks)
            b->noteOn(60, 1, 777);
        start(*manual, global);
        render(*manual, reference, 1024);
        for (auto &audio : output)
            audio.clear();
        realtime(
            [&]
            {
                budget.beginModulationBlock(0, 1024);
                for (int i = 3; i >= 1; --i)
                    banks[size_t(i)]->render(output[size_t(i)], 0, 1024);
                banks[0]->render(output[0], 0, 73);
                banks[0]->render(output[0], 73, 951);
                budget.endModulationBlock();
            });
        exact = true;
        double gapEffect = 0;
        for (int i = 0; i < 1024; ++i)
            for (int side = 0; side < 2; ++side)
            {
                for (size_t b = 1; b < banks.size(); ++b)
                    exact = exact && output[b].getSample(side, i) == output[0].getSample(side, i);
                gapEffect += std::abs(output[0].getSample(side, i) - reference.getSample(side, i));
            }
        require(exact && gapEffect > .1, "Score/MIDI/typing/preview budget shares one Global phase across "
                                         "reverse bank order, split render and silent gap");
    }
    start(*bank, neutral);
    render(*bank, pcm, 1200);
    double age = 0, phase = 0;
    bank->visitActive(
        [&](const auto &voice) noexcept
        {
            age = voice.modulation.ageSeconds();
            phase = voice.modulation.localPhases()[1];
        });
    const auto old = state(*bank);
    auto live = pan;
    live.modulation[1].lfoTempo = true;
    live.modulation[1].lfoPeriod = 1;
    const PreparedSamplerSettings prepared(live, 90);
    realtime([&] { bank->configure(prepared); });
    bank->visitActive(
        [&](const auto &voice) noexcept
        {
            require(voice.modulation.ageSeconds() == age && voice.modulation.localPhases()[1] == phase,
                    "Live enable/tempo configure retains exact note age and local LFO phase");
        });
    render(*bank, pcm, 128);
    const auto changed = state(*bank);
    require(changed.serial == old.serial && changed.note == old.note &&
                changed.position == old.position + 128,
            "Live enable/tempo changes retain source position and actual trigger identity");
    bank->visitActive(
        [&](const auto &voice) noexcept
        {
            require(std::abs(voice.modulation.localPhases()[1] - (phase + 128 * 1.5 / 48000.)) < 1.e-10,
                    "Tempo changes increment the retained phase at independent90BPM beat rate");
        });
    for (bool patternMode : {true, false})
    {
        auto project = Project::empty();
        const auto channel = project.channels.front().id, pattern = project.patterns.front().id;
        const auto source = project.allocate();
        project.patterns.front().autoLength = false;
        project.patterns.front().length = 4;
        project.patterns.front().notes = {{source, channel, 0, 3, 60, 1.f, true}};
        project.placements = {{project.allocate(), pattern, 0, 0, 4, 0}};
        project.channels.front().sampler = neutral;
        Controller controller;
        controller.replace(project);
        LiveTimeline timeline;
        LiveSequencer sequencer;
        auto score = std::make_unique<VoiceBank>(), midi = std::make_unique<VoiceBank>();
        score->prepare(sample, 48000);
        midi->prepare(sample, 48000);
        midi->configure(PreparedSamplerSettings(neutral));
        midi->noteOn(60, .25f, 0, 0, 0, 93);
        std::array<VoiceBank *, 1> extra{midi.get()};
        std::uint64_t generation = 0;
        const auto publish = [&]
        {
            auto doc = compileLiveDocument(controller.project, patternMode, pattern, ++generation);
            require(timeline.publish(doc) && !doc,
                    "Actual prepared envelope generation publishes before DSP");
        };
        const auto drive = [&](int begin, int count)
        {
            pcm.clear();
            realtime(
                [&]
                {
                    auto lease = timeline.acquire();
                    const auto *lane = lease.document->find(channel);
                    score->configure(lane->sampler);
                    midi->configure(lane->sampler);
                    sequencer.process(*lease.document, channel, begin / 48000., count, 48000, true, *score,
                                      pcm, 0, extra);
                });
        };
        publish();
        drive(0, 1024);
        const auto scoreBefore = state(*score), midiBefore = state(*midi);
        CommandArgs args;
        args.channel = channel;
        args.text = "volume";
        args.enabled = true;
        const juce::String owner = "sampler-envelope:live-original";
        require(controller.beginGesture(owner), "Own live envelope model gesture");
        for (const auto *name : {"enabled", "attack", "decay", "sustain", "amount"})
        {
            args.value = juce::String(name) == "sustain" ? .25 : juce::String(name) == "amount" ? 1 : 0;
            require(
                controller.execute("channel.sampler.modulation." + juce::String(name), args, owner).wasOk(),
                "Apply canonical live envelope parameter");
        }
        require(controller.endGestureChecked("Envelope", owner).wasOk(),
                "Commit one real envelope history action");
        publish();
        drive(1024, 1024);
        const auto scoreEdited = state(*score), midiEdited = state(*midi);
        require(scoreEdited.serial == scoreBefore.serial && midiEdited.serial == midiBefore.serial &&
                    scoreEdited.note == source && midiEdited.live == 93 && scoreEdited.position == 2048 &&
                    midiEdited.position == 2048,
                "Pattern/Song compiled envelope edits preserve sounding score and held MIDI bank "
                "identities/phase");
        require(controller.undoEditChecked().wasOk() &&
                    controller.project.state().isEquivalentTo(project.state()),
                "Actual Undo restores complete original envelope model");
        publish();
        drive(2048, 4096);
        exact = true;
        for (int i = 3840; i < 4096; ++i)
            exact = exact && pcm.getSample(0, i) ==
                                 float(double(wave(2048 + i))) + float(double(wave(2048 + i)) * .25f);
        require(exact && state(*score).serial == scoreBefore.serial &&
                    state(*midi).serial == midiBefore.serial,
                "Undo settles to literal dry summed score/held-input PCM without restarting either owner");
    }
    // Actual one-shot source amplitude and logical key/modulation release are independent.
    // Binary-exact signed constant PCM makes an unintended12ms amplitude truncation decisive.
    Sample drum;
    drum.sampleRate = 48000;
    drum.oneShot = true;
    drum.data.setSize(2, 48001);
    for (int i = 0; i < drum.data.getNumSamples(); ++i)
    {
        drum.data.setSample(0, i, .125f);
        drum.data.setSample(1, i, -.0625f);
    }
    drum.prepareNormalization();
    SamplerSettings oneShot;
    oneShot.filterType = 0; // Isolate source amplitude from the three destination sound changes.
    const auto startDrum = [&](const SamplerSettings &settings)
    {
        bank->prepare(drum, 48000);
        bank->configure(PreparedSamplerSettings(settings));
        bank->noteOn(60, 1, 901, 17);
        render(*bank, pcm, 2048);
    };
    const auto signedDrum = [&](int count)
    {
        bool same = true;
        for (int i = 0; i < count; ++i)
            same = same && pcm.getSample(0, i) == .125f && pcm.getSample(1, i) == -.0625f;
        return same;
    };
    for (size_t target : {size_t(2), size_t(3), size_t(4)})
    {
        auto settings = oneShot;
        auto &m = settings.modulation[target];
        m.enabled = true;
        m.amount = .25;
        m.attack = m.decay = 0;
        m.sustain = 1;
        m.release = .05;
        startDrum(settings);
        const auto beforeOff = state(*bank);
        realtime([&] { bank->noteOff(60); });
        render(*bank, pcm, 1024);
        bool independent = false;
        bank->visitActive(
            [&](const auto &voice) noexcept
            {
                independent = voice.noteReleased && voice.modulation.released() && voice.oneShot &&
                              !voice.releasing && voice.triggerSerial == beforeOff.serial &&
                              voice.note == 901 && voice.placement == 17;
            });
        require(independent && bank->activeVoices() == 1 && signedDrum(1024),
                "Cutoff/resonance/pitch envelope off releases modulation without truncating signed one-shot "
                "amplitude");
        render(*bank, pcm, 4096); // Beyond50ms modulation R and the old12ms amplitude cut.
        require(bank->activeVoices() == 1 && signedDrum(4096),
                "Finished nonvolume release preserves the original one-shot source instead of retiring it");
        realtime([&] { bank->releaseTagged(901, 17, true); });
        render(*bank, pcm, 1024);
        require(bank->activeVoices() == 0 && pcm.getMagnitude(0, 896, 128) == 0,
                "Forced tagged delete still releases an already-key-released one-shot source");
    }
    startDrum(oneShot);
    const auto lateTrigger = state(*bank).serial;
    realtime([&] { bank->releaseTagged(901, 17); });
    render(*bank, pcm, 4096);
    bool remembered = false;
    bank->visitActive(
        [&](const auto &voice) noexcept
        {
            remembered = voice.noteReleased && voice.modulation.released() && !voice.releasing &&
                         voice.triggerSerial == lateTrigger;
        });
    require(remembered && bank->activeVoices() == 1 && signedDrum(4096),
            "Neutral one-shot remembers actual tagged off while retaining its original sound");
    auto lateVolume = oneShot;
    auto &late = lateVolume.modulation[0];
    late.enabled = true;
    late.amount = 1;
    late.attack = late.decay = 0;
    late.sustain = 1;
    late.release = .05;
    const PreparedSamplerSettings latePrepared(lateVolume);
    realtime([&] { bank->configure(latePrepared); });
    require(bank->activeVoices() == 1,
            "Late Volume enable retains the existing source owner before its release span");
    render(*bank, pcm, 512);
    require(bank->activeVoices() == 0 && pcm.getMagnitude(0, 384, 128) == 0,
            "Late Volume enable honors earlier off and elapsed R instead of inventing a newly held gate");

    bank->prepare(drum, 48000);
    bank->configure(PreparedSamplerSettings(oneShot));
    bank->noteOn(60, .5f, 911);
    bank->noteOn(60, .5f, 912);
    render(*bank, pcm, 1024);
    realtime([&] { bank->noteOff(60); });
    bool firstReleased = false, secondHeld = false;
    bank->visitActive(
        [&](const auto &voice) noexcept
        {
            if (voice.note == 911)
                firstReleased = voice.noteReleased && voice.modulation.released() && !voice.releasing;
            if (voice.note == 912)
                secondHeld = !voice.noteReleased && !voice.modulation.released() && !voice.releasing;
        });
    require(firstReleased && secondHeld && bank->activeVoices() == 2,
            "First same-pitch off records exactly one one-shot key without stopping either source");
    realtime([&] { bank->noteOff(60); });
    int releasedKeys = 0;
    bank->visitActive(
        [&](const auto &voice) noexcept
        {
            if (voice.noteReleased && voice.modulation.released() && !voice.releasing)
                ++releasedKeys;
        });
    require(releasedKeys == 2,
            "Second same-pitch off skips prior released tail and reaches the other held key");
    realtime([&] { bank->noteOff(60); }); // Surplus off cannot reset either captured release.
    render(*bank, pcm, 4096);
    realtime([&] { bank->configure(latePrepared); });
    render(*bank, pcm, 512);
    require(bank->activeVoices() == 0,
            "Both actual same-pitch key-offs remain authoritative on later Volume enable");

    startDrum(oneShot);
    realtime([&] { bank->releaseConfigured(); });
    render(*bank, pcm, 1024);
    require(bank->activeVoices() == 0, "Configured cut still overrides ordinary one-shot key-off immunity");
    auto looping = oneShot;
    looping.loop = true;
    startDrum(looping);
    realtime([&] { bank->noteOff(60); });
    render(*bank, pcm, 1024);
    require(bank->activeVoices() == 0, "Captured loop still obeys ordinary key-off and amplitude release");
    auto longVolume = lateVolume;
    longVolume.modulation[0].release = 64;
    startDrum(longVolume);
    realtime([&] { bank->releaseAllQuickly(); });
    render(*bank, pcm, 1024);
    require(bank->activeVoices() == 0,
            "Explicit quick panic overrides long custom Volume on a one-shot source");
    auto sourceBound = oneShot;
    sourceBound.modulation[2].enabled = true;
    sourceBound.modulation[2].amount = .25;
    sourceBound.modulation[2].release = 64;
    startDrum(sourceBound);
    realtime([&] { bank->noteOff(60); });
    for (int block = 0; block < 12; ++block)
        render(*bank, pcm, 4096);
    require(bank->activeVoices() == 0,
            "Long nonvolume release never extends the finite original one-shot source boundary");
}
