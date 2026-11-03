// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vibera::synth
{
enum class SourceMode
{
    oscillator,
    sample,
    hybrid
};
enum class Waveform
{
    sine,
    saw,
    square,
    triangle,
    noise,
    wavetable // Appended: morphing band-limited table (Wavetables.h).
};
enum class VoiceMode
{
    poly,
    mono,
    legato
};
enum class FilterType
{
    lowpass,
    highpass,
    bandpass,
    bypass
};

struct SampleZone
{
    std::vector<float> left, right; // Empty right means mono; otherwise lengths must match.
    double sampleRate = 48000;
    int rootNote = 60, lowNote = 0, highNote = 127, lowVelocity = 0, highVelocity = 127;
    float gainDb = 0, pan = 0;
    bool loopEnabled = false;
    std::size_t loopStart = 0, loopEnd = 0; // End-exclusive, explicit source-provenance loop only.
    // Zero disables round robin. A positive group identifies one key/velocity/root
    // region whose variants have contiguous indices from zero. Dedicated note
    // regions can represent recorded articulations without changing another note.
    int roundRobinGroup = 0, roundRobinIndex = 0;
};

class PreparedSampleMap
{
  public:
    // Off-thread only: validates every PCM sample and region before publication.
    static std::unique_ptr<PreparedSampleMap> create(std::vector<SampleZone>, std::string &error);
    const SampleZone *zoneFor(int note, int velocity, std::uint32_t noteEventOrdinal = 0) const noexcept;
    std::size_t zoneCount() const noexcept
    {
        return zones_.size();
    }

  private:
    PreparedSampleMap(std::vector<SampleZone>, std::vector<std::uint16_t> variantCounts);
    std::vector<SampleZone> zones_;
    std::vector<std::uint16_t> variantCounts_;
};

struct Parameters
{
    SourceMode sourceMode = SourceMode::oscillator;
    Waveform waveform1 = Waveform::saw, waveform2 = Waveform::saw;
    float oscillator1Level = .7f, oscillator2Level = .3f, oscillator2Semitones = 0;
    float detuneCents = 7, pulseWidth = .5f, sampleLevel = 1;
    float attackMs = 5, decayMs = 120, sustain = .75f, releaseMs = 180;
    FilterType filterType = FilterType::lowpass;
    float cutoffHz = 14000, resonance = .70710678f, filterEnvelopeOctaves = 0;
    float masterDb = -12, pan = 0, bendRangeSemitones = 2;
    float vibratoRateHz = 5, vibratoDepthCents = 5, motion = 0;
    float pitchBend = 0, modWheel = 0;
    int polyphony = 32;
    // Appended production controls. Their defaults are the neutral legacy voice:
    // one unison voice, no sub/noise/drive/FM/drift, 12 dB filter following the
    // amplitude envelope, linear segments, linear velocity and polyphonic play.
    int unisonVoices = 1;
    float unisonDetuneCents = 0, unisonSpread = 0;
    float subLevel = 0, noiseLevel = 0;
    bool ladderFilter = false;
    float filterDrive = 0, filterKeyTrack = 0, filterVelocityOctaves = 0;
    // Zero decay means the filter/FM modulation follows the amplitude envelope.
    float modAttackMs = 0, modDecayMs = 0, modSustain = 0;
    float envelopeCurve = 0, velocityToAmp = 1, glideMs = 0;
    VoiceMode voiceMode = VoiceMode::poly;
    float fmAmount = 0, analogDrift = 0;
    // Wavetable oscillators: bank, morph position and how far the mod envelope (or the
    // amplitude envelope when it is off) moves the position (-1..1).
    int wavetableBank = 0;
    float wavetablePosition = 0, wavetableEnvelope = 0;
};

enum class MidiType
{
    noteOn,
    noteOff,
    sustain,
    pitchBend,
    modWheel,
    allNotesOff,
    allSoundOff
};
struct MidiEvent
{
    MidiType type = MidiType::noteOn;
    int sampleOffset = 0, channel = 1, note = 60;
    float value = 1; // Velocity/controllers [0,1]; pitch bend [-1,1].
};

class Engine
{
  public:
    static constexpr std::size_t maximumVoices = 64;
    void prepare(double sampleRate);
    void reset() noexcept;
    void panic() noexcept;
    bool setParameters(const Parameters &) noexcept;
    // DSP control boundary only. Maps are immutable, prevalidated and retained
    // by the caller until audio has quiesced; this never retires resources.
    void bindSampleMap(const PreparedSampleMap *) noexcept;
    bool process(float *left, float *right, int frames, std::span<const MidiEvent> = {}) noexcept;
    std::size_t activeVoices() const noexcept;
    float peak(int channel) const noexcept;
    const Parameters &parameters() const noexcept
    {
        return target_;
    }
    double sampleRate() const noexcept
    {
        return sampleRate_;
    }

  private:
    enum class Stage
    {
        attack,
        decay,
        sustain,
        release,
        finished
    };
    static constexpr int maximumUnison = 16;
    struct Voice
    {
        const SampleZone *zone = nullptr;
        bool active = false, keyDown = false, sustained = false;
        int note = 0, channel = 0;
        std::uint64_t age = 0;
        std::array<double, maximumUnison> phase1{}, phase2{};
        std::array<float, maximumUnison> triangle1{}, triangle2{};
        double subPhase = 0, samplePosition = 0;
        double frequency = 440, glideOctaves = 0, sampleStep = 1, filterG = 0, filterR = 1,
               filterReciprocal = 1, ladderG = 0, ladderK = 0, cascadeReciprocal = 1;
        double filterBand[2]{}, filterLow[2]{}, cascadeBand[2]{}, cascadeLow[2]{}, ladder[2][4]{};
        double dcInput[2]{}, dcOutput[2]{}, saturationDcInput[2]{}, saturationDcOutput[2]{};
        unsigned filterClock = 0;
        float envelope = 0, releaseStep = 0, velocity = 0;
        float modEnvelope = 0, modReleaseStep = 0;
        float driftRatio = 1, driftCutoff = 1;
        double tablePosition = 0; // Wavetable morph position for this sample.
        float sampleGain = 1, samplePanLeft = 1, samplePanRight = 1;
        float stealLeft = 0, stealRight = 0, lastLeft = 0, lastRight = 0;
        unsigned stealSamples = 0;
        std::uint32_t noise = 1;
        Stage stage = Stage::finished, modStage = Stage::finished;
    };
    static bool validParameters(const Parameters &) noexcept;
    bool validEvents(std::span<const MidiEvent>, int frames) const noexcept;
    void event(const MidiEvent &) noexcept;
    void noteOn(int channel, int note, float velocity) noexcept;
    void noteOff(int channel, int note) noexcept;
    void release(Voice &) noexcept;
    float envelope(Voice &) noexcept;
    float modEnvelope(Voice &) noexcept;
    float random(Voice &) noexcept;
    void startVoice(Voice &, int channel, int note, float velocity, const SampleZone *) noexcept;
    void retarget(Voice &, int note) noexcept;
    float oscillator(Voice &, Waveform, double &, float &, double step, double offset = 0) noexcept;
    void render(Voice &, double lfo, float &, float &) noexcept;
    void smooth() noexcept;
    double sampleRate_ = 48000, smoothing_ = .004;
    Parameters target_{}, current_{};
    bool prepared_ = false, configured_ = false;
    const PreparedSampleMap *map_ = nullptr;
    std::array<Voice, maximumVoices> voices_{};
    std::array<bool, 16> sustain_{};
    std::array<float, 16> bends_{}, modulation_{};
    std::array<float, 16> currentBends_{}, currentModulation_{};
    std::array<double, 16> pitchMultipliers_{};
    std::array<std::array<std::uint32_t, 128>, 16> roundRobinOrdinals_{};
    double oscillator2Ratio_ = 1, motionCutoff_ = 1, triangleLeak_ = .9997;
    // Per-sample shared segment rates (CONTROL-free, computed in the DSP loop).
    float attackExp_ = 0, decayExp_ = 0, releaseExp_ = 0, modAttackLinear_ = 0, modAttackExp_ = 0,
          modDecayLinear_ = 0, modDecayExp_ = 0, glideCoefficient_ = 0;
    std::array<double, maximumUnison> unisonRatio_{};
    std::array<float, maximumUnison> unisonLeft_{}, unisonRight_{};
    float unisonGain_ = 1;
    double lastFrequency_ = 0;
    // Mono/legato held-key stack, newest last. Bounded; overflow drops the oldest.
    std::array<std::pair<int, int>, 32> held_{};
    std::size_t heldCount_ = 0;
    std::uint64_t age_ = 0;
    double lfoPhase_ = 0;
    double dcCoefficient_ = .9993;
    float peaks_[2]{};
};
} // namespace vibera::synth
