// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
// Loudness measurement after ITU-R BS.1770-4 / EBU R128 (independent implementation):
// K-weighting, gated integrated loudness, momentary (400 ms) and short-term (3 s) maxima,
// loudness range (EBU Tech 3342) and true peak by 4x oversampling. Offline, not real-time.
#include <algorithm>
#include <cmath>
#include <vector>

namespace vibera::loudness
{
inline constexpr double pi = 3.14159265358979323846;
struct Result
{
    double integratedLufs = -INFINITY, momentaryMaxLufs = -INFINITY, shortTermMaxLufs = -INFINITY;
    double rangeLu = 0, truePeakDbtp = -INFINITY, samplePeakDbfs = -INFINITY;
    double seconds = 0;
};

namespace detail
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process(double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
// BS.1770 pre-filter (high shelf) and RLB high-pass, derived for any sample rate.
inline Biquad shelf(double rate)
{
    const double gain = 3.99984385397, q = .7071752369554193, fc = 1681.9744509555319;
    const double k = std::tan(pi * fc / rate), vh = std::pow(10., gain / 20.), vb = std::pow(vh, .4996667741545416);
    const double a0 = 1. + k / q + k * k;
    Biquad f;
    f.b0 = (vh + vb * k / q + k * k) / a0;
    f.b1 = 2. * (k * k - vh) / a0;
    f.b2 = (vh - vb * k / q + k * k) / a0;
    f.a1 = 2. * (k * k - 1.) / a0;
    f.a2 = (1. - k / q + k * k) / a0;
    return f;
}
inline Biquad highPass(double rate)
{
    const double q = .5003270373253953, fc = 38.13547087613982;
    const double k = std::tan(pi * fc / rate), a0 = 1. + k / q + k * k;
    Biquad f;
    f.b0 = 1;
    f.b1 = -2;
    f.b2 = 1;
    f.a1 = 2. * (k * k - 1.) / a0;
    f.a2 = (1. - k / q + k * k) / a0;
    return f;
}
inline double toLufs(double meanSquare)
{
    return meanSquare > 0 ? -.691 + 10. * std::log10(meanSquare) : -INFINITY;
}
// Gated mean of block powers (absolute -70 LUFS, then relative gate below the first mean).
inline double gatedLoudness(const std::vector<double> &powers, double relativeLu)
{
    double sum = 0;
    int count = 0;
    for (auto p : powers)
        if (toLufs(p) > -70.)
            sum += p, ++count;
    if (!count)
        return -INFINITY;
    const double gate = toLufs(sum / count) - relativeLu;
    sum = 0;
    count = 0;
    for (auto p : powers)
        if (toLufs(p) > -70. && toLufs(p) > gate)
            sum += p, ++count;
    return count ? toLufs(sum / count) : -INFINITY;
}
} // namespace detail

// channels[c][i]: planar samples (1 or 2 channels; more channels count with weight 1).
inline Result measure(const std::vector<std::vector<float>> &channels, double rate)
{
    Result result;
    if (channels.empty() || rate <= 0 || channels[0].empty())
        return result;
    const size_t frames = channels[0].size();
    result.seconds = double(frames) / rate;
    // K-weighted squared signal summed over channels (L, R weight 1).
    std::vector<double> power(frames, 0.);
    double samplePeak = 0;
    for (const auto &channel : channels)
    {
        auto shelf = detail::shelf(rate), hp = detail::highPass(rate);
        for (size_t i = 0; i < frames && i < channel.size(); ++i)
        {
            const double x = std::isfinite(channel[i]) ? channel[i] : 0.;
            samplePeak = std::max(samplePeak, std::abs(x));
            const double y = hp.process(shelf.process(x));
            power[i] += y * y;
        }
    }
    // Prefix sums give any window's mean square in O(1).
    std::vector<double> prefix(frames + 1, 0.);
    for (size_t i = 0; i < frames; ++i)
        prefix[i + 1] = prefix[i] + power[i];
    auto windows = [&](double windowSeconds, double hopSeconds)
    {
        std::vector<double> out;
        const auto window = size_t(std::llround(windowSeconds * rate)), hop = size_t(std::llround(hopSeconds * rate));
        for (size_t start = 0; window && start + window <= frames; start += hop)
            out.push_back((prefix[start + window] - prefix[start]) / double(window));
        return out;
    };
    const auto momentary = windows(.4, .1), shortTerm = windows(3., .1);
    result.integratedLufs = detail::gatedLoudness(momentary, 10.);
    for (auto p : momentary)
        result.momentaryMaxLufs = std::max(result.momentaryMaxLufs, detail::toLufs(p));
    for (auto p : shortTerm)
        result.shortTermMaxLufs = std::max(result.shortTermMaxLufs, detail::toLufs(p));
    // Loudness range: short-term values passing absolute and -20 LU relative gates, P95 - P10.
    {
        double sum = 0;
        int count = 0;
        for (auto p : shortTerm)
            if (detail::toLufs(p) > -70.)
                sum += p, ++count;
        std::vector<double> gated;
        if (count)
        {
            const double gate = detail::toLufs(sum / count) - 20.;
            for (auto p : shortTerm)
                if (detail::toLufs(p) > -70. && detail::toLufs(p) > gate)
                    gated.push_back(detail::toLufs(p));
        }
        if (gated.size() > 1)
        {
            std::sort(gated.begin(), gated.end());
            auto at = [&](double q) { return gated[size_t(std::llround(q * double(gated.size() - 1)))]; };
            result.rangeLu = at(.95) - at(.10);
        }
    }
    // True peak: 4x polyphase windowed-sinc interpolation (48 taps per phase).
    constexpr int factor = 4, taps = 48;
    std::vector<double> kernel(size_t(factor * taps));
    for (int n = 0; n < factor * taps; ++n)
    {
        const double t = (double(n) - (factor * taps - 1) / 2.) / factor;
        const double sinc = std::abs(t) < 1e-12 ? 1. : std::sin(pi * t) / (pi * t);
        const double window = .5 - .5 * std::cos(2. * pi * (n + .5) / (factor * taps));
        kernel[size_t(n)] = sinc * window;
    }
    double truePeak = samplePeak;
    for (const auto &channel : channels)
        for (size_t i = 0; i + taps < channel.size(); ++i)
            for (int phase = 0; phase < factor; ++phase)
            {
                double y = 0;
                for (int k = 0; k < taps; ++k)
                    y += kernel[size_t(k * factor + phase)] * channel[i + size_t(taps - 1 - k)];
                truePeak = std::max(truePeak, std::abs(y));
            }
    result.samplePeakDbfs = samplePeak > 0 ? 20. * std::log10(samplePeak) : -INFINITY;
    result.truePeakDbtp = truePeak > 0 ? 20. * std::log10(truePeak) : -INFINITY;
    return result;
}
} // namespace vibera::loudness
