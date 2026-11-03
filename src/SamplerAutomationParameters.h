// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerModulationSettings.h"
#include <cstdint>
#include <optional>

namespace vibera
{
// Stable sampler modulation codes. DSP eligibility is owned by the prepared consumer.
// These explicit names/codes are independent of the manual registry's row order.
enum class SamplerAutomationTargetCode : std::uint32_t
{
    volume = 0,
    pan = 1,
    cutoff = 2,
    resonance = 3,
    pitch = 4
};
enum class SamplerAutomationFieldCode : std::uint32_t
{
    enabled = 0,
    tempo = 1,
    delay = 2,
    attack = 3,
    hold = 4,
    decay = 5,
    sustain = 6,
    release = 7,
    attackTension = 8,
    decayTension = 9,
    releaseTension = 10,
    amount = 11,
    lfoEnabled = 12,
    lfoTempo = 13,
    lfoGlobal = 14,
    lfoDelay = 15,
    lfoAttack = 16,
    lfoPeriod = 17,
    lfoAmount = 18,
    lfoShape = 19
};
enum class SamplerAutomationValueKind
{
    continuous,
    boolean,
    shape
};
enum class SamplerAutomationTimebase
{
    none,
    envelopeTempo,
    lfoTempo
};
struct SamplerAutomationField
{
    SamplerAutomationFieldCode code;
    std::string_view id, units;
    SamplerAutomationValueKind kind;
    SamplerAutomationTimebase timebase = SamplerAutomationTimebase::none;
};
inline constexpr std::array<SamplerAutomationField, 20> samplerAutomationFields{
    {{SamplerAutomationFieldCode::enabled, "enabled", "boolean", SamplerAutomationValueKind::boolean},
     {SamplerAutomationFieldCode::tempo, "tempo", "boolean", SamplerAutomationValueKind::boolean},
     {SamplerAutomationFieldCode::delay, "delay", "secondsOrBeats", SamplerAutomationValueKind::continuous,
      SamplerAutomationTimebase::envelopeTempo},
     {SamplerAutomationFieldCode::attack, "attack", "secondsOrBeats", SamplerAutomationValueKind::continuous,
      SamplerAutomationTimebase::envelopeTempo},
     {SamplerAutomationFieldCode::hold, "hold", "secondsOrBeats", SamplerAutomationValueKind::continuous,
      SamplerAutomationTimebase::envelopeTempo},
     {SamplerAutomationFieldCode::decay, "decay", "secondsOrBeats", SamplerAutomationValueKind::continuous,
      SamplerAutomationTimebase::envelopeTempo},
     {SamplerAutomationFieldCode::sustain, "sustain", "normalized", SamplerAutomationValueKind::continuous},
     {SamplerAutomationFieldCode::release, "release", "secondsOrBeats",
      SamplerAutomationValueKind::continuous, SamplerAutomationTimebase::envelopeTempo},
     {SamplerAutomationFieldCode::attackTension, "attackTension", "bipolarTension",
      SamplerAutomationValueKind::continuous},
     {SamplerAutomationFieldCode::decayTension, "decayTension", "bipolarTension",
      SamplerAutomationValueKind::continuous},
     {SamplerAutomationFieldCode::releaseTension, "releaseTension", "bipolarTension",
      SamplerAutomationValueKind::continuous},
     {SamplerAutomationFieldCode::amount, "amount", "bipolarNormalized",
      SamplerAutomationValueKind::continuous},
     {SamplerAutomationFieldCode::lfoEnabled, "lfoEnabled", "boolean", SamplerAutomationValueKind::boolean},
     {SamplerAutomationFieldCode::lfoTempo, "lfoTempo", "boolean", SamplerAutomationValueKind::boolean},
     {SamplerAutomationFieldCode::lfoGlobal, "lfoGlobal", "boolean", SamplerAutomationValueKind::boolean},
     {SamplerAutomationFieldCode::lfoDelay, "lfoDelay", "secondsOrBeats",
      SamplerAutomationValueKind::continuous, SamplerAutomationTimebase::lfoTempo},
     {SamplerAutomationFieldCode::lfoAttack, "lfoAttack", "secondsOrBeats",
      SamplerAutomationValueKind::continuous, SamplerAutomationTimebase::lfoTempo},
     {SamplerAutomationFieldCode::lfoPeriod, "lfoPeriod", "secondsOrBeats",
      SamplerAutomationValueKind::continuous, SamplerAutomationTimebase::lfoTempo},
     {SamplerAutomationFieldCode::lfoAmount, "lfoAmount", "bipolarNormalized",
      SamplerAutomationValueKind::continuous},
     {SamplerAutomationFieldCode::lfoShape, "lfoShape", "shapeIndex", SamplerAutomationValueKind::shape}}};
struct SamplerAutomationTargetName
{
    SamplerAutomationTargetCode code;
    std::string_view id;
};
inline constexpr std::array<SamplerAutomationTargetName, 5> samplerAutomationTargetNames{
    {{SamplerAutomationTargetCode::volume, "volume"},
     {SamplerAutomationTargetCode::pan, "pan"},
     {SamplerAutomationTargetCode::cutoff, "cutoff"},
     {SamplerAutomationTargetCode::resonance, "resonance"},
     {SamplerAutomationTargetCode::pitch, "pitch"}}};
inline constexpr std::uint32_t samplerAutomationTargetStride = 32;
inline constexpr unsigned samplerAutomationParameterCount = 88;
struct SamplerAutomationParameter
{
    std::uint32_t code;
    std::string_view target, id, label, units;
    double minimum, maximum, initial;
    SamplerAutomationValueKind kind;
    SamplerAutomationTimebase timebase;
    int registryTarget;
    const SamplerModulationParameter *registryParameter;
};
inline std::optional<SamplerAutomationParameter> lookupSamplerAutomationParameter(std::uint32_t code) noexcept
{
    const auto targetCode = code / samplerAutomationTargetStride;
    const auto fieldCode = code % samplerAutomationTargetStride;
    const SamplerAutomationTargetName *target = nullptr;
    const SamplerAutomationField *field = nullptr;
    for (const auto &entry : samplerAutomationTargetNames)
        if (std::uint32_t(entry.code) == targetCode)
            target = &entry;
    for (const auto &entry : samplerAutomationFields)
        if (std::uint32_t(entry.code) == fieldCode)
            field = &entry;
    if (!target || !field || (target->code == SamplerAutomationTargetCode::pan && fieldCode < 12))
        return {};
    const auto registryTarget = samplerModulationTarget(target->id);
    const auto *parameter = samplerModulationParameter(field->id);
    // A missing/retyped registry row is unsupported, never silently mapped by position.
    if (registryTarget < 0 || !parameter ||
        parameter->boolean != (field->kind == SamplerAutomationValueKind::boolean) ||
        parameter->integral != (field->kind != SamplerAutomationValueKind::continuous))
        return {};
    return SamplerAutomationParameter{
        code,         target->id,         parameter->id,      parameter->label,
        field->units, parameter->minimum, parameter->maximum, parameter->initial,
        field->kind,  field->timebase,    registryTarget,     parameter};
}
inline std::optional<std::uint32_t> samplerAutomationParameterCode(std::string_view target,
                                                                   std::string_view parameter) noexcept
{
    for (const auto &owner : samplerAutomationTargetNames)
        if (owner.id == target)
            for (const auto &field : samplerAutomationFields)
                if (field.id == parameter)
                {
                    const auto code =
                        std::uint32_t(owner.code) * samplerAutomationTargetStride + std::uint32_t(field.code);
                    if (lookupSamplerAutomationParameter(code))
                        return code;
                }
    return {};
}
// Existing AutomationLink/InitialValue/Lane store physical values as float.
// Recognize ONLY the exact float encoding of each declared double endpoint;
// neighboring floats still pass/fail ordinary bounds. This is not epsilon
// tolerance or a clamp, and must not be applied to arbitrary double manual input.
// Interiors preserve their exact float value promoted to double. Discrete point
// validation or interpolated-curve quantization remains a separate caller step.
inline bool samplerAutomationFloatToPhysical(const SamplerAutomationParameter &parameter, float stored,
                                             double &output) noexcept
{
    if (!std::isfinite(stored))
        return false;
    const double physical = stored == float(parameter.minimum)   ? parameter.minimum
                            : stored == float(parameter.maximum) ? parameter.maximum
                                                                 : double(stored);
    if (physical < parameter.minimum || physical > parameter.maximum)
        return false;
    output = physical;
    return true;
}
// Strict physical values for defaults/manual controls/persisted discrete points.
// JSON type validation belongs to the eventual canonical parser, not this numeric helper.
inline bool samplerAutomationValueValid(const SamplerAutomationParameter &parameter, double value) noexcept
{
    return std::isfinite(value) && value >= parameter.minimum && value <= parameter.maximum &&
           (parameter.kind == SamplerAutomationValueKind::continuous || std::floor(value) == value);
}
// Explicit policy for a future interpolated curve: physical, NOT normalized input.
// Boolean switches at .5; shape rounds nearest with .5 ties upward. No clamping,
// coercion, or output mutation on bad input; this helper does not consume curves.
inline bool quantizeSamplerAutomationValue(const SamplerAutomationParameter &parameter, double value,
                                           double &output) noexcept
{
    if (!std::isfinite(value) || value < parameter.minimum || value > parameter.maximum)
        return false;
    output = parameter.kind == SamplerAutomationValueKind::continuous ? value : std::floor(value + .5);
    return true;
}
} // namespace vibera
