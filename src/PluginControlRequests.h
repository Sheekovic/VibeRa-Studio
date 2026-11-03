// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "PluginParameters.h"
#include <array>
#include <cmath>
#include <limits>

namespace vibera
{
// Control-thread bookkeeping, never used by DSP. Each accepted token owns one
// bounded receipt until its caller consumes or abandons it. Wire IDs never wrap
// or get reused within a host instance.
class PluginControlRequests
{
    struct Entry
    {
        PluginParameterResult result;
        unsigned started = 0;
        bool used = false, completed = false, abandoned = false;
    };
    std::array<Entry, 64> entries{};
    unsigned serial = 0;

  public:
    explicit PluginControlRequests(unsigned lastSerial = 0) noexcept : serial(lastSerial) {}
    unsigned reserve(std::uint32_t id, unsigned now) noexcept
    {
        if (serial == std::numeric_limits<unsigned>::max())
            return 0;
        for (auto &entry : entries)
            if (!entry.used)
            {
                entry = {{++serial, id, 0, 0}, now, true, false, false};
                return serial;
            }
        return 0;
    }
    bool complete(const PluginParameterResult &result) noexcept
    {
        if (!result.serial || result.result < 0 || result.result > 3 || !std::isfinite(result.value) ||
            result.value < 0 || result.value > 1)
            return false;
        for (auto &entry : entries)
            if (entry.used && entry.result.serial == result.serial)
            {
                if (entry.completed || entry.result.id != result.id)
                    return false;
                if (entry.abandoned)
                    entry = {};
                else
                {
                    entry.result = result;
                    entry.completed = true;
                }
                return true;
            }
        return false;
    }
    bool ready(unsigned token) const noexcept
    {
        for (const auto &entry : entries)
            if (entry.used && entry.completed && entry.result.serial == token)
                return true;
        return false;
    }
    bool consume(unsigned token, PluginParameterResult &out) noexcept
    {
        for (auto &entry : entries)
            if (entry.used && entry.completed && entry.result.serial == token)
            {
                out = entry.result;
                entry = {};
                return true;
            }
        return false; // Wrong/unready tokens preserve every other caller's receipt and out.
    }
    void abandon(unsigned token) noexcept
    {
        for (auto &entry : entries)
            if (entry.used && entry.result.serial == token)
            {
                if (entry.completed)
                    entry = {};
                else
                    entry.abandoned = true; // The physical request still owns its slot until drained.
                return;
            }
    }
    void failPending(int outcome) noexcept
    {
        for (auto &entry : entries)
            if (entry.used && !entry.completed)
            {
                if (entry.abandoned)
                    entry = {};
                else
                {
                    entry.result.result = outcome;
                    entry.result.value = 0; // Failure placeholder, never an applied native value.
                    entry.completed = true;
                }
            }
    }
    bool expired(unsigned now, unsigned budget) const noexcept
    {
        for (const auto &entry : entries)
            if (entry.used && !entry.completed && now - entry.started > budget)
                return true;
        return false;
    }
    unsigned outstanding() const noexcept
    {
        unsigned count = 0;
        for (const auto &entry : entries)
            count += entry.used;
        return count;
    }
};
} // namespace vibera
