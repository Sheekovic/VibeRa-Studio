// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SynthPresets.h"

#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace vibera::synth
{
namespace
{
constexpr std::array<ParameterSpec, parameterCount> baseSpecs{{
    {ParameterId::tone, "tone", "Tone", 0, 1, .65f, ""},
    {ParameterId::warmth, "warmth", "Warmth", 0, 1, .15f, ""},
    {ParameterId::space, "space", "Space", 0, 1, .2f, ""},
    {ParameterId::motion, "motion", "Motion", 0, 1, .1f, ""},
    {ParameterId::attackMs, "attack-ms", "Attack", 1, 10000, 12, "ms", 0, .3f},
    {ParameterId::decayMs, "decay-ms", "Decay", 1, 10000, 420, "ms", 0, .3f},
    {ParameterId::sustain, "sustain", "Sustain", 0, 1, .78f, ""},
    {ParameterId::releaseMs, "release-ms", "Release", 5, 15000, 1200, "ms", 0, .3f},
    {ParameterId::filterType, "filter-type", "Filter type", 0, 3, 0, "", 1},
    {ParameterId::cutoffHz, "cutoff-hz", "Cutoff", 20, 20000, 8000, "Hz", 0, .25f},
    {ParameterId::resonance, "resonance", "Resonance", 0, 1, .15f, ""},
    {ParameterId::eqEnabled, "eq-enabled", "EQ enabled", 0, 1, 1, "", 1},
    {ParameterId::eqLowDb, "eq-low-db", "Low gain", -18, 18, 0, "dB"},
    {ParameterId::eqMidDb, "eq-mid-db", "Mid gain", -18, 18, 0, "dB"},
    {ParameterId::eqHighDb, "eq-high-db", "High gain", -18, 18, 0, "dB"},
    {ParameterId::compressorEnabled, "compressor-enabled", "Compressor enabled", 0, 1, 0, "", 1},
    {ParameterId::compressorThresholdDb, "compressor-threshold-db", "Threshold", -60, 0, -18, "dB"},
    {ParameterId::compressorRatio, "compressor-ratio", "Ratio", 1, 20, 3, ":1", 0, .5f},
    {ParameterId::chorusEnabled, "chorus-enabled", "Chorus enabled", 0, 1, 0, "", 1},
    {ParameterId::chorusMix, "chorus-mix", "Chorus mix", 0, 1, .25f, ""},
    {ParameterId::delayEnabled, "delay-enabled", "Delay enabled", 0, 1, 0, "", 1},
    {ParameterId::delayTimeMs, "delay-time-ms", "Delay time", 1, 2000, 300, "ms", 0, .4f},
    {ParameterId::delayFeedback, "delay-feedback", "Delay feedback", 0, .9f, .3f, ""},
    {ParameterId::delayMix, "delay-mix", "Delay mix", 0, 1, .2f, ""},
    {ParameterId::reverbEnabled, "reverb-enabled", "Reverb enabled", 0, 1, 1, "", 1},
    {ParameterId::reverbSize, "reverb-size", "Room size", 0, 1, .6f, ""},
    {ParameterId::reverbDecaySeconds, "reverb-decay-seconds", "Reverb decay", .1f, 12, 2, "s", 0, .5f},
    {ParameterId::reverbMix, "reverb-mix", "Reverb mix", 0, 1, .2f, ""},
    {ParameterId::masterDb, "master-db", "Master", -60, 6, -6, "dB"},
    {ParameterId::sourceType, "source-type", "Source", 0, 2, 0, "", 1},
    {ParameterId::oscillatorType, "oscillator-type", "Oscillator 1", 0, 4, 1, "", 1},
    {ParameterId::oscillatorBlend, "oscillator-blend", "Oscillator blend", 0, 1, .35f, ""},
    {ParameterId::detuneCents, "detune-cents", "Detune", -50, 50, 7, "cents"},
    {ParameterId::pitchBend, "pitch-bend", "Pitch bend", -1, 1, 0, ""},
    {ParameterId::modWheel, "mod-wheel", "Mod wheel", 0, 1, 0, ""},
    {ParameterId::oscillator2Type, "oscillator-2-type", "Oscillator 2", 0, 4, 1, "", 1},
    {ParameterId::oscillator2Semitones, "oscillator-2-semitones", "Oscillator 2 tuning", -24, 24, 0, "st", 1},
    {ParameterId::pulseWidth, "pulse-width", "Pulse width", .05f, .95f, .5f, ""},
    {ParameterId::sampleLevel, "sample-level", "Sample level", 0, 1, 1, ""},
    {ParameterId::filterEnvelopeOctaves, "filter-envelope-octaves", "Filter envelope", -4, 4, 0, "oct"},
    {ParameterId::pan, "pan", "Pan", -1, 1, 0, ""},
    {ParameterId::bendRangeSemitones, "bend-range-semitones", "Bend range", 0, 24, 2, "st", 1},
    {ParameterId::vibratoRateHz, "vibrato-rate-hz", "Vibrato rate", .05f, 12, 5, "Hz", 0, .5f},
    {ParameterId::vibratoDepthCents, "vibrato-depth-cents", "Vibrato depth", 0, 100, 0, "cents"},
    {ParameterId::polyphony, "polyphony", "Polyphony", 1, 64, 64, "voices", 1},
    {ParameterId::factoryPreset, "factory-preset", "Sound source identity", 0, 0, 0, "", 1},
    {ParameterId::unisonVoices, "unison-voices", "Unison voices", 1, 7, 1, "voices", 1},
    {ParameterId::unisonDetuneCents, "unison-detune-cents", "Unison detune", 0, 100, 20, "cents"},
    {ParameterId::unisonSpread, "unison-spread", "Unison width", 0, 1, .7f, ""},
    {ParameterId::subLevel, "sub-level", "Sub oscillator", 0, 1, 0, ""},
    {ParameterId::noiseLevel, "noise-level", "Noise", 0, 1, 0, ""},
    {ParameterId::filterModel, "filter-model", "Filter slope", 0, 1, 0, "", 1},
    {ParameterId::filterDrive, "filter-drive", "Filter drive", 0, 1, 0, ""},
    {ParameterId::filterKeyTrack, "filter-key-track", "Key tracking", 0, 1, 0, ""},
    {ParameterId::filterVelocityOctaves, "filter-velocity-octaves", "Velocity to cutoff", 0, 4, 0, "oct"},
    {ParameterId::modAttackMs, "mod-attack-ms", "Mod env attack", 0, 5000, 0, "ms", 0, .3f},
    {ParameterId::modDecayMs, "mod-decay-ms", "Mod env decay", 0, 10000, 0, "ms", 0, .3f},
    {ParameterId::modSustain, "mod-sustain", "Mod env sustain", 0, 1, 0, ""},
    {ParameterId::envelopeCurve, "envelope-curve", "Envelope curve", 0, 1, 0, ""},
    {ParameterId::velocityToAmp, "velocity-to-amp", "Velocity sensitivity", 0, 1, 1, ""},
    {ParameterId::glideMs, "glide-ms", "Glide", 0, 2000, 0, "ms", 0, .4f},
    {ParameterId::voiceMode, "voice-mode", "Voice mode", 0, 2, 0, "", 1},
    {ParameterId::fmAmount, "fm-amount", "FM amount", 0, 1, 0, ""},
    {ParameterId::analogDrift, "analog-drift", "Analog drift", 0, 1, 0, ""},
}};
constexpr std::array<const char *, 9> genres{"Pop",        "Jazz", "Funk",      "Hip-Hop", "R&B",
                                             "Electronic", "Rock", "Cinematic", "Arabic"};
constexpr std::array<const char *, 13> families{
    "Piano",         "Electric Piano", "Violin",      "Strings", "Bass",   "Guitar",
    "Brass",         "Woodwinds",      "Synth Leads", "Pads",    "Plucks", "Drums / Percussion",
    "Arabic / World"};

FactoryPreset oscillator(const char *id, const char *name, const char *genre, const char *family, int wave,
                         float attack, float decay, float sustain, float release, float cutoff,
                         float resonance, const char *description)
{
    auto values = defaultValues();
    const auto set = [&](ParameterId parameter, float value) { values[parameterIndex(parameter)] = value; };
    set(ParameterId::oscillatorType, float(wave));
    set(ParameterId::oscillator2Type, float(wave));
    set(ParameterId::attackMs, attack);
    set(ParameterId::decayMs, decay);
    set(ParameterId::sustain, sustain);
    set(ParameterId::releaseMs, release);
    set(ParameterId::cutoffHz, cutoff);
    set(ParameterId::resonance, resonance);
    return {id, name, genre, family, SourceKind::synthesis, "", description, values, {}};
}

FactoryPreset sampled(const char *id, const char *name, const char *genre, const char *family,
                      const char *asset, float attack, float release, const char *description)
{
    auto preset =
        oscillator(id, name, genre, family, 0, attack, 1800, .8f, release, 16000, .05f, description);
    preset.source = SourceKind::sample;
    preset.sampleAsset = asset;
    preset.values[parameterIndex(ParameterId::sourceType)] = 1;
    preset.values[parameterIndex(ParameterId::sampleLevel)] = 1;
    preset.values[parameterIndex(ParameterId::oscillatorBlend)] = 0;
    // Recorded notes are mostly transient; this matches their level to the
    // oscillator presets (measured through the export path, limiter-guarded).
    preset.values[parameterIndex(ParameterId::masterDb)] = 2;
    return preset;
}

using Id = ParameterId;
struct Setting
{
    ParameterId id;
    float value;
};
using Settings = std::initializer_list<Setting>;
enum Wave
{
    sine,
    saw,
    square,
    triangle,
    noise
};

// Production baseline for the original synthesis voicings: curved analogue-style
// segments, moderate velocity response, mild per-voice drift, half key tracking,
// the 24 dB ladder and a neutral Tone macro (so the written cutoff is literal).
Values production(Settings settings)
{
    auto values = defaultValues();
    const Settings baseline{{Id::tone, .5f},
                            {Id::warmth, .1f},
                            {Id::space, .1f},
                            {Id::motion, 0},
                            {Id::envelopeCurve, 1},
                            {Id::analogDrift, .18f},
                            {Id::velocityToAmp, .6f},
                            {Id::filterKeyTrack, .5f},
                            {Id::filterModel, 1},
                            {Id::detuneCents, 0},
                            {Id::oscillatorBlend, 0},
                            {Id::oscillatorType, saw},
                            {Id::oscillator2Type, saw},
                            {Id::reverbEnabled, 1},
                            {Id::reverbSize, .55f},
                            {Id::reverbDecaySeconds, 1.8f},
                            {Id::reverbMix, .14f},
                            {Id::vibratoRateHz, 5.2f},
                            {Id::vibratoDepthCents, 18},
                            {Id::polyphony, 24},
                            {Id::masterDb, -6}};
    for (const auto &setting : baseline)
        values[parameterIndex(setting.id)] = setting.value;
    for (const auto &setting : settings)
        values[parameterIndex(setting.id)] = setting.value;
    return values;
}
// Replaces one legacy oscillator patch's values in place. Its stable ID, index,
// name, genre/family and macro limits are unchanged, so earlier user presets and
// projects (which store their own complete scalars) keep validating.
void revoice(std::vector<FactoryPreset> &presets, std::string_view id, const char *description,
             Settings settings)
{
    const auto found = std::find_if(presets.begin(), presets.end(),
                                    [&](const FactoryPreset &preset) { return id == preset.id; });
    if (found == presets.end() || found->source != SourceKind::synthesis)
        throw std::logic_error("Unknown synthesis preset voicing");
    const auto index = found->values[parameterIndex(Id::factoryPreset)];
    found->values = production(settings);
    found->values[parameterIndex(Id::factoryPreset)] = index;
    found->description = description;
}
void append(std::vector<FactoryPreset> &presets, const char *id, const char *name, const char *genre,
            const char *family, const char *description, Settings settings)
{
    const std::string_view kind(family);
    const MacroMapping macros = kind == "Bass"   ? MacroMapping{80, 4200, 9, .2f, 20}
                                : kind == "Pads" ? MacroMapping{400, 18000, 12, .65f, 18}
                                                 : MacroMapping{400, 18000, 12, .45f, 25};
    auto values = production(settings);
    values[parameterIndex(Id::factoryPreset)] = float(presets.size());
    presets.push_back({id, name, genre, family, SourceKind::synthesis, "", description, values, macros});
}

void addProductionVoicings(std::vector<FactoryPreset> &p)
{
    // Recorded and hybrid patches keep their sources; curved releases give them
    // natural exponential tails instead of linear fades.
    for (auto &preset : p)
        if (preset.source != SourceKind::synthesis)
            preset.values[parameterIndex(Id::envelopeCurve)] = 1;

    // ---- Re-voiced legacy oscillator patches (same IDs and families) ----
    revoice(p, "pop-crystal", "Triangle/sine pluck with a decaying FM shimmer through a 24 dB ladder.",
            {{Id::oscillatorType, triangle},
             {Id::oscillator2Type, sine},
             {Id::oscillator2Semitones, 12},
             {Id::oscillatorBlend, .35f},
             {Id::fmAmount, .25f},
             {Id::cutoffHz, 900},
             {Id::resonance, .15f},
             {Id::filterEnvelopeOctaves, 3},
             {Id::modDecayMs, 300},
             {Id::attackMs, 1},
             {Id::decayMs, 800},
             {Id::sustain, 0},
             {Id::releaseMs, 450},
             {Id::delayEnabled, 1},
             {Id::delayTimeMs, 280},
             {Id::delayFeedback, .28f},
             {Id::delayMix, .16f},
             {Id::reverbMix, .2f},
             {Id::polyphony, 32}});
    revoice(p, "pop-air", "Wide five-voice unison saw ensemble pad with a slow filter bloom and chorus.",
            {{Id::detuneCents, 7},
             {Id::oscillatorBlend, .5f},
             {Id::unisonVoices, 5},
             {Id::unisonDetuneCents, 20},
             {Id::unisonSpread, .85f},
             {Id::cutoffHz, 1800},
             {Id::resonance, .08f},
             {Id::filterEnvelopeOctaves, 1.2f},
             {Id::modAttackMs, 900},
             {Id::modDecayMs, 2500},
             {Id::modSustain, .5f},
             {Id::attackMs, 450},
             {Id::decayMs, 1500},
             {Id::sustain, .9f},
             {Id::releaseMs, 2400},
             {Id::chorusEnabled, 1},
             {Id::chorusMix, .28f},
             {Id::reverbMix, .3f},
             {Id::reverbDecaySeconds, 4.2f},
             {Id::reverbSize, .8f},
             {Id::motion, .08f},
             {Id::vibratoRateHz, .25f},
             {Id::polyphony, 16},
             {Id::masterDb, -9}});
    revoice(p, "pop-round", "Rounded sine/triangle bass with a touch of drive and a sub octave.",
            {{Id::oscillatorType, sine},
             {Id::oscillator2Type, triangle},
             {Id::oscillatorBlend, .25f},
             {Id::subLevel, .3f},
             {Id::filterModel, 0},
             {Id::cutoffHz, 900},
             {Id::filterDrive, .2f},
             {Id::attackMs, 2},
             {Id::decayMs, 600},
             {Id::sustain, .7f},
             {Id::releaseMs, 160},
             {Id::velocityToAmp, .5f},
             {Id::voiceMode, 1},
             {Id::reverbEnabled, 0},
             {Id::compressorEnabled, 1},
             {Id::compressorThresholdDb, -18},
             {Id::compressorRatio, 3}});
    revoice(p, "pop-neon", "Three-voice unison saw chords with a soft filter envelope and chorus.",
            {{Id::detuneCents, 9},
             {Id::oscillatorBlend, .45f},
             {Id::unisonVoices, 3},
             {Id::unisonDetuneCents, 14},
             {Id::cutoffHz, 2200},
             {Id::resonance, .12f},
             {Id::filterEnvelopeOctaves, 1.5f},
             {Id::modDecayMs, 700},
             {Id::modSustain, .35f},
             {Id::attackMs, 8},
             {Id::decayMs, 900},
             {Id::sustain, .65f},
             {Id::releaseMs, 700},
             {Id::chorusEnabled, 1},
             {Id::chorusMix, .2f},
             {Id::reverbMix, .2f},
             {Id::masterDb, -8}});
    revoice(p, "jazz-mellow", "Smooth triangle lead with legato glide; mod wheel adds vibrato.",
            {{Id::oscillatorType, triangle},
             {Id::oscillator2Type, sine},
             {Id::oscillator2Semitones, 12},
             {Id::oscillatorBlend, .2f},
             {Id::filterModel, 0},
             {Id::cutoffHz, 2600},
             {Id::resonance, .05f},
             {Id::attackMs, 25},
             {Id::decayMs, 400},
             {Id::sustain, .85f},
             {Id::releaseMs, 260},
             {Id::voiceMode, 2},
             {Id::glideMs, 45},
             {Id::vibratoDepthCents, 22},
             {Id::delayEnabled, 1},
             {Id::delayTimeMs, 330},
             {Id::delayFeedback, .25f},
             {Id::delayMix, .14f}});
    revoice(p, "jazz-sub", "Soft sine/triangle bass for walking and melodic low parts.",
            {{Id::oscillatorType, sine},
             {Id::oscillator2Type, triangle},
             {Id::oscillatorBlend, .3f},
             {Id::filterModel, 0},
             {Id::cutoffHz, 600},
             {Id::filterDrive, .1f},
             {Id::attackMs, 6},
             {Id::decayMs, 900},
             {Id::sustain, .6f},
             {Id::releaseMs, 220},
             {Id::voiceMode, 1},
             {Id::reverbEnabled, 0}});
    revoice(p, "jazz-hush", "Slow triangle/saw bed with a gentle filter swell and long tail.",
            {{Id::oscillatorType, triangle},
             {Id::oscillatorBlend, .3f},
             {Id::detuneCents, 6},
             {Id::unisonVoices, 3},
             {Id::unisonDetuneCents, 12},
             {Id::unisonSpread, .6f},
             {Id::cutoffHz, 1100},
             {Id::resonance, .05f},
             {Id::filterEnvelopeOctaves, 1},
             {Id::modAttackMs, 1500},
             {Id::modDecayMs, 3000},
             {Id::modSustain, .6f},
             {Id::attackMs, 900},
             {Id::decayMs, 2000},
             {Id::sustain, .9f},
             {Id::releaseMs, 3000},
             {Id::chorusEnabled, 1},
             {Id::chorusMix, .3f},
             {Id::reverbMix, .35f},
             {Id::reverbDecaySeconds, 5},
             {Id::motion, .06f},
             {Id::vibratoRateHz, .2f},
             {Id::polyphony, 16}});
    revoice(p, "funk-square", "Snappy square/saw bass with resonant ladder envelope and drive.",
            {{Id::oscillatorType, square},
             {Id::pulseWidth, .45f},
             {Id::oscillatorBlend, .3f},
             {Id::cutoffHz, 260},
             {Id::resonance, .35f},
             {Id::filterEnvelopeOctaves, 2.6f},
             {Id::modDecayMs, 220},
             {Id::modSustain, .1f},
             {Id::filterDrive, .3f},
             {Id::filterVelocityOctaves, 1.2f},
             {Id::attackMs, 1},
             {Id::decayMs, 400},
             {Id::sustain, .55f},
             {Id::releaseMs, 90},
             {Id::voiceMode, 1},
             {Id::reverbEnabled, 0},
             {Id::compressorEnabled, 1},
             {Id::compressorThresholdDb, -16},
             {Id::compressorRatio, 3}});
    revoice(p, "funk-stab", "Detuned unison saw stab with a fast filter snap.",
            {{Id::detuneCents, 12},
             {Id::oscillatorBlend, .5f},
             {Id::unisonVoices, 3},
             {Id::unisonDetuneCents, 16},
             {Id::cutoffHz, 700},
             {Id::resonance, .2f},
             {Id::filterEnvelopeOctaves, 3},
             {Id::modDecayMs, 260},
             {Id::modSustain, .05f},
             {Id::filterVelocityOctaves, 1},
             {Id::attackMs, 1},
             {Id::decayMs, 320},
             {Id::sustain, .1f},
             {Id::releaseMs, 200},
             {Id::reverbMix, .18f},
             {Id::masterDb, -8}});
    revoice(p, "funk-snap", "Band-passed noise snap with a sine body; pitched by key.",
            {{Id::oscillatorType, noise},
             {Id::oscillator2Type, sine},
             {Id::oscillator2Semitones, -12},
             {Id::oscillatorBlend, .25f},
             {Id::filterType, 2},
             {Id::filterModel, 0},
             {Id::cutoffHz, 1800},
             {Id::resonance, .3f},
             {Id::filterEnvelopeOctaves, 1.5f},
             {Id::modDecayMs, 60},
             {Id::attackMs, 1},
             {Id::decayMs, 120},
             {Id::sustain, 0},
             {Id::releaseMs, 90},
             {Id::velocityToAmp, .9f},
             {Id::analogDrift, 0},
             {Id::reverbEnabled, 0},
             {Id::polyphony, 16}});
    revoice(p, "funk-lead", "Driven pulse/saw lead with legato glide and filter bite.",
            {{Id::oscillatorType, square},
             {Id::pulseWidth, .35f},
             {Id::oscillatorBlend, .4f},
             {Id::detuneCents, 5},
             {Id::cutoffHz, 1600},
             {Id::resonance, .3f},
             {Id::filterEnvelopeOctaves, 1.8f},
             {Id::modDecayMs, 450},
             {Id::modSustain, .35f},
             {Id::filterDrive, .25f},
             {Id::voiceMode, 2},
             {Id::glideMs, 70},
             {Id::attackMs, 4},
             {Id::decayMs, 360},
             {Id::sustain, .75f},
             {Id::releaseMs, 180},
             {Id::delayEnabled, 1},
             {Id::delayTimeMs, 250},
             {Id::delayFeedback, .3f},
             {Id::delayMix, .15f}});
    revoice(p, "hiphop-low", "Long sine low end with saturation for small speakers and legato glide.",
            {{Id::oscillatorType, sine},
             {Id::oscillator2Type, triangle},
             {Id::oscillatorBlend, .15f},
             {Id::filterModel, 0},
             {Id::cutoffHz, 1200},
             {Id::filterDrive, .3f},
             {Id::attackMs, 2},
             {Id::decayMs, 1500},
             {Id::sustain, .35f},
             {Id::releaseMs, 260},
             {Id::voiceMode, 2},
             {Id::glideMs, 40},
             {Id::reverbEnabled, 0}});
    revoice(p, "hiphop-dust", "Dusty FM keys with tape-like drift, a little noise and rolled-off highs.",
            {{Id::oscillatorType, sine},
             {Id::oscillator2Type, sine},
             {Id::oscillatorBlend, .05f},
             {Id::fmAmount, .3f},
             {Id::modDecayMs, 900},
             {Id::modSustain, .1f},
             {Id::filterModel, 0},
             {Id::cutoffHz, 2800},
             {Id::noiseLevel, .03f},
             {Id::analogDrift, .35f},
             {Id::attackMs, 2},
             {Id::decayMs, 1600},
             {Id::sustain, .3f},
             {Id::releaseMs, 500},
             {Id::chorusEnabled, 1},
             {Id::chorusMix, .2f},
             {Id::eqHighDb, -3},
             {Id::reverbMix, .2f},
             {Id::polyphony, 32}});
    revoice(p, "hiphop-haze", "Dark five-voice unison saw texture with slow movement.",
            {{Id::detuneCents, 8},    {Id::oscillatorBlend, .5f},
             {Id::unisonVoices, 5},   {Id::unisonDetuneCents, 24},
             {Id::unisonSpread, .9f}, {Id::cutoffHz, 900},
             {Id::resonance, .1f},    {Id::filterEnvelopeOctaves, .8f},
             {Id::modAttackMs, 1200}, {Id::modDecayMs, 2500},
             {Id::modSustain, .6f},   {Id::attackMs, 500},
             {Id::decayMs, 1500},     {Id::sustain, .85f},
             {Id::releaseMs, 2400},   {Id::chorusEnabled, 1},
             {Id::chorusMix, .3f},    {Id::reverbMix, .3f},
             {Id::motion, .07f},      {Id::vibratoRateHz, .18f},
             {Id::eqHighDb, -4},      {Id::polyphony, 16},
             {Id::masterDb, -9}});
    revoice(p, "hiphop-click", "Short high-passed noise click with a low sine thump.",
            {{Id::oscillatorType, noise},
             {Id::oscillator2Type, sine},
             {Id::oscillator2Semitones, -12},
             {Id::oscillatorBlend, .3f},
             {Id::filterType, 1},
             {Id::filterModel, 0},
             {Id::cutoffHz, 2500},
             {Id::resonance, .1f},
             {Id::attackMs, 1},
             {Id::decayMs, 35},
             {Id::sustain, 0},
             {Id::releaseMs, 30},
             {Id::velocityToAmp, .9f},
             {Id::analogDrift, 0},
             {Id::reverbEnabled, 0},
             {Id::polyphony, 16}});
    revoice(p, "rnb-silk", "Soft unison triangle/saw chords with a long, warm release.",
            {{Id::oscillatorType, triangle},
             {Id::oscillatorBlend, .3f},
             {Id::detuneCents, 7},
             {Id::unisonVoices, 3},
             {Id::unisonDetuneCents, 10},
             {Id::unisonSpread, .6f},
             {Id::cutoffHz, 1500},
             {Id::resonance, .08f},
             {Id::filterEnvelopeOctaves, 1.4f},
             {Id::modAttackMs, 20},
             {Id::modDecayMs, 1200},
             {Id::modSustain, .4f},
             {Id::attackMs, 30},
             {Id::decayMs, 1500},
             {Id::sustain, .65f},
             {Id::releaseMs, 1300},
             {Id::chorusEnabled, 1},
             {Id::chorusMix, .25f},
             {Id::reverbMix, .25f}});
    revoice(p, "rnb-sine", "Pure sine lead with slow onset, glide and mod-wheel vibrato.",
            {{Id::oscillatorType, sine},
             {Id::oscillator2Type, triangle},
             {Id::oscillator2Semitones, 12},
             {Id::oscillatorBlend, .12f},
             {Id::filterModel, 0},
             {Id::cutoffHz, 6000},
             {Id::attackMs, 60},
             {Id::decayMs, 800},
             {Id::sustain, .85f},
             {Id::releaseMs, 900},
             {Id::voiceMode, 2},
             {Id::glideMs, 90},
             {Id::vibratoDepthCents, 25},
             {Id::delayEnabled, 1},
             {Id::delayTimeMs, 420},
             {Id::delayFeedback, .3f},
             {Id::delayMix, .16f},
             {Id::reverbMix, .22f}});
    revoice(p, "rnb-bass", "Steady sine/square bass with a gentle ladder envelope.",
            {{Id::oscillatorType, sine},
             {Id::oscillator2Type, square},
             {Id::oscillatorBlend, .12f},
             {Id::cutoffHz, 500},
             {Id::resonance, .1f},
             {Id::filterEnvelopeOctaves, 1},
             {Id::modDecayMs, 300},
             {Id::modSustain, .2f},
             {Id::filterDrive, .15f},
             {Id::attackMs, 4},
             {Id::decayMs, 700},
             {Id::sustain, .7f},
             {Id::releaseMs, 200},
             {Id::voiceMode, 1},
             {Id::reverbEnabled, 0}});
    revoice(p, "rnb-drift", "Slow saw/triangle unison ensemble with a long evolving tail.",
            {{Id::oscillator2Type, triangle},
             {Id::oscillatorBlend, .45f},
             {Id::unisonVoices, 5},
             {Id::unisonDetuneCents, 18},
             {Id::cutoffHz, 1300},
             {Id::resonance, .1f},
             {Id::filterEnvelopeOctaves, 1.3f},
             {Id::modAttackMs, 2000},
             {Id::modDecayMs, 4000},
             {Id::modSustain, .5f},
             {Id::attackMs, 1100},
             {Id::decayMs, 2100},
             {Id::sustain, .9f},
             {Id::releaseMs, 3500},
             {Id::chorusEnabled, 1},
             {Id::chorusMix, .35f},
             {Id::reverbMix, .4f},
             {Id::reverbDecaySeconds, 5.5f},
             {Id::motion, .1f},
             {Id::vibratoRateHz, .15f},
             {Id::polyphony, 16},
             {Id::masterDb, -9}});
    revoice(p, "electronic-prism", "Bright five-voice unison saw lead with an octave layer and glide.",
            {{Id::oscillator2Semitones, 12},
             {Id::oscillatorBlend, .35f},
             {Id::unisonVoices, 5},
             {Id::unisonDetuneCents, 22},
             {Id::unisonSpread, .8f},
             {Id::cutoffHz, 3200},
             {Id::resonance, .18f},
             {Id::filterEnvelopeOctaves, 1},
             {Id::modDecayMs, 500},
             {Id::modSustain, .5f},
             {Id::filterDrive, .15f},
             {Id::voiceMode, 2},
             {Id::glideMs, 50},
             {Id::attackMs, 2},
             {Id::decayMs, 500},
             {Id::sustain, .85f},
             {Id::releaseMs, 280},
             {Id::delayEnabled, 1},
             {Id::delayTimeMs, 375},
             {Id::delayFeedback, .32f},
             {Id::delayMix, .18f},
             {Id::reverbMix, .2f},
             {Id::masterDb, -8}});
    revoice(p, "electronic-arp", "Resonant pulse/saw sequence pluck with tempo-style echo.",
            {{Id::oscillatorType, square},
             {Id::pulseWidth, .3f},
             {Id::oscillatorBlend, .4f},
             {Id::cutoffHz, 500},
             {Id::resonance, .45f},
             {Id::filterEnvelopeOctaves, 3.2f},
             {Id::modDecayMs, 180},
             {Id::filterVelocityOctaves, 1},
             {Id::attackMs, 1},
             {Id::decayMs, 220},
             {Id::sustain, .05f},
             {Id::releaseMs, 140},
             {Id::delayEnabled, 1},
             {Id::delayTimeMs, 375},
             {Id::delayFeedback, .35f},
             {Id::delayMix, .22f}});
    revoice(p, "electronic-sub", "Clean sine/triangle foundation with mono legato glide.",
            {{Id::oscillatorType, sine},
             {Id::oscillator2Type, triangle},
             {Id::oscillatorBlend, .1f},
             {Id::filterModel, 0},
             {Id::cutoffHz, 400},
             {Id::attackMs, 2},
             {Id::decayMs, 500},
             {Id::sustain, .95f},
             {Id::releaseMs, 140},
             {Id::voiceMode, 2},
             {Id::glideMs, 30},
             {Id::reverbEnabled, 0},
             {Id::compressorEnabled, 1},
             {Id::compressorThresholdDb, -16},
             {Id::compressorRatio, 2.5f}});
    revoice(p, "electronic-snow", "Broadband noise hit with a short airy tail.",
            {{Id::oscillatorType, noise},
             {Id::oscillator2Type, noise},
             {Id::filterModel, 0},
             {Id::cutoffHz, 9000},
             {Id::resonance, .1f},
             {Id::attackMs, 1},
             {Id::decayMs, 220},
             {Id::sustain, .02f},
             {Id::releaseMs, 180},
             {Id::velocityToAmp, .9f},
             {Id::analogDrift, 0},
             {Id::reverbMix, .2f},
             {Id::polyphony, 16}});
    revoice(p, "rock-edge", "Driven saw/square lead with ladder bite and legato glide.",
            {{Id::oscillator2Type, square},
             {Id::oscillatorBlend, .4f},
             {Id::detuneCents, 8},
             {Id::unisonVoices, 3},
             {Id::unisonDetuneCents, 12},
             {Id::cutoffHz, 2400},
             {Id::resonance, .25f},
             {Id::filterDrive, .55f},
             {Id::filterEnvelopeOctaves, 1.2f},
             {Id::modDecayMs, 400},
             {Id::modSustain, .5f},
             {Id::voiceMode, 2},
             {Id::glideMs, 35},
             {Id::attackMs, 3},
             {Id::decayMs, 400},
             {Id::sustain, .85f},
             {Id::releaseMs, 300},
             {Id::delayEnabled, 1},
             {Id::delayTimeMs, 300},
             {Id::delayFeedback, .3f},
             {Id::delayMix, .14f},
             {Id::masterDb, -8}});
    revoice(p, "rock-pulse", "Drawbar-style sine organ: fundamental, octave and sub with rotary-like chorus.",
            {{Id::oscillatorType, sine},
             {Id::oscillator2Type, sine},
             {Id::oscillator2Semitones, 12},
             {Id::oscillatorBlend, .45f},
             {Id::subLevel, .55f},
             {Id::filterModel, 0},
             {Id::cutoffHz, 8000},
             {Id::filterKeyTrack, 0},
             {Id::filterDrive, .12f},
             {Id::attackMs, 2},
             {Id::decayMs, 30},
             {Id::sustain, 1},
             {Id::releaseMs, 50},
             {Id::velocityToAmp, 0},
             {Id::chorusEnabled, 1},
             {Id::chorusMix, .35f},
             {Id::eqLowDb, 2},
             {Id::reverbMix, .15f},
             {Id::analogDrift, .05f},
             {Id::polyphony, 32},
             {Id::masterDb, -9}});
    revoice(p, "rock-bass", "Driven saw/square bass with sub and a punchy ladder envelope.",
            {{Id::oscillator2Type, square},
             {Id::oscillatorBlend, .35f},
             {Id::subLevel, .3f},
             {Id::cutoffHz, 350},
             {Id::resonance, .25f},
             {Id::filterDrive, .4f},
             {Id::filterEnvelopeOctaves, 2.2f},
             {Id::modDecayMs, 300},
             {Id::modSustain, .2f},
             {Id::attackMs, 1},
             {Id::decayMs, 450},
             {Id::sustain, .6f},
             {Id::releaseMs, 120},
             {Id::voiceMode, 1},
             {Id::reverbEnabled, 0},
             {Id::compressorEnabled, 1},
             {Id::compressorThresholdDb, -18},
             {Id::compressorRatio, 3}});
    revoice(p, "rock-veil", "Soft five-voice saw ensemble for sustained support.",
            {{Id::detuneCents, 6},
             {Id::oscillatorBlend, .5f},
             {Id::unisonVoices, 5},
             {Id::unisonDetuneCents, 16},
             {Id::cutoffHz, 2000},
             {Id::resonance, .1f},
             {Id::filterEnvelopeOctaves, 1},
             {Id::modAttackMs, 800},
             {Id::modDecayMs, 2000},
             {Id::modSustain, .6f},
             {Id::attackMs, 500},
             {Id::decayMs, 1400},
             {Id::sustain, .85f},
             {Id::releaseMs, 2200},
             {Id::chorusEnabled, 1},
             {Id::chorusMix, .25f},
             {Id::reverbMix, .3f},
             {Id::polyphony, 16},
             {Id::masterDb, -9}});
    revoice(p, "cinematic-slow", "Seven-voice unison string bed with a very slow filter swell.",
            {{Id::detuneCents, 9},        {Id::oscillatorBlend, .5f},
             {Id::unisonVoices, 7},       {Id::unisonDetuneCents, 20},
             {Id::unisonSpread, 1},       {Id::cutoffHz, 700},
             {Id::resonance, .12f},       {Id::filterEnvelopeOctaves, 2},
             {Id::modAttackMs, 4000},     {Id::modDecayMs, 6000},
             {Id::modSustain, .7f},       {Id::attackMs, 2000},
             {Id::decayMs, 2600},         {Id::sustain, .9f},
             {Id::releaseMs, 5500},       {Id::chorusEnabled, 1},
             {Id::chorusMix, .3f},        {Id::reverbMix, .45f},
             {Id::reverbDecaySeconds, 7}, {Id::reverbSize, .9f},
             {Id::motion, .1f},           {Id::vibratoRateHz, .12f},
             {Id::polyphony, 12},         {Id::masterDb, -10}});
    revoice(p, "cinematic-low", "Long unison saw/sine low drone with a slow ladder bloom.",
            {{Id::oscillator2Type, sine},
             {Id::oscillator2Semitones, -12},
             {Id::oscillatorBlend, .4f},
             {Id::unisonVoices, 3},
             {Id::unisonDetuneCents, 10},
             {Id::unisonSpread, .4f},
             {Id::cutoffHz, 300},
             {Id::resonance, .2f},
             {Id::filterEnvelopeOctaves, 1.5f},
             {Id::modAttackMs, 2000},
             {Id::modDecayMs, 4000},
             {Id::modSustain, .5f},
             {Id::filterDrive, .2f},
             {Id::attackMs, 300},
             {Id::decayMs, 2200},
             {Id::sustain, .8f},
             {Id::releaseMs, 2500},
             {Id::reverbMix, .25f},
             {Id::polyphony, 8}});
    revoice(p, "cinematic-air", "Band-passed noise ambience with slow resonant movement.",
            {{Id::oscillatorType, noise},
             {Id::oscillator2Type, triangle},
             {Id::oscillatorBlend, .2f},
             {Id::filterType, 2},
             {Id::filterModel, 0},
             {Id::cutoffHz, 1200},
             {Id::resonance, .4f},
             {Id::motion, .25f},
             {Id::vibratoRateHz, .1f},
             {Id::vibratoDepthCents, 0},
             {Id::attackMs, 1600},
             {Id::decayMs, 2200},
             {Id::sustain, .7f},
             {Id::releaseMs, 5000},
             {Id::reverbMix, .5f},
             {Id::reverbDecaySeconds, 8},
             {Id::reverbSize, .9f},
             {Id::polyphony, 12}});
    revoice(p, "cinematic-spark", "FM bell strike (3:1 ratio) with a long shimmering tail.",
            {{Id::oscillatorType, sine},  {Id::oscillator2Type, sine}, {Id::oscillator2Semitones, 19},
             {Id::oscillatorBlend, .08f}, {Id::fmAmount, .45f},        {Id::modDecayMs, 1500},
             {Id::filterModel, 0},        {Id::cutoffHz, 12000},       {Id::filterKeyTrack, 0},
             {Id::attackMs, 1},           {Id::decayMs, 3000},         {Id::sustain, 0},
             {Id::releaseMs, 2500},       {Id::reverbMix, .35f},       {Id::reverbDecaySeconds, 4},
             {Id::delayEnabled, 1},       {Id::delayTimeMs, 450},      {Id::delayFeedback, .35f},
             {Id::delayMix, .18f},        {Id::polyphony, 32}});
    revoice(p, "arabic-pulse",
            "Pulse/saw lead with glide and expressive mod-wheel vibrato (equal temperament).",
            {{Id::oscillatorType, square},
             {Id::pulseWidth, .4f},
             {Id::oscillatorBlend, .3f},
             {Id::cutoffHz, 2000},
             {Id::resonance, .2f},
             {Id::filterEnvelopeOctaves, 1.2f},
             {Id::modDecayMs, 400},
             {Id::modSustain, .4f},
             {Id::voiceMode, 2},
             {Id::glideMs, 60},
             {Id::vibratoDepthCents, 30},
             {Id::attackMs, 5},
             {Id::decayMs, 400},
             {Id::sustain, .8f},
             {Id::releaseMs, 260},
             {Id::delayEnabled, 1},
             {Id::delayTimeMs, 300},
             {Id::delayFeedback, .25f},
             {Id::delayMix, .12f}});
    revoice(p, "arabic-pluck", "Original triangle/saw pluck with a light FM edge; equal-tempered tuning.",
            {{Id::oscillatorType, triangle},
             {Id::oscillator2Semitones, 12},
             {Id::oscillatorBlend, .25f},
             {Id::fmAmount, .2f},
             {Id::cutoffHz, 1200},
             {Id::resonance, .2f},
             {Id::filterEnvelopeOctaves, 2.5f},
             {Id::modDecayMs, 400},
             {Id::attackMs, 1},
             {Id::decayMs, 900},
             {Id::sustain, 0},
             {Id::releaseMs, 600},
             {Id::reverbMix, .25f},
             {Id::delayEnabled, 1},
             {Id::delayTimeMs, 320},
             {Id::delayFeedback, .25f},
             {Id::delayMix, .15f},
             {Id::polyphony, 32}});
    revoice(p, "arabic-low", "Sine/triangle bass with saturation and legato glide.",
            {{Id::oscillatorType, sine},
             {Id::oscillator2Type, triangle},
             {Id::oscillatorBlend, .25f},
             {Id::filterModel, 0},
             {Id::cutoffHz, 900},
             {Id::filterDrive, .2f},
             {Id::attackMs, 3},
             {Id::decayMs, 700},
             {Id::sustain, .6f},
             {Id::releaseMs, 220},
             {Id::voiceMode, 2},
             {Id::glideMs, 50},
             {Id::reverbEnabled, 0}});
    revoice(p, "arabic-night", "Atmospheric saw/triangle unison ensemble; not a bowed-string recording.",
            {{Id::oscillator2Type, triangle},
             {Id::oscillatorBlend, .4f},
             {Id::unisonVoices, 5},
             {Id::unisonDetuneCents, 16},
             {Id::cutoffHz, 1400},
             {Id::resonance, .1f},
             {Id::filterEnvelopeOctaves, 1},
             {Id::modAttackMs, 1500},
             {Id::modDecayMs, 3000},
             {Id::modSustain, .6f},
             {Id::attackMs, 700},
             {Id::decayMs, 1800},
             {Id::sustain, .85f},
             {Id::releaseMs, 3000},
             {Id::chorusEnabled, 1},
             {Id::chorusMix, .3f},
             {Id::reverbMix, .4f},
             {Id::motion, .08f},
             {Id::vibratoRateHz, .2f},
             {Id::polyphony, 16},
             {Id::masterDb, -9}});

    // ---- New production patches (original synthesis; no recordings) ----
    append(p, "electronic-supersaw", "Supersaw Anthem", "Electronic|Pop", "Synth Leads",
           "Seven-voice detuned saw stack with wide stereo spread for anthemic leads and chords.",
           {{Id::detuneCents, 9},
            {Id::oscillatorBlend, .45f},
            {Id::unisonVoices, 7},
            {Id::unisonDetuneCents, 38},
            {Id::unisonSpread, 1},
            {Id::filterModel, 0},
            {Id::cutoffHz, 7000},
            {Id::resonance, .05f},
            {Id::filterEnvelopeOctaves, .6f},
            {Id::modDecayMs, 800},
            {Id::modSustain, .6f},
            {Id::attackMs, 6},
            {Id::decayMs, 600},
            {Id::sustain, .9f},
            {Id::releaseMs, 450},
            {Id::chorusEnabled, 1},
            {Id::chorusMix, .2f},
            {Id::reverbMix, .25f},
            {Id::reverbDecaySeconds, 2.5f},
            {Id::delayEnabled, 1},
            {Id::delayTimeMs, 375},
            {Id::delayFeedback, .3f},
            {Id::delayMix, .15f},
            {Id::polyphony, 10},
            {Id::masterDb, -10}});
    append(p, "electronic-reese", "Reese Bass", "Electronic|Hip-Hop", "Bass",
           "Two detuned saws with unison beating, ladder filter and drive for dark moving bass.",
           {{Id::detuneCents, 14},
            {Id::oscillatorBlend, .5f},
            {Id::unisonVoices, 3},
            {Id::unisonDetuneCents, 18},
            {Id::unisonSpread, .25f},
            {Id::cutoffHz, 520},
            {Id::resonance, .15f},
            {Id::filterDrive, .45f},
            {Id::attackMs, 3},
            {Id::decayMs, 600},
            {Id::sustain, .9f},
            {Id::releaseMs, 160},
            {Id::voiceMode, 2},
            {Id::glideMs, 40},
            {Id::reverbEnabled, 0},
            {Id::compressorEnabled, 1},
            {Id::compressorThresholdDb, -16},
            {Id::compressorRatio, 3},
            {Id::masterDb, -7}});
    append(p, "electronic-acid", "Acid Line", "Electronic|Funk", "Bass",
           "Single saw through a highly resonant driven ladder with snappy envelope and slides.",
           {{Id::cutoffHz, 260},
            {Id::resonance, .82f},
            {Id::filterEnvelopeOctaves, 3.6f},
            {Id::modDecayMs, 220},
            {Id::filterDrive, .5f},
            {Id::filterVelocityOctaves, 1.5f},
            {Id::filterKeyTrack, .3f},
            {Id::voiceMode, 2},
            {Id::glideMs, 70},
            {Id::attackMs, 1},
            {Id::decayMs, 300},
            {Id::sustain, .7f},
            {Id::releaseMs, 90},
            {Id::reverbEnabled, 0},
            {Id::delayEnabled, 1},
            {Id::delayTimeMs, 375},
            {Id::delayFeedback, .3f},
            {Id::delayMix, .12f},
            {Id::masterDb, -8}});
    append(p, "hiphop-808", "Sine 808 Glide", "Hip-Hop|R&B", "Bass",
           "808-style synthesized sine bass with long decay, saturation and legato glide; not a drum-machine "
           "recording.",
           {{Id::oscillatorType, sine},
            {Id::oscillator2Type, triangle},
            {Id::oscillatorBlend, .08f},
            {Id::filterModel, 0},
            {Id::cutoffHz, 1500},
            {Id::filterDrive, .35f},
            {Id::attackMs, 1},
            {Id::decayMs, 2400},
            {Id::sustain, .15f},
            {Id::releaseMs, 350},
            {Id::voiceMode, 2},
            {Id::glideMs, 110},
            {Id::velocityToAmp, .5f},
            {Id::analogDrift, 0},
            {Id::reverbEnabled, 0},
            {Id::compressorEnabled, 1},
            {Id::compressorThresholdDb, -14},
            {Id::compressorRatio, 2.5f}});
    append(p, "pop-sub", "Deep Sub", "Pop|Electronic", "Bass", "Clean sustained sine sub bass.",
           {{Id::oscillatorType, sine},
            {Id::oscillator2Type, sine},
            {Id::filterModel, 0},
            {Id::cutoffHz, 250},
            {Id::filterKeyTrack, .3f},
            {Id::filterDrive, .05f},
            {Id::attackMs, 3},
            {Id::decayMs, 300},
            {Id::sustain, 1},
            {Id::releaseMs, 120},
            {Id::voiceMode, 1},
            {Id::analogDrift, 0},
            {Id::reverbEnabled, 0}});
    append(p, "funk-ladder-bass", "Fat Ladder Bass", "Funk|Rock", "Bass",
           "Saw with a square one octave down through a punchy 24 dB ladder; velocity opens the filter.",
           {{Id::oscillator2Type, square},
            {Id::oscillator2Semitones, -12},
            {Id::oscillatorBlend, .35f},
            {Id::cutoffHz, 380},
            {Id::resonance, .28f},
            {Id::filterEnvelopeOctaves, 2.4f},
            {Id::modDecayMs, 260},
            {Id::modSustain, .15f},
            {Id::filterDrive, .3f},
            {Id::filterVelocityOctaves, 1.3f},
            {Id::attackMs, 1},
            {Id::decayMs, 500},
            {Id::sustain, .5f},
            {Id::releaseMs, 110},
            {Id::voiceMode, 1},
            {Id::reverbEnabled, 0},
            {Id::compressorEnabled, 1},
            {Id::compressorThresholdDb, -18},
            {Id::compressorRatio, 3}});
    append(p, "rnb-tine-keys", "Tine FM Keys", "R&B|Jazz|Pop", "Electric Piano",
           "Original two-operator FM electric keys: a velocity-bright bark that settles into a mellow tone. "
           "Not a "
           "sampled Rhodes.",
           {{Id::oscillatorType, sine},  {Id::oscillator2Type, sine},
            {Id::oscillatorBlend, .08f}, {Id::detuneCents, 3},
            {Id::fmAmount, .5f},         {Id::modDecayMs, 1400},
            {Id::modSustain, .12f},      {Id::filterModel, 0},
            {Id::cutoffHz, 6000},        {Id::filterVelocityOctaves, 2},
            {Id::filterKeyTrack, .3f},   {Id::attackMs, 1},
            {Id::decayMs, 3200},         {Id::sustain, .25f},
            {Id::releaseMs, 700},        {Id::velocityToAmp, .85f},
            {Id::chorusEnabled, 1},      {Id::chorusMix, .22f},
            {Id::eqLowDb, 1.5f},         {Id::eqHighDb, -2},
            {Id::reverbMix, .2f},        {Id::polyphony, 32}});
    append(p, "pop-fm-bell", "Crystal FM Bell", "Pop|Cinematic", "Plucks",
           "Bright 4:1 FM bell with a slightly detuned modulator for shimmer and a long decay.",
           {{Id::oscillatorType, sine},
            {Id::oscillator2Type, sine},
            {Id::oscillator2Semitones, 24},
            {Id::detuneCents, 4},
            {Id::oscillatorBlend, .05f},
            {Id::fmAmount, .4f},
            {Id::modDecayMs, 2200},
            {Id::filterModel, 0},
            {Id::cutoffHz, 14000},
            {Id::filterKeyTrack, 0},
            {Id::attackMs, 1},
            {Id::decayMs, 4500},
            {Id::sustain, 0},
            {Id::releaseMs, 3000},
            {Id::velocityToAmp, .75f},
            {Id::reverbMix, .3f},
            {Id::reverbDecaySeconds, 3.5f},
            {Id::polyphony, 32}});
    append(
        p, "cinematic-brass", "Analog Brass Section", "Cinematic|Pop", "Brass",
        "Unison saw brass with a swelling filter attack; play harder for brighter blasts. Synthesized, not a "
        "brass recording.",
        {{Id::detuneCents, 8},    {Id::oscillatorBlend, .5f},
         {Id::unisonVoices, 3},   {Id::unisonDetuneCents, 12},
         {Id::unisonSpread, .6f}, {Id::cutoffHz, 650},
         {Id::resonance, .06f},   {Id::filterEnvelopeOctaves, 2.3f},
         {Id::modAttackMs, 80},   {Id::modDecayMs, 600},
         {Id::modSustain, .55f},  {Id::filterVelocityOctaves, 1.5f},
         {Id::filterDrive, .15f}, {Id::attackMs, 45},
         {Id::decayMs, 400},      {Id::sustain, .85f},
         {Id::releaseMs, 260},    {Id::vibratoDepthCents, 15},
         {Id::reverbMix, .28f},   {Id::reverbDecaySeconds, 2.8f},
         {Id::polyphony, 16},     {Id::masterDb, -8}});
    append(p, "funk-brass-stab", "Brass Stab", "Funk|Pop", "Brass",
           "Short punchy synth-brass stab with a fast filter blip.",
           {{Id::detuneCents, 10},
            {Id::oscillatorBlend, .5f},
            {Id::unisonVoices, 3},
            {Id::unisonDetuneCents, 10},
            {Id::cutoffHz, 900},
            {Id::resonance, .08f},
            {Id::filterEnvelopeOctaves, 2.5f},
            {Id::modAttackMs, 25},
            {Id::modDecayMs, 250},
            {Id::modSustain, .2f},
            {Id::filterVelocityOctaves, 1},
            {Id::attackMs, 12},
            {Id::decayMs, 300},
            {Id::sustain, .35f},
            {Id::releaseMs, 140},
            {Id::reverbMix, .2f},
            {Id::compressorEnabled, 1},
            {Id::compressorThresholdDb, -16},
            {Id::compressorRatio, 3},
            {Id::masterDb, -8}});
    append(p, "cinematic-strings", "Synth String Ensemble", "Cinematic|Pop", "Strings",
           "Five-voice unison saw ensemble with ensemble chorus and a bowed-style swell. Synthesized, not a "
           "string "
           "recording.",
           {{Id::detuneCents, 6},
            {Id::oscillatorBlend, .5f},
            {Id::unisonVoices, 5},
            {Id::unisonDetuneCents, 16},
            {Id::unisonSpread, .9f},
            {Id::filterModel, 0},
            {Id::cutoffHz, 4200},
            {Id::resonance, .02f},
            {Id::attackMs, 380},
            {Id::decayMs, 800},
            {Id::sustain, .9f},
            {Id::releaseMs, 900},
            {Id::velocityToAmp, .4f},
            {Id::chorusEnabled, 1},
            {Id::chorusMix, .32f},
            {Id::reverbMix, .32f},
            {Id::reverbDecaySeconds, 3.2f},
            {Id::motion, .03f},
            {Id::vibratoRateHz, 5.5f},
            {Id::eqLowDb, -2},
            {Id::eqHighDb, -1},
            {Id::polyphony, 16},
            {Id::masterDb, -9}});
    append(p, "pop-warm-pad", "Warm Poly Pad", "Pop|R&B", "Pads",
           "Classic warm poly pad: saw and pulse through a soft ladder with slow attack and chorus.",
           {{Id::oscillator2Type, square}, {Id::pulseWidth, .45f}, {Id::oscillatorBlend, .35f},
            {Id::detuneCents, 7},          {Id::unisonVoices, 3},  {Id::unisonDetuneCents, 14},
            {Id::cutoffHz, 1200},          {Id::resonance, .1f},   {Id::filterEnvelopeOctaves, 1.2f},
            {Id::modAttackMs, 600},        {Id::modDecayMs, 2000}, {Id::modSustain, .55f},
            {Id::attackMs, 400},           {Id::decayMs, 1500},    {Id::sustain, .85f},
            {Id::releaseMs, 1800},         {Id::chorusEnabled, 1}, {Id::chorusMix, .3f},
            {Id::reverbMix, .3f},          {Id::polyphony, 16},    {Id::masterDb, -8}});
    append(p, "electronic-dark-pad", "Dark Motion Pad", "Electronic|Cinematic", "Pads",
           "Low unison saws through a resonant driven ladder with slow filter motion and echo.",
           {{Id::oscillator2Semitones, -12},
            {Id::oscillatorBlend, .4f},
            {Id::detuneCents, 10},
            {Id::unisonVoices, 5},
            {Id::unisonDetuneCents, 22},
            {Id::cutoffHz, 450},
            {Id::resonance, .35f},
            {Id::filterDrive, .2f},
            {Id::motion, .2f},
            {Id::vibratoRateHz, .12f},
            {Id::vibratoDepthCents, 0},
            {Id::attackMs, 1200},
            {Id::decayMs, 2500},
            {Id::sustain, .9f},
            {Id::releaseMs, 3500},
            {Id::reverbMix, .4f},
            {Id::reverbDecaySeconds, 6},
            {Id::delayEnabled, 1},
            {Id::delayTimeMs, 500},
            {Id::delayFeedback, .4f},
            {Id::delayMix, .15f},
            {Id::polyphony, 12},
            {Id::masterDb, -9}});
    append(p, "cinematic-glass-pad", "Glass FM Pad", "Cinematic|Electronic", "Pads",
           "Octave FM pad whose brightness slowly blooms and settles, with wide unison and chorus.",
           {{Id::oscillatorType, sine},  {Id::oscillator2Type, sine}, {Id::oscillator2Semitones, 12},
            {Id::oscillatorBlend, .15f}, {Id::fmAmount, .35f},        {Id::modAttackMs, 1500},
            {Id::modDecayMs, 4000},      {Id::modSustain, .4f},       {Id::unisonVoices, 3},
            {Id::unisonDetuneCents, 8},  {Id::unisonSpread, .8f},     {Id::filterModel, 0},
            {Id::cutoffHz, 9000},        {Id::attackMs, 900},         {Id::decayMs, 2000},
            {Id::sustain, .85f},         {Id::releaseMs, 3000},       {Id::chorusEnabled, 1},
            {Id::chorusMix, .3f},        {Id::reverbMix, .45f},       {Id::reverbDecaySeconds, 5},
            {Id::polyphony, 16},         {Id::masterDb, -8}});
    append(p, "electronic-house-pluck", "House Chord Pluck", "Electronic|Pop", "Plucks",
           "Detuned unison saw chord pluck with a fast ladder snap and echo.",
           {{Id::detuneCents, 10},
            {Id::oscillatorBlend, .5f},
            {Id::unisonVoices, 3},
            {Id::unisonDetuneCents, 14},
            {Id::unisonSpread, .7f},
            {Id::cutoffHz, 500},
            {Id::resonance, .2f},
            {Id::filterEnvelopeOctaves, 3.4f},
            {Id::modDecayMs, 230},
            {Id::attackMs, 1},
            {Id::decayMs, 400},
            {Id::sustain, 0},
            {Id::releaseMs, 260},
            {Id::delayEnabled, 1},
            {Id::delayTimeMs, 375},
            {Id::delayFeedback, .3f},
            {Id::delayMix, .18f},
            {Id::reverbMix, .2f},
            {Id::masterDb, -8}});
    append(p, "pop-soft-pluck", "Soft Mallet Pluck", "Pop|R&B", "Plucks",
           "Gentle triangle/sine mallet pluck with a light FM attack.",
           {{Id::oscillatorType, triangle},
            {Id::oscillator2Type, sine},
            {Id::oscillator2Semitones, 12},
            {Id::oscillatorBlend, .3f},
            {Id::fmAmount, .15f},
            {Id::modDecayMs, 200},
            {Id::filterModel, 0},
            {Id::cutoffHz, 1800},
            {Id::resonance, .05f},
            {Id::filterEnvelopeOctaves, 1.5f},
            {Id::attackMs, 1},
            {Id::decayMs, 1200},
            {Id::sustain, 0},
            {Id::releaseMs, 700},
            {Id::reverbMix, .25f},
            {Id::polyphony, 32}});
    append(p, "electronic-lead-saw", "Classic Saw Lead", "Electronic|Rock", "Synth Leads",
           "Two detuned saws through a driven resonant ladder with legato glide.",
           {{Id::detuneCents, 7},
            {Id::oscillatorBlend, .45f},
            {Id::cutoffHz, 2000},
            {Id::resonance, .3f},
            {Id::filterDrive, .35f},
            {Id::filterEnvelopeOctaves, 1.5f},
            {Id::modDecayMs, 350},
            {Id::modSustain, .45f},
            {Id::voiceMode, 2},
            {Id::glideMs, 50},
            {Id::attackMs, 2},
            {Id::decayMs, 300},
            {Id::sustain, .85f},
            {Id::releaseMs, 220},
            {Id::vibratoDepthCents, 25},
            {Id::delayEnabled, 1},
            {Id::delayTimeMs, 330},
            {Id::delayFeedback, .28f},
            {Id::delayMix, .14f}});
    append(p, "funk-whistle-lead", "Sine Whistle Lead", "Funk|R&B", "Synth Leads",
           "Portamento sine lead in the classic funk style, with mod-wheel vibrato.",
           {{Id::oscillatorType, sine},
            {Id::oscillator2Type, triangle},
            {Id::oscillator2Semitones, 12},
            {Id::oscillatorBlend, .1f},
            {Id::filterModel, 0},
            {Id::cutoffHz, 8000},
            {Id::voiceMode, 2},
            {Id::glideMs, 120},
            {Id::attackMs, 20},
            {Id::decayMs, 300},
            {Id::sustain, .9f},
            {Id::releaseMs, 250},
            {Id::vibratoDepthCents, 30},
            {Id::delayEnabled, 1},
            {Id::delayTimeMs, 280},
            {Id::delayFeedback, .3f},
            {Id::delayMix, .15f}});
    append(p, "jazz-breath-lead", "Breath Lead", "Jazz|Cinematic", "Woodwinds",
           "Synthesized wind-like lead: triangle tone with breath noise and a soft onset. Not a flute "
           "recording.",
           {{Id::oscillatorType, triangle},
            {Id::oscillator2Type, sine},
            {Id::oscillator2Semitones, 12},
            {Id::oscillatorBlend, .15f},
            {Id::noiseLevel, .12f},
            {Id::filterModel, 0},
            {Id::cutoffHz, 2800},
            {Id::resonance, .1f},
            {Id::filterVelocityOctaves, 1},
            {Id::filterEnvelopeOctaves, .7f},
            {Id::modAttackMs, 60},
            {Id::modDecayMs, 400},
            {Id::modSustain, .7f},
            {Id::attackMs, 50},
            {Id::decayMs, 300},
            {Id::sustain, .85f},
            {Id::releaseMs, 220},
            {Id::voiceMode, 2},
            {Id::glideMs, 30},
            {Id::vibratoDepthCents, 20},
            {Id::reverbMix, .25f}});
    append(p, "hiphop-lofi-keys", "Lo-Fi Poly Keys", "Hip-Hop|R&B", "Electric Piano",
           "Wobbly triangle/pulse keys with strong drift, faint noise and dark highs. Synthesized keys.",
           {{Id::oscillatorType, triangle},
            {Id::oscillator2Type, square},
            {Id::pulseWidth, .5f},
            {Id::oscillatorBlend, .2f},
            {Id::detuneCents, 5},
            {Id::cutoffHz, 1400},
            {Id::resonance, .08f},
            {Id::filterEnvelopeOctaves, 1.4f},
            {Id::modDecayMs, 600},
            {Id::modSustain, .2f},
            {Id::analogDrift, .45f},
            {Id::noiseLevel, .02f},
            {Id::attackMs, 2},
            {Id::decayMs, 1800},
            {Id::sustain, .35f},
            {Id::releaseMs, 600},
            {Id::chorusEnabled, 1},
            {Id::chorusMix, .25f},
            {Id::eqHighDb, -5},
            {Id::reverbMix, .2f},
            {Id::polyphony, 32}});
    append(p, "electronic-ladder-arp", "Ladder Arp", "Electronic", "Plucks",
           "Resonant square/saw arpeggio pluck with velocity-sensitive filter and echo.",
           {{Id::oscillatorType, square},
            {Id::oscillator2Semitones, 12},
            {Id::oscillatorBlend, .3f},
            {Id::cutoffHz, 350},
            {Id::resonance, .6f},
            {Id::filterEnvelopeOctaves, 3.2f},
            {Id::modDecayMs, 160},
            {Id::filterVelocityOctaves, 1.2f},
            {Id::attackMs, 1},
            {Id::decayMs, 200},
            {Id::sustain, 0},
            {Id::releaseMs, 150},
            {Id::delayEnabled, 1},
            {Id::delayTimeMs, 250},
            {Id::delayFeedback, .4f},
            {Id::delayMix, .22f}});
    // Output trims measured from real project exports (chord + line phrase) so
    // every synthesis patch peaks near -9 dBFS; mono leads are capped at +6 dB.
    const std::array<std::pair<std::string_view, float>, 56> levels{{
        {"pop-crystal", 1.0f},
        {"pop-air", 0.5f},
        {"pop-round", 1.0f},
        {"pop-neon", 1.0f},
        {"jazz-mellow", 4.5f},
        {"jazz-sub", 2.0f},
        {"jazz-hush", 1.0f},
        {"funk-square", 5.5f},
        {"funk-stab", 1.0f},
        {"funk-snap", 6.0f},
        {"funk-lead", 6.0f},
        {"hiphop-low", -1.0f},
        {"hiphop-dust", -4.0f},
        {"hiphop-haze", 2.0f},
        {"hiphop-click", -1.5f},
        {"rnb-silk", 0.5f},
        {"rnb-sine", 5.0f},
        {"rnb-bass", 5.5f},
        {"rnb-drift", 0.5f},
        {"electronic-prism", 6.0f},
        {"electronic-arp", 1.0f},
        {"electronic-sub", -1.0f},
        {"electronic-snow", -3.0f},
        {"rock-edge", 6.0f},
        {"rock-pulse", -8.5f},
        {"rock-bass", 5.5f},
        {"rock-veil", 1.0f},
        {"cinematic-slow", 5.0f},
        {"cinematic-low", -1.5f},
        {"cinematic-air", 6.0f},
        {"cinematic-spark", -2.5f},
        {"arabic-pulse", 6.0f},
        {"arabic-pluck", 1.5f},
        {"arabic-low", -1.5f},
        {"arabic-night", 2.0f},
        {"electronic-supersaw", -1.5f},
        {"electronic-reese", 6.0f},
        {"electronic-acid", 6.0f},
        {"hiphop-808", -0.5f},
        {"pop-sub", -1.0f},
        {"funk-ladder-bass", 5.5f},
        {"rnb-tine-keys", -3.5f},
        {"pop-fm-bell", -4.5f},
        {"funk-brass-stab", 0.0f},
        {"cinematic-strings", -3.5f},
        {"pop-warm-pad", -0.5f},
        {"electronic-dark-pad", 2.0f},
        {"cinematic-glass-pad", -1.5f},
        {"electronic-house-pluck", 3.0f},
        {"pop-soft-pluck", -2.5f},
        {"electronic-lead-saw", 6.0f},
        {"funk-whistle-lead", 4.0f},
        {"jazz-breath-lead", 5.5f},
        {"hiphop-lofi-keys", -1.5f},
        {"electronic-ladder-arp", 2.0f},
        {"cinematic-brass", -1.0f},
    }};
    for (const auto &[id, decibels] : levels)
        for (auto &preset : p)
            if (id == preset.id)
                preset.values[parameterIndex(Id::masterDb)] = decibels;
}

auto makeFactoryPresets()
{
    // Every source claim is explicit. These oscillator patches are original
    // designs, not recordings or emulations of named commercial instruments.
    std::array<FactoryPreset, 56> result{{
        sampled("pop-velvet-upright", "Velvet Upright", "Pop", "Piano", "piano-upright", 4, 1200,
                "Recorded upright piano from the CC0 VSCO2CE library. Not a sampled grand piano."),
        sampled("cinematic-solo-violin", "Solo Violin", "Cinematic", "Strings", "violin-solo", 70, 700,
                "Real recorded solo violin from the CC0 VSCO 2 Community Edition."),
        sampled("jazz-pizz-bass", "Pizzicato Contrabass", "Jazz", "Bass", "contrabass-pizz", 3, 280,
                "Real recorded pizzicato contrabass from the CC0 orchestral library."),
        sampled("cinematic-cellos", "Cello Ensemble", "Cinematic", "Strings", "cello-ensemble", 120, 1200,
                "Real recorded cello ensemble. Macro processing is original VibeRa DSP."),
        sampled("pop-harp", "Clear Harp", "Pop", "Strings", "harp", 2, 1500,
                "Real recorded harp with mapped sample regions."),
        sampled("arabic-flute", "Air Flute", "Arabic", "Winds", "flute", 45, 450,
                "Recorded orchestral flute, not a ney; genre is a browsing suggestion. Equal temperament."),
        sampled("jazz-clarinet", "Studio Clarinet", "Jazz", "Winds", "clarinet", 30, 350,
                "Real recorded clarinet with sample-region pitch mapping."),
        sampled("funk-trumpet", "Trumpet Staccato", "Funk", "Winds", "trumpet-staccato", 2, 120,
                "Real recorded staccato trumpet, not a continuous legato articulation."),
        sampled("cinematic-horn", "Horizon Horn", "Cinematic", "Winds", "horn", 80, 900,
                "Real recorded orchestral horn from a CC0 source library."),
        sampled("jazz-trombone", "Warm Trombone", "Jazz", "Winds", "trombone", 35, 400,
                "Real recorded trombone with original VibeRa envelope and effects."),
        sampled("cinematic-tuba", "Low Brass", "Cinematic", "Winds", "tuba", 60, 600,
                "Real recorded tuba; this preset does not layer an entire brass section."),
        sampled("pop-marimba", "Wood Marimba", "Pop", "Synth", "marimba", 1, 900,
                "Real recorded marimba; filed with melodic keys for browsing."),
        sampled("rock-strumstick", "Acoustic Strumstick", "Rock", "Guitar", "strumstick", 2, 900,
                "Real recorded three-string fretted strumstick; not a six-string guitar recording."),
        sampled("rnb-kalimba", "Soft Kalimba", "R&B", "Synth", "kalimba", 1, 1000,
                "Real recorded kalimba with original ambience processing."),
        sampled("arabic-balafon", "Wood Balafon", "Arabic", "Synth", "balafon", 1, 800,
                "Recorded balafon; genre tag does not claim Arabic instrument origin or tuning."),
        sampled("funk-conga", "Studio Conga", "Funk", "Drums", "conga", 1, 180,
                "Real recorded conga mapped as an individual percussion instrument, not a full kit."),
        sampled("hiphop-bass-drum", "Round Bass Drum", "Hip-Hop", "Drums", "bass-drum", 1, 200,
                "Real recorded acoustic bass drum. Not an electronic 808 emulation."),
        sampled("rock-snare", "Dry Snare", "Rock", "Drums", "snare-drum", 1, 160,
                "Real recorded snare drum with an original amplitude envelope."),
        oscillator("pop-crystal", "Crystal Pluck", "Pop", "Synth", 3, 3, 420, .2f, 600, 7000, .1f,
                   "Bright triangle-oscillator pluck with a short sustain."),
        oscillator("pop-air", "Air Ensemble", "Pop", "Strings", 1, 380, 1400, .85f, 2200, 4200, .1f,
                   "A sustained synthetic string pad; no string recording is used."),
        oscillator("pop-round", "Round Bass", "Pop", "Bass", 0, 4, 400, .65f, 180, 1400, .15f,
                   "A rounded sine bass designed for melodic low parts."),
        oscillator("pop-neon", "Neon Chords", "Pop", "Synth", 1, 24, 800, .6f, 900, 6000, .2f,
                   "Detuned saw chords with a soft envelope."),
        oscillator("jazz-glass", "Glass Keys", "Jazz", "Synth", 3, 5, 1700, .2f, 1000, 10000, .05f,
                   "Triangle-based keys; an intentionally synthetic keyboard tone."),
        oscillator("jazz-mellow", "Mellow Lead", "Jazz", "Synth", 3, 35, 600, .7f, 250, 2200, .12f,
                   "A restrained triangle lead for smooth phrasing."),
        oscillator("jazz-sub", "Velour Bass", "Jazz", "Bass", 0, 12, 800, .5f, 240, 900, .08f,
                   "Soft sine bass, not an acoustic double-bass sample."),
        oscillator("jazz-hush", "Hush Pad", "Jazz", "Synth", 3, 800, 1900, .8f, 2800, 3400, .05f,
                   "A slow synthetic bed with gentle upper harmonics."),
        oscillator("funk-square", "Pocket Square", "Funk", "Bass", 2, 3, 220, .3f, 120, 1800, .4f,
                   "Short square-wave bass with a resonant low-pass response."),
        oscillator("funk-stab", "Violet Stab", "Funk", "Synth", 1, 2, 180, .1f, 140, 4500, .3f,
                   "A sharp saw-oscillator chord stab."),
        oscillator("funk-snap", "Snap Circuit", "Funk", "Drums", 4, 1, 85, 0, 80, 9000, .15f,
                   "A pitched-key noise percussion patch, not a recorded drum kit."),
        oscillator("funk-lead", "Elastic Lead", "Funk", "Synth", 2, 10, 360, .7f, 200, 5000, .35f,
                   "A sustained pulse lead with a flexible envelope."),
        oscillator("hiphop-low", "Low Current", "Hip-Hop", "Bass", 0, 2, 900, .55f, 260, 1000, .05f,
                   "Clean synthetic low-end with a long decay."),
        oscillator("hiphop-dust", "Dust Keys", "Hip-Hop", "Synth", 3, 9, 1300, .3f, 800, 2300, .12f,
                   "Filtered triangle keys; no copyrighted keyboard recording."),
        oscillator("hiphop-haze", "Haze Ensemble", "Hip-Hop", "Strings", 1, 500, 1500, .8f, 2400, 2200, .08f,
                   "A dark, sustained synthetic string texture."),
        oscillator("hiphop-click", "Circuit Click", "Hip-Hop", "Drums", 4, 1, 40, 0, 35, 5500, .2f,
                   "Original noise percussion with a fast amplitude envelope."),
        oscillator("rnb-silk", "Silk Chords", "R&B", "Synth", 3, 60, 1600, .65f, 1600, 4800, .07f,
                   "Soft layered triangle chords for spacious voicings."),
        oscillator("rnb-sine", "Sine Ribbon", "R&B", "Synth", 0, 80, 1000, .8f, 1300, 12000, .05f,
                   "A pure sine lead with a slow onset."),
        oscillator("rnb-bass", "Warm Anchor", "R&B", "Bass", 0, 7, 650, .7f, 250, 1200, .12f,
                   "Steady sine bass designed to leave room for chords."),
        oscillator("rnb-drift", "Evening Drift", "R&B", "Strings", 1, 1100, 2100, .9f, 3500, 3800, .05f,
                   "A slow synthetic ensemble texture."),
        oscillator("electronic-prism", "Prism Lead", "Electronic", "Synth", 1, 4, 500, .8f, 300, 9000, .3f,
                   "An open, detuned saw lead."),
        oscillator("electronic-arp", "Glass Sequence", "Electronic", "Synth", 2, 1, 160, .08f, 120, 6200, .4f,
                   "A short pulse pluck; sequencing is supplied by your notes."),
        oscillator("electronic-sub", "Foundation", "Electronic", "Bass", 0, 2, 500, .9f, 150, 1500, .1f,
                   "A sustained sine foundation for electronic bass lines."),
        oscillator("electronic-snow", "Digital Snow", "Electronic", "Drums", 4, 1, 200, .05f, 160, 12000, .2f,
                   "A broadband noise hit with a controllable filter."),
        oscillator("rock-edge", "Edge Lead", "Rock", "Synth", 1, 8, 420, .8f, 320, 6500, .28f,
                   "A forward saw lead, not a guitar recording."),
        oscillator("rock-pulse", "Pulse Organ", "Rock", "Synth", 2, 3, 400, .95f, 90, 5500, .1f,
                   "A sustained square-wave keyboard tone."),
        oscillator("rock-bass", "Copper Bass", "Rock", "Bass", 1, 4, 300, .65f, 160, 1500, .25f,
                   "A filtered saw bass with original synthetic timbre."),
        oscillator("rock-veil", "Backlight", "Rock", "Strings", 1, 450, 1400, .8f, 2400, 4000, .1f,
                   "A soft synthetic ensemble for sustained support."),
        oscillator("cinematic-slow", "Slow Horizon", "Cinematic", "Strings", 1, 1900, 2600, .85f, 5500, 3000,
                   .08f, "A slowly evolving oscillator string bed."),
        oscillator("cinematic-low", "Deep Horizon", "Cinematic", "Bass", 0, 280, 2200, .7f, 2500, 800, .05f,
                   "A long synthetic low drone."),
        oscillator("cinematic-air", "Airfield", "Cinematic", "Synth", 4, 1600, 2200, .6f, 5000, 1800, .15f,
                   "Filtered noise ambience, generated by the synthesizer."),
        oscillator("cinematic-spark", "Signal Bloom", "Cinematic", "Synth", 3, 4, 2200, .1f, 3200, 8500, .12f,
                   "A bright triangle strike with a long release."),
        oscillator("arabic-pulse", "Amber Pulse", "Arabic", "Synth", 2, 6, 500, .6f, 300, 4300, .3f,
                   "A pulse lead; genre tag is a browsing suggestion, not acoustic authenticity."),
        oscillator("arabic-pluck", "Desert Glass", "Arabic", "Synth", 3, 2, 650, .1f, 600, 7500, .15f,
                   "Original synthetic pluck with standard equal-tempered tuning."),
        oscillator("arabic-low", "Dune Bass", "Arabic", "Bass", 0, 5, 600, .6f, 220, 1200, .15f,
                   "A sine bass for melodic arrangements."),
        oscillator("arabic-night", "Night Veil", "Arabic", "Strings", 1, 700, 1800, .8f, 3000, 3200, .08f,
                   "An atmospheric oscillator ensemble; not a bowed-string recording."),
        sampled("pop-clean-electric", "Clean Electric Guitar", "Pop|Rock|Funk", "Guitar", "guitar-clean", 2,
                900, "Real recorded six-string clean electric guitar from the CC0 FreePats library."),
        sampled("jazz-fm-electric", "FM Electric Keys", "Jazz|Pop|R&B", "Electric Piano", "electric-piano-fm",
                2, 1100,
                "Real recorded Yamaha TX81Z hardware FM keyboard from CC0 VCSL. Not a Rhodes or acoustic "
                "piano recording."),
    }};
    for (std::size_t index = 0; index < result.size(); ++index)
    {
        auto &preset = result[index];
        preset.values[parameterIndex(ParameterId::factoryPreset)] = float(index);
        const bool bass = std::string_view(preset.family) == "Bass";
        const bool pad = std::string_view(preset.family) == "Strings";
        const bool percussion = std::string_view(preset.family) == "Drums";
        preset.values[parameterIndex(ParameterId::reverbEnabled)] = bass || percussion ? 0.0f : 1.0f;
        preset.values[parameterIndex(ParameterId::space)] = bass ? 0.0f : pad ? .45f : .2f;
        preset.values[parameterIndex(ParameterId::warmth)] = bass ? .2f : .1f;
        preset.values[parameterIndex(ParameterId::motion)] = pad ? .18f : .03f;
        preset.values[parameterIndex(ParameterId::chorusEnabled)] = pad ? 1.0f : 0.0f;
        preset.values[parameterIndex(ParameterId::detuneCents)] = bass ? 0.0f : pad ? 11.0f : 4.0f;
        preset.values[parameterIndex(ParameterId::oscillatorBlend)] = preset.source == SourceKind::sample
                                                                          ? 0.0f
                                                                      : bass ? 0.0f
                                                                      : pad  ? .5f
                                                                             : .25f;
        preset.macros = {bass ? 80.0f : 400.0f, bass ? 4200.0f : 18000.0f, bass ? 9.0f : 12.0f,
                         bass  ? .2f
                         : pad ? .65f
                               : .45f,
                         pad ? 18.0f : 45.0f};
        auto &v = preset.values;
        if (preset.source == SourceKind::synthesis)
        {
            if (pad)
            {
                preset.family = "Pads";
                v[parameterIndex(ParameterId::oscillator2Type)] = index % 2 == 0 ? 3.0f : 1.0f;
                v[parameterIndex(ParameterId::oscillator2Semitones)] = index % 3 == 0 ? 12.0f : 0.0f;
                v[parameterIndex(ParameterId::detuneCents)] = 8.0f + float(index % 7);
                v[parameterIndex(ParameterId::chorusMix)] = .18f + .025f * float(index % 5);
                v[parameterIndex(ParameterId::reverbDecaySeconds)] = 2.8f + .25f * float(index % 9);
                v[parameterIndex(ParameterId::filterEnvelopeOctaves)] = .35f;
            }
            else if (percussion)
            {
                preset.family = "Drums / Percussion";
                v[parameterIndex(ParameterId::oscillator2Type)] = 0;
                v[parameterIndex(ParameterId::oscillatorBlend)] = .1f;
                v[parameterIndex(ParameterId::oscillator2Semitones)] = -12;
                v[parameterIndex(ParameterId::filterEnvelopeOctaves)] = -1.5f;
                v[parameterIndex(ParameterId::polyphony)] = 16;
            }
            else if (!bass)
            {
                const bool pluck = v[parameterIndex(ParameterId::sustain)] < .3f;
                preset.family = pluck ? "Plucks" : "Synth Leads";
                v[parameterIndex(ParameterId::oscillator2Type)] = index % 2 == 0 ? 0.0f : 3.0f;
                v[parameterIndex(ParameterId::oscillator2Semitones)] = index % 5 == 0   ? 7.0f
                                                                       : index % 3 == 0 ? 12.0f
                                                                                        : 0.0f;
                v[parameterIndex(ParameterId::pulseWidth)] = .3f + .05f * float(index % 7);
                v[parameterIndex(ParameterId::filterEnvelopeOctaves)] = pluck ? 1.6f : .55f;
                v[parameterIndex(ParameterId::bendRangeSemitones)] = 2.0f + float(index % 3);
                v[parameterIndex(ParameterId::delayEnabled)] = pluck && index % 2 == 0 ? 1.0f : 0.0f;
                v[parameterIndex(ParameterId::delayTimeMs)] = 180.0f + 40.0f * float(index % 6);
                v[parameterIndex(ParameterId::delayFeedback)] = .22f + .025f * float(index % 4);
                v[parameterIndex(ParameterId::delayMix)] = .12f + .025f * float(index % 4);
            }
            else
            {
                v[parameterIndex(ParameterId::oscillator2Type)] = 0;
                v[parameterIndex(ParameterId::oscillator2Semitones)] = index % 2 == 0 ? -12.0f : 0.0f;
                v[parameterIndex(ParameterId::oscillatorBlend)] = .12f;
                v[parameterIndex(ParameterId::filterEnvelopeOctaves)] = .75f + .2f * float(index % 3);
                v[parameterIndex(ParameterId::compressorEnabled)] = 1;
                v[parameterIndex(ParameterId::compressorThresholdDb)] = -16;
                v[parameterIndex(ParameterId::compressorRatio)] = 2.5f;
                v[parameterIndex(ParameterId::space)] = 0;
            }
        }
        if (std::string_view(preset.genre) == "Pop")
            preset.genre = "Pop|R&B";
        else if (std::string_view(preset.genre) == "Jazz")
            preset.genre = "Jazz|R&B";
        else if (std::string_view(preset.genre) == "Funk")
            preset.genre = "Funk|Hip-Hop";
        else if (std::string_view(preset.genre) == "Rock")
            preset.genre = "Rock|Pop";
        else if (std::string_view(preset.genre) == "Cinematic")
            preset.genre = "Cinematic|Electronic";
    }
    result[1].family = "Violin";
    for (const auto index : {5u, 6u})
        result[index].family = "Woodwinds";
    for (const auto index : {7u, 8u, 9u, 10u})
        result[index].family = "Brass";
    for (const auto index : {11u, 13u})
        result[index].family = "Plucks";
    result[14].family = "Arabic / World";
    result[25].family = "Pads"; // Hush Pad keeps its slow synthetic-bed settings.
    for (const auto index : {15u, 16u, 17u})
        result[index].family = "Drums / Percussion";
    // A genuine recorded/synthesized layer provides an honest electric-keyboard
    // option without claiming a sampled Rhodes, Wurlitzer or proprietary preset.
    auto &electric = result[22];
    electric.name = "Glass Electric Keys";
    electric.family = "Electric Piano";
    electric.source = SourceKind::hybrid;
    electric.sampleAsset = "piano-upright";
    electric.description = "Recorded upright layered with sine/triangle oscillators. Original electric keys, "
                           "not a sampled Rhodes or Wurlitzer.";
    electric.values[parameterIndex(ParameterId::sourceType)] = 2;
    electric.values[parameterIndex(ParameterId::sampleLevel)] = .65f;
    electric.values[parameterIndex(ParameterId::oscillatorType)] = 0;
    electric.values[parameterIndex(ParameterId::oscillator2Type)] = 3;
    electric.values[parameterIndex(ParameterId::oscillatorBlend)] = .35f;
    electric.values[parameterIndex(ParameterId::chorusEnabled)] = 1;
    electric.values[parameterIndex(ParameterId::chorusMix)] = .15f;
    std::vector<FactoryPreset> all(result.begin(), result.end());
    addProductionVoicings(all);
    return all;
}
} // namespace

std::span<const ParameterSpec> parameterSpecs() noexcept
{
    static const auto specs = []
    {
        auto result = baseSpecs;
        result[parameterIndex(ParameterId::factoryPreset)].maximum = float(factoryPresets().size() - 1);
        return result;
    }();
    return specs;
}
const ParameterSpec &parameterSpec(ParameterId id)
{
    if (parameterIndex(id) >= parameterCount)
        throw std::out_of_range("Unknown Synth parameter");
    return parameterSpecs()[parameterIndex(id)];
}
Values defaultValues() noexcept
{
    Values values{};
    for (const auto &spec : baseSpecs)
        values[parameterIndex(spec.id)] = spec.initial;
    return values;
}
std::span<const FactoryPreset> factoryPresets() noexcept
{
    static const auto presets = makeFactoryPresets();
    return presets;
}
const FactoryPreset *findFactoryPreset(std::string_view id) noexcept
{
    for (const auto &preset : factoryPresets())
        if (id == preset.id)
            return &preset;
    return nullptr;
}
const char *sourceKindName(SourceKind source) noexcept
{
    switch (source)
    {
    case SourceKind::sample:
        return "SAMPLE";
    case SourceKind::hybrid:
        return "SAMPLE + SYNTH";
    default:
        return "SYNTHESIS";
    }
}
std::span<const char *const> genreNames() noexcept
{
    return genres;
}
std::span<const char *const> familyNames() noexcept
{
    return families;
}
bool presetHasGenre(const FactoryPreset &preset, std::string_view genre) noexcept
{
    if (genre.empty() || genre == "All" || genre == "All Genres")
        return true;
    std::string_view tags(preset.genre);
    while (!tags.empty())
    {
        const auto end = tags.find_first_of("|,");
        auto tag = tags.substr(0, end);
        while (!tag.empty() && tag.front() == ' ')
            tag.remove_prefix(1);
        while (!tag.empty() && tag.back() == ' ')
            tag.remove_suffix(1);
        if (tag == genre)
            return true;
        if (end == std::string_view::npos)
            break;
        tags.remove_prefix(end + 1);
    }
    return false;
}
} // namespace vibera::synth
