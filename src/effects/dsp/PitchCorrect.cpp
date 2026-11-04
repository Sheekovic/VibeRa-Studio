// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PitchCorrect.h"
#include <cstring>
#include <signalsmith-stretch.h>

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

struct PitchCorrect::Stretch
{
    signalsmith::stretch::SignalsmithStretch<float> engine;
};
PitchCorrect::PitchCorrect() = default;
PitchCorrect::~PitchCorrect() = default;
PitchCorrect::PitchCorrect(PitchCorrect &&) noexcept = default;
PitchCorrect &PitchCorrect::operator=(PitchCorrect &&) noexcept = default;
void PitchCorrect::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    analysis_.assign(analysisSize, 0.f);
    difference_.assign(analysisSize / 2, 0.f);
    raw_.assign(analysisSize / 2, 0.f);
    if (!stretch_)
        stretch_ = std::make_unique<Stretch>();
    // About 50 ms analysis blocks: long enough for low voices, short enough to track.
    const int blockSamples = std::max(512, int(sampleRate * .1));
    stretch_->engine.configure(2, blockSamples, blockSamples / 4);
    latency_ = block + stretch_->engine.inputLatency() + stretch_->engine.outputLatency();
    dryL_.assign(size_t(latency_), 0.f);
    dryR_.assign(size_t(latency_), 0.f);
    ratioHistory_.assign(size_t(std::max(1, stretch_->engine.inputLatency() / block) + 1), 1.);
    prepared_ = true;
    reset();
}
void PitchCorrect::reset() noexcept
{
    std::fill(analysis_.begin(), analysis_.end(), 0.f);
    std::fill(dryL_.begin(), dryL_.end(), 0.f);
    std::fill(dryR_.begin(), dryR_.end(), 0.f);
    std::fill(ratioHistory_.begin(), ratioHistory_.end(), 1.);
    inL_.fill(0);
    inR_.fill(0);
    outL_.fill(0);
    outR_.fill(0);
    if (stretch_)
        stretch_->engine.reset();
    analysisWrite_ = hopCounter_ = fill_ = dryWrite_ = ratioWrite_ = 0;
    decimateCarry_ = 0;
    decimatePhase_ = false;
    detected_ = target_ = lastTarget_ = heldSeconds_ = 0;
    ratio_ = ratioTarget_ = 1;
}
void PitchCorrect::setParameters(const Values &values) noexcept
{
    key_ = std::clamp(int(std::lround(values[0])), 0, 11);
    scale_ = std::clamp(int(std::lround(values[1])), 0, scaleCount - 1);
    retuneSeconds_ = std::max(0., double(values[2]) / 1000.);
    amount_ = std::clamp(double(values[3]), 0., 1.);
    mix_ = std::clamp(double(values[4]), 0., 1.);
    humanize_ = std::clamp(double(values[5]), 0., 1.);
    flexCents_ = std::clamp(double(values[6]), 0., 100.);
    formants_ = values[7] >= .5f;
    transpose_ = std::pow(2., std::clamp(double(std::lround(values[8])), -12., 12.) / 12.);
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
        raw_[size_t(lag)] = float(sum);
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
    // Parabolic interpolation on the raw difference (the normalized curve is skewed by its
    // running mean and biases the period by several cents).
    double lag = chosen;
    if (chosen > 1 && chosen < maxLag)
    {
        // The raw minimum can sit one lag beside the normalized one; refine around it.
        int centre = chosen;
        if (raw_[size_t(centre - 1)] < raw_[size_t(centre)])
            --centre;
        else if (centre + 1 < maxLag && raw_[size_t(centre + 1)] < raw_[size_t(centre)])
            ++centre;
        if (centre > 1 && centre < maxLag)
        {
            const double a = raw_[size_t(centre - 1)], b = raw_[size_t(centre)], c = raw_[size_t(centre + 1)];
            const double denominator = a - 2 * b + c;
            lag = centre;
            if (std::abs(denominator) > 1.e-12)
                lag += .5 * (a - c) / denominator;
        }
    }
    detected_ = rate / lag;
    target_ = nearestDegree(detected_);
    // Flex: full correction within the window around a degree, fading to none at twice it,
    // so slides and ornaments between notes pass untouched.
    double strength = amount_;
    if (flexCents_ > 0)
    {
        const double deviation = std::abs(1200. * std::log2(detected_ / target_));
        strength *= std::clamp(2. - deviation / flexCents_, 0., 1.);
    }
    if (std::abs(target_ - lastTarget_) > 1.e-6)
        heldSeconds_ = 0;
    lastTarget_ = target_;
    ratioTarget_ = std::pow(target_ / detected_, strength);
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
    // Glide toward the target over the retune time; Humanize stretches it up to 5x as a
    // note is held, keeping long notes natural while fast changes still snap.
    heldSeconds_ += 1. / sampleRate_;
    const double retune = retuneSeconds_ * (1. + humanize_ * 4. * std::min(1., heldSeconds_ / .4));
    const double coefficient = retune <= 0 ? 1. : 1. - std::exp(-1. / (retune * sampleRate_));
    ratio_ += (ratioTarget_ - ratio_) * coefficient;
    // Aligned dry path for Mix: the slot about to be overwritten is latency_ samples old.
    const float dryOutL = dryL_[size_t(dryWrite_)], dryOutR = dryR_[size_t(dryWrite_)];
    dryL_[size_t(dryWrite_)] = left;
    dryR_[size_t(dryWrite_)] = right;
    dryWrite_ = (dryWrite_ + 1) % int(dryL_.size());
    // Block FIFO through the stretcher.
    inL_[size_t(fill_)] = left;
    inR_[size_t(fill_)] = right;
    const float wetL = outL_[size_t(fill_)], wetR = outR_[size_t(fill_)];
    if (++fill_ == block)
    {
        fill_ = 0;
        // The stretcher is working on audio about inputLatency old: use the ratio detected then.
        ratioHistory_[size_t(ratioWrite_)] = ratio_;
        ratioWrite_ = (ratioWrite_ + 1) % int(ratioHistory_.size());
        const double ratio = ratioHistory_[size_t(ratioWrite_)] * transpose_;
        auto &engine = stretch_->engine;
        engine.setTransposeFactor(float(ratio));
        engine.setFormantFactor(1.f, formants_);
        float *inputs[2]{inL_.data(), inR_.data()}, *outputs[2]{outL_.data(), outR_.data()};
        engine.process(inputs, block, outputs, block);
    }
    left = float(mix_) * wetL + float(1 - mix_) * dryOutL;
    right = float(mix_) * wetR + float(1 - mix_) * dryOutR;
}
} // namespace vibera::effects
