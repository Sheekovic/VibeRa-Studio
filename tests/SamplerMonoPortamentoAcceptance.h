// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/LiveTimeline.h"
#include "../src/MidiInput.h"
#include <array>
#include <memory>

// Original direct runtime/PCM oracles. Include after require and callback probes.
// Does not establish FL retrigger/phase parity or physical controller behavior.
namespace sampler_mono_portamento_acceptance
{
using namespace vibera;
inline Sample stimulus(int rate, bool constant = true)
{
    Sample sample;
    sample.sampleRate = rate;
    sample.rootNote = 60;
    sample.oneShot = false;
    sample.data.setSize(2, rate * 2 + 2);
    for (int frame = 0; frame < sample.data.getNumSamples(); ++frame)
    {
        const float value = constant ? .25f : float(double(frame) / (8. * rate));
        sample.data.setSample(0, frame, value);
        sample.data.setSample(1, frame, -.5f * value);
    }
    return sample;
}
inline SamplerSettings settings()
{
    SamplerSettings result;
    result.envelope = true;
    result.attack = result.decay = 0;
    result.sustain = 1;
    result.release = .05;
    result.filterType = 0;
    return result;
}
template <class F> inline void callback(F &&fn)
{
    const auto allocations = callbackAllocations.load(), destructions = callbackDestructions.load();
    const auto previous = callbackAllocationProbe;
    callbackAllocationProbe = true;
    fn();
    callbackAllocationProbe = previous;
    require(callbackAllocations.load() == allocations && callbackDestructions.load() == destructions,
            "Mono/Porta/Gate admission, configuration and rendering allocate/destroy no C++ objects");
}
struct Banks
{
    std::array<VoiceBank, 4> bank;
    SamplerVoiceBudget budget{bank[0], bank[1], bank[2], bank[3]};
    Banks(const Sample &sample, int rate, const SamplerSettings &value)
    {
        for (auto &voice : bank)
            voice.prepare(sample, rate);
        budget.resetTransitions();
        configure(value);
    }
    void configure(const PreparedSamplerSettings &value) noexcept
    {
        for (auto &voice : bank)
            voice.configure(value);
        budget.configure(value.values.maxPoly);
    }
    void configure(const SamplerSettings &value, double tempo = 120)
    {
        configure(PreparedSamplerSettings(value, tempo)); // CONTROL preparation only.
    }
    int active() const noexcept
    {
        int result = 0;
        for (const auto &voice : bank)
            result += voice.activeVoices();
        return result;
    }
    void render(juce::AudioBuffer<float> &pcm, int frames) noexcept
    {
        pcm.clear();
        for (auto &voice : bank)
            voice.render(pcm, 0, frames);
    }
};
struct Snapshot
{
    bool found = false, noteReleased = false;
    std::int64_t note = 0, placement = 0;
    std::uint32_t repeat = 0;
    std::uint64_t serial = 0, sequence = 0, glideFrames = 0, elapsed = 0;
    double delta = 0, position = 0, cutoff = 0, resonance = 0;
    float left = 0, right = 0;
};
inline Snapshot snapshot(VoiceBank &bank, std::int64_t note)
{
    Snapshot result;
    bank.visitActive(
        [&](const auto &voice) noexcept
        {
            if (voice.note != note)
                return;
            result = {true,
                      voice.noteReleased,
                      voice.note,
                      voice.placement,
                      voice.repeatOrdinal,
                      voice.triggerSerial,
                      voice.liveSequence,
                      voice.glide.frames,
                      voice.glide.elapsed,
                      std::abs(voice.delta),
                      voice.position,
                      voice.glide.frames ? voice.glide.current.cutoff : voice.expression.filterCutoffOffset,
                      voice.glide.frames ? voice.glide.current.resonance
                                         : voice.expression.filterResonanceOffset,
                      voice.leftGain,
                      voice.rightGain};
        });
    return result;
}
inline void tests()
{
    for (int rate : {44100, 48000, 96000})
    {
        auto sample = stimulus(rate);
        auto value = settings();
        const int glideFrames = int(std::floor(.01 * rate + .5));
        juce::AudioBuffer<float> pcm(2, rate / 4 + 16);
        auto neutral = std::make_unique<Banks>(sample, rate, value);
        callback(
            [&]() noexcept
            {
                neutral->bank[0].noteOn(60, .5f, 101);
                neutral->render(pcm, 512);
            });
        bool literal = true;
        for (int frame = 0; frame < 512; ++frame)
            literal = literal && pcm.getSample(0, frame) == .125f && pcm.getSample(1, frame) == -.0625f;
        require(literal, "All five neutral defaults retain literal signed stereo source PCM");
        value.maxPoly = 1;
        neutral->configure(value);
        callback([&]() noexcept { neutral->bank[3].noteOn(72, .5f, 102); });
        require(neutral->active() == 1 && neutral->bank[3].activeVoices() == 1,
                "Off modes preserve immediate shared MaxPoly1 stealing");

        value.mono = true;
        value.slideTime = .01;
        for (int previous = 0; previous < 4; ++previous)
            for (int incoming = 0; incoming < 4; ++incoming)
            {
                auto voices = std::make_unique<Banks>(sample, rate, value);
                callback(
                    [&]() noexcept
                    {
                        voices->bank[size_t(previous)].noteOn(60, .5f, 1001, 81, .125, 11, {}, 3);
                        voices->render(pcm, 96);
                        voices->bank[size_t(incoming)].noteOn(72, .75f, 1002, 82, .25, 12, {}, 4);
                    });
                const auto began = snapshot(voices->bank[size_t(incoming)], 1002);
                require(voices->active() == 2 && began.found && began.delta == 1 &&
                            began.glideFrames == std::uint64_t(glideFrames) && began.position == 0,
                        "Every ordered bank pair seeds Mono from held foreground and retains old tail "
                        "despite MaxPoly1");
                const PreparedSamplerSettings retained(value);
                callback(
                    [&]() noexcept
                    {
                        voices->configure(retained);
                        voices->bank[size_t(previous)].releaseTagged(1001, 81, false, 3);
                        voices->render(pcm, glideFrames);
                    });
                const auto ended = snapshot(voices->bank[size_t(incoming)], 1002);
                require(ended.found && ended.delta == 2 && ended.glideFrames == 0 &&
                            ended.serial == began.serial && ended.note == 1002 && ended.placement == 82 &&
                            ended.repeat == 4 && ended.position > 0,
                        "Finite Mono glide ends exactly without config restart or old-owner off retargeting "
                        "its identity");
            }

        // Mono entry must release all old polyphonic foregrounds, without returning
        // to an older key when the newest owner ends.
        value.mono = false;
        value.maxPoly = 0;
        auto entry = std::make_unique<Banks>(sample, rate, value);
        entry->bank[0].noteOn(60, .5f, 1);
        entry->bank[1].noteOn(64, .5f, 2);
        value.mono = true;
        value.maxPoly = 1;
        const PreparedSamplerSettings monoEntry(value);
        callback(
            [&]() noexcept
            {
                entry->configure(monoEntry);
                entry->bank[2].noteOn(72, .5f, 3);
            });
        require(entry->active() == 3 && !snapshot(entry->bank[0], 1).found &&
                    !snapshot(entry->bank[1], 2).found && snapshot(entry->bank[2], 3).found,
                "Mono admission releases every logically held polyphonic predecessor into captured tails");
        entry->bank[2].releaseTagged(3, 0);
        callback([&]() noexcept { entry->render(pcm, rate / 8); });
        require(entry->active() == 0, "Ending the last Mono key never resurrects older held ownership");

        value.mono = false;
        value.portamento = true;
        value.release = 0;
        auto gaps = std::make_unique<Banks>(sample, rate, value);
        gaps->bank[0].noteOn(60, .5f, 11);
        gaps->bank[0].releaseTagged(11, 0);
        gaps->render(pcm, 2);
        gaps->bank[0].reset(); // Score-only Stop must not clear Channel scalar gap memory.
        callback([&]() noexcept { gaps->bank[3].noteOn(72, .5f, 12); });
        require(gaps->active() == 1 && snapshot(gaps->bank[3], 12).delta == 1,
                "Porta gaps seed last admitted scalar target across score-only reset, without a stale Voice "
                "pointer");
        gaps->render(pcm, glideFrames / 2);
        const auto mid = snapshot(gaps->bank[3], 12);
        callback([&]() noexcept { gaps->bank[2].noteOn(84, .5f, 13); });
        require(snapshot(gaps->bank[2], 13).delta == mid.delta,
                "A new overlapping Porta key seeds current audible rate rather than the old final target");

        auto tailValue = value;
        tailValue.release = .05;
        auto tails = std::make_unique<Banks>(sample, rate, tailValue);
        tails->bank[0].noteOn(60, .5f, 15);
        tails->bank[1].noteOn(72, .5f, 16);
        tails->render(pcm, glideFrames / 2);
        const auto audibleTail = snapshot(tails->bank[1], 16);
        callback(
            [&]() noexcept
            {
                tails->bank[1].releaseTagged(16, 0);
                tails->bank[2].noteOn(84, .5f, 17);
            });
        require(audibleTail.delta > 1 && audibleTail.delta < 2 &&
                    snapshot(tails->bank[2], 17).delta == audibleTail.delta,
                "Porta uses a still-audible released foreground rate before falling back to the last target");
        for (auto &voice : gaps->bank)
            voice.reset();
        gaps->budget.resetTransitions();
        gaps->bank[1].noteOn(84, .5f, 14);
        require(snapshot(gaps->bank[1], 14).delta == 4 && snapshot(gaps->bank[1], 14).glideFrames == 0,
                "Real transition reset removes scalar memory and the next first key starts at its own pitch");

        value.portamento = false;
        value.mono = true;
        value.gateBeats = .01;
        const int gateFrame = int(std::floor(.005 * rate + .5));
        for (int source = 0; source < 4; ++source)
        {
            auto gates = std::make_unique<Banks>(sample, rate, value);
            callback(
                [&]() noexcept
                {
                    gates->bank[size_t(source)].noteOn(60, .5f, 21);
                    gates->render(pcm, gateFrame + 1);
                });
            bool exact = pcm.getSample(0, gateFrame) == 0;
            for (int frame = 0; frame < gateFrame; ++frame)
                exact = exact && pcm.getSample(0, frame) == .125f;
            require(exact && gates->active() == 0, "Every bank Gate sends literal nearest-frame logical "
                                                   "key-off, including half-frame ties upward");
        }
        for (bool porta : {false, true})
            for (bool full : {false, true})
            {
                value.portamento = porta;
                value.fullPortamento = full;
                auto bypass = std::make_unique<Banks>(sample, rate, value);
                bypass->bank[3].noteOn(60, .5f, 31);
                bypass->render(pcm, gateFrame + 1);
                require(
                    (bypass->active() == 1) == (porta && full),
                    "Full porta bypasses Gate only for incoming effective Porta, including the first note");
            }

        value.portamento = value.fullPortamento = false;
        auto oneShot = stimulus(rate);
        oneShot.oneShot = true;
        value.envelope = false;
        auto natural = std::make_unique<Banks>(oneShot, rate, value);
        natural->bank[0].noteOn(60, .5f, 41);
        natural->render(pcm, gateFrame); // Exactly the pending Gate boundary, before another note-on.
        callback([&]() noexcept { natural->bank[1].noteOn(72, .5f, 42); });
        const auto previous = snapshot(natural->bank[0], 41), next = snapshot(natural->bank[1], 42);
        require(previous.found && previous.noteReleased && next.delta == 2 && next.glideFrames == 0,
                "Gate expiry precedes same-boundary admission; released one-shot PCM is not logical Mono "
                "overlap");
        natural->render(pcm, gateFrame + 8);
        require(
            natural->bank[0].activeVoices() == 1,
            "Natural Gate retains envelope-disabled one-shot source tail without an invented forced fade");
        value = settings();
    }

    // Closed-form source-ramp oracle for a1->2 playback-rate transition. The
    // first interval uses1+1/N; after N intervals the base is exactly2.
    constexpr int rate = 48000, frames = 480;
    auto ramp = stimulus(rate, false);
    auto value = settings();
    value.mono = true;
    value.slideTime = .01;
    auto sweep = std::make_unique<Banks>(ramp, rate, value);
    juce::AudioBuffer<float> pcm(2, 2048);
    pcm.clear();
    sweep->bank[0].noteOn(60, .5f, 51);
    sweep->bank[1].noteOn(72, .5f, 52);
    callback([&]() noexcept { sweep->bank[1].render(pcm, 0, frames); });
    double largestError = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        const double position = frame + double(frame) * (frame + 1) / (2. * frames);
        const double expected = position / (8. * rate) * .5;
        largestError = std::max(largestError, std::abs(pcm.getSample(0, frame) - expected));
    }
    require(largestError < 1.e-8 && snapshot(sweep->bank[1], 52).delta == 2,
            "Original nonperiodic source PCM follows independently derived finite linear-rate law and fresh "
            "source onset");

    for (double duration : {0., .5 / rate})
    {
        value.slideTime = duration;
        auto shortSlide = std::make_unique<Banks>(ramp, rate, value);
        shortSlide->bank[0].noteOn(60, .5f, 55);
        shortSlide->bank[1].noteOn(72, .5f, 56);
        if (duration == 0)
            require(snapshot(shortSlide->bank[1], 56).delta == 2 &&
                        snapshot(shortSlide->bank[1], 56).glideFrames == 0,
                    "Zero SLIDE is an immediate target with a retained predecessor tail");
        pcm.clear();
        callback([&]() noexcept { shortSlide->bank[1].render(pcm, 0, 1); });
        require(snapshot(shortSlide->bank[1], 56).delta == 2 &&
                    snapshot(shortSlide->bank[1], 56).glideFrames == 0,
                "Positive half-frame SLIDE rounds upward to one finite interval and cannot stick");
    }

    value.slideTime = .01;
    value.reverse = true;
    auto reverse = std::make_unique<Banks>(ramp, rate, value);
    reverse->bank[0].noteOn(60, .5f, 57);
    reverse->bank[1].noteOn(72, .5f, 58);
    pcm.clear();
    callback([&]() noexcept { reverse->bank[1].render(pcm, 0, frames); });
    bool negative = false;
    reverse->bank[1].visitActive(
        [&](const auto &voice) noexcept
        {
            if (voice.note == 58)
                negative = voice.delta == -2 && voice.direction == -1;
        });
    require(negative &&
                std::abs(snapshot(reverse->bank[1], 58).position - (2. * rate - (1.5 * frames + .5))) < 1.e-8,
            "Finite playback-rate magnitude glide preserves incoming reverse direction and literal source "
            "travel");
    value.reverse = false;

    auto constant = stimulus(rate);
    value.slideTime = .02;
    auto properties = std::make_unique<Banks>(constant, rate, value);
    properties->bank[0].noteOn(60, .5f, 61, 1, 0, 0, PreparedNoteExpression::prepare(-1, 0, .25, .75));
    properties->bank[1].noteOn(72, .5f, 62, 2, .125, 0, PreparedNoteExpression::prepare(1, 0, .75, .25));
    pcm.clear();
    callback([&]() noexcept { properties->bank[1].render(pcm, 0, 480); });
    const auto center = snapshot(properties->bank[1], 62);
    require(center.left == 1 && center.right == 1 && center.delta == 1.5 && center.cutoff == 0 &&
                center.resonance == 0 && pcm.getSample(0, 479) == .125f && pcm.getSample(1, 479) == -.0625f,
            "Signed pan glide reaches undimmed neutral stereo while Mod X/Y bases cross their literal "
            "neutral midpoint");
    value.filterCutoff = .5;
    value.slideTime = 1.; // An edit affects future transitions, not this captured960-frame duration.
    const PreparedSamplerSettings filterEdit(value);
    const auto targetExpression = PreparedNoteExpression::prepare(-1, 100, .25, .75);
    pcm.clear();
    callback(
        [&]() noexcept
        {
            properties->configure(filterEdit);
            properties->bank[1].visitActive(
                [&](auto &voice) noexcept
                {
                    if (voice.note == 62)
                        properties->bank[1].retargetExpression(voice, targetExpression);
                });
            properties->bank[1].render(pcm, 0, 1);
        });
    const auto edited = snapshot(properties->bank[1], 62);
    const double editedRate = 2. * std::pow(2., 100. / 1200.);
    const double firstRate = 1.5 + (editedRate - 1.5) / 480.;
    require(edited.elapsed == 481 && edited.glideFrames == 960 &&
                std::abs(edited.delta - firstRate) < 1.e-12 && edited.left == 1 &&
                edited.right == float(1. - 1. / 480.) && std::abs(edited.cutoff + .5 / 480.) < 1.e-12 &&
                std::abs(edited.resonance - .5 / 480.) < 1.e-12 &&
                std::abs(edited.position - center.position - firstRate) < 1.e-9 &&
                pcm.getSample(0, 0) == .125f && pcm.getSample(1, 0) == float(-.0625 * float(1. - 1. / 480.)),
            "Changed rate/pan/Mod endpoints move one bounded remaining-frame increment from audible bases, "
            "with literal first-frame stereo PCM and retained source position");
    callback(
        [&]() noexcept
        {
            properties->configure(filterEdit); // Identical endpoint publication cannot rebase again.
            properties->bank[1].render(pcm, 1, 479);
        });
    const auto final = snapshot(properties->bank[1], 62);
    require(final.glideFrames == 0 && final.elapsed == 960 && final.delta == editedRate &&
                final.serial == center.serial && final.placement == 2 && final.position > center.position &&
                final.left == 1 && final.right == 0 && final.cutoff == -.5 && final.resonance == .5,
            "Changed endpoints reach their exact target at the original960-frame deadline; unchanged "
            "configuration and edited Slide duration retain source/occurrence identity");

    // Independent manual-filter control oracle: binary fractions avoid ambiguous
    // coefficient interpolation differences while testing actual stereo PCM.
    value = settings();
    value.mono = true;
    value.slideTime = 128. / rate;
    value.filterType = 1;
    value.filterCutoff = .5;
    value.filterResonance = .25;
    auto filtered = std::make_unique<Banks>(constant, rate, value);
    filtered->bank[0].noteOn(60, .5f, 65, 0, 0, 0, PreparedNoteExpression::prepare(0, 0, .25, .5));
    filtered->bank[1].noteOn(60, .5f, 66, 0, 0, 0, PreparedNoteExpression::prepare(0, 0, .75, .625));
    auto reference = std::make_unique<VoiceBank>();
    reference->prepare(constant, rate);
    auto manual = settings();
    manual.filterType = 1;
    manual.filterCutoff = 0;
    manual.filterResonance = .25;
    reference->configure(PreparedSamplerSettings(manual));
    reference->noteOn(60, .5f, 67);
    std::vector<PreparedSamplerSettings> targets;
    targets.reserve(128);
    for (int frame = 0; frame < 128; ++frame)
    {
        manual.filterCutoff = (frame + 1) / 128.;
        manual.filterResonance = .25 + (frame + 1) / 512.;
        targets.emplace_back(manual); // Tables/pow only on CONTROL, outside probe.
    }
    juce::AudioBuffer<float> expected(2, 128);
    expected.clear();
    pcm.clear();
    callback(
        [&]() noexcept
        {
            for (int frame = 0; frame < 128; ++frame)
            {
                filtered->bank[1].render(pcm, frame, 1);
                reference->configure(targets[size_t(frame)]);
                reference->render(expected, frame, 1);
            }
        });
    bool exactFilter = true;
    for (int side = 0; side < 2; ++side)
        for (int frame = 0; frame < 128; ++frame)
            exactFilter = exactFilter && pcm.getSample(side, frame) == expected.getSample(side, frame);
    require(
        exactFilter && expected.getMagnitude(0, 0, 128) > .001f,
        "Actual per-voice Mod X/Y glide PCM equals independently authored manual cutoff/resonance targets");

    // Current tempo consumes remaining musical Gate duration, not a restarted gate.
    value = settings();
    value.release = 0;
    value.gateBeats = .02;
    auto tempo = std::make_unique<Banks>(constant, rate, value);
    tempo->bank[2].noteOn(60, .5f, 71);
    tempo->render(pcm, 240);
    const PreparedSamplerSettings slower(value, 60);
    callback(
        [&]() noexcept
        {
            tempo->configure(slower);
            tempo->render(pcm, 481);
        });
    require(pcm.getSample(0, 479) == .125f && pcm.getSample(0, 480) == 0 && tempo->active() == 0,
            "Gate beat age survives tempo change:240 frames at120 then480 at60 before literal key-off");

    auto captured = std::make_unique<Banks>(constant, rate, value);
    captured->bank[0].noteOn(60, .5f, 73);
    captured->render(pcm, 240);
    value.portamento = value.fullPortamento = true;
    value.gateBeats = 0;
    const PreparedSamplerSettings futureOnly(value);
    callback(
        [&]() noexcept
        {
            captured->configure(futureOnly);
            captured->render(pcm, 241);
        });
    require(pcm.getSample(0, 239) == .125f && pcm.getSample(0, 240) == 0 && captured->active() == 0,
            "Gate amount and FullPorta exemption are captured at admission; ordinary edits affect new keys");

    auto attack = settings();
    attack.mono = true;
    attack.attack = .01;
    auto retrigger = std::make_unique<Banks>(constant, rate, attack);
    retrigger->bank[0].noteOn(60, .5f, 81);
    retrigger->render(pcm, 960);
    retrigger->bank[1].noteOn(72, .5f, 82);
    pcm.clear();
    retrigger->bank[1].render(pcm, 0, 1);
    require(pcm.getSample(0, 0) == float(.125 * float(1. / 480.)),
            "Original Mono policy starts a fresh incoming amplitude attack independently of the old "
            "sustained voice");
    pcm.clear();
    retrigger->bank[0].render(pcm, 0, 1);
    require(pcm.getSample(0, 0) > .1f,
            "Predecessor keeps a measured captured release tail after fresh Mono attack");

    // Capacity remains global and bounded when user Max poly is ignored in Mono.
    auto capped = std::make_unique<Banks>(constant, rate, attack);
    attack.maxPoly = 1;
    capped->configure(attack);
    callback(
        [&]() noexcept
        {
            for (int note = 0; note < 160; ++note)
                capped->bank[size_t(note % 4)].noteOn(60, .5f, 10000 + note);
        });
    require(capped->active() == int(VoiceBank::capacity),
            "Mono retains a real global128-tail safety ceiling");

    // Exercise the existing physical-lane parser/held/sustain path, not only
    // direct noteOn calls. Its original source tags remain authoritative.
    value = settings();
    value.mono = true;
    auto input = std::make_unique<Banks>(constant, rate, value);
    MidiMonitor monitor;
    monitor.count = 1;
    monitor.events[0] = {0, {0x90, 60, 100}, 3};
    monitor.sequences[0] = 101;
    callback([&]() noexcept { monitor.apply(input->bank[1]); });
    input->render(pcm, 64);
    monitor.events[0] = {0, {0x90, 72, 110}, 3};
    monitor.sequences[0] = 102;
    callback([&]() noexcept { monitor.apply(input->bank[1]); });
    constexpr std::int64_t currentTag = -73; // MIDI channel0, pitch72.
    const auto physical = snapshot(input->bank[1], currentTag);
    monitor.events[0] = {0, {0x80, 60, 64}, 3};
    callback([&]() noexcept { monitor.apply(input->bank[1]); });
    require(physical.found && physical.sequence == 102 &&
                snapshot(input->bank[1], currentTag).sequence == physical.sequence &&
                snapshot(input->bank[1], currentTag).serial == physical.serial &&
                !snapshot(input->bank[1], currentTag).noteReleased,
            "Physical old-key off cannot release the newer Mono owner or rewrite its live sequence");
    monitor.events[0] = {0, {0xb0, 64, 127}, 3};
    monitor.apply(input->bank[1]);
    monitor.events[0] = {0, {0x80, 72, 127}, 3};
    monitor.apply(input->bank[1]);
    require(!snapshot(input->bank[1], currentTag).noteReleased,
            "Physical sustain-deferred ownership stays logically held until pedal-up");
    monitor.events[0] = {0, {0xb0, 64, 0}, 3};
    callback([&]() noexcept { monitor.apply(input->bank[1]); });
    require(!snapshot(input->bank[1], currentTag).found,
            "Pedal-up releases its exact Mono owner through the existing monitor path");

    auto equalPitch = std::make_unique<Banks>(constant, rate, value);
    equalPitch->bank[0].noteOn(60, .5f, 201, 4, .25, 0, {}, 1);
    equalPitch->bank[3].noteOn(60, .5f, 202, 5, .5, 0, {}, 2);
    callback([&]() noexcept { equalPitch->bank[0].releaseTagged(201, 4, false, 1); });
    require(
        snapshot(equalPitch->bank[3], 202).found && !snapshot(equalPitch->bank[3], 202).noteReleased &&
            equalPitch->active() == 2,
        "Equal-pitch Mono owners retain separate source/placement/repeat identity and independent key-off");

    auto standalone = std::make_unique<VoiceBank>();
    standalone->prepare(constant, rate);
    value.maxPoly = 1;
    standalone->configure(PreparedSamplerSettings(value));
    callback(
        [&]() noexcept
        {
            standalone->noteOn(60, .5f, 91);
            standalone->noteOn(72, .5f, 92);
        });
    require(standalone->activeVoices() == 2 && snapshot(*standalone, 92).delta == 1,
            "Standalone bank uses the same Mono release/transition policy rather than a hidden MaxPoly1 "
            "substitute");

    {
        auto editedProject = Project::empty();
        editedProject.tempo = 120;
        const auto editedChannel = editedProject.channels.front().id;
        auto &editedPattern = editedProject.patterns.front();
        auto modes = settings();
        modes.mono = true;
        modes.slideTime = .02;
        editedProject.channels.front().sampler = modes;
        Note oldKey{editedProject.allocate(), editedChannel, 0, 2, 60, .5f, false};
        oldKey.pan = -1;
        oldKey.modX = .25;
        oldKey.modY = .75;
        Note newKey{editedProject.allocate(), editedChannel, .01, 2, 72, .5f, false};
        newKey.pan = 1;
        newKey.modX = .75;
        newKey.modY = .25;
        editedPattern.notes = {oldKey, newKey};
        auto original = compileLiveDocument(editedProject, true, editedPattern.id, 201);
        editedPattern.notes.back().pan = -1;
        editedPattern.notes.back().fineCents = 100;
        editedPattern.notes.back().modX = .25;
        editedPattern.notes.back().modY = .75;
        auto changed = compileLiveDocument(editedProject, true, editedPattern.id, 202);
        require(original && changed, "Real score expression edit compiles distinct immutable generations");
        auto reconcile = std::make_unique<Banks>(constant, rate, modes);
        LiveSequencer playback;
        const std::array<VoiceBank *, 3> siblings{&reconcile->bank[1], &reconcile->bank[2],
                                                  &reconcile->bank[3]};
        pcm.clear();
        callback(
            [&]() noexcept
            {
                playback.process(*original, editedChannel, 0, 720, rate, true, reconcile->bank[0], pcm, 0,
                                 siblings);
            });
        const auto beforeEdit = snapshot(reconcile->bank[0], newKey.id);
        require(beforeEdit.found && beforeEdit.elapsed == 480 && beforeEdit.delta == 1.5 &&
                    beforeEdit.left == 1 && beforeEdit.right == 1,
                "Score-triggered Mono transition reaches literal halfway before the generation edit");
        pcm.clear();
        callback(
            [&]() noexcept
            {
                playback.process(*changed, editedChannel, 720. / rate, 1, rate, true, reconcile->bank[0], pcm,
                                 0, siblings);
            });
        const auto afterEdit = snapshot(reconcile->bank[0], newKey.id);
        require(afterEdit.serial == beforeEdit.serial && afterEdit.elapsed == 481 &&
                    afterEdit.glideFrames == 960 && std::abs(afterEdit.delta - firstRate) < 1.e-12 &&
                    afterEdit.left == 1 && afterEdit.right == float(1. - 1. / 480.) &&
                    std::abs(afterEdit.cutoff + .5 / 480.) < 1.e-12 &&
                    std::abs(afterEdit.resonance - .5 / 480.) < 1.e-12 &&
                    std::abs(afterEdit.position - beforeEdit.position - firstRate) < 1.e-9,
                "Actual LiveSequencer generation reconciliation rebases all changed expression targets "
                "without replacing ownership or jumping the source phase");
        callback(
            [&]() noexcept
            {
                playback.process(*changed, editedChannel, 721. / rate, 479, rate, true, reconcile->bank[0],
                                 pcm, 0, siblings);
            });
        const auto completed = snapshot(reconcile->bank[0], newKey.id);
        require(completed.elapsed == 960 && completed.glideFrames == 0 && completed.delta == editedRate &&
                    completed.left == 1 && completed.right == 0 && completed.cutoff == -.5 &&
                    completed.resonance == .5 && completed.serial == beforeEdit.serial &&
                    editedPattern.notes.back().beat == .01 && editedPattern.notes.back().length == 2,
                "Edited score reaches exact new expression endpoints at its original deadline with raw "
                "source geometry and identity intact");
    }

    auto project = Project::empty();
    auto &pattern = project.patterns.front();
    const auto channel = project.channels.front().id;
    value.gateBeats = .02;
    value.release = 0;
    project.channels.front().sampler = value;
    pattern.notes = {{project.allocate(), channel, 0, 3, 60, .5f, false}};
    auto first = compileLiveDocument(project, true, pattern.id, 101);
    auto second = compileLiveDocument(project, true, pattern.id, 102);
    require(first && second, "Gate source fixture compiles two real immutable timeline generations");
    auto publication = std::make_unique<Banks>(constant, rate, value);
    LiveSequencer sequencer;
    const std::array<VoiceBank *, 3> extra{&publication->bank[1], &publication->bank[2],
                                           &publication->bank[3]};
    pcm.clear();
    callback(
        [&]() noexcept
        {
            sequencer.process(*first, channel, 0, 1200, rate, true, publication->bank[0], pcm, 0, extra);
            pcm.clear();
            sequencer.process(*second, channel, 1200. / rate, 512, rate, true, publication->bank[0], pcm, 0,
                              extra);
        });
    require(publication->active() == 0 && pcm.getMagnitude(0, 0, 512) == 0 &&
                project.patterns.front().notes.front().length == 3,
            "Gate expiry keeps raw source and sequencer ownership; generation publication cannot "
            "chase-resurrect it");
}
} // namespace sampler_mono_portamento_acceptance

inline void samplerMonoPortamentoTests()
{
    sampler_mono_portamento_acceptance::tests();
}
