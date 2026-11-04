// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "CoProducer.h"
#include <atomic>
#include <mutex>
#include <thread>

namespace vibera::coproducer
{
struct ProviderConfig
{
    juce::File executable, script, installation, cache;
    juce::String model = "basic_rnn";
    unsigned timeoutMilliseconds = 120000;
    size_t memoryMegabytes = 1024;
    unsigned cpuPercent = 20;
};
enum class JobStatus
{
    idle,
    running,
    succeeded,
    cancelled,
    timedOut,
    failed
};
struct JobResult
{
    JobStatus status = JobStatus::idle;
    float progress = 0;
    juce::String error;
    std::vector<Candidate> candidates;
    juce::StringArray warnings;
    juce::var measurements;
    juce::var contextReport; // Provider consumption/omission ledger; control-thread display only.
};
juce::Result providerPreflight(const Request &, const ProviderConfig &);
juce::var serializeRequest(const Request &, const ProviderConfig &);
juce::Result parseResponse(const juce::var &, const Request &, JobResult &);

// Control/UI API; no callback-side locks or model code. Destruction cancels only its owned worker.
class GenerationJob
{
    mutable std::mutex mutex;
    JobResult result;
    std::atomic<bool> cancelled{false};
    std::thread thread;

  public:
    GenerationJob() = default;
    ~GenerationJob();
    juce::Result start(Request, ProviderConfig);
    void cancel() noexcept
    {
        cancelled.store(true);
    }
    JobResult poll() const;
};
} // namespace vibera::coproducer
