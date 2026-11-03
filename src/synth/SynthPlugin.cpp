// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SynthPlugin.h"
#include "SynthUserPresets.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <set>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <bcrypt.h>
 #pragma comment(lib, "bcrypt.lib")
#endif

namespace vibera::synth
{
class SynthPlugin::Parameter final : public juce::HostedAudioProcessorParameter
{
    SynthPlugin &owner;
    const ParameterSpec &spec;
    juce::NormalisableRange<float> range;
    std::atomic<float> value;

  public:
    Parameter(SynthPlugin &o, const ParameterSpec &s)
        : HostedAudioProcessorParameter(1), owner(o), spec(s),
          range(s.minimum, s.maximum, s.interval, s.skew), value(range.convertTo0to1(s.initial))
    {
    }
    juce::String getParameterID() const override
    {
        return juce::String(juce::int64(nativeId(spec.id)));
    }
    float getValue() const override
    {
        return value.load(std::memory_order_relaxed);
    }
    void store(float v) noexcept
    {
        value.store(v, std::memory_order_relaxed);
    }
    float normalized(float native) const noexcept
    {
        return range.convertTo0to1(range.snapToLegalValue(std::clamp(native, spec.minimum, spec.maximum)));
    }
    float actual() const noexcept
    {
        return range.snapToLegalValue(range.convertFrom0to1(getValue()));
    }
    float nativeValue(float normalizedValue) const noexcept
    {
        return range.snapToLegalValue(range.convertFrom0to1(normalizedValue));
    }
    bool legalNormalized(float v) const noexcept
    {
        return std::isfinite(v) && v >= 0 && v <= 1 &&
               (spec.interval <= 0 || std::abs(v - normalized(nativeValue(v))) <= 1.0e-6f);
    }
    void setValue(float v) override
    {
        if (!std::isfinite(v))
            return;
        v = std::clamp(v, 0.f, 1.f);
        if (spec.interval > 0)
            v = normalized(range.convertFrom0to1(v));
        if ((spec.id == ParameterId::sourceType || spec.id == ParameterId::factoryPreset) &&
            !(owner.patchSequence_.load(std::memory_order_acquire) & 1u))
        {
            const std::pair<std::uint32_t, float> candidate{nativeId(spec.id), v};
            if (!owner.validateParameterPatch({&candidate, 1}))
            {
                owner.sourceError_.store(true, std::memory_order_relaxed);
                return;
            }
            owner.sourceError_.store(false, std::memory_order_relaxed);
        }
        const auto previous = value.exchange(v, std::memory_order_relaxed);
        if (spec.id == ParameterId::factoryPreset && previous != v)
            owner.presetRevision_.fetch_add(1, std::memory_order_release);
    }
    float getDefaultValue() const override
    {
        return normalized(spec.initial);
    }
    juce::String getName(int n) const override
    {
        return juce::String(spec.name).substring(0, n > 0 ? n : 1024);
    }
    juce::String getLabel() const override
    {
        return spec.unit;
    }
    int getNumSteps() const override
    {
        return spec.interval > 0 ? 1 + int(std::lround((spec.maximum - spec.minimum) / spec.interval))
                                 : getDefaultNumParameterSteps();
    }
    bool isDiscrete() const override
    {
        return spec.interval > 0;
    }
    bool isBoolean() const override
    {
        return isDiscrete() && getNumSteps() == 2;
    }
    juce::String getText(float v, int n) const override
    {
        const auto text = juce::String(range.convertFrom0to1(std::clamp(v, 0.f, 1.f)), isDiscrete() ? 0 : 2);
        return n > 0 ? text.substring(0, n) : text;
    }
    float getValueForText(const juce::String &text) const override
    {
        const auto token = text.trim();
        char *end = nullptr;
        const auto native = std::strtod(token.toRawUTF8(), &end);
        if (token.isEmpty() || !end || *end != '\0' || !std::isfinite(native))
            return getValue();
        return normalized(float(std::clamp(native, double(spec.minimum), double(spec.maximum))));
    }
};

SynthPlugin::SynthPlugin()
    : AudioPluginInstance(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    for (const auto &spec : parameterSpecs())
    {
        auto p = std::make_unique<Parameter>(*this, spec);
        parameters_[parameterIndex(spec.id)] = p.get();
        addHostedParameter(std::move(p));
    }
    loadLibrary();
    applyFactory(0);
    contentReady_ = contentError_.isEmpty();
    dspValues_ = values();
    telemetry_.store((std::uint64_t(1) << 63) | (std::uint64_t(contentReady_) << 32) |
                         (std::uint64_t(presetMaps_[0] != nullptr) << 33) |
                         (std::uint64_t(factoryPresets()[0].source) << 24),
                     std::memory_order_relaxed);
    keyboard_.addListener(this);
}
SynthPlugin::~SynthPlugin()
{
    keyboard_.removeListener(this);
}
juce::File SynthPlugin::factoryDirectory()
{
    // Never search the developer checkout or the current working directory.
    return juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile("Content/Synthesizer");
}
juce::File SynthPlugin::userDirectory()
{
    return synthUserDirectory();
}
// Factory sample integrity check. juce::SHA256 hashed the 170 MB library in ~4 s on every
// synth start (adding the instrument, opening a project, each export); the system SHA-256 is
// the same digest at memory speed.
static juce::String sha256Hex(const juce::File &file)
{
#if JUCE_WINDOWS
    juce::MemoryBlock data;
    if (!file.loadFileAsData(data) || data.getSize() > 0xffffffffu)
        return {};
    unsigned char digest[32]{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, static_cast<PUCHAR>(data.getData()), ULONG(data.getSize()),
                   digest, ULONG(sizeof(digest))) != 0)
        return {};
    return juce::String::toHexString(digest, int(sizeof(digest)), 0);
#else
    return juce::SHA256(file).toHexString();
#endif
}
void SynthPlugin::loadLibrary()
{
    auto directory = factoryDirectory();
    const auto manifestFile = directory.getChildFile("library.json");
    const auto manifest =
        manifestFile.getSize() <= 2 * 1024 * 1024 ? juce::JSON::parse(manifestFile) : juce::var();
    const auto *instruments = manifest["instruments"].getArray();
    if (!instruments || int(manifest["schemaVersion"]) != 1)
        contentError_ = "Factory content missing or corrupt: reinstall Content/Synthesizer/library.json "
                        "beside VibeRa Studio.";
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::int64_t decodedFrames = 0;
    std::set<juce::String> identities;
    if (instruments && instruments->size() > 64)
        contentError_ = "Factory instrument count exceeds64";
    if (instruments && instruments->size() <= 64 && int(manifest["schemaVersion"]) == 1)
        for (const auto &instrument : *instruments)
        {
            const auto identity = instrument["id"].toString();
            if (identity.isEmpty() || !identities.insert(identity).second)
            {
                contentError_ = "Duplicate or empty factory instrument identity";
                continue;
            }
            std::vector<SampleZone> zones;
            juce::String error;
            auto *regions = instrument["regions"].getArray();
            if (!regions || regions->isEmpty() || regions->size() > 512)
                error = "Invalid region count (1..512 required)";
            if (regions && error.isEmpty())
                for (const auto &region : *regions)
                {
                    const auto relative = region["sample"].toString();
                    const auto file = directory.getChildFile(relative);
                    if (relative.isEmpty() || juce::File::isAbsolutePath(relative) ||
                        !file.isAChildOf(directory))
                    {
                        error = "Unsafe bundled sample reference";
                        break;
                    }
                    const auto expectedHash = region["sha256"].toString();
                    if (expectedHash.length() != 64 || file.getSize() != juce::int64(region["sourceBytes"]) ||
                        !sha256Hex(file).equalsIgnoreCase(expectedHash))
                    {
                        error = "Missing or corrupt factory sample (SHA256): " + relative;
                        break;
                    }
                    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
                    if (!reader || reader->lengthInSamples < 2 || reader->lengthInSamples > 48000000 ||
                        reader->numChannels < 1 || reader->numChannels > 2 ||
                        decodedFrames + reader->lengthInSamples > 96000000)
                    {
                        error = "Missing, corrupt or oversized sample: " + relative;
                        break;
                    }
                    const int sourceOffset = int(region.getProperty("sourceOffsetFrames", 0));
                    if (sourceOffset < 0 || sourceOffset >= reader->lengthInSamples - 1)
                    {
                        error = "Invalid source offset: " + relative;
                        break;
                    }
                    const int frames = int(reader->lengthInSamples) - sourceOffset;
                    juce::AudioBuffer<float> pcm(int(reader->numChannels), frames);
                    if (!reader->read(&pcm, 0, frames, sourceOffset, true, true))
                    {
                        error = "Cannot decode factory sample: " + relative;
                        break;
                    }
                    SampleZone z;
                    z.left.assign(pcm.getReadPointer(0), pcm.getReadPointer(0) + frames);
                    if (pcm.getNumChannels() > 1)
                        z.right.assign(pcm.getReadPointer(1), pcm.getReadPointer(1) + frames);
                    z.sampleRate =
                        reader->sampleRate * std::pow(2., double(region.getProperty("tuneCents", 0)) / 1200.);
                    z.roundRobinGroup = int(region.getProperty("roundRobinGroup", 0));
                    z.roundRobinIndex = int(region.getProperty("roundRobinIndex", 0));
                    z.rootNote = int(region["rootNote"]);
                    z.lowNote = int(region["lowNote"]);
                    z.highNote = int(region["highNote"]);
                    z.lowVelocity = int(region["lowVelocity"]);
                    z.highVelocity = int(region["highVelocity"]);
                    z.gainDb = float(region["gainDb"]);
                    z.pan = float(region.getProperty("pan", 0));
                    if (region["loop"]["mode"].toString() == "forward")
                    {
                        z.loopEnabled = true;
                        z.loopStart = std::size_t(juce::int64(region["loop"]["startFrame"]));
                        z.loopEnd = std::size_t(juce::int64(region["loop"]["endFrame"]));
                    }
                    decodedFrames += frames;
                    zones.push_back(std::move(z));
                }
            // The curated maps keep -12 dB source headroom, which left recorded
            // instruments 10-15 dB below the oscillator presets. Normalize each
            // instrument once, off the audio thread: its loudest zone (after its
            // authored gain) reaches oscillator full scale, so zone-to-zone
            // dynamics are preserved. The output limiter still guards the sum.
            if (error.isEmpty())
            {
                float loudest = 0;
                for (const auto &zone : zones)
                {
                    const float gain = std::pow(10.0f, zone.gainDb / 20);
                    for (const auto *pcm : {&zone.left, &zone.right})
                        for (const float sample : *pcm)
                            loudest = std::max(loudest, std::abs(sample) * gain);
                }
                if (loudest > 0 && std::isfinite(loudest))
                {
                    const float normalize = std::clamp(1.0f / loudest, 1.0f, 15.85f); // at most +24 dB
                    for (auto &zone : zones)
                        for (auto *pcm : {&zone.left, &zone.right})
                            for (auto &sample : *pcm)
                                sample *= normalize;
                }
            }
            std::string validation;
            auto map = error.isEmpty() ? PreparedSampleMap::create(std::move(zones), validation) : nullptr;
            if (!map)
            {
                contentError_ = "Factory instrument " + instrument["id"].toString() + ": " + error +
                                juce::String(validation);
                continue;
            }
            mapIds_.push_back(instrument["id"].toString());
            maps_.push_back(std::move(map));
        }
    for (const auto &preset : factoryPresets())
    {
        const PreparedSampleMap *map = nullptr;
        for (std::size_t i = 0; i < mapIds_.size(); ++i)
            if (mapIds_[i] == preset.sampleAsset)
                map = maps_[i].get();
        if (!map && preset.source != SourceKind::synthesis)
        {
            if (contentError_.isNotEmpty())
                contentError_ += "\n";
            contentError_ += "Factory preset " + juce::String(preset.id) + " requires recorded instrument " +
                             preset.sampleAsset + ". Reinstall Content/Synthesizer.";
        }
        presetMaps_.push_back(map);
    }
}
void SynthPlugin::applyFactory(int index) noexcept
{
    if (index < 0 || std::size_t(index) >= factoryPresets().size())
        return;
    const auto &preset = factoryPresets()[std::size_t(index)];
    for (std::size_t i = 0; i < parameterCount; ++i)
        parameters_[i]->store(parameters_[i]->normalized(
            i == parameterIndex(ParameterId::factoryPreset) ? float(index) : preset.values[i]));
    presetRevision_.fetch_add(1, std::memory_order_release);
}
Values SynthPlugin::values() noexcept
{
    const auto before = patchSequence_.load(std::memory_order_acquire);
    if (before & 1u)
        return dspValues_;
    Values v{};
    for (std::size_t i = 0; i < parameterCount; ++i)
        v[i] = parameters_[i]->actual();
    if (before != patchSequence_.load(std::memory_order_acquire))
        return dspValues_;
    dspValues_ = v;
    return v;
}
juce::AudioProcessorParameter *SynthPlugin::parameterForId(juce::uint32 id) const noexcept
{
    return id >= 0x10000 && id - 0x10000 < parameterCount ? parameters_[id - 0x10000] : nullptr;
}
bool SynthPlugin::validateParameterPatch(
    std::span<const std::pair<std::uint32_t, float>> patch) const noexcept
{
    int preset = int(parameters_[parameterIndex(ParameterId::factoryPreset)]->actual());
    int source = int(parameters_[parameterIndex(ParameterId::sourceType)]->actual());
    for (const auto &[id, value] : patch)
    {
        if (id == nativeId(ParameterId::factoryPreset))
            preset =
                int(std::lround(parameters_[parameterIndex(ParameterId::factoryPreset)]->nativeValue(value)));
        if (id == nativeId(ParameterId::sourceType))
            source =
                int(std::lround(parameters_[parameterIndex(ParameterId::sourceType)]->nativeValue(value)));
    }
    return preset >= 0 && std::size_t(preset) < presetMaps_.size() && source >= 0 && source <= 2 &&
           (source == 0 || presetMaps_[std::size_t(preset)] != nullptr);
}
bool SynthPlugin::parameterInfo(juce::uint32 id, ParameterInfo &info) const
{
    const auto *p = parameterForId(id);
    if (!p)
        return false;
    info = {};
    info.id = id;
    info.flags = 1;
    info.defaultValue = p->getDefaultValue();
    info.stepCount = p->isDiscrete() ? p->getNumSteps() - 1 : 0;
    return true;
}
bool SynthPlugin::parameterValue(juce::uint32 id, float &v) const
{
    const auto *p = parameterForId(id);
    if (!p)
        return false;
    v = p->getValue();
    return true;
}
bool SynthPlugin::setParameterEventsForNextBlock(juce::Span<const ParameterEvent> events, int frames) noexcept
{
    eventCount_ = 0;
    eventFrames_ = 0;
    int previous = -1;
    if (frames < 1 || events.size() > events_.size())
        return false;
    for (const auto &e : events)
    {
        if (!parameterForId(e.id) || e.sampleOffset < previous || e.sampleOffset < 0 ||
            e.sampleOffset >= frames || !std::isfinite(e.value) || e.value < 0 || e.value > 1)
            return false;
        previous = e.sampleOffset;
    }
    std::copy(events.begin(), events.end(), events_.begin());
    eventCount_ = events.size();
    eventFrames_ = frames;
    return true;
}
bool SynthPlugin::flushParameterChangesForState() noexcept
{
    // Current targets are already held in scalar atomics. Future timed events
    // must remain future events; state capture must never advance their cursor.
    return true;
}
void SynthPlugin::prepareToPlay(double rate, int)
{
    rate_ = rate;
    masterSmoothing_ = float(1 - std::exp(-1 / (.005 * rate)));
    masterConfigured_ = false;
    limiterGain_ = 1;
    limiterRelease_ = float(1 - std::exp(-1 / (.08 * rate)));
    clearHostHeld();
    engine_.prepare(rate);
    for (auto &fx : effects_)
        fx.prepare(rate);
    eventCount_ = 0;
    eventFrames_ = 0;
    appliedPresetRevision_ = ~0u;
    boundPreset_ = -1;
    boundSource_ = -1;
    prepared_ = true;
}
void SynthPlugin::releaseResources()
{
    prepared_ = false;
    eventCount_ = 0;
    eventFrames_ = 0;
    engine_.panic();
    noteOwners_.reset();
    clearHostHeld();
}
void SynthPlugin::reset()
{
    panicRequested_.store(true, std::memory_order_release);
}
bool SynthPlugin::isBusesLayoutSupported(const BusesLayout &b) const
{
    return b.inputBuses.isEmpty() && b.outputBuses.size() == 1 &&
           b.outputBuses[0] == juce::AudioChannelSet::stereo();
}
void SynthPlugin::configure(const Values &v) noexcept
{
    auto get = [&](ParameterId id) { return v[parameterIndex(id)]; };
    const auto index = std::clamp(int(get(ParameterId::factoryPreset)), 0, int(factoryPresets().size()) - 1);
    const auto &macro = factoryPresets()[std::size_t(index)].macros;
    processedPreset_ = index;
    processedSource_ = int(get(ParameterId::sourceType));
    const auto revision = presetRevision_.load(std::memory_order_acquire);
    if (appliedPresetRevision_ != revision || boundPreset_ != index || boundSource_ != processedSource_)
    {
        engine_.panic();
        noteOwners_.reset();
        engine_.bindSampleMap(presetMaps_[std::size_t(index)]);
        for (auto &fx : effects_)
            fx.reset();
        appliedPresetRevision_ = revision;
        boundPreset_ = index;
        boundSource_ = processedSource_;
    }
    Parameters p;
    p.sourceMode = static_cast<SourceMode>(int(get(ParameterId::sourceType)));
    p.waveform1 = static_cast<Waveform>(int(get(ParameterId::oscillatorType)));
    p.waveform2 = static_cast<Waveform>(int(get(ParameterId::oscillator2Type)));
    p.oscillator2Level = get(ParameterId::oscillatorBlend);
    p.oscillator1Level = 1 - p.oscillator2Level;
    p.oscillator2Semitones = get(ParameterId::oscillator2Semitones);
    p.detuneCents = get(ParameterId::detuneCents);
    p.pulseWidth = get(ParameterId::pulseWidth);
    p.sampleLevel = get(ParameterId::sampleLevel);
    p.attackMs = get(ParameterId::attackMs);
    p.decayMs = get(ParameterId::decayMs);
    p.sustain = get(ParameterId::sustain);
    p.releaseMs = get(ParameterId::releaseMs);
    p.filterType = static_cast<FilterType>(int(get(ParameterId::filterType)));
    // Tone is a multiplicative timbral offset around the explicit filter value.
    const float tone = get(ParameterId::tone);
    const float macroHz = macro.toneMinimumHz * std::pow(macro.toneMaximumHz / macro.toneMinimumHz, tone);
    const float neutralHz = std::sqrt(macro.toneMinimumHz * macro.toneMaximumHz);
    p.cutoffHz = std::clamp(get(ParameterId::cutoffHz) * macroHz / neutralHz, 20.f, 20000.f);
    p.resonance = .5f + get(ParameterId::resonance) * 7.5f;
    p.filterEnvelopeOctaves = get(ParameterId::filterEnvelopeOctaves);
    p.masterDb = -12;
    masterTarget_ = std::pow(10.0f, (get(ParameterId::masterDb) + 12 + outputMakeupDb) / 20);
    if (!masterConfigured_)
    {
        masterGain_ = masterTarget_;
        masterConfigured_ = true;
    }
    p.pan = get(ParameterId::pan);
    p.pitchBend = get(ParameterId::pitchBend);
    p.modWheel = get(ParameterId::modWheel);
    p.bendRangeSemitones = get(ParameterId::bendRangeSemitones);
    p.polyphony = int(get(ParameterId::polyphony));
    p.vibratoRateHz = get(ParameterId::vibratoRateHz);
    p.vibratoDepthCents =
        std::clamp(get(ParameterId::vibratoDepthCents) + macro.motionMaximumCents * get(ParameterId::motion),
                   0.f, 100.f);
    p.motion = get(ParameterId::motion);
    p.unisonVoices = int(get(ParameterId::unisonVoices));
    p.wavetableBank = int(get(ParameterId::wavetableBank));
    p.wavetablePosition = get(ParameterId::wavetablePosition);
    p.wavetableEnvelope = get(ParameterId::wavetableEnvelope);
    p.unisonDetuneCents = get(ParameterId::unisonDetuneCents);
    p.unisonSpread = get(ParameterId::unisonSpread);
    p.subLevel = get(ParameterId::subLevel);
    p.noiseLevel = get(ParameterId::noiseLevel);
    p.ladderFilter = get(ParameterId::filterModel) >= .5f;
    p.filterDrive = get(ParameterId::filterDrive);
    p.filterKeyTrack = get(ParameterId::filterKeyTrack);
    p.filterVelocityOctaves = get(ParameterId::filterVelocityOctaves);
    p.modAttackMs = get(ParameterId::modAttackMs);
    p.modDecayMs = get(ParameterId::modDecayMs);
    p.modSustain = get(ParameterId::modSustain);
    p.envelopeCurve = get(ParameterId::envelopeCurve);
    p.velocityToAmp = get(ParameterId::velocityToAmp);
    p.glideMs = get(ParameterId::glideMs);
    p.voiceMode = static_cast<VoiceMode>(std::clamp(int(get(ParameterId::voiceMode)), 0, 2));
    p.fmAmount = get(ParameterId::fmAmount);
    p.analogDrift = get(ParameterId::analogDrift);
    (void)engine_.setParameters(p);
    const float warmth = get(ParameterId::warmth) * macro.warmthMaximumBoostDb;
    const float ambience = get(ParameterId::space) * macro.spaceMaximumMix;
    const std::array<effects::EffectId, 5> ids{effects::EffectId::graphicEq, effects::EffectId::compressor,
                                               effects::EffectId::chorus, effects::EffectId::delay,
                                               effects::EffectId::reverb};
    const std::array<ParameterId, 5> enables{ParameterId::eqEnabled, ParameterId::compressorEnabled,
                                             ParameterId::chorusEnabled, ParameterId::delayEnabled,
                                             ParameterId::reverbEnabled};
    for (std::size_t i = 0; i < 5; ++i)
    {
        effects::Values x{};
        const auto &spec = effects::effectSpec(ids[i]);
        for (std::size_t j = 0; j < spec.parameterCount; ++j)
            x[j] = spec.parameters[j].initial;
        switch (i)
        {
        case 0:
            x[0] = x[1] = std::clamp(get(ParameterId::eqLowDb) + warmth * .25f, -18.f, 18.f);
            x[2] = x[3] = get(ParameterId::eqMidDb);
            x[4] = x[5] = x[6] = get(ParameterId::eqHighDb);
            x[7] = -warmth * .12f;
            break;
        case 1:
            x[0] = get(ParameterId::compressorThresholdDb);
            x[1] = get(ParameterId::compressorRatio);
            break;
        case 2:
            x[2] = get(ParameterId::chorusMix);
            x[0] = .3f + get(ParameterId::motion) * 2;
            break;
        case 3:
            x[0] = get(ParameterId::delayTimeMs);
            x[1] = get(ParameterId::delayFeedback);
            x[2] = std::clamp(get(ParameterId::delayMix) + ambience * .35f, 0.f, 1.f);
            break;
        case 4:
            x[0] = get(ParameterId::reverbSize);
            x[1] = get(ParameterId::reverbDecaySeconds);
            x[4] = std::clamp(get(ParameterId::reverbMix) + ambience, 0.f, 1.f);
            break;
        }
        const bool enabled = get(enables[i]) >= .5f;
        if (!enabled && effectEnabled_[i])
            effects_[i].reset();
        effectEnabled_[i] = enabled;
        (void)effects_[i].setParameters(ids[i], x);
    }
}
void SynthPlugin::render(float *l, float *r, int n, std::span<const MidiEvent> midi,
                         std::span<const int> sources) noexcept
{
    configure(values());
    std::size_t count = 0;
    bool overflow = false;
    for (std::size_t i = 0; i < midi.size(); ++i)
    {
        const auto &input = midi[i];
        const auto append = [&](const MidiEvent &e)
        {
            if (count < ownedMidi_.size())
                ownedMidi_[count++] = e;
            else
                overflow = true;
        };
        if (input.type == MidiType::noteOn || input.type == MidiType::noteOff ||
            input.type == MidiType::allNotesOff || input.type == MidiType::allSoundOff)
        {
            // Mirror external key state for the editor keyboard; GUI notes (source 2)
            // are already shown by the MidiKeyboardState itself.
            if (sources[i] != 2)
            {
                if (input.type == MidiType::allNotesOff || input.type == MidiType::allSoundOff)
                    clearHostHeld();
                else
                {
                    const auto bit = std::uint64_t(1) << (input.note & 63);
                    auto &bits = hostHeld_[std::size_t((input.note & 127) >> 6)];
                    if (input.type == MidiType::noteOn)
                        bits.fetch_or(bit, std::memory_order_relaxed);
                    else
                        bits.fetch_and(~bit, std::memory_order_relaxed);
                }
            }
            const bool on = input.type == MidiType::noteOn, off = input.type == MidiType::noteOff;
            const std::uint8_t status = std::uint8_t((on ? 0x90 : off ? 0x80 : 0xb0) | (input.channel - 1));
            ipc::MidiEvent event{
                input.sampleOffset,
                {status,
                 std::uint8_t(on || off                             ? input.note
                              : input.type == MidiType::allSoundOff ? 120
                                                                    : 123),
                 std::uint8_t(on ? std::clamp(int(std::lround(input.value * 127)), 1, 127) : 0)},
                3};
            // Incoming Host MIDI is already coalesced by its source owners.
            // Neither its final release nor the editor keyboard's release may
            // cancel the other input's held same-pitch note.
            noteOwners_.process(sources[i], event,
                                [&](const ipc::MidiEvent &owned)
                                {
                                    auto output = input;
                                    if ((owned.bytes[0] & 0xf0) == 0x80)
                                    {
                                        output.type = MidiType::noteOff;
                                        output.note = owned.bytes[1];
                                        output.value = 0;
                                    }
                                    append(output);
                                });
        }
        else
            append(input);
    }
    if (overflow)
    {
        engine_.panic();
        noteOwners_.reset();
        for (auto &fx : effects_)
            fx.reset();
        std::fill_n(l, n, 0.f);
        std::fill_n(r, n, 0.f);
        return;
    }
    (void)engine_.process(l, r, n, {ownedMidi_.data(), count});
    for (std::size_t i = 0; i < 5; ++i)
        if (effectEnabled_[i])
            effects_[i].process(l, r, n);
    // Stereo-linked peak limiter: instant attack, 80 ms release, -1 dBFS ceiling.
    constexpr float ceiling = .891f;
    for (int sample = 0; sample < n; ++sample)
    {
        masterGain_ += masterSmoothing_ * (masterTarget_ - masterGain_);
        const float left = l[sample] * masterGain_, right = r[sample] * masterGain_;
        const float level = std::max(std::abs(left), std::abs(right));
        const float target = level > ceiling ? ceiling / level : 1.0f;
        limiterGain_ =
            target < limiterGain_ ? target : limiterGain_ + limiterRelease_ * (target - limiterGain_);
        l[sample] = left * limiterGain_;
        r[sample] = right * limiterGain_;
    }
}
void SynthPlugin::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ScopedNoDenormals denormals;
    buffer.clear();
    if (!prepared_ || buffer.getNumChannels() < 2)
        return;
    if (eventCount_ != 0 && eventFrames_ != buffer.getNumSamples())
    {
        eventCount_ = 0;
        eventFrames_ = 0;
        engine_.panic();
        noteOwners_.reset();
        for (auto &fx : effects_)
            fx.reset();
        voices_.store(0, std::memory_order_relaxed);
        for (auto &peak : peaks_)
            peak.store(0, std::memory_order_relaxed);
        midi.clear();
        return;
    }
    if (buffer.getNumSamples() == 0)
        return;
    if (panicRequested_.exchange(false, std::memory_order_acq_rel))
    {
        clearHostHeld();
        engine_.panic();
        noteOwners_.reset();
        for (auto &fx : effects_)
            fx.reset();
        guiRead_.store(guiWrite_.load(std::memory_order_acquire), std::memory_order_release);
    }
    std::size_t count = 0;
    auto read = guiRead_.load(std::memory_order_relaxed);
    const auto write = guiWrite_.load(std::memory_order_acquire);
    while (read != write && count < midi_.size())
    {
        midiSources_[count] = 2;
        midi_[count++] = guiEvents_[read % guiEvents_.size()];
        ++read;
    }
    guiRead_.store(read, std::memory_order_release);
    for (const auto metadata : midi)
    {
        if (metadata.numBytes < 1)
            continue;
        const auto *data = metadata.data;
        const unsigned status = data[0] & 0xf0;
        MidiEvent e;
        e.sampleOffset = std::clamp(metadata.samplePosition, 0, std::max(0, buffer.getNumSamples() - 1));
        e.channel = (data[0] & 15) + 1;
        if (status == 0x90 && metadata.numBytes >= 3 && data[2] > 0)
        {
            e.type = MidiType::noteOn;
            e.note = data[1] & 127;
            e.value = (data[2] & 127) / 127.f;
        }
        else if ((status == 0x80 || status == 0x90) && metadata.numBytes >= 3)
        {
            e.type = MidiType::noteOff;
            e.note = data[1] & 127;
        }
        else if (status == 0xe0 && metadata.numBytes >= 3)
        {
            e.type = MidiType::pitchBend;
            e.value = std::clamp(((data[1] & 127) + 128 * (data[2] & 127) - 8192) / 8192.f, -1.f, 1.f);
        }
        else if (status == 0xb0 && metadata.numBytes >= 3)
        {
            if (data[1] == 64)
            {
                e.type = MidiType::sustain;
                e.value = data[2] >= 64 ? 1.f : 0.f;
            }
            else if (data[1] == 1)
            {
                e.type = MidiType::modWheel;
                e.value = (data[2] & 127) / 127.f;
            }
            else if (data[1] == 120)
                e.type = MidiType::allSoundOff;
            else if (data[1] == 123)
                e.type = MidiType::allNotesOff;
            else
                continue;
        }
        else
            continue;
        if (count == midi_.size())
        {
            engine_.panic();
            noteOwners_.reset();
            count = 0;
            break;
        }
        midiSources_[count] = 1;
        midi_[count++] = e;
    }
    // GUI events precede already time-ordered incoming MIDI, both at offset0.
    // Insertion sort is bounded and preserves same-offset MIDI event ordering.
    for (std::size_t i = 1; i < count; ++i)
    {
        auto e = midi_[i];
        const auto source = midiSources_[i];
        auto j = i;
        while (j > 0 && midi_[j - 1].sampleOffset > e.sampleOffset)
        {
            midi_[j] = midi_[j - 1];
            midiSources_[j] = midiSources_[j - 1];
            --j;
        }
        midi_[j] = e;
        midiSources_[j] = source;
    }
    int cursor = 0;
    std::size_t event = 0, note = 0;
    while (cursor < buffer.getNumSamples())
    {
        while (event < eventCount_ && events_[event].sampleOffset <= cursor)
        {
            parameterForId(events_[event].id)->setValue(events_[event].value);
            ++event;
        }
        const int end = event < eventCount_ ? std::min(buffer.getNumSamples(), events_[event].sampleOffset)
                                            : buffer.getNumSamples();
        const auto start = note;
        while (note < count && midi_[note].sampleOffset < end)
        {
            midi_[note].sampleOffset -= cursor;
            ++note;
        }
        if (end > cursor)
            render(buffer.getWritePointer(0) + cursor, buffer.getWritePointer(1) + cursor, end - cursor,
                   {midi_.data() + start, note - start}, {midiSources_.data() + start, note - start});
        cursor = end;
    }
    eventCount_ = 0;
    eventFrames_ = 0;
    for (int c = 0; c < 2; ++c)
        peaks_[std::size_t(c)].store(buffer.getMagnitude(c, 0, buffer.getNumSamples()),
                                     std::memory_order_relaxed);
    voices_.store(int(engine_.activeVoices()), std::memory_order_relaxed);
    telemetry_.store((std::uint64_t(1) << 63) | std::uint64_t(engine_.activeVoices()) |
                         (std::uint64_t(processedPreset_) << 8) | (std::uint64_t(processedSource_) << 24) |
                         (std::uint64_t(contentReady_) << 32) |
                         (std::uint64_t(presetMaps_[std::size_t(processedPreset_)] != nullptr) << 33) |
                         (std::uint64_t(sourceError_.load(std::memory_order_relaxed)) << 34),
                     std::memory_order_relaxed);
    midi.clear();
}
float SynthPlugin::peak(int c) const noexcept
{
    return peaks_[std::size_t(std::clamp(c, 0, 1))].load(std::memory_order_relaxed);
}
void SynthPlugin::queueGui(MidiEvent e) noexcept
{
    const auto w = guiWrite_.load(std::memory_order_relaxed);
    if (w - guiRead_.load(std::memory_order_acquire) >= guiEvents_.size())
    {
        panicRequested_ = true;
        return;
    }
    guiEvents_[w % guiEvents_.size()] = e;
    guiWrite_.store(w + 1, std::memory_order_release);
}
void SynthPlugin::handleNoteOn(juce::MidiKeyboardState *, int ch, int note, float v)
{
    queueGui({MidiType::noteOn, 0, ch, note, v});
}
void SynthPlugin::handleNoteOff(juce::MidiKeyboardState *, int ch, int note, float)
{
    queueGui({MidiType::noteOff, 0, ch, note, 0});
}
int SynthPlugin::getCurrentProgram()
{
    return int(std::lround(parameters_[parameterIndex(ParameterId::factoryPreset)]->actual()));
}
void SynthPlugin::setCurrentProgram(int i)
{
    applyFactory(i);
}
const juce::String SynthPlugin::getProgramName(int i)
{
    return i >= 0 && std::size_t(i) < factoryPresets().size() ? factoryPresets()[std::size_t(i)].name : "";
}
juce::String SynthPlugin::currentPresetId() const
{
    return factoryPresets()[std::size_t(std::clamp(
                                int(parameters_[parameterIndex(ParameterId::factoryPreset)]->actual()), 0,
                                int(factoryPresets().size()) - 1))]
        .id;
}
juce::Result SynthPlugin::selectFactoryPreset(const juce::String &id)
{
    const auto *preset = findFactoryPreset(id.toStdString());
    if (!preset)
        return juce::Result::fail("synth.unknownPreset: rediscover factory presets");
    const auto index = std::size_t(preset - factoryPresets().data());
    if (preset->source != SourceKind::synthesis && !presetMaps_[index])
        return juce::Result::fail("synth.missingContent: " + contentError_);
    // One CONTROL gesture batch, including every dependent scalar. No hidden
    // program mutation: automation and history can witness the whole patch.
    beginParameterPatch();
    for (std::size_t i = 0; i < parameterCount; ++i)
        parameters_[i]->beginChangeGesture();
    for (std::size_t i = 0; i < parameterCount; ++i)
        parameters_[i]->setValueNotifyingHost(parameters_[i]->normalized(preset->values[i]));
    for (std::size_t i = 0; i < parameterCount; ++i)
        parameters_[i]->endChangeGesture();
    endParameterPatch();
    return juce::Result::ok();
}
void SynthPlugin::getStateInformation(juce::MemoryBlock &out)
{
    juce::XmlElement xml("VIBERA_SYNTHESIZER");
    xml.setAttribute("schemaVersion", 1);
    xml.setAttribute("preset", currentPresetId());
    for (std::size_t i = 0; i < parameterCount; ++i)
    {
        auto *p = xml.createNewChildElement("PARAM");
        p->setAttribute("id", parameters_[i]->getParameterID());
        p->setAttribute("value", double(parameters_[i]->getValue()));
    }
    copyXmlToBinary(xml, out);
}
bool SynthPlugin::acceptsStateEnvelope(const juce::XmlElement &xml) const
{
    return xml.hasTagName("VIBERA_SYNTHESIZER") && xml.getNumAttributes() == 2 &&
           xml.getStringAttribute("schemaVersion") == "1" &&
           findFactoryPreset(xml.getStringAttribute("preset").toStdString()) != nullptr;
}
bool SynthPlugin::restoreState(const void *data, int bytes)
{
    Values next{};
    if (parseSynthNativeState(data, bytes, next).failed())
        return false;
    for (std::size_t index = 0; index < parameterCount; ++index)
        parameters_[index]->store(next[index]);
    presetRevision_.fetch_add(1, std::memory_order_release);
    return true;
}
juce::Result SynthPlugin::saveUserPreset(const juce::File &file)
{
    juce::MemoryBlock state;
    getStateInformation(state);
    return writeSynthUserPreset(file, state);
}
juce::Result SynthPlugin::loadUserPreset(const juce::File &file)
{
    SynthUserPreset document;
    if (auto result = readSynthUserPreset(file, document); result.failed())
        return result;
    beginParameterPatch();
    for (auto *p : parameters_)
        p->beginChangeGesture();
    for (std::size_t i = 0; i < parameterCount; ++i)
        parameters_[i]->setValueNotifyingHost(document.normalizedValues[i]);
    for (auto *p : parameters_)
        p->endChangeGesture();
    endParameterPatch();
    return juce::Result::ok();
}
SynthBrowserState SynthPlugin::readBrowserState()
{
    return readSynthBrowserState();
}
juce::Result SynthPlugin::writeBrowserState(const SynthBrowserState &s)
{
    return writeSynthBrowserState(s);
}
juce::AudioProcessorEditor *SynthPlugin::createEditor()
{
    SynthEditor::Callbacks c;
    c.currentPresetId = [this] { return currentPresetId(); };
    c.selectFactoryPreset = [this](const juce::String &id) { return selectFactoryPreset(id); };
    c.saveUserPreset = [this](const juce::File &f) { return saveUserPreset(f); };
    c.loadUserPreset = [this](const juce::File &f) { return loadUserPreset(f); };
    c.browserState = [] { return readBrowserState(); };
    c.setBrowserState = [](const SynthBrowserState &s) { return writeBrowserState(s); };
    c.activeVoiceCount = [this] { return activeVoiceCount(); };
    c.externalNoteHeld = [this](int note) { return hostNoteHeld(note); };
    c.peak = [this](int i) { return peak(i); };
    c.loadStatus = [this]
    {
        if (sourceError_.load(std::memory_order_relaxed))
            return juce::String("Source rejected: this preset has no recorded sample map. Choose a sampled "
                                "factory sound first.");
        return contentError_.isNotEmpty() ? contentError_
                                          : juce::String("Factory content ready / Native instrument");
    };
    return new SynthEditor(*this, keyboard_, std::move(c));
}
} // namespace vibera::synth
