// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace vibera::synth
{
// Persisted native parameter IDs. Append only after the first qualified release.
enum class ParameterId : std::uint32_t
{
    tone,
    warmth,
    space,
    motion,
    attackMs,
    decayMs,
    sustain,
    releaseMs,
    filterType,
    cutoffHz,
    resonance,
    eqEnabled,
    eqLowDb,
    eqMidDb,
    eqHighDb,
    compressorEnabled,
    compressorThresholdDb,
    compressorRatio,
    chorusEnabled,
    chorusMix,
    delayEnabled,
    delayTimeMs,
    delayFeedback,
    delayMix,
    reverbEnabled,
    reverbSize,
    reverbDecaySeconds,
    reverbMix,
    masterDb,
    sourceType,
    oscillatorType,
    oscillatorBlend,
    detuneCents,
    pitchBend,
    modWheel,
    oscillator2Type,
    oscillator2Semitones,
    pulseWidth,
    sampleLevel,
    filterEnvelopeOctaves,
    pan,
    bendRangeSemitones,
    vibratoRateHz,
    vibratoDepthCents,
    polyphony,
    factoryPreset,
    // Appended production engine controls (schema 1 states without them load
    // with these neutral defaults, which reproduce the earlier voice exactly).
    unisonVoices,
    unisonDetuneCents,
    unisonSpread,
    subLevel,
    noiseLevel,
    filterModel,
    filterDrive,
    filterKeyTrack,
    filterVelocityOctaves,
    modAttackMs,
    modDecayMs,
    modSustain,
    envelopeCurve,
    velocityToAmp,
    glideMs,
    voiceMode,
    fmAmount,
    analogDrift,
    wavetableBank,
    wavetablePosition,
    wavetableEnvelope,
    count
};
inline constexpr std::size_t parameterCount = static_cast<std::size_t>(ParameterId::count);
// Scalars present in every earlier schema 1 state; later IDs are optional there.
inline constexpr std::size_t legacyParameterCount = static_cast<std::size_t>(ParameterId::factoryPreset) + 1;
// A complete preset patch must fit the bounded native patch ACK (ipc::patchValues = 96).
static_assert(parameterCount <= 96);
using Values = std::array<float, parameterCount>;
constexpr std::size_t parameterIndex(ParameterId id) noexcept
{
    return static_cast<std::size_t>(id);
}

struct ParameterSpec
{
    ParameterId id;
    const char *key;
    const char *name;
    float minimum, maximum, initial;
    const char *unit;
    float interval = 0.0f;
    float skew = 1.0f;
};
enum class SourceKind
{
    synthesis,
    sample,
    hybrid
};
// Macro values are normalized. The adapter uses these per-preset limits for
// tone cutoff, warmth through low-EQ boost/output trim, wet ambience and vibrato.
struct MacroMapping
{
    float toneMinimumHz = 400.0f, toneMaximumHz = 18000.0f;
    float warmthMaximumBoostDb = 12.0f;
    float spaceMaximumMix = 0.5f;
    float motionMaximumCents = 35.0f;
};
struct FactoryPreset
{
    const char *id;
    const char *name;
    const char *genre;
    const char *family;
    SourceKind source;
    // Stable sample-library instrument ID; empty for an oscillator-only patch.
    const char *sampleAsset;
    const char *description;
    Values values;
    MacroMapping macros;
};

std::span<const ParameterSpec> parameterSpecs() noexcept;
const ParameterSpec &parameterSpec(ParameterId);
Values defaultValues() noexcept;
std::span<const FactoryPreset> factoryPresets() noexcept;
const FactoryPreset *findFactoryPreset(std::string_view id) noexcept;
const char *sourceKindName(SourceKind) noexcept;
std::span<const char *const> genreNames() noexcept;
std::span<const char *const> familyNames() noexcept;
bool presetHasGenre(const FactoryPreset &, std::string_view genre) noexcept;
} // namespace vibera::synth
