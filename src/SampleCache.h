// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Sampler.h"
#include "Project.h"
#include <atomic>
#include <map>
#include <memory>
#include <thread>

namespace vibera
{
// Single control/decoder-thread owned per instance. Graph plugins retain immutable samples.
// Cache eviction never changes or releases the last reference of an active voice bank.
class SampleCache
{
  public:
    explicit SampleCache(std::size_t limit = 128 * 1024 * 1024) : byteLimit(limit) {}
    ~SampleCache(); // Joins owned stretch workers; never detaches.
    SampleCache(const SampleCache &) = delete;
    SampleCache &operator=(const SampleCache &) = delete;
    // A key is a source, optionally suffixed "|stretch=<playback rate>" for a
    // pitch-preserving time-stretched copy. Stretching runs on an owned worker;
    // until it finishes prepare() fails with stretchPending (preparationPending).
    juce::Result prepare(const juce::String &, std::shared_ptr<const Sample> &);
    static constexpr const char *stretchPending = "Preparing time-stretched sample...";
    static juce::String preparationKey(const Project &, const Channel &);
    static constexpr const char *stretchMarker = "|stretch=";
    struct Statistics
    {
        std::uint64_t preparations = 0, hits = 0;
        std::size_t bytes = 0, entries = 0;
    };
    Statistics statistics() const;

  private:
    struct Entry
    {
        std::shared_ptr<const Sample> sample;
        juce::int64 fileSize = 0, modified = 0;
        std::uint64_t used = 0;
        std::size_t bytes = 0;
        std::shared_ptr<const Sample> base; // Stretched entries: the exact source PCM they derive from.
    };
    std::map<juce::String, Entry> entries;
    struct StretchJob
    {
        std::shared_ptr<const Sample> base;
        std::shared_ptr<Sample> output;
        std::atomic<bool> done{false};
        std::thread worker;
    };
    std::map<juce::String, std::unique_ptr<StretchJob>> stretchJobs;
    static constexpr std::size_t maximumStretchJobs = 2;
    std::size_t byteLimit, bytes = 0;
    std::uint64_t clock = 0, preparations = 0, hits = 0;
    void trim();
};
} // namespace vibera
