// SPDX-License-Identifier: AGPL-3.0-or-later
#include "synth/SynthEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <numbers>
#include <stdexcept>

namespace synth_test
{
thread_local bool callback = false;
thread_local unsigned allocations = 0, destructions = 0;
struct CallbackScope
{
    CallbackScope()
    {
        callback = true;
    }
    ~CallbackScope()
    {
        callback = false;
    }
};
} // namespace synth_test
void *operator new(std::size_t size)
{
    if (synth_test::callback)
        ++synth_test::allocations;
    if (auto *memory = std::malloc(std::max<std::size_t>(size, 1)))
        return memory;
    throw std::bad_alloc();
}
void *operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete(void *memory) noexcept
{
    if (memory && synth_test::callback)
        ++synth_test::destructions;
    std::free(memory);
}
void operator delete[](void *memory) noexcept
{
    ::operator delete(memory);
}
void operator delete(void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}
void operator delete[](void *memory, std::size_t) noexcept
{
    ::operator delete(memory);
}

using namespace vibera::synth;
namespace
{
unsigned checks = 0;
void require(bool pass, const char *message)
{
    ++checks;
    if (!pass)
        throw std::runtime_error(message);
}
Parameters plain()
{
    Parameters p;
    p.waveform1 = p.waveform2 = Waveform::sine;
    p.oscillator1Level = 1;
    p.oscillator2Level = 0;
    p.attackMs = p.decayMs = 0;
    p.sustain = 1;
    p.releaseMs = 10;
    p.filterType = FilterType::bypass;
    p.masterDb = 0;
    p.detuneCents = 0;
    p.vibratoDepthCents = 0;
    return p;
}
struct Audio
{
    std::vector<float> left, right;
    explicit Audio(int frames) : left(std::size_t(frames)), right(std::size_t(frames)) {}
};
Audio render(Parameters parameters, int frames, std::span<const MidiEvent> events, int partition = 256,
             const PreparedSampleMap *map = nullptr, double rate = 48000)
{
    Engine engine;
    engine.prepare(rate);
    engine.bindSampleMap(map);
    require(engine.setParameters(parameters), "Valid test controls refused");
    Audio audio(frames);
    std::array<MidiEvent, 8192> blockEvents{};
    std::size_t next = 0;
    for (int position = 0; position < frames;)
    {
        const auto count = std::min(partition, frames - position);
        std::size_t used = 0;
        while (next < events.size() && events[next].sampleOffset < position + count)
        {
            blockEvents[used] = events[next++];
            blockEvents[used++].sampleOffset -= position;
        }
        bool accepted = false;
        {
            synth_test::CallbackScope guard;
            accepted = engine.process(audio.left.data() + position, audio.right.data() + position, count,
                                      std::span<const MidiEvent>(blockEvents.data(), used));
        }
        require(accepted, "Valid MIDI block refused");
        position += count;
    }
    return audio;
}
double energy(const std::vector<float> &audio, std::size_t begin, std::size_t end)
{
    double result = 0;
    for (auto i = begin; i < end; ++i)
        result += double(audio[i]) * audio[i];
    return result / double(std::max<std::size_t>(end - begin, 1));
}
double frequency(const std::vector<float> &audio, std::size_t begin, std::size_t end, double rate)
{
    double first = -1, last = -1;
    int crossings = 0;
    for (auto i = begin + 1; i < end; ++i)
        if (audio[i - 1] < 0 && audio[i] >= 0)
        {
            const auto crossing = double(i - 1) - audio[i - 1] / double(audio[i] - audio[i - 1]);
            if (first < 0)
                first = crossing;
            last = crossing;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0;
}
void oscillatorsAndOffsets()
{
    const std::array<MidiEvent, 2> notes{
        {{MidiType::noteOn, 37, 1, 69, 1}, {MidiType::noteOff, 12037, 1, 69, 0}}};
    for (const auto wave :
         {Waveform::sine, Waveform::saw, Waveform::square, Waveform::triangle, Waveform::noise})
    {
        auto p = plain();
        p.waveform1 = wave;
        const auto a = render(p, 16000, notes, 256);
        const auto b = render(p, 16000, notes, 73);
        for (std::size_t i = 0; i < a.left.size(); ++i)
            require(a.left[i] == b.left[i] && a.right[i] == b.right[i] && std::isfinite(a.left[i]),
                    "Oscillator changed with MIDI/block partition or became non-finite");
        require(std::all_of(a.left.begin(), a.left.begin() + 37, [](float value) { return value == 0; }),
                "Note began before its MIDI sample offset");
        require(energy(a.left, 1000, 10000) > .001, "Oscillator produced no meaningful PCM");
        require(std::all_of(a.left.begin() + 13000, a.left.end(), [](float value) { return value == 0; }),
                "Note release did not finish deterministically");
        if (wave == Waveform::sine)
            require(std::abs(frequency(a.left, 1000, 10000, 48000) - 440) < .1,
                    "A4 oscillator tuning is wrong");
    }
    auto p = plain();
    p.oscillator1Level = 0;
    p.oscillator2Level = 1;
    p.oscillator2Semitones = 48;
    p.waveform2 = Waveform::saw;
    const std::array<MidiEvent, 1> on{{{MidiType::noteOn, 0, 1, 69, 1}}};
    const auto audio = render(p, 4096, on);
    double naiveError = 0, correctedError = 0, phase = 0;
    const auto step = 7040.0 / 48000;
    for (std::size_t i = 0; i < audio.left.size(); ++i)
    {
        double ideal = 0;
        for (int harmonic = 1; harmonic <= 3; ++harmonic)
            ideal -= 2 / std::numbers::pi * std::sin(2 * std::numbers::pi * harmonic * phase) / harmonic;
        const auto naive = 2 * phase - 1;
        const auto corrected = std::atanh(double(audio.left[i])) * 2;
        naiveError += (naive - ideal) * (naive - ideal);
        correctedError += (corrected - ideal) * (corrected - ideal);
        phase += step;
        phase -= std::floor(phase);
    }
    require(correctedError < naiveError * .36,
            "PolyBLEP saw does not reduce error against a Nyquist-bounded additive oracle");
}
void envelopesAndControllers()
{
    auto p = plain();
    p.attackMs = 10;
    p.decayMs = 20;
    p.sustain = .25;
    const std::array<MidiEvent, 1> on{{{MidiType::noteOn, 0, 1, 69, 1}}};
    const auto env = render(p, 8000, on);
    require(energy(env.left, 0, 100) < energy(env.left, 400, 800) * .15,
            "ADSR attack did not rise from silence");
    require(energy(env.left, 3000, 7000) < energy(env.left, 400, 800) * .25,
            "ADSR decay did not reach its lower sustain");
    Engine engine;
    engine.prepare(48000);
    require(engine.setParameters(plain()), "Sustain setup failed");
    std::array<float, 1024> l{}, r{};
    const std::array<MidiEvent, 3> held{{{MidiType::noteOn, 0, 1, 60, 1},
                                         {MidiType::sustain, 32, 1, 0, 1},
                                         {MidiType::noteOff, 64, 1, 60, 0}}};
    require(engine.process(l.data(), r.data(), 1024, held) && engine.activeVoices() == 1,
            "Sustain pedal lost a released key");
    const std::array<MidiEvent, 1> pedalUp{{{MidiType::sustain, 0, 1, 0, 0}}};
    require(engine.process(l.data(), r.data(), 1024, pedalUp) && engine.activeVoices() == 0,
            "Sustain release retained a hanging voice");
    p = plain();
    p.bendRangeSemitones = 12;
    const std::array<MidiEvent, 2> bend{{{MidiType::noteOn, 0, 1, 69, 1}, {MidiType::pitchBend, 0, 1, 0, 1}}};
    const auto bent = render(p, 24000, bend);
    require(std::abs(frequency(bent.left, 6000, 23000, 48000) - 880) < .2,
            "Channel pitch bend did not reach one octave");
    p.vibratoDepthCents = 80;
    const std::array<MidiEvent, 2> wheel{{{MidiType::noteOn, 0, 1, 69, 1}, {MidiType::modWheel, 0, 1, 0, 1}}};
    const auto modulated = render(p, 24000, wheel);
    const auto dry = render(p, 24000, on);
    double difference = 0;
    for (std::size_t i = 2000; i < dry.left.size(); ++i)
        difference += std::abs(modulated.left[i] - dry.left[i]);
    require(difference > 100, "Mod wheel produced no actual vibrato PCM change");
    p.motion = 1;
    const auto moving = render(p, 24000, on);
    require(moving.left != dry.left, "Motion macro produced no independent PCM modulation");
}
std::unique_ptr<PreparedSampleMap> syntheticMap(bool loop, bool stereo)
{
    // Explicit original synthetic fixture PCM. It does not qualify any acoustic library.
    SampleZone zone;
    zone.rootNote = 60;
    zone.left.resize(48000);
    if (stereo)
        zone.right.resize(zone.left.size());
    for (std::size_t i = 0; i < zone.left.size(); ++i)
    {
        zone.left[i] = .2f * float(std::sin(2 * std::numbers::pi * 261.6255653 * double(i) / 48000));
        if (stereo)
            zone.right[i] = -.5f * zone.left[i];
    }
    zone.loopEnabled = loop;
    zone.loopStart = 100;
    zone.loopEnd = 24000;
    std::vector<SampleZone> zones;
    zones.push_back(std::move(zone));
    std::string error;
    auto map = PreparedSampleMap::create(std::move(zones), error);
    require(bool(map) && error.empty(), "Original synthetic PCM map refused");
    return map;
}
void samplesAndValidation()
{
    std::string error;
    require(!PreparedSampleMap::create({}, error) && !error.empty(), "Empty recorded map accepted");
    SampleZone bad;
    bad.left = {0, .1f, .2f, .3f};
    bad.loopEnabled = true;
    bad.loopStart = 3;
    bad.loopEnd = 2;
    require(!PreparedSampleMap::create({bad}, error), "Reversed loop accepted");
    bad.loopStart = 0;
    bad.loopEnd = 5;
    require(!PreparedSampleMap::create({bad}, error), "Out-of-PCM loop accepted");
    bad.loopEnd = 4;
    bad.left[2] = std::numeric_limits<float>::quiet_NaN();
    require(!PreparedSampleMap::create({bad}, error), "Non-finite sample PCM accepted");
    bad.left[2] = .2f;
    bad.lowNote = 72;
    bad.highNote = 60;
    require(!PreparedSampleMap::create({bad}, error), "Reversed key region accepted");
    bad.lowNote = 0;
    bad.highNote = 127;
    bad.right = {0, 1};
    require(!PreparedSampleMap::create({bad}, error), "Mismatched stereo PCM accepted");
    auto map = syntheticMap(false, true);
    auto p = plain();
    p.sourceMode = SourceMode::sample;
    const std::array<MidiEvent, 1> note{{{MidiType::noteOn, 0, 1, 72, 1}}};
    const auto transposed = render(p, 20000, note, 73, map.get());
    require(std::abs(frequency(transposed.left, 1000, 19000, 48000) - 523.2511306) < .2,
            "Recorded-region root-note resampling did not transpose by one octave");
    for (std::size_t i = 1; i < transposed.left.size(); ++i)
        require(std::abs(std::atanh(double(transposed.right[i])) +
                         .5 * std::atanh(double(transposed.left[i]))) < 1.0e-6,
                "Stereo sample channels were collapsed or unlinked");
    const auto missing = render(p, 1024, note);
    require(energy(missing.left, 0, missing.left.size()) == 0,
            "Missing recorded map silently substituted a synthetic acoustic sound");
    SampleZone zone;
    zone.left = {0, .1f, .2f, .3f, .4f, .5f, .6f, .7f};
    zone.loopEnabled = true;
    zone.loopStart = 2;
    zone.loopEnd = 6;
    auto loop = PreparedSampleMap::create({zone}, error);
    require(bool(loop), "Valid explicit synthetic loop refused");
    const std::array<MidiEvent, 1> rootNote{{{MidiType::noteOn, 0, 1, 60, 1}}};
    const auto looped = render(p, 32, rootNote, 7, loop.get());
    for (std::size_t i = 0; i < looped.left.size(); ++i)
    {
        const auto index = i < 6 ? i : 2 + (i - 6) % 4;
        require(std::abs(looped.left[i] - std::tanh(zone.left[index] * .5f)) < 1.0e-6,
                "End-exclusive sample loop or fractional PCM cursor is wrong");
    }
    SampleZone soft = zone, hard = zone;
    soft.loopEnabled = hard.loopEnabled = false;
    soft.left.assign(1000, .1f);
    hard.left.assign(1000, .4f);
    soft.highVelocity = 63;
    hard.lowVelocity = 64;
    auto layered = PreparedSampleMap::create({soft, hard}, error);
    require(bool(layered) && layered->zoneFor(60, 40)->highVelocity == 63 &&
                layered->zoneFor(60, 100)->lowVelocity == 64,
            "Velocity region selection is wrong");
    const std::array<MidiEvent, 1> gentle{{{MidiType::noteOn, 0, 1, 60, .2f}}};
    const std::array<MidiEvent, 1> strong{{{MidiType::noteOn, 0, 1, 60, .8f}}};
    const auto low = render(p, 64, gentle, 64, layered.get());
    const auto high = render(p, 64, strong, 64, layered.get());
    require(std::abs(low.left[0] - std::tanh(.01f)) < 1.0e-6 &&
                std::abs(high.left[0] - std::tanh(.16f)) < 1.0e-6,
            "Velocity layers or amplitude response did not affect actual PCM");
    const auto sampleOnly = render(p, 1024, note, 256, map.get());
    const auto oscillatorOnly = render(plain(), 1024, note);
    p.sourceMode = SourceMode::hybrid;
    const auto hybrid = render(p, 1024, note, 256, map.get());
    require(hybrid.left != sampleOnly.left && hybrid.left != oscillatorOnly.left,
            "Hybrid source omitted either its sample or oscillator layer");
    for (std::size_t i = 0; i < hybrid.left.size(); ++i)
        require(std::abs(std::atanh(double(hybrid.left[i])) - std::atanh(double(sampleOnly.left[i])) -
                         std::atanh(double(oscillatorOnly.left[i]))) < 1.0e-6,
                "Hybrid PCM does not equal its independently rendered layer sum before the soft ceiling");
}
void roundRobinsAndArticulations()
{
    // Distinct constant PCM gives an exact source-selection oracle, independent
    // of a real instrument's dynamics, pitch interpolation or human listening.
    const auto constant = [](float value)
    {
        SampleZone zone;
        zone.left.assign(256, value);
        return zone;
    };
    auto first = constant(.2f), second = constant(.6f);
    first.roundRobinGroup = second.roundRobinGroup = 7;
    second.roundRobinIndex = 1;
    std::string error;
    // The first variant is an explicit identity, not manifest/list order.
    const auto map = PreparedSampleMap::create({second, first}, error);
    require(bool(map) && error.empty(), "Valid shuffled two-variant sample map refused");
    for (std::uint32_t ordinal = 0; ordinal < 8; ++ordinal)
        require(map->zoneFor(60, 127, ordinal)->left.front() == (ordinal % 2 == 0 ? .2f : .6f),
                "Prepared round-robin selection did not alternate explicit variant identities");
    require(map->zoneFor(60, 127)->roundRobinIndex == 0,
            "Two-argument region lookup did not retain a deterministic first variant");
    auto bad = first;
    bad.roundRobinGroup = -1;
    require(!PreparedSampleMap::create({bad}, error), "Negative round-robin group accepted");
    bad = first;
    bad.roundRobinIndex = -1;
    require(!PreparedSampleMap::create({bad}, error), "Negative round-robin variant accepted");
    bad.roundRobinGroup = 0;
    bad.roundRobinIndex = 1;
    require(!PreparedSampleMap::create({bad}, error), "Variant without a round-robin group accepted");
    require(!PreparedSampleMap::create({first, first}, error), "Duplicate round-robin identity accepted");
    bad = second;
    bad.roundRobinIndex = 2;
    require(!PreparedSampleMap::create({first, bad}, error), "Missing round-robin variant accepted");
    require(!PreparedSampleMap::create({second}, error), "Round-robin group without variant zero accepted");
    bad = second;
    bad.lowVelocity = 1;
    require(!PreparedSampleMap::create({first, bad}, error),
            "One round-robin group with incompatible velocity regions accepted");
    bad = second;
    bad.rootNote = 61;
    require(!PreparedSampleMap::create({first, bad}, error),
            "One round-robin group with incompatible root notes accepted");
    bad = constant(.4f);
    require(!PreparedSampleMap::create({first, second, bad}, error),
            "Ambiguous recorded region overlapping a round-robin group accepted");
    // Natural recorded takes may have different lengths, levels and real loops.
    bad = second;
    bad.left.resize(128);
    bad.gainDb = -1;
    bad.pan = .1f;
    bad.loopEnabled = true;
    bad.loopStart = 16;
    bad.loopEnd = 120;
    require(bool(PreparedSampleMap::create({first, bad}, error)),
            "Valid per-take duration/gain/pan/explicit-loop variation was rejected");

    Engine engine;
    engine.prepare(48000);
    engine.bindSampleMap(map.get());
    auto p = plain();
    p.sourceMode = SourceMode::sample;
    p.releaseMs = 0;
    require(engine.setParameters(p), "Round-robin PCM setup refused");
    int lastChannel = 1, lastNote = 60;
    const auto trigger = [&](int channel, int note, float velocity = 1)
    {
        const std::array<MidiEvent, 2> events{{{MidiType::noteOff, 0, lastChannel, lastNote, 0},
                                               {MidiType::noteOn, 0, channel, note, velocity}}};
        float left = 0, right = 0;
        bool accepted = false;
        {
            synth_test::CallbackScope guard;
            accepted = engine.process(&left, &right, 1, events);
        }
        require(accepted && left == right && engine.activeVoices() == 1,
                "Round-robin retrigger failed or retained a released voice");
        lastChannel = channel;
        lastNote = note;
        return left;
    };
    const auto expect = [&](int channel, int note, float sample)
    {
        require(std::abs(trigger(channel, note) - std::tanh(.5f * sample)) < 1.0e-6,
                "Actual PCM did not follow the independent note/channel round-robin sequence");
    };
    expect(1, 60, .2f);
    expect(1, 60, .6f);
    expect(1, 61, .2f);
    expect(2, 60, .2f);
    expect(1, 60, .2f);
    expect(1, 61, .6f);
    expect(2, 60, .6f);
    engine.panic();
    expect(1, 60, .2f);
    expect(2, 60, .2f);
    const std::array<MidiEvent, 1> stopChannel{{{MidiType::allSoundOff, 0, 1, 0, 0}}};
    float left = 0, right = 0;
    require(engine.process(&left, &right, 1, stopChannel), "Channel panic refused");
    expect(1, 60, .2f);
    expect(2, 60, .6f);
    expect(1, 60, .6f);
    engine.bindSampleMap(nullptr);
    engine.bindSampleMap(map.get());
    expect(1, 60, .2f);
    engine.prepare(96000);
    require(engine.setParameters(p), "Round-robin reprepare setup refused");
    expect(1, 60, .2f);
    const std::array<MidiEvent, 1> zeroVelocity{{{MidiType::noteOn, 0, 1, 60, 0}}};
    require(engine.process(&left, &right, 1, zeroVelocity) && engine.activeVoices() == 0,
            "Zero-velocity note-on did not release the recorded voice");
    expect(1, 60, .6f);

    // Repeated notes at real sample offsets must select identical takes for
    // different audio-block partitions, including more than one onset/block.
    const std::array<MidiEvent, 7> sequence{{{MidiType::noteOn, 3, 1, 60, 1},
                                             {MidiType::noteOff, 18, 1, 60, 0},
                                             {MidiType::noteOn, 19, 1, 60, 1},
                                             {MidiType::noteOff, 34, 1, 60, 0},
                                             {MidiType::noteOn, 35, 1, 60, 1},
                                             {MidiType::noteOff, 50, 1, 60, 0},
                                             {MidiType::noteOn, 51, 1, 60, 1}}};
    const auto oneBlock = render(p, 70, sequence, 70, map.get());
    const auto split = render(p, 70, sequence, 7, map.get());
    require(oneBlock.left == split.left && oneBlock.right == split.right,
            "Round-robin selection depended on audio-block partition");
    for (std::size_t index = 0; index < 4; ++index)
        require(std::abs(oneBlock.left[3 + 16 * index] - std::tanh(.5f * (index % 2 == 0 ? .2f : .6f))) <
                    1.0e-6,
                "Round-robin source began at the wrong MIDI sample offset");

    auto softA = first, softB = second, loudA = first, loudB = second;
    softA.highVelocity = softB.highVelocity = 63;
    loudA.lowVelocity = loudB.lowVelocity = 64;
    loudA.roundRobinGroup = loudB.roundRobinGroup = 8;
    loudA.left.assign(256, .4f);
    loudB.left.assign(256, .8f);
    const auto layered = PreparedSampleMap::create({softB, loudA, softA, loudB}, error);
    require(bool(layered) && layered->zoneFor(60, 40, 0)->left.front() == .2f &&
                layered->zoneFor(60, 40, 1)->left.front() == .6f &&
                layered->zoneFor(60, 100, 0)->left.front() == .4f &&
                layered->zoneFor(60, 100, 1)->left.front() == .8f,
            "Velocity-layer regions did not retain their independent round-robin variants");
    const std::array<MidiEvent, 3> changingVelocity{{{MidiType::noteOn, 0, 1, 60, .25f},
                                                     {MidiType::noteOff, 15, 1, 60, 0},
                                                     {MidiType::noteOn, 16, 1, 60, 1}}};
    const auto layeredAudio = render(p, 32, changingVelocity, 7, layered.get());
    require(std::abs(layeredAudio.left[0] - std::tanh(.025f)) < 1.0e-6 &&
                std::abs(layeredAudio.left[16] - std::tanh(.4f)) < 1.0e-6,
            "Actual PCM did not combine velocity-layer selection with the per-note round-robin ordinal");

    std::vector<SampleZone> articulations;
    for (int index = 0; index < 3; ++index)
    {
        auto zone = constant(.1f * float(index + 1));
        zone.rootNote = zone.lowNote = zone.highNote = 60 + 2 * index;
        articulations.push_back(std::move(zone));
    }
    const auto keyed = PreparedSampleMap::create(std::move(articulations), error);
    require(bool(keyed), "Dedicated-note recorded articulation map refused");
    for (int index = 0; index < 3; ++index)
    {
        const std::array<MidiEvent, 1> on{{{MidiType::noteOn, 0, 1, 60 + 2 * index, 1}}};
        const auto audio = render(p, 8, on, 8, keyed.get());
        require(std::abs(audio.left[0] - std::tanh(.05f * float(index + 1))) < 1.0e-6,
                "Dedicated-note articulation selected the wrong recorded PCM");
        require(keyed->zoneFor(60 + 2 * index, 127, 97) == keyed->zoneFor(60 + 2 * index, 127),
                "Default non-round-robin region changed with note ordinal");
    }
    const std::array<MidiEvent, 1> unmapped{{{MidiType::noteOn, 0, 1, 61, 1}}};
    const auto silence = render(p, 8, unmapped, 8, keyed.get());
    require(energy(silence.left, 0, silence.left.size()) == 0,
            "Unmapped articulation note substituted a different recorded sound");
    require(synth_test::allocations == 0 && synth_test::destructions == 0,
            "Round-robin source selection allocated or destroyed callback heap objects");
}
void safetyAndStealing()
{
    Engine engine;
    engine.prepare(48000);
    auto p = plain();
    p.polyphony = 2;
    require(engine.setParameters(p), "Polyphony setup refused");
    std::array<float, 1024> left{}, right{};
    const std::array<MidiEvent, 3> chord{
        {{MidiType::noteOn, 0, 1, 60, 1}, {MidiType::noteOn, 0, 1, 64, 1}, {MidiType::noteOn, 0, 1, 67, 1}}};
    require(engine.process(left.data(), right.data(), 1024, chord) && engine.activeVoices() == 2,
            "Voice stealing exceeded configured polyphony");
    require(engine.peak(0) > .1f && engine.peak(0) <= 1 && engine.peak(1) <= 1,
            "Actual synth output meter or headroom is wrong");
    const std::array<MidiEvent, 1> stop{{{MidiType::allSoundOff, 123, 1, 0, 0}}};
    require(engine.process(left.data(), right.data(), 1024, stop) && engine.activeVoices() == 0,
            "All Sound Off left active voices");
    require(std::all_of(left.begin() + 123, left.end(), [](float x) { return x == 0; }),
            "Panic did not obey its exact MIDI offset");
    auto bad = p;
    bad.cutoffHz = std::numeric_limits<float>::quiet_NaN();
    require(!engine.setParameters(bad) && engine.parameters().cutoffHz == p.cutoffHz,
            "Invalid synth controls partially mutated current state");
    auto invalid = chord;
    invalid[1].sampleOffset = -1;
    require(!engine.process(left.data(), right.data(), 1024, invalid) && engine.activeVoices() == 0 &&
                std::all_of(left.begin(), left.end(), [](float x) { return x == 0; }),
            "Malformed MIDI block partially applied notes or retained stale sound");
    const std::array<MidiEvent, 1> on{{{MidiType::noteOn, 0, 1, 69, 1}}};
    require(engine.process(left.data(), right.data(), 1024, on), "Panic restart failed");
    const auto first = left;
    {
        synth_test::CallbackScope guard;
        engine.panic();
        engine.setParameters(p);
        engine.process(left.data(), right.data(), 1024, on);
    }
    require(first == left, "Panic/restart was not deterministic");
    auto map = syntheticMap(true, false);
    {
        synth_test::CallbackScope guard;
        engine.bindSampleMap(map.get());
        engine.reset();
        engine.bindSampleMap(nullptr);
    }
    for (double rate : {8000.0, 44100.0, 96000.0, 192000.0, 384000.0})
    {
        engine.prepare(rate);
        require(engine.setParameters(p), "Reprepare setup refused");
        require(engine.process(left.data(), right.data(), 1024, on) &&
                    std::all_of(left.begin(), left.end(), [](float x) { return std::isfinite(x); }),
                "Supported-rate reprepare produced invalid PCM");
    }
    bool refused = false;
    try
    {
        engine.prepare(0);
    }
    catch (const std::invalid_argument &)
    {
        refused = true;
    }
    require(refused, "Invalid sample rate accepted");
    require(synth_test::allocations == 0 && synth_test::destructions == 0,
            "Synth note/process/parameter/panic/map binding allocated or destroyed C++ heap objects");
}
void filterAndPolyphonyStress()
{
    const std::array<MidiEvent, 1> on{{{MidiType::noteOn, 0, 1, 69, 1}}};
    auto response = plain();
    const auto dry = render(response, 12000, on);
    response.filterType = FilterType::lowpass;
    response.cutoffHz = 100;
    const auto lowpass = render(response, 12000, on);
    response.filterType = FilterType::highpass;
    const auto highpass = render(response, 12000, on);
    response.filterType = FilterType::bandpass;
    response.cutoffHz = 440;
    const auto bandpass = render(response, 12000, on);
    const auto dryEnergy = energy(dry.left, 2000, 12000);
    require(energy(lowpass.left, 2000, 12000) < dryEnergy * .01 &&
                energy(highpass.left, 2000, 12000) > dryEnergy * .85 &&
                std::abs(energy(bandpass.left, 2000, 12000) / dryEnergy - 1) < .01,
            "Actual LP/HP/BP synth filter frequency responses are incorrect");
    Engine engine;
    engine.prepare(48000);
    auto p = plain();
    p.polyphony = 64;
    p.filterType = FilterType::lowpass;
    p.cutoffHz = 50;
    p.resonance = 12;
    p.masterDb = 12;
    p.waveform1 = Waveform::saw;
    require(engine.setParameters(p), "Stress setup refused");
    std::array<MidiEvent, 64> chord{};
    for (std::size_t i = 0; i < chord.size(); ++i)
        chord[i] = {MidiType::noteOn, 0, 1, int(i) + 24, 1};
    std::array<float, 64> left{}, right{};
    require(engine.process(left.data(), right.data(), 64, chord) && engine.activeVoices() == 64,
            "Full polyphony did not allocate all fixed voices");
    for (int offset = 64; offset < 48000; offset += 64)
    {
        p.cutoffHz = 50 + 30 * float(std::sin(2 * std::numbers::pi * 100 * offset / 48000));
        bool accepted = false;
        {
            synth_test::CallbackScope guard;
            accepted = engine.setParameters(p) && engine.process(left.data(), right.data(), 64);
        }
        require(accepted, "Legal rapid cutoff edit refused");
        for (const auto sample : left)
            require(std::isfinite(sample) && std::abs(sample) <= 1,
                    "Polyphonic resonant filter/headroom produced non-finite or unbounded PCM");
    }
    engine.panic();
    require(engine.process(left.data(), right.data(), 64) && engine.activeVoices() == 0 &&
                std::all_of(left.begin(), left.end(), [](float x) { return x == 0; }),
            "Stress panic retained filter or sample state");
    require(synth_test::allocations == 0 && synth_test::destructions == 0,
            "Polyphonic filter editing allocated or destroyed C++ heap objects");
}
double highFrequencyEnergy(const std::vector<float> &audio, std::size_t begin, std::size_t end)
{
    double result = 0;
    for (auto i = begin + 1; i < end; ++i)
        result += double(audio[i] - audio[i - 1]) * (audio[i] - audio[i - 1]);
    return result / double(std::max<std::size_t>(end - begin, 1));
}
void productionVoice()
{
    const std::array<MidiEvent, 2> held{
        {{MidiType::noteOn, 0, 1, 57, 1}, {MidiType::noteOff, 24000, 1, 57, 0}}};
    // Unison: one voice stays exactly mono; seven detuned voices decorrelate stereo.
    auto p = plain();
    p.waveform1 = p.waveform2 = Waveform::saw;
    p.masterDb = -12;
    const auto mono = render(p, 30000, held);
    require(std::equal(mono.left.begin(), mono.left.end(), mono.right.begin()),
            "Single unison voice is no longer the exact mono legacy voice");
    p.unisonVoices = 7;
    p.unisonDetuneCents = 30;
    p.unisonSpread = 1;
    const auto wide = render(p, 30000, held);
    double cross = 0, leftPower = 0, rightPower = 0;
    for (std::size_t i = 4800; i < 24000; ++i)
    {
        cross += double(wide.left[i]) * wide.right[i];
        leftPower += double(wide.left[i]) * wide.left[i];
        rightPower += double(wide.right[i]) * wide.right[i];
    }
    require(cross / std::sqrt(leftPower * rightPower) < .9 && energy(wide.left, 4800, 24000) > .001,
            "Unison stack did not produce a decorrelated stereo image");
    require(std::all_of(wide.left.begin(), wide.left.end(), [](float x) { return std::isfinite(x); }),
            "Unison stack produced non-finite PCM");
    // The 24 dB ladder rejects upper harmonics far more strongly than the 12 dB SVF.
    p = plain();
    p.waveform1 = Waveform::saw;
    p.filterType = FilterType::lowpass;
    p.cutoffHz = 500;
    p.resonance = .7071f;
    p.masterDb = -12;
    const auto svf = render(p, 24000, held);
    p.ladderFilter = true;
    const auto ladder = render(p, 24000, held);
    require(highFrequencyEnergy(ladder.left, 4800, 20000) < highFrequencyEnergy(svf.left, 4800, 20000) * .6 &&
                energy(ladder.left, 4800, 20000) > energy(svf.left, 4800, 20000) * .2,
            "Ladder filter is not a steeper musical low-pass");
    // Maximum resonance, drive and polyphony stay finite, bounded and allocation-free.
    {
        Engine engine;
        engine.prepare(48000);
        p.resonance = 8;
        p.filterDrive = 1;
        p.unisonVoices = 3;
        p.polyphony = 64;
        p.masterDb = 12;
        require(engine.setParameters(p), "Ladder stress setup refused");
        std::array<MidiEvent, 64> chord{};
        for (std::size_t i = 0; i < chord.size(); ++i)
            chord[i] = {MidiType::noteOn, 0, 1, int(i) + 24, 1};
        std::array<float, 64> left{}, right{};
        const auto before = synth_test::allocations;
        for (int block = 0; block < 400; ++block)
        {
            bool accepted = false;
            {
                synth_test::CallbackScope guard;
                accepted = engine.process(left.data(), right.data(), 64,
                                          block == 0 ? std::span<const MidiEvent>(chord)
                                                     : std::span<const MidiEvent>());
            }
            require(accepted, "Ladder stress block refused");
            for (const auto sample : left)
                require(std::isfinite(sample) && std::abs(sample) <= 1,
                        "Self-resonant driven ladder produced non-finite or unbounded PCM");
        }
        require(synth_test::allocations == before, "Ladder/unison callback allocated");
    }
    // Sub oscillator sits one octave below the played note.
    p = plain();
    p.oscillator1Level = 0;
    p.subLevel = 1;
    const auto sub = render(p, 24000, held);
    require(std::abs(frequency(sub.left, 4800, 20000, 48000) - 110) < .5,
            "Sub oscillator is not one octave down");
    // Legato: one voice, pitch follows the newest key and returns to the held one.
    p = plain();
    p.voiceMode = VoiceMode::legato;
    const std::array<MidiEvent, 4> phrase{{{MidiType::noteOn, 0, 1, 57, 1},
                                           {MidiType::noteOn, 12000, 1, 69, 1},
                                           {MidiType::noteOff, 24000, 1, 69, 0},
                                           {MidiType::noteOff, 36000, 1, 57, 0}}};
    {
        Engine engine;
        engine.prepare(48000);
        require(engine.setParameters(p), "Legato setup refused");
        Audio audio(40000);
        std::array<MidiEvent, 1> one{};
        std::size_t next = 0;
        for (int frame = 0; frame < 40000; frame += 1000)
        {
            std::size_t used = 0;
            if (next < phrase.size() && phrase[next].sampleOffset < frame + 1000)
            {
                one[0] = phrase[next++];
                one[0].sampleOffset -= frame;
                used = 1;
            }
            require(engine.process(audio.left.data() + frame, audio.right.data() + frame, 1000,
                                   {one.data(), used}),
                    "Legato block refused");
            require(engine.activeVoices() <= 1, "Legato allocated more than one voice");
        }
        require(std::abs(frequency(audio.left, 2000, 11000, 48000) - 220) < .5 &&
                    std::abs(frequency(audio.left, 14000, 23000, 48000) - 440) < .5 &&
                    std::abs(frequency(audio.left, 26000, 35000, 48000) - 220) < .5,
                "Legato did not follow and return to the held key");
    }
    // Glide moves between keys over time instead of jumping.
    p.glideMs = 100;
    {
        const auto glide = render(p, 30000, phrase);
        const auto early = frequency(glide.left, 12200, 13400, 48000);
        require(early > 225 && early < 430, "Glide jumped instead of sliding between keys");
        require(std::abs(frequency(glide.left, 22000, 23900, 48000) - 440) < 3,
                "Glide never reached its target");
    }
    // Curved release decays faster early (exponential) than the legacy linear ramp.
    p = plain();
    p.releaseMs = 1000;
    const std::array<MidiEvent, 2> shortNote{
        {{MidiType::noteOn, 0, 1, 69, 1}, {MidiType::noteOff, 4800, 1, 69, 0}}};
    const auto linear = render(p, 40000, shortNote);
    p.envelopeCurve = 1;
    const auto curved = render(p, 40000, shortNote);
    require(energy(curved.left, 14400, 19200) < energy(linear.left, 14400, 19200) * .5 &&
                energy(curved.left, 4800, 5280) > energy(linear.left, 4800, 5280) * .7,
            "Curved release is not an exponential tail");
    // Velocity sensitivity zero makes soft and hard notes equal.
    p = plain();
    p.velocityToAmp = 0;
    const std::array<MidiEvent, 1> soft{{{MidiType::noteOn, 0, 1, 69, .2f}}};
    const std::array<MidiEvent, 1> hard{{{MidiType::noteOn, 0, 1, 69, 1}}};
    require(std::abs(energy(render(p, 9600, soft).left, 2000, 9000) -
                     energy(render(p, 9600, hard).left, 2000, 9000)) < 1e-9,
            "Velocity sensitivity zero still scales amplitude");
    // A separate mod envelope closes the filter after its decay.
    p = plain();
    p.waveform1 = Waveform::saw;
    p.filterType = FilterType::lowpass;
    p.cutoffHz = 200;
    p.filterEnvelopeOctaves = 5;
    p.modDecayMs = 60;
    p.masterDb = -12;
    const auto plucked = render(p, 24000, held);
    require(highFrequencyEnergy(plucked.left, 200, 1600) >
                highFrequencyEnergy(plucked.left, 14000, 15400) * 10,
            "Mod envelope did not sweep the filter");
    // FM adds sidebands to a sine carrier.
    p = plain();
    p.fmAmount = .6f;
    const auto fm = render(p, 24000, held);
    p.fmAmount = 0;
    const auto pure = render(p, 24000, held);
    require(highFrequencyEnergy(fm.left, 4800, 20000) > highFrequencyEnergy(pure.left, 4800, 20000) * 2,
            "FM amount did not add sidebands");
    // Asymmetric pulses carry no DC offset into the output.
    p = plain();
    p.waveform1 = Waveform::square;
    p.pulseWidth = .2f;
    p.masterDb = -24; // Linear region of the soft ceiling.
    const auto pulse = render(p, 48000, held);
    double mean = 0;
    for (std::size_t i = 12000; i < 24000; ++i)
        mean += pulse.left[i];
    require(std::abs(mean / 12000) < .002 && energy(pulse.left, 12000, 24000) > 1e-4,
            "Asymmetric pulse left a DC offset");
    Parameters bad = plain();
    bad.unisonVoices = 17;
    Engine engine;
    engine.prepare(48000);
    require(!engine.setParameters(bad), "Out-of-range unison count admitted");
}
} // namespace
// Wavetable oscillator: right pitch, the position changes the timbre, a high note stays
// band-limited (no energy folding below its fundamental), and 16-voice unison works.
void wavetableVoice()
{
    const std::array<MidiEvent, 2> notes{{{MidiType::noteOn, 0, 1, 69, 1}, {MidiType::noteOff, 20000, 1, 69, 0}}};
    auto p = plain();
    p.waveform1 = Waveform::wavetable;
    p.wavetableBank = 0;
    p.wavetablePosition = 0; // Pure sine frame.
    const auto sine = render(p, 24000, notes);
    require(energy(sine.left, 2000, 18000) > .001, "Wavetable produced no PCM");
    require(std::abs(frequency(sine.left, 2000, 18000, 48000) - 440) < .2, "Wavetable A4 tuning is wrong");
    p.wavetablePosition = 1; // Square frame: far more high harmonics.
    const auto square = render(p, 24000, notes);
    const auto roughness = [](const std::vector<float> &a)
    {
        double d = 0, e = 0;
        for (std::size_t i = 2001; i < 18000; ++i)
        {
            d += double(a[i] - a[i - 1]) * (a[i] - a[i - 1]);
            e += double(a[i]) * a[i];
        }
        return d / e;
    };
    require(roughness(square.left) > roughness(sine.left) * 8, "Table position did not change the timbre");
    p.wavetableEnvelope = 1;
    p.wavetablePosition = 0;
    p.decayMs = 300;
    p.sustain = 0;
    const auto swept = render(p, 24000, notes);
    require(swept.left != sine.left, "Envelope to position did not move the table");
    // A high note (C8, 4186 Hz) on the brightest frame: no alias below the fundamental.
    auto high = plain();
    high.waveform1 = Waveform::wavetable;
    high.wavetablePosition = 1;
    const std::array<MidiEvent, 1> c8{{{MidiType::noteOn, 0, 1, 108, 1}}};
    const auto bright = render(high, 16384, c8);
    double low = 0, total = 0;
    for (int k = 13; k < 23900; k += 13)
    {
        double re = 0, im = 0;
        for (int i = 4000; i < 16384; ++i)
        {
            const double window = .5 - .5 * std::cos(2 * 3.141592653589793 * (i - 4000) / 12384.0);
            re += window * bright.left[std::size_t(i)] * std::cos(2 * 3.141592653589793 * k * i / 48000.0);
            im += window * bright.left[std::size_t(i)] * std::sin(2 * 3.141592653589793 * k * i / 48000.0);
        }
        const double power = re * re + im * im;
        total += power;
        if (k < 4000)
            low += power;
    }
    require(total > 0 && low < total * .02, "High wavetable note aliased below its fundamental");
    auto wide = plain();
    wide.waveform1 = Waveform::wavetable;
    wide.unisonVoices = 16;
    wide.unisonDetuneCents = 30;
    wide.unisonSpread = 1;
    const auto stack = render(wide, 24000, notes);
    require(energy(stack.left, 2000, 18000) > .0005 && stack.left != stack.right,
            "16-voice wavetable unison produced no wide PCM");
    Parameters bad = plain();
    bad.wavetableBank = 9;
    Engine engine;
    engine.prepare(48000);
    require(!engine.setParameters(bad), "Unknown wavetable bank accepted");
}
int main()
{
    const auto start = std::chrono::steady_clock::now();
    try
    {
        oscillatorsAndOffsets();
        envelopesAndControllers();
        samplesAndValidation();
        roundRobinsAndArticulations();
        safetyAndStealing();
        filterAndPolyphonyStress();
        productionVoice();
        wavetableVoice();
        std::cout
            << "PASS original hybrid synth DSP; " << checks
            << " checks; callback C++ heap=" << synth_test::allocations << "/" << synth_test::destructions
            << "; seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
            << "; synthetic oscillator/PCM fixtures only; recorded library/listening/hardware separate\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
