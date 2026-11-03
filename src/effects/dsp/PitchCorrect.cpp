// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PitchCorrect.h"

#include <algorithm>
#include <cmath>

namespace vibera::effects
{
namespace
{
constexpr int analysisSize = 1024; // Decimated samples kept (about 43 ms at 48 kHz input).
constexpr int windowSize = 512;    // YIN integration window (decimated).
constexpr int hop = 256;           // Input samples between analyses.
struct Scale
{
    const char *name;
    int tonicCents;
    std::array<int, 12> cents;
    int count;
};
// Degrees in cents above the tonic; maqamat use quarter tones (24-tone equal temperament).
constexpr Scale scales[PitchCorrect::scaleCount] = {
    {"Chromatic", 0, {0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100}, 12},
    {"Major", 0, {0, 200, 400, 500, 700, 900, 1100}, 7},
    {"Minor", 0, {0, 200, 300, 500, 700, 800, 1000}, 7},
    {"Harmonic minor", 0, {0, 200, 300, 500, 700, 800, 1100}, 7},
    {"Minor pentatonic", 0, {0, 300, 500, 700, 1000}, 5},
    {"Rast", 0, {0, 200, 350, 500, 700, 900, 1050}, 7},
    {"Bayati", 0, {0, 150, 300, 500, 700, 800, 1000}, 7},
    {"Saba", 0, {0, 150, 300, 400, 700, 800, 1000}, 7},
    {"Sikah", -50, {0, 150, 350, 550, 700, 850, 1050}, 7},
    {"Huzam", -50, {0, 150, 450, 550, 700, 850, 1050}, 7},
    {"Hijaz", 0, {0, 100, 400, 500, 700, 800, 1000}, 7},
    {"Nahawand", 0, {0, 200, 300, 500, 700, 800, 1100}, 7},
    {"Kurd", 0, {0, 100, 300, 500, 700, 800, 1000}, 7},
    {"Nawa Athar", 0, {0, 200, 300, 600, 700, 800, 1100}, 7},
    {"Jiharkah", 0, {0, 200, 400, 500, 700, 900, 1050}, 7}};
} // namespace

const char *PitchCorrect::scaleName(int index) noexcept
{
    return index >= 0 && index < scaleCount ? scales[index].name : "";
}

void PitchCorrect::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    analysis_.assign(analysisSize, 0.f);
    difference_.assign(analysisSize / 2, 0.f);
    window_ = std::max(64, int(sampleRate * .02));
    lineL_.assign(size_t(window_ * 2 + 4), 0.f);
    lineR_.assign(size_t(window_ * 2 + 4), 0.f);
    prepared_ = true;
    reset();
}

void PitchCorrect::reset() noexcept
{
    std::fill(analysis_.begin(), analysis_.end(), 0.f);
    std::fill(lineL_.begin(), lineL_.end(), 0.f);
    std::fill(lineR_.begin(), lineR_.end(), 0.f);
    analysisWrite_ = hopCounter_ = write_ = 0;
    decimateCarry_ = 0;
    decimatePhase_ = false;
    detected_ = target_ = 0;
    ratio_ = ratioTarget_ = 1;
    phase_ = 0;
}

void PitchCorrect::setParameters(const Values &values) noexcept
{
    key_ = std::clamp(int(std::lround(values[0])), 0, 11);
    scale_ = std::clamp(int(std::lround(values[1])), 0, scaleCount - 1);
    retuneSeconds_ = std::max(0., double(values[2]) / 1000.);
    amount_ = std::clamp(double(values[3]), 0., 1.);
    mix_ = std::clamp(double(values[4]), 0., 1.);
}

double PitchCorrect::nearestDegree(double hz) const noexcept
{
    const auto &s = scales[scale_];
    // Cents of the input relative to C0-relative tonic, then the closest degree in any octave.
    const double cents = 1200. * std::log2(hz / 440.) + 5700.; // Cents above C0 (A4 = 5700 + ...).
    const double tonic = key_ * 100. + s.tonicCents;
    double best = cents, distance = 1.e9;
    const int octave = int(std::floor((cents - tonic) / 1200.));
    for (int o = octave - 1; o <= octave + 1; ++o)
        for (int i = 0; i < s.count; ++i)
        {
            const double candidate = tonic + o * 1200. + s.cents[size_t(i)];
            if (std::abs(candidate - cents) < distance)
            {
                distance = std::abs(candidate - cents);
                best = candidate;
            }
        }
    return 440. * std::pow(2., (best - 5700.) / 1200.);
}

// YIN (cumulative mean normalized difference) over the newest decimated window.
void PitchCorrect::analyse() noexcept
{
    const double rate = sampleRate_ / 2;
    const int minLag = std::max(2, int(rate / 1000.)), maxLag = std::min(analysisSize / 2 - 1, int(rate / 70.));
    const int start = analysisWrite_ - windowSize - maxLag;
    auto at = [&](int i) { return analysis_[size_t((i % analysisSize + analysisSize) % analysisSize)]; };
    double energy = 0;
    for (int i = 0; i < windowSize; ++i)
        energy += double(at(start + maxLag + i)) * at(start + maxLag + i);
    if (energy / windowSize < 1.e-6)
    {
        detected_ = 0;
        ratioTarget_ = 1;
        return;
    }
    double running = 0;
    int chosen = -1;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0;
        for (int i = 0; i < windowSize; ++i)
        {
            const double d = double(at(start + maxLag + i)) - at(start + maxLag + i - lag);
            sum += d * d;
        }
        running += sum;
        const double normalized = running > 0 ? sum * lag / running : 1;
        difference_[size_t(lag)] = float(normalized);
        if (lag >= minLag && normalized < .15 && chosen < 0)
            chosen = lag;
        if (chosen > 0 && lag > chosen && difference_[size_t(lag)] > difference_[size_t(lag - 1)])
            break; // Walk to the local minimum after the threshold crossing.
        if (chosen > 0 && normalized < difference_[size_t(chosen)])
            chosen = lag;
    }
    if (chosen < 0)
    {
        detected_ = 0;
        ratioTarget_ = 1;
        return;
    }
    // Parabolic interpolation around the minimum.
    double lag = chosen;
    if (chosen > 1 && chosen < maxLag)
    {
        const double a = difference_[size_t(chosen - 1)], b = difference_[size_t(chosen)],
                     c = difference_[size_t(chosen + 1)];
        const double denominator = a - 2 * b + c;
        if (std::abs(denominator) > 1.e-12)
            lag += .5 * (a - c) / denominator;
    }
    detected_ = rate / lag;
    target_ = nearestDegree(detected_);
    ratioTarget_ = std::pow(target_ / detected_, amount_);
}

void PitchCorrect::process(float &left, float &right) noexcept
{
    if (!prepared_)
        return;
    // Decimate the mono sum by two (average of each pair) into the analysis ring.
    const float mono = .5f * (left + right);
    if (decimatePhase_)
    {
        analysis_[size_t(analysisWrite_ % analysisSize)] = .5f * (decimateCarry_ + mono);
        ++analysisWrite_;
    }
    else
        decimateCarry_ = mono;
    decimatePhase_ = !decimatePhase_;
    if (++hopCounter_ >= hop)
    {
        hopCounter_ = 0;
        analyse();
    }
    // Glide toward the target ratio over the retune time (instant when zero).
    const double coefficient = retuneSeconds_ <= 0 ? 1. : 1. - std::exp(-1. / (retuneSeconds_ * sampleRate_));
    ratio_ += (ratioTarget_ - ratio_) * coefficient;
    // Two read heads half a window apart; each delay sweeps at (1 - ratio) and wraps,
    // crossfaded with a raised-cosine so the wrap is never heard.
    const int size = int(lineL_.size());
    lineL_[size_t(write_)] = left;
    lineR_[size_t(write_)] = right;
    phase_ += (1. - ratio_) / window_;
    phase_ -= std::floor(phase_);
    float outL = 0, outR = 0;
    for (int head = 0; head < 2; ++head)
    {
        double p = phase_ + head * .5;
        p -= std::floor(p);
        const double delay = 2. + p * window_;
        const double gain = .5 - .5 * std::cos(2. * 3.141592653589793 * p);
        double read = write_ - delay;
        while (read < 0)
            read += size;
        const int i0 = int(read), i1 = (i0 + 1) % size;
        const float frac = float(read - i0);
        outL += float(gain) * (lineL_[size_t(i0)] + frac * (lineL_[size_t(i1)] - lineL_[size_t(i0)]));
        outR += float(gain) * (lineR_[size_t(i0)] + frac * (lineR_[size_t(i1)] - lineR_[size_t(i0)]));
    }
    write_ = (write_ + 1) % size;
    left = float(mix_) * outL + float(1 - mix_) * left;
    right = float(mix_) * outR + float(1 - mix_) * right;
}
} // namespace vibera::effects
