// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SampleCache.h"
#include <algorithm>
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace vibera
{
juce::String SampleCache::preparationKey(const Project &project, const Channel &channel)
{
    if (channel.instrument)
        return "builtin:bass";
    const auto &s = channel.sampler;
    if (!s.fitTempo || !s.stretch || !(s.sourceTempo > 0) || !(project.tempo > 0))
        return channel.source;
    const double rate = project.tempo / s.sourceTempo; // Playback speed the stretch replaces.
    if (std::abs(rate - 1) < 1e-6)
        return channel.source;
    return channel.source + stretchMarker + juce::String(rate, 6);
}
namespace
{
// Pitch-preserving offline stretch to round(length / rate) frames. Short sources
// that the stretcher cannot analyse fall back to linear resampling of the length.
std::shared_ptr<Sample> stretched(const Sample &base, double rate)
{
    auto out = std::make_shared<Sample>();
    out->sampleRate = base.sampleRate;
    out->rootNote = base.rootNote;
    out->oneShot = base.oneShot;
    out->fileLoopStart = base.fileLoopStart;
    out->fileLoopEnd = base.fileLoopEnd;
    const int inputFrames = base.data.getNumSamples();
    const int outputFrames = std::max(2, int(std::lround(inputFrames / rate)));
    out->data.setSize(2, outputFrames);
    signalsmith::stretch::SignalsmithStretch<float> stretcher(0x56494245); // Deterministic.
    stretcher.presetDefault(2, float(base.sampleRate));
    const float *inputs[2] = {base.data.getReadPointer(0), base.data.getReadPointer(1)};
    float *outputs[2] = {out->data.getWritePointer(0), out->data.getWritePointer(1)};
    if (!stretcher.exact(inputs, inputFrames, outputs, outputFrames))
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < outputFrames; ++i)
            {
                const double at = double(i) * (inputFrames - 1) / std::max(1, outputFrames - 1);
                const int index = std::min(int(at), inputFrames - 2);
                const double f = at - index;
                out->data.setSample(c, i, float(base.data.getSample(c, index) +
                                                f * (base.data.getSample(c, index + 1) - base.data.getSample(c, index))));
            }
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < outputFrames; ++i)
            if (!std::isfinite(out->data.getSample(c, i)))
                out->data.setSample(c, i, 0);
    out->prepareNormalization();
    return out;
}
} // namespace
juce::Result SampleCache::prepare(const juce::String &requested, std::shared_ptr<const Sample> &result)
{
    if (requested.contains(stretchMarker))
    {
        const auto source = requested.upToFirstOccurrenceOf(stretchMarker, false, false);
        const auto rate = requested.fromFirstOccurrenceOf(stretchMarker, false, false).getDoubleValue();
        if (!(rate >= .05 && rate <= 20))
            return juce::Result::fail("samplerStretch.invalidRatio: Tempo fit ratio must be 0.05..20");
        std::shared_ptr<const Sample> base;
        if (auto ready = prepare(source, base); ready.failed())
            return ready;
        if (auto it = entries.find(requested); it != entries.end() && it->second.base == base)
        {
            it->second.used = ++clock;
            ++hits;
            result = it->second.sample;
            return juce::Result::ok();
        }
        if (double(base->data.getNumSamples()) / rate > base->sampleRate * 120)
            return juce::Result::fail("samplerStretch.tooLong: Stretched sample would exceed 120 seconds");
        auto job = stretchJobs.find(requested);
        if (job != stretchJobs.end() && job->second->base != base)
        {
            job->second->worker.join(); // Source changed: discard the obsolete result.
            stretchJobs.erase(job);
            job = stretchJobs.end();
        }
        if (job == stretchJobs.end())
        {
            if (stretchJobs.size() >= maximumStretchJobs)
                return juce::Result::fail(stretchPending);
            auto created = std::make_unique<StretchJob>();
            created->base = base;
            auto *raw = created.get();
            created->worker = std::thread([raw, rate] {
                raw->output = stretched(*raw->base, rate);
                raw->done.store(true, std::memory_order_release);
            });
            stretchJobs.emplace(requested, std::move(created));
            return juce::Result::fail(stretchPending);
        }
        if (!job->second->done.load(std::memory_order_acquire))
            return juce::Result::fail(stretchPending);
        job->second->worker.join();
        std::shared_ptr<const Sample> sample = std::move(job->second->output);
        stretchJobs.erase(job);
        const auto sampleBytes = std::size_t(sample->data.getNumSamples()) * 2 * sizeof(float);
        if (auto old = entries.find(requested); old != entries.end())
            bytes -= old->second.bytes;
        entries[requested] = {sample, 0, 0, ++clock, sampleBytes, base};
        bytes += sampleBytes;
        ++preparations;
        result = std::move(sample);
        trim();
        return juce::Result::ok();
    }
    const auto &source = requested;
    const bool builtin = source.startsWith("builtin:");
    juce::File file;
    juce::String key = source;
    juce::int64 size = 0, modified = 0;
    if (!builtin)
    {
        file = juce::File(source);
        if (!file.existsAsFile())
            return juce::Result::fail("Sample unavailable: " + source);
        key = file.getFullPathName();
        size = file.getSize();
        modified = file.getLastModificationTime().toMilliseconds();
    }
    else if (source != "builtin:kick" && source != "builtin:snare" && source != "builtin:hat" &&
             source != "builtin:bass")
        return juce::Result::fail("Unknown built-in sample");

    if (auto it = entries.find(key);
        it != entries.end() && it->second.fileSize == size && it->second.modified == modified)
    {
        it->second.used = ++clock;
        ++hits;
        result = it->second.sample;
        return juce::Result::ok();
    }
    auto sample = std::make_shared<Sample>();
    if (builtin)
        *sample = makeOriginalSample(source.fromFirstOccurrenceOf(":", false, false));
    else
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if (!reader || reader->sampleRate <= 0 || reader->lengthInSamples < 2 ||
            reader->lengthInSamples > reader->sampleRate * 30)
            return juce::Result::fail("Choose a readable sample up to 30 seconds long: " +
                                      file.getFileName());
        sample->sampleRate = reader->sampleRate;
        sample->data.setSize(2, int(reader->lengthInSamples));
        if (!reader->read(&sample->data, 0, sample->data.getNumSamples(), 0, true, true))
            return juce::Result::fail("Sample decoding failed: " + file.getFileName());
        if (reader->numChannels == 1)
            sample->data.copyFrom(1, 0, sample->data, 0, 0, sample->data.getNumSamples());
        if (reader->metadataValues.getValue("NumSampleLoops", "0").getIntValue() > 0)
        {
            const double last = double(sample->data.getNumSamples() - 1);
            const auto start = reader->metadataValues.getValue("Loop0Start", "-1").getLargeIntValue();
            const auto end = reader->metadataValues.getValue("Loop0End", "-1").getLargeIntValue();
            // smpl end is the inclusive last frame; loopEnd is the exclusive boundary.
            if (start >= 0 && end > start && end < sample->data.getNumSamples())
            {
                sample->fileLoopStart = double(start) / last;
                sample->fileLoopEnd = std::min(1., double(end + 1) / last);
            }
        }
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < sample->data.getNumSamples(); ++i)
                if (!std::isfinite(sample->data.getSample(c, i)))
                    return juce::Result::fail("Sample contains nonfinite audio: " + file.getFileName());
        // Do not accept a fingerprint for a file that changed during decoding.
        if (file.getSize() != size || file.getLastModificationTime().toMilliseconds() != modified)
            return juce::Result::fail("Sample changed while loading; try again");
    }
    if (!builtin)
        sample->prepareNormalization();
    const auto sampleBytes = std::size_t(sample->data.getNumSamples()) * 2 * sizeof(float);
    if (auto old = entries.find(key); old != entries.end())
        bytes -= old->second.bytes;
    entries[key] = {sample, size, modified, ++clock, sampleBytes};
    bytes += sampleBytes;
    ++preparations;
    result = std::move(sample);
    trim();
    return juce::Result::ok();
}
SampleCache::~SampleCache()
{
    for (auto &[key, job] : stretchJobs)
        if (job->worker.joinable())
            job->worker.join();
}
void SampleCache::trim()
{
    while (bytes > byteLimit && !entries.empty())
    {
        auto oldest = std::min_element(entries.begin(), entries.end(),
                                       [](auto &a, auto &b) { return a.second.used < b.second.used; });
        bytes -= oldest->second.bytes;
        entries.erase(oldest);
    }
}
SampleCache::Statistics SampleCache::statistics() const
{
    return {preparations, hits, bytes, entries.size()};
}
} // namespace vibera
