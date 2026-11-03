// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_cryptography/juce_cryptography.h>
inline juce::File pickerAudioFixture(const juce::File &folder)
{
    const auto file = folder.getChildFile("picker-tone.wav");
    {
        auto out = file.createOutputStream();
        if (!out || !out->setPosition(0))
            return {};
        out->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(out.release(), 48000, 1, 16, {}, 0));
        juce::AudioBuffer<float> samples(1, 120000);
        for (int i = 0; i < samples.getNumSamples(); ++i)
            samples.setSample(
                0, i, .1f * std::sin(float(i) * float(juce::MathConstants<double>::twoPi * 220 / 48000)));
        if (!writer || !writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples()))
            return {};
    }
    const auto media = folder.getChildFile("media");
    const auto collected = media.getChildFile(juce::MD5(file).toHexString() + ".wav");
    if (media.createDirectory().failed() || !file.copyFileTo(collected))
        return {};
    return collected;
}
