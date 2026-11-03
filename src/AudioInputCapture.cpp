// SPDX-License-Identifier: AGPL-3.0-or-later
#include "AudioInputCapture.h"
#include <chrono>
#include <cmath>

namespace vibera
{
struct AudioInputCapture::Session
{
    TakeRequest request;
    double rate = 0, latency = 0;
    juce::AbstractFifo fifo;
    juce::AudioBuffer<float> ring;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    // Callback-owned until stopping; read by the writer thread after acquire.
    std::atomic<bool> started{false}, mapped{false}, overflowed{false}, inputMissing{false};
    std::atomic<double> firstStream{0}, mapStream{0}, mapEdit{0};
    std::atomic<juce::int64> captured{0}, dropped{0};
    // CONTROL -> writer.
    std::atomic<bool> stopping{false}, interrupted{false};
    // Writer-owned, read by CONTROL after join (startPublished/mappedPublished any time).
    std::atomic<bool> mappedPublished{false};
    std::atomic<double> startPublished{0};
    juce::int64 written = 0, trimmed = 0, flushedAt = 0;
    double startEdit = 0;
    bool mappedTake = false, writeFailed = false;
    std::thread thread;
    Session(int frames, int channels) : fifo(frames), ring(channels, frames) { ring.clear(); }
};

std::vector<TakePass> loopPasses(double startEdit, double seconds, double loopStart, double loopEnd,
                                 double minimum)
{
    std::vector<TakePass> passes;
    const bool loop = loopEnd > loopStart && startEdit < loopEnd;
    double offset = 0, edit = startEdit;
    while (offset < seconds - 1.e-9)
    {
        const double length = loop ? std::min(seconds - offset, loopEnd - edit) : seconds - offset;
        if (length <= 0)
            break;
        if (passes.empty() || length >= minimum)
            passes.push_back({edit, offset, length});
        offset += length;
        edit = loopStart;
        if (!loop)
            break;
    }
    return passes;
}

AudioInputCapture::AudioInputCapture(StreamClock c, StampSource s) : clock(std::move(c)), stamps(std::move(s))
{
    for (auto &peak : peaks)
        peak.store(0);
}
AudioInputCapture::~AudioInputCapture() { cancel(); }

juce::Result AudioInputCapture::begin(const TakeRequest &request)
{
    if (owned)
        return juce::Result::fail("audioTake.busy: Finish the current audio take first");
    const double deviceRate = rate.load();
    if (!running.load() || deviceRate <= 0)
        return juce::Result::fail("audioTake.noDevice: No running audio device");
    if (request.channels < 1 || request.channels > 16)
        return juce::Result::fail("audioTake.channels: Record 1..16 inputs");
    if (request.firstInput < 0 || request.firstInput + request.channels > inputs.load())
        return juce::Result::fail("audioTake.noInput: Enable " +
                                  (request.channels == 1 ? juce::String("an input") : juce::String(request.channels) + " inputs") +
                                  " in Settings > Audio");
    if (!std::isfinite(request.startEdit) || request.file == juce::File{})
        return juce::Result::fail("audioTake.request: Invalid take destination");
    // Ten seconds of ring absorbs disk stalls and a transport that starts late.
    auto next = std::make_unique<Session>(juce::jmax(48000, int(deviceRate * 10)), request.channels);
    next->request = request;
    next->rate = deviceRate;
    next->latency = request.compensateLatency ? double(inputLatency.load() + outputLatency.load()) / deviceRate : 0.;
    if (!request.file.getParentDirectory().createDirectory())
        return juce::Result::fail("audioTake.folder: Cannot create " + request.file.getParentDirectory().getFullPathName());
    request.file.deleteFile();
    auto stream = std::make_unique<juce::FileOutputStream>(request.file);
    if (!stream->openedOk())
        return juce::Result::fail("audioTake.file: Cannot write " + request.file.getFullPathName());
    juce::WavAudioFormat wav;
    next->writer.reset(wav.createWriterFor(stream.get(), deviceRate, unsigned(request.channels), 24, {}, 0));
    if (!next->writer)
        return juce::Result::fail("audioTake.writer: Cannot open a 24-bit WAV writer");
    stream.release(); // Owned by the writer now.
    auto *published = next.get();
    next->thread = std::thread([published] { write(*published); });
    owned = std::move(next);
    for (auto &peak : peaks)
        peak.store(0);
    session.store(published);
    return juce::Result::ok();
}

juce::int64 AudioInputCapture::capturedFrames() const noexcept { return owned ? owned->captured.load() : 0; }
juce::int64 AudioInputCapture::droppedFrames() const noexcept { return owned ? owned->dropped.load() : 0; }
std::optional<double> AudioInputCapture::mappedStartEdit() const noexcept
{
    if (!owned || !owned->mappedPublished.load())
        return std::nullopt;
    return owned->startPublished.load();
}

AudioInputCapture::Device AudioInputCapture::device() const noexcept
{
    return {rate.load(),          inputs.load(), outputs.load(), inputLatency.load(),
            outputLatency.load(), block.load(), running.load()};
}

std::array<float, 2> AudioInputCapture::takePeaks() noexcept { return {peaks[0].exchange(0), peaks[1].exchange(0)}; }

AudioInputCapture::TakeResult AudioInputCapture::stop(bool keep)
{
    TakeResult result;
    if (!owned)
        return result;
    // Unpublish, then wait for a callback that may still hold the old pointer.
    session.store(nullptr);
    while (inCallback.load())
        std::this_thread::yield();
    auto &s = *owned;
    s.stopping.store(true);
    if (s.thread.joinable())
        s.thread.join();
    s.writer.reset(); // Flushes and closes the WAV.
    result.file = s.request.file;
    result.rate = s.rate;
    result.channels = s.request.channels;
    result.captured = s.captured.load();
    result.dropped = s.dropped.load();
    result.frames = s.written;
    result.trimmed = s.trimmed;
    result.startEdit = s.startEdit;
    result.latencySeconds = s.latency;
    result.interrupted = s.interrupted.load();
    if (s.writeFailed)
    {
        result.outcome = Outcome::failed;
        result.error = "audioTake.write: Writing " + s.request.file.getFileName() + " failed (disk full or removed?)";
    }
    else if (s.inputMissing.load())
    {
        result.outcome = Outcome::failed;
        result.error = "audioTake.input: The selected input disappeared during the take";
    }
    else if (!s.mappedTake)
        result.outcome = Outcome::unmapped;
    else if (s.written == 0)
        result.outcome = Outcome::empty;
    else
        result.outcome = Outcome::completed;
    if (!keep || result.outcome != Outcome::completed)
        s.request.file.deleteFile();
    owned.reset();
    return result;
}
AudioInputCapture::TakeResult AudioInputCapture::finish() { return stop(true); }
void AudioInputCapture::cancel() { stop(false); }

void AudioInputCapture::write(Session &s)
{
    using namespace std::chrono_literals;
    juce::int64 skip = -1, consumed = 0;
    for (;;)
    {
        const bool stopping = s.stopping.load();
        if (skip < 0)
        {
            if (s.started.load() && s.mapped.load())
            {
                // Edit time of the first captured frame, moved back by the round trip:
                // input heard with output that left the device that much earlier.
                const double first = s.mapEdit.load() + (s.firstStream.load() - s.mapStream.load()) - s.latency;
                const double behind = (s.request.startEdit - first) * s.rate;
                skip = behind > 0 ? juce::int64(std::ceil(behind - 1.e-6)) : 0;
                s.startEdit = first + double(skip) / s.rate;
                s.mappedTake = true;
                s.startPublished.store(s.startEdit);
                s.mappedPublished.store(true);
            }
            else if (stopping)
                return;
            else
            {
                std::this_thread::sleep_for(5ms);
                continue;
            }
        }
        const int ready = s.fifo.getNumReady();
        if (ready == 0)
        {
            if (stopping)
                return;
            std::this_thread::sleep_for(5ms);
            continue;
        }
        const auto scope = s.fifo.read(ready);
        for (const auto [start, size] : {std::pair{scope.startIndex1, scope.blockSize1},
                                         std::pair{scope.startIndex2, scope.blockSize2}})
        {
            if (size <= 0)
                continue;
            const int dropped = int(juce::jlimit<juce::int64>(0, size, skip - consumed));
            consumed += size;
            s.trimmed += dropped;
            if (size > dropped && !s.writeFailed)
            {
                if (s.writer->writeFromAudioSampleBuffer(s.ring, start + dropped, size - dropped))
                    s.written += size - dropped;
                else
                    s.writeFailed = true;
            }
        }
        // Rewrite the WAV header every second of audio: after a crash the file stays
        // readable up to here.
        if (!s.writeFailed && s.written - s.flushedAt >= juce::int64(s.rate))
        {
            s.writer->flush();
            s.flushedAt = s.written;
        }
    }
}

void AudioInputCapture::audioDeviceIOCallbackWithContext(const float *const *input, int numInputs,
                                                         float *const *output, int numOutputs, int numSamples,
                                                         const juce::AudioIODeviceCallbackContext &)
{
    inCallback.store(true);
    if (auto *s = session.load())
    {
        if (!s->started.load(std::memory_order_relaxed))
        {
            s->firstStream.store(clock ? clock() : 0.);
            s->started.store(true);
        }
        TimelineStamp::Value stamp;
        // Only a playing stamp from a block of this take (published after its first
        // callback) maps device stream time to edit time: an older one can belong to a
        // previous take whose transport has since stopped, sought or wrapped.
        if (!s->mapped.load(std::memory_order_relaxed) && stamps && stamps(stamp) && stamp.playing &&
            stamp.stream >= s->firstStream.load() - 1.e-9)
        {
            s->mapStream.store(stamp.stream);
            s->mapEdit.store(stamp.edit);
            s->mapped.store(true);
        }
        const int channels = s->request.channels;
        if (s->overflowed.load(std::memory_order_relaxed))
            s->dropped.fetch_add(numSamples);
        else if (s->fifo.getFreeSpace() < numSamples)
        {
            // Truncate at the gap: keeping later audio would shift it in time.
            s->overflowed.store(true);
            s->dropped.fetch_add(numSamples);
        }
        else
        {
            const auto scope = s->fifo.write(numSamples);
            int offset = 0;
            for (const auto [start, size] : {std::pair{scope.startIndex1, scope.blockSize1},
                                             std::pair{scope.startIndex2, scope.blockSize2}})
            {
                for (int c = 0; c < channels && size > 0; ++c)
                {
                    const int index = s->request.firstInput + c;
                    if (index < numInputs && input[index] != nullptr)
                        s->ring.copyFrom(c, start, input[index] + offset, size);
                    else
                    {
                        s->ring.clear(c, start, size);
                        s->inputMissing.store(true);
                    }
                }
                offset += juce::jmax(0, size);
            }
            s->captured.fetch_add(numSamples);
        }
    }
    const int first = monitorFirstInput.load(), mono = monitorChannels.load() == 1;
    for (int c = 0; c < 2; ++c)
    {
        const int index = first + (mono ? 0 : c);
        if (index < numInputs && input[index] != nullptr)
        {
            const float level = juce::FloatVectorOperations::findMaximum(input[index], numSamples);
            const float floor = -juce::FloatVectorOperations::findMinimum(input[index], numSamples);
            const float peak = std::max(level, floor);
            float previous = peaks[size_t(c)].load(std::memory_order_relaxed);
            while (peak > previous && !peaks[size_t(c)].compare_exchange_weak(previous, peak))
            {
            }
        }
    }
    // Every device callback owns its output: start from silence, then monitor.
    for (int c = 0; c < numOutputs; ++c)
        if (output[c] != nullptr)
            juce::FloatVectorOperations::clear(output[c], numSamples);
    if (monitor.load() && numInputs > 0)
    {
        const float gain = monitorGain.load();
        for (int c = 0; c < juce::jmin(2, numOutputs); ++c)
        {
            const int index = first + (mono ? 0 : c);
            if (output[c] != nullptr && index < numInputs && input[index] != nullptr)
                juce::FloatVectorOperations::addWithMultiply(output[c], input[index], gain, numSamples);
        }
    }
    inCallback.store(false);
}

void AudioInputCapture::audioDeviceAboutToStart(juce::AudioIODevice *device)
{
    if (device == nullptr)
        return;
    rate.store(device->getCurrentSampleRate());
    inputs.store(device->getActiveInputChannels().countNumberOfSetBits());
    outputs.store(device->getActiveOutputChannels().countNumberOfSetBits());
    inputLatency.store(device->getInputLatencyInSamples());
    outputLatency.store(device->getOutputLatencyInSamples());
    block.store(device->getCurrentBufferSizeSamples());
    running.store(true);
}

void AudioInputCapture::audioDeviceStopped()
{
    running.store(false);
    // A restarted device has a new stream clock: the take cannot continue seamlessly.
    if (auto *s = session.load())
    {
        s->interrupted.store(true);
        s->overflowed.store(true);
    }
}
} // namespace vibera
