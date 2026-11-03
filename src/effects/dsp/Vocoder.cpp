// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Vocoder.h"

#include <algorithm>
#include <cmath>

namespace vibera::effects
{
namespace
{
constexpr double pi = 3.141592653589793;
}

void Vocoder::BandPass::design(double frequency, double q, double rate) noexcept
{
    // RBJ constant-peak-gain band-pass.
    const double w = 2 * pi * frequency / rate, alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
    b0 = alpha / a0;
    b2 = -alpha / a0;
    a1 = -2 * std::cos(w) / a0;
    a2 = (1 - alpha) / a0;
}

double Vocoder::BandPass::run(double x) noexcept
{
    const double y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
}

void Vocoder::prepare(double sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    const double low = 120, high = std::min(8000., sampleRate * .45);
    for (int b = 0; b < bands; ++b)
    {
        const double frequency = low * std::pow(high / low, double(b) / (bands - 1));
        analysis_[size_t(b)].design(frequency, 6, sampleRate);
        synthesis_[size_t(b)].design(frequency, 6, sampleRate);
    }
    attack_ = 1 - std::exp(-1. / (.003 * sampleRate));
    reset();
}

void Vocoder::reset() noexcept
{
    for (auto &f : analysis_)
        f.reset();
    for (auto &f : synthesis_)
        f.reset();
    envelope_.fill(0);
    phase_ = 0;
}

void Vocoder::setParameters(const Values &values) noexcept
{
    const double note = std::clamp(double(values[0]), 24., 96.);
    increment_ = 440. * std::pow(2., (note - 69) / 12.) / sampleRate_;
    release_ = 1 - std::exp(-1. / (std::max(1., double(values[1])) * .001 * sampleRate_));
    noise_ = std::clamp(double(values[2]), 0., 1.);
    mix_ = std::clamp(double(values[3]), 0., 1.);
    gain_ = std::pow(10., double(values[4]) / 20.);
}

void Vocoder::process(float &left, float &right) noexcept
{
    const double modulator = .5 * (double(left) + right);
    // Band-limited sawtooth (PolyBLEP) plus white noise as the carrier.
    phase_ += increment_;
    if (phase_ >= 1)
        phase_ -= 1;
    double saw = 2 * phase_ - 1;
    auto blep = [&](double t)
    {
        if (t < increment_)
        {
            t /= increment_;
            return t + t - t * t - 1;
        }
        if (t > 1 - increment_)
        {
            t = (t - 1) / increment_;
            return t * t + t + t + 1;
        }
        return 0.;
    };
    saw -= blep(phase_);
    random_ = random_ * 1664525u + 1013904223u;
    const double white = double(random_) / 2147483648. - 1;
    const double carrier = (1 - noise_) * saw + noise_ * white;
    double out = 0;
    for (int b = 0; b < bands; ++b)
    {
        const double level = std::abs(analysis_[size_t(b)].run(modulator));
        auto &e = envelope_[size_t(b)];
        e += float((level > e ? attack_ : release_) * (level - e));
        out += synthesis_[size_t(b)].run(carrier) * e;
    }
    out *= gain_ * 4; // Band-pass peaks and envelope averaging lose about 12 dB.
    left = float(mix_ * out + (1 - mix_) * left);
    right = float(mix_ * out + (1 - mix_) * right);
}
} // namespace vibera::effects
