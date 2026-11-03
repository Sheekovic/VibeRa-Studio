// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerAutomationParameters.h"
#include <limits>

// Original metadata/quantization contract tests only; no automation playback proof.
static void samplerAutomationParameterTests()
{
    using namespace vibera;
    struct GoldenField
    {
        const char *id, *units;
        double minimum, maximum, initial;
        SamplerAutomationValueKind kind;
    };
    using Kind = SamplerAutomationValueKind;
    // Literal persistence contract, independent of the production code table/order.
    const GoldenField golden[]{{"enabled", "boolean", 0, 1, 0, Kind::boolean},
                               {"tempo", "boolean", 0, 1, 0, Kind::boolean},
                               {"delay", "secondsOrBeats", 0, 64, 0, Kind::continuous},
                               {"attack", "secondsOrBeats", 0, 64, .01, Kind::continuous},
                               {"hold", "secondsOrBeats", 0, 64, 0, Kind::continuous},
                               {"decay", "secondsOrBeats", 0, 64, .1, Kind::continuous},
                               {"sustain", "normalized", 0, 1, 1, Kind::continuous},
                               {"release", "secondsOrBeats", 0, 64, .1, Kind::continuous},
                               {"attackTension", "bipolarTension", -1, 1, 0, Kind::continuous},
                               {"decayTension", "bipolarTension", -1, 1, 0, Kind::continuous},
                               {"releaseTension", "bipolarTension", -1, 1, 0, Kind::continuous},
                               {"amount", "bipolarNormalized", -1, 1, 0, Kind::continuous},
                               {"lfoEnabled", "boolean", 0, 1, 0, Kind::boolean},
                               {"lfoTempo", "boolean", 0, 1, 0, Kind::boolean},
                               {"lfoGlobal", "boolean", 0, 1, 0, Kind::boolean},
                               {"lfoDelay", "secondsOrBeats", 0, 64, 0, Kind::continuous},
                               {"lfoAttack", "secondsOrBeats", 0, 64, 0, Kind::continuous},
                               {"lfoPeriod", "secondsOrBeats", .01, 64, 1, Kind::continuous},
                               {"lfoAmount", "bipolarNormalized", -1, 1, 0, Kind::continuous},
                               {"lfoShape", "shapeIndex", 0, 2, 0, Kind::shape}};
    const char *targets[]{"volume", "pan", "cutoff", "resonance", "pitch"};
    unsigned count = 0;
    for (unsigned target = 0; target < 5; ++target)
        for (unsigned field = 0; field < 32; ++field)
        {
            const auto code = target * 32 + field;
            const auto parameter = lookupSamplerAutomationParameter(code);
            const bool expected = field < 20 && (target != 1 || field >= 12);
            require(bool(parameter) == expected,
                    "Exactly88 frozen target/field pairs; Pan envelope and all20..31 reservations refuse");
            if (!expected)
                continue;
            ++count;
            const auto &g = golden[field];
            require(
                parameter->code == code && parameter->target == targets[target] && parameter->id == g.id &&
                    parameter->units == g.units && parameter->minimum == g.minimum &&
                    parameter->maximum == g.maximum && parameter->initial == g.initial &&
                    parameter->kind == g.kind &&
                    samplerAutomationParameterCode(targets[target], g.id) == code,
                "Golden codes, physical ranges, defaults, kinds and units never follow registry reordering");
            const auto *registry = samplerModulationParameter(g.id);
            require(parameter->registryParameter == registry &&
                        parameter->registryTarget == samplerModulationTarget(targets[target]) &&
                        parameter->label == registry->label,
                    "Metadata borrows the original static registry by stable name, with process-lifetime "
                    "identity");
            SamplerModulation manual;
            require(samplerModulationValue(manual, *registry) == g.initial &&
                        samplerAutomationValueValid(*parameter, g.initial),
                    "Automation metadata preserves every original manual default");
            require(setSamplerModulationValue(manual, *registry, g.maximum) &&
                        samplerModulationValue(manual, *registry) == g.maximum,
                    "Each supported field still uses its original manual setter and physical domain");
            for (double invalid :
                 {g.minimum - 1, g.maximum + 1, std::numeric_limits<double>::quiet_NaN(),
                  std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
            {
                double untouched = 123;
                require(!samplerAutomationValueValid(*parameter, invalid) &&
                            !quantizeSamplerAutomationValue(*parameter, invalid, untouched) &&
                            untouched == 123,
                        "Nonfinite/out-of-range physical values refuse without changing output");
            }
            const bool envelopeTime = field == 2 || field == 3 || field == 4 || field == 5 || field == 7;
            const bool lfoTime = field == 15 || field == 16 || field == 17;
            require(parameter->timebase == (envelopeTime ? SamplerAutomationTimebase::envelopeTempo
                                            : lfoTime    ? SamplerAutomationTimebase::lfoTempo
                                                         : SamplerAutomationTimebase::none),
                    "Time units explicitly bind their own Tempo flag, not document tempo by assumption");
        }
    require(count == 88 && samplerAutomationParameterCount == 88,
            "Independent exhaustive golden enumeration has exactly88 usable sampler parameters");
    for (auto code : {160u, 191u, 10000u, std::numeric_limits<std::uint32_t>::max()})
        require(!lookupSamplerAutomationParameter(code),
                "Unknown target codes cannot alias a supported target");
    require(!samplerAutomationParameterCode("Volume", "attack") &&
                !samplerAutomationParameterCode("volume", "unknown") &&
                !samplerAutomationParameterCode("unknown", "attack") &&
                !samplerAutomationParameterCode("pan", "enabled") &&
                !samplerAutomationParameterCode("pan", "amount") &&
                !samplerAutomationParameterCode("pan", "releaseTension"),
            "Unknown/case-changed names and all Pan non-LFO fields refuse");
    // Distinct persistence landmarks also catch target stride and explicit field changes.
    require(samplerAutomationParameterCode("volume", "enabled") == 0 &&
                samplerAutomationParameterCode("pan", "lfoEnabled") == 44 &&
                samplerAutomationParameterCode("cutoff", "attack") == 67 &&
                samplerAutomationParameterCode("resonance", "lfoShape") == 115 &&
                samplerAutomationParameterCode("pitch", "lfoPeriod") == 145,
            "Independent literal saved-code landmarks remain fixed");
    // Actual float storage boundaries, independent of curve quantization.
    const auto period = *lookupSamplerAutomationParameter(17);
    const float storedMinimum = .01f;
    const float belowMinimum = std::nextafter(storedMinimum, -std::numeric_limits<float>::infinity());
    const float aboveMinimum = std::nextafter(storedMinimum, std::numeric_limits<float>::infinity());
    double physical = -99;
    require(double(storedMinimum) < .01 && !samplerAutomationValueValid(period, double(storedMinimum)) &&
                samplerAutomationFloatToPhysical(period, storedMinimum, physical) && physical == .01 &&
                samplerAutomationValueValid(period, physical),
            "Exact actual float LFO-period endpoint converts back to its canonical double minimum");
    physical = 456;
    require(!samplerAutomationFloatToPhysical(period, belowMinimum, physical) && physical == 456,
            "Adjacent out-of-range float is not legalized by endpoint adaptation");
    require(samplerAutomationFloatToPhysical(period, aboveMinimum, physical) &&
                physical == double(aboveMinimum) && physical > .01,
            "Adjacent interior float retains its exact promoted value without snapping");
    const float maximum = 64.f;
    const auto belowMaximum = std::nextafter(maximum, -std::numeric_limits<float>::infinity());
    require(samplerAutomationFloatToPhysical(period, maximum, physical) && physical == 64 &&
                samplerAutomationFloatToPhysical(period, belowMaximum, physical) &&
                physical == double(belowMaximum),
            "Upper endpoint and interior neighbor preserve the declared physical domain");
    for (float invalid : {std::nextafter(maximum, std::numeric_limits<float>::infinity()),
                          std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()})
    {
        physical = 456;
        require(!samplerAutomationFloatToPhysical(period, invalid, physical) && physical == 456,
                "Nonfinite or neighboring upper out-of-range storage refuses without output mutation");
    }
    const auto bipolar = *lookupSamplerAutomationParameter(11);
    require(samplerAutomationFloatToPhysical(bipolar, -1.f, physical) && physical == -1,
            "Exactly representable negative endpoint retains its canonical value");
    physical = 456;
    require(!samplerAutomationFloatToPhysical(
                bipolar, std::nextafter(-1.f, -std::numeric_limits<float>::infinity()), physical) &&
                physical == 456,
            "Negative out-of-range neighboring float still refuses");
    const auto boolean = *lookupSamplerAutomationParameter(0);
    const auto shape = *lookupSamplerAutomationParameter(19);
    const auto attack = *lookupSamplerAutomationParameter(3);
    require(samplerAutomationFloatToPhysical(boolean, .5f, physical) && physical == .5 &&
                !samplerAutomationValueValid(boolean, physical),
            "Endpoint adapter never silently quantizes or relaxes discrete stored-point validation");
    double result = -99;
    require(!samplerAutomationValueValid(boolean, .5) && !samplerAutomationValueValid(shape, 1.5) &&
                samplerAutomationValueValid(attack, .12345),
            "Strict stored discrete values reject fractional booleans/shapes without narrowing continuous "
            "controls");
    for (const auto &p : {boolean, shape})
    {
        SamplerModulation manual;
        const auto before = manual;
        require(!setSamplerModulationValue(manual, *p.registryParameter, .5) && manual == before,
                "Curve quantization never relaxes the original strict manual setter");
    }
    require(quantizeSamplerAutomationValue(boolean, .499999, result) && result == 0 &&
                quantizeSamplerAutomationValue(boolean, .5, result) && result == 1 &&
                quantizeSamplerAutomationValue(shape, .499999, result) && result == 0 &&
                quantizeSamplerAutomationValue(shape, .5, result) && result == 1 &&
                quantizeSamplerAutomationValue(shape, 1.499999, result) && result == 1 &&
                quantizeSamplerAutomationValue(shape, 1.5, result) && result == 2 &&
                quantizeSamplerAutomationValue(shape, 2, result) && result == 2,
            "Future curve discrete quantization has exact half-up boundaries, including endpoint2");
    require(
        quantizeSamplerAutomationValue(attack, 32, result) && result == 32,
        "Future curves use physical values; a32-second/beat attack is never mistaken for normalized data");
}
