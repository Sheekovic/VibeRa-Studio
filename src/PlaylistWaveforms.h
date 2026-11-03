// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <deque>
#include <map>
#include <cmath>
namespace vibera
{
// A control/UI service. All file access and decoding belong to its worker, never DSP or paint.
class PlaylistWaveforms final : private juce::Thread, private juce::Timer
{
  public:
    struct Data
    {
        double duration = 0, sampleRate = 0;
        juce::int64 framesPerBin = 0, fileSize = 0, modified = 0;
        std::vector<std::vector<std::array<float, 2>>> peaks;
    };
    struct View
    {
        std::shared_ptr<const Data> data;
        juce::String message = "Loading waveform...";
        double getTotalLength() const
        {
            return data ? data->duration : 0;
        }
        void drawChannels(juce::Graphics &g, juce::Rectangle<int> area, double start, double end,
                          float gain) const
        {
            if (area.isEmpty())
                return;
            if (!data)
            {
                g.drawFittedText(message, area, juce::Justification::centredLeft, 1);
                return;
            }
            if (end <= start || data->framesPerBin <= 0)
                return;
            juce::Graphics::ScopedSaveState saved(g);
            g.reduceClipRegion(area);
            const int channels = int(data->peaks.size());
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto &bins = data->peaks[size_t(channel)];
                const float centre = area.getY() + (channel + .5f) * area.getHeight() / channels;
                const float extent = .46f * area.getHeight() / channels;
                for (int x = 0; x < area.getWidth(); ++x)
                {
                    const auto firstTime = start + (end - start) * x / area.getWidth();
                    const auto lastTime = start + (end - start) * (x + 1) / area.getWidth();
                    if (lastTime <= 0 || firstTime >= data->duration)
                        continue;
                    const int first =
                        juce::jlimit(0, int(bins.size()) - 1,
                                     int(std::max(0., firstTime) * data->sampleRate / data->framesPerBin));
                    const int last =
                        juce::jlimit(first, int(bins.size()) - 1,
                                     int(std::max(0., lastTime) * data->sampleRate / data->framesPerBin));
                    float low = bins[size_t(first)][0], high = bins[size_t(first)][1];
                    for (int bin = first + 1; bin <= last; ++bin)
                    {
                        low = std::min(low, bins[size_t(bin)][0]);
                        high = std::max(high, bins[size_t(bin)][1]);
                    }
                    g.drawVerticalLine(area.getX() + x,
                                       centre - juce::jlimit(-1.f, 1.f, high * gain) * extent,
                                       centre - juce::jlimit(-1.f, 1.f, low * gain) * extent + 1.f);
                }
            }
        }
    };

  private:
    struct Job
    {
        juce::String path;
        std::uint64_t ticket = 0;
        View view;
    };
    struct Entry
    {
        View view;
        std::uint64_t ticket = 0, used = 0;
        double nextProbe = 0;
        bool pending = false;
    };
    juce::Component &owner;
    juce::CriticalSection mutex;
    std::deque<Job> requests, results;
    std::map<juce::String, Entry> entries;
    std::uint64_t nextTicket = 0, usage = 0;
    bool working = false;
    static constexpr size_t capacity = 32;
    void run() override
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        while (!threadShouldExit())
        {
            Job job;
            {
                const juce::ScopedLock lock(mutex);
                if (!requests.empty())
                {
                    job = std::move(requests.front());
                    requests.pop_front();
                    working = true;
                }
            }
            if (!job.ticket)
            {
                wait(50);
                continue;
            }
            auto previous = std::move(job.view.data);
            job.view.message = "Waveform unavailable";
            try
            {
                juce::File file(job.path);
                const auto size = file.getSize(), modified = file.getLastModificationTime().toMilliseconds();
                if (!file.existsAsFile())
                    job.view.message = "Missing audio file";
                else if (previous && previous->fileSize == size && previous->modified == modified)
                {
                    job.view.data = std::move(previous);
                    job.view.message.clear();
                }
                else
                {
                    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
                    if (reader && reader->lengthInSamples >= 2 && reader->numChannels > 0 &&
                        std::isfinite(reader->sampleRate) && reader->sampleRate > 0)
                    {
                        auto data = std::make_shared<Data>();
                        data->sampleRate = reader->sampleRate;
                        data->duration = reader->lengthInSamples / reader->sampleRate;
                        data->fileSize = size;
                        data->modified = modified;
                        const auto requestedBins =
                            std::min<juce::int64>(32768, (reader->lengthInSamples - 1) / 512 + 1);
                        data->framesPerBin = (reader->lengthInSamples - 1) / requestedBins + 1;
                        const auto bins = size_t((reader->lengthInSamples - 1) / data->framesPerBin + 1);
                        const int channels = int(std::min(2u, reader->numChannels));
                        data->peaks.resize(size_t(channels));
                        for (auto &channel : data->peaks)
                            channel.resize(bins, {1.f, -1.f});
                        juce::AudioBuffer<float> buffer(channels, 32768);
                        bool valid = true;
                        for (juce::int64 position = 0;
                             position < reader->lengthInSamples && !threadShouldExit();)
                        {
                            const int count =
                                int(std::min<juce::int64>(32768, reader->lengthInSamples - position));
                            if (!reader->read(&buffer, 0, count, position, true, true))
                            {
                                valid = false;
                                break;
                            }
                            for (int channel = 0; channel < channels; ++channel)
                            {
                                const auto *samples = buffer.getReadPointer(channel);
                                for (int frame = 0; frame < count; ++frame)
                                {
                                    const auto value = std::isfinite(samples[frame]) ? samples[frame] : 0.f;
                                    auto &peak = data->peaks[size_t(channel)]
                                                            [size_t((position + frame) / data->framesPerBin)];
                                    peak[0] = std::min(peak[0], value);
                                    peak[1] = std::max(peak[1], value);
                                }
                            }
                            position += count;
                        }
                        if (valid && !threadShouldExit() && size == file.getSize() &&
                            modified == file.getLastModificationTime().toMilliseconds())
                        {
                            job.view.data = std::move(data);
                            job.view.message.clear();
                        }
                        else
                            job.view.message = "Audio file changed or could not be read";
                    }
                    else
                        job.view.message = "Unreadable audio file";
                }
            }
            catch (...)
            {
                job.view.message = "Waveform read failed";
            }
            const juce::ScopedLock lock(mutex);
            results.push_back(std::move(job));
            working = false;
        }
    }
    void timerCallback() override
    {
        bool changed = false;
        std::deque<Job> ready;
        {
            const juce::ScopedLock lock(mutex);
            ready.swap(results);
        }
        for (auto &job : ready)
            if (auto i = entries.find(job.path); i != entries.end() && i->second.ticket == job.ticket)
            {
                i->second.view = std::move(job.view);
                i->second.pending = false;
                i->second.nextProbe = juce::Time::getMillisecondCounterHiRes() + 2000;
                changed = true;
            }
        if (changed)
            owner.repaint();
    }

  public:
    explicit PlaylistWaveforms(juce::Component &component) : Thread("VibeRa waveform peaks"), owner(component)
    {
        startTimer(50);
    }
    ~PlaylistWaveforms() override
    {
        stopTimer();
        signalThreadShouldExit();
        notify();
        waitForThreadToExit(-1);
    }
    View get(const juce::String &path)
    {
        auto found = entries.find(path);
        if (found == entries.end())
        {
            if (entries.size() >= capacity)
            {
                auto oldest = entries.begin();
                for (auto i = entries.begin(); i != entries.end(); ++i)
                    if (i->second.used < oldest->second.used)
                        oldest = i;
                entries.erase(oldest);
            }
            found = entries.emplace(path, Entry{}).first;
        }
        auto &entry = found->second;
        entry.used = ++usage;
        if (!entry.pending && juce::Time::getMillisecondCounterHiRes() >= entry.nextProbe)
        {
            const juce::ScopedLock lock(mutex);
            if (requests.size() + results.size() + size_t(working) < capacity)
            {
                entry.ticket = ++nextTicket;
                requests.push_back({path, entry.ticket, entry.view});
                if (!isThreadRunning() && !startThread())
                    requests.pop_back();
                else
                {
                    entry.pending = true;
                    notify();
                }
            }
        }
        return entry.view;
    }
    void invalidate(const juce::String &path)
    {
        if (auto i = entries.find(path); i != entries.end())
            i->second.nextProbe = 0;
    }
    size_t cachedSources() const
    {
        return entries.size();
    }
    void pollForTest()
    {
        timerCallback();
    }
};
} // namespace vibera
