// SPDX-License-Identifier: AGPL-3.0-or-later
#include "StudioEffects.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace vibera::effects
{
namespace
{
constexpr double pi = std::numbers::pi;
double decibels(double gain) noexcept
{
    return 20 * std::log10(std::max(gain, 1.e-9));
}
double gainOf(double db) noexcept
{
    return std::pow(10., db / 20.);
}
} // namespace

// Trapezoidal state-variable filter (independent implementation of the published
// linear-trapezoidal SVF equations); outputs are mixed into each response type.
void StudioEffects::Filter::set(Kind kind, double frequency, double q, double gainDb, double sampleRate) noexcept
{
    frequency = std::clamp(frequency, 10., sampleRate * .49);
    q = std::max(q, .05);
    const double a = std::pow(10., gainDb / 40.);
    double g = std::tan(pi * frequency / sampleRate), k = 1 / q;
    switch (kind)
    {
    case Kind::bell:
        k = 1 / (q * a);
        m0 = 1, m1 = k * (a * a - 1), m2 = 0;
        break;
    case Kind::lowShelf:
        g /= std::sqrt(a);
        m0 = 1, m1 = k * (a - 1), m2 = a * a - 1;
        break;
    case Kind::highShelf:
        g *= std::sqrt(a);
        m0 = a * a, m1 = k * (1 - a) * a, m2 = 1 - a * a;
        break;
    case Kind::lowPass:
        m0 = 0, m1 = 0, m2 = 1;
        break;
    case Kind::highPass:
        m0 = 1, m1 = -k, m2 = -1;
        break;
    case Kind::bandPass:
        m0 = 0, m1 = 1, m2 = 0;
        break;
    }
    a1 = 1 / (1 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
}
float StudioEffects::Filter::tick(float input, int channel) noexcept
{
    const double v3 = input - ic2[channel];
    const double v1 = a1 * ic1[channel] + a2 * v3;
    const double v2 = ic2[channel] + a2 * ic1[channel] + a3 * v3;
    ic1[channel] = 2 * v1 - ic1[channel];
    ic2[channel] = 2 * v2 - ic2[channel];
    return float(m0 * input + m1 * v1 + m2 * v2);
}
void StudioEffects::Filter::reset() noexcept
{
    ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0;
}

void StudioEffects::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    lookahead_ = std::max(1, int(std::lround(sampleRate * .0015)));
    delayL_.assign(size_t(lookahead_), 0.f);
    delayR_.assign(size_t(lookahead_), 0.f);
    required_.assign(size_t(lookahead_ + 1), 1.f);
    boxcar_.assign(size_t(lookahead_), 1.);
    prepared_ = true;
    reset();
}
void StudioEffects::reset() noexcept
{
    for (auto &f : eq_)
        f.reset();
    sibilance_.reset();
    split_.reset();
    for (auto *bank : {&lowSplit_, &highSplit_, &allpass_})
        for (auto &f : *bank)
            f.reset();
    tiltLow_.reset();
    tiltHigh_.reset();
    deEssEnvelope_ = fast_ = slow_ = 0;
    transientGain_ = 1;
    bandEnvelope_.fill(0);
    bandGain_.fill(1);
    dcIn_[0] = dcIn_[1] = dcOut_[0] = dcOut_[1] = 0;
    std::fill(delayL_.begin(), delayL_.end(), 0.f);
    std::fill(delayR_.begin(), delayR_.end(), 0.f);
    std::fill(required_.begin(), required_.end(), 1.f);
    std::fill(boxcar_.begin(), boxcar_.end(), 1.);
    boxcarSum_ = double(boxcar_.size());
    held_ = 1;
    cursor_ = boxcarCursor_ = 0;
    reductionDb_ = 0;
}
int StudioEffects::latencySamples(EffectId effect) const noexcept
{
    return effect == EffectId::maximizer && prepared_ ? lookahead_ : 0;
}
double StudioEffects::coefficient(double milliseconds) const noexcept
{
    return 1 - std::exp(-1 / (std::max(milliseconds, .01) * .001 * sampleRate_));
}
void StudioEffects::setParameters(EffectId effect, const Values &values) noexcept
{
    effect_ = effect;
    values_ = values;
    const double rate = sampleRate_;
    using K = Filter::Kind;
    if (effect == EffectId::parametricEq)
    {
        eqActive_ = {values[0] > 20.5f, std::abs(values[2]) > .01f, std::abs(values[4]) > .01f,
                     std::abs(values[7]) > .01f, std::abs(values[10]) > .01f, values[11] < 19900.f};
        eq_[0].set(K::highPass, values[0], .70710678, 0, rate);
        eq_[1].set(K::lowShelf, values[1], .70710678, values[2], rate);
        eq_[2].set(K::bell, values[3], values[5], values[4], rate);
        eq_[3].set(K::bell, values[6], values[8], values[7], rate);
        eq_[4].set(K::highShelf, values[9], .70710678, values[10], rate);
        eq_[5].set(K::lowPass, values[11], .70710678, 0, rate);
    }
    else if (effect == EffectId::deEsser)
    {
        sibilance_.set(K::bandPass, values[0], 1.2, 0, rate);
        split_.set(K::lowPass, values[0] * .7, .5, 0, rate); // Complementary split: high = input - low.
    }
    else if (effect == EffectId::multiband)
    {
        // Linkwitz-Riley 4th order: two identical Butterworth sections per split.
        const double low = values[0], high = std::max(double(values[1]), values[0] * 1.5);
        lowSplit_[0].set(K::lowPass, low, .70710678, 0, rate);
        lowSplit_[1].set(K::lowPass, low, .70710678, 0, rate);
        lowSplit_[2].set(K::highPass, low, .70710678, 0, rate);
        lowSplit_[3].set(K::highPass, low, .70710678, 0, rate);
        highSplit_[0].set(K::lowPass, high, .70710678, 0, rate);
        highSplit_[1].set(K::lowPass, high, .70710678, 0, rate);
        highSplit_[2].set(K::highPass, high, .70710678, 0, rate);
        highSplit_[3].set(K::highPass, high, .70710678, 0, rate);
        // The low band passes the high crossover as an allpass (LP + HP) to stay in phase.
        for (int i = 0; i < 4; ++i)
            allpass_[size_t(i)].set(i < 2 ? K::lowPass : K::highPass, high, .70710678, 0, rate);
    }
    else if (effect == EffectId::saturator)
    {
        tiltLow_.set(K::lowShelf, 300, .70710678, -values[2] * 6, rate);
        tiltHigh_.set(K::highShelf, 3000, .70710678, values[2] * 6, rate);
    }
}
void StudioEffects::process(float &left, float &right) noexcept
{
    if (!prepared_)
        return;
    switch (effect_)
    {
    case EffectId::parametricEq:
        processEq(left, right);
        break;
    case EffectId::deEsser:
        processDeEsser(left, right);
        break;
    case EffectId::transientShaper:
        processTransient(left, right);
        break;
    case EffectId::multiband:
        processMultiband(left, right);
        break;
    case EffectId::saturator:
        processSaturator(left, right);
        break;
    case EffectId::maximizer:
        processMaximizer(left, right);
        break;
    default:
        break;
    }
}
void StudioEffects::processEq(float &left, float &right) noexcept
{
    for (size_t i = 0; i < eq_.size(); ++i)
        if (eqActive_[i])
        {
            left = eq_[i].tick(left, 0);
            right = eq_[i].tick(right, 1);
        }
}
double StudioEffects::eqResponseDb(const Values &values, double frequency, double sampleRate) noexcept
{
    // Analog prototype magnitudes evaluated at the prewarped frequency (matches the
    // trapezoidal filters exactly at every frequency).
    const auto warped = [&](double f) { return std::tan(pi * std::min(f, sampleRate * .49) / sampleRate); };
    const double w = warped(frequency);
    const auto magnitude = [&](Filter::Kind kind, double f0, double q, double gainDb)
    {
        const double a = std::pow(10., gainDb / 40.), wc = warped(f0);
        const double s = w / wc;
        using K = Filter::Kind;
        // |H(js)|^2 for the standard second-order sections.
        const auto ratio = [&](double nr, double ni, double dr, double di)
        { return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di)); };
        switch (kind)
        {
        case K::bell:
            return ratio(1 - s * s, s * a / q, 1 - s * s, s / (a * q));
        case K::lowShelf:
            return a * ratio(a - s * s, s * std::sqrt(a) / q, 1 - a * s * s, s * std::sqrt(a) / q);
        case K::highShelf:
            return a * ratio(1 - a * s * s, s * std::sqrt(a) / q, a - s * s, s * std::sqrt(a) / q);
        case K::lowPass:
            return ratio(1, 0, 1 - s * s, s / q);
        case K::highPass:
            return ratio(-s * s, 0, 1 - s * s, s / q);
        default:
            return 1.;
        }
    };
    using K = Filter::Kind;
    double db = 0;
    if (values[0] > 20.5f)
        db += decibels(magnitude(K::highPass, values[0], .70710678, 0));
    db += decibels(magnitude(K::lowShelf, values[1], .70710678, values[2]));
    db += decibels(magnitude(K::bell, values[3], values[5], values[4]));
    db += decibels(magnitude(K::bell, values[6], values[8], values[7]));
    db += decibels(magnitude(K::highShelf, values[9], .70710678, values[10]));
    if (values[11] < 19900.f)
        db += decibels(magnitude(K::lowPass, values[11], .70710678, 0));
    return db;
}
void StudioEffects::processDeEsser(float &left, float &right) noexcept
{
    // Detect the sibilance band; above the threshold turn down only the split high band.
    const double threshold = values_[1], range = values_[2];
    const double detect = std::max(std::abs(sibilance_.tick(left, 0)), std::abs(sibilance_.tick(right, 1)));
    const double attack = coefficient(.5), release = coefficient(60);
    deEssEnvelope_ += (detect - deEssEnvelope_) * (detect > deEssEnvelope_ ? attack : release);
    const double over = decibels(deEssEnvelope_) - threshold;
    const double reduction = over > 0 ? std::min(range, over * .75) : 0.;
    reductionDb_ = float(-reduction);
    const double keep = gainOf(-reduction);
    const float highL = left - split_.tick(left, 0), highR = right - split_.tick(right, 1);
    if (values_[3] >= .5f) // Listen: hear only what is being removed.
    {
        left = float(highL * (1 - keep));
        right = float(highR * (1 - keep));
        return;
    }
    left -= float(highL * (1 - keep));
    right -= float(highR * (1 - keep));
}
void StudioEffects::processTransient(float &left, float &right) noexcept
{
    // A fast and a slow envelope: their ratio marks attacks (fast above slow) and tails.
    const double level = std::max(std::abs(left), std::abs(right));
    fast_ += (level - fast_) * (level > fast_ ? coefficient(.3) : coefficient(30));
    slow_ += (level - slow_) * (level > slow_ ? coefficient(25) : coefficient(250));
    const double ratio = (fast_ + 1.e-6) / (slow_ + 1.e-6);
    const double attackDb = values_[0] / 100. * 18. * std::clamp(std::log2(ratio), 0., 1.);
    const double sustainDb = values_[1] / 100. * 18. * std::clamp(-std::log2(ratio) * 2, 0., 1.);
    const double target = gainOf(attackDb + sustainDb);
    transientGain_ += (target - transientGain_) * coefficient(1);
    const double output = gainOf(values_[2]), mix = values_[3];
    const double wet = transientGain_ * output;
    left = float(left * (mix * wet + (1 - mix)));
    right = float(right * (mix * wet + (1 - mix)));
}
void StudioEffects::processMultiband(float &left, float &right) noexcept
{
    float bands[3][2];
    const float in[2]{left, right};
    for (int c = 0; c < 2; ++c)
    {
        float low = lowSplit_[1].tick(lowSplit_[0].tick(in[c], c), c);
        const float rest = lowSplit_[3].tick(lowSplit_[2].tick(in[c], c), c);
        low = allpass_[1].tick(allpass_[0].tick(low, c), c) + allpass_[3].tick(allpass_[2].tick(low, c), c);
        bands[0][c] = low;
        bands[1][c] = highSplit_[1].tick(highSplit_[0].tick(rest, c), c);
        bands[2][c] = highSplit_[3].tick(highSplit_[2].tick(rest, c), c);
    }
    // Upward and downward compression per band (OTT-style), scaled by depth.
    const double depth = values_[2], time = values_[3];
    const double up = values_[4], down = values_[5];
    constexpr double attackMs[3]{30, 15, 8};
    const double bandTrim[3]{values_[6], values_[7], values_[8]};
    double worst = 0;
    double outL = 0, outR = 0;
    for (int b = 0; b < 3; ++b)
    {
        const double level = std::max(std::abs(bands[b][0]), std::abs(bands[b][1]));
        auto &env = bandEnvelope_[size_t(b)];
        env += (level - env) * (level > env ? coefficient(attackMs[b] * time) : coefficient(attackMs[b] * 4 * time));
        const double db = decibels(env);
        double change = 0;
        if (db > -24)
            change -= (db + 24) * (1 - 1 / (1 + down * 7));
        else if (db > -70 && db < -40)
            change += std::min(24., (-40 - db) * up * .75);
        change *= depth;
        worst = std::min(worst, change);
        const double gain = gainOf(change + bandTrim[b]);
        outL += bands[b][0] * gain;
        outR += bands[b][1] * gain;
    }
    reductionDb_ = float(worst);
    const double output = gainOf(values_[9]);
    left = float(outL * output);
    right = float(outR * output);
}
void StudioEffects::processSaturator(float &left, float &right) noexcept
{
    const double drive = gainOf(values_[0]), character = values_[1], mix = values_[3];
    const double norm = 1 / std::tanh(drive);
    const auto shape = [&](double x)
    {
        const double tape = std::tanh(x * drive) * norm;
        // Tube: asymmetric, softer on the negative side, adding even harmonics.
        const double y = x * drive;
        const double tube = (y >= 0 ? std::tanh(y) : std::tanh(y * .6) / .6 * .85) * norm;
        return tape + (tube - tape) * character;
    };
    double wet[2]{shape(left), shape(right)};
    for (int c = 0; c < 2; ++c)
    {
        // DC blocker after the asymmetric curve.
        const double out = wet[c] - dcIn_[c] + .9995 * dcOut_[c];
        dcIn_[c] = wet[c];
        dcOut_[c] = out;
        wet[c] = tiltHigh_.tick(tiltLow_.tick(float(out), c), c);
    }
    const double output = gainOf(values_[4]);
    left = float((wet[0] * mix + left * (1 - mix)) * output);
    right = float((wet[1] * mix + right * (1 - mix)) * output);
}
void StudioEffects::processMaximizer(float &left, float &right) noexcept
{
    // Lookahead peak limiting: the minimum required gain over the lookahead window,
    // released smoothly, then averaged over the window so the gain is already down
    // when the delayed peak arrives; no sample exceeds the ceiling.
    const double gain = gainOf(values_[0]), ceiling = gainOf(values_[1]);
    const float inL = float(left * gain), inR = float(right * gain);
    const double peak = std::max(std::abs(inL), std::abs(inR));
    const int window = int(required_.size());
    required_[size_t(cursor_ % window)] = float(peak > ceiling ? ceiling / peak : 1.);
    double minimum = 1;
    for (float r : required_)
        minimum = std::min(minimum, double(r));
    held_ = minimum < held_ ? minimum : held_ + (minimum - held_) * coefficient(values_[2]);
    boxcarSum_ += held_ - boxcar_[size_t(boxcarCursor_)];
    boxcar_[size_t(boxcarCursor_)] = held_;
    boxcarCursor_ = (boxcarCursor_ + 1) % lookahead_;
    if (boxcarCursor_ == 0) // Re-sum once per window so rounding never accumulates.
    {
        boxcarSum_ = 0;
        for (double value : boxcar_)
            boxcarSum_ += value;
    }
    const double smoothed = boxcarSum_ / lookahead_;
    const int slot = cursor_ % lookahead_;
    const float delayedL = delayL_[size_t(slot)], delayedR = delayR_[size_t(slot)];
    delayL_[size_t(slot)] = inL;
    delayR_[size_t(slot)] = inR;
    cursor_ = (cursor_ + 1) % (lookahead_ * window);
    reductionDb_ = float(decibels(smoothed));
    const float limit = float(ceiling);
    left = std::clamp(float(delayedL * smoothed), -limit, limit);
    right = std::clamp(float(delayedR * smoothed), -limit, limit);
}
} // namespace vibera::effects
