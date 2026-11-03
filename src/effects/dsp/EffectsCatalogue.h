// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "EffectsTypes.h"
#include <span>
#include <stdexcept>

namespace vibera::effects
{
namespace catalogue
{
inline constexpr ParameterSpec balance[] = {{"gain", "Gain", -96, 12, 0, "dB"}, {"pan", "Pan", -1, 1, 0, ""}};
inline constexpr ParameterSpec width[] = {{"width", "Width", 0, 2, 1, ""}};
inline constexpr ParameterSpec polarity[] = {{"left", "Invert left", 0, 1, 0, "", 1, 1},
                                             {"right", "Invert right", 0, 1, 0, "", 1, 1}};
inline constexpr ParameterSpec mute[] = {{"muted", "Muted", 0, 1, 0, "", 1, 1}};
inline constexpr ParameterSpec matrix[] = {{"ll", "Left to left", -2, 2, 1, ""},
                                           {"rl", "Right to left", -2, 2, 0, ""},
                                           {"lr", "Left to right", -2, 2, 0, ""},
                                           {"rr", "Right to right", -2, 2, 1, ""}};
inline constexpr ParameterSpec dc[] = {{"cutoff", "Cutoff", 5, 80, 20, "Hz", .5f}};
inline constexpr ParameterSpec bass[] = {{"frequency", "Frequency", 30, 400, 100, "Hz", .4f},
                                         {"gain", "Gain", 0, 18, 0, "dB"}};
inline constexpr ParameterSpec eq[] = {
    {"band80", "80 Hz", -18, 18, 0, "dB"},      {"band200", "200 Hz", -18, 18, 0, "dB"},
    {"band500", "500 Hz", -18, 18, 0, "dB"},    {"band1250", "1.25 kHz", -18, 18, 0, "dB"},
    {"band3150", "3.15 kHz", -18, 18, 0, "dB"}, {"band8000", "8 kHz", -18, 18, 0, "dB"},
    {"band16000", "16 kHz", -18, 18, 0, "dB"},  {"output", "Output", -24, 12, 0, "dB"}};
inline constexpr ParameterSpec filter[] = {{"mode", "Mode: 0 LP, 1 HP, 2 BP", 0, 2, 0, "", 1, 1},
                                           {"cutoff", "Cutoff", 20, 20000, 1000, "Hz", .25f},
                                           {"q", "Resonance Q", .1f, 12, .70710678f, "", .4f},
                                           {"mix", "Mix", 0, 1, 1, ""}};
inline constexpr ParameterSpec clip[] = {{"threshold", "Threshold", -36, 0, -6, "dB"},
                                         {"output", "Output", -24, 12, 0, "dB"},
                                         {"mix", "Mix", 0, 1, 1, ""}};
inline constexpr ParameterSpec drive[] = {
    {"drive", "Drive", 0, 36, 6, "dB"}, {"output", "Output", -36, 12, -6, "dB"}, {"mix", "Mix", 0, 1, 1, ""}};
inline constexpr ParameterSpec crush[] = {{"bits", "Bit depth", 2, 24, 12, "bits", 1, 1},
                                          {"downsample", "Sample hold", 1, 64, 1, "samples", 1, 1},
                                          {"mix", "Mix", 0, 1, 1, ""}};
inline constexpr ParameterSpec compressor[] = {
    {"threshold", "Threshold", -60, 0, -18, "dB"},  {"ratio", "Ratio", 1, 20, 4, ":1"},
    {"attack", "Attack", .1f, 200, 10, "ms", .35f}, {"release", "Release", 5, 2000, 120, "ms", .35f},
    {"makeup", "Makeup", -12, 24, 0, "dB"},         {"mix", "Mix", 0, 1, 1, ""}};
inline constexpr ParameterSpec limiter[] = {{"ceiling", "Ceiling", -24, 0, -1, "dB"},
                                            {"release", "Release", 5, 1000, 80, "ms", .35f},
                                            {"input", "Input", -12, 24, 0, "dB"}};
inline constexpr ParameterSpec gate[] = {{"threshold", "Threshold", -90, 0, -48, "dB"},
                                         {"attack", "Attack", .1f, 100, 2, "ms", .35f},
                                         {"release", "Release", 5, 2000, 150, "ms", .35f},
                                         {"floor", "Floor", -96, 0, -96, "dB"}};
inline constexpr ParameterSpec delay[] = {{"time", "Time", 1, 2000, 250, "ms", .4f},
                                          {"feedback", "Feedback", 0, .95f, .35f, ""},
                                          {"mix", "Mix", 0, 1, .3f, ""},
                                          {"crossfeed", "Crossfeed", 0, 1, 0, ""},
                                          {"tone", "Feedback low-pass", 200, 20000, 8000, "Hz", .3f}};
inline constexpr ParameterSpec chorus[] = {{"rate", "Rate", .05f, 8, .8f, "Hz", .4f},
                                           {"depth", "Depth", 0, 12, 4, "ms"},
                                           {"mix", "Mix", 0, 1, .35f, ""},
                                           {"width", "Stereo width", 0, 1, 1, ""}};
inline constexpr ParameterSpec flanger[] = {{"rate", "Rate", .02f, 5, .2f, "Hz", .4f},
                                            {"depth", "Depth", .1f, 10, 2, "ms"},
                                            {"feedback", "Feedback", -.9f, .9f, .35f, ""},
                                            {"mix", "Mix", 0, 1, .5f, ""}};
inline constexpr ParameterSpec phaser[] = {{"rate", "Rate", .02f, 8, .4f, "Hz", .4f},
                                           {"depth", "Depth", 0, 1, .7f, ""},
                                           {"feedback", "Feedback", -.9f, .9f, .2f, ""},
                                           {"mix", "Mix", 0, 1, .5f, ""}};
inline constexpr ParameterSpec reverb[] = {{"size", "Room size", 0, 1, .6f, ""},
                                           {"decay", "Decay", .1f, 12, 2, "s", .4f},
                                           {"damping", "Damping", 0, 1, .5f, ""},
                                           {"predelay", "Pre-delay", 0, 200, 20, "ms"},
                                           {"mix", "Mix", 0, 1, .25f, ""}};
inline constexpr ParameterSpec tremolo[] = {{"rate", "Rate", .05f, 20, 4, "Hz", .4f},
                                            {"depth", "Depth", 0, 1, .5f, ""},
                                            {"shape", "Shape: sine to square", 0, 1, 0, ""}};
inline constexpr ParameterSpec ring[] = {{"frequency", "Frequency", .1f, 2000, 30, "Hz", .25f},
                                         {"mix", "Mix", 0, 1, .5f, ""}};
inline constexpr ParameterSpec pan[] = {{"rate", "Rate", .02f, 10, .5f, "Hz", .4f},
                                        {"depth", "Depth", 0, 1, 1, ""}};
inline constexpr ParameterSpec tune[] = {
    {"key", "Key: 0 C .. 11 B", 0, 11, 0, "", 1, 1},
    {"scale", "Scale 0Chr 1Maj 2Min 3HMin 4Pent 5Rast 6Bayati 7Saba 8Sikah 9Huzam 10Hijaz 11Nahawand 12Kurd 13Athar 14Jiharkah", 0, 14, 1, "", 1, 1},
    {"retune", "Retune time", 0, 400, 40, "ms", .4f},
    {"amount", "Amount", 0, 1, 1, ""},
    {"mix", "Mix", 0, 1, 1, ""}};
inline constexpr ParameterSpec vocoder[] = {{"note", "Carrier note (MIDI)", 24, 96, 48, "", 1, 1},
                                            {"release", "Release", 5, 500, 60, "ms", .4f},
                                            {"noise", "Noise", 0, 1, .15f, ""},
                                            {"mix", "Mix", 0, 1, 1, ""},
                                            {"output", "Output", -24, 24, 0, "dB"}};
template <std::size_t N>
constexpr EffectSpec entry(EffectId id, const char *slug, const char *name, const char *category,
                           const ParameterSpec (&parameters)[N])
{
    static_assert(N <= maximumParameters);
    return {id, slug, name, category, parameters, N};
}
inline constexpr std::array effects = {
    entry(EffectId::balance, "balance", "Balance", "Utility", balance),
    entry(EffectId::stereoWidth, "stereo-width", "Stereo Width", "Stereo", width),
    entry(EffectId::polarity, "polarity", "Polarity", "Utility", polarity),
    entry(EffectId::mute, "mute", "Mute", "Utility", mute),
    entry(EffectId::stereoMatrix, "stereo-matrix", "Stereo Matrix", "Stereo", matrix),
    entry(EffectId::dcBlock, "dc-block", "DC Block", "Utility", dc),
    entry(EffectId::bassBoost, "bass-boost", "Bass Shelf", "EQ", bass),
    entry(EffectId::graphicEq, "graphic-eq", "Seven Band EQ", "EQ", eq),
    entry(EffectId::filter, "filter", "Filter", "EQ", filter),
    entry(EffectId::softClip, "soft-clip", "Soft Clip", "Distortion", clip),
    entry(EffectId::distortion, "distortion", "Drive", "Distortion", drive),
    entry(EffectId::bitCrusher, "bit-crusher", "Crush", "Distortion", crush),
    entry(EffectId::compressor, "compressor", "Compressor", "Dynamics", compressor),
    entry(EffectId::limiter, "limiter", "Peak Limiter", "Dynamics", limiter),
    entry(EffectId::gate, "gate", "Gate", "Dynamics", gate),
    entry(EffectId::delay, "delay", "Stereo Delay", "Time", delay),
    entry(EffectId::chorus, "chorus", "Chorus", "Modulation", chorus),
    entry(EffectId::flanger, "flanger", "Flanger", "Modulation", flanger),
    entry(EffectId::phaser, "phaser", "Phaser", "Modulation", phaser),
    entry(EffectId::reverb, "reverb", "Room Reverb", "Time", reverb),
    entry(EffectId::tremolo, "tremolo", "Tremolo", "Modulation", tremolo),
    entry(EffectId::ringMod, "ring-mod", "Ring Mod", "Modulation", ring),
    entry(EffectId::autoPan, "auto-pan", "Auto Pan", "Modulation", pan),
    entry(EffectId::pitchCorrect, "tune", "Tune (pitch correction)", "Pitch", tune),
    entry(EffectId::vocoder, "vocoder", "Vocoder", "Pitch", vocoder)};
static_assert(effects.size() == static_cast<std::size_t>(EffectId::count));
} // namespace catalogue
inline std::span<const EffectSpec> catalogueEntries() noexcept
{
    return catalogue::effects;
}
inline const EffectSpec &effectSpec(EffectId id)
{
    const auto index = static_cast<std::size_t>(id);
    if (index >= catalogue::effects.size())
        throw std::out_of_range("Unknown VibeRa effect");
    return catalogue::effects[index];
}
inline Values defaultValues(EffectId id)
{
    Values result{};
    const auto &spec = effectSpec(id);
    for (std::size_t i = 0; i < spec.parameterCount; ++i)
        result[i] = spec.parameters[i].initial;
    return result;
}
} // namespace vibera::effects
