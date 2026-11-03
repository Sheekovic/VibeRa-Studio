// SPDX-License-Identifier: AGPL-3.0-or-later
// Retained audio-history primitive; graph integration has separate acceptance.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace vibera::dsp
{
struct CompensationEdge
{
    std::uint64_t producer = 0, consumer = 0;
    std::uint32_t port = 0;
    bool valid() const noexcept
    {
        return producer != 0 && consumer != 0;
    }
    bool operator==(const CompensationEdge &) const = default;
};

struct CompensationConfiguration
{
    CompensationEdge edge;
    double sampleRate = 0;
    std::uint32_t channels = 0, maximumDelay = 0;
    bool operator==(const CompensationConfiguration &) const = default;
};

enum class CompensationResult
{
    ready,
    insufficientHistory,
    capacityExceeded,
    invalidShape
};

struct CompensationFrame
{
    CompensationResult result = CompensationResult::ready;
    bool discontinuity = false;
    // Meaningful only when ready; this is the global reference stream, not the
    // looped musical playhead. No unbounded mutable-state diagnostic reads.
    std::int64_t sourceReference = 0;
};

// Construction/destruction and shared-handle copying belong to preparation /
// graph reclamation. One adopted graph owns all writes. A replacement node may
// acquire this bank on the control thread, but MUST NOT read/copy cursor or PCM.
// Requested delay belongs to each immutable prepared node, not to this bank.
class CompensationHistory
{
    const CompensationConfiguration config;
    const std::size_t frameCapacity;
    std::vector<float> samples;
    std::size_t writePosition = 0, available = 0;
    std::int64_t previousReference = 0;
    bool havePrevious = false;

    static std::size_t checkedFrames(const CompensationConfiguration &c, std::size_t budget)
    {
        if (!c.edge.valid() || !std::isfinite(c.sampleRate) || c.sampleRate <= 0 || c.channels == 0)
            throw std::invalid_argument("Invalid prepared compensation configuration");
        const auto frames = std::uint64_t(c.maximumDelay) + 1;
        if (frames > std::numeric_limits<std::size_t>::max() / sizeof(float) / c.channels ||
            frames * sizeof(float) * c.channels > budget)
            throw std::length_error("Compensation history exceeds explicit preparation budget");
        return std::size_t(frames);
    }

  public:
    explicit CompensationHistory(CompensationConfiguration configuration, std::size_t storageBudgetBytes)
        : config(configuration), frameCapacity(checkedFrames(config, storageBudgetBytes)),
          samples(frameCapacity * config.channels, 0.f)
    {
    }
    CompensationHistory(const CompensationHistory &) = delete;
    CompensationHistory &operator=(const CompensationHistory &) = delete;

    const CompensationConfiguration &configuration() const noexcept
    {
        return config;
    }

    // Allocation/lock/I/O-free scalar processing. Write every channel before
    // reading, so input and output spans may alias. Per-frame operation needs
    // maximumDelay+1 storage, with no block-size-dependent overwrite hazard.
    // A gap resets availability in O(1); stale prior-stream samples never replay.
    CompensationFrame processFrame(std::span<const float> input, std::span<float> output,
                                   std::uint32_t requestedDelay, std::int64_t reference) noexcept
    {
        if (input.size() != config.channels || output.size() != config.channels)
            return {CompensationResult::invalidShape}; // Cannot safely assume output shape.
        if (requestedDelay > config.maximumDelay)
        {
            std::fill(output.begin(), output.end(), 0.f);
            return {CompensationResult::capacityExceeded}; // Explicit failure, never a successful mute.
        }
        const bool discontinuity =
            havePrevious && (previousReference == std::numeric_limits<std::int64_t>::max() ||
                             reference != previousReference + 1);
        if (discontinuity)
            available = 0;
        for (std::size_t channel = 0; channel < config.channels; ++channel)
            samples[channel * frameCapacity + writePosition] = input[channel];
        available = std::min(frameCapacity, available + 1);
        previousReference = reference;
        havePrevious = true;

        const bool referenceFits =
            reference >= std::numeric_limits<std::int64_t>::min() + std::int64_t(requestedDelay);
        const bool ready = requestedDelay < available && referenceFits;
        const auto readPosition = (writePosition + frameCapacity - requestedDelay) % frameCapacity;
        for (std::size_t channel = 0; channel < config.channels; ++channel)
            output[channel] = ready ? samples[channel * frameCapacity + readPosition] : 0.f;
        writePosition = (writePosition + 1) % frameCapacity;
        return {ready ? CompensationResult::ready : CompensationResult::insufficientHistory, discontinuity,
                ready ? reference - std::int64_t(requestedDelay) : 0};
    }
};

enum class CompensationReuse
{
    matched,
    noPreviousEdge,
    ambiguousEdge,
    incompatibleConfiguration
};
struct CompensationSelection
{
    CompensationReuse result = CompensationReuse::noPreviousEdge;
    std::shared_ptr<CompensationHistory> bank;
};

// CONTROL THREAD ONLY. Both old and new graph edge identities must be unique.
// Reads immutable configuration only. Never walks/copies live PCM or cursors.
// On no previous edge allocate a cold bank explicitly; other outcomes are an
// admission failure requiring diagnosis, not permission to silently lose history.
inline CompensationSelection
selectCompensationHistory(const CompensationConfiguration &desired,
                          std::span<const std::shared_ptr<CompensationHistory>> previousBanks,
                          std::span<const CompensationEdge> newGraphEdges)
{
    if (std::count(newGraphEdges.begin(), newGraphEdges.end(), desired.edge) != 1)
        return {CompensationReuse::ambiguousEdge, {}};
    const std::shared_ptr<CompensationHistory> *found = nullptr;
    for (const auto &bank : previousBanks)
        if (bank && bank->configuration().edge == desired.edge)
        {
            if (found)
                return {CompensationReuse::ambiguousEdge, {}};
            found = &bank;
        }
    if (!found)
        return {};
    if ((*found)->configuration() != desired)
        return {CompensationReuse::incompatibleConfiguration, {}};
    return {CompensationReuse::matched, *found};
}
} // namespace vibera::dsp
