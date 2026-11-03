// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "EffectsTypes.h"

#include <array>
#include <vector>

namespace vibera::effects
{
// Mixing and mastering processors: Parametric EQ, De-esser, Transient Shaper, Multiband
// Dynamics, Saturator and Maximizer. Independently implemented; filters are trapezoidal
// state-variable sections (stable under modulation). prepare() is the only allocating call.
class StudioEffects
{
  public:
    void prepare(double sampleRate);
    void reset() noexcept;
    void setParameters(EffectId effect, const Values &values) noexcept;
    void process(float &left, float &right) noexcept;
    // Fixed added delay (only the Maximizer looks ahead).
    int latencySamples(EffectId effect) const noexcept;
    // Last gain reduction in dB (dynamics processors), for meters and tests.
    float gainReductionDb() const noexcept { return reductionDb_; }
    // Magnitude response of the Parametric EQ at a frequency (for the editor curve).
    static double eqResponseDb(const Values &values, double frequency, double sampleRate) noexcept;

    struct Filter
    {
        double a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
        double ic1[2]{}, ic2[2]{};
        enum class Kind
        {
            bell,
            lowShelf,
            highShelf,
            lowPass,
            highPass,
            bandPass
        };
        void set(Kind kind, double frequency, double q, double gainDb, double sampleRate) noexcept;
        float tick(float input, int channel) noexcept;
        void reset() noexcept;
    };

  private:
    void processEq(float &left, float &right) noexcept;
    void processDeEsser(float &left, float &right) noexcept;
    void processTransient(float &left, float &right) noexcept;
    void processMultiband(float &left, float &right) noexcept;
    void processSaturator(float &left, float &right) noexcept;
    void processMaximizer(float &left, float &right) noexcept;
    double coefficient(double milliseconds) const noexcept;

    EffectId effect_ = EffectId::parametricEq;
    Values values_{};
    double sampleRate_ = 48000;
    float reductionDb_ = 0;
    // Parametric EQ: low cut, low shelf, two bells, high shelf, high cut.
    std::array<Filter, 6> eq_{};
    std::array<bool, 6> eqActive_{};
    // De-esser: detector band and the split high band that is turned down.
    Filter sibilance_{}, split_{};
    double deEssEnvelope_ = 0;
    // Transient shaper envelopes.
    double fast_ = 0, slow_ = 0, transientGain_ = 1;
    // Multiband: Linkwitz-Riley crossovers (two cascaded Butterworth sections each).
    std::array<Filter, 4> lowSplit_{}, highSplit_{};
    std::array<Filter, 4> allpass_{};
    std::array<double, 3> bandEnvelope_{}, bandGain_{1, 1, 1};
    // Saturator.
    Filter tiltLow_{}, tiltHigh_{};
    double dcIn_[2]{}, dcOut_[2]{};
    // Maximizer: lookahead delay, required-gain history and the smoothed gain.
    std::vector<float> delayL_, delayR_, required_;
    int lookahead_ = 1, cursor_ = 0;
    double held_ = 1, boxcarSum_ = 0;
    std::vector<double> boxcar_;
    int boxcarCursor_ = 0;
    bool prepared_ = false;
};
} // namespace vibera::effects
