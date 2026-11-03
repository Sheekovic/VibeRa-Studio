// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PlaylistWaveforms.h"
static void waveformWorkerTests(const juce::File &folder)
{
    juce::Component owner;
    PlaylistWaveforms waveforms(owner);
    const auto original = folder.getChildFile("audio-original-45s.wav");
    const auto file = folder.getChildFile("waveform-worker-source.wav");
    require(original.copyFileTo(file), "waveform disposable source copy");
    const auto path = file.getFullPathName();
    require(!waveforms.get(path).data, "initial waveform request publishes no unprepared data");
    auto await = [&](auto predicate)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + 8000;
        do
        {
            waveforms.pollForTest();
            const auto view = waveforms.get(path);
            if (predicate(view))
                return view;
            dispatchMessages(2);
        } while (juce::Time::getMillisecondCounter() < deadline);
        throw std::runtime_error("waveform worker result timeout");
    };
    const auto first = await([](const auto &view) { return bool(view.data); });
    require(std::abs(first.data->duration - 45.) < 1.e-8 && first.data->peaks.size() == 2,
            "waveform whole long-file duration/stereo");
    require(first.data->peaks[0].size() <= 32768 && first.data->peaks[0].front()[1] > .12f &&
                first.data->peaks[0].back()[0] < -.12f,
            "waveform actual source polarity and bin bound");
    juce::Image image(juce::Image::RGB, 256, 80, true);
    {
        juce::Graphics graphics(image);
        graphics.setColour(juce::Colours::white);
        first.drawChannels(graphics, image.getBounds(), 35, 36, 1.f);
    }
    require(image.getPixelAt(20, 22).getBrightness() > .5f && image.getPixelAt(20, 17).getBrightness() < .1f,
            "waveform source-window uses negative PCM after thirty seconds");
    {
        juce::AudioBuffer<float> pcm(1, 24000);
        pcm.clear();
        pcm.applyGain(0.f);
        auto stream = file.createOutputStream();
        require(bool(stream), "waveform replacement stream");
        stream->setPosition(0);
        stream->truncate();
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 24000., 1, 16, {}, 0));
        require(writer && writer->writeFromAudioSampleBuffer(pcm, 0, pcm.getNumSamples()),
                "waveform replace source");
    }
    waveforms.invalidate(path);
    const auto replaced = await([](const auto &view) { return view.data && view.data->duration == 1.; });
    require(replaced.data->peaks.size() == 1 && replaced.data != first.data && first.data->duration == 45.,
            "changed file publishes new immutable data, old view remains valid");
    require(file.deleteFile(), "remove owned waveform fixture");
    waveforms.invalidate(path);
    const auto missing =
        await([](const auto &view) { return !view.data && view.message == "Missing audio file"; });
    require(!missing.data, "missing file cannot paint stale waveform as valid");
    for (int index = 0; index < 70; ++index)
        waveforms.get(folder.getChildFile("missing-peak-" + juce::String(index) + ".wav").getFullPathName());
    require(waveforms.cachedSources() == 32 && first.data->peaks[0].back()[0] < -.12f,
            "LRU bound and retained immutable view safe across eviction");
    folder.getChildFile("waveform-worker-verification.txt")
        .replaceWithText(
            "PASS: initial unprepared state, actual 45-second stereo polarity/source-window raster, bounded "
            "bins/cache, changed-file immutable publication, missing-file invalidation and eviction "
            "lifetime. File IO/reader/decode preparation is worker-owned by source review. No hard "
            "wall-clock/network responsiveness, DSP, native interaction or listening qualification.\n");
}
