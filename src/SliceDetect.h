// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <optional>
#include <vector>

namespace vibera
{
// Onset (transient) detection for slicing: rises of the short-time energy envelope above
// a slow average. Returns slice starts as fractions of the file (always including 0), at
// least `minimumGap` seconds apart, at most `maximum` of them. Control thread only.
inline std::vector<double> detectSlices(const juce::File &file, double sensitivity = .5, int maximum = 16,
                                        double minimumGap = .08)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    std::vector<double> slices{0.};
    if (!reader || reader->lengthInSamples < 2048 || reader->sampleRate <= 0)
        return slices;
    const int frames = int(std::min<juce::int64>(reader->lengthInSamples, juce::int64(reader->sampleRate * 120)));
    juce::AudioBuffer<float> pcm(int(juce::jlimit(1u, 2u, reader->numChannels)), frames);
    reader->read(&pcm, 0, frames, 0, true, true);
    const int hop = 256;
    std::vector<double> energy;
    for (int start = 0; start + hop <= frames; start += hop)
    {
        double sum = 0;
        for (int c = 0; c < pcm.getNumChannels(); ++c)
            for (int i = 0; i < hop; ++i)
                sum += double(pcm.getSample(c, start + i)) * pcm.getSample(c, start + i);
        energy.push_back(sum / hop);
    }
    // Onset when the frame energy jumps well above the recent average (threshold falls as
    // sensitivity rises) and above an absolute floor.
    const double ratio = juce::jmap(juce::jlimit(0., 1., sensitivity), 8., 1.8);
    double average = energy.empty() ? 0 : energy[0];
    std::vector<std::pair<double, double>> onsets; // (strength, fraction)
    int last = -1000000;
    const int gap = int(minimumGap * reader->sampleRate / hop);
    for (size_t k = 1; k < energy.size(); ++k)
    {
        const double rise = energy[k] / (average + 1.e-9);
        if (rise > ratio && energy[k] > 1.e-5 && int(k) - last >= gap)
        {
            onsets.push_back({rise, double(k * hop) / frames});
            last = int(k);
        }
        average = .9 * average + .1 * energy[k];
    }
    // Keep the strongest onsets when there are too many.
    std::sort(onsets.begin(), onsets.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    for (const auto &[strength, fraction] : onsets)
        if (int(slices.size()) < maximum && fraction > minimumGap * reader->sampleRate / frames)
            slices.push_back(fraction);
    std::sort(slices.begin(), slices.end());
    return slices;
}
// First transient inside a window of a file: the first 128-frame hop whose energy reaches
// 10% (-10 dB) of the window's loudest hop, refined to the first sample above a quarter of
// that hop's peak. Seconds from the window start; empty for a silent or unreadable window.
inline std::optional<double> firstTransientSeconds(const juce::File &file, double from, double length)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader || reader->sampleRate <= 0 || length <= 0)
        return {};
    const auto start = juce::int64(std::max(0., from) * reader->sampleRate);
    const int frames = int(std::min<juce::int64>(juce::int64(std::min(length, 600.) * reader->sampleRate),
                                                 reader->lengthInSamples - start));
    if (frames < 256)
        return {};
    juce::AudioBuffer<float> pcm(int(juce::jlimit(1u, 2u, reader->numChannels)), frames);
    reader->read(&pcm, 0, frames, start, true, true);
    const int hop = 128;
    std::vector<double> energy;
    for (int i = 0; i + hop <= frames; i += hop)
    {
        double sum = 0;
        for (int c = 0; c < pcm.getNumChannels(); ++c)
            for (int k = 0; k < hop; ++k)
                sum += double(pcm.getSample(c, i + k)) * pcm.getSample(c, i + k);
        energy.push_back(sum / hop);
    }
    const double loudest = energy.empty() ? 0 : *std::max_element(energy.begin(), energy.end());
    if (loudest < 1.e-8)
        return {};
    for (size_t k = 0; k < energy.size(); ++k)
        if (energy[k] >= .1 * loudest)
        {
            const int first = int(k) * hop;
            float peak = 0;
            for (int c = 0; c < pcm.getNumChannels(); ++c)
                peak = std::max(peak, pcm.getMagnitude(c, first, hop));
            for (int i = std::max(0, first - hop); i < first + hop; ++i)
                for (int c = 0; c < pcm.getNumChannels(); ++c)
                    if (std::abs(pcm.getSample(c, i)) >= .25f * peak)
                        return double(i) / reader->sampleRate;
            return double(first) / reader->sampleRate;
        }
    return {};
}
} // namespace vibera
