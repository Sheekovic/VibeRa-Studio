// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AudioStretch.h"
#include <signalsmith-stretch.h>

namespace vibera
{
juce::Result stretchAudioFile(const juce::File &source, double offsetSeconds, double durationSeconds, double ratio,
                              const juce::File &target)
{
    if (!(ratio >= .25 && ratio <= 4) || !(durationSeconds > 0) || offsetSeconds < 0)
        return juce::Result::fail("Stretch ratio is 0.25..4 for a positive source window");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (!reader || reader->sampleRate <= 0)
        return juce::Result::fail("Cannot read " + source.getFileName());
    const auto rate = reader->sampleRate;
    const int channels = int(juce::jlimit(1u, 2u, reader->numChannels));
    const auto start = juce::int64(offsetSeconds * rate);
    const int inFrames = int(std::min<juce::int64>(juce::int64(durationSeconds * rate), reader->lengthInSamples - start));
    if (inFrames < 4096)
        return juce::Result::fail("The clip is too short to stretch (under 0.1 s)");
    const int outFrames = int(std::round(inFrames * ratio));
    juce::AudioBuffer<float> input(channels, inFrames), output(channels, outFrames);
    reader->read(&input, 0, inFrames, start, true, channels > 1);
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault(channels, float(rate));
    auto in = std::vector<float *>(static_cast<size_t>(channels)), out = in;
    for (int c = 0; c < channels; ++c)
    {
        in[size_t(c)] = input.getWritePointer(c);
        out[size_t(c)] = output.getWritePointer(c);
    }
    if (!stretch.exact(in, inFrames, out, outFrames))
        return juce::Result::fail("The stretch engine refused this clip");
    const auto part = target.getSiblingFile(target.getFileName() + ".part");
    part.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(part.createOutputStream().release(), rate, unsigned(channels), 24, {}, 0));
        if (!writer || !writer->writeFromAudioSampleBuffer(output, 0, outFrames))
        {
            part.deleteFile();
            return juce::Result::fail("Writing " + target.getFileName() + " failed");
        }
    }
    target.deleteFile();
    if (!part.moveFileTo(target))
        return juce::Result::fail("Cannot finish " + target.getFileName());
    return juce::Result::ok();
}
juce::Result editAudioFile(const juce::File &source, double offsetSeconds, double durationSeconds,
                           const juce::String &operation, double value, double fadeIn, double fadeOut,
                           const juce::File &target)
{
    if (operation != "reverse" && operation != "normalize" && operation != "gain" && operation != "consolidate")
        return juce::Result::fail("Clip edits: reverse, normalize, gain or consolidate");
    if (!std::isfinite(value) || (operation == "gain" && std::abs(value) > 48) ||
        (operation == "normalize" && (value > 0 || value < -48)))
        return juce::Result::fail("Gain is +/-48 dB; normalize targets -48..0 dBFS");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(source));
    if (!reader || reader->sampleRate <= 0)
        return juce::Result::fail("Cannot read " + source.getFileName());
    const auto rate = reader->sampleRate;
    const int channels = int(juce::jlimit(1u, 2u, reader->numChannels));
    const auto start = juce::int64(offsetSeconds * rate);
    const int frames = int(std::min<juce::int64>(juce::int64(durationSeconds * rate), reader->lengthInSamples - start));
    if (frames <= 0)
        return juce::Result::fail("Empty source window");
    juce::AudioBuffer<float> pcm(channels, frames);
    reader->read(&pcm, 0, frames, start, true, channels > 1);
    if (operation == "reverse")
        pcm.reverse(0, frames);
    else if (operation == "gain")
        pcm.applyGain(juce::Decibels::decibelsToGain(float(value)));
    else if (operation == "normalize")
    {
        const float peak = pcm.getMagnitude(0, frames);
        if (peak <= 0)
            return juce::Result::fail("The clip is silent: nothing to normalize");
        pcm.applyGain(juce::Decibels::decibelsToGain(float(value)) / peak);
    }
    else
    {
        const int in = juce::jlimit(0, frames, int(fadeIn * rate)), out = juce::jlimit(0, frames, int(fadeOut * rate));
        for (int c = 0; c < channels; ++c)
        {
            auto *data = pcm.getWritePointer(c);
            for (int i = 0; i < in; ++i)
                data[i] *= float(std::sin(.5 * juce::MathConstants<double>::pi * i / in));
            for (int i = 0; i < out; ++i)
                data[frames - 1 - i] *= float(std::sin(.5 * juce::MathConstants<double>::pi * i / out));
        }
    }
    const auto part = target.getSiblingFile(target.getFileName() + ".part");
    part.deleteFile();
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(part.createOutputStream().release(), rate, unsigned(channels), 24, {}, 0));
        if (!writer || !writer->writeFromAudioSampleBuffer(pcm, 0, frames))
        {
            part.deleteFile();
            return juce::Result::fail("Writing " + target.getFileName() + " failed");
        }
    }
    target.deleteFile();
    return part.moveFileTo(target) ? juce::Result::ok() : juce::Result::fail("Cannot finish " + target.getFileName());
}
} // namespace vibera
