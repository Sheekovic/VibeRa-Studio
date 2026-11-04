// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "../src/SampleCache.h"
#include <chrono>
#include <thread>

// Sampler time stretching (PITCH / MUL / TIME / Mode): the prepared copy's length and pitch.
namespace sampler_stretch_acceptance
{
using namespace vibera;
inline double crossingHz(const juce::AudioBuffer<float> &data, double rate, int from, int to)
{
    double first = -1, last = -1;
    int crossings = 0;
    for (int i = std::max(1, from); i < std::min(to, data.getNumSamples()); ++i)
        if (data.getSample(0, i - 1) < 0 && data.getSample(0, i) >= 0)
        {
            const double t = i - 1 + data.getSample(0, i - 1) / double(data.getSample(0, i - 1) - data.getSample(0, i));
            if (first < 0)
                first = t;
            last = t;
            ++crossings;
        }
    return crossings > 1 ? (crossings - 1) * rate / (last - first) : 0;
}
inline std::shared_ptr<const Sample> prepared(SampleCache &cache, const juce::String &key)
{
    std::shared_ptr<const Sample> result;
    for (int attempt = 0; attempt < 600; ++attempt)
    {
        const auto ready = cache.prepare(key, result);
        if (ready.wasOk())
            return result;
        if (ready.getErrorMessage() != SampleCache::stretchPending)
            throw std::runtime_error(("Preparation failed: " + ready.getErrorMessage()).toStdString());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw std::runtime_error("Preparation did not finish");
}
inline void tests(const juce::File &output)
{
    // One second of a 440 Hz sine at 48 kHz.
    const auto wav = output.getChildFile("stretch-source.wav");
    {
        wav.deleteFile();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::OutputStream> stream(wav.createOutputStream().release());
        std::unique_ptr<juce::AudioFormatWriter> writer(format.createWriterFor(stream.get(), 48000, 1, 24, {}, 0));
        require(writer != nullptr, "Write the stretch source");
        stream.release();
        juce::AudioBuffer<float> sine(1, 48000);
        for (int i = 0; i < 48000; ++i)
            sine.setSample(0, i, float(.5 * std::sin(2 * juce::MathConstants<double>::pi * 440 * i / 48000)));
        writer->writeFromAudioSampleBuffer(sine, 0, 48000);
    }
    Project project = Project::empty();
    project.tempo = 120;
    Channel channel = project.channels.front();
    channel.source = wav.getFullPathName();
    SampleCache cache;
    const auto shape = [&](double mul, double beats, double cents, bool keepPitch)
    {
        channel.sampler = {};
        channel.sampler.timeMul = mul;
        channel.sampler.timeBeats = beats;
        channel.sampler.stretchPitch = cents;
        channel.sampler.stretch = keepPitch;
        require(channel.sampler.valid(), "Time stretching settings are valid");
        return prepared(cache, SampleCache::preparationKey(project, channel));
    };
    channel.sampler = {};
    require(SampleCache::preparationKey(project, channel) == channel.source, "Defaults play the source itself");

    const auto slow = shape(2, 0, 0, false);
    require(std::abs(slow->data.getNumSamples() - 96000) <= 1, "MUL 2 (Resample) doubles the length");
    const double slowHz = crossingHz(slow->data, 48000, 4000, 90000);
    require(std::abs(slowHz - 220) < 2, "Resample halves the pitch with the length (" + std::to_string(slowHz) + ")");

    const auto kept = shape(2, 0, 0, true);
    require(std::abs(kept->data.getNumSamples() - 96000) <= 1, "MUL 2 (Stretch) doubles the length");
    const double keptHz = crossingHz(kept->data, 48000, 8000, 88000);
    require(std::abs(keptHz - 440) < 6, "Stretch keeps the pitch (" + std::to_string(keptHz) + ")");

    const auto up = shape(1, 0, 1200, true);
    require(std::abs(up->data.getNumSamples() - 48000) <= 1, "Stretch PITCH keeps the length");
    const double upHz = crossingHz(up->data, 48000, 6000, 42000);
    require(std::abs(upHz - 880) < 12, "Stretch PITCH +1200 cents raises an octave (" + std::to_string(upHz) + ")");

    const auto timed = shape(1, 4, 0, true);
    require(std::abs(timed->data.getNumSamples() - 96000) <= 1, "TIME 4 beats at 120 BPM is two seconds");
    const auto timedMul = shape(.5, 4, 0, true);
    require(std::abs(timedMul->data.getNumSamples() - 48000) <= 1, "MUL scales the TIME length");

    // Resample-mode PITCH stays a real-time detune (no copy); Stretch-mode PITCH needs one.
    channel.sampler = {};
    channel.sampler.stretchPitch = 300;
    require(SampleCache::preparationKey(project, channel) == channel.source &&
                std::abs(PreparedSamplerSettings(channel.sampler, 120).tuning - std::pow(2., 3 / 12.)) < 1e-9,
            "Resample PITCH detunes in real time");
    channel.sampler.stretch = true;
    require(SampleCache::preparationKey(project, channel).contains(SampleCache::shapeMarker) &&
                std::abs(PreparedSamplerSettings(channel.sampler, 120).tuning - 1) < 1e-9,
            "Stretch PITCH is carried by the prepared copy, not the playback rate");
    // Persisted with the project.
    auto saved = project;
    saved.channels.front().sampler = channel.sampler;
    saved.channels.front().sampler.timeMul = 1.5;
    saved.channels.front().sampler.timeBeats = 8;
    Project reloaded;
    require(Project::parse(saved.state(), reloaded).wasOk() && reloaded.channels.front().sampler == saved.channels.front().sampler,
            "Time stretching settings survive save and reload");
}
} // namespace sampler_stretch_acceptance
