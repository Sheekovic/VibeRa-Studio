// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "TunePitch.h"
#include <cstdint>
#include <signalsmith-stretch.h>
#include <vector>
#if VIBERA_TUNE_RUBBERBAND
#include "TuneRubberCandidate.h"
#endif

namespace vibera::tune
{
struct CorrectionParameters
{
    Targets targets;
    double amount = 1, speedMs = 50;
    bool bypass = false;
    bool valid() const noexcept
    {
        return targets.valid() && std::isfinite(amount) && amount >= 0 && amount <= 1 &&
               std::isfinite(speedMs) && speedMs >= 0 && speedMs <= 2000;
    }
};
// Experimental correction policy and prepared stereo shifter. Analysis is deliberately separate:
// the caller supplies an estimated pitch; no detector work or worker waits occur in process().
// This is not yet a Tracktion effect or an audited realtime callback implementation.
class Correction
{
#if VIBERA_TUNE_RUBBERBAND
    RubberCandidate shifter;
#elif VIBERA_TUNE_DOUBLE
    signalsmith::stretch::SignalsmithStretch<double> shifter{0};
#else
    signalsmith::stretch::SignalsmithStretch<float> shifter{0};
#endif
    std::vector<std::array<float, 2>> dry;
    CorrectionParameters parameters;
    Pitch lastPitch;
    double rate = 0, desired = 0, applied = 0;
    size_t position = 0;

  public:
    bool prepare(double sampleRate)
    {
        if (!std::isfinite(sampleRate) || sampleRate < 8000 || sampleRate > 192000)
            return false;
        rate = sampleRate;
#if !VIBERA_TUNE_RUBBERBAND && VIBERA_TUNE_WINDOW_MS > 0
        shifter.configure(2, int(rate * VIBERA_TUNE_WINDOW_MS / 1000), int(rate * VIBERA_TUNE_HOP_MS / 1000),
                          true);
#else
        shifter.presetDefault(2, float(rate), true);
#endif
        shifter.reset();
        dry.assign(size_t(latency()) + 1, {});
        position = 0;
        desired = applied = 0;
        lastPitch = {};
        shifter.setTransposeFactor(1);
        return true;
    }
    int latency() const noexcept
    {
        return shifter.inputLatency() + shifter.outputLatency();
    }
    bool setParameters(const CorrectionParameters &next) noexcept
    {
        if (!next.valid())
            return false;
        parameters = next;
        setPitch(lastPitch);
        return true;
    }
    void setPitch(Pitch pitch) noexcept
    {
        lastPitch = pitch;
        desired = 0;
        if (pitch.hz > 0 && std::isfinite(pitch.hz) && std::isfinite(pitch.confidence) &&
            pitch.confidence >= .8 && pitch.confidence <= 1)
        {
            const double midi = 69 + 12 * std::log2(pitch.hz / parameters.targets.referenceA);
            desired = std::clamp((parameters.targets.nearest(midi) - midi) * parameters.amount, -12., 12.);
        }
    }
    double correctionSemitones() const noexcept
    {
        return applied;
    }
    std::uint64_t diagnosticCount() const noexcept
    {
#if VIBERA_TUNE_RUBBERBAND
        return shifter.diagnostics().total;
#else
        return 0;
#endif
    }
    bool process(const float *const *input, float *const *output, int frames) noexcept
    {
        if (rate == 0 || frames < 0 || !input || !output || !input[0] || !input[1] || !output[0] ||
            !output[1] || input[0] == output[0] || input[0] == output[1] || input[1] == output[0] ||
            input[1] == output[1])
            return false;
        if (frames == 0)
            return true;
        const double alpha =
            parameters.speedMs == 0 ? 1 : -std::expm1(-frames * 1000. / (rate * parameters.speedMs));
        applied += alpha * (desired - applied);
        shifter.setTransposeFactor(float(std::exp2(applied / 12.)));
        shifter.process(input, frames, output, frames);
        // The bypass path stays delayed while the wet processor keeps running, so toggling bypass
        // does not reset spectral history. A production wrapper still needs a tested bypass ramp.
        for (int i = 0; i < frames; ++i)
        {
            const auto read = (position + 1) % dry.size();
            const auto delayed = dry[read];
            dry[position] = {input[0][i], input[1][i]};
            position = read;
            if (parameters.bypass)
            {
                output[0][i] = delayed[0];
                output[1][i] = delayed[1];
            }
        }
        return true;
    }
};
} // namespace vibera::tune
