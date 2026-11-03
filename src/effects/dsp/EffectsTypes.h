// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>

namespace vibera::effects
{
// Persisted identifiers. Append new effects; never reorder existing values.
enum class EffectId
{
    balance,
    stereoWidth,
    polarity,
    mute,
    stereoMatrix,
    dcBlock,
    bassBoost,
    graphicEq,
    filter,
    softClip,
    distortion,
    bitCrusher,
    compressor,
    limiter,
    gate,
    delay,
    chorus,
    flanger,
    phaser,
    reverb,
    tremolo,
    ringMod,
    autoPan,
    pitchCorrect,
    vocoder,
    parametricEq,
    deEsser,
    transientShaper,
    multiband,
    saturator,
    maximizer,
    studioReverb,
    count
};
inline constexpr std::size_t maximumParameters = 12;
using Values = std::array<float, maximumParameters>;

struct ParameterSpec
{
    const char *id;
    const char *name;
    float minimum, maximum, initial;
    const char *unit;
    float skew = 1.0f;
    float interval = 0.0f;
};
struct EffectSpec
{
    EffectId id;
    const char *slug;
    const char *name;
    const char *category;
    const ParameterSpec *parameters;
    std::size_t parameterCount;
};
} // namespace vibera::effects
