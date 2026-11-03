// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once
#include <algorithm>
namespace vibera
{
// One DSP owner; configured before callbacks. No host/native object ownership.
class HostWetMix
{
    double current = 1, target = 1, increment = 0;
    int length = 1, remaining = 0;
    bool initialized = false;

  public:
    void prepare(int frames) noexcept
    {
        length = std::max(1, frames);
        current = target = 1;
        remaining = 0;
        initialized = false;
    }
    // Caller admits finite0..1 before touching any processing state.
    void setTarget(float value) noexcept
    {
        if (!initialized)
        {
            current = target = value;
            remaining = 0;
            initialized = true; // Saved initial value is immediate, like initial bypass.
        }
        else if (target != double(value))
        {
            target = value;
            increment = (target - current) / length;
            remaining = length; // Retarget from actual current value, never copy DSP state.
        }
    }
    float next() noexcept
    {
        if (remaining > 0)
        {
            current = --remaining == 0 ? target : current + increment;
        }
        return float(current);
    }
    float value() const noexcept
    {
        return float(current);
    }
    // Preserve legacy wet==1 bypass arithmetic EXACTLY.
    static float render(float processed, float dry, float wet, float bypass) noexcept
    {
        const float mixed = wet == 1 ? processed : wet == 0 ? dry : dry + (processed - dry) * wet;
        return bypass == 1 ? dry : bypass == 0 ? mixed : mixed + (dry - mixed) * bypass;
    }
};
} // namespace vibera
