// SPDX-License-Identifier: AGPL-3.0-or-later
// Loudness analysis of rendered/exported audio (BS.1770 / EBU R128, src/Loudness.h).
#include "Studio.h"
#include "Loudness.h"

namespace vibera
{
juce::Result Studio::analyzeLoudnessFile(const juce::File &file, loudness::Result &result)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader)
        return juce::Result::fail("loudness.unreadable: " + file.getFileName() + " is not a readable audio file");
    if (reader->sampleRate <= 0 || reader->lengthInSamples <= 0)
        return juce::Result::fail("loudness.empty: " + file.getFileName() + " has no audio");
    if (reader->lengthInSamples > juce::int64(reader->sampleRate * 60 * 30))
        return juce::Result::fail("loudness.tooLong: Analysis is limited to 30 minutes of audio");
    const int channels = int(std::min<unsigned>(2, reader->numChannels));
    const auto frames = int(reader->lengthInSamples);
    juce::AudioBuffer<float> buffer(channels, frames);
    if (!reader->read(&buffer, 0, frames, 0, true, channels > 1))
        return juce::Result::fail("loudness.readFailed: Could not read " + file.getFileName());
    std::vector<std::vector<float>> planar(static_cast<size_t>(channels));
    for (int c = 0; c < channels; ++c)
        planar[size_t(c)].assign(buffer.getReadPointer(c), buffer.getReadPointer(c) + frames);
    result = loudness::measure(planar, reader->sampleRate);
    return juce::Result::ok();
}

juce::String Studio::describeLoudness(const loudness::Result &r)
{
    auto value = [](double v, int places) { return std::isfinite(v) ? juce::String(v, places) : juce::String("-inf"); };
    return value(r.integratedLufs, 1) + " LUFS integrated, " + value(r.shortTermMaxLufs, 1) +
           " LUFS short-term max, " + value(r.truePeakDbtp, 1) + " dBTP, LRA " + value(r.rangeLu, 1) + " LU";
}

void Studio::reportExportLoudness(const juce::File &file)
{
    // Analysis runs off the message thread; only the status update returns to it.
    auto safe = juce::Component::SafePointer<Studio>(this);
    const auto exported = status;
    juce::Thread::launch(
        [safe, file, exported]
        {
            loudness::Result result;
            const auto analysed = analyzeLoudnessFile(file, result);
            juce::MessageManager::callAsync(
                [safe, analysed, result, exported]
                {
                    if (!safe || !analysed.wasOk() || safe->status != exported)
                        return; // A newer status (or a failed read) wins; nothing is overwritten.
                    safe->status = exported + " | " + describeLoudness(result);
                    safe->repaint();
                });
        });
}
} // namespace vibera
