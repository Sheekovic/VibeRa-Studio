// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include "Automation.h"
#include "SamplerAutomationParameters.h"
#include "SamplerLfoPhaseBlock.h"
#include <vector>

namespace vibera
{
// CONTROL-built, immutable, borrowed from the same pinned LiveDocument as its lanes.
struct SamplerModulationAutomationLane
{
    const AutomationProgram::Lane *lane = nullptr;
    SamplerAutomationParameter parameter;
};
inline bool prepareSamplerModulationAutomation(const AutomationProgram &program, std::int64_t owner,
                                               bool localSampler,
                                               std::vector<SamplerModulationAutomationLane> &output)
{
    std::vector<SamplerModulationAutomationLane> next;
    if (localSampler)
        for (const auto &lane : program.compiledLanes())
            if (lane.key.kind == AutomationParameter::samplerModulation && lane.key.owner == owner)
            {
                const auto parameter = lookupSamplerAutomationParameter(lane.key.parameter);
                if (!parameter || next.size() == samplerAutomationParameterCount)
                    return false;
                next.push_back({&lane, *parameter});
            }
    output = std::move(next);
    return true;
}

// One nonmoving Runtime owner. prepare/destruction require quiescent CONTROL.
// No frame plane of envelope values: evaluate only actual immutable active lanes.
class SamplerModulationAutomationBlock
{
  public:
    enum class Status
    {
        idle,
        ready,
        invalidBlock,
        invalidCurve,
        staleView
    };

  private:
    SamplerLfoPhaseBlock phase;
    std::unique_ptr<double[]> periods;
    std::unique_ptr<std::uint8_t[]> flags;
    std::span<const SamplerModulationAutomationLane> lanes;
    int maximum = 0, count = 0;
    double beat = 0, step = 0;
    Status state = Status::idle;
    static bool value(const SamplerModulationAutomationLane &row, double beat, double &output) noexcept
    {
        double physical;
        return row.lane &&
               samplerAutomationFloatToPhysical(row.parameter, row.lane->valueAt(beat), physical) &&
               quantizeSamplerAutomationValue(row.parameter, physical, output);
    }

  public:
    SamplerModulationAutomationBlock() = default;
    SamplerModulationAutomationBlock(const SamplerModulationAutomationBlock &) = delete;
    SamplerModulationAutomationBlock &operator=(const SamplerModulationAutomationBlock &) = delete;
    SamplerModulationAutomationBlock(SamplerModulationAutomationBlock &&) = delete;
    SamplerModulationAutomationBlock &operator=(SamplerModulationAutomationBlock &&) = delete;
    bool prepare(int frames) noexcept
    {
        if (phase.pending() || frames < 1 ||
            size_t(frames) > std::numeric_limits<size_t>::max() / 5 / sizeof(double))
            return false;
        try
        {
            auto p = std::make_unique<double[]>(size_t(frames) * 5);
            auto f = std::make_unique<std::uint8_t[]>(size_t(frames) * 5);
            if (!phase.prepare(frames))
                return false;
            periods = std::move(p);
            flags = std::move(f);
            maximum = frames;
            state = Status::idle;
            return true;
        }
        catch (const std::bad_alloc &)
        {
            return false;
        }
    }
    Status status() const noexcept
    {
        return state;
    }
    const SamplerModulationPhases &committedPhases() const noexcept
    {
        return phase.phases();
    }
    bool pending() const noexcept
    {
        return phase.pending();
    }
    void reject() noexcept
    {
        state = Status::invalidBlock;
    }
    bool beginManual(const PreparedSamplerModulation &manual, int frames) noexcept
    {
        if (phase.pending())
        {
            state = Status::invalidBlock;
            return false;
        }
        lanes = {};
        count = frames;
        const bool ok = phase.beginManual(manual, frames);
        state = ok ? Status::ready : Status::invalidBlock;
        return ok;
    }
    bool begin(const PreparedSamplerModulation &manual,
               std::span<const SamplerModulationAutomationLane> bindings, double firstBeat,
               double beatsPerFrame, int frames) noexcept
    {
        if (phase.pending())
        {
            state = Status::invalidBlock;
            return false;
        }
        lanes = {};
        count = 0;
        if (frames < 1 || frames > maximum || bindings.size() > samplerAutomationParameterCount ||
            !std::isfinite(firstBeat) || !std::isfinite(beatsPerFrame) || beatsPerFrame < 0 ||
            !std::isfinite(firstBeat + (frames - 1) * beatsPerFrame))
        {
            state = Status::invalidBlock;
            return false;
        }
        SamplerLfoPhaseAutomationBlock columns{};
        std::array<std::uint32_t, samplerAutomationParameterCount> codes{};
        size_t used = 0;
        // Validate every row/frame BEFORE phase publication, input consumption or score reconciliation.
        for (const auto &row : bindings)
        {
            if (!row.lane || row.lane->key.kind != AutomationParameter::samplerModulation ||
                row.lane->key.parameter != row.parameter.code || row.parameter.registryTarget < 0 ||
                row.parameter.registryTarget >= 5 || !row.parameter.registryParameter)
            {
                state = Status::invalidCurve;
                return false;
            }
            for (size_t i = 0; i < used; ++i)
                if (codes[i] == row.parameter.code)
                {
                    state = Status::invalidCurve;
                    return false;
                }
            codes[used++] = row.parameter.code;
            const auto target = size_t(row.parameter.registryTarget);
            const auto field = row.parameter.code % samplerAutomationTargetStride;
            for (int frame = 0; frame < frames; ++frame)
            {
                double selected;
                if (!value(row, firstBeat + frame * beatsPerFrame, selected))
                {
                    state = Status::invalidCurve;
                    return false;
                }
                if (field == 17)
                    periods[target * size_t(maximum) + size_t(frame)] = selected;
                if (field == 13)
                    flags[target * size_t(maximum) + size_t(frame)] = std::uint8_t(selected);
            }
            if (field == 17)
                columns[target].periods = {periods.get() + target * size_t(maximum), size_t(frames)};
            if (field == 13)
                columns[target].tempoFlags = {flags.get() + target * size_t(maximum), size_t(frames)};
        }
        const bool ok =
            bindings.empty() ? phase.beginManual(manual, frames) : phase.begin(manual, frames, columns);
        if (!ok)
        {
            state = Status::invalidBlock;
            return false;
        }
        lanes = bindings;
        beat = firstBeat;
        step = beatsPerFrame;
        count = frames;
        state = Status::ready;
        return true;
    }
    bool readPhases(int frame, SamplerModulationPhases &output) const noexcept
    {
        return phase.view().read(frame, output);
    }
    // All metadata/member pointers were selected on CONTROL. Update active fields only.
    bool select(int frame, PreparedSamplerModulation &prepared, SamplerModulationArray &source) noexcept
    {
        if (state != Status::ready || !phase.pending() || frame < 0 || frame >= count)
        {
            state = Status::staleView;
            return false;
        }
        unsigned clocks = 0;
        for (const auto &row : lanes)
        {
            double selected;
            if (!value(row, beat + frame * step, selected))
            {
                state = Status::invalidCurve;
                return false;
            }
            const auto target = size_t(row.parameter.registryTarget);
            const auto &p = *row.parameter.registryParameter;
            auto apply = [&](SamplerModulation &m) noexcept
            {
                if (p.number)
                    m.*p.number = selected;
                else if (p.flag)
                    m.*p.flag = selected != 0;
                else
                    m.lfoShape = int(selected);
            };
            apply(prepared.settings[target]);
            apply(source[target]);
            const auto field = row.parameter.code % samplerAutomationTargetStride;
            if (field == 13 || field == 17)
                clocks |= 1u << target;
        }
        for (size_t target = 0; target < 5; ++target)
            if (clocks & (1u << target))
            {
                const auto &m = prepared.settings[target];
                prepared.globalIncrements[target] =
                    (m.lfoTempo ? prepared.beatsPerSample : prepared.secondsPerSample) / m.lfoPeriod;
            }
        return true;
    }
    bool finish(bool success) noexcept
    {
        const bool ok = success && state == Status::ready;
        // The admitted clock interval is fully prevalidated. Even an unexpected
        // partial source failure retires that full physical span; never replay its origin.
        // Failure remains explicit and the next valid block can resume automatically.
        const bool result = phase.commit();
        lanes = {};
        count = 0;
        if (ok && result)
            state = Status::idle;
        return ok && result;
    }
};
} // namespace vibera
