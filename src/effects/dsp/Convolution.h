// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "EffectsTypes.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

namespace vibera::effects
{
// Convolution reverb: the input convolved with one of four built-in stereo impulse responses
// (Room, Studio, Hall, Plate), generated here from decaying, darkening noise with early
// reflections (original material, no recorded IRs). Uniform partitioned FFT convolution:
// 512-sample blocks, so the wet path is 512 samples late; the dry path is delayed to match
// and the delay is reported as latency. All IR spectra are built in prepare(); switching the
// space, pre-delay, tone, width or mix in the audio callback only selects or scales.
// Space 4 ("File") uses an impulse loaded on the message thread (setUserImpulse): its spectra
// are built there and published atomically; replaced ones stay alive until release/prepare.
class ConvolutionReverb
{
  public:
    static constexpr int block = 512, fftSize = 2 * block, bins = block + 1, spaces = 4;
    static constexpr double rt60[spaces]{.7, 1.3, 2.4, 1.9};        // Seconds.
    static constexpr double brightness[spaces]{.55, .7, .5, .95};   // High-frequency survival.
    static constexpr double earlyMs[spaces]{9, 15, 28, 0};          // First reflection (Plate: none).
    static constexpr const char *names[spaces + 1]{"Room", "Studio", "Hall", "Plate", "File"};
    static constexpr double maximumSeconds = 2.6;

    int latencySamples() const noexcept { return active ? block : 0; }
    // Releases the IRs (other effects of an Engine never pay for them).
    void release()
    {
        active = false;
        spectra = {};
        history = {};
        twiddles = {};
        bitReverse = {};
        predelayLine = {};
        user.store(nullptr, std::memory_order_release);
        userOwned.clear();
    }
    // Message thread only: `ir` is stereo at the prepared rate (longer impulses are cut to the
    // built-in maximum); its energy is normalised like the built-ins.
    bool setUserImpulse(std::array<std::vector<float>, 2> ir)
    {
        if (!active || ir[0].empty() || ir[1].size() != ir[0].size())
            return false;
        const auto limit = std::min(ir[0].size(), size_t(maxPartitions) * block);
        for (auto &channel : ir)
            channel.resize(limit);
        normalise(ir);
        auto made = std::make_unique<UserImpulse>();
        made->partitions = int((limit + block - 1) / block);
        for (int c = 0; c < 2; ++c)
            made->spectra[size_t(c)] = transform(ir[size_t(c)], made->partitions);
        user.store(made.get(), std::memory_order_release);
        userOwned.push_back(std::move(made));
        return true;
    }
    void clearUserImpulse() noexcept { user.store(nullptr, std::memory_order_release); }
    bool hasUserImpulse() const noexcept { return user.load(std::memory_order_acquire) != nullptr; }
    void prepare(double sampleRate)
    {
        rate = sampleRate;
        active = true;
        twiddles.resize(fftSize / 2);
        for (int k = 0; k < fftSize / 2; ++k)
            twiddles[size_t(k)] = std::polar(1.f, float(-2 * std::numbers::pi * k / fftSize));
        bitReverse.resize(fftSize);
        int bits = 0;
        while ((1 << bits) < fftSize)
            ++bits;
        for (int i = 0; i < fftSize; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                r |= ((i >> b) & 1) << (bits - 1 - b);
            bitReverse[size_t(i)] = r;
        }
        // IR spectra per space and channel: partitions[space] blocks of `bins` values.
        maxPartitions = 0;
        for (int s = 0; s < spaces; ++s)
        {
            const auto ir = impulse(s);
            const int count = int((ir[0].size() + block - 1) / block);
            partitions[size_t(s)] = count;
            maxPartitions = std::max(maxPartitions, count);
            for (int c = 0; c < 2; ++c)
                spectra[size_t(s)][size_t(c)] = transform(ir[size_t(c)], count);
        }
        for (auto &channel : history)
            channel.assign(size_t(maxPartitions) * bins, {});
        predelayLine.assign(size_t(rate * .2) + 2, {});
        user.store(nullptr, std::memory_order_release);
        userOwned.clear();
        reset();
    }
    void reset() noexcept
    {
        // Logical invalidation: only partitions written after the reset are used.
        filled = 0;
        newest = 0;
        position = 0;
        for (int c = 0; c < 2; ++c)
        {
            input[size_t(c)].fill(0);
            previousInput[size_t(c)].fill(0);
            wetOut[size_t(c)].fill(0);
            dryDelay[size_t(c)].fill(0);
        }
        std::fill(predelayLine.begin(), predelayLine.end(), std::array<float, 2>{});
        predelayWrite = 0;
        lowState[0] = lowState[1] = highState[0] = highState[1] = 0;
    }
    void setParameters(const Values &v) noexcept
    {
        space = std::clamp(int(std::lround(v[0])), 0, spaces);
        predelay = predelayLine.empty() ? 0
                                        : std::clamp(int(std::lround(double(v[1]) / 1000. * rate)), 0,
                                                     int(predelayLine.size()) - 2);
        lowCoefficient = float(std::exp(-2 * std::numbers::pi * std::clamp(double(v[2]), 20., 1000.) / rate));
        highCoefficient = float(1 - std::exp(-2 * std::numbers::pi * std::clamp(double(v[3]), 1000., rate * .45) / rate));
        width = std::clamp(v[4], 0.f, 1.f);
        mix = std::clamp(v[5], 0.f, 1.f);
    }
    void process(float &left, float &right) noexcept
    {
        if (!active)
            return;
        // Pre-delay into the convolution input.
        const int size = int(predelayLine.size());
        predelayLine[size_t(predelayWrite)] = {left, right};
        const auto delayed = predelayLine[size_t((predelayWrite - predelay + size) % size)];
        predelayWrite = (predelayWrite + 1) % size;
        const float dryIn[2]{left, right};
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            input[size_t(c)][size_t(position)] = delayed[size_t(c)];
            float wet = wetOut[size_t(c)][size_t(position)];
            // Tone on the wet path: one-pole high-pass (low cut) then low-pass (high cut).
            const float highPassed = wet - lowState[c];
            lowState[c] = wet + lowCoefficient * (lowState[c] - wet);
            highState[c] += (highPassed - highState[c]) * highCoefficient;
            wet = highState[c];
            const float dry = dryDelay[size_t(c)][size_t(position)];
            dryDelay[size_t(c)][size_t(position)] = dryIn[c];
            out[c] = wet;
            dryOut[c] = dry;
        }
        // Width on the wet image, then the mix against the latency-aligned dry signal.
        const float mid = .5f * (out[0] + out[1]), side = .5f * (out[0] - out[1]) * width;
        left = dryOut[0] + ((mid + side) - dryOut[0]) * mix;
        right = dryOut[1] + ((mid - side) - dryOut[1]) * mix;
        if (++position == block)
        {
            position = 0;
            convolveBlock();
        }
    }

  private:
    struct UserImpulse
    {
        std::array<std::vector<std::complex<float>>, 2> spectra;
        int partitions = 0;
    };
    std::vector<std::complex<float>> transform(const std::vector<float> &ir, int count) const
    {
        std::vector<std::complex<float>> target(size_t(count) * bins);
        std::vector<std::complex<float>> frame(fftSize);
        for (int p = 0; p < count; ++p)
        {
            std::fill(frame.begin(), frame.end(), std::complex<float>{});
            for (int i = 0; i < block; ++i)
                if (const auto at = size_t(p) * block + size_t(i); at < ir.size())
                    frame[size_t(i)] = ir[at];
            fft(frame.data(), false);
            std::copy(frame.begin(), frame.begin() + bins, target.begin() + std::ptrdiff_t(p) * bins);
        }
        return target;
    }
    static void normalise(std::array<std::vector<float>, 2> &ir)
    {
        for (auto &channel : ir)
        {
            double energy = 0;
            for (float x : channel)
                energy += double(x) * x;
            const float scale = float(.6 / std::sqrt(std::max(1e-12, energy))); // Comparable loudness.
            for (auto &x : channel)
                x *= scale;
        }
    }
    void fft(std::complex<float> *data, bool inverse) const noexcept
    {
        for (int i = 0; i < fftSize; ++i)
            if (const int j = bitReverse[size_t(i)]; j > i)
                std::swap(data[i], data[j]);
        for (int length = 2; length <= fftSize; length <<= 1)
        {
            const int step = fftSize / length;
            for (int start = 0; start < fftSize; start += length)
                for (int k = 0; k < length / 2; ++k)
                {
                    auto w = twiddles[size_t(k * step)];
                    if (inverse)
                        w = std::conj(w);
                    const auto even = data[start + k], odd = data[start + k + length / 2] * w;
                    data[start + k] = even + odd;
                    data[start + k + length / 2] = even - odd;
                }
        }
        if (inverse)
            for (int i = 0; i < fftSize; ++i)
                data[i] /= float(fftSize);
    }
    // Overlap-save: spectrum of [previous block, this block] goes into the delay line; the
    // output block is the second half of the inverse transform of the summed products.
    void convolveBlock() noexcept
    {
        newest = (newest + 1) % maxPartitions;
        filled = std::min(filled + 1, maxPartitions);
        // Space 4 uses the loaded impulse (Studio until one is loaded).
        const auto *loaded = space == spaces ? user.load(std::memory_order_acquire) : nullptr;
        const int chosen = space == spaces ? 1 : space;
        const int count = std::min(filled, loaded ? loaded->partitions : partitions[size_t(chosen)]);
        for (int c = 0; c < 2; ++c)
        {
            auto *frame = scratch.data();
            for (int i = 0; i < block; ++i)
            {
                frame[i] = previousInput[size_t(c)][size_t(i)];
                frame[block + i] = input[size_t(c)][size_t(i)];
            }
            previousInput[size_t(c)] = input[size_t(c)];
            fft(frame, false);
            auto *slot = history[size_t(c)].data() + std::ptrdiff_t(newest) * bins;
            std::copy(frame, frame + bins, slot);
            auto *sum = accumulator.data();
            std::fill(sum, sum + bins, std::complex<float>{});
            const auto *ir = loaded ? loaded->spectra[size_t(c)].data() : spectra[size_t(chosen)][size_t(c)].data();
            for (int p = 0; p < count; ++p)
            {
                const auto *x = history[size_t(c)].data() +
                                std::ptrdiff_t((newest - p + maxPartitions) % maxPartitions) * bins;
                const auto *h = ir + std::ptrdiff_t(p) * bins;
                for (int k = 0; k < bins; ++k)
                    sum[k] += x[k] * h[k];
            }
            // Real signals: rebuild the upper half by conjugate symmetry.
            for (int k = 0; k < bins; ++k)
                frame[k] = sum[k];
            for (int k = 1; k < block; ++k)
                frame[fftSize - k] = std::conj(sum[k]);
            fft(frame, true);
            for (int i = 0; i < block; ++i)
                wetOut[size_t(c)][size_t(i)] = std::isfinite(frame[block + i].real()) ? frame[block + i].real() : 0.f;
        }
    }
    std::array<std::vector<float>, 2> impulse(int s) const
    {
        const int length = int(rate * std::min(maximumSeconds, rt60[s] * 1.1));
        std::array<std::vector<float>, 2> ir{std::vector<float>(size_t(length)), std::vector<float>(size_t(length))};
        std::uint32_t seed = 0x5eed1234u + std::uint32_t(s) * 977u;
        const auto noise = [&seed]
        {
            seed = seed * 1664525u + 1013904223u;
            return float(int(seed >> 8) - (1 << 23)) / float(1 << 23);
        };
        const double decay = 6.907755 / (rt60[s] * rate); // -60 dB at rt60.
        for (int c = 0; c < 2; ++c)
        {
            float low = 0;
            for (int i = 0; i < length; ++i)
            {
                // Darkening tail: a one-pole low-pass that closes as the tail ages.
                const double age = double(i) / length;
                const float coefficient = float(std::clamp(brightness[s] * (1 - .8 * age), .02, .98));
                low += (noise() - low) * coefficient;
                const double fadeIn = std::min(1., i / (rate * .004));
                ir[size_t(c)][size_t(i)] = float(low * std::exp(-decay * i) * fadeIn);
            }
            // Early reflections: a few decaying taps, different per channel.
            if (earlyMs[s] > 0)
                for (int tap = 0; tap < 6; ++tap)
                {
                    const int at = int(rate * (earlyMs[s] + tap * earlyMs[s] * (.55 + .13 * c)) / 1000.);
                    if (at < length)
                        ir[size_t(c)][size_t(at)] += float((tap % 2 ? -.6 : .7) * std::pow(.72, tap));
                }
        }
        normalise(ir);
        return ir;
    }

    double rate = 48000;
    bool active = false;
    int space = 1, predelay = 0, filled = 0, newest = 0, position = 0, maxPartitions = 1, predelayWrite = 0;
    std::array<int, spaces> partitions{};
    float lowCoefficient = .99f, highCoefficient = .5f, width = 1, mix = .3f;
    float lowState[2]{}, highState[2]{}, dryOut[2]{};
    std::array<std::array<std::vector<std::complex<float>>, 2>, spaces> spectra{};
    std::array<std::vector<std::complex<float>>, 2> history{};
    std::array<std::array<float, block>, 2> input{}, previousInput{}, wetOut{}, dryDelay{};
    std::array<std::complex<float>, fftSize> scratch{};
    std::array<std::complex<float>, bins> accumulator{};
    std::vector<std::complex<float>> twiddles;
    std::vector<int> bitReverse;
    std::vector<std::array<float, 2>> predelayLine;
    std::atomic<const UserImpulse *> user{nullptr};
    std::vector<std::unique_ptr<UserImpulse>> userOwned; // Message thread; never freed in the callback.
};
} // namespace vibera::effects
