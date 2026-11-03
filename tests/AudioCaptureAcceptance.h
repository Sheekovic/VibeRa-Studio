// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerMonoPortamentoAcceptance.h"
#include "AudioInputCapture.h"

// Audio-input takes: callback ring, timeline mapping, start trim, latency
// compensation, mono/stereo files, monitoring, truncation and refusals.
namespace audio_capture_acceptance
{
using namespace vibera;
using sampler_mono_portamento_acceptance::callback;

struct FakeDevice final : juce::AudioIODevice
{
    int ins = 2, inLatency = 64, outLatency = 128;
    FakeDevice() : juce::AudioIODevice("Fake input", "Fake") {}
    juce::StringArray getOutputChannelNames() override { return {"L", "R"}; }
    juce::StringArray getInputChannelNames() override { return {"In 1", "In 2"}; }
    juce::Array<double> getAvailableSampleRates() override { return {48000.}; }
    juce::Array<int> getAvailableBufferSizes() override { return {256}; }
    int getDefaultBufferSize() override { return 256; }
    juce::String open(const juce::BigInteger &, const juce::BigInteger &, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback *) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 256; }
    double getCurrentSampleRate() override { return 48000.; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return (1 << ins) - 1; }
    int getOutputLatencyInSamples() override { return outLatency; }
    int getInputLatencyInSamples() override { return inLatency; }
};

// Drives the capture like a device: one block per call, stream time advancing.
struct Rig
{
    static constexpr int block = 256;
    static constexpr double rate = 48000;
    double stream = 10;          // Device stream seconds at the current block start.
    double editOffset = -10;     // Edit seconds = stream + editOffset while playing.
    bool playing = true;
    TimelineStamp stamp;
    juce::int64 sent = 0;
    AudioInputCapture capture{[this] { return stream; },
                              [this](TimelineStamp::Value &v) { return stamp.read(v); }};
    juce::AudioBuffer<float> in{2, block}, out{2, block};
    // Distinct, exactly representable 24-bit values per channel and frame.
    static float value(juce::int64 frame, int channel)
    {
        return float((frame % 4096) - 2048) / 8192.f + (channel == 1 ? .5f : 0.f) * (frame % 2 ? 1.f : -1.f) * .25f;
    }
    void blocks(int count)
    {
        for (int b = 0; b < count; ++b)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < block; ++i)
                    in.setSample(c, i, value(sent + i, c));
            for (int c = 0; c < 2; ++c)
                juce::FloatVectorOperations::fill(out.getWritePointer(c), .7f, block);
            callback(
                [&]() noexcept
                {
                    capture.audioDeviceIOCallbackWithContext(in.getArrayOfReadPointers(), 2,
                                                             out.getArrayOfWritePointers(), 2, block, {});
                });
            // Tracktion publishes the Master stamp after the capture callback ran.
            stamp.publish(stream, stream + editOffset, playing);
            stream += block / rate;
            sent += block;
        }
    }
};

inline juce::AudioBuffer<float> readWav(const juce::File &file, int &channels, double &rate)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(file.createInputStream().release(), true));
    require(reader != nullptr, "Recorded take is a readable WAV");
    channels = int(reader->numChannels);
    rate = reader->sampleRate;
    require(reader->bitsPerSample == 24, "Recorded take is 24-bit");
    juce::AudioBuffer<float> pcm(channels, int(reader->lengthInSamples));
    reader->read(&pcm, 0, int(reader->lengthInSamples), 0, true, true);
    return pcm;
}

inline void tests(const juce::File &folder)
{
    FakeDevice device;
    const double latency = double(device.inLatency + device.outLatency) / Rig::rate;
    {
        Rig rig;
        AudioInputCapture::TakeRequest request;
        request.file = folder.getChildFile("stopped.wav");
        require(rig.capture.begin(request).failed(), "No running device refuses a take");
        rig.capture.audioDeviceAboutToStart(&device);
        const auto device = rig.capture.device();
        require(device.running && device.inputs == 2 && device.rate == 48000 && device.inputLatency == 64 &&
                    device.outputLatency == 128,
                "Device readback reports inputs, rate and both latencies");
        request.firstInput = 1;
        require(rig.capture.begin(request).failed(), "Stereo from the last input refuses: one input left");
    }
    {
        // Stereo take that starts at edit 0 while the transport already plays:
        // the round-trip latency of frames is trimmed, so the clip starts at 0.
        Rig rig;
        rig.capture.audioDeviceAboutToStart(&device);
        AudioInputCapture::TakeRequest request;
        request.file = folder.getChildFile("stereo.wav");
        request.startEdit = 0;
        require(rig.capture.begin(request).wasOk(), "Stereo take begins");
        require(rig.capture.capturing(), "Capture is published");
        rig.blocks(200);
        const auto result = rig.capture.finish();
        require(result.outcome == AudioInputCapture::Outcome::completed, "Stereo take completes");
        const auto skip = juce::int64(std::ceil(latency * Rig::rate - 1.e-6));
        require(result.trimmed == skip && result.frames == 200 * Rig::block - skip && result.dropped == 0 &&
                    result.captured == 200 * Rig::block,
                "Exactly the round-trip latency is trimmed before the requested start");
        require(std::abs(result.startEdit) < 1.e-9 && std::abs(result.latencySeconds - latency) < 1.e-12,
                "Take starts at the requested edit time");
        int channels = 0;
        double rate = 0;
        const auto pcm = readWav(result.file, channels, rate);
        require(channels == 2 && rate == 48000 && pcm.getNumSamples() == result.frames, "Stereo WAV shape");
        for (int i : {0, 1, 1000, pcm.getNumSamples() - 1})
            for (int c = 0; c < 2; ++c)
                require(std::abs(pcm.getSample(c, i) - Rig::value(skip + i, c)) < 1.e-6f,
                        "Recorded PCM equals the captured input after the trim");
    }
    {
        // Mono from the second input; transport starts two blocks after capture
        // (Play pressed with Record armed): mapping still lands on the timeline.
        Rig rig;
        rig.capture.audioDeviceAboutToStart(&device);
        rig.playing = false;
        AudioInputCapture::TakeRequest request;
        request.file = folder.getChildFile("mono.wav");
        request.channels = 1;
        request.firstInput = 1;
        request.startEdit = 0;
        request.compensateLatency = false;
        require(rig.capture.begin(request).wasOk(), "Mono take begins");
        rig.blocks(2);
        rig.playing = true;
        rig.blocks(50);
        const auto result = rig.capture.finish();
        require(result.outcome == AudioInputCapture::Outcome::completed && result.channels == 1,
                "Mono take completes");
        require(result.trimmed == 0 && result.frames == 52 * Rig::block,
                "Without compensation and with the stream mapped to edit 0, nothing is trimmed");
        int channels = 0;
        double rate = 0;
        const auto pcm = readWav(result.file, channels, rate);
        require(channels == 1 && std::abs(pcm.getSample(0, 7) - Rig::value(7, 1)) < 1.e-6f,
                "Mono file holds the selected input");
    }
    {
        // A requested start later than the whole take keeps nothing.
        Rig rig;
        rig.capture.audioDeviceAboutToStart(&device);
        AudioInputCapture::TakeRequest request;
        request.file = folder.getChildFile("late.wav");
        request.startEdit = 60;
        require(rig.capture.begin(request).wasOk(), "Late take begins");
        rig.blocks(10);
        const auto result = rig.capture.finish();
        require(result.outcome == AudioInputCapture::Outcome::empty && !result.file.existsAsFile(),
                "Fully trimmed take is reported empty and its file removed");
    }
    {
        // The transport never plays: the take cannot be placed, and a ring that
        // fills up while waiting truncates and counts the gap instead of hiding it.
        Rig rig;
        rig.capture.audioDeviceAboutToStart(&device);
        rig.playing = false;
        AudioInputCapture::TakeRequest request;
        request.file = folder.getChildFile("unmapped.wav");
        require(rig.capture.begin(request).wasOk(), "Unmapped take begins");
        rig.blocks(int(Rig::rate * 11 / Rig::block));
        require(rig.capture.droppedFrames() > 0, "Overflow is counted while the take waits for the timeline");
        const auto result = rig.capture.finish();
        require(result.outcome == AudioInputCapture::Outcome::unmapped && !result.file.existsAsFile() &&
                    result.captured <= juce::int64(Rig::rate * 10) && result.dropped > 0,
                "Unplaced take is discarded with its truncation reported");
    }
    {
        // A device restart mid-take is reported and the rest is not appended.
        Rig rig;
        rig.capture.audioDeviceAboutToStart(&device);
        AudioInputCapture::TakeRequest request;
        request.file = folder.getChildFile("interrupted.wav");
        request.compensateLatency = false;
        require(rig.capture.begin(request).wasOk(), "Take begins before a device restart");
        rig.blocks(10);
        rig.capture.audioDeviceStopped();
        rig.capture.audioDeviceAboutToStart(&device);
        rig.blocks(10);
        const auto result = rig.capture.finish();
        require(result.interrupted && result.frames == 10 * Rig::block && result.dropped == 10 * Rig::block,
                "Device restart truncates the take and reports it");
        require(result.outcome == AudioInputCapture::Outcome::completed, "Audio before the restart is kept");
    }
    {
        // Monitoring: silence when off (the callback owns its output), input x gain when on.
        Rig rig;
        rig.capture.audioDeviceAboutToStart(&device);
        rig.blocks(1);
        require(rig.out.getMagnitude(0, Rig::block) == 0.f, "Monitoring off writes silence");
        rig.capture.monitor = true;
        rig.capture.monitorGain = .5f;
        rig.blocks(1);
        require(std::abs(rig.out.getSample(0, 3) - .5f * Rig::value(Rig::block + 3, 0)) < 1.e-7f &&
                    std::abs(rig.out.getSample(1, 3) - .5f * Rig::value(Rig::block + 3, 1)) < 1.e-7f,
                "Stereo monitoring passes each input to its output");
        rig.capture.monitorChannels = 1;
        rig.capture.monitorFirstInput = 1;
        rig.blocks(1);
        require(std::abs(rig.out.getSample(0, 5) - .5f * Rig::value(2 * Rig::block + 5, 1)) < 1.e-7f &&
                    std::abs(rig.out.getSample(1, 5) - .5f * Rig::value(2 * Rig::block + 5, 1)) < 1.e-7f,
                "Mono monitoring sends the selected input to both outputs");
        const auto peaks = rig.capture.takePeaks();
        require(peaks[0] > .2f && rig.capture.takePeaks()[0] == 0.f, "Input peaks are read and cleared");
    }
    {
        // Cancel deletes the partial file.
        Rig rig;
        rig.capture.audioDeviceAboutToStart(&device);
        AudioInputCapture::TakeRequest request;
        request.file = folder.getChildFile("cancelled.wav");
        require(rig.capture.begin(request).wasOk(), "Cancelled take begins");
        rig.blocks(20);
        rig.capture.cancel();
        require(!rig.capture.capturing() && !request.file.existsAsFile(), "Cancel removes the partial take");
    }
}
} // namespace audio_capture_acceptance
