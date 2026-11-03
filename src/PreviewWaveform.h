// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Sampler.h"

namespace vibera
{
// Prepared by the decoder, copied on the control thread, never read by DSP.
struct PreviewWaveform
{
    struct Peak
    {
        float low = 0, high = 0;
    };
    std::array<Peak, 512> peaks{};
    juce::String name;
    double seconds = 0;
    static PreviewWaveform prepare(const Sample &sample, const juce::String &source)
    {
        PreviewWaveform result;
        result.name = source.startsWith("builtin:") ? source.substring(8) : juce::File(source).getFileName();
        const int frames = sample.data.getNumSamples();
        if (frames < 2 || !std::isfinite(sample.sampleRate) || sample.sampleRate <= 0)
            return result;
        result.seconds = frames / sample.sampleRate;
        for (size_t bin = 0; bin < result.peaks.size(); ++bin)
        {
            const int first = int(bin * size_t(frames) / result.peaks.size());
            const int end =
                std::min(frames, std::max(first + 1, int((bin + 1) * size_t(frames) / result.peaks.size())));
            for (int channel = 0; channel < std::min(2, sample.data.getNumChannels()); ++channel)
            {
                const auto *data = sample.data.getReadPointer(channel);
                for (int frame = first; frame < end; ++frame)
                    if (std::isfinite(data[frame]))
                    {
                        result.peaks[bin].low = std::min(result.peaks[bin].low, data[frame]);
                        result.peaks[bin].high = std::max(result.peaks[bin].high, data[frame]);
                    }
            }
        }
        return result;
    }
};
} // namespace vibera
