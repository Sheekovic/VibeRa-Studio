// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "SamplerModulation.h"
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>

namespace vibera
{
struct SamplerLfoPhaseAutomation
{
    // Each nonempty span contains exactly count frame values. Empty selects manual.
    std::span<const double> periods;
    std::span<const std::uint8_t> tempoFlags;
};
using SamplerLfoPhaseAutomationBlock = std::array<SamplerLfoPhaseAutomation, 5>;

// prepare/seed/destruction require quiescent CONTROL ownership.
// One DSP owner begins and commits; readers never advance time or own storage.
class SamplerLfoPhaseBlock
{
  public:
    class View
    {
        friend class SamplerLfoPhaseBlock;
        const SamplerLfoPhaseBlock *owner = nullptr;
        std::uint64_t serial = 0;
        View(const SamplerLfoPhaseBlock *p, std::uint64_t s) noexcept : owner(p), serial(s) {}

      public:
        View() = default;
        // Valid only while the actual owner exists. Stale/cancelled/committed reads refuse.
        bool read(int frame, SamplerModulationPhases &output) const noexcept
        {
            return owner && owner->read(serial, frame, output);
        }
    };

  private:
    std::unique_ptr<SamplerModulationPhases[]> storage;
    SamplerModulationPhases committed{}, origin{}, increments{}, pendingEnd{};
    int maximum = 0, frames = 0;
    std::uint64_t serial = 0;
    unsigned automated = 0;
    bool active = false;

    static bool validPhases(const SamplerModulationPhases &p) noexcept
    {
        return std::all_of(p.begin(), p.end(), [](double v) { return std::isfinite(v) && v >= 0 && v < 1; });
    }
    bool read(std::uint64_t ticket, int frame, SamplerModulationPhases &output) const noexcept
    {
        if (!active || ticket != serial || frame < 0 || frame > frames)
            return false;
        SamplerModulationPhases next;
        for (size_t i = 0; i < next.size(); ++i)
            // Preserve the existing constant-manual expression, even beside automated targets.
            next[i] = automated & (1u << i)
                          ? storage[size_t(frame)][i]
                          : PreparedSamplerModulation::wrap(origin[i] + frame * increments[i]);
        output = next;
        return true;
    }

  public:
    SamplerLfoPhaseBlock() = default;
    SamplerLfoPhaseBlock(const SamplerLfoPhaseBlock &) = delete;
    SamplerLfoPhaseBlock &operator=(const SamplerLfoPhaseBlock &) = delete;
    SamplerLfoPhaseBlock(SamplerLfoPhaseBlock &&) = delete;
    SamplerLfoPhaseBlock &operator=(SamplerLfoPhaseBlock &&) = delete;

    bool prepare(int maximumFrames) noexcept
    {
        if (active || maximumFrames < 1 ||
            size_t(maximumFrames) + 1 > std::numeric_limits<size_t>::max() / sizeof(SamplerModulationPhases))
            return false;
        try
        {
            auto next = std::make_unique<SamplerModulationPhases[]>(size_t(maximumFrames) + 1);
            storage = std::move(next); // CONTROL only, including old storage retirement.
            maximum = maximumFrames;
            return true;
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
    }
    bool seedOnControl(const SamplerModulationPhases &p) noexcept
    {
        if (active || !validPhases(p))
            return false;
        committed = p;
        return true;
    }
    int capacity() const noexcept
    {
        return maximum;
    }
    const SamplerModulationPhases &phases() const noexcept
    {
        return committed;
    }
    bool pending() const noexcept
    {
        return active;
    }
    bool begin(const PreparedSamplerModulation &p, int count,
               const SamplerLfoPhaseAutomationBlock &automation = {}) noexcept
    {
        if (active || !storage || count < 1 || count > maximum ||
            serial == std::numeric_limits<std::uint64_t>::max() || !validPhases(committed) ||
            !std::isfinite(p.sampleRate) || p.sampleRate <= 0 || !std::isfinite(p.secondsPerSample) ||
            p.secondsPerSample <= 0 || !std::isfinite(p.beatsPerSample) || p.beatsPerSample <= 0)
            return false;
        const auto *period = samplerModulationParameter("lfoPeriod");
        if (!period)
            return false;
        const auto validPeriod = [&](double v) noexcept
        { return std::isfinite(v) && v >= period->minimum && v <= period->maximum; };
        unsigned mask = 0;
        for (size_t i = 0; i < automation.size(); ++i)
        {
            const auto &a = automation[i];
            if ((!a.periods.empty() && a.periods.size() != size_t(count)) ||
                (!a.tempoFlags.empty() && a.tempoFlags.size() != size_t(count)) ||
                !validPeriod(p.settings[i].lfoPeriod) || !std::isfinite(p.globalIncrements[i]) ||
                p.globalIncrements[i] <= 0 || !std::isfinite(committed[i] + count * p.globalIncrements[i]))
                return false;
            if (!a.periods.empty() || !a.tempoFlags.empty())
                mask |= 1u << i;
            for (double v : a.periods)
                if (!validPeriod(v))
                    return false;
            for (auto v : a.tempoFlags)
                if (v > 1)
                    return false;
        }
        // Work storage is private until all data/increments have passed. Failure never commits phase.
        storage[0] = committed;
        for (size_t i = 0; i < automation.size(); ++i)
            if (mask & (1u << i))
                for (int frame = 0; frame < count; ++frame)
                {
                    const auto &a = automation[i];
                    const double duration =
                        a.periods.empty() ? p.settings[i].lfoPeriod : a.periods[size_t(frame)];
                    const bool tempo =
                        a.tempoFlags.empty() ? p.settings[i].lfoTempo : a.tempoFlags[size_t(frame)] != 0;
                    const auto next = storage[size_t(frame)][i] +
                                      (tempo ? p.beatsPerSample : p.secondsPerSample) / duration;
                    if (!std::isfinite(next))
                        return false;
                    storage[size_t(frame) + 1][i] = PreparedSamplerModulation::wrap(next);
                }
        SamplerModulationPhases end;
        for (size_t i = 0; i < end.size(); ++i)
            end[i] = mask & (1u << i)
                         ? storage[size_t(count)][i]
                         : PreparedSamplerModulation::wrap(committed[i] + count * p.globalIncrements[i]);
        origin = committed;
        increments = p.globalIncrements;
        pendingEnd = end;
        automated = mask;
        frames = count;
        ++serial;
        active = true;
        return true;
    }
    // Same sole clock owner, no capacity/storage requirement for constant manual values.
    // Existing begin() retains its explicit prepared-capacity contract.
    bool beginManual(const PreparedSamplerModulation &p, int count) noexcept
    {
        if (active || count < 1 || serial == std::numeric_limits<std::uint64_t>::max() ||
            !validPhases(committed) || !std::isfinite(p.sampleRate) || p.sampleRate <= 0 ||
            !std::isfinite(p.secondsPerSample) || p.secondsPerSample <= 0 ||
            !std::isfinite(p.beatsPerSample) || p.beatsPerSample <= 0)
            return false;
        const auto *period = samplerModulationParameter("lfoPeriod");
        if (!period)
            return false;
        SamplerModulationPhases end;
        for (size_t i = 0; i < end.size(); ++i)
        {
            const auto next = committed[i] + count * p.globalIncrements[i];
            if (!std::isfinite(p.settings[i].lfoPeriod) || p.settings[i].lfoPeriod < period->minimum ||
                p.settings[i].lfoPeriod > period->maximum || !std::isfinite(p.globalIncrements[i]) ||
                p.globalIncrements[i] <= 0 || !std::isfinite(next))
                return false;
            end[i] = PreparedSamplerModulation::wrap(next);
        }
        origin = committed;
        increments = p.globalIncrements;
        pendingEnd = end;
        automated = 0;
        frames = count;
        ++serial;
        active = true;
        return true;
    }
    View view() const noexcept
    {
        return active ? View(this, serial) : View();
    }
    bool commit() noexcept
    {
        if (!active)
            return false;
        committed = pendingEnd;
        active = false;
        return true;
    }
    bool cancel() noexcept
    {
        if (!active)
            return false;
        active = false;
        return true;
    }
};
} // namespace vibera
