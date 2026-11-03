// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <cmath>
#include <algorithm>
#include <limits>
#include <memory>
#include "SamplerSettings.h"
#include "NoteExpression.h"
#include "SamplerFilter.h"
#include "SamplerModulation.h"
#include "SamplerModulationAutomation.h"

namespace vibera
{
// Prepared off the audio thread, immutable while the graph is active.
struct Sample
{
    juce::AudioBuffer<float> data;
    double sampleRate = 48000;
    int rootNote = 60;
    bool oneShot = true;
    double sourcePeak = 0, normalizationGain = 1;
    // First WAV "smpl" loop as fractions of (frames - 1), like loopStart/loopEnd; -1 when absent.
    double fileLoopStart = -1, fileLoopEnd = -1;
    // Control/decoder thread only, before immutable publication. One gain keeps stereo balance.
    void prepareNormalization() noexcept
    {
        sourcePeak = 0;
        for (int channel = 0; channel < data.getNumChannels(); ++channel)
            for (int frame = 0; frame < data.getNumSamples(); ++frame)
                sourcePeak = std::max(sourcePeak, std::abs(double(data.getSample(channel, frame))));
        normalizationGain = sourcePeak > 0 ? 1. / sourcePeak : 1.;
    }
};
inline Sample makeOriginalSample(const juce::String &kind)
{
    Sample s;
    s.oneShot = kind != "bass";
    const int size = kind == "bass" ? 96000 : 24000;
    s.data.setSize(2, size);
    std::uint32_t random = 0x13579bdfu;
    double phase = 0, filtered = 0;
    for (int i = 0; i < size; ++i)
    {
        const double t = i / s.sampleRate;
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        const double noise = double(random) / 2147483648. - 1.;
        double value = 0;
        if (kind == "kick")
        {
            phase += 2 * juce::MathConstants<double>::pi * (48 + 110 * std::exp(-t * 35)) / s.sampleRate;
            value = .85 * std::sin(phase) * std::exp(-t * 12);
        }
        else if (kind == "snare")
            value = (.5 * noise + .22 * std::sin(2 * juce::MathConstants<double>::pi * 180 * t)) *
                    std::exp(-t * 22);
        else if (kind == "hat")
        {
            filtered = .88 * filtered + .12 * noise;
            value = .38 * (noise - filtered) * std::exp(-t * 65);
        }
        else
        {
            phase = 2 * juce::MathConstants<double>::pi * juce::MidiMessage::getMidiNoteInHertz(60) * t;
            value = .45 * (std::sin(phase) + .2 * std::sin(2 * phase) + .1 * std::sin(3 * phase));
        }
        value *= std::min(1., t * 1000.);
        value *= std::min(1., (size - i) / 240.);
        s.data.setSample(0, i, float(value));
        s.data.setSample(1, i, float(value));
    }
    s.prepareNormalization();
    return s;
}
class VoiceBank
{
  public:
    void setBend(double ratio) noexcept
    {
        bend = std::isfinite(ratio) && ratio > 0 ? ratio : 1.;
    }
    static constexpr std::size_t capacity = 128;

  private:
    friend class SamplerVoiceBudget;
    struct TransitionTarget
    {
        double rate = 1, pan = 0, cutoff = 0, resonance = 0;
        bool operator==(const TransitionTarget &) const = default;
    };
    struct TransitionSeed
    {
        bool valid = false;
        TransitionTarget value;
    };
    struct Voice
    {
        bool active = false, releasing = false, noteReleased = false;
        bool forcedQuickRelease = false;
        int pitch = 0, colourGroup = 0;
        PreparedNoteExpression sourceExpression;
        float sourceVelocity = 0;
        struct SlideControl
        {
            bool retained = false;
            std::int64_t note = 0, placement = 0;
            double start = 0, baseRate = 1, pitchRatio = 1;
        } slide;
        double position = 0, delta = 1;
        float gain = 0, envelope = 0, targetGain = 0;
        PreparedNoteExpression expression;
        float leftGain = 1, rightGain = 1;
        double normalizationGain = 1, targetNormalizationGain = 1;
        SamplerFilter filter;
        VoiceSamplerModulation modulation;
        bool modulationUsed = false;
        double modulationTail = 1;
        std::int64_t note = 0, placement = 0;
        std::uint32_t repeatOrdinal = 0;
        double onset = 0;
        double begin = 0, end = 0, targetDelta = 1, age = 0, releaseLevel = -1;
        // Only legacy gate/loop settings are captured per voice. The full live
        // modulation array belongs to the bank, avoiding128 redundant copies.
        struct CapturedSettings
        {
            bool envelope = false, loop = false, pingPong = false;
            double attack = .002, decay = .1, sustain = 1, release = .012;
        } settings;
        bool oneShot = true;
        double loopBegin = 0, loopEnd = 0;
        // Captured source-frame fades; reversed measures from the region end.
        double fadeInFrames = 0, fadeOutFrames = 0, crossfadeFrames = 0;
        bool reversed = false;
        int direction = 1;
        std::uint64_t triggerSerial = 0, liveSequence = 0;
        struct Glide
        {
            TransitionTarget from, current, target;
            std::uint64_t frames = 0, elapsed = 0, segmentStart = 0;
            bool velocityControl = false;
            double fromGain = 0;
        } glide;
        // Gate is logical key-off, independent of the captured amplitude envelope.
        double gateBeats = 0, gateElapsedBeats = 0, gateTempo = 120;
        std::uint64_t gateFrames = 0, gateDeadline = 0;
    };
    // CONTROL construction/destruction only; immutable capacity and stable voice addresses.
    using Voices = std::array<Voice, capacity>;
    std::unique_ptr<Voices> voiceStorage = std::make_unique<Voices>();
    Voices &voices = *voiceStorage;
    const Sample *sample = nullptr;
    double rate = 48000;
    std::array<double, 128> pitchDeltas{}, rootRatios{};
    PreparedSamplerSettings settings;
    PreparedSamplerFilter filterPreparation;
    PreparedSamplerModulation modulationPreparation;
    const double *modulationPitch = nullptr; // Borrowed immutable CONTROL-prepared table.
    SamplerModulationPhases localGlobalPhases{};
    const SamplerModulationPhases *sharedGlobalPhases = nullptr;
    int sharedGlobalStart = 0;
    bool modulationActive = false;
    SamplerModulationAutomationBlock *automationBlock = nullptr; // Borrowed sole Runtime owner.
    int selectedModulationFrame = -1;
    bool modulationHealthy = true;
    void reconcileModulationGate() noexcept
    {
        modulationActive = std::any_of(
            settings.values.modulation.begin(), settings.values.modulation.end(),
            [](const auto &m) { return (m.enabled && m.amount != 0) || (m.lfoEnabled && m.lfoAmount != 0); });
        const auto &volume = settings.values.modulation[0];
        const bool gate = volume.enabled && volume.amount != 0;
        for (auto &v : voices)
            if (v.active)
            {
                v.oneShot = sample->oneShot && !v.settings.envelope && !v.settings.loop && !gate;
                if (v.noteReleased && !v.oneShot)
                    v.releasing = true;
            }
    }
    double modulationPitchRatio(double value) const noexcept
    {
        const double position = (std::clamp(value, -2., 2.) + 2.) * 1024.;
        const auto index = std::min(size_t(position), SamplerModulationTables::pitchIntervals - 1);
        return modulationPitch[index] +
               (position - index) * (modulationPitch[index + 1] - modulationPitch[index]);
    }
    void advanceLocalModulationClock(int count) noexcept
    {
        if (!sharedGlobalPhases && !automationBlock)
            for (size_t i = 0; i < localGlobalPhases.size(); ++i)
                localGlobalPhases[i] = PreparedSamplerModulation::wrap(
                    localGlobalPhases[i] + count * modulationPreparation.globalIncrements[i]);
    }
    // Channel pitch bend as a playback-rate ratio, set per block by the owning sampler (1 = none).
    double bend = 1;
    double delta(int pitch) const noexcept
    {
        return pitchDeltas[size_t(juce::jlimit(0, 127, pitch))] * settings.tuning *
               (settings.values.root < 0 ? 1. : rootRatios[size_t(settings.values.root)]);
    }
    unsigned steal = 0;
    std::uint64_t localSerial = 0, *serial = &localSerial;
    void *budget = nullptr;
    void (*beforeNote)(void *, VoiceBank &, TransitionSeed &, int, bool) noexcept = nullptr;
    void (*afterNote)(void *, VoiceBank &, size_t) noexcept = nullptr;
    struct GroupMemory
    {
        size_t slot = capacity;
        std::uint64_t serial = 0;
        TransitionTarget target;
        bool valid = false;
    };
    std::array<GroupMemory, 16> foregroundGroups{};
    bool gateTimers = false;

    static bool logicallyHeld(const Voice &voice) noexcept
    {
        return voice.active && !voice.releasing && !voice.noteReleased;
    }
    Voice *foreground(size_t slot, std::uint64_t order) noexcept
    {
        if (slot >= capacity)
            return nullptr;
        auto &voice = voices[slot];
        return voice.active && voice.triggerSerial == order ? &voice : nullptr;
    }
    static TransitionTarget targetOf(const Voice &voice) noexcept
    {
        return {std::abs(voice.targetDelta), double(voice.expression.right - voice.expression.left),
                voice.expression.filterCutoffOffset, voice.expression.filterResonanceOffset};
    }
    static TransitionTarget audibleOf(const Voice &voice) noexcept
    {
        return {std::abs(voice.delta), double(voice.rightGain - voice.leftGain),
                voice.glide.frames ? voice.glide.current.cutoff : voice.expression.filterCutoffOffset,
                voice.glide.frames ? voice.glide.current.resonance : voice.expression.filterResonanceOffset};
    }
    void releaseHeld(int colourGroup) noexcept
    {
        for (auto &voice : voices)
            if (logicallyHeld(voice) && voice.colourGroup == colourGroup)
                releaseVoice(voice, true);
    }
    void admitStandalone(TransitionSeed &seed, int colourGroup, bool porta) noexcept
    {
        expireGates();
        const auto &mode = settings.values;
        auto &memory = foregroundGroups[size_t(colourGroup)];
        auto *previous = foreground(memory.slot, memory.serial);
        if (previous && previous->colourGroup != colourGroup)
        {
            previous = nullptr;
            memory.valid = false; // A live recolor must not leave cross-group gap memory.
        }
        for (auto &candidate : voices)
            if (candidate.active && candidate.colourGroup == colourGroup &&
                (!previous || candidate.triggerSerial > previous->triggerSerial))
                previous = &candidate;
        if (previous && (porta || (mode.mono && logicallyHeld(*previous))))
            seed = {true, audibleOf(*previous)};
        else if (porta && memory.valid)
            seed = {true, memory.target};
        if (mode.mono)
            releaseHeld(colourGroup);
        const auto limit = mode.mono || mode.maxPoly == 0 ? int(capacity) : mode.maxPoly;
        while (activeVoices() >= limit)
            stealOldest();
    }
    void beginTransition(Voice &voice, const TransitionSeed &seed) noexcept
    {
        if (!seed.valid || settings.values.slideTime <= 0 || seed.value == targetOf(voice))
            return;
        voice.glide.from = voice.glide.current = seed.value;
        voice.glide.target = targetOf(voice);
        voice.glide.frames = std::uint64_t(std::max(1., std::floor(settings.values.slideTime * rate + .5)));
        voice.delta = voice.direction * seed.value.rate;
        voice.leftGain = float(1. - std::max(0., seed.value.pan));
        voice.rightGain = float(1. + std::min(0., seed.value.pan));
        voice.filter.configure(
            filterPreparation, settings.values.filterType,
            samplerFilterModulatedValue(settings.values.filterCutoff, seed.value.cutoff),
            samplerFilterModulatedValue(settings.values.filterResonance, seed.value.resonance), true);
    }
    bool advanceTransition(Voice &voice) noexcept
    {
        if (!voice.glide.frames)
            return false;
        auto &glide = voice.glide;
        const auto target = targetOf(voice);
        if (target != glide.target)
        {
            // Ordinary edits start from the audible base, while keeping the
            // original absolute completion interval. Identical publication is inert.
            glide.from = glide.current = audibleOf(voice);
            glide.target = target;
            glide.segmentStart = glide.elapsed;
            if (glide.velocityControl)
                glide.fromGain = voice.gain;
        }
        const double fraction =
            double(++glide.elapsed - glide.segmentStart) / double(glide.frames - glide.segmentStart);
        const auto interpolate = [fraction](double from, double to) noexcept
        { return from + fraction * (to - from); };
        glide.current = glide.elapsed == glide.frames
                            ? target
                            : TransitionTarget{interpolate(glide.from.rate, target.rate),
                                               interpolate(glide.from.pan, target.pan),
                                               interpolate(glide.from.cutoff, target.cutoff),
                                               interpolate(glide.from.resonance, target.resonance)};
        voice.delta = voice.direction * glide.current.rate;
        voice.leftGain = float(1. - std::max(0., glide.current.pan));
        voice.rightGain = float(1. + std::min(0., glide.current.pan));
        if (glide.velocityControl)
            voice.gain = float(glide.fromGain + fraction * (voice.targetGain - glide.fromGain));
        voice.filter.configure(
            filterPreparation, settings.values.filterType,
            samplerFilterModulatedValue(settings.values.filterCutoff, glide.current.cutoff),
            samplerFilterModulatedValue(settings.values.filterResonance, glide.current.resonance));
        if (glide.elapsed == glide.frames)
            glide.frames = 0;
        return true;
    }
    void applyGate(Voice &voice) noexcept
    {
        if (voice.gateBeats <= 0 || voice.noteReleased)
            return;
        if (voice.gateTempo != settings.tempo)
        {
            voice.gateElapsedBeats += double(voice.gateFrames) * voice.gateTempo / (60. * rate);
            voice.gateFrames = 0;
            voice.gateTempo = settings.tempo;
            const double remainingFrames =
                (voice.gateBeats - voice.gateElapsedBeats) * (60. * rate) / voice.gateTempo;
            voice.gateDeadline = std::uint64_t(std::max(0., std::floor(remainingFrames + .5)));
        }
        // Nearest-frame event membership, without accumulated per-sample rounding error.
        if (voice.gateFrames >= voice.gateDeadline)
            releaseVoice(voice, !voice.oneShot);
    }
    void expireGates() noexcept
    {
        if (!gateTimers)
            return;
        bool pending = false;
        for (auto &voice : voices)
            if (voice.active && voice.gateBeats > 0 && !voice.noteReleased)
            {
                applyGate(voice);
                pending = pending || !voice.noteReleased;
            }
        gateTimers = pending;
    }
    // Logical key-off releases modulation even while one-shot source amplitude continues.
    void releaseVoice(Voice &voice, bool releaseSource) noexcept
    {
        if (!voice.noteReleased && voice.slide.retained)
        {
            // A logical off ends participation without moving the source phase.
            // Freeze the reached control endpoint through the captured release tail.
            voice.targetDelta = voice.delta;
            voice.slide.pitchRatio = std::abs(voice.delta) / (delta(voice.pitch) * voice.expression.tuning);
            voice.expression.left = voice.leftGain;
            voice.expression.right = voice.rightGain;
            if (voice.glide.frames)
            {
                voice.expression.filterCutoffOffset = voice.glide.current.cutoff;
                voice.expression.filterResonanceOffset = voice.glide.current.resonance;
            }
            voice.targetGain = voice.gain;
            voice.glide = {};
        }
        voice.noteReleased = true;
        voice.modulation.beginRelease(modulationPreparation);
        if (releaseSource)
            voice.releasing = true;
    }

  public:
    VoiceBank() = default;
    VoiceBank(const VoiceBank &) = delete;
    VoiceBank &operator=(const VoiceBank &) = delete;
    VoiceBank(VoiceBank &&) = delete;
    VoiceBank &operator=(VoiceBank &&) = delete;
    void beginModulationBlock(const SamplerModulationPhases &phases, int start) noexcept
    {
        sharedGlobalPhases = &phases;
        sharedGlobalStart = start;
        automationBlock = nullptr;
        selectedModulationFrame = -1;
        modulationHealthy = true;
    }
    void attachModulationBlock(SamplerModulationAutomationBlock &block, int start) noexcept
    {
        automationBlock = &block;
        sharedGlobalStart = start;
        selectedModulationFrame = -1;
        modulationHealthy = true;
        sharedGlobalPhases = nullptr;
    }
    // Select BEFORE off/on/chase/reconciliation, even if preceding render had zero length.
    bool selectModulationFrame(int absoluteFrame) noexcept
    {
        if (!automationBlock)
            return true;
        const int frame = absoluteFrame - sharedGlobalStart;
        if (frame == selectedModulationFrame)
            return modulationHealthy;
        const auto &previousVolume = settings.values.modulation[0];
        const bool previousGate = previousVolume.enabled && previousVolume.amount != 0;
        modulationHealthy = automationBlock->select(frame, modulationPreparation, settings.values.modulation);
        if (!modulationHealthy)
            return false;
        selectedModulationFrame = frame;
        const auto &volume = settings.values.modulation[0];
        if (previousGate != (volume.enabled && volume.amount != 0))
            reconcileModulationGate();
        else
            modulationActive = std::any_of(
                settings.values.modulation.begin(), settings.values.modulation.end(), [](const auto &m)
                { return (m.enabled && m.amount != 0) || (m.lfoEnabled && m.lfoAmount != 0); });
        return true;
    }
    bool modulationBlockHealthy() const noexcept
    {
        return modulationHealthy;
    }
    const PreparedSamplerModulation &preparedModulation() const noexcept
    {
        return modulationPreparation;
    }
    void endModulationBlock() noexcept
    {
        sharedGlobalPhases = nullptr;
        automationBlock = nullptr;
        selectedModulationFrame = -1;
    }
    const SamplerModulationPhases &modulationIncrements() const noexcept
    {
        return modulationPreparation.globalIncrements;
    }
    void bindBudget(void *owner, void (*admit)(void *, VoiceBank &, TransitionSeed &, int, bool) noexcept,
                    void (*started)(void *, VoiceBank &, size_t) noexcept, std::uint64_t &order) noexcept
    {
        budget = owner;
        beforeNote = admit;
        afterNote = started;
        serial = &order;
    }
    int activeVoices() const noexcept
    {
        return int(std::count_if(voices.begin(), voices.end(), [](const auto &v) { return v.active; }));
    }
    std::uint64_t oldestSerial() const noexcept
    {
        auto oldest = std::numeric_limits<std::uint64_t>::max();
        for (const auto &v : voices)
            if (v.active)
                oldest = std::min(oldest, v.triggerSerial);
        return oldest;
    }
    void stealOldest() noexcept
    {
        const auto oldest = oldestSerial();
        for (auto &v : voices)
            if (v.active && v.triggerSerial == oldest)
            {
                v.active = false;
                return;
            }
    }
    void releaseConfigured(std::uint64_t beforeSequence = std::numeric_limits<std::uint64_t>::max()) noexcept
    {
        for (auto &v : voices)
            if (v.active && (v.liveSequence == 0 || v.liveSequence < beforeSequence))
                releaseVoice(v, true); // Keep each captured amplitude release, including one-shots.
    }
    void prepare(const Sample &s, double sampleRate) noexcept
    {
        sample = &s;
        rate = sampleRate;
        filterPreparation.prepare(rate);
        modulationPreparation.prepare(rate, 120, SamplerModulationArray{});
        modulationActive = false;
        localGlobalPhases = {};
        sharedGlobalPhases = nullptr;
        automationBlock = nullptr;
        selectedModulationFrame = -1;
        modulationHealthy = true;
        modulationPitch = SamplerModulationTables::prepareOnControl().pitch.data();
        for (size_t pitch = 0; pitch < pitchDeltas.size(); ++pitch)
            pitchDeltas[pitch] =
                std::pow(2., (int(pitch) - sample->rootNote) / 12.) * sample->sampleRate / rate;
        for (size_t root = 0; root < rootRatios.size(); ++root)
            rootRatios[root] = std::pow(2., (sample->rootNote - int(root)) / 12.);
        settings = PreparedSamplerSettings{};
        reset();
    }
    // Fixed-size copy/arithmetic only. Existing voices keep source bounds/envelope,
    // while pitch approaches the new target without resetting their phase/position.
    void configure(const PreparedSamplerSettings &next,
                   const PreparedSamplerModulation *effective = nullptr) noexcept
    {
        settings = next;
        if (effective)
        {
            modulationPreparation = *effective;
            settings.values.modulation = effective->settings;
        }
        else
            modulationPreparation.configure(next.tempo, next.values.modulation);
        modulationActive = std::any_of(
            settings.values.modulation.begin(), settings.values.modulation.end(),
            [](const auto &m) { return (m.enabled && m.amount != 0) || (m.lfoEnabled && m.lfoAmount != 0); });
        const auto &volume = settings.values.modulation[0];
        const bool modulationGate = volume.enabled && volume.amount != 0;
        for (auto &v : voices)
            if (v.active)
            {
                v.oneShot = sample->oneShot && !v.settings.envelope && !v.settings.loop && !modulationGate;
                // A previously consumed off remains authoritative when live Volume enables its gate.
                if (v.noteReleased && !v.oneShot)
                    v.releasing = true;
                v.targetDelta = v.direction * delta(v.pitch) * v.expression.tuning * v.slide.pitchRatio;
                v.targetNormalizationGain = settings.values.normalize ? sample->normalizationGain : 1.;
                v.filter.configure(
                    filterPreparation, settings.values.filterType,
                    samplerFilterModulatedValue(settings.values.filterCutoff,
                                                v.glide.frames ? v.glide.current.cutoff
                                                               : v.expression.filterCutoffOffset),
                    samplerFilterModulatedValue(settings.values.filterResonance,
                                                v.glide.frames ? v.glide.current.resonance
                                                               : v.expression.filterResonanceOffset));
            }
    }
    void reset() noexcept
    {
        for (auto &v : voices)
            v = {};
        steal = 0;
        foregroundGroups = {};
        gateTimers = false;
    }
    void releaseAllQuickly() noexcept
    {
        for (auto &voice : voices)
            if (voice.active)
            {
                releaseVoice(voice, true);
                voice.forcedQuickRelease = true;
                voice.settings.envelope = false; // Bounded legacy 12 ms release; only the caller's bank.
            }
    }
    void noteOn(int pitch, float velocity, std::int64_t note = 0, std::int64_t placement = 0,
                double onset = 0, std::uint64_t liveSequence = 0,
                const PreparedNoteExpression &expression = {}, std::uint32_t repeatOrdinal = 0,
                bool notePortamento = false, int colourGroup = 0) noexcept
    {
        if (!sample || sample->data.getNumSamples() < 2)
            return;
        if (colourGroup < 0 || colourGroup > 15)
            return;
        const bool porta = settings.values.portamento != notePortamento;
        TransitionSeed transition;
        if (beforeNote)
            beforeNote(budget, *this, transition, colourGroup, porta);
        else
            admitStandalone(transition, colourGroup, porta);
        Voice *chosen = nullptr;
        for (auto &v : voices)
            if (!v.active)
            {
                chosen = &v;
                break;
            }
        if (!chosen)
            chosen = &voices[steal++ % voices.size()];
        *chosen = {};
        chosen->triggerSerial = ++*serial;
        chosen->liveSequence = liveSequence;
        chosen->active = true;
        chosen->pitch = pitch;
        chosen->colourGroup = colourGroup;
        chosen->sourceExpression = expression;
        chosen->sourceVelocity = velocity;
        chosen->gain = chosen->targetGain = velocity;
        chosen->expression = expression;
        chosen->leftGain = expression.left;
        chosen->rightGain = expression.right;
        chosen->normalizationGain = chosen->targetNormalizationGain =
            settings.values.normalize ? sample->normalizationGain : 1.;
        chosen->note = note;
        chosen->placement = placement;
        chosen->repeatOrdinal = repeatOrdinal;
        chosen->onset = onset;
        chosen->settings = {settings.values.envelope, settings.values.loop,  settings.values.pingPong,
                            settings.values.attack,   settings.values.decay, settings.values.sustain,
                            settings.values.release};
        chosen->modulation.noteOn(modulationPreparation);
        chosen->modulationUsed = modulationActive;
        chosen->filter.configure(
            filterPreparation, settings.values.filterType,
            samplerFilterModulatedValue(settings.values.filterCutoff, expression.filterCutoffOffset),
            samplerFilterModulatedValue(settings.values.filterResonance, expression.filterResonanceOffset),
            true);
        const auto &volume = settings.values.modulation[0];
        const bool modulationGate = volume.enabled && volume.amount != 0;
        chosen->oneShot =
            sample->oneShot && !settings.values.envelope && !settings.values.loop && !modulationGate;
        chosen->begin = std::floor(settings.values.start * (sample->data.getNumSamples() - 1));
        chosen->end =
            std::max(chosen->begin + 1., std::ceil(settings.values.end * (sample->data.getNumSamples() - 1)));
        chosen->position = settings.values.reverse ? chosen->end - 1 : chosen->begin;
        chosen->loopBegin = std::max(
            chosen->begin, std::floor(settings.values.loopStart * (sample->data.getNumSamples() - 1)));
        chosen->loopEnd = std::min(
            chosen->end, std::max(chosen->loopBegin + 1.,
                                  std::ceil(settings.values.loopEnd * (sample->data.getNumSamples() - 1))));
        chosen->direction = settings.values.reverse ? -1 : 1;
        chosen->reversed = settings.values.reverse;
        chosen->fadeInFrames = settings.values.fadeIn * sample->sampleRate;
        chosen->fadeOutFrames = settings.values.fadeOut * sample->sampleRate;
        {
            // Forward loops borrow source before loopBegin; reverse loops borrow after loopEnd.
            const double span = chosen->loopEnd - chosen->loopBegin;
            const double available =
                settings.values.reverse ? chosen->end - 1. - chosen->loopEnd : chosen->loopBegin - chosen->begin;
            const double frames = std::floor(std::min({settings.values.loopCrossfade * sample->sampleRate,
                                                       span / 2., available}));
            chosen->crossfadeFrames = frames >= 1 ? frames : 0;
        }
        chosen->delta = chosen->targetDelta =
            (settings.values.reverse ? -1. : 1.) * delta(pitch) * expression.tuning;
        chosen->gateBeats = settings.values.fullPortamento && porta ? 0. : settings.values.gateBeats;
        chosen->gateTempo = settings.tempo;
        chosen->gateDeadline = std::uint64_t(
            std::max(0., std::floor(chosen->gateBeats * (60. * rate) / chosen->gateTempo + .5)));
        gateTimers = gateTimers || chosen->gateBeats > 0;
        beginTransition(*chosen, transition);
        const auto slot = size_t(chosen - voices.data());
        foregroundGroups[size_t(colourGroup)] = {slot, chosen->triggerSerial, targetOf(*chosen), true};
        if (afterNote)
            afterNote(budget, *this, slot);
    }
    // Stable occurrence reconciliation: keep phase, envelope, source and trigger
    // ownership. The caller already matched note/placement/onset/pitch.
    void retargetExpression(Voice &voice, const PreparedNoteExpression &expression) noexcept
    {
        voice.sourceExpression = expression;
        voice.expression = expression;
        voice.targetDelta = voice.direction * delta(voice.pitch) * expression.tuning;
        voice.filter.configure(
            filterPreparation, settings.values.filterType,
            samplerFilterModulatedValue(settings.values.filterCutoff, voice.glide.frames
                                                                          ? voice.glide.current.cutoff
                                                                          : expression.filterCutoffOffset),
            samplerFilterModulatedValue(settings.values.filterResonance,
                                        voice.glide.frames ? voice.glide.current.resonance
                                                           : expression.filterResonanceOffset));
    }
    void reconcileScore(Voice &voice, float velocity, const PreparedNoteExpression &expression,
                        int colourGroup) noexcept
    {
        if (voice.sourceVelocity == velocity && voice.sourceExpression == expression &&
            voice.colourGroup == colourGroup)
            return; // Unrelated immutable publication must not undo an admitted Slide.
        voice.sourceVelocity = velocity;
        voice.colourGroup = colourGroup;
        if (voice.slide.retained)
            voice.glide = {}; // Keep audible values, then use existing ordinary smoothing.
        voice.slide = {};
        voice.targetGain = velocity;
        retargetExpression(voice, expression);
    }
    double slideReference(int colourGroup) noexcept
    {
        expireGates();
        double highest = 0;
        for (const auto &voice : voices)
            if (logicallyHeld(voice) && voice.colourGroup == colourGroup)
                highest = std::max(highest, std::abs(voice.delta));
        return highest;
    }
    void applySlide(std::int64_t note, std::int64_t placement, double start, int pitch, float velocity,
                    const PreparedNoteExpression &expression, int colourGroup, double reference,
                    double duration, bool retarget = false) noexcept
    {
        if (!(reference > 0) || !std::isfinite(reference))
            return;
        const double factor = delta(pitch) * expression.tuning / reference;
        const auto frames = std::uint64_t(std::max(1., std::floor(duration * rate + .5)));
        for (auto &voice : voices)
            if (logicallyHeld(voice) && voice.colourGroup == colourGroup &&
                (!retarget || (voice.slide.retained && voice.slide.note == note &&
                               voice.slide.placement == placement && voice.slide.start == start)))
            {
                const auto audible = audibleOf(voice);
                const auto baseRate = retarget ? voice.slide.baseRate : audible.rate;
                voice.slide = {true,  note,     placement,
                               start, baseRate, baseRate * factor / (delta(voice.pitch) * expression.tuning)};
                voice.expression = expression;
                voice.targetDelta = voice.direction * baseRate * factor;
                voice.targetGain = velocity;
                voice.glide = {};
                voice.glide.from = voice.glide.current = audible;
                voice.glide.target = targetOf(voice);
                voice.glide.frames = frames;
                voice.glide.velocityControl = true;
                voice.glide.fromGain = voice.gain;
            }
    }
    void cancelSlide(std::int64_t note, std::int64_t placement, double start) noexcept
    {
        for (auto &voice : voices)
            if (logicallyHeld(voice) && voice.slide.retained && voice.slide.note == note &&
                voice.slide.placement == placement && voice.slide.start == start)
            {
                voice.slide = {};
                voice.glide = {};
                voice.targetGain = voice.sourceVelocity;
                retargetExpression(voice, voice.sourceExpression);
            }
    }
    void releaseTagged(std::int64_t note, std::int64_t placement, bool force = false,
                       std::uint32_t repeatOrdinal = 0) noexcept
    {
        for (auto &v : voices)
            if (v.active && v.note == note && v.placement == placement && v.repeatOrdinal == repeatOrdinal)
                releaseVoice(v, force || !v.oneShot);
    }
    template <class F> void visitActive(F &&fn) noexcept
    {
        for (auto &v : voices)
            if (v.active && !v.releasing)
                fn(v);
    }
    void noteOff(int pitch, int colourGroup = 0) noexcept
    {
        for (auto &v : voices)
            if (v.active && !v.releasing && !v.noteReleased && v.pitch == pitch &&
                v.colourGroup == colourGroup)
            {
                releaseVoice(v, !v.oneShot);
                break;
            }
    }
    void render(juce::AudioBuffer<float> &buffer, int start, int count) noexcept
    {
        if (!sample || count <= 0)
            return;
        if (std::none_of(voices.begin(), voices.end(), [](const auto &voice) { return voice.active; }))
        {
            // The common clock already owns every frame, even with no active voices.
            // Keep only this bank's effective endpoint; explicit event preselection handles later notes.
            if (!selectModulationFrame(start + count - 1))
                return;
            advanceLocalModulationClock(count);
            return;
        }
        const int size = sample->data.getNumSamples();
        for (int i = 0; i < count; ++i)
        {
            if (!selectModulationFrame(start + i))
                return; // No later prefix/event processing after refusal.
            float l = 0, r = 0;
            SamplerModulationPhases global{};
            const auto &origin = sharedGlobalPhases ? *sharedGlobalPhases : localGlobalPhases;
            const auto offset = sharedGlobalPhases ? start + i - sharedGlobalStart : i;
            if (automationBlock)
            {
                if (!automationBlock->readPhases(start + i - sharedGlobalStart, global))
                {
                    modulationHealthy = false;
                    return;
                }
            }
            else if (modulationActive)
                for (size_t target = 0; target < global.size(); ++target)
                    global[target] = PreparedSamplerModulation::wrap(
                        origin[target] + offset * modulationPreparation.globalIncrements[target]);
            for (auto &v : voices)
                if (v.active)
                {
                    applyGate(v);
                    const bool transitioning = advanceTransition(v);
                    auto index = int(v.position);
                    if (index < 0 || index >= size - 1 || v.position < v.begin || v.position >= v.end)
                    {
                        v.active = false;
                        continue;
                    }
                    const float f = float(v.position - index);
                    SamplerModulationFrame modulation;
                    if (v.releasing)
                        releaseVoice(v, true); // Also normalizes existing direct reconciliation releases.
                    v.modulationUsed = v.modulationUsed || modulationActive;
                    if (v.modulationUsed)
                        modulation = v.modulation.tick(modulationPreparation, global);
                    else
                        v.modulation.advanceInactive(modulationPreparation);
                    if (modulation.volumeEnvelopeActive && !v.forcedQuickRelease)
                    {
                        v.envelope = float(std::min(1., v.modulation.ageSeconds() / .002));
                        if (modulation.volumeReleaseFinished)
                        {
                            if (settings.values.modulation[0].amount == 1)
                            {
                                if (modulation.volumeMultiplier == 0)
                                    v.envelope = 0;
                            }
                            else
                            {
                                v.modulationTail = std::max(0., v.modulationTail - 1. / (.012 * rate));
                                v.envelope *= float(v.modulationTail);
                            }
                        }
                        v.age += 1. / rate;
                    }
                    else if (!v.settings.envelope)
                        v.envelope = v.releasing ? std::max(0.f, v.envelope - float(1. / (.012 * rate)))
                                                 : std::min(1.f, v.envelope + float(1. / (.002 * rate)));
                    else if (v.releasing)
                    {
                        if (v.releaseLevel < 0)
                            v.releaseLevel = v.envelope;
                        v.envelope = v.settings.release == 0
                                         ? 0.f
                                         : std::max(0.f, v.envelope - float(v.releaseLevel /
                                                                            (v.settings.release * rate)));
                    }
                    else
                    {
                        if (v.settings.attack > 0 && v.age < v.settings.attack)
                            v.envelope = float(std::min(1., (v.age + 1. / rate) / v.settings.attack));
                        else if (v.settings.decay > 0 && v.age < v.settings.attack + v.settings.decay)
                            v.envelope = float(1. - (1. - v.settings.sustain) *
                                                        std::min(1., (v.age - v.settings.attack + 1. / rate) /
                                                                         v.settings.decay));
                        else
                            v.envelope = float(v.settings.sustain);
                        v.age += 1. / rate;
                    }
                    if (v.releasing && v.envelope <= 0)
                    {
                        v.active = false;
                        continue;
                    }
                    auto read = [&](int c)
                    {
                        const auto *data = sample->data.getReadPointer(c);
                        const int right = v.settings.loop && !v.settings.pingPong && !v.releasing &&
                                                  index + 1 >= v.loopEnd && v.position >= v.loopBegin &&
                                                  v.position < v.loopEnd
                                              ? int(v.loopBegin)
                                              : index + 1;
                        // Interpolate in double before normalization, including very quiet or
                        // finite float PCM above unity. Avoid intermediate float overflow.
                        if (!settings.values.hqInterpolation)
                            return double(data[index]) + double(f) * (double(data[right]) - double(data[index]));
                        // 4-point Hermite: neighbours wrap like `right` inside a forward loop, else clamp.
                        const int previous = std::max(0, index - 1);
                        const int after = std::min(size - 1, right + 1);
                        const double y0 = data[previous], y1 = data[index], y2 = data[right], y3 = data[after];
                        const double t = f;
                        const double c1 = .5 * (y2 - y0);
                        const double c2 = y0 - 2.5 * y1 + 2. * y2 - .5 * y3;
                        const double c3 = .5 * (y3 - y0) + 1.5 * (y1 - y2);
                        return ((c3 * t + c2) * t + c1) * t + y1;
                    };
                    // Smooth live velocity changes over roughly 5 ms without retriggering.
                    if (!(transitioning && v.glide.velocityControl))
                        v.gain += (v.targetGain - v.gain) * float(1. / (.005 * rate));
                    v.normalizationGain +=
                        (v.targetNormalizationGain - v.normalizationGain) * std::min(1., 1. / (.005 * rate));
                    if (!transitioning)
                    {
                        v.leftGain += (v.expression.left - v.leftGain) * float(1. / (.005 * rate));
                        v.rightGain += (v.expression.right - v.rightGain) * float(1. / (.005 * rate));
                    }
                    double left = read(0), right = read(1);
                    if (v.crossfadeFrames > 0 && v.settings.loop && !v.settings.pingPong && !v.releasing)
                    {
                        const double span = v.loopEnd - v.loopBegin;
                        const double weight =
                            v.direction > 0 ? (v.position - (v.loopEnd - v.crossfadeFrames)) / v.crossfadeFrames
                                            : ((v.loopBegin + v.crossfadeFrames) - v.position) / v.crossfadeFrames;
                        if (weight > 0 && weight <= 1)
                        {
                            const double other = v.position + (v.direction > 0 ? -span : span);
                            const int at = int(other);
                            if (at >= 0 && at + 1 < size)
                            {
                                const double g = other - at;
                                auto blend = [&](int c)
                                {
                                    const auto *data = sample->data.getReadPointer(c);
                                    return double(data[at]) + g * (double(data[at + 1]) - double(data[at]));
                                };
                                left += weight * (blend(0) - left);
                                right += weight * (blend(1) - right);
                            }
                        }
                    }
                    if (v.fadeInFrames > 0 || v.fadeOutFrames > 0)
                    {
                        const double last = v.end - 1.;
                        const double fromStart = v.reversed ? last - v.position : v.position - v.begin;
                        const double toEnd = v.reversed ? v.position - v.begin : last - v.position;
                        double fade = 1;
                        if (v.fadeInFrames > 0)
                            fade *= std::clamp(fromStart / v.fadeInFrames, 0., 1.);
                        if (v.fadeOutFrames > 0)
                            fade *= std::clamp(toEnd / v.fadeOutFrames, 0., 1.);
                        left *= fade;
                        right *= fade;
                    }
                    if (v.modulationUsed)
                    {
                        v.filter.configure(
                            filterPreparation, settings.values.filterType,
                            std::clamp(settings.values.filterCutoff +
                                           (v.glide.frames ? v.glide.current.cutoff
                                                           : v.expression.filterCutoffOffset) +
                                           modulation.output[2],
                                       0., 1.),
                            std::clamp(settings.values.filterResonance +
                                           (v.glide.frames ? v.glide.current.resonance
                                                           : v.expression.filterResonanceOffset) +
                                           modulation.output[3],
                                       0., 1.));
                        left *= modulation.volumeMultiplier *
                                (modulation.output[1] > 0 ? 1. - std::min(1., modulation.output[1]) : 1.);
                        right *= modulation.volumeMultiplier *
                                 (modulation.output[1] < 0 ? 1. + std::max(-1., modulation.output[1]) : 1.);
                    }
                    v.filter.process(left, right, filterPreparation);
                    l += float(left * v.normalizationGain * v.gain * v.envelope * v.leftGain);
                    r += float(right * v.normalizationGain * v.gain * v.envelope * v.rightGain);
                    if (!transitioning)
                        v.delta += (v.targetDelta - v.delta) * std::min(1., 1. / (.005 * rate));
                    if (v.gateBeats > 0 && !v.noteReleased)
                        ++v.gateFrames;
                    v.position += v.delta * bend *
                                  (v.modulationUsed ? modulationPitchRatio(modulation.output[4]) : 1.);
                    if (v.settings.loop && !v.releasing)
                    {
                        if (v.settings.pingPong)
                        {
                            const double span = v.loopEnd - v.loopBegin - 1.;
                            if (span <= 0 && ((v.direction > 0 && v.position >= v.loopEnd) ||
                                              (v.direction < 0 && v.position < v.loopBegin)))
                                v.position = v.loopBegin;
                            else if (span > 0 && ((v.direction > 0 && v.position > v.loopEnd - 1.) ||
                                                  (v.direction < 0 && v.position < v.loopBegin)))
                            {
                                double phase = std::fmod(v.position - v.loopBegin, 2. * span);
                                if (phase < 0)
                                    phase += 2. * span;
                                const int incoming = v.direction;
                                v.position = v.loopBegin + (phase <= span ? phase : 2. * span - phase);
                                v.direction = phase == 0      ? 1
                                              : phase == span ? -1
                                                              : incoming * (phase < span ? 1 : -1);
                                if (v.direction != incoming)
                                {
                                    v.delta = -v.delta;
                                    v.targetDelta = -v.targetDelta;
                                }
                            }
                        }
                        else if ((v.direction > 0 && v.position >= v.loopEnd) ||
                                 (v.direction < 0 && v.position < v.loopBegin))
                        {
                            const double length = v.loopEnd - v.loopBegin;
                            double phase = std::fmod(v.position - v.loopBegin, length);
                            if (phase < 0)
                                phase += length;
                            v.position = v.loopBegin + phase;
                        }
                    }
                }
            buffer.addSample(0, start + i, l);
            if (buffer.getNumChannels() > 1)
                buffer.addSample(1, start + i, r);
        }
        advanceLocalModulationClock(count);
    }
};
// One Channel budget across independent score/MIDI/typing/preview ownership.
// No cross-instrument state, allocation, locks or asynchronous destruction.
class SamplerVoiceBudget
{
    std::array<VoiceBank *, 4> banks;
    std::uint64_t serial = 0;
    int maximum = int(VoiceBank::capacity);
    SamplerModulationAutomationBlock modulationBlock;
    bool blockStarted = false;
    bool automationHealthy = true;
    bool wasAutomated = false; // Sole DSP owner; restore manual base once on Stop/binding exit.
    struct GroupMemory
    {
        VoiceBank *bank = nullptr;
        size_t slot = VoiceBank::capacity;
        std::uint64_t serial = 0;
        VoiceBank::TransitionTarget target;
        bool valid = false;
    };
    std::array<GroupMemory, 16> foregroundGroups{};
    static void admit(void *self, VoiceBank &incoming, VoiceBank::TransitionSeed &seed, int colourGroup,
                      bool porta) noexcept
    {
        auto &owner = *static_cast<SamplerVoiceBudget *>(self);
        const auto &mode = incoming.settings.values;
        for (auto *bank : owner.banks)
            bank->expireGates(); // Includes exact event-boundary Gate/off before any new admission.
        auto &memory = owner.foregroundGroups[size_t(colourGroup)];
        auto *previous = memory.bank ? memory.bank->foreground(memory.slot, memory.serial) : nullptr;
        if (previous && previous->colourGroup != colourGroup)
        {
            previous = nullptr;
            memory.valid = false;
        }
        for (auto *bank : owner.banks)
            for (auto &candidate : bank->voices)
                if (candidate.active && candidate.colourGroup == colourGroup &&
                    (!previous || candidate.triggerSerial > previous->triggerSerial))
                    previous = &candidate;
        if (previous && (porta || (mode.mono && VoiceBank::logicallyHeld(*previous))))
            seed = {true, VoiceBank::audibleOf(*previous)};
        else if (porta && memory.valid)
            seed = {true, memory.target};
        // Entering Mono after polyphonic playback releases every held predecessor,
        // without converting the user Max-poly limit into an immediate hard cut.
        if (mode.mono)
            for (auto *bank : owner.banks)
                bank->releaseHeld(colourGroup);
        owner.enforce(-1, mode.mono ? int(VoiceBank::capacity) : owner.maximum);
    }
    static void started(void *self, VoiceBank &bank, size_t slot) noexcept
    {
        auto &owner = *static_cast<SamplerVoiceBudget *>(self);
        const auto &voice = bank.voices[slot];
        owner.foregroundGroups[size_t(voice.colourGroup)] = {&bank, slot, voice.triggerSerial,
                                                             VoiceBank::targetOf(voice), true};
    }
    void enforce(int incoming, int limit) noexcept
    {
        for (int remaining = int(4 * VoiceBank::capacity); remaining > 0; --remaining)
        {
            int count = 0;
            VoiceBank *oldest = nullptr;
            auto age = std::numeric_limits<std::uint64_t>::max();
            for (auto *bank : banks)
            {
                count += bank->activeVoices();
                if (bank->oldestSerial() < age)
                {
                    age = bank->oldestSerial();
                    oldest = bank;
                }
            }
            if (count <= limit + incoming || !oldest)
                break;
            oldest->stealOldest(); // Max poly is immediate stealing, not Mono/portamento.
        }
    }

  public:
    // Only actual source/device/global-panic retirement clears gap memory.
    // A score-only Stop/reset must leave another bank's held foreground intact.
    void resetTransitions() noexcept
    {
        foregroundGroups = {};
    }
    bool prepareModulationAutomation(int capacity) noexcept
    {
        return modulationBlock.prepare(capacity);
    }
    SamplerModulationAutomationBlock::Status modulationStatus() const noexcept
    {
        return modulationBlock.status();
    }
    const SamplerModulationPhases &committedModulationPhases() const noexcept
    {
        return modulationBlock.committedPhases();
    }
    bool beginModulationBlock(int start, int count) noexcept
    {
        if (blockStarted)
        {
            modulationBlock.reject();
            automationHealthy = false;
            return false;
        }
        if (count == 0)
            return true;
        blockStarted = modulationBlock.beginManual(banks[0]->preparedModulation(), count);
        automationHealthy = blockStarted;
        if (blockStarted)
            for (auto *bank : banks)
                bank->beginModulationBlock(modulationBlock.committedPhases(), start);
        return blockStarted;
    }
    bool beginAutomatedModulationBlock(const PreparedSamplerSettings &base,
                                       std::span<const SamplerModulationAutomationLane> lanes,
                                       double firstBeat, double beatStep, int start, int count,
                                       bool generationChanged) noexcept
    {
        if (count < 1 || blockStarted)
        {
            modulationBlock.reject();
            return false;
        }
        if (lanes.empty())
        {
            if (!generationChanged && !wasAutomated)
                return beginModulationBlock(start, count); // Cheap steady manual path.
            auto manual = banks[0]->preparedModulation();
            if (!manual.configure(base.tempo, base.values.modulation))
            {
                modulationBlock.reject();
                return false;
            }
            blockStarted = modulationBlock.beginManual(manual, count);
            automationHealthy = blockStarted;
            if (!blockStarted)
                return false;
            // Admit clock first, then atomically restore complete base before any live input.
            for (auto *bank : banks)
            {
                bank->configure(base, &manual);
                bank->beginModulationBlock(modulationBlock.committedPhases(), start);
            }
            wasAutomated = false;
            return true;
        }
        // Fixed copy/validation once per active-automation block, never per voice/frame.
        auto manual = banks[0]->preparedModulation();
        if (!manual.configure(base.tempo, base.values.modulation))
        {
            modulationBlock.reject();
            return false;
        }
        blockStarted = modulationBlock.begin(manual, lanes, firstBeat, beatStep, count);
        automationHealthy = blockStarted;
        if (!blockStarted)
            return false;
        auto effective = manual;
        auto values = manual.settings;
        if (!modulationBlock.select(0, effective, values))
        {
            modulationBlock.finish(false);
            blockStarted = false;
            return false;
        }
        wasAutomated = true;
        for (auto *bank : banks)
        {
            if (generationChanged)
                bank->configure(base, &effective);
            bank->attachModulationBlock(modulationBlock, start);
            if (!bank->selectModulationFrame(start))
                automationHealthy = false;
        }
        if (!automationHealthy)
        {
            endModulationBlock();
            return false;
        }
        return true;
    }
    bool selectModulationFrame(int frame) noexcept
    {
        for (auto *bank : banks)
            if (!bank->selectModulationFrame(frame))
                automationHealthy = false;
        return automationHealthy;
    }
    bool endModulationBlock() noexcept
    {
        if (!blockStarted)
            return automationHealthy;
        for (auto *bank : banks)
        {
            automationHealthy = automationHealthy && bank->modulationBlockHealthy();
            bank->endModulationBlock();
        }
        const bool ok = modulationBlock.finish(automationHealthy);
        blockStarted = false;
        return ok;
    }
    SamplerVoiceBudget(VoiceBank &score, VoiceBank &midi, VoiceBank &typing, VoiceBank &preview)
        : banks{&score, &midi, &typing, &preview}
    {
        for (auto *bank : banks)
            bank->bindBudget(this, admit, started, serial);
    }
    void configure(int maxPoly) noexcept
    {
        maximum = maxPoly > 0 ? maxPoly : int(VoiceBank::capacity);
        enforce(0, banks[0]->settings.values.mono ? int(VoiceBank::capacity) : maximum);
    }
};
} // namespace vibera
