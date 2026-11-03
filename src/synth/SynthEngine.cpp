// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SynthEngine.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace vibera::synth
{
namespace
{
constexpr double pi = std::numbers::pi;
bool inRange(float value, float low, float high) noexcept
{
    return std::isfinite(value) && value >= low && value <= high;
}
bool noteRange(int value) noexcept
{
    return value >= 0 && value <= 127;
}
double blep(double phase, double step) noexcept
{
    // Original two-sided quadratic step correction. PolyBLEP background:
    // https://research.aalto.fi/en/publications/antialiasing-oscillators-in-subtractive-synthesis/
    if (phase < step)
    {
        const auto x = phase / step;
        return 2 * x - x * x - 1;
    }
    if (phase > 1 - step)
    {
        const auto x = (phase - 1) / step;
        return x * x + 2 * x + 1;
    }
    return 0;
}
} // namespace

PreparedSampleMap::PreparedSampleMap(std::vector<SampleZone> zones, std::vector<std::uint16_t> variantCounts)
    : zones_(std::move(zones)), variantCounts_(std::move(variantCounts))
{
}
std::unique_ptr<PreparedSampleMap> PreparedSampleMap::create(std::vector<SampleZone> zones,
                                                             std::string &error)
{
    error.clear();
    if (zones.empty() || zones.size() > 4096)
    {
        error = "Sample map requires 1..4096 regions";
        return {};
    }
    for (std::size_t index = 0; index < zones.size(); ++index)
    {
        const auto &zone = zones[index];
        const auto fail = [&](const char *reason)
        {
            error = "Region " + std::to_string(index) + ": " + reason;
            return std::unique_ptr<PreparedSampleMap>{};
        };
        if (!std::isfinite(zone.sampleRate) || zone.sampleRate < 8000 || zone.sampleRate > 384000 ||
            zone.left.size() < 2 || zone.left.size() > 33554432 ||
            (!zone.right.empty() && zone.right.size() != zone.left.size()))
            return fail("invalid PCM dimensions or sample rate");
        if (!noteRange(zone.rootNote) || !noteRange(zone.lowNote) || !noteRange(zone.highNote) ||
            zone.lowNote > zone.highNote || !noteRange(zone.lowVelocity) || !noteRange(zone.highVelocity) ||
            zone.lowVelocity > zone.highVelocity || !inRange(zone.gainDb, -96, 24) ||
            !inRange(zone.pan, -1, 1))
            return fail("invalid note/velocity/gain/pan mapping");
        if (zone.loopEnabled && (zone.loopStart >= zone.loopEnd || zone.loopEnd > zone.left.size() ||
                                 zone.loopEnd - zone.loopStart < 2))
            return fail("invalid explicit end-exclusive sample loop");
        if (zone.roundRobinGroup < 0 || zone.roundRobinIndex < 0 || zone.roundRobinIndex >= 4096 ||
            (zone.roundRobinGroup == 0 && zone.roundRobinIndex != 0))
            return fail("invalid round-robin group/index identity");
        const auto validPcm = [](const std::vector<float> &pcm)
        {
            return std::all_of(pcm.begin(), pcm.end(),
                               [](float value) { return std::isfinite(value) && std::abs(value) <= 8; });
        };
        if (!validPcm(zone.left) || !validPcm(zone.right))
            return fail("PCM contains non-finite or out-of-range data");
    }
    std::vector<std::uint16_t> variantCounts(zones.size(), 1);
    for (std::size_t index = 0; index < zones.size(); ++index)
    {
        const auto &zone = zones[index];
        if (zone.roundRobinGroup == 0)
            continue;
        int count = 0, highest = 0;
        for (std::size_t otherIndex = 0; otherIndex < zones.size(); ++otherIndex)
        {
            const auto &other = zones[otherIndex];
            if (other.roundRobinGroup == zone.roundRobinGroup)
            {
                if (other.rootNote != zone.rootNote || other.lowNote != zone.lowNote ||
                    other.highNote != zone.highNote || other.lowVelocity != zone.lowVelocity ||
                    other.highVelocity != zone.highVelocity)
                {
                    error = "Round-robin group " + std::to_string(zone.roundRobinGroup) +
                            " has incompatible key/velocity/root regions";
                    return {};
                }
                if (otherIndex != index && other.roundRobinIndex == zone.roundRobinIndex)
                {
                    error = "Round-robin group " + std::to_string(zone.roundRobinGroup) +
                            " has duplicate variant indices";
                    return {};
                }
                ++count;
                highest = std::max(highest, other.roundRobinIndex);
            }
            else if (other.rootNote == zone.rootNote && other.lowNote <= zone.highNote &&
                     other.highNote >= zone.lowNote && other.lowVelocity <= zone.highVelocity &&
                     other.highVelocity >= zone.lowVelocity)
            {
                error = "Round-robin group " + std::to_string(zone.roundRobinGroup) +
                        " ambiguously overlaps a different region with the same root";
                return {};
            }
        }
        if (highest != count - 1)
        {
            error = "Round-robin group " + std::to_string(zone.roundRobinGroup) +
                    " requires contiguous variant indices from zero";
            return {};
        }
        variantCounts[index] = std::uint16_t(count);
    }
    return std::unique_ptr<PreparedSampleMap>(
        new PreparedSampleMap(std::move(zones), std::move(variantCounts)));
}
const SampleZone *PreparedSampleMap::zoneFor(int note, int velocity,
                                             std::uint32_t noteEventOrdinal) const noexcept
{
    const SampleZone *best = nullptr;
    int distance = 128;
    for (std::size_t index = 0; index < zones_.size(); ++index)
    {
        const auto &zone = zones_[index];
        if (note >= zone.lowNote && note <= zone.highNote && velocity >= zone.lowVelocity &&
            velocity <= zone.highVelocity && std::abs(note - zone.rootNote) < distance &&
            (zone.roundRobinGroup == 0 ||
             noteEventOrdinal % variantCounts_[index] == std::uint32_t(zone.roundRobinIndex)))
        {
            best = &zone;
            distance = std::abs(note - zone.rootNote);
        }
    }
    return best;
}

bool Engine::validParameters(const Parameters &p) noexcept
{
    return p.sourceMode >= SourceMode::oscillator && p.sourceMode <= SourceMode::hybrid &&
           p.waveform1 >= Waveform::sine && p.waveform1 <= Waveform::noise && p.waveform2 >= Waveform::sine &&
           p.waveform2 <= Waveform::noise && p.filterType >= FilterType::lowpass &&
           p.filterType <= FilterType::bypass && inRange(p.oscillator1Level, 0, 1) &&
           inRange(p.oscillator2Level, 0, 1) && inRange(p.oscillator2Semitones, -48, 48) &&
           inRange(p.detuneCents, -100, 100) && inRange(p.pulseWidth, .05f, .95f) &&
           inRange(p.sampleLevel, 0, 2) && inRange(p.attackMs, 0, 10000) && inRange(p.decayMs, 0, 20000) &&
           inRange(p.sustain, 0, 1) && inRange(p.releaseMs, 0, 30000) && inRange(p.cutoffHz, 20, 20000) &&
           inRange(p.resonance, .1f, 12) && inRange(p.filterEnvelopeOctaves, -8, 8) &&
           inRange(p.masterDb, -96, 12) && inRange(p.pan, -1, 1) && inRange(p.bendRangeSemitones, 0, 48) &&
           inRange(p.vibratoRateHz, .05f, 20) && inRange(p.vibratoDepthCents, 0, 100) &&
           inRange(p.motion, 0, 1) && inRange(p.pitchBend, -1, 1) && inRange(p.modWheel, 0, 1) &&
           p.polyphony >= 1 && p.polyphony <= int(maximumVoices) && p.unisonVoices >= 1 &&
           p.unisonVoices <= maximumUnison && inRange(p.unisonDetuneCents, 0, 100) &&
           inRange(p.unisonSpread, 0, 1) && inRange(p.subLevel, 0, 1) && inRange(p.noiseLevel, 0, 1) &&
           inRange(p.filterDrive, 0, 1) && inRange(p.filterKeyTrack, 0, 1) &&
           inRange(p.filterVelocityOctaves, 0, 4) && inRange(p.modAttackMs, 0, 10000) &&
           inRange(p.modDecayMs, 0, 20000) && inRange(p.modSustain, 0, 1) && inRange(p.envelopeCurve, 0, 1) &&
           inRange(p.velocityToAmp, 0, 1) && inRange(p.glideMs, 0, 5000) && p.voiceMode >= VoiceMode::poly &&
           p.voiceMode <= VoiceMode::legato && inRange(p.fmAmount, 0, 1) && inRange(p.analogDrift, 0, 1);
}
void Engine::prepare(double sampleRate)
{
    if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 384000)
        throw std::invalid_argument("Synthesizer requires a sample rate from 8 to 384 kHz");
    sampleRate_ = sampleRate;
    smoothing_ = 1 - std::exp(-1 / (.005 * sampleRate));
    triangleLeak_ = std::exp(-2 * pi * 2 / sampleRate);
    dcCoefficient_ = std::exp(-2 * pi * 5 / sampleRate);
    configured_ = false;
    prepared_ = true;
    reset();
}
void Engine::reset() noexcept
{
    current_ = target_;
    panic();
}
void Engine::panic() noexcept
{
    voices_.fill(Voice{});
    sustain_.fill(false);
    bends_.fill(0);
    modulation_.fill(0);
    currentBends_.fill(0);
    currentModulation_.fill(0);
    pitchMultipliers_.fill(1);
    roundRobinOrdinals_.fill({});
    age_ = 0;
    lfoPhase_ = 0;
    lastFrequency_ = 0;
    heldCount_ = 0;
    peaks_[0] = peaks_[1] = 0;
}
void Engine::bindSampleMap(const PreparedSampleMap *map) noexcept
{
    if (map_ != map)
    {
        panic();
        map_ = map;
    }
}
bool Engine::setParameters(const Parameters &parameters) noexcept
{
    if (!validParameters(parameters))
        return false;
    if (!configured_)
        current_ = parameters;
    if (parameters.filterType != target_.filterType || parameters.ladderFilter != target_.ladderFilter)
        for (auto &voice : voices_)
        {
            std::fill_n(voice.filterBand, 2, 0.0);
            std::fill_n(voice.filterLow, 2, 0.0);
            std::fill_n(voice.cascadeBand, 2, 0.0);
            std::fill_n(voice.cascadeLow, 2, 0.0);
            for (auto &stages : voice.ladder)
                std::fill_n(stages, 4, 0.0);
            voice.filterClock = 0;
        }
    if (configured_ && parameters.voiceMode != target_.voiceMode)
    {
        heldCount_ = 0;
        for (auto &voice : voices_)
            if (voice.active && voice.stage != Stage::release)
                release(voice);
    }
    target_ = parameters;
    configured_ = true;
    for (std::size_t i = std::size_t(parameters.polyphony); i < voices_.size(); ++i)
        voices_[i] = Voice{};
    return true;
}
void Engine::smooth() noexcept
{
    const auto update = [&](float &current, float target)
    { current += float(smoothing_ * (target - current)); };
    update(current_.oscillator1Level, target_.oscillator1Level);
    update(current_.oscillator2Level, target_.oscillator2Level);
    update(current_.oscillator2Semitones, target_.oscillator2Semitones);
    update(current_.detuneCents, target_.detuneCents);
    update(current_.pulseWidth, target_.pulseWidth);
    update(current_.sampleLevel, target_.sampleLevel);
    update(current_.attackMs, target_.attackMs);
    update(current_.decayMs, target_.decayMs);
    update(current_.sustain, target_.sustain);
    update(current_.releaseMs, target_.releaseMs);
    update(current_.cutoffHz, target_.cutoffHz);
    update(current_.resonance, target_.resonance);
    update(current_.filterEnvelopeOctaves, target_.filterEnvelopeOctaves);
    update(current_.masterDb, target_.masterDb);
    update(current_.pan, target_.pan);
    update(current_.bendRangeSemitones, target_.bendRangeSemitones);
    update(current_.vibratoRateHz, target_.vibratoRateHz);
    update(current_.vibratoDepthCents, target_.vibratoDepthCents);
    update(current_.motion, target_.motion);
    update(current_.pitchBend, target_.pitchBend);
    update(current_.modWheel, target_.modWheel);
    update(current_.unisonDetuneCents, target_.unisonDetuneCents);
    update(current_.unisonSpread, target_.unisonSpread);
    update(current_.subLevel, target_.subLevel);
    update(current_.noiseLevel, target_.noiseLevel);
    update(current_.filterDrive, target_.filterDrive);
    update(current_.filterKeyTrack, target_.filterKeyTrack);
    update(current_.filterVelocityOctaves, target_.filterVelocityOctaves);
    update(current_.modAttackMs, target_.modAttackMs);
    update(current_.modSustain, target_.modSustain);
    update(current_.envelopeCurve, target_.envelopeCurve);
    update(current_.velocityToAmp, target_.velocityToAmp);
    update(current_.glideMs, target_.glideMs);
    update(current_.fmAmount, target_.fmAmount);
    update(current_.analogDrift, target_.analogDrift);
    // Zero decay is a mode switch (follow the amplitude envelope), not a time.
    if (target_.modDecayMs <= 0 || current_.modDecayMs <= 0)
        current_.modDecayMs = target_.modDecayMs;
    else
        update(current_.modDecayMs, target_.modDecayMs);
    current_.sourceMode = target_.sourceMode;
    current_.waveform1 = target_.waveform1;
    current_.waveform2 = target_.waveform2;
    current_.filterType = target_.filterType;
    current_.polyphony = target_.polyphony;
    current_.unisonVoices = target_.unisonVoices;
    current_.ladderFilter = target_.ladderFilter;
    current_.voiceMode = target_.voiceMode;
}
float Engine::random(Voice &voice) noexcept
{
    voice.noise ^= voice.noise << 13;
    voice.noise ^= voice.noise >> 17;
    voice.noise ^= voice.noise << 5;
    return float((double(voice.noise) / 4294967295.0) * 2 - 1);
}
void Engine::release(Voice &voice) noexcept
{
    voice.keyDown = voice.sustained = false;
    if (current_.releaseMs <= 0 || voice.envelope <= 0)
    {
        voice.active = false;
        voice.stage = Stage::finished;
        voice.envelope = 0;
        return;
    }
    const auto samples = float(std::max(1.0, current_.releaseMs * .001 * sampleRate_));
    voice.releaseStep = voice.envelope / samples;
    voice.stage = Stage::release;
    voice.modReleaseStep = voice.modEnvelope / samples;
    voice.modStage = Stage::release;
}
void Engine::noteOff(int channel, int note) noexcept
{
    if (current_.voiceMode != VoiceMode::poly)
    {
        const auto end = held_.begin() + std::ptrdiff_t(heldCount_);
        const auto kept = std::remove(held_.begin(), end, std::pair<int, int>{channel, note});
        heldCount_ = std::size_t(kept - held_.begin());
        auto &voice = voices_[0];
        if (!voice.active || !voice.keyDown || voice.channel != channel || voice.note != note)
            return;
        if (heldCount_ > 0)
        {
            // Return to the newest still-held key: legato keeps the envelope,
            // mono retriggers it from the current level (no reset click).
            voice.channel = held_[heldCount_ - 1].first;
            retarget(voice, held_[heldCount_ - 1].second);
            if (current_.voiceMode == VoiceMode::mono)
                voice.stage = voice.modStage = Stage::attack;
            return;
        }
        voice.keyDown = false;
        if (sustain_[std::size_t(channel)])
            voice.sustained = true;
        else
            release(voice);
        return;
    }
    for (auto &voice : voices_)
        if (voice.active && voice.channel == channel && voice.note == note && voice.keyDown)
        {
            voice.keyDown = false;
            if (sustain_[std::size_t(channel)])
                voice.sustained = true;
            else
                release(voice);
        }
}
void Engine::retarget(Voice &voice, int note) noexcept
{
    const double target = 440 * std::exp2((note - 69) / 12.0);
    voice.glideOctaves =
        current_.glideMs > 0 ? std::log2(voice.frequency / target) + voice.glideOctaves : 0.0;
    voice.frequency = target;
    voice.note = note;
    lastFrequency_ = target;
    if (voice.zone)
        voice.sampleStep =
            voice.zone->sampleRate / sampleRate_ * std::exp2((note - voice.zone->rootNote) / 12.0);
}
void Engine::startVoice(Voice &voice, int channel, int note, float velocity, const SampleZone *zone) noexcept
{
    voice = Voice{};
    voice.active = voice.keyDown = true;
    voice.channel = channel;
    voice.note = note;
    voice.age = ++age_;
    voice.velocity = velocity;
    voice.frequency = 440 * std::exp2((note - 69) / 12.0);
    if (current_.glideMs > 0 && lastFrequency_ > 0)
        voice.glideOctaves = std::log2(lastFrequency_ / voice.frequency);
    lastFrequency_ = voice.frequency;
    voice.triangle1.fill(-1);
    voice.triangle2.fill(-1);
    voice.noise = 0x9e3779b9u ^ (std::uint32_t(note + channel * 128) * 747796405u) ^ std::uint32_t(age_);
    if (voice.noise == 0)
        voice.noise = 1;
    if (current_.unisonVoices > 1 || current_.analogDrift > 0)
    {
        // Free-running analogue-style start phases; triangles start on their slope.
        const auto triangleAt = [](double phase)
        { return float(phase < .5 ? 4 * phase - 1 : 3 - 4 * phase); };
        for (int u = 0; u < maximumUnison; ++u)
        {
            voice.phase1[std::size_t(u)] = .5 + .5 * random(voice) * .999;
            voice.phase2[std::size_t(u)] = .5 + .5 * random(voice) * .999;
            voice.triangle1[std::size_t(u)] = triangleAt(voice.phase1[std::size_t(u)]);
            voice.triangle2[std::size_t(u)] = triangleAt(voice.phase2[std::size_t(u)]);
        }
    }
    if (current_.analogDrift > 0)
    {
        voice.driftRatio = float(std::exp2(current_.analogDrift * 9.0 * random(voice) / 1200.0));
        voice.driftCutoff = float(std::exp2(current_.analogDrift * .3 * random(voice)));
    }
    voice.stage = voice.modStage = Stage::attack;
    voice.zone = zone;
    if (zone)
    {
        voice.sampleStep = zone->sampleRate / sampleRate_ * std::exp2((note - zone->rootNote) / 12.0);
        voice.sampleGain = std::pow(10.0f, zone->gainDb / 20);
        voice.samplePanLeft = std::sqrt(1 - zone->pan);
        voice.samplePanRight = std::sqrt(1 + zone->pan);
    }
}
void Engine::noteOn(int channel, int note, float velocity) noexcept
{
    auto &ordinal = roundRobinOrdinals_[std::size_t(channel)][std::size_t(note)];
    const auto *zone =
        map_ ? map_->zoneFor(note, std::clamp(int(std::lround(velocity * 127)), 1, 127), ordinal) : nullptr;
    ++ordinal;
    if (current_.sourceMode == SourceMode::sample && !zone)
        return; // Unmapped recorded notes do not silently substitute an oscillator.
    if (current_.voiceMode != VoiceMode::poly)
    {
        const auto end = held_.begin() + std::ptrdiff_t(heldCount_);
        heldCount_ =
            std::size_t(std::remove(held_.begin(), end, std::pair<int, int>{channel, note}) - held_.begin());
        if (heldCount_ == held_.size())
        {
            std::move(held_.begin() + 1, held_.end(), held_.begin());
            --heldCount_;
        }
        held_[heldCount_++] = {channel, note};
        auto &voice = voices_[0];
        if (!voice.active)
        {
            startVoice(voice, channel, note, velocity, zone);
            return;
        }
        const bool legato = current_.voiceMode == VoiceMode::legato && voice.keyDown;
        voice.channel = channel;
        voice.keyDown = true;
        voice.sustained = false;
        voice.age = ++age_;
        if (!legato)
        {
            voice.velocity = velocity;
            voice.stage = voice.modStage = Stage::attack;
            if (zone)
            {
                voice.zone = zone;
                voice.samplePosition = 0;
            }
        }
        retarget(voice, note);
        return;
    }
    Voice *selected = nullptr;
    for (std::size_t i = 0; i < std::size_t(target_.polyphony); ++i)
        if (!voices_[i].active)
        {
            selected = &voices_[i];
            break;
        }
    if (!selected)
    {
        selected = &voices_[0];
        for (std::size_t i = 1; i < std::size_t(target_.polyphony); ++i)
        {
            auto &candidate = voices_[i];
            const auto score = [](const Voice &voice)
            { return voice.envelope * voice.velocity + (voice.stage == Stage::release ? 0 : 2); };
            if (score(candidate) < score(*selected) ||
                (score(candidate) == score(*selected) && candidate.age < selected->age))
                selected = &candidate;
        }
    }
    const auto oldLeft = selected->active ? selected->lastLeft : 0;
    const auto oldRight = selected->active ? selected->lastRight : 0;
    const bool stolen = selected->active;
    startVoice(*selected, channel, note, velocity, zone);
    if (stolen)
    {
        selected->stealLeft = oldLeft;
        selected->stealRight = oldRight;
        selected->stealSamples = 64;
    }
}
void Engine::event(const MidiEvent &message) noexcept
{
    const auto channel = std::size_t(message.channel - 1);
    switch (message.type)
    {
    case MidiType::noteOn:
        if (message.value == 0)
            noteOff(int(channel), message.note);
        else
            noteOn(int(channel), message.note, message.value);
        break;
    case MidiType::noteOff:
        noteOff(int(channel), message.note);
        break;
    case MidiType::sustain:
        sustain_[channel] = message.value >= .5f;
        if (!sustain_[channel])
            for (auto &voice : voices_)
                if (voice.active && voice.channel == int(channel) && !voice.keyDown && voice.sustained)
                    release(voice);
        break;
    case MidiType::pitchBend:
        bends_[channel] = message.value;
        break;
    case MidiType::modWheel:
        modulation_[channel] = message.value;
        break;
    case MidiType::allNotesOff:
        for (int note = 0; note < 128; ++note)
            noteOff(int(channel), note);
        break;
    case MidiType::allSoundOff:
        for (auto &voice : voices_)
            if (voice.channel == int(channel))
                voice = Voice{};
        heldCount_ = std::size_t(std::remove_if(held_.begin(), held_.begin() + std::ptrdiff_t(heldCount_),
                                                [&](const auto &key) { return key.first == int(channel); }) -
                                 held_.begin());
        sustain_[channel] = false;
        bends_[channel] = modulation_[channel] = 0;
        currentBends_[channel] = currentModulation_[channel] = 0;
        roundRobinOrdinals_[channel].fill(0);
        break;
    }
}
namespace
{
// One curved ADSR step. Curve zero is the exact legacy linear segment; curve one
// is an analogue-style RC charge (overshooting target) and exponential decays.
float advance(float level, float curve, float attackLinear, float attackExp, float decayLinear,
              float decayExp, float sustain, float releaseStep, float releaseExp, int &stage) noexcept
{
    enum
    {
        attack,
        decay,
        hold,
        released
    };
    if (stage == attack)
    {
        level += (1 - curve) * attackLinear + curve * (1.3f - level) * attackExp;
        if (level >= 1)
        {
            level = 1;
            stage = decay;
        }
    }
    else if (stage == decay)
    {
        level += -(1 - curve) * decayLinear + curve * (sustain - level) * decayExp;
        if (level <= sustain || level - sustain < 1e-4f * curve)
        {
            level = sustain;
            stage = hold;
        }
    }
    else if (stage == hold)
        level = sustain;
    else if (stage == released)
    {
        level -= (1 - curve) * releaseStep + curve * level * releaseExp;
        if (curve > 0 && level < 1e-4f)
            level = 0;
    }
    return level;
}
float softClip(double x) noexcept
{
    x = std::clamp(x, -3.0, 3.0);
    return float(x * (27 + x * x) / (27 + 9 * x * x));
}
} // namespace
float Engine::envelope(Voice &voice) noexcept
{
    const float curve = current_.envelopeCurve;
    if (curve > 0 && voice.stage != Stage::finished)
    {
        int stage = voice.stage == Stage::attack    ? 0
                    : voice.stage == Stage::decay   ? 1
                    : voice.stage == Stage::sustain ? 2
                                                    : 3;
        const float attackLinear =
            current_.attackMs <= 0 ? 1 : float(1 / (current_.attackMs * .001 * sampleRate_));
        const float decayLinear =
            current_.decayMs <= 0 ? 1
                                  : float((1 - current_.sustain) / (current_.decayMs * .001 * sampleRate_));
        if (stage == 0 && current_.attackMs <= 0)
        {
            voice.envelope = 1;
            stage = 1;
        }
        voice.envelope = advance(voice.envelope, curve, attackLinear, attackExp_, decayLinear,
                                 current_.decayMs <= 0 ? 0 : decayExp_, current_.sustain, voice.releaseStep,
                                 releaseExp_, stage);
        if (current_.decayMs <= 0 && stage == 1)
        {
            voice.envelope = current_.sustain;
            stage = 2;
        }
        voice.stage = stage == 0   ? Stage::attack
                      : stage == 1 ? Stage::decay
                      : stage == 2 ? Stage::sustain
                                   : Stage::release;
    }
    else if (voice.stage == Stage::attack)
    {
        voice.envelope += current_.attackMs <= 0 ? 1 : float(1 / (current_.attackMs * .001 * sampleRate_));
        if (voice.envelope >= 1)
        {
            voice.envelope = 1;
            voice.stage = Stage::decay;
        }
    }
    else if (voice.stage == Stage::decay)
    {
        voice.envelope -= current_.decayMs <= 0
                              ? 1
                              : float((1 - current_.sustain) / (current_.decayMs * .001 * sampleRate_));
        if (voice.envelope <= current_.sustain)
        {
            voice.envelope = current_.sustain;
            voice.stage = Stage::sustain;
        }
    }
    else if (voice.stage == Stage::sustain)
        voice.envelope = current_.sustain;
    else if (voice.stage == Stage::release)
        voice.envelope -= voice.releaseStep;
    if (voice.envelope <= 0 && voice.stage != Stage::attack)
    {
        voice.active = false;
        voice.stage = Stage::finished;
        voice.envelope = 0;
    }
    return voice.envelope;
}
float Engine::modEnvelope(Voice &voice) noexcept
{
    if (voice.modStage == Stage::finished)
        return 0;
    int stage = voice.modStage == Stage::attack    ? 0
                : voice.modStage == Stage::decay   ? 1
                : voice.modStage == Stage::sustain ? 2
                                                   : 3;
    const bool instant = current_.modAttackMs <= 0;
    if (instant && stage == 0)
    {
        voice.modEnvelope = 1;
        stage = 1;
    }
    voice.modEnvelope =
        advance(voice.modEnvelope, current_.envelopeCurve, modAttackLinear_, modAttackExp_, modDecayLinear_,
                modDecayExp_, current_.modSustain, voice.modReleaseStep, releaseExp_, stage);
    if (stage == 3 && voice.modEnvelope <= 0)
    {
        voice.modEnvelope = 0;
        voice.modStage = Stage::finished;
        return 0;
    }
    voice.modStage = stage == 0   ? Stage::attack
                     : stage == 1 ? Stage::decay
                     : stage == 2 ? Stage::sustain
                                  : Stage::release;
    return voice.modEnvelope;
}
float Engine::oscillator(Voice &voice, Waveform waveform, double &phase, float &triangle, double step,
                         double offset) noexcept
{
    if (step >= .49 || step <= 0)
        return 0; // No folded substitute pitch above the supported oscillator bandwidth.
    double result = 0;
    double p = phase;
    if (offset != 0)
    {
        p += offset;
        p -= std::floor(p);
    }
    if (waveform == Waveform::noise)
        result = random(voice);
    else if (waveform == Waveform::sine)
        result = std::sin(2 * pi * p);
    else if (waveform == Waveform::saw)
        result = 2 * p - 1 - blep(p, step);
    else
    {
        const auto width = waveform == Waveform::triangle ? .5 : double(current_.pulseWidth);
        auto secondEdge = p - width;
        if (secondEdge < 0)
            secondEdge += 1;
        result = (p < width ? 1.0 : -1.0) + blep(p, step) - blep(secondEdge, step);
        if (waveform == Waveform::triangle)
        {
            triangle = float((triangle + 4 * step * result) * triangleLeak_);
            result = triangle;
        }
    }
    phase += step;
    if (phase >= 1)
        phase -= 1;
    return float(result);
}
void Engine::render(Voice &voice, double lfo, float &left, float &right) noexcept
{
    const auto amount = envelope(voice);
    if (!voice.active)
        return;
    const bool separateModulation = current_.modDecayMs > 0;
    const float modulation = separateModulation ? modEnvelope(voice) : amount;
    auto pitch = pitchMultipliers_[std::size_t(voice.channel)];
    if (voice.driftRatio != 1)
        pitch *= voice.driftRatio;
    if (voice.glideOctaves != 0)
    {
        pitch *= std::exp2(voice.glideOctaves);
        voice.glideOctaves *= glideCoefficient_;
        if (std::abs(voice.glideOctaves) < 1e-5)
            voice.glideOctaves = 0;
    }
    double l = 0, r = 0;
    if (current_.sourceMode != SourceMode::sample)
    {
        const double step = voice.frequency * pitch / sampleRate_;
        const double fm = current_.fmAmount > 0 ? double(current_.fmAmount) * current_.fmAmount * 2.5 *
                                                      (separateModulation ? modulation : 1)
                                                : 0.0;
        if (current_.unisonVoices <= 1)
        {
            if (fm > 0)
            {
                const auto second = oscillator(voice, current_.waveform2, voice.phase2[0], voice.triangle2[0],
                                               step * oscillator2Ratio_);
                const auto first = oscillator(voice, current_.waveform1, voice.phase1[0], voice.triangle1[0],
                                              step, fm * second);
                l = r = first * current_.oscillator1Level + second * current_.oscillator2Level;
            }
            else
            {
                const auto first =
                    oscillator(voice, current_.waveform1, voice.phase1[0], voice.triangle1[0], step);
                const auto second = oscillator(voice, current_.waveform2, voice.phase2[0], voice.triangle2[0],
                                               step * oscillator2Ratio_);
                l = r = first * current_.oscillator1Level + second * current_.oscillator2Level;
            }
        }
        else
            for (std::size_t u = 0; u < std::size_t(current_.unisonVoices); ++u)
            {
                const auto unisonStep = step * unisonRatio_[u];
                const auto second = oscillator(voice, current_.waveform2, voice.phase2[u], voice.triangle2[u],
                                               unisonStep * oscillator2Ratio_);
                const auto first = oscillator(voice, current_.waveform1, voice.phase1[u], voice.triangle1[u],
                                              unisonStep, fm * second);
                const double mixed = first * current_.oscillator1Level + second * current_.oscillator2Level;
                l += mixed * unisonLeft_[u];
                r += mixed * unisonRight_[u];
            }
        if (current_.subLevel > 0 && step * .5 < .49)
        {
            const double sub = std::sin(2 * pi * voice.subPhase) * current_.subLevel;
            voice.subPhase += step * .5;
            if (voice.subPhase >= 1)
                voice.subPhase -= 1;
            l += sub;
            r += sub;
        }
        if (current_.noiseLevel > 0)
        {
            l += random(voice) * current_.noiseLevel * .5;
            r += random(voice) * current_.noiseLevel * .5;
        }
        // Per-voice 5 Hz DC blocker on the oscillator layer (recorded PCM stays exact):
        // removes offsets from asymmetric pulses and 1:1 FM, and ends with the voice.
        const auto block = [&](double input, std::size_t channel)
        {
            const auto output = input - voice.dcInput[channel] + dcCoefficient_ * voice.dcOutput[channel];
            voice.dcInput[channel] = input;
            voice.dcOutput[channel] = output;
            return output;
        };
        l = block(l, 0);
        r = block(r, 1);
    }
    if (current_.sourceMode != SourceMode::oscillator && voice.zone)
    {
        const auto &zone = *voice.zone;
        if (zone.loopEnabled && voice.samplePosition >= double(zone.loopEnd))
            voice.samplePosition = zone.loopStart + std::fmod(voice.samplePosition - zone.loopStart,
                                                              double(zone.loopEnd - zone.loopStart));
        if (voice.samplePosition < double(zone.left.size()))
        {
            const auto index = std::size_t(voice.samplePosition);
            const auto fraction = float(voice.samplePosition - double(index));
            const auto next = zone.loopEnabled && index + 1 >= zone.loopEnd
                                  ? zone.loopStart
                                  : std::min(index + 1, zone.left.size() - 1);
            const auto read = [&](const std::vector<float> &pcm)
            { return pcm[index] + fraction * (pcm[next] - pcm[index]); };
            const auto gain = voice.sampleGain * current_.sampleLevel;
            l += read(zone.left) * gain * voice.samplePanLeft;
            r += read(zone.right.empty() ? zone.left : zone.right) * gain * voice.samplePanRight;
            voice.samplePosition += voice.sampleStep * pitch;
        }
        else if (current_.sourceMode == SourceMode::sample)
        {
            voice.active = false;
            voice.stage = Stage::finished;
            return;
        }
    }
    if (current_.filterDrive > 0)
    {
        // Pre-filter saturation; the make-up keeps mid-level signals near unity.
        const double drive = 1 + current_.filterDrive * 7;
        const double makeup = 1 / (1 + .1 * (drive - 1));
        l = softClip(l * drive) * makeup;
        r = softClip(r * drive) * makeup;
    }
    if (current_.filterType != FilterType::bypass)
    {
        if ((voice.filterClock++ & 15u) == 0)
        {
            const auto cutoff =
                std::clamp(current_.cutoffHz * motionCutoff_ *
                               std::exp2(current_.filterEnvelopeOctaves * modulation +
                                         current_.filterKeyTrack * (voice.note - 60) / 12.0 +
                                         current_.filterVelocityOctaves * (voice.velocity - 1)) *
                               voice.driftCutoff,
                           5.0, sampleRate_ * .45);
            voice.filterG = std::tan(pi * cutoff / sampleRate_);
            voice.filterR = 1.0 / current_.resonance;
            voice.filterReciprocal = 1 / (1 + voice.filterG * (voice.filterG + voice.filterR));
            if (current_.ladderFilter)
            {
                voice.ladderG = voice.filterG / (1 + voice.filterG);
                voice.ladderK = std::clamp((current_.resonance - .5) / 7.5, 0.0, 1.0) * 3.85;
                voice.cascadeReciprocal = 1 / (1 + voice.filterG * (voice.filterG + std::numbers::sqrt2));
            }
        }
        const auto svf = [&](double input, double &bandState, double &lowState, double damping,
                             double reciprocal, FilterType type)
        {
            const auto band = (bandState + voice.filterG * (input - lowState)) * reciprocal;
            const auto low = lowState + voice.filterG * band;
            const auto high = input - damping * band - low;
            bandState = 2 * band - bandState;
            lowState = 2 * low - lowState;
            return type == FilterType::lowpass ? low : type == FilterType::highpass ? high : damping * band;
        };
        const auto filter = [&](double input, std::size_t channel)
        {
            if (current_.ladderFilter && current_.filterType == FilterType::lowpass)
            {
                // Zero-delay-feedback four-pole ladder with a soft-saturated core.
                auto *s = voice.ladder[channel];
                const double g = voice.ladderG, k = voice.ladderK, one = 1 - g;
                const double feedback =
                    g * g * g * one * s[0] + g * g * one * s[1] + g * one * s[2] + one * s[3];
                auto u = softClip((input * (1 + .5 * k) - k * feedback) / (1 + k * g * g * g * g));
                double y = u;
                for (int stage = 0; stage < 4; ++stage)
                {
                    const auto v = (y - s[stage]) * g;
                    y = v + s[stage];
                    s[stage] = y + v;
                }
                return y;
            }
            if (current_.ladderFilter)
                input = svf(input, voice.cascadeBand[channel], voice.cascadeLow[channel], std::numbers::sqrt2,
                            voice.cascadeReciprocal, current_.filterType);
            return svf(input, voice.filterBand[channel], voice.filterLow[channel], voice.filterR,
                       voice.filterReciprocal, current_.filterType);
        };
        l = filter(l, 0);
        r = filter(r, 1);
    }
    if (current_.filterDrive > 0 || (current_.ladderFilter && current_.filterType == FilterType::lowpass))
    {
        // The saturating stages rectify asymmetric waves; block their DC too.
        const auto block = [&](double input, std::size_t channel)
        {
            const auto output =
                input - voice.saturationDcInput[channel] + dcCoefficient_ * voice.saturationDcOutput[channel];
            voice.saturationDcInput[channel] = input;
            voice.saturationDcOutput[channel] = output;
            return output;
        };
        l = block(l, 0);
        r = block(r, 1);
    }
    const float velocityGain =
        current_.velocityToAmp >= 1 ? voice.velocity : 1 - current_.velocityToAmp * (1 - voice.velocity);
    auto outLeft = float(l * amount * velocityGain);
    auto outRight = float(r * amount * velocityGain);
    if (voice.stealSamples > 0)
    {
        const auto old = float(voice.stealSamples) / 64;
        outLeft = old * voice.stealLeft + (1 - old) * outLeft;
        outRight = old * voice.stealRight + (1 - old) * outRight;
        --voice.stealSamples;
    }
    voice.lastLeft = outLeft;
    voice.lastRight = outRight;
    left += outLeft;
    right += outRight;
    (void)lfo;
}
bool Engine::validEvents(std::span<const MidiEvent> events, int frames) const noexcept
{
    if (events.size() > 8192)
        return false;
    int previous = -1;
    for (const auto &message : events)
    {
        if (message.type < MidiType::noteOn || message.type > MidiType::allSoundOff ||
            message.sampleOffset < previous || message.sampleOffset < 0 || message.sampleOffset >= frames ||
            message.channel < 1 || message.channel > 16 || !noteRange(message.note) ||
            !inRange(message.value, message.type == MidiType::pitchBend ? -1.0f : 0.0f, 1.0f))
            return false;
        previous = message.sampleOffset;
    }
    return true;
}
bool Engine::process(float *left, float *right, int frames, std::span<const MidiEvent> events) noexcept
{
    peaks_[0] = peaks_[1] = 0;
    if (frames < 0 || (frames > 0 && (!left || !right)))
        return false;
    if (!prepared_ || !validEvents(events, frames))
    {
        if (frames > 0)
        {
            std::fill_n(left, frames, 0.0f);
            std::fill_n(right, frames, 0.0f);
        }
        panic();
        return false;
    }
    std::size_t nextEvent = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        smooth();
        // Shared segment rates for this frame; independent of any voice state.
        const auto samples = [&](float ms) { return std::max(1.0, ms * .001 * sampleRate_); };
        if (current_.envelopeCurve > 0)
        {
            attackExp_ = float(1 - std::exp(-1.4663 / samples(current_.attackMs)));
            decayExp_ = float(1 - std::exp(-4.6 / samples(current_.decayMs)));
            releaseExp_ = float(1 - std::exp(-4.6 / samples(current_.releaseMs)));
        }
        if (current_.modDecayMs > 0)
        {
            modAttackLinear_ = float(1 / samples(current_.modAttackMs));
            modAttackExp_ = float(1 - std::exp(-1.4663 / samples(current_.modAttackMs)));
            modDecayLinear_ = float((1 - current_.modSustain) / samples(current_.modDecayMs));
            modDecayExp_ = float(1 - std::exp(-4.6 / samples(current_.modDecayMs)));
        }
        glideCoefficient_ = current_.glideMs > 0 ? float(std::exp(-3 / samples(current_.glideMs))) : 0.0f;
        if (current_.unisonVoices > 1)
        {
            const int count = current_.unisonVoices;
            unisonGain_ = float(1 / std::sqrt(double(count)));
            for (int u = 0; u < count; ++u)
            {
                const double offset = 2.0 * u / (count - 1) - 1;
                unisonRatio_[std::size_t(u)] = std::exp2(offset * current_.unisonDetuneCents * .5 / 1200);
                const double pan = current_.unisonSpread * (u % 2 == 0 ? offset : -offset);
                unisonLeft_[std::size_t(u)] = float((1 - pan) * unisonGain_);
                unisonRight_[std::size_t(u)] = float((1 + pan) * unisonGain_);
            }
        }
        while (nextEvent < events.size() && events[nextEvent].sampleOffset == frame)
            event(events[nextEvent++]);
        const auto lfo = std::sin(2 * pi * lfoPhase_);
        lfoPhase_ += current_.vibratoRateHz / sampleRate_;
        if (lfoPhase_ >= 1)
            lfoPhase_ -= 1;
        oscillator2Ratio_ = std::exp2(current_.oscillator2Semitones / 12.0 + current_.detuneCents / 1200.0);
        motionCutoff_ = std::exp2(current_.motion * 1.5 * lfo);
        for (std::size_t channel = 0; channel < 16; ++channel)
        {
            currentBends_[channel] += float(smoothing_ * (bends_[channel] - currentBends_[channel]));
            currentModulation_[channel] +=
                float(smoothing_ * (modulation_[channel] - currentModulation_[channel]));
            const auto bend = std::clamp(currentBends_[channel] + current_.pitchBend, -1.0f, 1.0f);
            const auto depth =
                current_.vibratoDepthCents * std::max(currentModulation_[channel], current_.modWheel) +
                current_.motion * 25;
            pitchMultipliers_[channel] =
                std::exp2((bend * current_.bendRangeSemitones + depth * lfo / 100) / 12.0);
        }
        float l = 0, r = 0;
        for (auto &voice : voices_)
            if (voice.active)
                render(voice, lfo, l, r);
        const auto gain = std::pow(10.0, current_.masterDb / 20.0) * .5;
        // Six dB fixed summing headroom, then an explicit soft ceiling. This is
        // sample-peak protection, with no true-peak or acoustic-realism claim.
        left[frame] = float(std::tanh(l * gain * std::sqrt(1 - current_.pan)));
        right[frame] = float(std::tanh(r * gain * std::sqrt(1 + current_.pan)));
        peaks_[0] = std::max(peaks_[0], std::abs(left[frame]));
        peaks_[1] = std::max(peaks_[1], std::abs(right[frame]));
    }
    return true;
}
std::size_t Engine::activeVoices() const noexcept
{
    return std::count_if(voices_.begin(), voices_.end(), [](const Voice &voice) { return voice.active; });
}
float Engine::peak(int channel) const noexcept
{
    return peaks_[channel == 0 ? 0 : 1];
}
} // namespace vibera::synth
