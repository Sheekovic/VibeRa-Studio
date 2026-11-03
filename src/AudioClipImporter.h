// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <deque>
namespace vibera
{
// Header probing is owned by this worker, never by the UI or audio callback.
class AudioClipImporter final : private juce::Thread
{
  public:
    struct Job
    {
        juce::File file;
        juce::String identity;
        double beat = 0, duration = 0;
        int lane = 0;
        juce::Result result = juce::Result::ok();
    };

  private:
    juce::CriticalSection mutex;
    std::deque<Job> pending, completed;
    bool working = false;
    void run() override
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        while (!threadShouldExit())
        {
            Job job;
            bool found = false;
            {
                const juce::ScopedLock guard(mutex);
                if (!pending.empty())
                {
                    job = std::move(pending.front());
                    pending.pop_front();
                    found = true;
                    working = true;
                }
            }
            if (!found)
            {
                wait(100);
                continue;
            }
            try
            {
                const auto size = job.file.getSize();
                const auto modified = job.file.getLastModificationTime();
                std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(job.file));
                if (!reader || !std::isfinite(reader->sampleRate) || reader->sampleRate <= 0 ||
                    reader->lengthInSamples < 2 || reader->numChannels == 0)
                    job.result = juce::Result::fail("Unreadable audio clip: " + job.file.getFileName());
                else
                    job.duration = reader->lengthInSamples / reader->sampleRate;
                if (size != job.file.getSize() || modified != job.file.getLastModificationTime())
                    job.result = juce::Result::fail("Audio file changed while importing; drop it again");
            }
            catch (...)
            {
                job.result = juce::Result::fail("Audio clip metadata read failed");
            }
            const juce::ScopedLock guard(mutex);
            completed.push_back(std::move(job));
            working = false;
        }
    }

  public:
    AudioClipImporter() : Thread("VibeRa audio clip metadata") {}
    ~AudioClipImporter() override
    {
        signalThreadShouldExit();
        notify();
        // Safe control-thread join. Abortable filesystem IO remains an explicit gap.
        waitForThreadToExit(-1);
    }
    bool request(Job job)
    {
        const juce::ScopedLock guard(mutex);
        if (pending.size() + completed.size() + (working ? 1 : 0) >= 32)
            return false;
        pending.push_back(std::move(job));
        if (!isThreadRunning() && !startThread())
        {
            pending.pop_back();
            return false;
        }
        notify();
        return true;
    }
    bool idle()
    {
        const juce::ScopedLock guard(mutex);
        return pending.empty() && completed.empty() && !working;
    }
    bool poll(Job &job)
    {
        const juce::ScopedLock guard(mutex);
        if (completed.empty())
            return false;
        job = std::move(completed.front());
        completed.pop_front();
        return true;
    }
};
} // namespace vibera
